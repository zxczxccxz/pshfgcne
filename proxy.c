#include <stdio.h>
#include "csapp.h"

#define MAX_OBJECT_SIZE 7204056

struct Cache {
    char *uri;
    char *headers;
    char *object;
    size_t headersSize;
    size_t objectSize;
};

static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
struct Cache cache = {NULL, NULL, NULL, 0, 0};

void dealWithClient(int);
void read_requesthdrs(rio_t *rp);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);
void setRequestHeaders(char *path, char *host, char* buf);
void initCache();
void clearCache();

int main(int argc, char **argv) {
  int listenfd = 0, connfd = 0;
  char hostname[MAXLINE] = "", port[MAXLINE] = "";
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  Signal(SIGPIPE, SIG_IGN);

  initCache();

  listenfd = Open_listenfd(argv[1]); // Quiting here is ok
  while (1) {
    clientlen = sizeof(clientaddr);

    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
    Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE,
                port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    dealWithClient(connfd);

    Close(connfd);                                            //line:netp:tiny:close
  }
}

void initCache() {
  cache.uri = malloc(MAXLINE);
  cache.headers = malloc(MAX_OBJECT_SIZE);
  cache.object = malloc(MAX_OBJECT_SIZE);
}

void clearCache() {
  free(cache.uri);
  free(cache.headers);
  free(cache.object);
  cache.uri = malloc(MAXLINE);
  cache.headers = malloc(MAX_OBJECT_SIZE);
  cache.object = malloc(MAX_OBJECT_SIZE);
}

void dealWithClient(int fd) {
  int serverFD = 0;
  char httpHeaderBuf[MAXLINE] = "", buf[MAXLINE] = "";
  char method[MAXLINE] = "", uri[MAXLINE] = "", version[MAXLINE] = "";
  char hostname[MAXLINE] = "", port[MAXLINE] = "", path[MAXLINE] = "";
  char uriPortPath[MAXLINE] = "";
  rio_t rio, rio_server;
  size_t bytesRead = 0, headersSize = 0, objectSize = 0, bufSize = 0;
  char *cacheHeaderBuf = malloc(MAX_OBJECT_SIZE), *cacheObjectBuf = malloc(MAX_OBJECT_SIZE);
  char *cacheHeaderBufPtr = cacheHeaderBuf, *cacheObjectBufPtr = cacheObjectBuf;


  /* Read request line and headers */
  rio_readinitb(&rio, fd);
  if (!rio_readlineb(&rio, buf, MAXLINE)) {  //line:netp:doit:readrequest
    return;
  }

  sscanf(buf, "%s %s %s", method, uri, version);

  // Method must be GET
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
    return;
  }

  // Read headers
  read_requesthdrs(&rio);
  // Parse uri
  parse_uri(uri, hostname, port, path);
  snprintf(uriPortPath, sizeof(uriPortPath), "%s:%s%s", uri, port, path);

  // CACHE
  if (strcmp(cache.uri, uriPortPath) == 0) {
    printf("Sending cached object\n");
    Rio_writen(fd, cache.headers, cache.headersSize);
    Rio_writen(fd, cache.object, cache.objectSize);
    printf("Finished sending cached object\n");

    free(cacheHeaderBuf);
    free(cacheObjectBuf);
    return;
  }

  // Make request to server
  serverFD = open_clientfd(hostname, port);
  setRequestHeaders(path, hostname, httpHeaderBuf);
  rio_writen(serverFD, httpHeaderBuf, strlen(httpHeaderBuf));
  rio_readinitb(&rio_server, serverFD);

  // Read in headers
  while((bytesRead = Rio_readlineb(&rio_server, buf, MAXLINE)) != 0)
  {
    headersSize += bytesRead;
    bufSize += bytesRead;

    printf("Bytes sent: %zu\n", bufSize);
    Rio_writen(fd, buf, bytesRead);

    if (bufSize < MAX_OBJECT_SIZE) {
      memcpy(cacheHeaderBufPtr, buf, bytesRead);
      cacheHeaderBufPtr += bytesRead;
    }

    if (strcmp(buf, "\r\n") == 0) {
      break;
    }
  }

  // Read in body
  while((bytesRead = rio_readnb(&rio_server, buf, MAXLINE)) != 0) {
    objectSize += bytesRead;
    bufSize += bytesRead;

    printf("Bytes sent: %zu\n", bufSize);
    Rio_writen(fd, buf, bytesRead);

    if (bufSize < MAX_OBJECT_SIZE) {
      memcpy(cacheObjectBufPtr, buf, bytesRead);
      cacheObjectBufPtr += bytesRead;
    }
  }

  if (bufSize < MAX_OBJECT_SIZE) {
    clearCache();
    strcpy(cache.uri, uriPortPath);
    strcpy(cache.headers, cacheHeaderBuf);
    cache.headersSize = headersSize;
    memcpy(cache.object, cacheObjectBuf, objectSize);
    cache.objectSize = objectSize;
  }
  
  Close(serverFD);

  free(cacheHeaderBuf);
  free(cacheObjectBuf);
}

void setRequestHeaders(char *path, char *host, char* buf) {
  sprintf(buf, "GET %s HTTP/1.0\r\n", path);
  sprintf(buf, "%sHost: %s\r\n", buf, host);
  sprintf(buf, "%sUser-Agent: %s\r\n", buf, user_agent_hdr);

  // Not necessary when sending request to server
  // sprintf(buf, "%sRange: %s\r\n", buf, host);

  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sProxy-Connection: close\r\n", buf);

  sprintf(buf, "%s\r\n", buf);
}

void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  if (!rio_readlineb(rp, buf, MAXLINE))
    return;
  printf("%s", buf);
  while(strcmp(buf, "\r\n")) {          //line:netp:readhdrs:checkterm
    rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

void parse_uri(char *uri, char *hostname, char *port, char *path)
{
  char *startingChar = uri, *endingChar;
  int portInt = 80;
  int portIsSpecified, pathIsSpecified; // Used as booleans

  // Test for transfer protocol, and skip it since we only deal with http
  if (strstr(startingChar, "//") != NULL) {
    startingChar = strstr(startingChar, "//") + 2;
  }

  portIsSpecified = strstr(startingChar, ":") != NULL ? 1 : 0;
  pathIsSpecified = strstr(startingChar, "/") != NULL ? 1 : 0;

  // Port and path are specified
  if (portIsSpecified && pathIsSpecified) {
    endingChar = strstr(startingChar, ":");
    *endingChar = '\0';
    sscanf(startingChar, "%s", hostname);
    startingChar = endingChar + 1;
    sscanf(startingChar, "%d%s", &portInt, path);
  }
  // Port is specified and path is not
  else if (portIsSpecified) {
    *strstr(startingChar, ":") = ' ';
    sscanf(startingChar, "%s %d", hostname, &portInt);
    path = "/\0";
  }
  // Path is specified and port is not
  else if (pathIsSpecified) {
    endingChar = strstr(startingChar, "/");
    *endingChar = '\0';
    sscanf(startingChar, "%s", hostname);
    *endingChar = '/';
    sscanf(endingChar, "%s", path);
  }
  // Neither were specified
  else {
    sscanf(startingChar, "%s", hostname);
    path = "/\0";
  }

  // Set port from portInt
  sprintf(port, "%d", portInt);

  printf("Hostname: %s\nPort: %s\nPath: %s\n", hostname, port, path);
}

void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  /* Build the HTTP response body */
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  /* Print the HTTP response */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  sprintf(buf, "%sContent-type: text/html\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n\r\n", buf, (int)strlen(body));
  rio_writen(fd, buf, strlen(buf));
  rio_writen(fd, body, strlen(body));
}

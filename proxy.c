#include <stdio.h>
#include "csapp.h"

#define MAX_OBJECT_SIZE 7204056
static const char *user_agent_hdr = "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

void dealWithClient(int);
void read_requesthdrs(rio_t *rp);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);
void setRequestHeaders(char *path, char *host, char* buf);

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  Signal(SIGPIPE, SIG_IGN);
  listenfd = Open_listenfd(argv[1]); // Quiting here is ok
  while (1) {
    clientlen = sizeof(clientaddr);
    // TODO: Create own wrapper functions that handler errors
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); //line:netp:tiny:accept
    Getnameinfo((SA *) &clientaddr, clientlen, hostname, MAXLINE,
                port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);

    // TODO: What im currently implementing
    dealWithClient(connfd);

    Close(connfd);                                            //line:netp:tiny:close
  }
}

void dealWithClient(int fd) {
  struct stat sbuf;
  int serverFD;
  char httpHeaderBuf[MAXLINE], buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE];
  rio_t rio, rio_server;

  /* Read request line and headers */
  rio_readinitb(&rio, fd);
  if (!rio_readlineb(&rio, buf, MAXLINE)) {  //line:netp:doit:readrequest
    return;
  }
  // (FINISHED)
  sscanf(buf, "%s %s %s", method, uri, version);
  printf("Method: %s\nURI: %s\nVersion: %s\n", method, uri, version);
  // Method must be GET (FINISHED)
  if (strcasecmp(method, "GET")) {
    clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
    return;
  }
  // Read headers
  read_requesthdrs(&rio);
  // Parse uri
  parse_uri(uri, hostname, port, path);

  // Make request to server
  serverFD = open_clientfd(hostname, port);
  setRequestHeaders(path, hostname, httpHeaderBuf);
  rio_writen(serverFD, httpHeaderBuf, strlen(httpHeaderBuf));
  rio_readinitb(&rio_server, serverFD);





  size_t n;
  while((n=Rio_readlineb(&rio_server,buf,MAXLINE))!=0)
  {
    printf("proxy received %d bytes,then send\n",n);
    Rio_writen(fd,buf,n);
  }
  Close(serverFD);
}

void setRequestHeaders(char *path, char *host, char* buf) {
  sprintf(buf, "GET %s HTTP/1.0\r\n", path);
  sprintf(buf, "%sHost: %s\r\n", buf, host);
  sprintf(buf, "%sUser-Agent: %s\r\n", buf, user_agent_hdr);

  // Not necessary when sending request to server
  // sprintf(buf, "%sRange: %s\r\n", buf, host);

  sprintf(buf, "%sConnection: close\r\n", buf);

  // Idk if necessary when sending to server
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

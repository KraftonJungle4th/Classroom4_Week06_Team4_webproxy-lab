#include <stdio.h>
#include <assert.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

// Tiny 웹 서버 전용 프록시 서버.

/**
 * Request 수신(클라 -> 프록시) + URI Parsing >> Request 전송 (프록시 -> 서버)
 *   - Request 헤더(Host, User-Agent, Connection, Proxy-Connection)
 * Response 수신 (서버 -> 프록시) >> (캐싱) >> Response 전송 (프록시 -> 클라)
 * 
 * 
 * 프록시는 서버이자 클라이언트! => 서버용 소켓 + 클라이언트용 소켓
*/
void doit(int client_acceptfd);
void read_requesthdrs(rio_t *rp, char *request_hdrs) ;
void parse_uri(char *uri, char *hostname, char *port, char *path);
void get_filetype(char *filename, char *filetype);
void add_request_hdrs_if_needed(char *request_header, char *host);
void *isExists(char *request_header, char *header);

/* You won't lose style points for including this long line in your code */
// static const char *user_agent_hdr =
//     "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
  int proxyserver_fd, client_acceptfd, webserver_connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2) {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  proxyserver_fd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    client_acceptfd = Accept(proxyserver_fd, (SA *)&clientaddr, &clientlen); // 클라이언트와 프록시 서버와의 연결
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0);
    doit(client_acceptfd);   
    Close(client_acceptfd);  
  }
}

void doit(int client_acceptfd) // client_acceptfd -> client와의 연결 식별자
{
  char buf[MAXLINE], request_start_line[MAXLINE], request_hdrs[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], path[MAXLINE]; // {host}:{host port}/{resource}
  char method[MAXLINE], uri[MAXLINE]; 
  rio_t rio;
  rio_t rio_for_tiny;

  Rio_readinitb(&rio, client_acceptfd);
  Rio_readlineb(&rio, buf, MAXLINE);
  sscanf(buf, "%s %s", method, uri);
  parse_uri(uri, hostname, port, path);
  // 요청 라인 작성
  sprintf(request_start_line, "%s %s HTTP/1.0", method, path);
  // 헤더 추가!
  read_requesthdrs(&rio, request_hdrs);
  add_request_hdrs_if_needed(request_hdrs, hostname);
  sprintf(buf, "%s", strcat(strcat(request_start_line, "\r\n"), request_hdrs));
  
  // 라인 -> 헤더 -> 바디
  int web_server_fd = Open_clientfd(hostname, port);
  Rio_readinitb(&rio_for_tiny, web_server_fd);
  // proxy -> web server
  Rio_writen(web_server_fd, buf, strlen(buf));
  // web server -> proxy
  while (Rio_readlineb(&rio_for_tiny, buf, MAXLINE) != NULL) {
    // proxy -> client
    Rio_writen(client_acceptfd, buf, strlen(buf));
  }
}

void read_requesthdrs(rio_t *rp, char *request_hdrs) 
{ 
  char buf[MAXLINE];
  char cur_buf[MAXLINE];

  Rio_readlineb(rp, cur_buf, MAXLINE);
  while (strcmp(cur_buf, "\r\n")) {
    // Connection 관련 헤더가 아닐 때 헤더 추가
    // *Connection 관련 헤더는 기본 헤더가 있기 때문!*
    if (!strstr(cur_buf, "Connection: ")) strcat(buf, cur_buf);
    Rio_readlineb(rp, cur_buf, MAXLINE);
  }
  strncpy(request_hdrs, buf, MAXLINE);
}

/**
 * before : http://localhost:8080/cgi-bin/adder?3&5
 * after  : host: localhost:8080, path: /cgi-bin/adder?3&5
*/
void parse_uri(char *uri, char *hostname, char *port, char *path)
{
  char *hostname_ptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri;
  assert(hostname_ptr != NULL);
  char *port_ptr = strchr(hostname_ptr, ':'); 
  char *path_ptr = strchr(hostname_ptr, '/'); 
  strcpy(path, path_ptr);

  if (port_ptr) {
    strncpy(port, port_ptr + 1, path_ptr - port_ptr - 1); 
    strncpy(hostname, hostname_ptr, port_ptr - hostname_ptr); // localhost:8080
  }
  else {
    strcpy(port, "8090"); // port의 기본 값인 80으로 설정
    strncpy(hostname, hostname_ptr, path_ptr - hostname_ptr); // localhost:8080
  }
  if (strlen(hostname) == 0) 
    strcpy(hostname, "localhost");
}

void add_request_hdrs_if_needed(char *request_header, char *host)
{
  // 필요한 헤더가 있는지 확인후 없으면 추가!
  // Host, User-Agent, Connection, Proxy-Connection 헤더들 확인
  char *user_agent_hdr = "Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 Firefox/10.0.3\r\n";
  char *ESSENTIAL_HEADERS[4]         = {"Host: ", "User-Agent: ", "Connection: ", "Proxy-Connection: "};
  char *ESSENTIAL_HEADERS_CONTENT[4] = { host   , user_agent_hdr, "close", "close"};
    
  for (int i = 0; i < 4; i++) {
    if (isExists(request_header, ESSENTIAL_HEADERS[i]) == NULL) {
      assert(strstr(request_header, "\r\n") != NULL);
      char *lastCRLF = strstr(request_header, "\r\n\r\n");
      if (lastCRLF != NULL) {
        int header_length = strlen(request_header);
        request_header[header_length - 2] = '\0';
      }
      sprintf(request_header, "%s%s%s\r\n\r\n", request_header, ESSENTIAL_HEADERS[i], ESSENTIAL_HEADERS_CONTENT[i]);
    }
  }
}

void *isExists(char *request_header, char *header)
{ 
  return strstr(request_header, header);
}
#include <stdio.h>
#include <signal.h>

#include "csapp.h"
#include "cache.h"

void *thread(void *vargp);
void doit(int clientfd);
void read_requesthdrs(rio_t *request_rio, char *request_buf, int serverfd, char *hostname, char *port);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);
void parse_uri(char *uri, char *hostname, char *port, char *path);

void read_request_line(rio_t *request_rio, int clientfd, char *request_buf);
void parse_request_line(char *request_buf, char *method, char *uri);

/* You won't lose style points for including this long line in your code */
static const int is_local_test = 0;
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
  int listenfd, *clientfd;
  char client_hostname[MAXLINE], client_port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  pthread_t tid;
  signal(SIGPIPE, SIG_IGN);

  rootp = (web_object_t *)calloc(1, sizeof(web_object_t));
  lastp = (web_object_t *)calloc(1, sizeof(web_object_t));

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    clientfd = (int *)Malloc(sizeof(int));
    *clientfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    Getnameinfo((SA *)&clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
    printf("# Accepted connection from %s:%s\n", client_hostname, client_port);
    Pthread_create(&tid, NULL, thread, clientfd);
  }
}

void *thread(void *vargp)
{
  int clientfd = *((int *)vargp);
  Pthread_detach(pthread_self());
  Free(vargp);
  doit(clientfd);
  Close(clientfd);
  return NULL;
}

void doit(int clientfd)
{
  int serverfd, content_length;
  char request_buf[MAXLINE], response_buf[MAXLINE];
  char method[MAXLINE], uri[MAXLINE], path[MAXLINE], hostname[MAXLINE], port[MAXLINE];
  char *response_ptr, filename[MAXLINE], cgiargs[MAXLINE];
  rio_t request_rio, response_rio;

  // Request Line 읽기 (Client -> Proxy)
  Rio_readinitb(&request_rio, clientfd);
  Rio_readlineb(&request_rio, request_buf, MAXLINE);
  printf("# Request Line from Client:\n %s\n", request_buf);

  // Request Line 파싱을 통해 method, URI 추출
  sscanf(request_buf, "%s %s", method, uri);
  printf("method: %s, uri: %s\n", method, uri);

  // URI를 hostname, port, path로 파싱
  parse_uri(uri, hostname, port, path);

  // 서버로 전송하기 위해 Request Line의 형식 변경: 'METHOD URI VERSON' -> 'METHOD PATH HTTP/1.0'
  sprintf(request_buf, "%s %s HTTP/1.0\r\n", method, path);

  // // 지원하지 않는 메소드일 경우 에러 메시지 전송
  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD"))
  {
    clienterror(clientfd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }

  // 현재 요청이 캐싱된 요청(path)인지 확인
  web_object_t *cached_object = find_cache(path);
  if (cached_object)
  {
    send_cache(cached_object, clientfd); // 캐싱된 객체를 Client에 전송
    read_cache(cached_object);           // 사용한 웹 객체의 순서를 맨 앞으로 갱신
    return;                              // Server로 요청을 보내지 않고 통신 종료
  }

  // Request Line 전송 (Proxy -> Server)
  // 서버 소켓 생성
  serverfd = is_local_test ? Open_clientfd("127.0.0.1", port) : Open_clientfd(hostname, port);
  if (serverfd < 0)
  {
    clienterror(serverfd, method, "502", "Bad Gateway", "Tiny couldn't connect to the server");
    return;
  }
  Rio_writen(serverfd, request_buf, strlen(request_buf));
  printf("# Request Line to Server:\n %s\n", request_buf);

  // Request Header 읽기 & 전송 (Client -> Proxy -> Server)
  read_requesthdrs(&request_rio, request_buf, serverfd, hostname, port);

  // Response Header 읽기 & 전송 (Server -> Proxy -> Client)
  Rio_readinitb(&response_rio, serverfd);
  printf("# Response Header from Server & to Client:\n");
  while (strcmp(response_buf, "\r\n"))
  {
    Rio_readlineb(&response_rio, response_buf, MAXLINE);
    printf(" %s", response_buf);
    if (strstr(response_buf, "Content-length")) // Response Body의 길이 추출
      content_length = atoi(strchr(response_buf, ':') + 1);
    Rio_writen(clientfd, response_buf, strlen(response_buf));
  }

  // Response Body 읽기 & 전송 (Server -> Proxy -> Client)
  printf("# Response Body from Server & to Client:\n");
  response_ptr = (char *)Malloc(content_length);
  Rio_readnb(&response_rio, response_ptr, content_length);
  Rio_writen(clientfd, response_ptr, content_length); // Client에게 Response Body 전송
  printf(" %s\n", response_ptr);

  if (content_length <= MAX_OBJECT_SIZE) // 캐싱 가능한 크기인 경우
  {
    // `web_object` 구조체 생성
    web_object_t *web_object = (web_object_t *)malloc(sizeof(web_object_t));
    strcpy(web_object->path, path);
    web_object->content_length = content_length;
    web_object->response_ptr = response_ptr;
    web_object->prev = NULL;
    web_object->next = NULL;
    write_cache(web_object); // 캐시 연결 리스트에 추가
  }
  else
    free(response_ptr); // 캐싱하지 않은 경우만 메모리 반환

  Close(serverfd);
}

/*
 * clienterror - 클라이언트에게 에러 메시지 전송
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE], body[MAXBUF];

  // Build the HTTP response body
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor="
                "ffffff"
                ">\r\n",
          body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web server</em>\r\n", body);

  // Print the HTTP response
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  // 에러 Body 전송
  Rio_writen(fd, body, strlen(body));
}

/*
 * parse_uri - URI를 hostname, port, path로 파싱
 * URI 형태: 'http://hostname:port/path' or 'http://hostname/path' (default port: 80)
 */
void parse_uri(char *uri, char *hostname, char *port, char *path)
{
  char *ptr;
  char *port_ptr;

  if (strstr(uri, "http://"))
  {
    uri += 7; // 'http://' 제거
  }

  if ((ptr = strchr(uri, '/')) != NULL)
  {
    strcpy(path, ptr);
    *ptr = '\0';
  }
  else
  {
    strcpy(path, "/");
  }

  if ((port_ptr = strchr(uri, ':')) != NULL)
  {
    *port_ptr = '\0';
    strcpy(hostname, uri);
    strcpy(port, port_ptr + 1);
  }
  else
  {
    strcpy(hostname, uri);
    if (is_local_test)
      strcpy(port, "8080");
    else
      strcpy(port, "80");
  }
  // printf("# Parsed -> hostname: %s, port: %s, path: %s\n", hostname, port, path);
}

/*
 * read_requesthdrs - Request Header를 읽고 서버로 전송
 */
void read_requesthdrs(rio_t *request_rio, char *request_buf, int serverfd, char *hostname, char *port)
{
  int is_host_exist;
  int is_connection_exist;
  int is_proxy_connection_exist;
  int is_user_agent_exist;

  Rio_readlineb(request_rio, request_buf, MAXLINE); // 첫번째 줄 읽기
  printf("# Request Header from Client & to Server:\n");
  while (strcmp(request_buf, "\r\n"))
  {
    if (strstr(request_buf, "Proxy-Connection")) // Keep-Alive -> Close
    {
      sprintf(request_buf, "Proxy-Connection: close\r\n");
      is_proxy_connection_exist = 1;
    }
    else if (strstr(request_buf, "Connection"))
    {
      sprintf(request_buf, "Connection: close\r\n");
      is_connection_exist = 1;
    }
    else if (strstr(request_buf, "User-Agent")) // 이건 왜 하드코딩된걸로 바꾸어줄까?
    {
      sprintf(request_buf, user_agent_hdr);
      is_user_agent_exist = 1;
    }
    else if (strstr(request_buf, "Host"))
    {
      is_host_exist = 1;
    }

    Rio_writen(serverfd, request_buf, strlen(request_buf)); // Server에 전송
    printf(" %s", request_buf);
    Rio_readlineb(request_rio, request_buf, MAXLINE); // 다음 줄 읽기
  }

  // // 필수 헤더 미포함 시 추가로 전송
  // if (!is_proxy_connection_exist)
  // {
  //     sprintf(request_buf, "Proxy-Connection: Close\r\n");
  //     Rio_writen(serverfd, request_buf, strlen(request_buf));
  // }
  // if (!is_connection_exist)
  // {
  //     sprintf(request_buf, "Connection: close\r\n");
  //     Rio_writen(serverfd, request_buf, strlen(request_buf));
  // }
  // if (!is_host_exist)
  // {
  //     // if (!is_local_test)
  //     //   hostname = "52.79.234.188";
  //     sprintf(request_buf, "Host: %s:%s\r\n", hostname, port);
  //     Rio_writen(serverfd, request_buf, strlen(request_buf));
  // }
  // if (!is_user_agent_exist)
  // {
  //     sprintf(request_buf, user_agent_hdr);
  //     Rio_writen(serverfd, request_buf, strlen(request_buf));
  // }

  sprintf(request_buf, "\r\n"); // 종료문
  Rio_writen(serverfd, request_buf, strlen(request_buf));

  return;
}
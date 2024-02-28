#include <stdio.h>
#include <signal.h>

#include "csapp.h"

void doit(int fd);
void *thread(void *vargp);
void read_requesthdrs(rio_t *request_rio, void *request_buf, int serverfd, char *hostname, char *port);
void parse_uri(char *uri, char *hostname, char *port, char *path);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);

static const int is_local_test = 1;
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv)
{
  int listenfd, *connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;
  pthread_t tid; // long int형 pthread
  signal(SIGPIPE, SIG_IGN); // SIGPIPE 예외처리


  if (argc != 2)  //인수 개수 안맞는 경우
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // 전달받은 포트 번호를 사용해 수신 소켓 생성, fd 반환
  while (1)
  {
    clientlen = sizeof(clientaddr); //clientaddr 구조체 변수 크기 만큼
    connfd = Malloc(sizeof(int));

    // 클라이언트 연결 요청 수신
    // 연결된 클라이언트와 통신하기 위한 새로운 fd 반환하여 connfd에 저장
    *connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

    //클라이언트의 소켓 주소에서 호스트 이름과 포트 번호 추출
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    Pthread_create(&tid, NULL, thread, connfd);
    // &tid : 생성된 스레드의 식별자를 저장, 스레드의 속성을 지정(NULL은 기본속성),
    // thread : 스레드가 실행할 함수의 포인터로, 새로운 스레드는 이 함수를 시작점으로 실행한다.
    // connfd : 스레드 함수에 전달 인자로 client와의 연결을 나타내는 파일 디스크립터를 전달.
  }
}

void *thread(void *vargp)
{
  int clientfd = *((int *)vargp); // client와 연결을 나타내는 파일 디스크립터
  Pthread_detach(pthread_self()); // 현재 스레드를 분리한다. -> 스레드 종료시 자원 자동 회수
  Free(vargp);                    // pthread_create 함수에서 동적으로 할당한 메모리 해제
  doit(clientfd);                 // clientfd를 인자로 전달해 해당 client와 통신 처리
  Close(clientfd);
  return NULL;
}

void doit(int fd)
{
  int serverfd, content_length; // server 파일 디스크립터와 body를 구분할 content_length
  char request_buf[MAXLINE], response_buf[MAXLINE];
  char hostname[MAXLINE], port[MAXLINE], method[MAXLINE], uri[MAXLINE], path[MAXLINE], *response_ptr;
  rio_t response_rio, request_rio;

  Rio_readinitb(&request_rio, fd);
  Rio_readlineb(&request_rio, request_buf, MAXLINE);  //클라이언트로부터 받은 요청 헤더를 request_buf에 저장
  printf("Request Headers : %s\n", request_buf);

  sscanf(request_buf, "%s %s", method, uri);
  parse_uri(uri, hostname, port, path); //uri에서 hostname, port, path 분리

  // Server에 전송하기 위해 요청 라인의 형식 변경: `method uri version` -> `method path HTTP/1.0`
  sprintf(request_buf, "%s %s %s\r\n", method, path, "HTTP/1.0");

  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD"))
  { 
    clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
    return;
  }

  // Server 소켓 생성
  serverfd = is_local_test ? Open_clientfd(hostname, port) : Open_clientfd("125.209.222.141", port);
  if (serverfd < 0)
  {
    clienterror(serverfd, method, "502", "Bad Gateway", "📍 Failed to establish connection with the end server");
    return;
  }
  Rio_writen(serverfd, request_buf, strlen(request_buf)); //request_buf를 서버 소켓에 쓰고 요청 헤더를 전송

  // Request Header 읽기/전송 (Client ->  Proxy ->  Server)
  // 클라이언트로부터 받은 요청 헤더를 읽고 서버에 전송
  read_requesthdrs(&request_rio, request_buf, serverfd, hostname, port);

  // Response Header 읽기/전송 (Server ->  Proxy -> Client)
  // 서버로부터 받은 응답 헤더를 읽고 클라이언트에 전송
  Rio_readinitb(&response_rio, serverfd);

  while (strcmp(response_buf, "\r\n"))
  {
    Rio_readlineb(&response_rio, response_buf, MAXLINE);

    // Response Body 수신에 사용하기 위해 
    // 응답 헤더에서 content-length를 찾아서 Content-length 저장
    if (strstr(response_buf, "Content-length")) 
      content_length = atoi(strchr(response_buf, ':') + 1);
    Rio_writen(fd, response_buf, strlen(response_buf));
  }

  // Response Body 읽기 & 전송 (Server -> Proxy -> Client)
  response_ptr = malloc(content_length);  // malloc으로 cotent_length 크기 메모리를 동적 할당
  Rio_readnb(&response_rio, response_ptr, content_length);  //서버로부터 받은 응답 body를 읽어 response_ptr에 저장
  Rio_writen(fd, response_ptr, content_length); // Client에 Response Body 전송

  Close(serverfd);
}

// Request Header를 읽고 Server에 전송
// 필수 헤더가 없는 경우에는 필수 헤더를 추가로 전송
void read_requesthdrs(rio_t *request_rio, void *request_buf, int serverfd, char *hostname, char *port)
{
  int is_host_exist;
  int is_connection_exist;
  int is_proxy_connection_exist;
  int is_user_agent_exist;

  Rio_readlineb(request_rio, request_buf, MAXLINE); // 첫번째 줄 읽기
  while (strcmp(request_buf, "\r\n"))
  {
    if (strstr(request_buf, "Proxy-Connection") != NULL)  // 헤더에 Proxy-Connection 포함
    {
      sprintf(request_buf, "Proxy-Connection: close\r\n");
      is_proxy_connection_exist = 1;
    }
    else if (strstr(request_buf, "Connection") != NULL) // 헤더에 Connection 포함
    {
      sprintf(request_buf, "Connection: close\r\n");
      is_connection_exist = 1;
    }
    else if (strstr(request_buf, "User-Agent") != NULL) // Header에 User-Agent 포함
    {
      sprintf(request_buf, user_agent_hdr);
      is_user_agent_exist = 1;
    }
    else if (strstr(request_buf, "Host") != NULL) // Header에 Host 포함
    {
      is_host_exist = 1;
    }

    Rio_writen(serverfd, request_buf, strlen(request_buf)); // Server에 전송
    Rio_readlineb(request_rio, request_buf, MAXLINE);       // 다음 줄 읽기
  }

  sprintf(request_buf, "\r\n"); // 종료문
  Rio_writen(serverfd, request_buf, strlen(request_buf));
  return;
}

// uri를 'hostname', 'port', 'path'로 파싱하는 함수
// uri 형태: 'http://hostname:port/path' 혹은 'http://hostname/path' (port는 optional)
void parse_uri(char *uri, char *hostname, char *port, char *path)
{
  // host_name의 시작 위치 포인터: '//'가 있으면 //뒤(ptr+2)부터, 없으면 uri 처음부터
  char *hostname_ptr = strstr(uri, "//") ? strstr(uri, "//") + 2 : uri;
  char *port_ptr = strchr(hostname_ptr, ':'); // port 시작 위치 (없으면 NULL)
  char *path_ptr = strchr(hostname_ptr, '/'); // path 시작 위치 (없으면 NULL)
  strcpy(path, path_ptr);

  if (port_ptr) // port 있는 경우
  {
    strncpy(port, port_ptr + 1, path_ptr - port_ptr - 1);
    strncpy(hostname, hostname_ptr, port_ptr - hostname_ptr);
  }
  else // port 없는 경우
  {
    if (is_local_test)
      strcpy(port, "80"); // port의 기본 값인 80으로 설정
    else
      strcpy(port, "4004");
    strncpy(hostname, hostname_ptr, path_ptr - hostname_ptr);
  }
}

// 클라이언트에 에러 전송
// cause: 오류 원인, errnum: 오류 번호, shortmsg: 짧은 오류 메시지, longmsg: 긴 오류 메시지
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
  char buf[MAXLINE];

  // HTTP response headers
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n\r\n");
  Rio_writen(fd, buf, strlen(buf));

  // HTTP response body
  sprintf(buf, "<html><title>Tiny Error</title>");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "<body bgcolor="
               "ffffff"
               ">\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "%s: %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "<p>%s: %s\r\n", longmsg, cause);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "<hr><em>The Tiny Web server</em>\r\n");
  Rio_writen(fd, buf, strlen(buf));
}
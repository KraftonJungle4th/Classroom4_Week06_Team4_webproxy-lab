/*
 * <tiny.c>
 * GET 메서드를 사용하여 정적 및 동적 컨텐츠를 제공하는 간단한 Iterative HTTP/1.0 웹 서버
 * HEAD 메서드도 사용 가능
 *
 * < HTTP Request Example >
 * GET /godzilla.gif HTTP/1.1                                                     - Request Line
 * Host: localhost:8080                                                           ]
 * Connection: keep-alive                                                         ]
 * sec-ch-ua: "Chromium";v="122", "Not(A:Brand";v="24", "Microsoft Edge";v="122"  ]
 * sec-ch-ua-mobile: ?0                                                           ]
 * User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36       ]
 *  (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36 Edg/122.0.0.0              ]
 * sec-ch-ua-platform: "Windows"                                                  ] HTTP Header
 * Accept: image/avif,image/webp,image/apng,image/svg+xml,image/*,/*;q=0.8        ]
 * Sec-Fetch-Site: same-origin                                                    ]
 * Sec-Fetch-Mode: no-cors                                                        ]
 * Sec-Fetch-Dest: image                                                          ]
 * Referer: http://localhost:8080/                                                ]
 * Accept-Encoding: gzip, deflate, br                                             ]
 * Accept-Language: ko,en;q=0.9,en-US;q=0.8                                       ]
 *
 *                                                                                ]
 *                                                                                ] HTTP Body
 *                                                                                ]
 *
 * < C Library Functions >
 * sprintf(a, b) : b 문자열을 생성하여 a에 저장한다.
 * sscanf(a, b, c) : a 문자열을 읽어들여서 b 형식으로 변환하여 c에 저장한다.
 * strcpy(a, b) : b 문자열을 a에 복사한다.
 * strcat(a, b) : b 문자열을 a에 이어붙인다.
 * index(a, b) : a 문자열에서 b 문자열을 찾아서 그 문자열이 시작하는 주소를 반환하거나, 찾지 못하면 NULL을 반환한다.
 * setenv(a, b, c) : a 환경 변수를 b로 설정한다. c가 1이면, 이미 존재하는 환경 변수를 덮어쓰고, 0이면 존재하는 환경 변수를 덮어쓰지 않는다.
 * dup2(a, b) : a 파일 디스크럽터를 b 파일 디스크럽터로 복사한다.
 * execve(a, b, c) : a 프로그램을 로드하고 실행한다. b는 명령행 인수를, c는 환경 변수를 나타낸다.
 *
 * < Robust Input/Output Functions > (fd = file descriptor, rp = rio_t pointer, b = buffer)
 * # Unbuffered
 * Rio_readn(fd, b, c) : fd에서 c 바이트만큼 읽어들여서 b에 저장한다.
 * Rio_writen(fd, b, c) : fd에 b를 c 바이트만큼 쓴다.
 *
 * # Buffered
 * Rio_readinitb(rp, fd) : rp를 fd로 초기화한다.
 * Rio_readlineb(rp, b, c) : rp에서 한 줄 단위로 입력을 받는데, 최대 c 바이트만큼 읽어들여서 b에 저장한다.
 * Rio_readbn(rp, b, c) : rp에서 c 바이트만큼 읽어들여서 b에 저장한다.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "../csapp.h"

// 함수 프로토타입
void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg);

int main(int argc, char **argv)
{
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]); // 듣기 소켓을 연다.
  while (1)                          // 무한 루프
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);                       // 연결 요청을 반복적으로 수락
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE, 0); // 클라이언트의 호스트 이름과 포트 번호를 찾는다.
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // 트랜잭션 수행
    Close(connfd); // 연결 종료
  }
}

/*
 * doit - 하나의 HTTP Request/Response 트랜잭션을 처리.
 */
void doit(int fd)
{
  int is_static;
  struct stat sbuf;
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  Rio_readinitb(&rio, fd);
  if (!Rio_readlineb(&rio, buf, MAXLINE)) // 요청 라인을 읽기
    return;
  printf("%s", buf);
  /* e.g.
   * buf에 저장된 요청 라인인 "GET /godzilla.gif HTTP/1.1"을 읽어들여서
   * method = "GET",
   * uri = "/godzilla.gif",
   * version = "HTTP/1.1" 로 변환하여 저장한다.
   */
  sscanf(buf, "%s %s %s", method, uri, version); // 요청 라인을 파싱

  if (strcasecmp(method, "GET") && strcasecmp(method, "HEAD")) // 숙제 문제 11.11 : HEAD 메서드를 지원
  {                                                            // 지원하는 메소드 이외의 다른 메서드를 요청하면 오류 메시지를 보냄.
    clienterror(fd, method, "501", "Not Implemented", "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio); // 요청 헤더를 읽고 무시함.

  // 요청의 URI를 파일 이름과 CGI 매개변수로 파싱하고, 요청이 정적 컨텐츠인지, 동적 컨텐츠인지를 나타내는 플래그를 설정한다.
  is_static = parse_uri(uri, filename, cgiargs); // line:netp:doit:staticcheck
  // 파일이 존재하지 않으면 오류 메시지를 보냄.
  if (stat(filename, &sbuf) < 0)
  { // line:netp:doit:beginnotfound
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  } // line:netp:doit:endnotfound

  if (is_static)
  { /* 정적 컨텐츠 제공
     * 파일이 일반 파일인지, 읽기 권한이 있는지 확인
     * S_ISREG : 일반 파일인지 확인, S_IRUSR : 사용자 읽기 권한 확인
     */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    { // line:netp:doit:readable
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size); // line:netp:doit:servestatic
  }
  else
  { /* 동적 컨텐츠 제공 */
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    { // line:netp:doit:executable
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs); // line:netp:doit:servedynamic
  }
}

/*
 * read_requesthdrs - HTTP 요청 헤더를 사용하지 않기에 단순히 읽고 무시한다.
 */
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  printf("%s", buf);
  while (strcmp(buf, "\r\n"))
  {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

/*
 * parse_uri - URI를 파일 이름과 CGI 매개변수로 분석
 * 동적 컨텐츠의 경우 0, 정적 컨텐츠의 경우 1을 반환
 *
 * TINY는 정적 컨텐츠의 홈 디렉토리가 현재 디렉토리(.)이고, 동적 컨텐츠의 홈 디렉토리가 ./cgi-bin라고 가정한다.
 * 또한 기본 파일의 이름은 ./home.html이다.
 *
 * 요청이 정적 컨텐츠의 경우, CGI 매개변수 문자열을 지우고, (clearcgi)
 * URI를 상대적인 리눅스 경로로 변환한다. (beginconvert1, endconvert1)
 * URI가 /로 끝나면, home.html을 추가한다. (slashcheck, appenddefault)
 *
 * 요청이 동적 컨텐츠의 경우, CGI 매개변수를 추출하고, (beginextract)
 * URI의 나머지 부분을 상대적인 리눅스 파일 이름으로 변환한다. (beginconvert2)
 *
 * 가령 URI가 /cgi-bin/adder?n1=37&n2=48라면, cgiargs에 "n1=37&n2=48"을 저장하고, filename에 ./cgi-bin/adder를 저장한다.
 *
 */
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  if (!strstr(uri, "cgi-bin"))
  { /* 정적 컨텐츠 */                // line:netp:parseuri:isstatic
    strcpy(cgiargs, "");             // line:netp:parseuri:clearcgi
    strcpy(filename, ".");           // line:netp:parseuri:beginconvert1
    strcat(filename, uri);           // line:netp:parseuri:endconvert1
    if (uri[strlen(uri) - 1] == '/') // line:netp:parseuri:slashcheck
      strcat(filename, "home.html"); // line:netp:parseuri:appenddefault
    return 1;
  }
  else
  { /* 동적 컨텐츠 */      // line:netp:parseuri:isdynamic
    ptr = index(uri, '?'); // line:netp:parseuri:beginextract
    if (ptr)
    {
      strcpy(cgiargs, ptr + 1);
      *ptr = '\0'; // filename만 남기고 나머지는 버림
    }
    else
      strcpy(cgiargs, ""); // line:netp:parseuri:endextract
    strcpy(filename, "."); // line:netp:parseuri:beginconvert2
    strcat(filename, uri); // line:netp:parseuri:endconvert2
    return 0;
  }
}

/*
 * serve_static - 파일을 클라이언트에 다시 복사한다.
 * TINY는 여섯 가지 일반적인 유형의 정적 컨텐츠를 제공한다: HTML, 서식없는 텍스트, GIF, JPEG, PNG, MP4
 */
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  get_filetype(filename, filetype);    // 파일 이름에서 파일 타입 결정

  /* Response Line과 Response Header를 클라이언트에게 보낸다. */
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); // Response Line
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n"); // Response Header
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n", filesize);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: %s\r\n\r\n", filetype); // Response Header + Empty Line
  Rio_writen(fd, buf, strlen(buf));

  /* Response Body를 클라이언트에게 보낸다 */
  srcfd = Open(filename, O_RDONLY, 0);                        // 읽을 파일을 열고 파일 디스크럽터를 얻는다.
  // srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // mmap을 사용하여 요청된 파일을 VM 영역에 매핑
  srcp = (char *)malloc(filesize);                            // 숙제 문제 11.9: mmap 대신 malloc을 사용하여 메모리를 할당하고,
  Rio_readn(srcfd, srcp, filesize);                           // 숙제 문제 11.9: 파일을 읽어서 srcp에 저장
  Close(srcfd);                                               // 더 이상 파일 디스크럽터가 필요하지 않으므로 파일을 닫는다
  Rio_writen(fd, srcp, filesize);                             // 클라이언트로 파일 전송을 수행.
  free(srcp);                                                 // 숙제 문제 11.9: malloc을 사용하였기에 Munmap 대신 free를 사용
  // Munmap(srcp, filesize);                                     // 매핑된 VM 영역을 해제한다.
}

/*
 * get_filetype - 파일 이름에서 파일 타입을 결정한다.
 */
void get_filetype(char *filename, char *filetype)
{
  if (strstr(filename, ".html"))
    strcpy(filetype, "text/html");
  else if (strstr(filename, ".gif"))
    strcpy(filetype, "image/gif");
  else if (strstr(filename, ".png"))
    strcpy(filetype, "image/png");
  else if (strstr(filename, ".jpg"))
    strcpy(filetype, "image/jpeg");
  else if (strstr(filename, ".mp4")) // 숙제 문제 11.7: TINY를 확장해서 MPG(MP4) 비디오 파일을 처리
    strcpy(filetype, "video/mp4");
  else
    strcpy(filetype, "text/plain");
}

/*
 * serve_dynamic - 클라이언트를 대신하여 CGI 프로그램을 실행하고, Response Line과 Response Header를 보낸다.
 */
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = {NULL};

  // Response Line과 Response Header를 클라이언트에게 보낸다.
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0)                        // 새 자식 프로세스를 포크
  {                                       // Child. 실제 서버는 모든 CGI 변수를 여기에 설정한다
    setenv("QUERY_STRING", cgiargs, 1);   // 자식은 요청 URI의 CGI 매개변수를 사용하여 QUERY_STRING 환경 변수를 초기화한다.
    Dup2(fd, STDOUT_FILENO);              // dup2를 사용하여 자식의 stdout을 클라이언트와 연결된 파일 디스크립터로 리디렉션한다.
    Execve(filename, emptylist, environ); // 자식이 execve를 호출하여 CGI 프로그램을 로드하고 실행한다. CGI 프로그램이 표준 출력에 쓰는 모든 내용은 클라이언트 프로세스로 직접 이동한다.
  }
  Wait(NULL); // 부모는 wait 호출을 block하여 자식이 종료될 때까지 기다린다.
}

/*
 * clienterror - Request Line에 적절한 상태 코드와 상태 메시지가 포함된 HTTP 응답을 클라이언트에게 보내고
 * 응답 본문에는 사용자에게 오류를 설명하는 HTML 파일을 포함한다.
 */
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg)
{
  char buf[MAXLINE];

  /* HTTP Response Headers 출력 */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n\r\n");
  Rio_writen(fd, buf, strlen(buf));

  /* HTTP Response Body 출력 */
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
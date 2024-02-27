#include "csapp.h"

void doit(int fd);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char *filename, char *cgiargs);
void serve_static(int fd, char *filename, int filesize);
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg);

/*
TINY는 반복실행 서버로 명령줄에서 넘겨받은 포트로의 연결 요청을 듣는다.
open_listenfd 함수를 호출해서 듣기 소켓을 오픈한 후 서버(무한루프) 실행하고
반복적으로 연결 요청을 접수하고 트랙잭션을 수행한 뒤 자신 쪽의 연결 끝을 닫는다.
*/
int main(int argc, char **argv)
{
  int listenfd, connfd; // 듣기 식별자, 연결 식별자 선언
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen); // 반복적으로 연결 요청 접수
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE,
                port, MAXLINE, 0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

/*
한 개의 HTTP 트랜잭션을 처리한다. 먼저 요청 라인을 읽고 분석한다.
포트로의 연결 요청을 듣고 open_listenfd 함수를 호출해서 듣기 소켓을 오픈한다.
서버는 반복적으로 연결 요청을 접수하고 트랙잭션을 수행한 뒤 자신 쪽의 연결 끝을 닫는다.
*/
void doit(int fd)
{
  int is_static;
  struct stat sbuf;

  // 여러가지 정보를 담을 char 변수 선언
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char filename[MAXLINE], cgiargs[MAXLINE];
  rio_t rio;

  /* Read request line and headers */
  Rio_readinitb(&rio, fd);                // &rio 주소를 가지는 읽기 버퍼를 만들고 초기화 (rio_t 구조체 초기화)
  if (!Rio_readlineb(&rio, buf, MAXLINE)) // 버퍼에서 읽은 것이 담겨있다.
                                          // rio_readlineb 함수로 요청 라인을 읽어 들인다
    return;

  printf("Request headers : \n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version); // 버퍼에서 자료형을 읽어 요청라인을 분석한다.

  // TINY는 GET 메소드만 지원한다. 만일 클라이언트가 다른 메소드(POST 같은)를 요청하면, 에러 메시지를 보내고
  // main 루틴으로 돌아오고 그 후에 연결을 닫고 다음 연결 요청을 기다린다.
  // 그렇지 않으면 밑에 read_requesthdrs에서 읽어들이고 다른 요청 헤더들은 무시한다.
  if (strcasecmp(method, "GET"))
  {
    clienterror(fd, method, "501", "Not Implemented",
                "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);

  // uri를 분석한다. 파일이 없는 경우 에러를 띄운다.
  // parse_uri를 들어가기 전에 filename과 cgiargs는 들어있지 않음.
  // 이 URI를 CGI 인자 스트링으로 분석하고 요청이 정적 또는 동적 컨텐츠인지 flag를 설정한다.
  is_static = parse_uri(uri, filename, cgiargs);

  // 파일이 디스크 상에 있지 않으면, 에러 메시지를 즉시 클라이언트에게 보내고 리턴한다.
  if (stat(filename, &sbuf) < 0)
  { // line:netp:doit:beginnotfound
    clienterror(fd, filename, "404", "Not found",
                "Tiny couldn't find this file");
    return;
  } // line:netp:doit:endnotfound

  // 정적 콘텐츠
  if (is_static)
  {
    // 파일 읽기 권한 확인
    // S_ISREG : 일반 파일인지? , S_IRUSR: 읽기 권한이 있는지? S_IXUSR 실행권한이 있는지?
    // 우리는 이 파일이 보통 파일이라는 것과 읽기 권한을 가지고 있는지를 검증한다.
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode))
    {
      // 권한이 없다면 클라이언트에게 에러를 전달
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't read the file");
      return;
    }
    // 클라이언트에게 파일 제공
    serve_static(fd, filename, sbuf.st_size); // line:netp:doit:servestatic
  }
  else
  { // 파일이 실행 가능한지 확인
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode))
    { // 실행 불가능하면 에러 전달
      clienterror(fd, filename, "403", "Forbidden",
                  "Tiny couldn't run the CGI program");
      return;
    }
    // 클라이언트에게 파일 제공
    serve_dynamic(fd, filename, cgiargs);
  }
}
/* $end doit */

/* $begin read_requesthdrs */
void read_requesthdrs(rio_t *rp)
{
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);
  printf("%s", buf);
  while (strcmp(buf, "\r\n"))
  { // line:netp:readhdrs:checkterm
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}
/* $end read_requesthdrs */

// TINY는 정적 컨텐츠를 위한 홈 디렉토리가 자신의 현재 디렉토리고
// 실행파일의 홈 디렉토리는 /cgi-bin이라고 가정한다. 스트링 cgi-bin을 포함하는
// 모든 URI는 동적 컨텐츠를 요청하는 것을 나타낸다고 가정한다. 기본 파일 이름은 ./home.html이다
int parse_uri(char *uri, char *filename, char *cgiargs)
{
  char *ptr;

  // URI를 파일 이름과 옵션으로 CGI 인자 스트링을 분석한다.
  // cgi-bin이 없다면 (즉 정적 컨텐츠를 위한 것이라면)
  if (!strstr(uri, "cgi-bin"))
  { /* Static content */             // line:netp:parseuri:isstatic
    strcpy(cgiargs, "");             //cgi 인자 스트링을 지운다.
    strcpy(filename, ".");           // line:netp:parseuri:beginconvert1
    strcat(filename, uri);           // ./home.html 이 된다 (상대 리눅스 경로 이름으로 변환함)

    // 만일 URI가 '/' 문자로 끝난다면
    if (uri[strlen(uri) - 1] == '/') // line:netp:parseuri:slashcheck
      strcat(filename, "home.html"); // 기본 파일 이름을 추가한다.
    return 1;
  }
  else  // 만약 동적 컨텐츠를 위한 것이라면
  {
    ptr = index(uri, '?'); // line:netp:parseuri:beginextract
    if (ptr)  // 모든 CGI인자 추출
    {
      strcpy(cgiargs, ptr + 1); // 물음표 뒤에 있는 인자 다 갖다 붙인다.
      *ptr = '\0';  //포인터를 문자열의 마지막으로 변경
    }
    else
      strcpy(cgiargs, ""); // 물음표 뒤 인자들 전부 넣기
    strcpy(filename, "."); // 나머지 부분 상대 리눅스 uri로 바꿈,
    strcat(filename, uri); // ./uri 가 된다.
    return 0;
  }
}
/* $end parse_uri */

// TINY는 다섯개의 서로 다른 정적 컨텐츠 타입을 지원 한다.
// TML 파일, 무형식 텍스트 파일, GIF, PNG, JPEG로 인코딩된 영상
// 이 함수는 지역 파일의 내용을 포함하고 있는 본체를 갖는 HTTP응답을 보낸다.
void serve_static(int fd, char *filename, int filesize)
{
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  /* Send response headers to client */
  // 접미어를 통해 파일 타입 결정
  get_filetype(filename, filetype);    // 클라이언트에게 응답 줄과 응답 헤더를 보낸다.
  sprintf(buf, "HTTP/1.0 200 OK\r\n"); // 데이터를 클라이언트로 보내기 전, 버퍼에 임시로 가지고 있는다.
  
  // rio_readn은 fd의 현재 파일 위치에서 메모리 위치 usrbuf로 최대 n바이트를 전송한다.
  // rio_writen은 usrfd에서 식별자 fd로 n바이트를 전송한다.
  // (요청한 파일의 내용을 연결 식별자 fd로 복사해서 응답 본체를 보낸다.)
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n", filesize);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: %s\r\n\r\n", filetype);
  Rio_writen(fd, buf, strlen(buf)); // line:netp:servestatic:endserve

  // 읽을 수 있는 파일로 열기 (읽기 위해서 filename을 오픈하고 식별자를 얻어온다.)
  srcfd = Open(filename, O_RDONLY, 0);                        // line:netp:servestatic:open
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0); // line:netp:servestatic:mmap
  Close(srcfd);                                               // 파일을 메모리로 매핑한 후 파일을 닫는다. 
  
  // rio_writen 함수는 주소 srcp에서 시작하는 filesize 바이트를
  // 클라이언트의 연결 식별자로 복사한다.
  Rio_writen(fd, srcp, filesize);                             // line:netp:servestatic:write
  Munmap(srcp, filesize);                                     // 메모리 해제, 매핑된 가상메모리 주소를 반환
}

/*
 * get_filetype - derive file type from file name
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
  else
    strcpy(filetype, "text/plain");
}
/* $end serve_static */

// TINY는 자식 프로세스를 fork하고 그 후에 CGI 프로그램을 자식의 컨텍스트에서 실행하며
// 모든 종류의 동적 컨텐츠를 제공한다. 이 함수는 클라이언트에 성공을 알려주는
// 응답 라인을 보내는 것으로 시작한다. CGI 프로그램은 응답의 나머지 부분을 보내야 한다.
// 이것은 우리가 기대하는 것만큼 견고하지 않은데, 그 이유는 이것이 CGI 프로그램이 에러를
// 만날 수 있다는 가능성을 염두에 두지 않았기 때문이다.
void serve_dynamic(int fd, char *filename, char *cgiargs)
{
  char buf[MAXLINE], *emptylist[] = {NULL};

  /* Return first part of HTTP response */
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  // 클라이언트는 성공을 알려주는 응답 라인을 보내는 것으로 시작
  // 새로운 자식프로세스를 fork하고 동적 컨텐츠를 제공한다.
  if (Fork() == 0)
  { /* Child */ // line:netp:servedynamic:fork
    /* Real server would set all CGI vars here */
    // 자식은 QUERY_STRING 환경변수를 요청 uri의 cgi인자로 초기화 한다.  (15000 & 213)
    setenv("QUERY_STRING", cgiargs, 1);                         // line:netp:servedynamic:setenv

    // 자식은 자식의 표준 출력을 연결 파일 식별자로 재지정(Duplicate)
    Dup2(fd, STDOUT_FILENO); /* Redirect stdout to client */    // line:netp:servedynamic:dup2

    // 그 후에 cgi프로그램을 로드하고 실행한다.
    // CGI 프로그램이 자식 컨텍스트에서 실행되기 때문에 execve함수를 호출하기 전에 존재하던 열린 파일들과
    // 환경 변수들에도 동일하게 접근할 수 있다. 그래서 CGI 프로그램이 표준 출력에 쓰는 모든 것은 
    // 직접 클라이언트 프로세스로 부모 프로세스의 어떤 간섭도 없이 전달된다.
    Execve(filename, emptylist, environ); /* Run CGI program */ // line:netp:servedynamic:execve
  }
  Wait(NULL); // 부모는 자식이 종료되어 정리되기 까지 대기
}
/* $end serve_dynamic */

/* $begin clienterror */
void clienterror(int fd, char *cause, char *errnum,
                 char *shortmsg, char *longmsg)
{
  char buf[MAXLINE];

  /* Print the HTTP response headers */
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n\r\n");
  Rio_writen(fd, buf, strlen(buf));

  /* Print the HTTP response body */
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
/* $end clienterror */
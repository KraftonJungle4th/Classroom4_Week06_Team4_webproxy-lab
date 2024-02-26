#include "../csapp.h"

int main(int argc, char **argv)
{
    int clientfd;
    char *host, *port, buf[MAXLINE];
    rio_t rio;
    
    // 인자값이 3개가 아니면 에러 메세지를 발생시키고 종료
    if (argc != 3) {
        fprintf(stderr, "인자값을 확인해주세요 <host> <port> \n");
        exit(0);
    }

    // 인자값으로부터 호스트 정보와 포트 정보를 받아온다.
    host = argv[1];
    port = argv[2];

    // 서버와 연결시키기위한 소켓을 생성한다.
    clientfd = Open_clientfd(host, port);

    // 입력값을 저장하기 위한 버퍼를 초기화시킨다.
    Rio_readinitb(&rio, clientfd);
    // 1. 표준 입력으로부터 문자열을 입력 받아, 소켓 버퍼에 내용을 담아서 보낸다.
    // 2. 표준 입력을 읽어 들어 들인후, 클라이언트에서 보여줄 버퍼에 담아 출력한다.
    // 위 두가지 동작을 클라이언트가 명시적으로 종료했을 때까지 반복한다.
    while (Fgets(buf, MAXLINE, stdin) != NULL) {
        Rio_writen(clientfd, buf, strlen(buf));
        Rio_readlineb(&rio, buf, MAXLINE);
        Fputs(buf, stdout);
    }
    // 사용한 소켓을 닫아준다.
    Close(clientfd);
    exit(0);
}
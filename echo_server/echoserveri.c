#include "../csapp.h"

void echo(int connfd);

int main(int argc, char **argv)
{
    int listenfd, connfd;
    socklen_t clientlen;
    struct sockaddr_storage clientaddr;
    char client_hostname[MAXLINE], client_port[MAXLINE];

    // 인자값이 2개가 아니면 에러메세지와 함께 종료한다.
    if (argc != 2) {
        fprintf(stderr, "인자값을 확인해주세요 <port>\n");
        exit(0);
    }

    // 입력받은 포트로 서버용 소켓을 연다.
    listenfd = Open_listenfd(argv[1]);
    // 1. 서버용 소켓(듣기 소켓)을 통해, 클라이언트의 연결을 받을 소켓을 생성한다. 
    //  1-1. 프로토콜 독립적이여야 한다.
    // 2. 클라이언트의 정보를 읽을 수 있는 문자열로 변환한뒤, 호스트명과 포트번호를 출력한다.
    // 3. 연결된 클라이언트로부터 받은 정보를 그대로 전송한다.
    // 4. 연결을 닫는다.
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);
        connfd = Accept(listenfd, (SA *) &clientaddr, &clientlen);
        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, client_port, MAXLINE, 0);
        fprintf(stdout, "client hostname : %s | client port : %s \n", client_hostname, client_port);
        echo(connfd);
        Close(connfd);
    }
    Close(listenfd);
    exit(0);
}
#include "csapp.h"

void echo(int connfd);  // client와 통신하는 echo 함수 선언

int main(int argc, char **argv) 
{
    int listenfd, connfd;   // listen 소켓과 연결된 클라이언트 소켓의 파일 디스크립터
    socklen_t clientlen;    // 클라이언트 주소 길이
    struct sockaddr_storage clientaddr;  // 클라이언트의 주소 정보 저장하는 구조체

    // 클라이언트 호스트 이름과 포트 번호 저장하는 배열
    char client_hostname[MAXLINE], client_port[MAXLINE];    

    if (argc != 2) {    //포트 번호 입력 안된 경우
	fprintf(stderr, "usage: %s <port>\n", argv[0]);     // 안내 메세지 출력
	exit(0);
    }

    // 주어진 포트 번호로 listening 소켓을 열어 파일 디스크립터를 얻어옴
    listenfd = Open_listenfd(argv[1]);

    while (1) {
	clientlen = sizeof(struct sockaddr_storage); // 클라이언트 주소 길이 초기화

    // 클라이언트 연결을 받고 연결된 클라이언트 소켓의 파일 디스크립터를 얻어온다
	connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        Getnameinfo((SA *) &clientaddr, clientlen, client_hostname, MAXLINE, 
                    client_port, MAXLINE, 0);   //클라이언트의 호스트 이름과 포트 번호를 얻어옴

        printf("Connected to (%s, %s)\n", client_hostname, client_port);

	echo(connfd);   // echo 함수로 클라이언트와 통신
	Close(connfd);
    }
    exit(0);
}
/* $end echoserverimain */
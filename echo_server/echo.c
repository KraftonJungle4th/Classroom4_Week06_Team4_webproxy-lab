#include "../csapp.h"

void echo(int connfd)
{
    size_t n;
    char buf[MAXLINE];
    rio_t rio;

    // 특정 소켓 디스크립터에 대한 입력 버퍼를 초기화시킨다.
    Rio_readinitb(&rio, connfd);
    // 모든 문자열을 입력받아,
    // 1. 입력받은 데이터의 크기(바이트)를 서버(실행 환경)에 출력한다.
    // 2. 연결된 소켓에 입력받은 그대로 출력한다.
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        fprintf(stdout, "Receive %lu bytes from client\n", n);
        Rio_writen(connfd, buf, n);
    }
}
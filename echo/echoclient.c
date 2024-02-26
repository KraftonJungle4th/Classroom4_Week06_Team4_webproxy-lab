#include "csapp.h"

int main(int argc, char **argv){        //argc : 입력 받은 인자 수, argv : 입력받은 인자들의 배열
    int clientfd;
    char *host, *port, buf[MAXLINE];
    rio_t rio;

    if(argc!=3){    //인자가 제대로 된 값이 아닌 경우
        fprintf(stderr, "usuage : %s <host> <port>\n", argv[0]);    //안내 메세지 출력
        exit(0);
    }
    host = argv[1];     //전달한 첫번째 인자를 host에 저장
    port = argv[2];     //전달한 두번째 인자를 port에 저장

    //소켓 인터페이스 핸들링을 도와주는 Open_clientfd를 호출해 서버와 연결,
    //리턴받은 소켓 식별자를 clientfd에 저장
    clientfd = Open_clientfd(host, port);

    //rio 구조체 초기화, rio를 통해 파일 디스크립터 clientfd에 대한 읽기 작업 수행 설정
    Rio_readinitb(&rio, clientfd);

    // 입력이 끊기거나 오류가 발생하기까지 유저에게 받은 입력을 buf에 저장 반복
    while (Fgets(buf, MAXLINE, stdin) != NULL)
    {
        //파일 디스크립터 통해 buf에 저장된 데이터를 서버로 전송
        Rio_writen(clientfd, buf, strlen(buf));

        //rio 구조체를 통해 파일 디스크립터에서 한 줄의 문자열을 읽어와 buf에 저장,
        //MAXLINE은 버퍼의 최대 크기를 나타냄.
        Rio_readlineb(&rio, buf, MAXLINE);

        //buf에 저장된 문자열을 표준 출력 stdout에 출력
        Fputs(buf, stdout);
    }
    Close(clientfd);
    exit(0);
}
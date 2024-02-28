#include "../csapp.h"

/* < hostinfo >
 * 도메인 이름과 여기에 연관된 IP 주소로의 매핑을 출력한다.
 * NSLOOKUP과 유사하다.
 * 1. hints 구조체를 초기화해서 getaddrinfo()가 hints 구조체로 우리가 원하는 주소를 반환한다.
 * 2. 여기서 우리는 IPv4를 사용하고, 이 IP 주소는 연결의 종단점으로 사용된다.
 * 3. getaddrinfo()가 도메인 이름만 변환하기를 원하기에, 서비스 이름을 NULL로 설정한다.
 * 4. getaddrinfo()는 addrinfo 구조체의 리스트를 반환한다. 이 리스트는 도메인 이름과 연관된 IP 주소들을 포함한다.
 * 5. getnameinfo()를 사용해서 각 IP 주소를 텍스트 형태로 변환한다.
 * 6. getaddrinfo()가 반환한 addrinfo 구조체의 리스트를 freeaddrinfo()로 해제한다.
 */

int main(int argc, char **argv)
{
    struct addrinfo *p, *listp, hints;
    char buf[MAXLINE];
    int rc, flags;

    if (argc != 2)
    {
        fprintf(stderr, "usage: %s <domain name>\n", argv[0]);
        exit(0);
    }

    /* Get a list of addrinfo records */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;       /* IPv4 only */
    hints.ai_socktype = SOCK_STREAM; /* TCP Connections only */
    if ((rc = getaddrinfo(argv[1], NULL, &hints, &listp)) != 0)
    {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(rc));
        exit(1);
    }

    /* Walk the list and display each IP address */
    flags = NI_NUMERICHOST; /* 숫자 IP 주소 형식 */
    for (p = listp; p; p = p->ai_next)
    {
        Getnameinfo(p->ai_addr, p->ai_addrlen, buf, MAXLINE, NULL, 0, flags);
        printf("%s\n", buf);
    }

    /* Clean up */
    Freeaddrinfo(listp);

    exit(0);
}
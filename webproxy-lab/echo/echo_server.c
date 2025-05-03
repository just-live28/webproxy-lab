#include <stdio.h>
#include "csapp.h"

int open_listenfd(char *port);
void echo(int connfd);

int main(int argc, char **argv) {
    int listenfd, connfd;   // listenfd: 클라이언트 연결 요청을 수신하는 소켓
                            // connfd: 연결이 수락된 후 실제 데이터 통신에 사용되는 소켓
    socklen_t clientlen;    // 클라이언트 주소 구조체의 크기 (accept 시 필요)
    struct sockaddr_storage clientaddr; // 클라이언트 주소 정보를 저장 (IPv4/IPv6 모두 대응)
    char client_hostname[MAXLINE], client_port[MAXLINE]; // 연결된 클라이언트의 호스트 이름과 포트 번호 저장

    // 명령행 인자 확인: 포트 번호 하나만 입력받아야 함
    if (argc != 2) {
        fprintf(stderr, "usage: %s <port>\n", argv[0]);
        exit(0);
    }

    // 주어진 포트로 수신용 서버 소켓 생성
    listenfd = open_listenfd(argv[1]);

    // 클라이언트 연결을 무한 반복으로 기다림 (서버는 보통 계속 실행됨)
    while (1) {
        clientlen = sizeof(struct sockaddr_storage);  // 주소 구조체 크기 초기화

        // 클라이언트의 연결 요청 수락 → 새로운 통신용 소켓 생성
        connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);

        // 클라이언트의 주소 정보를 사람이 읽을 수 있는 문자열로 변환
        Getnameinfo((SA *)&clientaddr, clientlen,
                    client_hostname, MAXLINE,
                    client_port, MAXLINE, 0);

        // 연결된 클라이언트 정보 출력
        printf("Connected to (%s, %s)\n", client_hostname, client_port);

        // echo 함수 호출: 클라이언트와 데이터 송수신 처리 (클라이언트가 보낸 데이터를 그대로 돌려줌)
        echo(connfd);

        // 통신 종료 후 소켓 닫기
        printf("연결 종료\n");
        fflush(stdout);
        Close(connfd);
    }

    // 정상 종료 (사실상 도달하지 않음)
    exit(0);
}

int open_listenfd(char *port) {
    int listenfd, optval = 1;    // 서버 소켓 디스크립터, 소켓 옵션 설정용 변수
    struct addrinfo hints, *listp, *p;  // 주소 정보 힌트와 결과 리스트 포인터들

    // hints 초기화: 어떤 종류의 주소 정보를 원하는지 설정
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;     // TCP(연결 지향형 스트림)
    hints.ai_flags = AI_NUMERICSERV;     // 포트가 숫자 문자열이라는 의미 ("80" 등)
    hints.ai_flags |= AI_ADDRCONFIG;     // 현재 시스템의 IPv4/IPv6 환경을 고려
    // NULL을 넘겨서 "모든 IP 주소"에 바인딩 (서버의 모든 네트워크 인터페이스에서 접속 허용)
    Getaddrinfo(NULL, port, &hints, &listp);

    // 주소 리스트를 하나씩 돌면서 소켓 생성 + 바인딩 시도
    for (p = listp; p != NULL; p = p->ai_next) {

        // 소켓 생성
        if ((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
            continue;  // 실패 시 다음 주소 시도
        }

        // 포트 재사용 설정 (서버 재시작 시 "Address already in use" 방지)
        Setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, (const void*)&optval, sizeof(int));

        // 바인딩 시도 (서버 소켓에 IP/포트 연결)
        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0) {
            break; // 바인딩 성공
        }

        // 바인딩 실패 → 소켓 닫고 다음 주소 시도
        Close(listenfd);
    }

    // 주소 리스트 메모리 해제
    Freeaddrinfo(listp);

    // 모든 바인딩 시도 실패
    if (p == NULL) {
        return -1;
    }

    // 클라이언트의 연결 요청을 대기 상태로 설정
    if (listen(listenfd, LISTENQ) < 0) {
        // 실패 시 소켓 닫고 -1 반환
        Close(listenfd);
        return -1;
    }

    // 성공 시, 클라이언트 연결을 받을 수 있는 서버 소켓 디스크립터 반환
    return listenfd;
}

void echo(int connfd) {
    size_t n;             // 읽어온 바이트 수
    char buf[MAXLINE];    // 데이터를 읽고 쓸 버퍼
    rio_t rio;            // Robust I/O 구조체 (CSAPP에서 제공)

    // Robust I/O 입력 버퍼 초기화 (connfd를 기반으로 rio 설정)
    Rio_readinitb(&rio, connfd);

    // 클라이언트로부터 한 줄씩 데이터를 읽어서 그대로 다시 돌려줌
    while ((n = Rio_readlineb(&rio, buf, MAXLINE)) != 0) {
        // 읽은 바이트 수 출력
        printf("server received %d bytes\n", (int)n);

        // 읽은 데이터를 클라이언트에게 다시 전송 (에코)
        Rio_writen(connfd, buf, n);
    }

    printf("echo 종료\n");
    fflush(stdout);

    // 클라이언트가 EOF(연결 종료)를 보내면 반복문 탈출
}
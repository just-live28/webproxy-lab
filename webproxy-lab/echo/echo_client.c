#include <stdio.h>
#include "csapp.h"

int open_clientfd(char *hostname, char*port);

int main(int argc, char **argv) {
    int clientfd;                         // 서버와 연결된 소켓 파일 디스크립터
    char *host, *port, buf[MAXLINE];      // 서버 주소, 포트 번호, 메시지 저장 버퍼
    rio_t rio;                            // Robust I/O를 위한 버퍼 구조체 (CSAPP 제공)

    // 인자 개수 확인: 실행 시 반드시 <host> <port> 두 인자를 받아야 함
    if (argc != 3) {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);  // 인자 부족 시 프로그램 종료
    }

    // 명령행 인자로부터 서버 주소와 포트 번호 가져오기
    host = argv[1];     // 예: "localhost" 또는 "127.0.0.1"
    port = argv[2];     // 예: "8080"

    // 클라이언트 소켓을 열고 서버에 연결
    clientfd = open_clientfd(host, port);

    // Robust I/O(안정적인 입출력)를 위한 rio 버퍼 초기화
    Rio_readinitb(&rio, clientfd);

    // 사용자로부터 입력을 받아 서버에 전송하고, 서버 응답을 받아 출력하는 반복문
    while (Fgets(buf, MAXLINE, stdin) != NULL) {
        // 입력한 문자열을 서버로 전송
        Rio_writen(clientfd, buf, strlen(buf));

        // 서버로부터 한 줄을 읽음 (버퍼를 사용해 안정적이고 효율적으로 처리)
        Rio_readlineb(&rio, buf, MAXLINE);

        // 서버로부터 받은 응답을 화면에 출력
        Fputs(buf, stdout);
    }

    // 통신이 끝나면 소켓 닫고 종료
    Close(clientfd);
    exit(0);
}

int open_clientfd(char *hostname, char *port) {
    int clientfd;                        // 클라이언트 측 소켓 파일 디스크립터 (연결 성공 시 반환됨)
    struct addrinfo hints, *listp, *p;   // 주소 정보를 위한 구조체들

    // 'hints' 구조체를 0으로 초기화 (초기값 설정)
    memset(&hints, 0, sizeof(struct addrinfo));

    // 연결 지향형 TCP 스트림을 원한다는 것을 명시
    hints.ai_socktype = SOCK_STREAM;

    // 포트 번호가 숫자임을 명시 (예: "80"은 숫자이므로 DNS 조회 필요 없음)
    hints.ai_flags = AI_NUMERICSERV;

    // 현재 시스템의 네트워크 환경에 맞는 주소만 조회
    hints.ai_flags |= AI_ADDRCONFIG;

    // hostname과 port에 해당하는 주소 정보를 가져옴
    // listp는 여러 개의 주소 정보를 가리키는 연결 리스트의 시작점이 됨
    Getaddrinfo(hostname, port, &hints, &listp);

    // 주소 리스트를 돌면서 접속 시도
    for (p = listp; p != NULL; p = p->ai_next) {

        // 현재 주소 정보에 맞는 소켓 생성 시도
        if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
            continue;   // 소켓 생성 실패 → 다음 주소로 넘어감
        }

        // 소켓을 통해 서버에 연결 시도
        if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1) {
            break;      // 연결 성공 → 루프 탈출
        }

        // 연결 실패 → 소켓을 닫고 다음 주소로 이동
        Close(clientfd);
    }

    // 주소 정보 해제 (메모리 반환)
    Freeaddrinfo(listp);

    // 연결이 실패한 경우(p == NULL)
    if (p == NULL) {
        return -1;  // 모든 연결 실패
    }

    // 연결 성공한 경우: 해당 소켓 파일 디스크립터 반환
    return clientfd;
}
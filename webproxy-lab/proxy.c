#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NTHREADS 4
#define SBUFSIZE 16

typedef struct {
  int *buf;            // connfd 저장 배열
  int front;                    // dequeue 인덱스
  int rear;                     // enqueue 인덱스
  int n;                        // 현재 들어있는 아이템 수

  pthread_mutex_t mutex;        // 큐 접근 mutex
  pthread_cond_t slots;         // 빈 슬롯이 생겼을 때 signal
  pthread_cond_t items;         // 아이템이 추가되었을 때 signal
} sbuf_t; // 작업 큐 구조체

void *thread(void *vargp);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char*host, char *port, char *path);
void func(int connfd);

// 스레드 풀 함수
void sbuf_init(sbuf_t *sp, int n);        // 큐 초기화
void sbuf_insert(sbuf_t *sp, int item);   // connfd 저장 (enqueue)
int sbuf_remove(sbuf_t *sp);              // connfd 꺼내기 (dequeue)


/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";
sbuf_t sbuf;

int main(int argc, char **argv) {
  int listenfd, connfd;
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  sbuf_init(&sbuf, SBUFSIZE); // 작업 큐 초기화

  // 워커 스레드 생성
  pthread_t tid;
  for (int i = 0; i < NTHREADS; i++) {
    pthread_create(&tid, NULL, thread, NULL);
  }

  // 메인 스레드: 클라이언트 연결 수락 및 큐에 삽입
  listenfd = Open_listenfd(argv[1]);
  while (1) {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    sbuf_insert(&sbuf, connfd); // connfd를 큐에 삽입
  }

  return 0;
}

void func(int connfd) {
  rio_t client_rio, server_rio;
  char buf[MAXLINE], req[MAX_OBJECT_SIZE];
  char method[MAXLINE], uri[MAXLINE], version[MAXLINE];
  char host[MAXLINE], port[10], path[MAXLINE];

  Rio_readinitb(&client_rio, connfd);

  // 1. 요청 라인 먼저 읽기
  if (!Rio_readlineb(&client_rio, buf, MAXLINE)) return;
  sscanf(buf, "%s %s %s", method, uri, version);

  if (parse_uri(uri, host, port, path) == -1) {
      fprintf(stderr, "올바른 URI가 아닙니다: %s\n", uri);
      return;
  }

  // 요청 라인 → path와 HTTP/1.0으로 변경
  sprintf(req, "GET %s HTTP/1.0\r\n", path);

  // 2. 헤더 읽기 및 필터링
  while (Rio_readlineb(&client_rio, buf, MAXLINE) > 0 && strcmp(buf, "\r\n") != 0) {
      if (strncasecmp(buf, "Host", 4) == 0 ||
          strncasecmp(buf, "User-Agent", 10) == 0 ||
          strncasecmp(buf, "Connection", 10) == 0 ||
          strncasecmp(buf, "Proxy-Connection", 16) == 0) {
          continue;
      }
      strcat(req, buf);  // 유효한 헤더는 저장
  }

  // 표준 헤더 추가
  sprintf(buf, "Host: %s\r\n", host); strcat(req, buf);
  sprintf(buf, "%s", user_agent_hdr); strcat(req, buf);
  sprintf(buf, "Connection: close\r\n"); strcat(req, buf);
  sprintf(buf, "Proxy-Connection: close\r\n\r\n"); strcat(req, buf);
  printf("최종 요청:\n%s\n", req);

  // 원 서버 Tiny와 연결 + 요청 전송
  int serverfd = Open_clientfd(host, port);
  if (serverfd < 0) {
      fprintf(stderr, "원 서버 연결 실패\n");
      return;
  }
  Rio_readinitb(&server_rio, serverfd);

  // 요청 전체 전송
  Rio_writen(serverfd, req, strlen(req));

  /* --- 응답 헤더 전달(라인 별) --- */
  int n;
  while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) > 0) {
    Rio_writen(connfd, buf, n);
    if (strcmp(buf, "\r\n") == 0) break;  // 헤더 끝 감지
  }

  /* --- 응답 바디 전달(바이트 스트림 전체) --- */
  while ((n = Rio_readnb(&server_rio, buf, MAXBUF)) > 0) {
    Rio_writen(connfd, buf, n);
  }

  Close(serverfd);
}

int parse_uri(char *uri, char*host, char *port, char *path) {
  // "http://www.example.com:8000/index.html"
  char *hostbegin, *hostend, *pathbegin, *portbegin;

  if (strncasecmp(uri, "http://", 7) != 0) {  // 대소문자 구분 없이 접두어 확인
    return -1;
  }
  // 파일 경로 찾기
  hostbegin = uri + 7;  // http:// 이후부터 시작
  pathbegin = strchr(hostbegin, '/'); 
  if (pathbegin) {  // '/'가 있다면
    strcpy(path, pathbegin);  // 경로에 이후 내용(파일 경로)을 저장
    *pathbegin = '\0';        // '/' 이전 이후로 문자열 분리
  } else {
    strcpy(path, "/");        // '/'가 없다면 path에 '/'를 저장
  }
  // 포트 번호 찾기
  portbegin = strchr(hostbegin, ':');
  if (portbegin) {  // ':'가 있다면
    *portbegin = '\0';  // ':'를 기준으로 host시작부를 분리
    strcpy(host, hostbegin);
    strcpy(port, portbegin + 1);
  } else {
    strcpy(host, hostbegin);  // ':'가 없다면 전부 호스트 문자열
    strcpy(port, "80"); // 기본 포트 설정
  }

  return 0;
}

void sbuf_init(sbuf_t *sp, int n) {
  sp->buf = Calloc(n, sizeof(int));     // connfd 저장용 배열 할당
  sp->n = n;                            // 버퍼 크기 저장
  sp->front = sp->rear = 0;             // 초기 인덱스 0
  pthread_mutex_init(&sp->mutex, NULL); // mutex 초기화
  pthread_cond_init(&sp->slots, NULL);  // 빈 슬롯 대기 조건 변수 초기화
  pthread_cond_init(&sp->items, NULL);  // 아이템 대기 조건 변수 초기화

}

void sbuf_insert(sbuf_t *sp, int item) {
  pthread_mutex_lock(&sp->mutex);       // 큐 접근 mutext 잠금

  while (((sp->rear + 1) % sp->n) == sp->front) { // 큐가 가득 찬 경우
    pthread_cond_wait(&sp->slots, &sp->mutex);    // 빈 슬롯이 생길 때까지 대기
  }

  sp->buf[sp->rear] = item;           // connfd를 rear 위치에 저장
  sp->rear = (sp->rear + 1) % sp->n;  // rear 인덱스 증가 (원형 회전)

  pthread_cond_signal(&sp->items);    // 대기 중인 워커 스레드에게 작업이 생겼음을 알림
  pthread_mutex_unlock(&sp->mutex);   // mutex 잠금 해제
}

int sbuf_remove(sbuf_t *sp) {
  pthread_mutex_lock(&sp->mutex); // 큐 접근 잠금

  while (sp->front == sp->rear) { // 큐가 비어있는 경우
    pthread_cond_wait(&sp->items, &sp->mutex);  // 아이템이 들어올 때까지 대기
  }

  int item = sp->buf[sp->front];        // front 위치의 connfd 가져오기
  sp->front = (sp->front + 1) % sp->n;  // front 인덱스 증가 (원형 회전)

  pthread_cond_signal(&sp->slots);      // 대기 중인 메인 스레드에게 공간이 생겼음을 알림
  pthread_mutex_unlock(&sp->mutex);     // 잠금 해제

  return item;  // connfd 반환
}

void *thread(void *vargp) {
  pthread_detach(pthread_self()); // 스레드 자원 자동 회수

  while (1) {
    int connfd = sbuf_remove(&sbuf);  // 작업 큐에서 connfd 꺼내기
    func(connfd);                     // 요청 처리 함수 호출
    close(connfd);                    // 클라이언트와의 연결 종료
  }
}
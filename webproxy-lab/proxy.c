#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define NTHREADS 4
#define SBUFSIZE 16

typedef struct {
  int *buf;                     // connfd 저장 배열
  int front;                    // dequeue 인덱스
  int rear;                     // enqueue 인덱스
  int n;                        // 현재 들어있는 아이템 수

  pthread_mutex_t mutex;        // 큐 접근 mutex
  pthread_cond_t slots;         // 빈 슬롯이 생겼을 때 signal
  pthread_cond_t items;         // 아이템이 추가되었을 때 signal
} sbuf_t; // 작업 큐 구조체

typedef struct cache_node {
  char *uri;  // 요청된 URI
  char *data; // 응답 데이터
  int size;   // data의 크기

  struct cache_node *prev;  // 이전 노드
  struct cache_node *next;  // 다음 노드
} cache_node_t; // 캐시 노드 구조체

typedef struct {
  cache_node_t *head;   // 가장 최근에 사용된 노드
  cache_node_t *tail;   // 가장 오래된 노드
  int total_size;       // 현재 캐시에 저장된 총 크기

  pthread_rwlock_t lock; // 캐시 접근 보호 mutex
} cache_t;  // 캐시 구조체

void *thread(void *vargp);
void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char*host, char *port, char *path);
void func(int connfd);

// 스레드 풀 함수
void sbuf_init(sbuf_t *sp, int n);        // 큐 초기화
void sbuf_insert(sbuf_t *sp, int item);   // connfd 저장 (enqueue)
int sbuf_remove(sbuf_t *sp);              // connfd 꺼내기 (dequeue)

// 캐시 함수
void cache_init(cache_t *cache);  // 캐시 초기화
cache_node_t *find_cache(cache_t *cache, const char *uri);  // 캐시 검색
void insert_cache(cache_t *cache, const char *uri, const char *data, int size); // 캐시에 새 노드 삽입
void evict_cache(cache_t *cache); // 캐시 마지막 노드 제거

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

sbuf_t sbuf;
cache_t cache;

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
  cache_init(&cache); // 캐시 초기화

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

  // 1. 요청 라인 읽기
  if (!Rio_readlineb(&client_rio, buf, MAXLINE)) return;
  sscanf(buf, "%s %s %s", method, uri, version);

  // 2. 캐시 검색
  cache_node_t *cached = find_cache(&cache, uri);
  if (cached) {
    // Cache Hit -> 응답 전송 후 종료
    Rio_writen(connfd, cached->data, cached->size);
    return;
  }

  // 3. URI 파싱 (호출 전 캐시 삽입용 URI 원본 복사)
  char uri_key[MAXLINE];
  strcpy(uri_key, uri);
  if (parse_uri(uri, host, port, path) == -1) {
      fprintf(stderr, "올바른 URI가 아닙니다: %s\n", uri);
      return;
  }

  // 4. 요청 헤더 재작성
  sprintf(req, "GET %s HTTP/1.0\r\n", path);
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

  // 5. 원 서버에 요청
  int serverfd = Open_clientfd(host, port);
  if (serverfd < 0) {
      fprintf(stderr, "원 서버 연결 실패\n");
      return;
  }

  Rio_readinitb(&server_rio, serverfd);
  Rio_writen(serverfd, req, strlen(req)); // 요청 전체 전송

  // 6. 응답 수신 + 클라이언트로 전송 + 캐싱 준비
  int n;
  int total_size = 0;
  char *object_buf = Malloc(MAX_OBJECT_SIZE);
  char *p = object_buf;

  // 응답 헤더 전송
  while ((n = Rio_readlineb(&server_rio, buf, MAXLINE)) > 0) {
    Rio_writen(connfd, buf, n);
    if (total_size + n < MAX_OBJECT_SIZE) {
      memcpy(p, buf, n);
      p += n;
      total_size += n;
    }
    if (strcmp(buf, "\r\n") == 0) break; // 요청 끝 감지
  }

  // 응답 바디 전송
  while ((n = Rio_readnb(&server_rio, buf, MAXBUF)) > 0) {
    Rio_writen(connfd, buf, n);
    if (total_size + n < MAX_OBJECT_SIZE) {
      memcpy(p, buf, n);
      p += n;
      total_size += n;
    }
  }

  // 7. 캐시 저장 (크기 조건 만족 시)
  if (total_size <= MAX_OBJECT_SIZE) {
    insert_cache(&cache, uri_key, object_buf, total_size);
  }

  free(object_buf);
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

void cache_init(cache_t *cache) {
  cache->head = NULL;
  cache->tail = NULL;
  cache->total_size = 0;
  pthread_rwlock_init(&cache->lock, NULL);
}

cache_node_t *find_cache(cache_t *cache, const char *uri) {
  pthread_rwlock_rdlock(&cache->lock); // 읽기 락(읽기 시에는 동시 접근 가능)

  cache_node_t *node = cache->head;
  while (node) {
    if (strcmp(node->uri, uri) == 0) {
      // Cache Hit: 적중한 노드를 Head로 이동
      pthread_rwlock_unlock(&cache->lock); // 캐시 접근 보호 해제
      return node;
    }
    node = node->next;
  }
  // Cache Miss
  pthread_rwlock_unlock(&cache->lock); // 읽기 락 해제
  return NULL;  // 캐시에 없으면 NULL 반환
}

void insert_cache(cache_t *cache, const char *uri, const char *data, int size) {
  pthread_rwlock_wrlock(&cache->lock); // 캐시 접근 보호(동기화)

  // 필요한 공간 확보. 초과한 경우 맨 뒤 노드를 제거
  while (cache->total_size + size > MAX_CACHE_SIZE) {
    evict_cache(cache);
  }

  // 새로운 노드 생성
  cache_node_t *node = Malloc(sizeof(cache_node_t));
  node->uri = Malloc(strlen(uri) + 1);  // 널 문자가 없을 수도 있으므로 +1 할당
  strcpy(node->uri, uri);

  node->data = Malloc(size);
  memcpy(node->data, data, size);
  node->size = size;

  // 리스트 앞에 삽입
  node->prev = NULL;
  node->next = cache->head;
  if (cache->head) {
    cache->head->prev = node;
  }
  cache->head = node;

  if (cache->tail == NULL) {  
    cache->tail = node; // 첫 노드라면 tail로도 설정
  }

  cache->total_size += size;  // 데이터 양만큼 전체 데이터 크기 증가
  pthread_rwlock_unlock(&cache->lock); // 캐시 접근 보호 해제(동기화 해제)
}

void evict_cache(cache_t *cache) {
  if (cache->tail == NULL) return;

  cache_node_t *node = cache->tail;

  // 리스트에서 제거
  if (node->prev) {
    node->prev->next = NULL;
  } else {
    cache->head = NULL;
  }

  cache->tail = node->prev;

  // 메모리 해제 및 데이터 양만큼 전체 데이터 크기 감소
  cache->total_size -= node->size;
  free(node->uri);
  free(node->data);
  free(node);
}
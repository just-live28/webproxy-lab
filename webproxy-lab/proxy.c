#include <stdio.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

void read_requesthdrs(rio_t *rp);
int parse_uri(char *uri, char*host, char *port, char *path);
void func(int connfd);

/* You won't lose style points for including this long line in your code */
static const char *user_agent_hdr =
    "User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:10.0.3) Gecko/20120305 "
    "Firefox/10.0.3\r\n";

int main(int argc, char **argv) {
  int listenfd, connfd;
  char hostname[MAXLINE], port[MAXLINE];
  socklen_t clientlen;
  struct sockaddr_storage clientaddr;

  /* Check command line args */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);
  while (1) {
    struct sockaddr_storage clientaddr;
    socklen_t clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr, &clientlen);
    func(connfd);
    Close(connfd);
  }
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
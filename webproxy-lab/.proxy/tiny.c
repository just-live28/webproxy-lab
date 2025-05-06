/* $begin tinymain */
/*
 * tiny.c - A simple, iterative HTTP/1.0 Web server that uses the
 *     GET method to serve static and dynamic content.
 *
 * Updated 11/2019 droh
 *   - Fixed sprintf() aliasing issue in serve_static(), and clienterror().
 */
#include "csapp.h"

void doit(int fd);  // 클라이언트 요청을 해석한 뒤, 정적 또는 동적 컨텐츠를 클라이언트에게 제공하는 tiny의 핵심 요청 처리 함수
void read_requesthdrs(rio_t *rp); // 요청 헤더를 읽는 함수(사실상 무시)
int parse_uri(char *uri, char *filename, char *cgiargs);  // uri 해석 함수 (정적/동적 여부 판단 + filename, cgiargs 추출)
void serve_static(int fd, char *filename, int filesize, char *method);  // 정적 파일 전송 함수
void get_filetype(char *filename, char *filetype);
void serve_dynamic(int fd, char *filename, char *cgiargs);
void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg); // 에러메시지 생성 및 전송 함수

int main(int argc, char **argv)
{
  int listenfd, connfd;                     // 서버 측 듣기 소켓, 통신 소켓
  char hostname[MAXLINE], port[MAXLINE];    // 클라이언트의 서버IP(도메인), 포트 번호를 담을 공간
  socklen_t clientlen;                      // 클라이언트 주소 구조체 길이
  struct sockaddr_storage clientaddr;       // 클라이언트 주소

  /* Check command line args 인자가 2개가 아닐 시 에러 출력 및 종료 */
  if (argc != 2)
  {
    fprintf(stderr, "usage: %s <port>\n", argv[0]);
    exit(1);
  }

  listenfd = Open_listenfd(argv[1]);      // 듣기 소켓 준비
  while (1)
  {
    clientlen = sizeof(clientaddr);
    connfd = Accept(listenfd, (SA *)&clientaddr,
                    &clientlen); // line:netp:tiny:accept 클라이언트 연결 수락
    Getnameinfo((SA *)&clientaddr, clientlen, hostname, MAXLINE, port, MAXLINE,
                0);
    printf("Accepted connection from (%s, %s)\n", hostname, port);
    doit(connfd);  // line:netp:tiny:doit
    Close(connfd); // line:netp:tiny:close
  }
}

void doit(int fd) {
  int is_static;  // 정적 컨텐츠 여부
  struct stat sbuf; // 요청한 파일의 상태를 기록하는 구조체
  char buf[MAXLINE], method[MAXLINE], uri[MAXLINE], version[MAXLINE]; // rio 버퍼, mathoe/uri/version 저장 공간
  char filename[MAXLINE], cgiargs[MAXLINE]; // filename, cgiargs 저장 공간
  rio_t rio;

  // 요청 줄, 헤더 읽기 (헤더는 무시)
  Rio_readinitb(&rio, fd);
  Rio_readlineb(&rio, buf, MAXLINE);  // 요청 줄 읽기
  printf("Request headers:\n");
  printf("%s", buf);
  sscanf(buf, "%s %s %s", method, uri, version);
  if (!(strcasecmp(method, "GET") == 0 || strcasecmp(method, "HEAD") == 0)) {
    clienterror(fd, method, "501", "Not Implemented", 
                "Tiny does not implement this method");
    return;
  }
  read_requesthdrs(&rio);

  // URI 해석
  is_static = parse_uri(uri, filename, cgiargs);  // 동적/정적 여부 판단 + filename/cgiargs 추출
  if (stat(filename, &sbuf) < 0) {  // filename을 통해 파일이 존재하는지 확인, 존재한다면 파일 정보를 담은 구조체가 sbuf로 반환
    clienterror(fd, filename, "404", "Not found", "Tiny couldn't find this file");
    return;
  }

  // 권한 검증 및 정적/동적 컨텐츠 분기
  if (is_static) {  // 정적 컨텐츠인 경우
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IRUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't read the file");
      return;
    }
    serve_static(fd, filename, sbuf.st_size, method);
  } else {  // 동적 컨텐츠인 경우
    if (!(S_ISREG(sbuf.st_mode)) || !(S_IXUSR & sbuf.st_mode)) {
      clienterror(fd, filename, "403", "Forbidden", "Tiny couldn't run the CGI program");
      return;
    }
    serve_dynamic(fd, filename, cgiargs);
  }
}

void serve_static(int fd, char *filename, int filesize, char *method) {
  int srcfd;
  char *srcp, filetype[MAXLINE], buf[MAXBUF];

  // 클라이언트에게 응답 헤더 전송
  get_filetype(filename, filetype);
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  sprintf(buf, "%sServer: Tiny Web Server\r\n", buf);
  sprintf(buf, "%sConnection: close\r\n", buf);
  sprintf(buf, "%sContent-length: %d\r\n", buf, filesize);
  sprintf(buf, "%sContent-type: %s\r\n\r\n", buf, filetype);
  Rio_writen(fd, buf, strlen(buf));
  printf("Response headers:\n");
  printf("%s", buf);

  if (strcasecmp(method, "HEAD") == 0)
    return;

  // 클라이언트에게 응답 body 전송
  srcfd = Open(filename, O_RDONLY, 0);
  srcp = Mmap(0, filesize, PROT_READ, MAP_PRIVATE, srcfd, 0);
  Close(srcfd);
  Rio_writen(fd, srcp, filesize);
  Munmap(srcp, filesize);
}

void serve_dynamic(int fd, char *filename, char *cgiargs) {
  char buf[MAXLINE], *emptylist[] = {NULL};

  // 클라이언트에게 응답 초기 부분 전송
  sprintf(buf, "HTTP/1.0 200 OK\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Server: Tiny Web Server\r\n");
  Rio_writen(fd, buf, strlen(buf));

  if (Fork() == 0) {  // 자식 프로세스 생성
    // CGI 설정
    setenv("QUERY_STRING", cgiargs, 1); // QUERY_STRING 환경 변수 설정
    Dup2(fd, STDOUT_FILENO);            // 표준 출력을 클라이언트 전송 소켓에게 리다이렉션
    Execve(filename, emptylist, environ); // CGI 프로그램 실행
  }
  Wait(NULL); // 자식이 끝날 때까지 대기
}


void clienterror(int fd, char *cause, char *errnum, char *shortmsg, char *longmsg) {
  char buf[MAXLINE], body[MAXBUF];

  // build HTTP 응답 body
  sprintf(body, "<html><title>Tiny Error</title>");
  sprintf(body, "%s<body bgcolor=""ffffff"">\r\n", body);
  sprintf(body, "%s%s: %s\r\n", body, errnum, shortmsg);
  sprintf(body, "%s<p>%s: %s\r\n", body, longmsg, cause);
  sprintf(body, "%s<hr><em>The Tiny Web Server</em>\r\n", body);

  // HTTP 응답 출력
  sprintf(buf, "HTTP/1.0 %s %s\r\n", errnum, shortmsg);
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-type: text/html\r\n");
  Rio_writen(fd, buf, strlen(buf));
  sprintf(buf, "Content-length: %d\r\n\r\n", (int)strlen(body));
  Rio_writen(fd, buf, strlen(buf));
  Rio_writen(fd, body, strlen(body));
}

void read_requesthdrs(rio_t *rp) {
  char buf[MAXLINE];

  Rio_readlineb(rp, buf, MAXLINE);  // 요청 헤더 첫 줄
  while (strcmp(buf, "\r\n")) {
    Rio_readlineb(rp, buf, MAXLINE);
    printf("%s", buf);
  }
  return;
}

int parse_uri(char *uri, char *filename, char *cgiargs) {
  char *ptr;

  if (!strstr(uri, "cgi-bin")) { // 정적 컨텐츠인 경우
    strcpy(cgiargs, "");
    strcpy(filename, ".");
    strcat(filename, uri);
    if (uri[strlen(uri)-1] == '/') {
      strcat(filename, "home.html");
    }
    return 1;
  } else {  // 동적 컨텐츠인 경우
    ptr = index(uri, '?');
    if (ptr) {
      strcpy(cgiargs, ptr+1);
      *ptr = '\0';
    } else {
      strcpy(cgiargs, "");
    }
    strcpy(filename, ".");
    strcat(filename, uri);
    return 0;
  }
}

void get_filetype(char *filename, char *filetype) {
  if (strstr(filename, ".html")) {
    strcpy(filetype, "text/html");
  } else if (strstr(filename, ".gif")) {
    strcpy(filetype, "image/gif");
  } else if (strstr(filename, ".png")) {
    strcpy(filetype, "image/png");
  } else if (strstr(filename, ".jpg")) {
    strcpy(filetype, "image/jpeg");
  } else if (strstr(filename, ".mp4")) {
    strcpy(filetype, "video/mp4");
  } else {
    strcpy(filetype, "text/plain");
  }
}
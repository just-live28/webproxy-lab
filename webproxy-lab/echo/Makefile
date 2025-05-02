# Makefile for echo_client and echo_server

CC = gcc
CFLAGS = -Wall -Wextra -g
LDFLAGS = -lpthread

# 디렉토리 및 파일
SRCDIR = .
TARGETS = echo_client echo_server

all: $(TARGETS)

echo_client: $(SRCDIR)/echo_client.c $(SRCDIR)/csapp.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

echo_server: $(SRCDIR)/echo_server.c $(SRCDIR)/csapp.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(TARGETS)
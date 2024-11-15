CC=gcc
CFLAGS= 

all: clean build

debug: CFLAGS=-g -fsanitize=address
debug: all

build: server.c client.c deque.h deque.c common.h common.c
	${CC} -o server server.c deque.c common.c ${CFLAGS}
	${CC} -o client client.c deque.c common.c ${CFLAGS}

clean:
	rm -rf server client *.bin *.out *.dSYM

zip: clean
	rm -f project0.zip
	mkdir -p project
	cp server.c client.c Makefile project
	zip project0.zip project/*

CC=gcc
CFLAGS= 


all: clean build

debug: CFLAGS=-g -fsanitize=address
debug: all

build: server client

server: server.c reliable.c reliable.h
	${CC} ${CFLAGS} -o $@ server.c reliable.c

client: client.c reliable.c reliable.h
	${CC} ${CFLAGS} -o $@ client.c reliable.c

clean:
	rm -rf server client *.bin *.out *.dSYM

zip: clean
	rm -f project0.zip
	mkdir -p project
	cp server.c client.c Makefile project
	zip project0.zip project/*

CC=gcc
CXX=g++
LDFLAGS=-lcrypto

debug: CFLAGS=-g -fsanitize=address
debug: CXXFLAGS=-g -fsanitize=address
debug: all

all: clean build

build: client server

client: client.cpp common.o deque.o sec.o security.o
	${CXX} -o $@ $^ ${LDFLAGS} ${CXXFLAGS}

server: server.cpp common.o deque.o sec.o security.o
	${CXX} -o $@ $^ ${LDFLAGS} ${CXXFLAGS}

clean:
	rm -rf server client *.out *.dSYM *.o

zip: clean
	rm -f project0.zip
	mkdir -p project
	cp server.c client.c Makefile project
	zip project0.zip project/*

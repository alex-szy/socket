CC=gcc
CXX=g++
LDLIBS=-lcrypto -l:libstdc++.a

all: clean build

debug: CFLAGS=-g -fsanitize=address
debug: CXXFLAGS=-g -fsanitize=address
debug: all

build: client server

client: client.o common.o deque.o sec.o security.o

server: server.o common.o deque.o sec.o security.o

clean:
	rm -rf server client *.out *.dSYM *.bin *.o

zip: clean
	rm -f project0.zip
	mkdir -p project
	cp server.c client.c Makefile project
	zip project0.zip project/*

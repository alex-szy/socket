#ifndef PROJECT_COMMON_H_
#define PROJECT_COMMON_H_

#include <errno.h>

static inline void die(const char s[]) {
    perror(s);
    exit(errno);
}

void init_server(int port);
void init_client(int port, const char* address);

void transport_listen();

#endif  // PROJECT_COMMON_H_

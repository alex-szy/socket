#ifndef PROJECT_COMMON_H_
#define PROJECT_COMMON_H_

#include <errno.h>
#include <stdint.h>
#include <stdio.h>

static inline void die(const char s[]) {
    perror(s);
    exit(errno);
}

#ifdef __cplusplus
extern "C" {
#endif

void init_server(int port, ssize_t(*read_sec)(uint8_t*, size_t), ssize_t(*write_sec)(uint8_t*, size_t));
void init_client(int port, const char* address, ssize_t(*read_sec)(uint8_t*, size_t), ssize_t(*write_sec)(uint8_t*, size_t));

void transport_listen();

#ifdef __cplusplus
}
#endif

#endif  // PROJECT_COMMON_H_

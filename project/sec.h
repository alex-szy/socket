#ifndef PROJECT_SEC_H
#define PROJECT_SEC_H

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void init_sec(int client);

extern ssize_t (*read_sec)(uint8_t*, size_t);
extern ssize_t (*write_sec)(uint8_t*, size_t);

#ifdef __cplusplus
}
#endif

#endif // PROJECT_SEC_H
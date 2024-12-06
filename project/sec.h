#ifndef PROJECT_SEC_H
#define PROJECT_SEC_H

#include <stdlib.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

ssize_t read_sec_client(uint8_t*, size_t);
ssize_t write_sec_client(uint8_t*, size_t);

ssize_t read_sec_server(uint8_t*, size_t);
ssize_t write_sec_server(uint8_t*, size_t);

#ifdef __cplusplus
}
#endif

#endif // PROJECT_SEC_H
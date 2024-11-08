#ifndef DEQUEH
#define DEQUEH

#include <stdlib.h>
#include <stdbool.h>
#include "utils.h"

typedef struct queue_t* q_handle_t;

q_handle_t q_init(uint8_t capacity);
void q_destroy(q_handle_t self);
void q_clear(q_handle_t self);
bool q_push_back(q_handle_t self, packet *pkt);
bool q_push_front(q_handle_t self, packet *pkt);
bool q_try_insert_keep_sorted(q_handle_t self, packet *pkt);
packet* q_pop_front(q_handle_t self);
packet* q_pop_front_get_next(q_handle_t self);
packet* q_front(q_handle_t self);
size_t q_size(q_handle_t self);
bool q_full(q_handle_t self);
bool q_empty(q_handle_t self);
void q_print(q_handle_t self, const char *str);

#endif
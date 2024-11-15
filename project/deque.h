#ifndef PROJECT_DEQUE_H_
#define PROJECT_DEQUE_H_

#include <stdbool.h>
#include <stdint.h>

#define MSS 1012  // MSS = Maximum Segment Size (aka max length)
typedef struct {
    uint32_t ack;
    uint32_t seq;
    uint16_t length;
    uint8_t flags;
    uint8_t unused;
    uint8_t payload[MSS];
} packet;

typedef struct queue_t* q_handle_t;

q_handle_t q_init(uint32_t capacity);
void q_destroy(q_handle_t self);
void q_clear(q_handle_t self);
bool q_push_back(q_handle_t self, const packet *pkt);
bool q_push_front(q_handle_t self, const packet *pkt);
bool q_try_insert_keep_sorted(q_handle_t self, const packet *pkt);
bool q_pop_front(q_handle_t self, packet *pkt);
packet* q_pop_front_get_next(q_handle_t self);
packet* q_front(q_handle_t self);
size_t q_size(q_handle_t self);
bool q_full(q_handle_t self);
bool q_empty(q_handle_t self);
void q_set_capacity(q_handle_t self, uint32_t capacity);
void q_print(q_handle_t self, const char *str);

#endif  // PROJECT_DEQUE_H_

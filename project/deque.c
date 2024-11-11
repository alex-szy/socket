#include <stdio.h>
#include "deque.h"

struct queue_t {
    packet *queue;
    uint8_t front;
    uint8_t back;
    uint8_t size;
    uint8_t capacity;
};

static uint8_t increment(q_handle_t self, uint8_t idx) {
    if (idx == self->capacity - 1)
        return 0;
    else
        return ++idx;
}

static uint8_t decrement(q_handle_t self, uint8_t idx) {
    if (idx == 0)
        return self->capacity - 1;
    else
        return --idx;
}

q_handle_t q_init(uint8_t capacity) {
    q_handle_t self = malloc(sizeof(packet*) + 4);
    if (self != NULL) {
        self->back = 0;
        self->front = 0;
        self->size = 0;
        self->capacity = capacity;
        self->queue = malloc(sizeof(packet) * capacity);
        if (self->queue == NULL) {
            free(self);
            self = NULL;
        }
    }
    return self;
}

void q_destroy(q_handle_t self) {
    free(self->queue);
    free(self);
}

void q_clear(q_handle_t self) {
    self->front = 0;
    self->back = 0;
    self->size = 0;
}

bool q_push_back(q_handle_t self, packet *pkt) {
    if (q_full(self)) return false;
    self->queue[self->back] = *pkt;
    self->back = increment(self, self->back);
    self->size++;
    return true;
}

bool q_push_front(q_handle_t self, packet *pkt) {
    if (q_full(self)) return false;
    self->front = decrement(self, self->front);
    self->queue[self->front] = *pkt;
    self->size++;
    return true;
}

bool q_try_insert_keep_sorted(q_handle_t self, packet *pkt) {
    if (q_full(self)) return false;
    // Prevent duplicates
    for (uint8_t i = self->front; i != self->back; i = increment(self, i)) {
        if (self->queue[i].seq == pkt->seq) return false;
    }
    self->front = decrement(self, self->front);
    uint8_t curr = self->front;
    for (uint8_t i = 0; i < self->size; curr = increment(self, curr), i++) {
        uint8_t next = increment(self, curr);
        if (ntohl(self->queue[next].seq) > ntohl(pkt->seq)) break;
        self->queue[curr] = self->queue[next];
    }
    self->size++;
    self->queue[curr] = *pkt;
    return true;
}

packet* q_pop_front(q_handle_t self) {
    if (q_empty(self)) {
        return NULL;
    } else {
        packet* retval = &self->queue[self->front];
        self->front = increment(self, self->front);
        self->size--;
        return retval;
    }
}

packet* q_pop_front_get_next(q_handle_t self) {
    q_pop_front(self);
    return q_front(self);
}

packet* q_front(q_handle_t self) {
    if (q_empty(self))
        return NULL;
    else
        return &self->queue[self->front];
}

size_t q_size(q_handle_t self) {
    return self->size;
}

bool q_full(q_handle_t self) {
    return self->size == self->capacity;
}

bool q_empty(q_handle_t self) {
    return self->size == 0;
}

void q_print(q_handle_t self, const char *str) {
    fprintf(stderr, "%s", str);
    for (uint8_t i = 0, j = self->front;
        i < self->size; i++,
        j = increment(self, j)) {
        fprintf(stderr, " %u", ntohl(self->queue[j].seq));
    }
    fprintf(stderr, "\n");
}

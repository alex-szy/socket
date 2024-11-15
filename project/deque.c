#include <stdio.h>
#include "deque.h"

typedef struct queue_node_t node;

struct queue_node_t {
    packet* p;
    node* next;
    node* prev;
};

struct queue_t {
    node* dummy_head;
    uint32_t size;
    uint32_t capacity;
};

q_handle_t q_init(uint32_t capacity) {
    q_handle_t self = malloc(sizeof(struct queue_t));
    if (self != NULL) {
        self->dummy_head = malloc(sizeof(node));
        self->size = 0;
        self->capacity = capacity;
        if (self->dummy_head == NULL) {
            free(self);
            self = NULL;
        } else {
            self->dummy_head->p = NULL;
            self->dummy_head->next = self->dummy_head;
            self->dummy_head->prev = self->dummy_head;
        }
    }
    return self;
}

void q_destroy(q_handle_t self) {
    q_clear(self);
    free(self->dummy_head);
    free(self);
}

void q_clear(q_handle_t self) {
    for (node* curr = self->dummy_head->next; curr != self->dummy_head;) {
        node* next = curr->next;
        free(curr->p);
        free(curr);
        curr = next;
    }
    self->dummy_head->next = self->dummy_head;
    self->dummy_head->prev = self->dummy_head;
    self->size = 0;
}

static node* make_node(const packet *pkt) {
    node* new_node = malloc(sizeof(node));
    if (new_node == NULL) return NULL;
    new_node->p = malloc(sizeof(packet));
    if (new_node->p == NULL) {
        free(new_node);
        return NULL;
    } else {
        *(new_node->p) = *pkt;
    }
    return new_node;
}

bool q_push_back(q_handle_t self, const packet *pkt) {
    if (q_full(self)) return false;
    node* new_node = make_node(pkt);
    if (new_node == NULL)
        return false;
    node* before = self->dummy_head->prev;
    before->next = new_node;
    new_node->prev = before;
    self->dummy_head->prev = new_node;
    new_node->next = self->dummy_head;
    self->size++;
    return true;
}

bool q_push_front(q_handle_t self, const packet *pkt) {
    if (q_full(self)) return false;
    node* new_node = make_node(pkt);
    if (new_node == NULL)
        return false;
    node* after = self->dummy_head->next;
    after->prev = new_node;
    new_node->next = after;
    self->dummy_head->next = new_node;
    new_node->prev = self->dummy_head;
    self->size++;
    return true;
}

bool q_try_insert_keep_sorted(q_handle_t self, const packet *pkt) {
    if (q_full(self)) return false;
    // Prevent duplicates
    node* curr;
    for (curr = self->dummy_head->next; curr != self->dummy_head; curr = curr->next) {
        if (curr->p->seq == pkt->seq) return false;
        if (curr->p->seq > pkt->seq) break;
    }
    node* new_node = make_node(pkt);
    new_node->next = curr;
    new_node->prev = curr->prev;
    curr->prev->next = new_node;
    curr->prev = new_node;
    self->size++;
    return true;
}

bool q_pop_front(q_handle_t self, packet *pkt) {
    if (q_empty(self))
        return false;
    node* target = self->dummy_head->next;
    self->dummy_head->next = target->next;
    target->next->prev = self->dummy_head;
    if (pkt != NULL)
        *pkt = *(target->p);
    free(target->p);
    free(target);
    self->size--;
    return true;
}

packet* q_pop_front_get_next(q_handle_t self) {
    q_pop_front(self, NULL);
    return q_front(self);
}

packet* q_front(q_handle_t self) {
    return self->dummy_head->next->p;
}

size_t q_size(q_handle_t self) {
    return self->size;
}

bool q_full(q_handle_t self) {
    return self->size >= self->capacity;
}

bool q_empty(q_handle_t self) {
    return self->size == 0;
}

void q_set_capacity(q_handle_t self, uint32_t capacity) {
    self->capacity = capacity;
}

void q_print(q_handle_t self, const char *str) {
    fprintf(stderr, "%s", str);
    for (node* curr = self->dummy_head->next; curr != self->dummy_head; curr = curr->next)
        fprintf(stderr, " %u", curr->p->seq);
    fprintf(stderr, "\n");
}

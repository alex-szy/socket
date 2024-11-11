#ifndef COMMONH
#define COMMONH

#include <time.h>
#include "utils.h"
#include "deque.h"

typedef struct socketparams {
    int sockfd;
    uint32_t recv_seq;
    uint32_t send_seq;
    uint32_t recv_ack;
    uint32_t ack_count;
    q_handle_t send_q;
    q_handle_t recv_q;
    packet pkt_recv;
    packet pkt_send;
    clock_t before;
    struct sockaddr_in addr;
} params;

void p_init(params *p, int q_capacity, int argc, char *argv[], void (*construct_addr)(struct sockaddr_in*, int, char*[]));

void p_retransmit_on_timeout(params *p);
bool p_send_packet_from_stdin(params *p);
void p_retransmit_on_duplicate_ack(params *p);
bool p_handle_data_packet(params *p);
void p_clear_acked_packets_from_sbuf(params *p);
void p_send_empty_ack(params *p);

void p_listen(params *p);

#endif
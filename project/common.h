#ifndef PROJECT_COMMON_H_
#define PROJECT_COMMON_H_

#include <time.h>
#include <fcntl.h>
#include <arpa/inet.h>
#include <errno.h>
#include "deque.h"

#define PKT_SYN 1
#define PKT_ACK 2
#define RANDMASK ~(1 << 31)

static inline void die(const char s[]) {
    perror(s);
    exit(errno);
}

static inline int make_nonblock_socket() {
    /* 1. Create socket */
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
                            // use IPv4  use UDP

    // Make stdin and socket non-blocking
    int socket_nonblock = fcntl(sockfd, F_SETFL, O_NONBLOCK);
    if (socket_nonblock < 0) die("non-block socket");
    return sockfd;
}

static inline void stdin_nonblock() {
    int stdin_nonblock = fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    if (stdin_nonblock < 0) die("non-block stdin");
}

typedef struct {
    int sockfd;
    uint32_t recv_seq;
    uint32_t send_seq;
    uint32_t ack_count;
    q_handle_t send_q;
    q_handle_t recv_q;
    packet pkt_recv;
    packet pkt_send;
    clock_t before;
    struct sockaddr_in addr;
} params;

void p_init(params *p,
            int q_capacity,
            int argc,
            char *argv[],
            void (*construct_addr)(struct sockaddr_in*, int, char*[]));

int p_send_packet(params *p, const packet *pkt, const char* str);
int p_recv_packet(params *p);

void p_retransmit_on_timeout(params *p);
void p_send_and_enqueue_pkt_send(params *p);
bool p_send_payload_ack(params *p);
void p_handle_data_packet(params *p);
bool p_clear_acked_packets_from_sbuf(params *p);

void p_listen(params *p);

#endif  // PROJECT_COMMON_H_

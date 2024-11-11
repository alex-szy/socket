#ifndef PROJECT_UTILS_H_
#define PROJECT_UTILS_H_

#include <stdbool.h>
#include <arpa/inet.h>

#define PKT_SYN 1
#define PKT_ACK 2
#define RANDMASK ~(1 << 31)

#define MSS 1012  // MSS = Maximum Segment Size (aka max length)
typedef struct {
    uint32_t ack;
    uint32_t seq;
    uint16_t length;
    uint8_t flags;
    uint8_t unused;
    uint8_t payload[MSS];
} packet;

void die(const char s[]);

int make_nonblock_socket();
void stdin_nonblock();

int send_packet(int sockfd, struct sockaddr_in *serveraddr, packet *pkt, const char* str);
int recv_packet(int sockfd, struct sockaddr_in *serveraddr, packet *pkt);

int read_stdin_to_pkt(packet* pkt);
void write_pkt_to_stdout(packet* pkt);

#endif  // PROJECT_UTILS_H_

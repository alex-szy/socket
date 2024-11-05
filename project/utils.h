#ifndef UTILSH
#define UTILSH

#define PKT_SYN 1
#define PKT_ACK 2
#define RANDMASK ~(1 << 31)

#define MSS 1012 // MSS = Maximum Segment Size (aka max length)
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

int send_packet(int sockfd, struct sockaddr_in *serveraddr, packet *pkt);

int recv_packet(int sockfd, struct sockaddr_in *serveraddr, packet *pkt);

#endif
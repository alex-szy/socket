#ifndef UTILSH
#define UTILSH

#define PKT_SYN 1
#define PKT_ACK 2
#define RANDMASK ~(1 << 31)

#define Q_SIZE 20

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

typedef struct queue_t* q_handle_t;

q_handle_t q_init();
void q_destroy(q_handle_t self);
void q_clear(q_handle_t self);
void q_push(q_handle_t self, packet *pkt);
packet* q_pop(q_handle_t self);
packet* q_top(q_handle_t self);
size_t q_size(q_handle_t self);


#endif
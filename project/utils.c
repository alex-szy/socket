#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include "utils.h"

void die(const char s[]) {
	perror(s);
	exit(errno);
}

int make_nonblock_socket() {
	/* 1. Create socket */
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
							// use IPv4  use UDP

	// Make stdin and socket non-blocking
	int socket_nonblock = fcntl(sockfd, F_SETFL, O_NONBLOCK);
	if (socket_nonblock < 0) die("non-block socket");
	return sockfd;
}

void stdin_nonblock() {
	int stdin_nonblock = fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
	if (stdin_nonblock < 0) die("non-block stdin");
}

int send_packet(int sockfd, struct sockaddr_in *serveraddr, packet *pkt) {
	fprintf(stderr, "Sent packet: ack %08x, seq %08x, flags %x, length %d\n", pkt->ack, pkt->seq, pkt->flags, pkt->length);
	// drop packet 75%
	if (rand() > RANDMASK >> 1) return 1;
	socklen_t serversize = sizeof(*serveraddr);
	int did_send = sendto(sockfd, pkt, sizeof(*pkt),
						// socket  send data   how much to send
							0, (struct sockaddr*) serveraddr,
						// flags   where to send
							serversize);
	if (did_send < 0) die("send");
	return did_send;
}

int recv_packet(int sockfd, struct sockaddr_in *serveraddr, packet *pkt) {
	socklen_t serversize = sizeof(*serveraddr);
	/* 5. Listen for response from server */
	int bytes_recvd = recvfrom(sockfd, pkt, sizeof(*pkt),
							// socket  store data  how much
								0, (struct sockaddr*) serveraddr,
								&serversize);
	// Error if bytes_recvd < 0 :(
	if (bytes_recvd < 0 && errno != EAGAIN) die("receive");
	if (bytes_recvd > 0)
		fprintf(stderr, "Recv packet: seq %08x, ack %08x, flags %x, length %d\n", pkt->seq, pkt->ack, pkt->flags, pkt->length);
	return bytes_recvd;
}

int read_packet_payload(packet *pkt) {
	int bytes_read = read(STDIN_FILENO, &pkt->payload, MSS);
	if (bytes_read >= 0)
		pkt->length = bytes_read;
	else
		pkt->length = 0;
	return bytes_read;
}

void write_packet_payload(packet *pkt) {
	write(STDOUT_FILENO, &pkt->payload, pkt->length);
}

struct queue_t {
	packet queue[Q_SIZE];
	uint8_t front;
	uint8_t back;
	uint8_t size;
};

static void increment(uint8_t *idx) {
	if (*idx == Q_SIZE - 1)
		*idx = 0;
	else
		(*idx)++;
}

static void decrement(uint8_t *idx) {
	if (*idx == 0)
		*idx = Q_SIZE - 1;
	else
		(*idx)--;
}

q_handle_t q_init() {
	q_handle_t self = calloc(Q_SIZE, sizeof(packet));
	return self;
}

void q_destroy(q_handle_t self) {
	free(self);
}

void q_clear(q_handle_t self) {
	self->front = 0;
	self->back = 0;
}

void q_push_back(q_handle_t self, packet *pkt) {
	if (q_full(self)) {
		increment(&self->front);
		self->size--;
		// fprintf(stderr, "Dropped packet because buffer was full\n");
	}
	self->queue[self->back] = *pkt;
	increment(&self->back);
	self->size++;
}

void q_push_front(q_handle_t self, packet *pkt) {
	if (q_full(self)) {
		decrement(&self->back);
		self->size--;
	}
	decrement(&self->front);
	self->queue[self->front] = *pkt;
	self->size++;
}

void q_insert_keep_sorted(q_handle_t self, packet *pkt) {
	// init new queue
	// while seq number of front packet is less, put the packets into a temp queue
	q_handle_t temp = q_init();
	packet *front = q_front(self);
	while (front != NULL && front->seq < pkt->seq) {
		q_push_front(temp, q_pop_front(self));
		front = q_front(self);
	}
	q_push_front(self, pkt);
	front = q_front(temp);
	while (front != NULL) {
		q_push_front(self, q_pop_front(temp));
		front = q_front(temp);
	}
	self->size++;
	q_destroy(temp);
}

packet* q_pop_front(q_handle_t self) {
	if (q_empty(self))
		return NULL;
	else {
		packet* retval = &self->queue[self->front];
		increment(&self->front);
		self->size--;
		return retval;
	}
}

packet* q_front(q_handle_t self) {
	if (self->size == 0)
		return NULL;
	else
		return &self->queue[self->front];
}

size_t q_size(q_handle_t self) {
	return self->size;
}

bool q_full(q_handle_t self) {
	return q_size(self) == Q_SIZE;
}

bool q_empty(q_handle_t self) {
	return q_size(self) == 0;
}

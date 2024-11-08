#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include "utils.h"

static void print_packet(packet *pkt, const char* op) {
	fprintf(stderr, "%s %d ACK %d SIZE %d FLAGS", op, ntohl(pkt->seq), ntohl(pkt->ack), ntohs(pkt->length));
	switch (pkt->flags) {
		case PKT_SYN:
			fprintf(stderr, " SYN\n");
			break;
		case PKT_ACK:
			fprintf(stderr, " ACK\n");
			break;
		case PKT_ACK | PKT_SYN:
			fprintf(stderr, " SYN ACK\n");
			break;
		default:
			fprintf(stderr, " NONE\n");
	}
}

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

int send_packet(int sockfd, struct sockaddr_in *serveraddr, packet *pkt, const char* str) {
	print_packet(pkt, str);
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
		print_packet(pkt, "RECV");
	return bytes_recvd;
}

int read_stdin_to_pkt(packet *pkt) {
	int bytes_read = read(STDIN_FILENO, &pkt->payload, MSS);
	if (bytes_read >= 0)
		pkt->length = htons(bytes_read);
	else
		pkt->length = 0;
	return bytes_read;
}

void write_pkt_to_stdout(packet *pkt) {
	write(STDOUT_FILENO, &pkt->payload, ntohs(pkt->length));
}

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
	if (q_empty(self))
		return NULL;
	else {
		packet* retval = &self->queue[self->front];
		self->front = increment(self, self->front);
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
	return self->size == self->capacity;
}

bool q_empty(q_handle_t self) {
	return self->size == 0;
}

void q_print(q_handle_t self) {
	for (uint8_t i = 0, j = self->front; i < self->size; i++, j = increment(self, j)) {
		fprintf(stderr, " %u", ntohl(self->queue[j].seq));
	}
	fprintf(stderr, "\n");
}

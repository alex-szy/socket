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

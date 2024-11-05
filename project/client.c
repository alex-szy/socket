#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <fcntl.h>
#include <time.h>
#include "utils.h"


static int construct_serveraddr(struct sockaddr_in *serveraddr, int argc, char *argv[]) {
	// Construct server address
	serveraddr->sin_family = AF_INET; // use IPv4
	if (argc > 1 && strcmp(argv[1], "localhost"))
		serveraddr->sin_addr.s_addr = inet_addr(argv[1]);
	else
		serveraddr->sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	// Set sending port
	int PORT;
	if (argc > 2)
		PORT = atoi(argv[2]);
	else
		PORT = 8080;
	serveraddr->sin_port = htons(PORT);
	return 1;
}

int main(int argc, char *argv[]) {
	// Seed the random number generator
	srand(0);

	// Create socket
	int sockfd = make_nonblock_socket();

	/* 2. Construct server address */
	struct sockaddr_in serveraddr;
	construct_serveraddr(&serveraddr, argc, argv);
	
	socklen_t serversize = sizeof(serveraddr); // Temp buffer for recvfrom API

	/* 4. Create buffer to store data */
	int BUF_SIZE = 1024;
	uint32_t recv_seq, send_seq;
	bool acked = false;
	send_seq = rand() & RANDMASK;

	// Create the SYN packet
	packet send_pkt = {0};
	packet recv_pkt = {0};
	send_pkt.length = 0;
	send_pkt.seq = send_seq;
	fprintf(stderr, "send_seq is: 0x%x\n", send_seq);
	send_pkt.flags = PKT_SYN;

	clock_t before = clock();


	for (;;) {
		if (acked) {
			fprintf(stderr, "Handshake complete!\n");
			return 0;
		} else {
			clock_t now = clock();
			if (now - before > CLOCKS_PER_SEC) { // 1 second timer
				before = now;
				// send the syn packet
				send_packet(sockfd, &serveraddr, &send_pkt);
				fprintf(stderr, "Sent syn packet\n");
			}
			if (recv_packet(sockfd, &serveraddr, &recv_pkt)) {
				// check for ack packet
				if (recv_pkt.flags & PKT_ACK) {
					// Syn ack
					if (recv_pkt.flags & PKT_SYN) {
						// send syn ack ack
						fprintf(stderr, "Received syn ack packet\n");
						recv_seq = recv_pkt.seq + 1;
						send_seq = recv_pkt.ack;
						send_pkt.ack = recv_seq;
						send_pkt.seq = send_seq;
						send_pkt.flags = PKT_ACK;
						send_packet(sockfd, &serveraddr, &send_pkt);
						fprintf(stderr, "Send syn ack ack packet\n");
						acked = true;
					} else { // normal ack
						// TODO: data transmission
					}
				} else {
					// not an ack, means data packet
				}
			}

		}
		
		// /* Send data to server */
		// int bytes_read = read(STDIN_FILENO, &send_pkt, MSS);
		// if (bytes_read > 0) { // Send the data (with ack, etc.)
		// 	send(sockfd, &serveraddr, &send_pkt);
		// } else if (bytes_read < 0 && errno != EAGAIN) die("stdin");


		
	}
}

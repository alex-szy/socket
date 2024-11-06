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
	uint32_t recv_seq; // seq number of the incoming packets
	uint32_t send_seq; // seq number of outgoing packets
	bool recved_syn_ack = false;
	send_seq = rand() & RANDMASK;

	// Create the SYN packet
	packet pkt_send = {0};
	packet pkt_recv = {0};
	pkt_send.length = 0;
	pkt_send.seq = send_seq;
	pkt_send.flags = PKT_SYN;

	clock_t before = clock();

	q_handle_t send_q = q_init();
	q_handle_t recv_q = q_init();
	q_push(send_q, &pkt_send);

	// pure acks no consume sequence numbers

	for (;;) {
		clock_t now = clock();
		// Packet retransmission
		if (now - before > CLOCKS_PER_SEC) { // 1 second timer
			before = now;
			// send the packet with lowest seq number in sending buffer, which is probably the top packet
			packet* send = q_top(send_q);
			if (send != NULL)
				send_packet(sockfd, &serveraddr, send);
		}

		if (recv_packet(sockfd, &serveraddr, &pkt_recv) > 0) {
			// check for ack packet
			if (pkt_recv.flags & PKT_ACK) {
				// for pure acks, do not respond. only clear send packets from queue
				// reset the timer
				before = clock();
				// pop all the packets from the q which have seq numbers less than the ack of this packet
				// if there's nothing to acknowledge, does nothing
				packet *pkt = q_top(send_q);
				while (pkt != NULL && pkt->seq < pkt_recv.ack) {
					q_pop(send_q);
					pkt = q_top(send_q);
				}
			}
				
			// data packet or syn ack need to be acked
			if (pkt_recv.flags & PKT_SYN) {
				if (!recved_syn_ack) {
					recv_seq = pkt_recv.seq + 1;
					send_seq++;
					recved_syn_ack = true;
					fprintf(stderr, "Handshake complete!\n");
				}
				pkt_send.ack = pkt_recv.seq + 1;
				pkt_send.seq = send_seq;
				pkt_send.length = 0;
				pkt_send.flags = PKT_ACK;
				send_packet(sockfd, &serveraddr, &pkt_send);
			}

			if (!(pkt_recv.flags & PKT_ACK)) {

			}
	}
		
		// /* Send data to server */
		// int bytes_read = read(STDIN_FILENO, &send_pkt, MSS);
		// if (bytes_read > 0) { // Send the data (with ack, etc.)
		// 	send(sockfd, &serveraddr, &send_pkt);
		// } else if (bytes_read < 0 && errno != EAGAIN) die("stdin");


		

}}

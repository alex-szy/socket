#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdbool.h>
#include <time.h>
#include "utils.h"


static int construct_serveraddr(struct sockaddr_in *servaddr, int argc, char *argv[]) {
	servaddr->sin_family = AF_INET; // use IPv4
	servaddr->sin_addr.s_addr = INADDR_ANY; // accept all connections
							// same as inet_addr("0.0.0.0")
									 // "Address string to network bytes"
	// Set receiving port
	int PORT;
	if (argc > 1)
		PORT = atoi(argv[1]);
	else
		PORT = 8080;
	servaddr->sin_port = htons(PORT); // Big endian
	return 1;
}


int main(int argc, char *argv[]) {
	// Seed the random number generator
	srand(2);

	/* 1. Create socket */
	int sockfd = make_nonblock_socket();
	stdin_nonblock();

	/* 2. Construct our address */
	struct sockaddr_in servaddr;
	construct_serveraddr(&servaddr, argc, argv);
	

	/* 3. Let operating system know about our config */
	int did_bind = bind(sockfd, (struct sockaddr*) &servaddr,
						sizeof(servaddr));
	// Error if did_bind < 0 :(
	if (did_bind < 0) return errno;

	/* 4. Create buffer to store data */
	int BUF_SIZE = 1024;
	char client_buf[BUF_SIZE];
	char server_buf[BUF_SIZE];

	uint32_t recv_seq, send_seq;
	send_seq = rand() & RANDMASK;
	packet pkt_recv = {};
	packet pkt_send = {};

	q_handle_t send_q = q_init();
	q_handle_t recv_q = q_init();

	struct sockaddr_in clientaddr; // Same information, but about client

	bool ready_to_send = false;
	bool syn = false;

	clock_t before = clock();

	for (;;) {/* 5. Listen for data from clients */

		clock_t now = clock();
		// Packet retransmission
		if (now - before > CLOCKS_PER_SEC) { // 1 second timer
			before = now;
			// send the packet with lowest seq number in sending buffer, which is probably the top packet
			packet* send = q_top(send_q);
			// fprintf(stderr, "q size: %ld\n", q_size(send_q));
			if (send != NULL)
				send_packet(sockfd, &clientaddr, send);
		}

		int bytes_recvd = recv_packet(sockfd, &clientaddr, &pkt_recv);
		if (bytes_recvd > 0) {
			if (pkt_recv.flags & PKT_SYN) {
				// syn packet received
				pkt_send.ack = pkt_recv.seq + 1;
				pkt_send.seq = send_seq;
				pkt_send.length = 0;
				pkt_send.flags = PKT_ACK | PKT_SYN;
				if (!syn) {
					recv_seq = pkt_recv.seq + 1;
					syn = true;
					q_push(send_q, &pkt_send);
				}
				send_packet(sockfd, &clientaddr, &pkt_send);
			} else if (pkt_recv.flags & PKT_ACK) { // normal ack
				// remove all acked packets from send queue
				if (!ready_to_send) {
					ready_to_send = true;
					fprintf(stderr, "Handshake complete!\n");
				}
				packet *pkt = q_top(send_q);
				while (pkt != NULL && pkt->seq < pkt_recv.ack) {
					q_pop(send_q);
					pkt = q_top(send_q);
				}
			}
		} else {
			// nothing received, check for data to send
			if (q_size(send_q) < Q_SIZE){
				int bytes_read = read_packet_payload(&pkt_send);
				if (bytes_read > 0) {
					pkt_send.seq = send_seq;
					send_seq++;
					pkt_send.flags = 0; // data packet

					send_packet(sockfd, &servaddr, &pkt_send);
					q_push(send_q, &pkt_send);
				}
			}
		}
	}
		

		// if (bytes_recvd > 0) {
		// 	// Write to stdout
		//  	write(1, client_buf, bytes_recvd);
		// 	char* client_ip = inet_ntoa(clientaddr.sin_addr);
		// 					// "Network bytes to address string"
		// 	int client_port = ntohs(clientaddr.sin_port); // Little endian
		// 	fprintf(stderr, "%s:%d\n", client_ip, client_port);
		// 	// Got the address, now ready to send
		// 	ready_to_send = true;

		// } else if (bytes_recvd < 0 && errno != EAGAIN) die("receive");


		// /* 7. Send data back to client */
		// // We have not received the address yet, cannot send data
		// if (!ready_to_send) continue;

		// int bytes_read = read(STDIN_FILENO, server_buf, BUF_SIZE);
		// if (bytes_read > 0) {
		// 	int did_send = sendto(sockfd, server_buf, bytes_read,
		// 						// socket  send data   how much to send
		// 						0, (struct sockaddr*) &clientaddr,
		// 						// flags   where to send
		// 						sizeof(clientaddr));
		// 	if (did_send < 0) die("send");

		// } else if (bytes_read < 0 && errno != EAGAIN) die("stdin");
}

// TODO: need waiting for stdin not to interfere with socket
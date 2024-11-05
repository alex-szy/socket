#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdbool.h>
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
	fprintf(stderr, "send seq is: 0x%x\n", send_seq);
	packet pkt_recv, pkt_send;

	struct sockaddr_in clientaddr; // Same information, but about client

	bool ready_to_send = false;
	bool syn = false;

	for (;;) {/* 5. Listen for data from clients */
		if (ready_to_send) {
			fprintf(stderr, "Handshake complete!\n");
			return 0;
		}
		int bytes_recvd = recv_packet(sockfd, &clientaddr, &pkt_recv);
		if (bytes_recvd > 0) {
			if (pkt_recv.flags & PKT_SYN) {
				// syn packet received
				fprintf(stderr, "Received syn packet\n");
				recv_seq = pkt_recv.seq + 1;
				pkt_send.ack = recv_seq;
				pkt_send.seq = send_seq;
				pkt_send.length = 0;
				pkt_send.flags = PKT_ACK | PKT_SYN;
				syn = true;
				send_packet(sockfd, &clientaddr, &pkt_send);
				fprintf(stderr, "Sent syn ack packet\n");
			} else if (pkt_recv.flags & PKT_ACK) { // normal ack

				if (pkt_recv.ack > send_seq + 1) {
					// reject
				} else if (pkt_recv.ack < send_seq + 1) { // go back
					recv_seq = pkt_recv.seq + 1;
					send_seq = pkt_recv.ack;

					
				} else { // correct packet number
					ready_to_send = true;
					// recv_seq = pkt_recv.seq + 1;
					// send_seq = pkt_recv.ack;

					// pkt_send.ack = recv_seq;
					// pkt_send.seq = send_seq;
					// pkt_send.length = 0;
					// pkt_send.flags
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
}

// TODO: need waiting for stdin not to interfere with socket
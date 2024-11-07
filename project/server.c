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

static void bind_socket(int sockfd, int argc, char *argv[]) {
	// Construct the address
	struct sockaddr_in servaddr;
	construct_serveraddr(&servaddr, argc, argv);
	int did_bind = bind(sockfd, (struct sockaddr*) &servaddr,
						sizeof(servaddr));
	// Error if did_bind < 0 :(
	if (did_bind < 0) die("bind socket");
}


int main(int argc, char *argv[]) {
	// Seed the random number generator
	srand(2);

	/* 1. Create socket */
	int sockfd = make_nonblock_socket();
	stdin_nonblock();
	bind_socket(sockfd, argc, argv);
	
	/* 4. Create the send and receive packets */
	packet pkt_recv = {};
	packet pkt_send = {};

	uint32_t recv_seq;
	uint32_t send_seq = rand() & RANDMASK;

	q_handle_t send_q = q_init();
	q_handle_t recv_q = q_init();

	struct sockaddr_in clientaddr; // Struct to store client address

	bool ready_to_send = false;
	bool syn = false;

	int ack_count = 0;
	uint32_t recv_ack = -1;

	// Start the clock
	clock_t before = clock();

	for (;;) {/* 5. Listen for data from clients */

		clock_t now = clock();
		// Packet retransmission
		if (now - before > CLOCKS_PER_SEC) { // 1 second timer
			before = now;
			// send the packet with lowest seq number in sending buffer, which is probably the top packet
			packet* send = q_front(send_q);
			// fprintf(stderr, "q size: %ld\n", q_size(send_q));
			if (send != NULL)
				send_packet(sockfd, &clientaddr, send);
		}

		if (recv_packet(sockfd, &clientaddr, &pkt_recv) <= 0) {
			// if handshake done, there's space in the queue, read a packet and send as a data packet
			if (ready_to_send && !q_full(send_q) && read_packet_payload(&pkt_send) > 0) { 
				pkt_send.ack = recv_seq;
				pkt_send.seq = send_seq;
				pkt_send.flags = 0;
				q_push_back(send_q, &pkt_send);
				send_seq++;
				send_packet(sockfd, &clientaddr, &pkt_send);
			}
		} else { // packet received
			if (pkt_recv.flags & PKT_ACK) { // if ack flag is set, this is ack or syn ack
				// reset the timer
				before = clock();
				// pop all the packets from the q which have seq numbers less than the ack of this packet
				packet *pkt = q_front(send_q);
				while (pkt != NULL && pkt->seq < pkt_recv.ack) {
					q_pop_front(send_q);
					pkt = q_front(send_q);
				}
				// retransmit if 3 same acks in a row
				if (pkt_recv.ack == recv_ack) {
					ack_count++;
					if (ack_count == 3) {
						ack_count = 0;
						packet* send = q_front(send_q);
						if (send != NULL)
							send_packet(sockfd, &clientaddr, send);
					}
				} else {
					recv_ack = pkt_recv.ack;
				}
			}

			if (pkt_recv.flags & PKT_SYN || pkt_recv.length != 0) { // syn packet or payload packets need to be acknowledged
				pkt_send.flags = PKT_ACK;
				if (pkt_recv.flags & PKT_SYN) { // syn packet, need to ack
					if (!syn) { // first syn ack packet should set recv_seq correctly
						recv_seq = pkt_recv.seq + 1;
						syn = true;
						pkt_send.flags |= PKT_SYN;
					}
				} else { // packet with payload
					if (pkt_recv.seq == recv_seq) { // write contents of packet if expected
						write_packet_payload(&pkt_recv);
						recv_seq++; // next packet

						// loop through sorted packet buffer and pop off next packets
						packet *pkt = q_front(recv_q);
						while (pkt != NULL && pkt->seq == recv_seq) {
							write_packet_payload(pkt);
							q_pop_front(recv_q);
							pkt = q_front(recv_q);
							recv_seq++;
						}
					} else { // unexpected packet, insert into buffer
						q_insert_keep_sorted(recv_q, &pkt_recv);
					}
				}

				pkt_send.ack = recv_seq;
				pkt_send.seq = send_seq;
				pkt_send.length = 0;

				// If the send queue isn't full yet and we have data to send, read data into payload
				// Or if we're responding to a syn packet
				if (syn && !ready_to_send || !q_full(send_q) && read_packet_payload(&pkt_send) > 0) {
					if (!ready_to_send) {
						ready_to_send = true;
						fprintf(stderr, "Handshake complete!\n");
					}
					q_push_back(send_q, &pkt_send);
					send_seq++;
				}

				send_packet(sockfd, &clientaddr, &pkt_send);
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
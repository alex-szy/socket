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
#include "deque.h"


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
	packet pkt_recv = {0};
	packet pkt_send = {0};

	// in network order
	uint32_t recv_seq;
	uint32_t send_seq = htonl(rand() & RANDMASK);

	q_handle_t send_q = q_init(20);
	q_handle_t recv_q = q_init(20);
	if (send_q == NULL || recv_q == NULL) die("queue initialization");

	struct sockaddr_in clientaddr; // Struct to store client address

	int ack_count = 0;
	uint32_t recv_ack = -1;

	// Start the clock
	clock_t before = clock();

	for (;;) { // listen for syn packet
		if (recv_packet(sockfd, &clientaddr, &pkt_recv) <= 0) continue;
		if (pkt_recv.flags & PKT_SYN) {
			recv_seq = htonl(ntohl(pkt_recv.seq)+1);
			pkt_send.flags = PKT_ACK | PKT_SYN;
			pkt_send.ack = recv_seq;
			pkt_send.seq = send_seq;
			send_seq = htonl(ntohl(send_seq)+1);
			q_push_back(send_q, &pkt_send);
			q_print(send_q, "SBUF");
			send_packet(sockfd, &clientaddr, q_front(send_q), "SEND");
			break;
		}
	}

	for (;;) { // listen for syn ack ack packet, may have payload
		clock_t now = clock();
		if (recv_packet(sockfd, &clientaddr, &pkt_recv) <= 0) {
			if (now - before > CLOCKS_PER_SEC) {
				before = now;
				send_packet(sockfd, &clientaddr, q_front(send_q), "RTOS"); // retransmit
			}
		} else {
			if (pkt_recv.flags & PKT_SYN) { // if we still get SYN packets
				send_packet(sockfd, &clientaddr, q_front(send_q), "SEND"); // ack, not retransmit
			} else if (pkt_recv.flags & PKT_ACK) { // syn ack ack packet, may have payload
				// TODO: still send ack if seq has already been received
				if (pkt_recv.seq != recv_seq) { // not the packet we want
					if (pkt_recv.length != 0) {
						if (ntohl(pkt_recv.seq) > ntohl(recv_seq))
							q_try_insert_keep_sorted(recv_q, &pkt_recv);
						send_packet(sockfd, &clientaddr, q_front(send_q), "SEND"); // ack, not retransmit
					}
				} else {
					q_pop_front(send_q);
					if (pkt_recv.length == 0) { // incoming zero length
						recv_seq = htonl(ntohl(recv_seq)+1);
					} else {
						q_push_front(recv_q, &pkt_recv);
						for (packet *pkt = q_front(recv_q); pkt != NULL && pkt->seq == recv_seq; pkt = q_pop_front_get_next(recv_q)) {
							recv_seq = htonl(ntohl(recv_seq) + ntohs(pkt->length));
							write_pkt_to_stdout(pkt);
						}
						pkt_send.flags = PKT_ACK;
						pkt_send.ack = recv_seq;
						pkt_send.seq = 0;
						int bytes = read_stdin_to_pkt(&pkt_send);
						if (bytes > 0) { // if there's payload
							pkt_send.seq = send_seq;
							send_seq = htonl(ntohl(send_seq)+bytes);
							q_push_back(send_q, &pkt_send);
							q_print(send_q, "SBUF");
							send_packet(sockfd, &clientaddr, &pkt_send, "SEND");
						}
					}
					break;
				}
			}
		}
	}

	for (;;) { // can transmit now, also can ignore syn packets

		clock_t now = clock();
		// Packet retransmission
		if (now - before > CLOCKS_PER_SEC) { // 1 second timer
			before = now;
			// send the packet with lowest seq number in sending buffer, which is probably the top packet
			packet* send = q_front(send_q);
			// fprintf(stderr, "q size: %ld\n", q_size(send_q));
			if (send != NULL)
				send_packet(sockfd, &clientaddr, send, "RTOS");
		}

		if (recv_packet(sockfd, &clientaddr, &pkt_recv) <= 0) {
			if (!q_full(send_q)) { 
				int bytes = read_stdin_to_pkt(&pkt_send);
				if (bytes > 0) {
					pkt_send.ack = recv_seq;
					pkt_send.seq = send_seq;
					pkt_send.flags = PKT_ACK;
					q_push_back(send_q, &pkt_send);
					q_print(send_q, "SBUF");
					send_seq = htonl(ntohl(send_seq)+bytes);
					send_packet(sockfd, &clientaddr, &pkt_send, "SEND");
				}
			}
		} else { // packet received
			// reset the timer
			before = clock();
			// pop all the packets from the q which have seq numbers less than the ack of this packet
			
			for (packet *pkt = q_front(send_q); pkt != NULL && ntohl(pkt->seq) < ntohl(pkt_recv.ack); pkt = q_pop_front_get_next(send_q));

			// retransmit if 3 same acks in a row
			if (pkt_recv.ack == recv_ack) {
				ack_count++;
				if (ack_count == 3) {
					ack_count = 0;
					packet* send = q_front(send_q);
					if (send != NULL)
						send_packet(sockfd, &clientaddr, send, "DUPS");
				}
			} else {
				recv_ack = pkt_recv.ack;
			}

			if (pkt_recv.length == 0) continue; // only payload packets need to be acknowledged

			if (pkt_recv.seq == recv_seq) { // write contents of packet if expected
				write_pkt_to_stdout(&pkt_recv);
				recv_seq = htonl(ntohl(recv_seq)+ntohs(pkt_recv.length)); // next packet

				// loop through sorted packet buffer and pop off next packets
				
				for (packet *pkt = q_front(recv_q); pkt != NULL && pkt->seq == recv_seq; pkt = q_pop_front_get_next(recv_q)) {
					write_pkt_to_stdout(pkt);
					recv_seq = htonl(ntohl(recv_seq)+ntohs(pkt->length));
				}
			} else if (ntohl(pkt_recv.seq) > ntohl(recv_seq)) { // unexpected packet, insert into buffer
				q_try_insert_keep_sorted(recv_q, &pkt_recv);
			}
			

			pkt_send.flags = PKT_ACK;
			pkt_send.ack = recv_seq;
			pkt_send.seq = 0;
			pkt_send.length = 0;

			// If the send queue isn't full yet and we have data to send, read data into payload
			// Or if we're responding to a syn packet
			if (!q_full(send_q)) {
				int bytes = read_stdin_to_pkt(&pkt_send); 
				if (bytes > 0) {
					pkt_send.seq = send_seq;
					q_push_back(send_q, &pkt_send);
					q_print(send_q, "SBUF");
					send_seq = htonl(ntohl(send_seq)+bytes);
				}
			}

			send_packet(sockfd, &clientaddr, &pkt_send, "SEND");
		}
	}
}

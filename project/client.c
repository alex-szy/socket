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
	stdin_nonblock();

	/* 2. Construct server address */
	struct sockaddr_in serveraddr;
	construct_serveraddr(&serveraddr, argc, argv);

	// in network order
	uint32_t recv_seq;
	// expected seq number of incoming packet, increment when received expected incoming packet, or when popping packet off from recv_q
	uint32_t send_seq = htonl(rand() & RANDMASK);
	// seq number of outgoing packets, increment after sending syn or nonzero length packet

	bool recved_syn_ack = false;
	bool sent_syn_ack_ack = false;

	// Create the SYN packet
	packet pkt_send = {0};
	pkt_send.length = 0;
	pkt_send.seq = send_seq;
	pkt_send.flags = PKT_SYN;

	packet pkt_recv = {0};

	// Send and receive queues
	q_handle_t send_q = q_init(20);
	q_handle_t recv_q = q_init(20);
	if (send_q == NULL || recv_q == NULL) die("queue initialization");

	// Push the syn packet onto the queue and send it
	q_push_back(send_q, &pkt_send);
	send_packet(sockfd, &serveraddr, &pkt_send, "SEND");
	send_seq = htonl(ntohl(send_seq)+1);

	int ack_count = 0;
	uint32_t recv_ack = -1;

	// Start the clock
	clock_t before = clock();

	for (;;) {
		clock_t now = clock();
		// retransmit if 1 second timer expired
		if (now - before > CLOCKS_PER_SEC) {
			before = now;
			packet* send = q_front(send_q);
			if (send != NULL)
				send_packet(sockfd, &serveraddr, send, "RTOS");
		}

		if (recv_packet(sockfd, &serveraddr, &pkt_recv) <= 0) { // no packet received
			// if handshake is done and there's space in the queue, read a packet and send as a data packet
			if (sent_syn_ack_ack && !q_full(send_q)) {
				int bytes = read_stdin_to_pkt(&pkt_send);
				if (bytes > 0) {
					pkt_send.ack = recv_seq;
					pkt_send.seq = send_seq;
					pkt_send.flags = PKT_ACK;
					q_push_back(send_q, &pkt_send);
					
					send_seq = htonl(ntohl(send_seq)+bytes);
					send_packet(sockfd, &serveraddr, &pkt_send, "SEND");
				}
			}
		} else { // packet received
			if (pkt_recv.flags & PKT_ACK) { // if ack flag is set, this is ack or syn ack
				// reset the timer
				before = clock();
				// pop all the packets from the q which have seq numbers less than the ack of this packet
				packet *pkt = q_front(send_q);
				while (pkt != NULL && ntohl(pkt->seq) < ntohl(pkt_recv.ack)) {
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
							send_packet(sockfd, &serveraddr, send, "DUPS");
					}
				} else {
					recv_ack = pkt_recv.ack;
				}
			}

			if (pkt_recv.flags & PKT_SYN || pkt_recv.length != 0) { // syn ack packet or payload packets need to be acknowledged
				if (pkt_recv.flags & PKT_SYN) { // syn ack packet, need to ack
					if (!recved_syn_ack) { // first syn ack packet should set recv_seq correctly
						recv_seq = htonl(ntohl(pkt_recv.seq) + 1);
						recved_syn_ack = true;
					}
				} else { // packet with payload
					if (pkt_recv.seq == recv_seq) { // write contents of packet if expected
						write_pkt_to_stdout(&pkt_recv);
						recv_seq = htonl(ntohl(recv_seq)+ntohs(pkt_recv.length)); // next packet

						// loop through sorted packet buffer and pop off next packets
						packet *pkt = q_front(recv_q);
						while (pkt != NULL && pkt->seq == recv_seq) {
							write_pkt_to_stdout(pkt);
							recv_seq = htonl(ntohl(recv_seq)+ntohs(pkt->length));
							q_pop_front(recv_q);
							pkt = q_front(recv_q);
						}
					} else if (ntohl(pkt_recv.seq) > ntohl(recv_seq)) { // unexpected unacked packet, insert into buffer
						q_try_insert_keep_sorted(recv_q, &pkt_recv);
						fprintf(stderr, "RBUF"); q_print(recv_q);
					}
				}

				pkt_send.ack = recv_seq;
				pkt_send.seq = send_seq;
				pkt_send.flags = PKT_ACK;
				pkt_send.length = 0;

				// If the send queue isn't full yet and we have data to send, read data into payload
				
				bool payload = false;
				if (!q_full(send_q)) {
					int bytes = read_stdin_to_pkt(&pkt_send);
					if (bytes > 0) {
						payload = true;
						q_push_back(send_q, &pkt_send);
						send_seq = htonl(ntohl(send_seq)+bytes);
					}
				}
				if (!sent_syn_ack_ack && recved_syn_ack) {
					sent_syn_ack_ack = true;
					if (!payload)
						send_seq = htonl(ntohl(send_seq)+1);
					// fprintf(stderr, "Handshake complete!\n");
				} else if (!payload)
					pkt_send.seq = 0;

				send_packet(sockfd, &serveraddr, &pkt_send, "SEND");
			}
		}
	}
}

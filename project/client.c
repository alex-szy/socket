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

	uint32_t recv_seq; 
	// expected seq number of incoming packet, increment when received expected incoming packet, or when popping packet off from recv_q
	uint32_t send_seq = rand() & RANDMASK; 
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
	q_handle_t send_q = q_init();
	q_handle_t recv_q = q_init();

	// Push the syn packet onto the queue and send it
	q_push_back(send_q, &pkt_send);
	send_packet(sockfd, &serveraddr, &pkt_send);
	send_seq++;

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
				send_packet(sockfd, &serveraddr, send);
		}

		if (recv_packet(sockfd, &serveraddr, &pkt_recv) <= 0) { // no packet received
			// if handshake is done and there's space in the queue, read a packet and send as a data packet
			if (sent_syn_ack_ack && !q_full(send_q) && read_packet_payload(&pkt_send) > 0) { 
				pkt_send.ack = recv_seq;
				pkt_send.seq = send_seq;
				pkt_send.flags = 0;
				q_push_back(send_q, &pkt_send);
				send_seq++;
				send_packet(sockfd, &serveraddr, &pkt_send);
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
							send_packet(sockfd, &serveraddr, send);
					}
				} else {
					recv_ack = pkt_recv.ack;
				}
			}

			if (pkt_recv.flags & PKT_SYN || pkt_recv.length != 0) { // syn ack packet or payload packets need to be acknowledged
				if (pkt_recv.flags & PKT_SYN) { // syn ack packet, need to ack
					if (!recved_syn_ack) { // first syn ack packet should set recv_seq correctly
						recv_seq = pkt_recv.seq + 1;
						recved_syn_ack = true;
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
				pkt_send.flags = PKT_ACK;
				pkt_send.length = 0;

				// If the send queue isn't full yet and we have data to send, read data into payload
				if (recved_syn_ack && !sent_syn_ack_ack || !q_full(send_q) && read_packet_payload(&pkt_send) > 0) {
					if (!sent_syn_ack_ack) {
						sent_syn_ack_ack = true;
						fprintf(stderr, "Handshake complete!\n");
					} else {
						q_push_back(send_q, &pkt_send);
						send_seq++;
					}
				}

				send_packet(sockfd, &serveraddr, &pkt_send);
			}
		}
	}
}

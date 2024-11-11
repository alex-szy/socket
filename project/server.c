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
#include "common.h"


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

	params p;
	p_init(&p, 20, argc, argv, NULL);

	/* 1. Create socket */
	stdin_nonblock();
	bind_socket(p.sockfd, argc, argv);
	
	/* 4. Create the send and receive packets */

	for (;;) { // listen for syn packet
		if (recv_packet(p.sockfd, &p.addr, &p.pkt_recv) <= 0) 		
			continue;
		if (p.pkt_recv.flags & PKT_SYN) {
			p.recv_seq = htonl(ntohl(p.pkt_recv.seq)+1);
			p.pkt_send.flags = PKT_ACK | PKT_SYN;
			p.pkt_send.ack = p.recv_seq;
			p.pkt_send.seq = p.send_seq;
			p.send_seq = htonl(ntohl(p.send_seq)+1);
			q_push_back(p.send_q, &p.pkt_send);
			q_print(p.send_q, "SBUF");
			send_packet(p.sockfd, &p.addr, q_front(p.send_q), "SEND");
			break;
		}
	}

	for (;;) { // listen for syn ack ack packet, may have payload
		p_retransmit_on_timeout(&p);
		if (recv_packet(p.sockfd, &p.addr, &p.pkt_recv) <= 0)
			continue;
		p.before = clock();
		if (p.pkt_recv.flags & PKT_ACK && p.pkt_recv.seq == p.recv_seq) { // syn ack ack packet, may have payload
			p_clear_acked_packets_from_sbuf(&p);
			if (p.pkt_recv.length == 0) { // incoming zero length
				p.recv_seq = htonl(ntohl(p.recv_seq)+1);
			} else {
				p_handle_data_packet(&p);
				p_send_packet_from_stdin(&p);
			}
			break;
		} else {
			send_packet(p.sockfd, &p.addr, q_front(p.send_q), "SEND"); // ack, not retransmit
		}
	}

	for (;;)
		p_listen(&p);
}

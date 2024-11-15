#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "deque.h"
#include "common.h"


static void construct_serveraddr(struct sockaddr_in *serveraddr,
                                int argc,
                                char *argv[]) {
    // Construct server address
    serveraddr->sin_family = AF_INET;  // use IPv4
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
}


int main(int argc, char *argv[]) {
    // Seed the random number generator
    srand(0);

    params p;
    p_init(&p, 20, argc, argv, construct_serveraddr);

    stdin_nonblock();

    // Create the SYN packet
    p.pkt_send.seq = p.send_seq;
    p.pkt_send.ack = p.recv_seq;
    p.pkt_send.length = 0;
    p.pkt_send.flags = PKT_SYN;

    // Push the syn packet onto the queue and send it
    p_send_and_enqueue_pkt_send(&p);
    p.send_seq++;

    for (;;) {  // wait for syn ack
        p_retransmit_on_timeout(&p);
        if (p_recv_packet(&p) <= 0)
            continue;
        if (p.pkt_recv.flags & PKT_ACK && p.pkt_recv.flags & PKT_SYN) {  // syn ack packet
            if (p_clear_acked_packets_from_sbuf(&p))  // reset the clock if new ack received
                p.before = clock();
            p.recv_seq = p.pkt_recv.seq + 1;
            if (!p_send_payload_ack(&p)) {
                p.pkt_send.flags = PKT_ACK;
                p.pkt_send.ack = p.recv_seq;
                p.pkt_send.seq = p.send_seq;
                p.pkt_send.length = 0;
                p_send_packet(&p, NULL, "SEND");
                p.send_seq++;
            }
            break;
        } else {
            p_send_packet(&p, q_front(p.send_q), "SEND");
        }
    }

    p_listen(&p);
}

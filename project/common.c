#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "utils.h"
#include "deque.h"
#include "common.h"

/* Initializes the parameters needed by the client or server. */
void p_init(params *p,
            int q_capacity,
            int argc,
            char *argv[],
            void (*construct_addr)(struct sockaddr_in*, int, char*[])) {
    p->sockfd = make_nonblock_socket();
    memset(&p->pkt_send, 0, sizeof(packet));
    memset(&p->pkt_recv, 0, sizeof(packet));
    p->recv_seq = 0;
    p->send_seq = htonl(rand() & RANDMASK);
    p->recv_q = q_init(q_capacity);
    p->send_q = q_init(q_capacity);
    if (p->recv_q == NULL || p->send_q == NULL)
        die("queue initialization malloc failed");
    p->recv_ack = -1;
    p->ack_count = 0;
    p->before = clock();
    if (construct_addr != NULL)
        construct_addr(&p->addr, argc, argv);
}

/* Checks for a 1 second timeout since timer was last reset,
and sends first packet in the send buffer, if any. */
void p_retransmit_on_timeout(params *p) {
    clock_t now = clock();
    // Packet retransmission
    if (now - p->before > CLOCKS_PER_SEC) {  // 1 second timer
        p->before = now;
        // send the packet with lowest seq number in sending buffer
        packet* send = q_front(p->send_q);
        // fprintf(stderr, "q size: %ld\n", q_size(send_q));
        if (send != NULL)
            send_packet(p->sockfd, &p->addr, send, "RTOS");
    }
}

/* Checks if send queue is full.
If not full and there is data in stdin, send a packet with the data and return true.
Else do nothing and return false. */
bool p_send_payload_ack(params *p) {
    if (q_full(p->send_q))
        return false;
    int bytes = read_stdin_to_pkt(&p->pkt_send);
    if (bytes <= 0)
        return false;
    p->pkt_send.ack = p->recv_seq;
    p->pkt_send.seq = p->send_seq;
    p->pkt_send.flags = PKT_ACK;
    q_push_back(p->send_q, &p->pkt_send);
    q_print(p->send_q, "SBUF");
    p->send_seq = htonl(ntohl(p->send_seq)+bytes);
    send_packet(p->sockfd, &p->addr, &p->pkt_send, "SEND");
    return true;
}

/* Check if the received ack is a duplicate.
If 3 in a row, retransmit the first packet in the send buffer. */
void p_retransmit_on_duplicate_ack(params *p) {
    // retransmit if 3 same acks in a row
    if (p->pkt_recv.ack == p->recv_ack) {
        p->ack_count++;
        if (p->ack_count == 3) {
            p->ack_count = 0;
            packet* send = q_front(p->send_q);
            if (send != NULL)
                send_packet(p->sockfd, &p->addr, send, "DUPS");
        }
    } else {
        p->recv_ack = p->pkt_recv.ack;
    }
}

/* Handles the incoming data packet.
If the packet is expected, write to stdout, check the received queue for the next expected packets and does the same.
Else if packet has not been acked, try to buffer it (do nothing if buffer is full).
If the packet has already been acked, do nothing. */
void p_handle_data_packet(params *p) {
    if (p->pkt_recv.seq == p->recv_seq) {  // write contents of packet if expected
        write_pkt_to_stdout(&p->pkt_recv);
        p->recv_seq = htonl(ntohl(p->recv_seq)+ntohs(p->pkt_recv.length));  // next packet

        // loop through sorted packet buffer and pop off next packets

        for (packet *pkt = q_front(p->recv_q);
                pkt != NULL && pkt->seq == p->recv_seq;
                pkt = q_pop_front_get_next(p->recv_q)) {
            write_pkt_to_stdout(pkt);
            p->recv_seq = htonl(ntohl(p->recv_seq)+ntohs(pkt->length));
        }
    } else if (ntohl(p->pkt_recv.seq) > ntohl(p->recv_seq)) {  // future packet, try to buffer
        q_try_insert_keep_sorted(p->recv_q, &p->pkt_recv);
        q_print(p->recv_q, "RBUF");
    }
}

/* Pops off all packets from send buffer that have a lower seq number than the incoming ack.
Returns true if any packets were popped. */
bool p_clear_acked_packets_from_sbuf(params *p) {
    bool flag = false;
    packet *pkt = q_front(p->send_q);
    while (pkt != NULL && ntohl(pkt->seq) < ntohl(p->pkt_recv.ack)) {
        flag = true;
        pkt = q_pop_front_get_next(p->send_q);
    }
    return flag;
}

/* Sends an ack packet without any data.
Does not increment the send sequence number. */
void p_send_empty_ack(params *p) {
    p->pkt_send.flags = PKT_ACK;
    p->pkt_send.ack = p->recv_seq;
    p->pkt_send.seq = 0;
    p->pkt_send.length = 0;
    send_packet(p->sockfd, &p->addr, &p->pkt_send, "SEND");
}

/* Called in the main loop of client and server.
Handles the data transmission between the sender and receiver. */
void p_listen(params *p) {
    for (;;) {
        p_retransmit_on_timeout(p);
        if (recv_packet(p->sockfd, &p->addr, &p->pkt_recv) <= 0) {
            p_send_payload_ack(p);
        } else {  // packet received
            p_retransmit_on_duplicate_ack(p);

            if (p_clear_acked_packets_from_sbuf(p))  // reset the timer if new ack received
                p->before = clock();

            if (p->pkt_recv.length == 0)  // no further handling for empty ack packets
                continue;

            p_handle_data_packet(p);

            // If the send queue isn't full yet and we have data to send, read data into payload
            // Or if we're responding to a syn packet
            if (!p_send_payload_ack(p))
                p_send_empty_ack(p);
        }
    }
}

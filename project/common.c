#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include "deque.h"
#include "common.h"

#define PKT_SYN 1
#define PKT_ACK 2
#define RANDMASK ~(1 << 31)
#define WINDOWSIZE 20

static int sockfd;
static uint32_t recv_seq;
static uint32_t send_seq;
static uint32_t ack_count;
static q_handle_t send_q;
static q_handle_t recv_q;
static packet pkt_recv;
static packet pkt_send;
static clock_t before;
static struct sockaddr_in addr;
static socklen_t addr_size;

static ssize_t(*read_func)(uint8_t*, size_t);
static ssize_t(*write_func)(uint8_t*, size_t);

static inline int make_nonblock_socket() {
    /* 1. Create socket */
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
                            // use IPv4  use UDP

    // Make stdin and socket non-blocking
    int socket_nonblock = fcntl(sockfd, F_SETFL, O_NONBLOCK);
    if (socket_nonblock < 0) die("non-block socket");
    return sockfd;
}

static inline void print_packet(const packet *pkt, const char* op) {
    fprintf(stderr, "%s %d ACK %d SIZE %d FLAGS",
            op, pkt->seq, pkt->ack, pkt->length);
    switch (pkt->flags) {
        case PKT_SYN:
            fprintf(stderr, " SYN\n");
            break;
        case PKT_ACK:
            fprintf(stderr, " ACK\n");
            break;
        case PKT_ACK | PKT_SYN:
            fprintf(stderr, " SYN ACK\n");
            break;
        default:
            fprintf(stderr, " NONE\n");
    }
}

static inline int read_stdin_to_pkt(packet *pkt) {
    int bytes_read = read_func(pkt->payload, MSS);
    if (bytes_read >= 0)
        pkt->length = bytes_read;
    else
        pkt->length = 0;
    return bytes_read;
}

static inline void write_pkt_to_stdout(packet *pkt) {
    write_func(pkt->payload, pkt->length);
}

static int send_packet(const packet *pkt, const char* str) {
    if (pkt == NULL)
        pkt = &pkt_send;
    print_packet(pkt, str);
    packet pkt_send = *pkt;
    pkt_send.seq = htonl(pkt_send.seq);
    pkt_send.ack = htonl(pkt_send.ack);
    pkt_send.length = htons(pkt_send.length);
    int did_send = sendto(sockfd, &pkt_send, sizeof(pkt_send),
                        // socket  send data   how much to send
                            0, (struct sockaddr*) &addr,
                        // flags   where to send
                            addr_size);
    if (did_send < 0) die("send");
    return did_send;
}

static int recv_packet() {
    /* 5. Listen for response from server */
    int bytes_recvd = recvfrom(sockfd, &pkt_recv, sizeof(pkt_recv),
                            // socket  store data  how much
                                0, (struct sockaddr*) &addr,
                                &addr_size);
    // Error if bytes_recvd < 0 :(
    if (bytes_recvd < 0 && errno != EAGAIN) die("receive");
    if (bytes_recvd > 0) {
        pkt_recv.seq = ntohl(pkt_recv.seq);
        pkt_recv.ack = ntohl(pkt_recv.ack);
        pkt_recv.length = ntohs(pkt_recv.length);
        print_packet(&pkt_recv, "RECV");
    }
    return bytes_recvd;
}

/* Initializes the parameters needed by the client or server. */
static void init_common(ssize_t(*read_sec)(uint8_t*, size_t), ssize_t(*write_sec)(uint8_t*, size_t)) {
    sockfd = make_nonblock_socket();
    read_func = read_sec;
    write_func = write_sec;
    memset(&pkt_send, 0, sizeof(packet));
    memset(&pkt_recv, 0, sizeof(packet));
    recv_seq = 0;
    send_seq = rand() & RANDMASK;
    recv_q = q_init(WINDOWSIZE);
    send_q = q_init(WINDOWSIZE);
    if (recv_q == NULL || send_q == NULL)
        die("queue initialization malloc failed");
    ack_count = 0;
    before = clock();
}

/* Checks for a 1 second timeout since timer was last reset,
and sends first packet in the send buffer, if any. */
static void retransmit_on_timeout() {
    clock_t now = clock();
    // Packet retransmission
    if (now - before > CLOCKS_PER_SEC) {  // 1 second timer
        before = now;
        // send the packet with lowest seq number in sending buffer
        packet* send = q_front(send_q);
        // fprintf(stderr, "q size: %ld\n", q_size(send_q));
        if (send != NULL) {
            send->ack = recv_seq;
            send_packet(send, "RTOS");
        }
    }
}

/* Sends the pkt_send packet and enqueues it.
Only use this function to send a new packet over the network which needs to be acked.
Like a syn packet, a syn ack packet, or a packet with data in it.
Do not call this function if the queue is full, as you will have already consumed and lost the data from stdin. */
static void send_and_enqueue_pkt_send() {
    send_packet(NULL, "SEND");
    q_push_back(send_q, &pkt_send);
    q_print(send_q, "SBUF");
    send_seq += pkt_send.length; 
}

/* Checks if send queue is full.
If not full and there is data in stdin, send a packet with the data and return true.
Else do nothing and return false. */
static bool send_payload_ack() {
    if (q_full(send_q))
        return false;
    int bytes = read_stdin_to_pkt(&pkt_send);
    if (bytes <= 0)
        return false;
    pkt_send.ack = recv_seq;
    pkt_send.seq = send_seq;
    pkt_send.flags = PKT_ACK;
    send_and_enqueue_pkt_send();
    return true;
}

/* Check if the received ack is a duplicate.
If 3 in a row, retransmit the first packet in the send buffer. */
static void retransmit_on_duplicate_ack() {
    // retransmit if 3 same acks in a row
    packet* front = q_front(send_q);
    if (front != NULL && pkt_recv.ack == front->seq) {
        ack_count++;
        if (ack_count == 3) {
            ack_count = 0;
            front->ack = recv_seq;
            send_packet(front, "DUPS");
        }
    } else {
        ack_count = 0;
    }
}

/* Handles the incoming data packet.
If the packet is expected, write to stdout, check the received queue for the next expected packets and does the same.
Else if packet has not been acked, try to buffer it (do nothing if buffer is full).
If the packet has already been acked, do nothing. */
static void handle_data_packet() {
    if (pkt_recv.seq == recv_seq) {  // write contents of packet if expected
        write_pkt_to_stdout(&pkt_recv);
        recv_seq += pkt_recv.length;  // next packet

        // loop through sorted packet buffer and pop off next packets
        bool removed = false;
        for (packet *pkt = q_front(recv_q);
                pkt != NULL && pkt->seq == recv_seq;
                pkt = q_pop_front_get_next(recv_q)) {
            removed = true;
            write_pkt_to_stdout(pkt);
            recv_seq += pkt->length;
        }
        if (removed)
            q_print(recv_q, "RBUF");
    } else if (pkt_recv.seq > recv_seq) {  // future packet, try to buffer
        q_try_insert_keep_sorted(recv_q, &pkt_recv);
        q_print(recv_q, "RBUF");
    }
}

/* Pops off all packets from send buffer that have a lower seq number than the incoming ack.
Returns true if any packets were popped. */
static bool clear_acked_packets_from_sbuf() {
    bool flag = false;
    packet *pkt = q_front(send_q);
    while (pkt != NULL && pkt->seq < pkt_recv.ack) {
        flag = true;
        pkt = q_pop_front_get_next(send_q);
    }
    if (flag)
        q_print(send_q, "SBUF");
    return flag;
}

/* Sends an ack packet without any data.
Does not increment the send sequence number. */
static inline void send_empty_ack() {
    pkt_send.flags = PKT_ACK;
    pkt_send.ack = recv_seq;
    pkt_send.seq = 0;
    pkt_send.length = 0;
    send_packet(NULL, "SEND");
}

void init_client(int port, const char* address, ssize_t(*read_sec)(uint8_t*, size_t), ssize_t(*write_sec)(uint8_t*, size_t)) {
    init_common(read_sec, write_sec);
    addr.sin_family = AF_INET;
    if (strcmp(address, "localhost"))
        addr.sin_addr.s_addr = inet_addr(address);
    else
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    addr_size = sizeof(addr);

    // Create the SYN packet
    pkt_send.seq = send_seq;
    pkt_send.ack = recv_seq;
    pkt_send.length = 0;
    pkt_send.flags = PKT_SYN;

    // Push the syn packet onto the queue and send it
    send_and_enqueue_pkt_send();
    send_seq++;

    for (;;) {  // wait for syn ack
        retransmit_on_timeout();
        if (recv_packet() <= 0)
            continue;
        if (pkt_recv.flags & PKT_ACK && pkt_recv.flags & PKT_SYN) {  // syn ack packet
            if (clear_acked_packets_from_sbuf())  // reset the clock if new ack received
                before = clock();
            recv_seq = pkt_recv.seq + 1;
            if (!send_payload_ack()) {
                pkt_send.flags = PKT_ACK;
                pkt_send.ack = recv_seq;
                pkt_send.seq = send_seq;
                pkt_send.length = 0;
                send_packet(NULL, "SEND");
                send_seq++;
            }
            break;
        } else {
            send_packet(q_front(send_q), "SEND");
        }
    }
}

void init_server(int port, ssize_t(*read_sec)(uint8_t*, size_t), ssize_t(*write_sec)(uint8_t*, size_t)) {
    init_common(read_sec, write_sec);

    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET;  // use IPv4
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(port);
    int did_bind = bind(sockfd, (struct sockaddr*) &servaddr,
                        sizeof(servaddr));
    // Error if did_bind < 0 :(
    if (did_bind < 0) die("bind socket");
    addr_size = sizeof(addr);

    for (;;) {  // listen for syn packet
        if (recv_packet() <= 0)
            continue;
        if (pkt_recv.flags & PKT_SYN) {
            recv_seq = pkt_recv.seq + 1;
            pkt_send.seq = send_seq;
            pkt_send.ack = recv_seq;
            pkt_send.flags = PKT_ACK | PKT_SYN;
            send_and_enqueue_pkt_send();
            send_seq++;
            before = clock();
            break;
        }
    }

    for (;;) {  // listen for syn ack ack packet, may have payload
        retransmit_on_timeout();
        if (recv_packet() <= 0)
            continue;
        if (pkt_recv.flags & PKT_ACK &&
            (pkt_recv.seq == recv_seq || pkt_recv.length == 0)) {
            // syn ack ack packet, may have payload
            if (clear_acked_packets_from_sbuf())
                before = clock();
            if (pkt_recv.length == 0) {  // incoming zero length syn ack ack
                recv_seq++;
            } else {
                handle_data_packet();
                send_payload_ack();
            }
            break;
        } else {
            send_packet(q_front(send_q), "SEND");
        }
    }
}

/* Called in the main loop of client and server.
Handles the data transmission between the sender and receiver. */
void transport_listen() {
    for (;;) {
        retransmit_on_timeout();
        if (recv_packet() <= 0) {
            send_payload_ack();
        } else {  // packet received
            if (clear_acked_packets_from_sbuf())  // reset the timer if new ack received
                before = clock();

            retransmit_on_duplicate_ack();

            // if syn ack received at this point, the syn ack ack must have been dropped.
            // the only case in which this could happen is if the syn ack was empty, since it wouldn't have been enqueued
            // we need to ack this syn ack to complete the handshake.
            // however, we cannot send data in this ack no matter what, since the original ack didn't have data. otherwise we corrupt our send_seq.
            // technically, only the client needs to handle this, since the server does not send syn ack acks.
            if (pkt_recv.flags & PKT_SYN) {
                pkt_send.seq = pkt_recv.ack;
                pkt_send.ack = recv_seq;
                pkt_send.flags = PKT_ACK;
                pkt_send.length = 0;
                send_packet(NULL, "SEND");
                continue;
            }
            
            if (pkt_recv.length == 0)  // no further handling for empty ack packets
                continue;

            handle_data_packet();

            // If the send queue isn't full yet and we have data to send, read data into payload
            // Or if we're responding to a syn packet
            if (!send_payload_ack())
                send_empty_ack();
        }
    }
}

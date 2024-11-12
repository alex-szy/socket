#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include "utils.h"

static void print_packet(packet *pkt, const char* op) {
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

void die(const char s[]) {
    perror(s);
    exit(errno);
}

int make_nonblock_socket() {
    /* 1. Create socket */
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
                            // use IPv4  use UDP

    // Make stdin and socket non-blocking
    int socket_nonblock = fcntl(sockfd, F_SETFL, O_NONBLOCK);
    if (socket_nonblock < 0) die("non-block socket");
    return sockfd;
}

void stdin_nonblock() {
    int stdin_nonblock = fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    if (stdin_nonblock < 0) die("non-block stdin");
}

int send_packet(int sockfd,
                struct sockaddr_in *serveraddr,
                packet *pkt,
                const char* str) {
    print_packet(pkt, str);
    socklen_t serversize = sizeof(*serveraddr);
    packet pkt_send = *pkt;
    pkt_send.seq = htonl(pkt_send.seq);
    pkt_send.ack = htonl(pkt_send.ack);
    pkt_send.length = htons(pkt_send.length);
    int did_send = sendto(sockfd, &pkt_send, sizeof(pkt_send),
                        // socket  send data   how much to send
                            0, (struct sockaddr*) serveraddr,
                        // flags   where to send
                            serversize);
    if (did_send < 0) die("send");
    return did_send;
}

int recv_packet(int sockfd, struct sockaddr_in *serveraddr, packet *pkt) {
    socklen_t serversize = sizeof(*serveraddr);
    /* 5. Listen for response from server */
    int bytes_recvd = recvfrom(sockfd, pkt, sizeof(*pkt),
                            // socket  store data  how much
                                0, (struct sockaddr*) serveraddr,
                                &serversize);
    // Error if bytes_recvd < 0 :(
    if (bytes_recvd < 0 && errno != EAGAIN) die("receive");
    if (bytes_recvd > 0) {
        pkt->seq = ntohl(pkt->seq);
        pkt->ack = ntohl(pkt->ack);
        pkt->length = ntohs(pkt->length);
        print_packet(pkt, "RECV");
    }
    return bytes_recvd;
}

int read_stdin_to_pkt(packet *pkt) {
    int bytes_read = read(STDIN_FILENO, &pkt->payload, MSS);
    if (bytes_read >= 0)
        pkt->length = bytes_read;
    else
        pkt->length = 0;
    return bytes_read;
}

void write_pkt_to_stdout(packet *pkt) {
    write(STDOUT_FILENO, &pkt->payload, pkt->length);
}

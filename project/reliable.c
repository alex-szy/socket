#include <arpa/inet.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include "reliable.h"

// Convert host to network byte order
static void packet_to_network(packet *pkt)
{
    pkt->ack = htonl(pkt->ack);
    pkt->seq = htonl(pkt->seq);
    pkt->length = htons(pkt->length);
}

// Convert network to host byte order
static void packet_to_host(packet *pkt)
{
    pkt->ack = ntohl(pkt->ack);
    pkt->seq = ntohl(pkt->seq);
    pkt->length = ntohs(pkt->length);
}

// Create a new packet
packet create_packet(uint32_t seq, uint32_t ack, uint16_t length, uint8_t flags)
{
    packet pkt = {0};
    pkt.seq = seq;
    pkt.ack = ack;
    pkt.length = length;
    pkt.flags = flags;
    return pkt;
}

int send_packet(int sockfd, packet pkt, struct sockaddr_in *addr, int diag) {
    packet_to_network(&pkt);
    print_diag(&pkt, diag);
    return sendto(sockfd, &pkt, sizeof(pkt), 0,
          (struct sockaddr *)addr, sizeof(*addr));
}

int recv_packet(int sockfd, packet *pkt, struct sockaddr_in *addr) {
    socklen_t addrsize = sizeof(*addr);
    int bytes_recvd = recvfrom(sockfd, pkt, sizeof(*pkt), 0,
                                 (struct sockaddr *)addr, &addrsize);
    if (bytes_recvd > 0) {
        print_diag(pkt, RECV);
        packet_to_host(pkt);
    }
    return bytes_recvd;
}

void print_send_window(send_window *window)
{
    fprintf(stderr, "SBUF");
    BufferNode *current = window->head;
    while (current != NULL)
    {
        fprintf(stderr, " %d", current->pkt.seq);
        current = current->next;
    }
    fprintf(stderr, "\n");
}

void print_recv_window(recv_window *window)
{
    fprintf(stderr, "RBUF");
    BufferNode *current = window->head;
    while (current != NULL)
    {
        fprintf(stderr, " %d", current->pkt.seq);
        current = current->next;
    }
    fprintf(stderr, "\n");
}

// Initialize windows
void init_send_window(send_window *window)
{
    window->head = NULL;
    window->size = 0;
    window->seq = rand() % (UINT32_MAX / 2);
    window->last_ack_time = clock();
}

void init_recv_window(recv_window *window)
{
    window->head = NULL;
    window->size = 0;
    window->expect_seq = 0;
}

// Add packet to sending window
bool add_to_send_window(send_window *window, packet *pkt)
{
    if (window->size >= MAX_WINDOW_SIZE / MSS)
    {
        return false;
    }

    BufferNode *new_node = (BufferNode *)malloc(sizeof(BufferNode));
    if (!new_node)
        return false;

    new_node->pkt = *pkt;
    new_node->next = NULL;

    // Insert in order of sequence numbers
    if (!window->head || window->head->pkt.seq > pkt->seq)
    {
        new_node->next = window->head;
        window->head = new_node;
    }
    else
    {
        BufferNode *current = window->head;
        while (current->next && current->next->pkt.seq < pkt->seq)
        {
            current = current->next;
        }
        new_node->next = current->next;
        current->next = new_node;
    }

    window->size++;
    window->seq += pkt->length;
    print_send_window(window);
    return true;
}

// Get packet for retransmission
packet *get_retransmit_packet(send_window *window)
{
    if (window->head)
    {
        return &window->head->pkt;
    }
    return NULL;
}
// Process acknowledgements
bool process_ack(send_window *window, uint32_t ack_num, int sockfd, struct sockaddr_in addr)
{
    bool found_new_ack = false;
    BufferNode *current = window->head;
    BufferNode *prev = NULL;

    while (current != NULL)
    {
        bool remove_node = false;

        if (current->pkt.seq < ack_num)
        {
            // Packet is fully acknowledged
            remove_node = true;
            found_new_ack = true;
            window->last_ack_time = clock();
        }

        if (remove_node)
        {
            BufferNode *to_remove = current;
            if (prev == NULL)
            {
                window->head = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            current = current->next;
            free(to_remove);
            window->size--;
        }
        else
        {
            prev = current;
            current = current->next;
        }
    }
    if (found_new_ack)
        print_send_window(window);

    // Update base_seq if we have remaining packets
    return found_new_ack;
}

// Add packet to receiving window
bool add_to_recv_window(recv_window *window, packet *pkt)
{
    if (window->size >= MAX_WINDOW_SIZE / MSS)
    {
        return false;
    }

    BufferNode *new_node = (BufferNode *)malloc(sizeof(BufferNode));
    if (!new_node)
        return false;

    new_node->pkt = *pkt;
    new_node->next = NULL;

    // Insert in order of sequence numbers
    if (!window->head || window->head->pkt.seq > pkt->seq)
    {
        new_node->next = window->head;
        window->head = new_node;
    }
    else
    {
        BufferNode *current = window->head;
        while (current->next && current->next->pkt.seq < pkt->seq)
        {
            current = current->next;
        }
        new_node->next = current->next;
        current->next = new_node;
    }

    window->size++;
    print_recv_window(window);
    return true;
}

// Process received data and write to output
uint32_t process_received_data(recv_window *window, int fd)
{
    BufferNode *current = window->head;
    BufferNode *prev = NULL;

    while (current != NULL)
    {
        if (current->pkt.seq <= window->expect_seq)
        {
            // Write data to output
            write(fd, current->pkt.payload, current->pkt.length);
            window->expect_seq += current->pkt.length;

            // Remove the processed node
            if (prev == NULL)
            {
                window->head = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            BufferNode *to_free = current;
            current = current->next;
            free(to_free);
            window->size--;
        }
        else
        {
            prev = current;
            current = current->next;
        }
    }

    return window->expect_seq;
}

// Check for retransmission
bool check_retransmit(send_window *window)
{
    if (window->size == 0)
        return false;

    return (clock() - window->last_ack_time) >= CLOCKS_PER_SEC;
}

// Clean up function to free memory
void cleanup_send_window(send_window *window)
{
    BufferNode *current = window->head;
    while (current != NULL)
    {
        BufferNode *to_free = current;
        current = current->next;
        free(to_free);
    }
    window->head = NULL;
    window->size = 0;
}

void cleanup_recv_window(recv_window *window)
{
    BufferNode *current = window->head;
    while (current != NULL)
    {
        BufferNode *to_free = current;
        current = current->next;
        free(to_free);
    }
    window->head = NULL;
    window->size = 0;
}

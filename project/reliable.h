// reliable.h
#ifndef RELIABLE_H
#define RELIABLE_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

#define MSS 1012              // Maximum Segment Size
#define MAX_WINDOW_SIZE 20240 // 20 full packets
#define RETRANSMIT_TIMEOUT 1  // 1 second timeout
#define MAX_DUP_ACKS 3

typedef struct
{
    uint32_t ack;
    uint32_t seq;
    uint16_t length;
    uint8_t flags;
    uint8_t unused;
    uint8_t payload[MSS];
} packet;

// typedef struct

// Convert host to network byte order
void packet_to_network(packet *pkt)
{
    pkt->ack = htonl(pkt->ack);
    pkt->seq = htonl(pkt->seq);
    pkt->length = htons(pkt->length);
}

// Convert network to host byte order
void packet_to_host(packet *pkt)
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

typedef struct BufferNode
{
    packet pkt;
    time_t send_time;
    bool acked;
    int dup_acks;
    struct BufferNode *next;
} BufferNode;

// Modified window structures to use linked lists
typedef struct
{
    BufferNode *head;
    int size;
    uint32_t base_seq;
    uint32_t next_seq;
    time_t last_ack_time;
} send_window;

typedef struct
{
    BufferNode *head;
    int size;
    uint32_t expect_seq;
} recv_window;

void print_send_window(send_window *window)
{
    // fprintf(stderr, "Last ACK Time: %ld\n", window->last_ack_time);
    fprintf(stderr, "SBUF");
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
    window->base_seq = 0;
    window->next_seq = 0;
    window->last_ack_time = time(NULL);
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
    new_node->send_time = time(NULL);
    new_node->acked = false;
    new_node->dup_acks = 0;
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
    window->next_seq = pkt->seq + pkt->length;
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
void process_ack(send_window *window, uint32_t ack_num, int sockfd, struct sockaddr_in addr)
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
            window->last_ack_time = time(NULL);
        }
        else if (current->pkt.seq == ack_num)
        {
            // Duplicate ACK
            current->dup_acks++;
            // if (current->dup_acks >= MAX_DUP_ACKS)
            // {
            //     // current->send_time = 0; // Force retransmission
            //     // current->dup_acks = 0;
            //     packet *retx_pkt = get_retransmit_packet(window);
            //     if (retx_pkt)
            //     {
            //         packet_to_network(retx_pkt);
            //         // print_diag(&retx_pkt, SEND);
            //         sendto(sockfd, retx_pkt, sizeof(packet), 0,
            //                (struct sockaddr *)&addr, sizeof(addr));
            //         current->dup_acks = 0;
            //     }
            // }
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

    // Update base_seq if we have remaining packets
    if (window->head)
    {
        window->base_seq = window->head->pkt.seq;
    }
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

    time_t current_time = time(NULL);
    return (current_time - window->last_ack_time) >= RETRANSMIT_TIMEOUT;
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

#endif

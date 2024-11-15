// reliable.h
#ifndef RELIABLE_H
#define RELIABLE_H

#include <stdbool.h>
#include <stdio.h>

#define MSS 1012              // Maximum Segment Size
#define MAX_WINDOW_SIZE 20240 // 20 full packets
#define RETRANSMIT_TIMEOUT 1  // 1 second timeout
#define MAX_DUP_ACKS 3

#define RECV 0
#define SEND 1
#define RTOS 2
#define DUPA 3

typedef struct
{
    uint32_t ack;
    uint32_t seq;
    uint16_t length;
    uint8_t flags;
    uint8_t unused;
    uint8_t payload[MSS];
} packet;

static inline void print_diag(packet *pkt, int diag)
{
   switch (diag)
   {
   case RECV:
      fprintf(stderr, "RECV");
      break;
   case SEND:
      fprintf(stderr, "SEND");
      break;
   case RTOS:
      fprintf(stderr, "RTOS");
      break;
   case DUPA:
      fprintf(stderr, "DUPS");
      break;
   }

   bool syn = pkt->flags & 0b01;
   bool ack = pkt->flags & 0b10;
   fprintf(stderr, " %u ACK %u SIZE %hu FLAGS ", ntohl(pkt->seq),
           ntohl(pkt->ack), ntohs(pkt->length));
   if (!syn && !ack)
   {
      fprintf(stderr, "NONE");
   }
   else
   {
      if (syn)
      {
         fprintf(stderr, "SYN ");
      }
      if (ack)
      {
         fprintf(stderr, "ACK ");
      }
   }
   fprintf(stderr, "\n");
}

// typedef struct
typedef struct BufferNode
{
    packet pkt;
    struct BufferNode *next;
} BufferNode;

// Modified window structures to use linked lists
typedef struct
{
   BufferNode *head;
   int size;
   uint32_t seq;
   clock_t last_ack_time;
} send_window;

typedef struct
{
   BufferNode *head;
   int size;
   uint32_t expect_seq;
} recv_window;

packet create_packet(uint32_t seq, uint32_t ack, uint16_t length, uint8_t flags);
int send_packet(int sockfd, packet pkt, struct sockaddr_in *addr, int diag);
int recv_packet(int sockfd, packet *pkt, struct sockaddr_in *addr);
void print_send_window(send_window *window);
void print_recv_window(recv_window *window);
void init_send_window(send_window *window);
void init_recv_window(recv_window *window);
bool add_to_send_window(send_window *window, packet *pkt);
packet *get_retransmit_packet(send_window *window);
bool process_ack(send_window *window, uint32_t ack_num, int sockfd, struct sockaddr_in addr);
bool add_to_recv_window(recv_window *window, packet *pkt);
uint32_t process_received_data(recv_window *window, int fd);
bool check_retransmit(send_window *window);
void cleanup_send_window(send_window *window);
void cleanup_recv_window(recv_window *window);

#endif

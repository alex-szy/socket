#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include "reliable.h"
#include <stdio.h>

#define RECV 0
#define SEND 1
#define RTOS 2
#define DUPA 3

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

int main(int argc, char *argv[])
{
   if (argc != 3)
   {
      exit(EXIT_FAILURE);
   }

   // Create socket and set non-blocking
   int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
   if (sockfd < 0)
      return errno;

   int flags = fcntl(sockfd, F_GETFL, 0);
   if (flags < 0)
      return errno;
   fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

   flags = fcntl(STDIN_FILENO, F_GETFL, 0);
   if (flags < 0)
      return errno;
   fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

   // Setup server address
   struct sockaddr_in serveraddr;
   memset(&serveraddr, 0, sizeof(serveraddr));
   serveraddr.sin_family = AF_INET;
   if (!strcmp(argv[1], "localhost"))
   {
      inet_aton("127.0.0.1", &serveraddr.sin_addr);
   }
   else
   {
      inet_aton(argv[1], &serveraddr.sin_addr);
   }
   serveraddr.sin_port = htons(atoi(argv[2]));

   // Initialize windows
   send_window send_win;
   recv_window recv_win;
   init_send_window(&send_win);
   init_recv_window(&recv_win);

   // Start three-way handshake
   uint32_t initial_seq = rand() % (UINT32_MAX / 2);
   packet syn_pkt = create_packet(initial_seq, 0, 0, 1); // SYN flag
   packet_to_network(&syn_pkt);
   print_diag(&syn_pkt, SEND);
   sendto(sockfd, &syn_pkt, sizeof(syn_pkt), 0,
          (struct sockaddr *)&serveraddr, sizeof(serveraddr));

   bool handshake_complete = false;
   socklen_t serversize = sizeof(serveraddr);

   // Main loop
   packet recv_pkt;
   while (true)
   {
      int bytes_recvd = recvfrom(sockfd, &recv_pkt, sizeof(recv_pkt), 0,
                                 (struct sockaddr *)&serveraddr, &serversize);
      if (bytes_recvd > 0)
      {
         print_diag(&recv_pkt, RECV);
         break;
      }
      else
      {
         continue;
      }
   }
   //any pkt
   packet_to_host(&recv_pkt);
   packet ack_pkt = create_packet(initial_seq + 1, recv_pkt.seq + 1, 0, 2);
   packet_to_network(&ack_pkt);
   print_diag(&ack_pkt, SEND);
   sendto(sockfd, &ack_pkt, sizeof(ack_pkt), 0,
          (struct sockaddr *)&serveraddr, serversize);

   send_win.base_seq = initial_seq + 1;
   send_win.next_seq = initial_seq + 2;
   recv_win.expect_seq = recv_pkt.seq + 1;

   uint32_t last_ack_received = 0;
   int dup_ack = 0;
   time_t last_ack_time = time(NULL);
   clock_t clk = clock();

   while (1)
   {
      // fprintf(stderr, "hi");
      packet recv_pkt;
      // Check for incoming packets
      int bytes_recvd = recvfrom(sockfd, &recv_pkt, sizeof(recv_pkt), 0,
                                 (struct sockaddr *)&serveraddr, &serversize);

      if (bytes_recvd > 0)
      {
         print_diag(&recv_pkt, RECV);
         packet_to_host(&recv_pkt);
         uint32_t ackk = recv_pkt.ack;
         // Process regular data packet
         if (recv_pkt.flags & 2)
         { // ACK flag set

            if (process_ack(&send_win, recv_pkt.ack, sockfd, serveraddr))
               clk = clock();
            if (ackk == last_ack_received)
            {
               dup_ack++;
               if (dup_ack >= 3)
               {
                  packet *retx_pkt = get_retransmit_packet(&send_win);
                  if (retx_pkt)
                  {
                     retx_pkt->ack = recv_win.expect_seq;
                     packet send_pkt = *retx_pkt;
                     packet_to_network(&send_pkt);
                     print_diag(&send_pkt, DUPA);
                     sendto(sockfd, &send_pkt, sizeof(packet), 0,
                            (struct sockaddr *)&serveraddr, sizeof(serveraddr));
                     // send_win.last_ack_time = time(NULL);
                  }
                  dup_ack = 0;
               }
            }
            else
            {
               last_ack_received = ackk;
               dup_ack = 0;
            }
            // last_ack_time = time(NULL);
            // clk = clock();
            // send_win.next_seq = 1069;
         }

         if (recv_pkt.length > 0)
         {
            if (recv_pkt.seq >= recv_win.expect_seq)
               add_to_recv_window(&recv_win, &recv_pkt);
            print_recv_window(&recv_win);
            uint32_t next_expected = process_received_data(&recv_win, STDOUT_FILENO);

            // Send ACK
            if (send_win.size < MAX_WINDOW_SIZE / MSS)
            {
               char buf[MSS];
               int bytes_read = read(STDIN_FILENO, buf, MSS);

               if (bytes_read > 0)
               {
                  packet data_pkt = create_packet(send_win.next_seq,
                                                  recv_win.expect_seq, bytes_read, 2);
                  memcpy(data_pkt.payload, buf, bytes_read);
                  // add_to_send_window(&send_win, &data_pkt);
                  add_to_send_window(&send_win, &data_pkt);
                  // print_send_window(&send_win);
                  packet_to_network(&data_pkt);
                  print_diag(&data_pkt, SEND);
                  sendto(sockfd, &data_pkt, sizeof(packet), 0,
                         (struct sockaddr *)&serveraddr, sizeof(serveraddr));
                  // packet_to_host(&data_pkt); I CHANGED THIS AFTER 10 PTS
                  // add_to_send_window(&send_win, &data_pkt);
               } else {
                  // send empty ack
                  packet ack_pkt = create_packet(0, recv_win.expect_seq, 0, 2);
                  packet_to_network(&ack_pkt);
                  print_diag(&ack_pkt, SEND);
                  sendto(sockfd, &ack_pkt, sizeof(packet), 0, 
                        (struct sockaddr *)&serveraddr, sizeof(serveraddr));
               }
            } else {
               // send empty ack
               packet ack_pkt = create_packet(0, recv_win.expect_seq, 0, 2);
               packet_to_network(&ack_pkt);
               print_diag(&ack_pkt, SEND);
               sendto(sockfd, &ack_pkt, sizeof(packet), 0, 
                     (struct sockaddr *)&serveraddr, sizeof(serveraddr));
            }
         }
         // if (check_retransmit(&send_win))
         // {
         //    packet *retx_pkt = get_retransmit_packet(&send_win);
         //    if (retx_pkt)
         //    {
         //       packet_to_network(retx_pkt);
         //       sendto(sockfd, retx_pkt, sizeof(packet), 0,
         //              (struct sockaddr *)&serveraddr, sizeof(serveraddr));
         //    }
         // }
      }
      else
      {
         // if (check_retransmit(&send_win))
         time_t currtime = time(NULL);
         clock_t now = clock();
         // if (currtime - last_ack_time >= 1)
         if (now - clk > CLOCKS_PER_SEC)
         {
            packet *retx_pkt = get_retransmit_packet(&send_win);
            if (retx_pkt)
            {
               retx_pkt->ack = recv_win.expect_seq;
               // packet_to_network(retx_pkt);
               packet send_pkt = *retx_pkt;
               packet_to_network(&send_pkt);
               print_diag(&send_pkt, RTOS);
               sendto(sockfd, &retx_pkt, sizeof(packet), 0,
                      (struct sockaddr *)&serveraddr, sizeof(serveraddr));
               // send_win.last_ack_time = time(NULL);
               last_ack_time = time(NULL);
               clk = clock();
            }
         }
         if (send_win.size < MAX_WINDOW_SIZE / MSS)
         {
            char buf[MSS];
            int bytes_read = read(STDIN_FILENO, buf, MSS);

            if (bytes_read > 0)
            {
               packet data_pkt = create_packet(send_win.next_seq,
                                                recv_win.expect_seq, bytes_read, 2);
               memcpy(data_pkt.payload, buf, bytes_read);
               // add_to_send_window(&send_win, &data_pkt);
               add_to_send_window(&send_win, &data_pkt);
               // print_send_window(&send_win);
               packet_to_network(&data_pkt);
               print_diag(&data_pkt, SEND);
               sendto(sockfd, &data_pkt, sizeof(packet), 0,
                        (struct sockaddr *)&serveraddr, sizeof(serveraddr));
               // packet_to_host(&data_pkt); I CHANGED THIS AFTER 10 PTS
               // add_to_send_window(&send_win, &data_pkt);
            }
         }
      }
      // else
      // {
      //    continue;
      // }

      // Check for retransmissions

      // printf(send_win);
      // printf(recv_win);
   }

   close(sockfd);
   return 0;
}

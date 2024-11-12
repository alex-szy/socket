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
   if (argc != 2)
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
   struct sockaddr_in servaddr;
   memset(&servaddr, 0, sizeof(servaddr));
   servaddr.sin_family = AF_INET;
   servaddr.sin_addr.s_addr = INADDR_ANY;
   servaddr.sin_port = htons(atoi(argv[1]));

   if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0)
   {
      return errno;
   }

   // Initialize windows
   send_window send_win;
   recv_window recv_win;
   init_send_window(&send_win);
   init_recv_window(&recv_win);

   // Start the server main loop
   // bool handshake_complete = false;
   struct sockaddr_in client_addr;
   socklen_t client_addr_len = sizeof(client_addr);

   packet recv_pkt;
   // AWAIT SYN PACKET
   while (true)
   {
      int bytes_recvd = recvfrom(sockfd, &recv_pkt, sizeof(recv_pkt), 0,
                                 (struct sockaddr *)&client_addr, &client_addr_len);
      if (bytes_recvd > 0)
      {
         break;
      }
      else
      {
         continue;
      }
   }
   packet_to_host(&recv_pkt);
   // SEND SYNACK
   uint32_t server_isn = rand() % (UINT32_MAX / 2);
   packet syn_ack_pkt = create_packet(server_isn, recv_pkt.seq + 1, 0, 3);
   packet_to_network(&syn_ack_pkt);
   print_diag(&syn_ack_pkt, SEND);
   sendto(sockfd, &syn_ack_pkt, sizeof(syn_ack_pkt), 0,
          (struct sockaddr *)&client_addr, client_addr_len);

   send_win.base_seq = server_isn + 1;
   send_win.next_seq = server_isn + 1;
   recv_win.expect_seq = recv_pkt.seq + 1;

   bool handshake_complete = false;

   clock_t clk = clock();
   uint32_t last_ack_received = 0;
   int dup_ack = 0;

   // RECEIVE ACK WHILE BEGINNING TO SEND DATA OVER
   while (1)
   {
      // fprintf(stderr, "hio");
      packet recv_pkt;

      // Check for incoming packets
      int bytes_recvd = recvfrom(sockfd, &recv_pkt, sizeof(recv_pkt), 0,
                                 (struct sockaddr *)&client_addr, &client_addr_len);

      if (bytes_recvd > 0)
      {
         // fprintf(stderr, "received");
         print_diag(&recv_pkt, RECV);
         packet_to_host(&recv_pkt);

         // Process regular data packet
         if (recv_pkt.flags & 2)
         { // ACK flag set
            // fprintf(stderr, "acker");
            if (process_ack(&send_win, recv_pkt.ack, sockfd, client_addr))
               clk = clock();
            if (recv_pkt.ack == last_ack_received)
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
                            (struct sockaddr *)&client_addr, sizeof(client_addr));
                     // send_win.last_ack_time = time(NULL);
                  }
                  dup_ack = 0;
               }
            }
            else
            {
               last_ack_received = recv_pkt.ack;
               dup_ack = 0;
            }
         }
         if (!handshake_complete) {
            handshake_complete = true;
            if (recv_pkt.length == 0)
               recv_win.expect_seq += 1;
         }
         // IF THERE IS A PAYLOAD
         if (recv_pkt.length > 0)
         {
            // fprintf(stderr, "recvwin");
            if (recv_pkt.seq >= recv_win.expect_seq)
               add_to_recv_window(&recv_win, &recv_pkt);
            process_received_data(&recv_win, STDOUT_FILENO);

            // Send ACK
            // sendwin next seq needs to be == recv_pkt.ack, and ack needs to be recv_pkt.seq + packet length + 1

            if (send_win.size < MAX_WINDOW_SIZE / MSS)
            {
               char buf[MSS];
               int bytes_read = read(STDIN_FILENO, buf, MSS);

               if (bytes_read > 0)
               {
                  // packet data_pkt = create_packet(send_win.next_seq,
                  //                                 recv_win.expect_seq, bytes_read, 2);
                  packet data_pkt = create_packet(send_win.next_seq,
                                                  recv_win.expect_seq, bytes_read, 2);
                  memcpy(data_pkt.payload, buf, bytes_read);

                  add_to_send_window(&send_win, &data_pkt);
                  print_send_window(&send_win);
                  packet_to_network(&data_pkt);
                  print_diag(&data_pkt, SEND);
                  sendto(sockfd, &data_pkt, sizeof(packet), 0,
                         (struct sockaddr *)&client_addr, client_addr_len);
               } else {
                  // send empty ack
                  packet ack_pkt = create_packet(0, recv_win.expect_seq, 0, 2);
                  packet_to_network(&ack_pkt);
                  print_diag(&ack_pkt, SEND);
                  sendto(sockfd, &ack_pkt, sizeof(packet), 0, 
                        (struct sockaddr *)&client_addr, sizeof(client_addr));
               }
            } else {
               // send empty ack
               packet ack_pkt = create_packet(0, recv_win.expect_seq, 0, 2);
               packet_to_network(&ack_pkt);
               print_diag(&ack_pkt, SEND);
               sendto(sockfd, &ack_pkt, sizeof(packet), 0, 
                     (struct sockaddr *)&client_addr, sizeof(client_addr));
            }
         }
      }
      else
      {
         clock_t now = clock();
         // if (currtime - last_ack_time >= 1)
         if (now - clk > CLOCKS_PER_SEC)
         {
            packet *retx_pkt = get_retransmit_packet(&send_win);
            if (retx_pkt)
            {
               retx_pkt->ack = recv_win.expect_seq;
               packet send_pkt = *retx_pkt;
               packet_to_network(&send_pkt);
               print_diag(&send_pkt, RTOS);
               sendto(sockfd, &send_pkt, sizeof(packet), 0,
                      (struct sockaddr *)&client_addr, client_addr_len);
               clk = clock();
            }
         }
         if (send_win.size < MAX_WINDOW_SIZE / MSS)
         {
            char buf[MSS];
            int bytes_read = read(STDIN_FILENO, buf, MSS);

            if (bytes_read > 0)
            {
               // packet data_pkt = create_packet(send_win.next_seq,
               //                                 recv_win.expect_seq, bytes_read, 2);
               packet data_pkt = create_packet(send_win.next_seq,
                                                recv_win.expect_seq, bytes_read, 2);
               memcpy(data_pkt.payload, buf, bytes_read);

               add_to_send_window(&send_win, &data_pkt);
               print_send_window(&send_win);
               packet_to_network(&data_pkt);
               print_diag(&data_pkt, SEND);
               sendto(sockfd, &data_pkt, sizeof(packet), 0,
                        (struct sockaddr *)&client_addr, client_addr_len);
            }
         }
      }
   }
   // Don't forget to cleanup before exiting
   // cleanup_send_window(&send_win);
   // cleanup_recv_window(&recv_win);

   close(sockfd);
   return 0;
}

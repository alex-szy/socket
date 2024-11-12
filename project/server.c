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
   sendto(sockfd, &syn_ack_pkt, sizeof(syn_ack_pkt), 0,
          (struct sockaddr *)&client_addr, client_addr_len);

   send_win.base_seq = server_isn + 1;
   send_win.next_seq = server_isn + 1;
   recv_win.expect_seq = recv_pkt.seq + 1;

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
            process_ack(&send_win, recv_pkt.ack, sockfd, client_addr);
         }
         // IF THERE IS A PAYLOAD
         if (recv_pkt.length > 0)
         {
            // fprintf(stderr, "recvwin");
            add_to_recv_window(&recv_win, &recv_pkt);
            uint32_t next_expected = process_received_data(&recv_win, STDOUT_FILENO);

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
                  packet_to_network(&data_pkt);
                  print_diag(&data_pkt, SEND);
                  sendto(sockfd, &data_pkt, sizeof(packet), 0,
                         (struct sockaddr *)&client_addr, client_addr_len);
               }
            }
         }
      }
      else
      {
         if (check_retransmit(&send_win))
         {
            packet *retx_pkt = get_retransmit_packet(&send_win);
            if (retx_pkt)
            {
               packet_to_network(retx_pkt);
               // print_diag(&retx_pkt, SEND);
               sendto(sockfd, retx_pkt, sizeof(packet), 0,
                      (struct sockaddr *)&client_addr, client_addr_len);
               send_win.last_ack_time = time(NULL);
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

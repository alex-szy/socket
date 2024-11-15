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
   send_packet(sockfd, create_packet(send_win.seq, 0, 0, 1), &serveraddr, SEND); // SYN flag
   send_win.seq++;

   bool handshake_complete = false;

   // Main loop
   packet recv_pkt;
   while (true)
   {
      int bytes_recvd = recv_packet(sockfd, &recv_pkt, &serveraddr);
      if (bytes_recvd > 0) break;
   }
   //any pkt
   send_packet(sockfd, create_packet(send_win.seq, recv_pkt.seq + 1, 0, 2), &serveraddr, SEND);
   send_win.seq++;
   recv_win.expect_seq = recv_pkt.seq + 1;

   uint32_t last_ack_received = 0;
   int dup_ack = 0;

   while (1)
   {
      // fprintf(stderr, "hi");
      packet recv_pkt;
      // Check for incoming packets
      int bytes_recvd = recv_packet(sockfd, &recv_pkt, &serveraddr);

      if (bytes_recvd > 0)
      {
         // Process regular data packet
         if (recv_pkt.flags & 2)
         { // ACK flag set

            process_ack(&send_win, recv_pkt.ack, sockfd, serveraddr);

            if (recv_pkt.ack == last_ack_received)
            {
               dup_ack++;
               if (dup_ack >= 3)
               {
                  packet *retx_pkt = get_retransmit_packet(&send_win);
                  if (retx_pkt)
                  {
                     retx_pkt->ack = recv_win.expect_seq;
                     send_packet(sockfd, *retx_pkt, &serveraddr, DUPA);
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

         if (recv_pkt.length > 0)
         {
            if (recv_pkt.seq >= recv_win.expect_seq)
            {
               add_to_recv_window(&recv_win, &recv_pkt);
            }
            uint32_t next_expected = process_received_data(&recv_win, STDOUT_FILENO);
            print_recv_window(&recv_win);

            packet data_pkt = create_packet(send_win.seq,
                                                  recv_win.expect_seq, 0, 2);
            // Send ACK
            if (send_win.size < MAX_WINDOW_SIZE / MSS)
            {
               int bytes_read = read(STDIN_FILENO, data_pkt.payload, MSS);

               if (bytes_read > 0)
               {
                  data_pkt.length = bytes_read;
                  add_to_send_window(&send_win, &data_pkt);
               }
            }
            send_packet(sockfd, data_pkt, &serveraddr, SEND);
         }
      }
      else
      {
         if (check_retransmit(&send_win))
         {
            send_win.last_ack_time = clock();
            packet *retx_pkt = get_retransmit_packet(&send_win);
            if (retx_pkt)
            {
               retx_pkt->ack = recv_win.expect_seq;
               send_packet(sockfd, *retx_pkt, &serveraddr, RTOS);
            }
         }
         if (send_win.size < MAX_WINDOW_SIZE / MSS)
         {
            packet data_pkt = create_packet(send_win.seq, recv_win.expect_seq, 0, 2);
            int bytes_read = read(STDIN_FILENO, data_pkt.payload, MSS);

            if (bytes_read > 0)
            {
               data_pkt.length = bytes_read;
               add_to_send_window(&send_win, &data_pkt);
               send_packet(sockfd, data_pkt, &serveraddr, SEND);
            }
         }
      }
   }

   close(sockfd);
   return 0;
}

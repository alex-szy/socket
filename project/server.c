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
      int bytes_recvd = recv_packet(sockfd, &recv_pkt, &client_addr);
      if (bytes_recvd > 0) break;
   }
   // SEND SYNACK
   send_packet(sockfd, create_packet(send_win.seq, recv_pkt.seq + 1, 0, 3), &client_addr, SEND);

   send_win.seq++;
   recv_win.expect_seq = recv_pkt.seq + 1;

   bool handshake_complete = false;

   uint32_t last_ack_received = 0;
   int dup_ack = 0;

   // RECEIVE ACK WHILE BEGINNING TO SEND DATA OVER
   while (1)
   {
      // fprintf(stderr, "hio");
      packet recv_pkt;

      // Check for incoming packets
      int bytes_recvd = recv_packet(sockfd, &recv_pkt, &client_addr);

      if (bytes_recvd > 0)
      {
         // Process regular data packet
         if (recv_pkt.flags & 2)
         { // ACK flag set
            // fprintf(stderr, "acker");
            process_ack(&send_win, recv_pkt.ack, sockfd, client_addr);

            if (recv_pkt.ack == last_ack_received)
            {
               dup_ack++;
               if (dup_ack >= 3)
               {
                  packet *retx_pkt = get_retransmit_packet(&send_win);
                  if (retx_pkt)
                  {
                     retx_pkt->ack = recv_win.expect_seq;
                     send_packet(sockfd, *retx_pkt, &client_addr, DUPA);
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
         if (!handshake_complete)
         {
            handshake_complete = true;
            if (recv_pkt.length == 0)
               recv_win.expect_seq++;
         }
         // IF THERE IS A PAYLOAD
         if (recv_pkt.length > 0)
         {
            // fprintf(stderr, "recvwin");
            if (recv_pkt.seq >= recv_win.expect_seq)
            {
               add_to_recv_window(&recv_win, &recv_pkt);
            }
            process_received_data(&recv_win, STDOUT_FILENO);

            // Send ACK
            // sendwin next seq needs to be == recv_pkt.ack, and ack needs to be recv_pkt.seq + packet length + 1

            packet data_pkt = create_packet(send_win.seq,
                                                  recv_win.expect_seq, 0, 2);
            if (send_win.size < MAX_WINDOW_SIZE / MSS)
            {
               int bytes_read = read(STDIN_FILENO, data_pkt.payload, MSS);

               if (bytes_read > 0)
               {
                  data_pkt.length = bytes_read;
                  add_to_send_window(&send_win, &data_pkt);
               }
            }
            send_packet(sockfd, data_pkt, &client_addr, SEND);
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
               send_packet(sockfd, *retx_pkt, &client_addr, RTOS);
            }
         }
         if (send_win.size < MAX_WINDOW_SIZE / MSS)
         {
            packet data_pkt = create_packet(send_win.seq,
                                                  recv_win.expect_seq, 0, 2);;
            int bytes_read = read(STDIN_FILENO, data_pkt.payload, MSS);

            if (bytes_read > 0)
            {
               data_pkt.length = bytes_read;
               add_to_send_window(&send_win, &data_pkt);
               send_packet(sockfd, data_pkt, &client_addr, SEND);
            }
         }
      }
   }

   close(sockfd);
   return 0;
}

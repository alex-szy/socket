#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include "utils.h"

int main(int argc, char *argv[]) {
	/* 1. Create socket */
	int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
							// use IPv4  use UDP

	/* Make stdin and socket non-blocking */
	int socket_nonblock = fcntl(sockfd, F_SETFL, O_NONBLOCK);
	if (socket_nonblock < 0) die("non-block socket");
	int stdin_nonblock = fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
	if (stdin_nonblock < 0) die("non-block stdin");

	/* 2. Construct server address */
	struct sockaddr_in serveraddr;
	serveraddr.sin_family = AF_INET; // use IPv4
	if (argc > 1 && strcmp(argv[1], "localhost"))
		serveraddr.sin_addr.s_addr = inet_addr(argv[1]);
	else
		serveraddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	// Set sending port
	int SEND_PORT;
	if (argc > 2) SEND_PORT = atoi(argv[2]);
	else SEND_PORT = 8080;
	serveraddr.sin_port = htons(SEND_PORT); // Big endian
	socklen_t serversize = sizeof(serveraddr); // Temp buffer for recvfrom API

	/* 4. Create buffer to store incoming data */
	int BUF_SIZE = 1024;
	char client_buf[BUF_SIZE];
	char server_buf[BUF_SIZE];

	for (;;) {
		/* Send data to server */
		int bytes_read = read(STDIN_FILENO, client_buf, BUF_SIZE);
		if (bytes_read > 0) {
			int did_send = sendto(sockfd, client_buf, bytes_read,
								// socket  send data   how much to send
									0, (struct sockaddr*) &serveraddr,
								// flags   where to send
									sizeof(serveraddr));
			if (did_send < 0) die("send");
		} else if (bytes_read < 0 && errno != EAGAIN) die("stdin");


		/* 5. Listen for response from server */
		int bytes_recvd = recvfrom(sockfd, server_buf, BUF_SIZE,
										// socket  store data  how much
											0, (struct sockaddr*) &serveraddr,
											&serversize);
		// Error if bytes_recvd < 0 :(
		if (bytes_recvd < 0 && errno != EAGAIN) die("receive");
		// Print out data
		else if (bytes_recvd > 0) write(1, server_buf, bytes_recvd);
	}
}

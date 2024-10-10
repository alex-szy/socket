#include <sys/socket.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdbool.h>
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

	/* 2. Construct our address */
	struct sockaddr_in servaddr;
	servaddr.sin_family = AF_INET; // use IPv4
	servaddr.sin_addr.s_addr = INADDR_ANY; // accept all connections
							// same as inet_addr("0.0.0.0")
									 // "Address string to network bytes"
	// Set receiving port
	int PORT;
	if (argc > 1) PORT = atoi(argv[1]);
	else PORT = 8080;
	servaddr.sin_port = htons(PORT); // Big endian

	/* 3. Let operating system know about our config */
	int did_bind = bind(sockfd, (struct sockaddr*) &servaddr,
						sizeof(servaddr));
	// Error if did_bind < 0 :(
	if (did_bind < 0) return errno;

	/* 4. Create buffer to store data */
	int BUF_SIZE = 1024;
	char client_buf[BUF_SIZE];
	char server_buf[BUF_SIZE];

	struct sockaddr_in clientaddr; // Same information, but about client
	socklen_t clientsize = sizeof(clientaddr);

	bool ready_to_send = false;

	for (;;) {/* 5. Listen for data from clients */
		int bytes_recvd = recvfrom(sockfd, client_buf, BUF_SIZE,
								// socket  store data  how much
									0, (struct sockaddr*) &clientaddr,
									&clientsize);

		if (bytes_recvd > 0) {
			// Write to stdout
		 	write(1, client_buf, bytes_recvd);
			char* client_ip = inet_ntoa(clientaddr.sin_addr);
							// "Network bytes to address string"
			int client_port = ntohs(clientaddr.sin_port); // Little endian
			fprintf(stderr, "%s:%d\n", client_ip, client_port);
			// Got the address, now ready to send
			ready_to_send = true;

		} else if (bytes_recvd < 0 && errno != EAGAIN) die("receive");


		/* 7. Send data back to client */
		// We have not received the address yet, cannot send data
		if (!ready_to_send) continue;

		int bytes_read = read(STDIN_FILENO, server_buf, BUF_SIZE);
		if (bytes_read > 0) {
			int did_send = sendto(sockfd, server_buf, bytes_read,
								// socket  send data   how much to send
								0, (struct sockaddr*) &clientaddr,
								// flags   where to send
								sizeof(clientaddr));
			if (did_send < 0) die("send");

		} else if (bytes_read < 0 && errno != EAGAIN) die("stdin");
	}
}

// TODO: need waiting for stdin not to interfere with socket
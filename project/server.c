#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include "common.h"
#include "sec.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: server <port>\n");
        exit(1);
    }

    // Seed the random number generator
    srand(2);

    int stdin_nonblock = fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    if (stdin_nonblock < 0) die("non-block stdin");

    init_sec(0);

    init_server(atoi(argv[1]), read_sec, write_sec);

    transport_listen();
}

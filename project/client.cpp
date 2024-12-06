#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include "common.h"
#include "sec.h"

using namespace std;

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: client <address> <port>\n");
        exit(1);
    }

    // Seed the random number generator
    srand(0);

    int stdin_nonblock = fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    if (stdin_nonblock < 0) die("non-block stdin");

    init_client(atoi(argv[2]), argv[1], read_sec_client, write_sec_client);

    transport_listen();
}

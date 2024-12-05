#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include "common.h"
#include "sec.h"

using namespace std;

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: server <port>\n");
        exit(1);
    }

    // Seed the random number generator
    srand(2);

    int stdin_nonblock = fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
    if (stdin_nonblock < 0) die("non-block stdin");

    ssize_t (*read_func)(uint8_t*, size_t) = [](uint8_t* buf, size_t nbytes) {return read(STDIN_FILENO, buf, nbytes);};
    ssize_t (*write_func)(uint8_t*, size_t) = [](uint8_t* buf, size_t nbytes) {return write(STDOUT_FILENO, buf, nbytes);};

    init_server(atoi(argv[1]), read_func, write_func);

    transport_listen();
}

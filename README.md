# CS 118 Fall 24 Project - Sockets

## Design
I chose to implement the sockets in C, with a small libary of helper functions to manipulate packets, as well as a deque implementation which serves as the send and receive buffers.

## Issues
It was difficult to think of possible edge cases as this is a networking problem. I had issues with the receive buffer, in which I was accepting duplicate packets into the receive buffer, and also accepting packets that were already acked.

## Solutions
To fix the algorithmic problems with the deque implementation, I used gdb to step through the code and make sure that it was behaving as I intended. Additionally, I tried a different code layout and implemented it on the server code, having a separate for loop for each stage in the handshake process. This allowed me to isolate the problems to a smaller section of code, making debugging easier.

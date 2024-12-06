# CS 118 Fall 24 Project - Sockets

## Design
For the security layer, I chose to implement my project in C++, to make use of the standard library's containers. I used vectors as buffers to build the packets, and dumped the contents into the buffers when I was done.

## Issues
This project was relatively easy compared with project 1. However, I had an issue with the client key exchange request testcase which was mysteriously failing despite my logs telling me that the code was working as it should.

## Solutions
Due to the similar interface between my implementation and the starter code given, I was very easily able to swap my transport layer over to the starter code, which fixed the issue.

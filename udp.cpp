/**
Copyright 2014 Joseph A Lewis III <joseph@josephlewis.net>
All Rights Reserved
**/

#include "udp.h"
#include "Debug.hpp"


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>

#include <string>


// get sockaddr, IPv4 or IPv6:
void *UDP::get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

std::string UDP::getHuman(struct sockaddr *sa)
{
    // IPv4 demo of inet_ntop() and inet_pton()
    char str[INET_ADDRSTRLEN];

    if(inet_ntop(AF_INET, sa, str, INET_ADDRSTRLEN) != NULL)
        return std::string(str);

    perror("inet_pton");
    return "ERROR";
}


int UDP::server(int portnumber)
{
    // Set hints
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE; // use my IP


    // Get possible binding points
    struct addrinfo *servinfo; // the place to store the addresses possible
    const char* service = std::to_string(portnumber).c_str();
    int ret = getaddrinfo(NULL, service, &hints, &servinfo);
    if (ret != 0)
    {
        log(ERROR, "get address error %s\n", gai_strerror(ret));
        return -1;
    }


    int sockfd; // eventual place to store the socket
    struct addrinfo *p;
    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            continue;
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            continue;
        }

        break;
    }

    if (p == NULL) {
        log(ERROR, "failed to bind socket\n");
        return -2;
    }

    freeaddrinfo(servinfo);

    return sockfd;
}

// Stolen/Modified from Beej's Guide To Networking
int UDP::send(const char* address, const int port, const char* buffer, const int bytes)
{
    int sockfd;
    struct addrinfo hints, *servinfo, *p;


    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    const char* service = std::to_string(port).c_str();
    int rv = getaddrinfo(address, service, &hints, &servinfo);
    if (rv != 0)
    {
        log(ERROR, "get address info: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and make a socket
    for(p = servinfo; p != NULL; p = p->ai_next)
    {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1)
        {
            log(ERROR, "socket\n");
            continue;
        }

        break;
    }

    if (p == NULL)
    {
        log(ERROR, "failed to bind socket\n");
        return 1;
    }

    if (sendto(sockfd, buffer, bytes, 0, p->ai_addr, p->ai_addrlen) == -1)
    {
        log(ERROR, "sendto error\n");
        return 1;
    }

    freeaddrinfo(servinfo);
    close(sockfd);

    return 0;
}

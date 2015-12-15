/* Copyright 2014 Joseph Lewis <joseph@josephlewis.net>
All Rights Reserved.

*/

#include "IPLookup.h"
#include "Debug.hpp"

#include <string>
#include <iostream>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <fstream>
#include <netdb.h>
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


IPLookup::IPLookup(const char* hostfile)
{
    // read hostfile
    std::ifstream infile(hostfile);
    std::string line;
    uint32_t id = 0;
    while (std::getline(infile, line))
    {

        insertHost(line, id);

        id++;
    }


    _numHosts = id;

    // map the localhost to one of the addresses.

    char hostname[1024]; // large enough for systems with many hostnames.
    int ret = gethostname(hostname, 1024);
    _thisComputer = -1;
    if(ret != 0)
    {
        log(ERROR, "could not determine localhost\n");
        perror("gethostname");
        _thisComputer = -1;
    }
    else
    {
        _thisComputer = lookupName(hostname);
    }
#ifndef NDEBUG

    log(DEBUG, "Generating host mapping:\n");

    for(auto a : _nameMap)
    {
        log(DEBUG, "\t%s -> %d\n", a.first.c_str(), a.second);
    }

    log(DEBUG, "This computer is index: %d\n", _thisComputer);
    log(DEBUG, "---------------------------------------------------------------\n");
#endif
}

void IPLookup::insertHost(std::string line, uint32_t id)
{
    _lineMap.insert(std::make_pair(id, line));  // the given line to a hostname.
    _nameMap.insert(std::make_pair(line, id));  // the given name of the resource in the hosts file.

    // Set hints
    struct addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET; // set to AF_INET to force IPv4
    hints.ai_socktype = SOCK_DGRAM;

    // Get possible binding points
    struct addrinfo *servinfo; // the place to store the addresses possible
    const char* node = line.c_str();
    int ret = getaddrinfo(node, NULL, &hints, &servinfo);
    if (ret == 0)
    {
        struct addrinfo *res;
        int error;

        /* loop over all returned results and do inverse lookup */
        for (res = servinfo; res != NULL; res = res->ai_next)
        {
            char hostname[NI_MAXHOST];

            _addressMap.insert(std::make_pair(std::string(res->ai_addr->sa_data, 14), id));

            error = getnameinfo(res->ai_addr, res->ai_addrlen, hostname, NI_MAXHOST, NULL, 0, 0);
            if (error != 0)
            {
                log(WARN, "error in getnameinfo: %s\n", gai_strerror(error));
                continue;
            }

            if (*hostname != '\0')
                _nameMap.insert(std::make_pair(std::string(hostname), id));
        }
    }
}


int IPLookup::lookupSock(struct sockaddr* theirAddress, socklen_t theirAddressLen)
{
    char hostname[NI_MAXHOST];

    if(getnameinfo(theirAddress, theirAddressLen, hostname, sizeof(hostname), NULL, 0, NI_NAMEREQD) == 0)
    {
        std::string host(hostname);

        if(host == "localhost" && _thisComputer != -1)
        {
            return _thisComputer;
        }

        auto loc = _nameMap.find(host);
        if(loc == _nameMap.end())
        {
            log(WARN, "Could not get index for host with name %s\n", hostname);
            return -1;
        }

        return loc->second;
    }

    return -1;
}

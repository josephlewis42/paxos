/* Copyright 2014 Joseph Lewis <joseph@josephlewis.net>
All Rights Reserved.

*/


#ifndef IP_LOOKUP_H
#define IP_LOOKUP_H

#include <map>
#include <netinet/in.h>
#include <string>

/**
Provides an ip lookup mechanism for a piece of software, translating hosts
we interact with into numbers that can be used.
**/
class IPLookup
{
    public:
    /**
    Sets up IPLookup with the given hosts file. Hosts are indexed starting
    with 0.
    **/
    IPLookup(const char* hostfilePath);

    /**
    Looks up the given name to return the ID. returns -1 if not found.
    **/
    int lookupName(const char* hostname)
    {
        auto val = _nameMap.find(std::string(hostname));
        if( val == _nameMap.end())
            return -1;
        return (int) val->second;
    }

    /**
    Returns the localhost id. or -1 if not specified in the
    given configuration.
    **/
    int localhost()
    {
        return _thisComputer;
    }

    /**
    Looks up the id of the given connection, returns the id or -1 if not in
    the hosts file.
    **/
    int lookupSock(struct sockaddr* theirAddress, socklen_t theirAddressLen);

    int getNumberOfHosts() const {return _numHosts;}
    std::string hostnameForId(uint32_t id) {return _lineMap[id];}

private:
    std::map<std::string, uint32_t> _nameMap;
    std::map<std::string, uint32_t> _addressMap;
    std::map<uint32_t, std::string> _lineMap;
    uint32_t _numHosts;
    int _thisComputer;

    void insertHost(std::string hostname, uint32_t id);

};

#endif

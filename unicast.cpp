/**
Copyright 2014 - Joseph Lewis <joseph@josephlewis.net>
All Rights Reserved

Part of CS505 Lab 2 - Reliable Total Order Multicast Protocol

Provides a UDP based unicast service.
**/

#include "udp.h"
#include "unicast.h"
#include "IPLookup.h"
#include "Debug.hpp"

#include <arpa/inet.h>
#include <fstream>
#include <string>

#include <iostream>

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

const int MAX_UDP_PACKET_SIZE_BYTES = 65507;

Unicast::Unicast(const char* hostfile, uint32_t portNumber, uint32_t retransmit_time_ms)
:IPLookup(hostfile),
_port(portNumber),
_retransmitMS(retransmit_time_ms)
{
    _socket = UDP::server(portNumber);
}

void Unicast::retransmit()
{
    // retransmit all things that have not yet gotten an ack

    if(_retransmitTimer.getMsSinceInit() < _retransmitMS)
        return;

    //log(DEBUG, "retransmitting %d items\n", _retransmitQueue.size());

    for(auto it : _retransmitQueue)
    {
        //auto ptr = (uint32_t*) &it.second[0];
        //log(DEBUG, "\t%d to %d\n", ptr[0], it.first);

        auto host = hostnameForId(it.first);
        UDP::send(host.c_str(), _port, (char*) &it.second[0], it.second.size());
    }

    _retransmitTimer.set_start_time();
}

void Unicast::reliableSend(const uint32_t node, const std::vector<char> &message)
{
    // send and add to list of things to retransmit.
    std::vector<char> message2(message);

    //log(DEBUG, "doing reliable send to %d of size %d\n", node, message2.size());

    _retransmitQueue.push_back(std::make_pair(node, message2));

    // transmit the first time.
    auto host = hostnameForId(node);
    UDP::send(host.c_str(), _port, &(message2[0]), message2.size());
}

bool Unicast::allMessagesDelivered()
{
    log(DEBUG, "have %d messages left\n", _retransmitQueue.size());
    return _retransmitQueue.size() == 0;
}


void Unicast::handleAck(paxos::UnivAck_t* msg, int sender)
{

    for(uint32_t i = 0; i < _retransmitQueue.size(); i++)
    {
        auto sender_message = _retransmitQueue[i];
        if(sender_message.first != (uint32_t)sender)
            continue;

        auto vec = sender_message.second;
        if(vec.size() != msg->size)
            continue;

        bool equals = true;
        for(uint32_t j = 0; j < vec.size(); j++)
        {
            if(vec[j] != msg->packet[j])
                equals = false;
        }

        if(! equals)
            continue;

        // remove from the list of things to retransmit.
        _retransmitQueue.erase(_retransmitQueue.begin() + i);
        return;
    }
}


void Unicast::sendAck(uint32_t node, std::vector<char> msg_array)
{
    paxos::UnivAck_t a;
    a.type = 1024;
    a.size = msg_array.size();

    for(uint32_t i = 0; i < msg_array.size(); i++)
    {
        a.packet[i] = msg_array[i];
    }

    std::vector<char> tosend;
    paxos::pack_UnivAck(a, tosend);

    auto host = hostnameForId(node);

    //log(DEBUG, "Sending ack of size: %d to host: %d\n",tosend.size(), node);

    UDP::send(host.c_str(), _port, &tosend[0], tosend.size());
}

int Unicast::readOrTimeout(char* buffer, int& length, int timeoutMs)
{
    recv:
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = timeoutMs * 1000;

    if (setsockopt(_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        log(WARN, "Error setting timeout\n");
        return -1;
    }


    struct sockaddr_storage their_addr;
    int numbytes;
    socklen_t addr_len;
    addr_len = sizeof their_addr;
    if ((numbytes = recvfrom(_socket, buffer, MAX_UDP_PACKET_SIZE_BYTES , 0,
                             (struct sockaddr *)&their_addr, &addr_len)) == -1) {
        if(errno != 11)
            perror("recvfrom");

        return -1;
    }

    length = numbytes;


    struct sockaddr* theirp = (struct sockaddr *)&their_addr;
    int ra =  lookupSock(theirp, addr_len);

    if(ra == -1)
        return -2;



    // reliability functions
    std::vector<char> vecbuf;
    for(int i = 0; i < length; i++)
        vecbuf.push_back(buffer[i]);

    uint32_t* bufstar = (uint32_t*) buffer;
    if(bufstar[0] == 1024)
    {
        //LOG(TRACE, "handling ack");
        handleAck((paxos::UnivAck_t*) buffer, ra);
        length = 0;
        ra = -1;

        goto recv;
    }
    else
    {
        sendAck(ra, vecbuf);
    }


    return ra;
}


void Unicast::sendMessage(const std::vector<char>& msg)
{

    std::vector<char> msgcopy(msg);

    //log(TRACE, "Sending message: %d\n", ((uint32_t*)(&msgcopy[0]))[0]);
    for(int i = 0; i < getNumberOfHosts(); i++)
    {
        reliableSend(i, msg);
    }

}








void Unicast::unreliableSend(const uint32_t node, const std::vector<char> &message)
{
    //log(DEBUG, "doing unreliable send to %d of size %d\n", node, message.size());

    auto host = hostnameForId(node);
    UDP::send(host.c_str(), _port, &(message[0]), message.size());
}



void Unicast::unreliableBroadcastMessage(const std::vector<char>& msg)
{
    log(TRACE, "Broadcasting message: %d\n", ((uint32_t*)(&msg[0]))[0]);

    for(int i = 0; i < getNumberOfHosts(); i++)
    {
        unreliableSend(i, msg);
    }
}

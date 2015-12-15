/**
Copyright 2014 - Joseph Lewis <joseph@josephlewis.net>
All Rights Reserved

Part of CS505 Lab 2 - Reliable Total Order Multicast Protocol

Provides a UDP based unicast service.
**/

#pragma once
#ifndef UNICAST_H
#define UNICAST_H

#include <list>
#include <mutex>
#include <map>
#include <vector>
#include <sys/socket.h>
#include <utility>

#include "Timer.hpp"
#include "IPLookup.h"
#include "Debug.hpp"
#include "paxos.h"


class Unicast : public IPLookup
{
    // IPLookup
    // int localhost() // gets id of localhost
    // int getNumberOfHosts() // gets number of processes
public:
    Unicast(const char* hostfile, uint32_t portNumber, uint32_t retransmit_time_ms);

    // reads a socket or times out, if read returns the id of the message sender
    // filling the buffer and setting the length.
    int readOrTimeout(char* buffer, int& length, int timeoutMs);

    void retransmit();  // retransmits messages.
    void handleAck(paxos::UnivAck_t* msg, int sender); // handles the ack
    void sendAck(uint32_t node, std::vector<char> msg); // send an ack for a message we got


    void reliableSend(const uint32_t node, const std::vector<char> &message);
    void sendMessage(const std::vector<char> &message);

    bool allMessagesDelivered(); // tells us if all messages have been delivered



    // sends a single message to a single node
    void unreliableSend(const uint32_t node, const std::vector<char> &message);

    // sends an unreliable broadcast
    void unreliableBroadcastMessage(const std::vector<char>& msg);



    // true if the ack is an ack for the given message, false if it is not.
    static inline bool identifies(char* universal_ack, std::vector<char> other){
        paxos::UnivAck_t* ua = (paxos::UnivAck_t*) universal_ack;

        for(uint32_t i = 0; i < ua->size; i++)
        {
            if(ua->packet[i] != other[i])
                return false;
        }

        return true;
    }

    private:
        std::vector<std::pair<uint32_t, std::vector<char> > > _retransmitQueue;
        uint32_t _port;
        uint32_t _socket;
        Timer _retransmitTimer;
        uint32_t _retransmitMS;
};

#endif

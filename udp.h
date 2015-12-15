/**
Provides the basic UDP mechanism for waiting for incoming messages, processing them, and
sending them.

Copyright 2014 Joseph A Lewis III <joseph@josephlewis.net>
All Rights Reserved
**/

#include <functional>
#include <string>


class UDP
{
    public:


    /**
     * starts a UDP client on the given port
     * @param portnumber - the number of the port to listen on
     *
     * @return - something on failure, an fd on success.
     **/
    static int server(int portnumber);

    /**
     * Sends a single datagram socket to the given address from the buffer
     * for the given number of bytes.
     *
     * @return 0 on success, anything else on failure.
     **/
    static int send(const char* address, const int port, const char* buffer, const int bytes);

    static void *get_in_addr(struct sockaddr *sa);

    static std::string getHuman(struct sockaddr *sa);

};

/**
Provides a basic implementation of a byzantine agreement protocol.

Copyright 2014 Joseph A Lewis III <joseph@josephlewis.net>
All Rights Reserved
**/

#include "optionparser.h"
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <thread>
#include <set>
#include <chrono>
#include <queue>
#include <map>
#include <cstring>

#include "unicast.h"
#include "Timer.hpp"
#include "Debug.hpp"
#include "psb.h"
#include "paxos.h"

extern "C" {
#include "dyad.h"
}



const int MAX_UDP_PACKET_SIZE_BYTES = 65507;
const int UDP_PORT_MIN = 1024;
const int UDP_PORT_MAX = 65536;
const int READ_TIMEOUT_MS = 20;
const int RETRANSMIT_TIME_MS = 1000;

#define IS_VALID_UDP(port) ((port >= UDP_PORT_MIN) && (port <= UDP_PORT_MAX))

void Paxos(const char* hostfile, const int paxosport, const int serverport);
void sync(const char* hostfile, const int port);


// Option Parser + Validation
static option::ArgStatus NonEmpty(const option::Option& option, bool msg)
{
    if (option.arg != 0 && option.arg[0] != 0)
        return option::ARG_OK;

    if(msg)
        std::cerr << "ERROR: " << option.name << " requires a non-empty argument" << std::endl;

    return option::ARG_ILLEGAL;
}

static option::ArgStatus Numeric(const option::Option& option, bool msg)
{
    char* endptr = 0;
    if (option.arg != 0 && strtol(option.arg, &endptr, 10)){};
    if (endptr != option.arg && *endptr == 0)
       return option::ARG_OK;

    if (msg)
        std::cerr << "ERROR: Option '" << option << "' requires a numeric arg." << std::endl;

    return option::ARG_ILLEGAL;
}

enum  optionIndex { UNKNOWN, HELP, PORT, HOST, SERVER, DBG };
const option::Descriptor usage[] =
{
    {UNKNOWN, 0,"" , ""    ,    option::Arg::None,  "USAGE: proj2 -p port -h hostfile -c count [--debug]\n\n"
                                                    "Options:" },
    {HELP,    0, "" , "help",   option::Arg::None,  "  --help  \tPrint usage and exit." },
    {HOST,    0, "h", "",       NonEmpty,           "  -h  \tPath to a file containing a list of hostnames for each process." },
    {PORT,    0, "p", "",       Numeric,            "  -p  \tpaxos port (udp) 1024 to 65535." },
    {SERVER,   0, "s", "",       Numeric,            "  -s  \tserver port (tcp) 1024 to 65535" },
    {DBG,     0, "" , "debug",  option::Arg::None,  "  --debug \tTurns on debugging for this process." },
    {UNKNOWN, 0, "" , "",       option::Arg::None,  "\nExamples:\n"
                                                    "  Normal:     proj3 -p 1024 -h hosts.txt -s 1025\n"
                                                    "  Debug:      proj3 -p 1024 -h hosts.txt -s 1025 --debug\n"},
    {0,0,0,0,0,0}
};



int main(int argc, char* argv[])
{

    argc -= (argc > 0);
    argv += (argc > 0); // skip program name argv[0] if present

    option::Stats  stats(usage, argc, argv);
    std::vector<option::Option> options(stats.options_max);
    std::vector<option::Option> buffer(stats.buffer_max);
    option::Parser parse(usage, argc, argv, &options[0], &buffer[0]);

    if (parse.error())
    {
        return 1;
    }

    if ( ! ( options[SERVER] &&
             options[PORT] &&
             options[HOST] ) ||
         options[HELP] ||
         argc == 0)
    {
        option::printUsage(std::cout, usage);
        return 0;
    }

    // Parse actual values

    int server_port = atoi(options[SERVER].arg);
    int paxos_port = atoi(options[PORT].arg);
    const char* hostfile = options[HOST].arg;

    // turn on/off logging if needed
    setLoggingLevel((options[DBG])? TRACE : OFF);

    if( ! IS_VALID_UDP(paxos_port) || ! IS_VALID_UDP(server_port) )
    {
        std::cerr << "Invalid port number, must be non-reserved!" << std::endl;
        exit(1);
    }


    //sync(hostfile, paxos_port);
    LOG(INFO, "Starting Paxos Protocol");
    Paxos(hostfile, paxos_port, server_port);
}



void sync(const char* hostfile, const int port)
{
    Unicast com(hostfile, port + 1, 20);

    if(com.getNumberOfHosts() < 3)
    {
        std::cerr << "Invalid number of hosts!" << std::endl;
        exit(1);
    }

    char buffer[MAX_UDP_PACKET_SIZE_BYTES];
    int length;

    // we use acks to sync a process.
    bool seenAcks[com.getNumberOfHosts()];
    int seenAcksCt = 0; // we know we're alive.

    std::vector<char> message;
    message.push_back('a');
    message.push_back('c');
    message.push_back('k');

    com.sendMessage(message);

    int timeout = 10;

    while(! com.allMessagesDelivered() && timeout >= 0)
    {
        int id = com.readOrTimeout(buffer, length, 999);


        if( id >= 0 && seenAcks[id] != true)
        {
            seenAcksCt ++;
            seenAcks[id] = true;

            log(DEBUG, "Host %d seen alive (seen %d of %d)\n", id, seenAcksCt, com.getNumberOfHosts());
        }


        if( seenAcksCt == com.getNumberOfHosts() )
        {
            log(DEBUG, "Seen all acks, waiting for other hosts.\n");

            if(id < 0)
                timeout -= 1;
        }

        com.retransmit();
    }
}

int myserverid;


class Client
{
public:
    int id;
    int updateno;

    Client()
    :id(0),
    updateno(0)
    {}
};

std::map<int, paxos::Client_Update_t> updates;


static void onData(dyad_Event *e) {
    int clientid;
    int updateno;

    log(INFO, "ondata %s\n", e->data);

    if(sscanf(e->data, "%d\t%d", &clientid, &updateno) == 2)
    {
        log(INFO, "Got update: %d from client: %d\n", updateno, clientid);

        dyad_setTimeout(e->stream, 0);

        Handle_New_Message(clientid, updateno);

        auto cli = (Client*) e->udata;
        cli->id = clientid;
        cli->updateno = updateno;
    }
}


static void onTick(dyad_Event *e) {
    LOG(DEBUG, "handling tick");
    auto cli = (Client*) e->udata;

    if(updates.find(cli->id) != updates.end())
    {
        LOG(INFO, "delivering");

        dyad_writef(e->stream, "%d\n", updates[cli->id].timestamp);
        updates.erase(cli->id);

        delete (Client*) e->udata;
        dyad_end(e->stream);
    }
}

static void onAccept(dyad_Event *e) {
    Client* client = new Client();

  dyad_addListener(e->remote, DYAD_EVENT_DATA, onData, client);
  dyad_addListener(e->remote, DYAD_EVENT_TICK, onTick, client);
}




void Paxos( const char* hostfile, const int paxosport, const int serverport)
{

    Unicast com(hostfile, paxosport, RETRANSMIT_TIME_MS);
    char buffer[MAX_UDP_PACKET_SIZE_BYTES];
    int length;

    myserverid = com.localhost();

    initPaxos(com);

    // start TCP
    dyad_init();
    dyad_Stream *serv = dyad_newStream();
    dyad_setTimeout(serv, 0);
    dyad_addListener(serv, DYAD_EVENT_ACCEPT, onAccept, NULL);
    dyad_listen(serv, serverport);

    while( true )
    {
        while(true)
        {
            int id = com.readOrTimeout(buffer, length, 20);
            setLastSender(id);

            if(id < 0)
                break;


            if(Conflict(buffer))
            {
                // ignore conflicting messages

                log(DEBUG, "-------------------------------------------------------\n");
                log(DEBUG, "Ignoring message %d from %d (of length: %d)\n", ((uint32_t*)buffer)[0], id, length);
                prettyPrint(buffer);

                continue;
            }

            log(TRACE, "-------------------------------------------------------\n");
            log(TRACE, "Recvd message %d from %d (of length: %d)\n", ((uint32_t*)buffer)[0], id, length);
            prettyPrint(buffer);


            parse_message(buffer, length);
        }


        //com.retransmit(); // provide reliability functions
        Check_Timers(); // update paxos
        dyad_update(); // update our TCP client stuff
    }

    dyad_shutdown();
}





void reply_to_client(paxos::Client_Update_t update)
{
    log(DEBUG, "replying to client %d\n", update.client_id);
    updates[update.client_id] = update;
}

/**

Copyright 2014 Joseph Lewis III <joseph@josephlewis.net>
All Rights Reserved

This file is part of the Paxos protocol coming from Paxos for System Builders.

**/



#include "optionparser.h"
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <cstring>

#include "unicast.h"
#include "Debug.hpp"

extern "C" {
#include "dyad.h"
}

const int UDP_PORT_MIN = 1024;
const int UDP_PORT_MAX = 65536;

#define IS_VALID_UDP(port) ((port >= UDP_PORT_MIN) && (port <= UDP_PORT_MAX))


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

enum  optionIndex { UNKNOWN, HELP, SERVER, HOST, ID, COMMANDFILE, DBG };
const option::Descriptor usage[] =
{
    {UNKNOWN,       0, "" , "",     option::Arg::None,  "USAGE: client -p port -h hostfile -c count [--debug]\n\nOptions:"},
    {HELP,          0, "" , "help", option::Arg::None,  "  --help  \tPrint usage and exit." },
    // Protocl info
    {HOST,          0, "h", "",     NonEmpty,           "  -h  \tPath to a file containing a list of hostnames for each process." },
    {SERVER,        0, "s", "",     Numeric,            "  -s  \tserver port (tcp) 1024 to 65535" },
    {ID,            0, "i", "",     Numeric,            "  -i  \ta unique client id (integer)" },
    {COMMANDFILE,   0, "f", "",     NonEmpty,           "  -f  \tcommand file, list of updates from the client in the format [<hostname> <update>\\n]+" },

    {DBG,     0, "" , "debug",  option::Arg::None,  "  --debug \tTurns on debugging for this process." },
    {UNKNOWN, 0, "" , "",       option::Arg::None,  "\nExamples:\n"
                                                    "  Normal:     proj3 -p 1024 -h hosts.txt -s 1025\n"
                                                    "  Debug:      proj3 -p 1024 -h hosts.txt -s 1025 --debug\n"},
    {0,0,0,0,0,0}
};


int currentUpdate;
int clientId;
int serverId;
bool updateRecvd = false;
bool connected = false;

static void onConnect(dyad_Event *e)
{
    connected = true;
    dyad_writef(e->stream, "%d\t%d\n", clientId, currentUpdate);
}

static void onData(dyad_Event *e) {

    log(DEBUG, "Got data: %s\n", e->data);

    updateRecvd = true;
    std::cout << clientId << ": Update " << currentUpdate << " sent to server " << serverId << " is executed" << std::endl;
    // dyad_end
    dyad_end(e->stream);
}

static void onClose(dyad_Event *e) {
    if(!connected)
    {
        std::cout << clientId << ": Unable to send update " << currentUpdate << " to server " << serverId << std::endl;
        return;
    }

    if(!updateRecvd)
    {
        std::cout << clientId << ": Connection for update " << currentUpdate << " is closed by server " << serverId << std::endl;
    }
}



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
             options[HOST] &&
             options[ID] &&
             options[COMMANDFILE]) ||
         options[HELP] ||
         argc == 0)
    {
        option::printUsage(std::cout, usage);
        return 0;
    }

    // Parse actual values

    int server_port = atoi(options[SERVER].arg);
    clientId = atoi(options[ID].arg);
    const char* hostfile = options[HOST].arg;
    const char* commandfile = options[COMMANDFILE].arg;

    // turn on/off logging if needed
    setLoggingLevel((options[DBG])? TRACE : OFF);

    if( ! IS_VALID_UDP(server_port) )
    {
        std::cerr << "Invalid port number, must be non-reserved!" << std::endl;
        exit(1);
    }

    LOG(INFO, "Set up, starting to sync.");

    //Unicast unicast(hostfile, server_port, 0);
    IPLookup ip(hostfile);

    dyad_init();

    std::ifstream infile(commandfile);

    if(! infile.is_open())
    {
        std::cerr << commandfile << "is not valid" << std::endl;
        exit(1);
    }


    std::string host;

    while (infile >> host >> currentUpdate)

    {
        //currentUpdate++;
        serverId = ip.lookupName(host.c_str());
        updateRecvd = false;
        connected = false;


        std::cout << clientId << ": Sending update " << currentUpdate << " to server " << serverId << std::endl;

        dyad_Stream *s = dyad_newStream();
        dyad_addListener(s, DYAD_EVENT_CONNECT, onConnect, NULL);
        dyad_addListener(s, DYAD_EVENT_LINE,    onData,    NULL);
        dyad_addListener(s, DYAD_EVENT_CLOSE,   onClose,   NULL);
        dyad_connect(s, host.c_str(), server_port);

        while (dyad_getStreamCount() > 0) {
          dyad_update();
        }
    }

    dyad_shutdown();
    return 0;

}

#include "unicast.h"
#include "paxos.h"

#ifndef psb_h
#define psb_h

void initPaxos(Unicast& unicast);

void setLastSender(int ls);

void reply_to_client(paxos::Client_Update_t update);

void Handle_New_Message(int clientid, int updateno); // handle cilent requests

void Check_Timers(); // check that the timers are still valid

bool Conflict(const char* message);

void prettyPrint(const char* message);

void parse_message(char* buffer, int bufsize);

#endif

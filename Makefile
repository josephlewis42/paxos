CC=g++
C=gcc
CCFLAGS= -c -g -Wall --std=c++0x
CFLAGS= -c -g -Wall
COMMON=IPLookup.o udp.o  Debug.o dyad.o

all: server client

server:  $(COMMON) unicast.o paxos.o psb.o main.o
	$(CC) $(COMMON) unicast.o paxos.o psb.o main.o -o server

client: $(COMMON) client.o
	$(CC) $(COMMON) client.o  -o client

clean:
	rm *.o server client

.cpp.o :
	$(CC) $(CCFLAGS) $< -o $@

.c.o :
	$(C) $(CFLAGS) $< -o $@

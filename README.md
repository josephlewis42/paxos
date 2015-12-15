# paxos
A (probably very buggy) paxos implementation in C++. It's based on a slightly modified version of Paxos Made Simple because the original article had a few omissions.

# Inforomation

Please compile this project on a machine that allows the auto keyword and
preferably c++11 extensions.

The servers must be executed and up before running the client as the client
handler does not start in the servers until all servers are up for a few
seconds and have established initial contact with one another.

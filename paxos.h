#ifndef paxos_h
#define paxos_h

#include <deque>
#include <cstdint>
#include <cstdio>
#include <vector>

// user supplied typedefs
#define UDP_PACKET_SIZE_BYTES 65535


namespace paxos
{
    // Message definitions


    struct Client_Update_t {
        uint32_t type;
        uint32_t client_id;
        uint32_t server_id;
        uint32_t timestamp;
        uint32_t update;
    };

    struct View_Change_t {
        uint32_t type;
        uint32_t server_id;
        uint32_t attempted;
    };

    struct VC_Proof_t {
        uint32_t type;
        uint32_t server_id;
        uint32_t installed;
    };

    struct Prepare_t {
        uint32_t type;
        uint32_t server_id;
        uint32_t view;
        uint32_t local_aru;
    };

    struct Proposal_t {
        uint32_t type;
        uint32_t server_id;
        uint32_t view;
        uint32_t seq;
        Client_Update_t update;
    };

    struct Accept_t {
        uint32_t type;
        uint32_t server_id;
        uint32_t view;
        uint32_t seq;
    };

    struct Globally_Ordered_Update_t {
        uint32_t type;
        uint32_t server_id;
        uint32_t seq;
        Client_Update_t update;
    };

    struct Prepare_OK_t {
        uint32_t type;
        uint32_t server_id;
        uint32_t view;
        uint32_t total_proposals;
        Proposal_t proposals[(UDP_PACKET_SIZE_BYTES / sizeof(Proposal_t))];
        uint32_t total_globally_ordered_updates;
        Globally_Ordered_Update_t globally_ordered_updates[(UDP_PACKET_SIZE_BYTES / sizeof(Globally_Ordered_Update_t))];
    };

    struct UnivAck_t {
        uint32_t type;
        uint32_t size;
        char packet[UDP_PACKET_SIZE_BYTES];
    };

    struct prefix_t {
        uint32_t type;
    };


    // Forward declarations
    void handle_Client_Update(Client_Update_t var); // User supplied
    void handle_View_Change(View_Change_t var); // User supplied
    void handle_VC_Proof(VC_Proof_t var); // User supplied
    void handle_Prepare(Prepare_t var); // User supplied
    void handle_Proposal(Proposal_t var); // User supplied
    void handle_Accept(Accept_t var); // User supplied
    void handle_Globally_Ordered_Update(Globally_Ordered_Update_t var); // User supplied
    void handle_Prepare_OK(Prepare_OK_t var); // User supplied
    void handle_UnivAck(UnivAck_t var); // User supplied
    void handle_invalid_message(const char* message); // usesupplied, when the parser encounters an error

    void update(int length, char* buffer);
    void clear();

    void pack_Client_Update(Client_Update_t input, std::vector<char> &message);
    void pack_View_Change(View_Change_t input, std::vector<char> &message);
    void pack_VC_Proof(VC_Proof_t input, std::vector<char> &message);
    void pack_Prepare(Prepare_t input, std::vector<char> &message);
    void pack_Proposal(Proposal_t input, std::vector<char> &message);
    void pack_Accept(Accept_t input, std::vector<char> &message);
    void pack_Globally_Ordered_Update(Globally_Ordered_Update_t input, std::vector<char> &message);
    void pack_Prepare_OK(Prepare_OK_t input, std::vector<char> &message);
    void pack_UnivAck(UnivAck_t input, std::vector<char> &message);
}

#endif

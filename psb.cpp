/**
 * Copyright 2014 - Joseph Lewis III <joseph@josephlewis.net>
 * All Rights Reserved
 *
 *
 * An implementation of Paxos from Paxos For System Builders
**/

//#define NDEBUG

#include <cstdint>
#include <map>
#include <deque>
#include <vector>
#include <cmath>
#include <cstring>
#include <sys/time.h>
#include <iostream>
//#include <assert.h>


#include "Debug.hpp"
#include "paxos.h"
#include "psb.h"
#include "Timer.hpp"
#include "unicast.h"

#define MSG_TYPE(message) (((uint32_t*)message)[0])


#define MAX_SERVERS 255
#define MAX_CLIENTS 255
#define MAX_SEQS 1024
#define DEFAULT_PROGRESS_TIMER_MS 5000
#define DEFAULT_UPDATE_TIMER_MS 100
#define DEFAULT_VC_PROOF_TIMER_MS 100
#define DEFAULT_PREPARE_TIMER_MS 50
#define DEFAULT_PROPOSAL_TIMER_MS 50


// for simple defs here in this file.
typedef paxos::Client_Update_t Client_Update_t;
typedef paxos::Proposal_t Proposal_t;
typedef paxos::Globally_Ordered_Update_t Globally_Ordered_Update_t;
typedef paxos::View_Change_t View_Change_t;
typedef paxos::VC_Proof_t VC_Proof_t;
typedef paxos::Prepare_t Prepare_t;
typedef paxos::Accept_t Accept_t;
typedef paxos::Prepare_OK_t Prepare_OK_t;

typedef uint32_t timestamp;


enum message_types{
    CLIENT_UPDATE = 1,
    VIEW_CHANGE = 2,
    VC_PROOF = 3,
    PREPARE = 4,
    PROPOSAL = 5,
    ACCEPT = 6,
    GLOBALLY_ORDERED_UPDATE = 7,
    PREPARE_OK = 8
};


enum states{
    LEADER_ELECTION=0,
    REG_LEADER=1,
    REG_NONLEADER=2
};

const char *STATENAMES[3] = {"leader election", "reg leader", "reg nonleader"};


// STRUCTS

struct datalist_t {
    uint32_t total_proposals;
    Proposal_t proposals[(UDP_PACKET_SIZE_BYTES / sizeof(Proposal_t))];
    uint32_t total_globally_ordered_updates;
    Globally_Ordered_Update_t globally_ordered_updates[(UDP_PACKET_SIZE_BYTES / sizeof(Globally_Ordered_Update_t))];
} datalist_t;

uint32_t num_servers;

struct global_slot
{
    Proposal_t prop;
    bool has_proposal;
    Accept_t accepts[MAX_SERVERS];
    bool has_accepts[MAX_SERVERS];
    int num_accepts;
    Globally_Ordered_Update_t update;
    bool has_update;

    global_slot()
    {
        LOG(TRACE, "Constructing global slot");
        prop = {};
        has_proposal = false;
        clear_accepts();
        update = {};
        has_update = false;
    }

    void clear_accepts()
    {
        for(int i = 0; i < MAX_SERVERS; i++)
            has_accepts[i] = 0;
        num_accepts = 0;
    }

    bool has_enough_accepts_for_proposal()
    {
        if(! has_proposal)
        {
            LOG(ERROR, "we shouldn't get here");
            return false;
        }

        LOG(TRACE, "Have accept:");
        int numaccepts = 0;
        for(int i = 0; i < MAX_SERVERS; i++)
        {
            if(!has_accepts[i])
                continue;

            auto accept = accepts[i];
            prettyPrint((char*) &accept);

            if(accept.view == prop.view)
            {
                numaccepts++;
            }
        }

        log(TRACE, "got %d from view %d, need %f\n", numaccepts, prop.view, floor(num_servers / 2.0));

        if(numaccepts >= floor(num_servers / 2.0))
        {
            LOG(TRACE, "\tYes enough accepts.");
            return true;
        }

        LOG(TRACE, "\tNo, not enough accepts.");
        return false;
    }
};


// VARIABLES

Unicast* unicast; // an instance of unicast to do our transmitting.

Client_Update_t Pending_Updates[MAX_CLIENTS];
bool Pending_Updates_Set[MAX_CLIENTS];

std::deque<Client_Update_t> update_queue;
std::map<int, struct global_slot> global_history;


timestamp Last_Executed[MAX_CLIENTS];
timestamp Last_Enqueued[MAX_CLIENTS];

std::map<int, View_Change_t> vc;
std::map<int, Prepare_OK_t> oks;


Prepare_t Prepare;
bool prepare_is_set = false;
Timer prepare_timer;

std::deque<Proposal_t> Proposal_Retransmit_Queue;
Timer proposal_timer;

uint32_t my_server_id;
uint32_t last_attempted;
uint32_t last_installed;
uint32_t local_aru;
uint32_t last_proposed;
Timer progress_timer;
Timer update_timer[MAX_CLIENTS];
Timer proof_timer;
int State;


// forward declaratoins
void Shift_To_Leader_Election(int view);
bool Preinstall_Ready(int view);
void Shift_To_Prepare_Phase();
void Shift_To_Reg_Non_Leader();
struct datalist_t Construct_Data_List(int aru);
void Shift_To_Reg_Leader();
bool View_Prepared_Ready(int view);
void Enqueue_Unbound_Pending_Updates();
void Remove_Bound_Updates_From_Queue();
void Send_Proposal();
bool Globally_Ordered_Ready(int seq);
void Advance_Aru();
bool Enqueue_Update(Client_Update_t U);
bool Leader_Of_Last_Attempted();
void Add_To_Pending_Updates(Client_Update_t U);
int Get_Leader();
void Client_Update_Handler(Client_Update_t U);
timestamp get_timestamp();
void Upon_Executing_A_Client_Update(Client_Update_t U);

////////////////////////////////////////////////////////////////////////////////
// Helper Methods
////////////////////////////////////////////////////////////////////////////////

bool client_update_equal(Client_Update_t a, Client_Update_t b)
{
    //assert(a.type == CLIENT_UPDATE);
    return a.type == b.type &&
            a.server_id == b.server_id &&
            a.client_id == b.client_id &&
            a.timestamp == b.timestamp &&
            a.update == b.update;
}

bool proposal_equal(Proposal_t a, Proposal_t b)
{
    //assert(a.type == PROPOSAL);
    return a.type == b.type &&
            a.server_id == b.server_id &&
            a.view == b.view &&
            client_update_equal(a.update, b.update);

}

bool globally_ordered_update_equal(Globally_Ordered_Update_t a, Globally_Ordered_Update_t b)
{
    //assert(a.type == GLOBALLY_ORDERED_UPDATE);

    return a.type == b.type &&
            a.server_id == b.server_id &&
            a.seq == b.seq &&
            client_update_equal(a.update, b.update);
}

void set_insert_proposal(Proposal_t proposal, Proposal_t *list, uint32_t& count)
{
    //assert(proposal.type == PROPOSAL);

    for(uint32_t i = 0; i < count; i++)
    {
        auto tmp = list[i];
        if(proposal_equal(proposal, tmp))
            return;
    }
    count++;
    list[count] = proposal;
}

void set_insert_globally_ordered_update(Globally_Ordered_Update_t glob, Globally_Ordered_Update_t* list, uint32_t& count)
{
    for(uint32_t i = 0; i < count; i++)
    {
        auto tmp = list[i];
        if(globally_ordered_update_equal(glob, tmp))
            return;
    }
    count++;
    list[count] = glob;
}

bool isBound(const Client_Update_t& cut)
{
    for(auto iter : global_history)
    {
        auto global = iter.second;
        if(! global.has_proposal)
            continue;

        if(client_update_equal(global.prop.update, cut))
            return true;
    }

    return false;
}

bool inProposalQueue(const Client_Update_t& cut)
{
    for(auto iter : update_queue)
    {
        if(client_update_equal(iter, cut))
        {
            return true;
        }
    }

    return false;
}

View_Change_t Construct_VC(int attempted)
{
    View_Change_t vc = {};
    vc.type = VIEW_CHANGE;
    vc.server_id = my_server_id;
    vc.attempted = attempted;

    return vc;
}

Prepare_t Construct_Prepare(int view, int aru)
{
    Prepare_t p = {};
    p.type = PREPARE;
    p.server_id = my_server_id;
    p.view = view;
    p.local_aru = aru;

    return p;
}

Prepare_OK_t Construct_Prepare_OK(const int last_installed, const struct datalist_t& datalist)
{
    Prepare_OK_t p = {};
    p.type = PREPARE_OK;
    p.server_id = my_server_id;
    p.view = last_installed;
    p.total_proposals = datalist.total_proposals;

    LOG(TRACE, "Constructing Prepare OK");
    log(TRACE, "Datalist: proposals %d, updates %d\n", datalist.proposals, datalist.total_globally_ordered_updates);

    for(uint32_t i = 0; i < datalist.total_proposals; i++)
    {
        log(TRACE, "Updating proposal %dth element\n", i);
        prettyPrint((char*) &datalist.proposals[i]);
        //assert(datalist.proposals[i].type != 0);
        p.proposals[i] = datalist.proposals[i];
    }
    p.total_globally_ordered_updates = datalist.total_globally_ordered_updates;

    for(uint32_t i = 0; i < p.total_globally_ordered_updates; i++)
    {
        log(TRACE, "Updating global %dth element\n", i);
        p.globally_ordered_updates[i] = datalist.globally_ordered_updates[i];
    }
    return p;
}

Accept_t Construct_Accept(int my_server_id, int view, int seq)
{
    Accept_t a = {};
    a.type = ACCEPT;
    a.server_id = my_server_id;
    a.view = view;
    a.seq = seq;

    return a;
}

Globally_Ordered_Update_t Construct_Globally_Ordered_Update(int seq)
{
    Globally_Ordered_Update_t u = {};
    u.type = GLOBALLY_ORDERED_UPDATE;
    u.server_id = my_server_id;
    u.seq = seq;

    //assert(global_history[seq].has_proposal);
    u.update = global_history[seq].prop.update;
    // TODO check this

    return u;
}

Proposal_t Construct_Proposal(int my_server_id, int view, int seq, Client_Update_t u)
{
    Proposal_t p = {};
    p.type = PROPOSAL;
    p.server_id = my_server_id;
    p.view = view;
    p.seq = seq;
    p.update = u;
    return p;
}

void Handle_New_Message(int clientid, int updateno)
{
    Client_Update_t u = {};
    u.type = CLIENT_UPDATE;
    u.timestamp = get_timestamp();
    u.server_id = my_server_id;
    u.update = updateno;
    u.client_id = clientid;

    Client_Update_Handler(u);

}

timestamp currentTimestamp = 0;
// monotonically increasing timestamp.
timestamp get_timestamp()
{
    currentTimestamp += 1;
    return currentTimestamp;
}

////////////////////////////////////////////////////////////////////////////////
// PSB Implementation
////////////////////////////////////////////////////////////////////////////////

void Update_Data_Structures(char* message)
{
    switch(MSG_TYPE(message))
    {
    case VIEW_CHANGE:
        {
            auto V = (View_Change_t*) message;
            if(vc.find(V->server_id) != vc.end())
            {
                LOG(DEBUG, "Ignoring VC");
                return;
            }

            auto Vi = *V;
            vc.insert(std::make_pair(V->server_id, Vi));
        }
        break;
    case PREPARE:
        {
            auto P = (Prepare_t*) message;
            Prepare = *P;
            prepare_is_set = true;
            prepare_timer.setAlarm(DEFAULT_PREPARE_TIMER_MS);
        }
        break;
    case PROPOSAL:
        {
            auto P = (Proposal_t*) message;
            auto ghs = &global_history[P->seq];

            //D2. if Global History[seq].Globally Ordered Update is not empty
            //    D3. ignore Proposal
            if(ghs->has_update)
                return;

            if(ghs->has_proposal)
            {
                if(P->view > ghs->prop.view)
                {
                    ghs->prop = *P;
                    ghs->clear_accepts();
                }

            }
            else
            {
                ghs->prop = *P;
                ghs->has_proposal = true;
            }
        }
        break;
    case ACCEPT:
        {
            LOG(TRACE, "Handling Accept");
            auto A = (Accept_t*) message;
            auto ghs = &global_history[A->seq];

            if(ghs->has_update)
            {
                LOG(TRACE, "\talready has update, ignoring");
                return;
            }
            //if(ghs->num_accepts >= floor(num_servers / 2.0))
            if(ghs->has_enough_accepts_for_proposal())
            {
                LOG(TRACE, "\talready has enough accepts, ignoring");
                return;
            }

            if(ghs->has_accepts[A->server_id])
                return;

            ghs->accepts[A->server_id] = *A;
            ghs->has_accepts[A->server_id] = true;
            ghs->num_accepts++;
            /**
            E2. if Global History[seq].Globally Ordered Update is not empty
                E3. ignore A
            E4. if Global History[seq].Accepts already contains ⌊N/2⌋ Accept messages
                E5. ignore A
            E6. if Global History[seq].Accepts[server id] is not empty
                E7. ignore A
            E8. Global History[seq].Accepts[server id] ← A
            **/
        }
        break;
    case GLOBALLY_ORDERED_UPDATE:
        {
            //F1. Globally Ordered Update G(server id, seq, update):
            auto G = (Globally_Ordered_Update_t*) message;
            auto ghs = &global_history[G->seq];

            //F2. if Global History[seq] does not contain a Globally Ordered Update
                //F3. Global History[seq] ← G
            if(!ghs->has_update)
            {
                ghs->has_update = true;
                ghs->update = *G;
            }
        }
        break;
    case PREPARE_OK:
        {
            auto P = (Prepare_OK_t*) message;
            // C2. if Prepare OK[server id] is not empty
            //   C3. ignore P
            if(oks.find(P->server_id) != oks.end())
                return;

            // C4. Prepare OK[server id] ← P
            oks.insert(std::make_pair(P->server_id, *P));

            // C5. for each entry e in data list
            for(uint32_t i = 0; i < P->total_globally_ordered_updates; i++)
            {
            //     C6. Apply e to data structures
                Update_Data_Structures((char*) &P->globally_ordered_updates[i]);
            }
        }
        break;

    case VC_PROOF:
    case CLIENT_UPDATE:
        break;

    default:
        {
            LOG(ERROR, "This should be unreachable!");
        }
        break;
    }
}


///////////////////////////////////////////////////////////////////////////////
// Leader Election Phase
///////////////////////////////////////////////////////////////////////////////

void Leader_Election()
{
    // upon expiration of progress timer:
    Shift_To_Leader_Election(last_attempted + 1);
}


void Upon_Receiving_View_Change(View_Change_t message)
{
    // Error checking
    if(State != LEADER_ELECTION)
    {
        LOG(WARN, "Not in leader election mode, cannot accept view change.");
        return;
    }

    if(message.attempted < last_installed)
    {
        LOG(WARN, "Got old view change from prior view");
        return;
    }

    auto V = &message;

    log(DEBUG, "Got view Change attempted: %d last attempted %d progress_timer set %s\n",
        V->attempted, last_attempted, (progress_timer.alarmSet())?"true":"false");


    //    B2. if attempted > Last Attempted and Progress Timer not set
    if(V->attempted > last_attempted && !progress_timer.alarmSet())
    {
        //        B3. Shift to Leader Election(attempted)
        Shift_To_Leader_Election(V->attempted);
        //        B4. Apply V to data structures
        Update_Data_Structures((char*) &message);
    }

    //    B5. if attempted = Last Attempted
    if(V->attempted == last_attempted)
    {
        // B6. Apply V to data structures
        Update_Data_Structures((char*) &message);


        // B7. if Preinstall Ready(attempted)
        if(Preinstall_Ready(V->attempted))
        {
            // B8. Progress Timer ← Progress Timer∗2
            // B9. Set progress timer
            progress_timer.setAlarm(DEFAULT_PROGRESS_TIMER_MS * 2);

            // B10. if leader of Last Attempted
            if(Leader_Of_Last_Attempted())
            {
                // B11. Shift to Prepare Phase()
                Shift_To_Prepare_Phase();
            }
        }
    }
}

bool Leader_Of_Last_Attempted()
{
    // My server id ≡ i mod N

    return my_server_id == last_attempted % num_servers;
}

int Get_Leader()
{
    return last_installed % num_servers;
}

//C1. Upon receiving VC Proof(server id, installed) message, V:
void Upon_Receiving_VC_Proof(VC_Proof_t message)
{

    auto V = &message;
    //C2. if installed > Last Installed
    if(V->installed > last_installed)
    {
        log(INFO, "VC Proof installed %d greater than last %d\n", V->installed, last_installed);

        //C3. Last Attempted ← installed
        last_attempted = V->installed;
        //C4. if leader of Last Attempted
        if( Leader_Of_Last_Attempted() )
        {
            //C5. Shift to Prepare Phase()
            //Shift_To_Prepare_Phase();
            
            // A5. data list ← Construct DataList(Local Aru)
            auto data_list = Construct_Data_List(local_aru);
            // A6. prepare ok ← Construct Prepare OK(Last Installed, data list)
            auto prepare_ok = Construct_Prepare_OK(last_installed, data_list);
            // A7. Prepare OK[My Server id] ← prepare ok
            oks.insert(std::make_pair(my_server_id, prepare_ok));
            // A8. Clear Last Enqueued[]
            for(int i = 0; i < MAX_CLIENTS; i++)
                Last_Enqueued[i] = 0;
                
            Shift_To_Reg_Leader();
        //C6. else
        } else {

            //C7. Shift to Reg Non Leader()
            Shift_To_Reg_Non_Leader();
        }
    }
    else
    {
        log(INFO, "VC Proof installed %d not greater than last %d\n", V->installed, last_installed);
    }
}


bool Preinstall_Ready(int view)
{
    int count = 0;

    for(auto num_server : vc)
    {
        if(num_server.second.attempted == (uint32_t)view)
            count++;
    }

    return (count >= floor(num_servers / 2.0) + 1);
}

void Shift_To_Leader_Election(int view)
{
    State = LEADER_ELECTION;

    log(INFO, "Shift to leader election for view %d\n", view);
    // E2. Clear data structures: VC[], Prepare, Prepare oks, Last Enqueued[]
    vc.clear();
    Prepare = {};
    prepare_is_set = false;
    prepare_timer.stopAlarm();
    Proposal_Retransmit_Queue.clear();
    oks.clear();

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        Last_Enqueued[i] = 0;
    }

    //    E3. Last Attempted ← view
    last_attempted = view;
    //     E4. vc ← Construct VC(Last Attempted)
    View_Change_t vct = Construct_VC(last_attempted);
    //     E5. SEND to all servers: vc
    std::vector<char> viewchange;
    paxos::pack_View_Change(vct, viewchange);
    //unicast->sendMessage(viewchange);
    unicast->sendMessage(viewchange);

    progress_timer.stopAlarm(); // we're in leader election

    // E6. Apply vc to data structures
    Update_Data_Structures((char*) &vct);
}


///////////////////////////////////////////////////////////////////////////////
//
///////////////////////////////////////////////////////////////////////////////

void Shift_To_Prepare_Phase()
{
    log(INFO, "Shifting to prepare phase\n");


    // A2. Last Installed ← Last Attempted
    last_installed = last_attempted;

    std::cout << my_server_id << ": Server " << Get_Leader() << " is the new leader of view " << last_attempted << std::endl;
    LOG(WARN, "New Leader!");

    // A3. prepare ← Construct Prepare(Last Installed, Local Aru)
    auto prepare = Construct_Prepare(last_installed, local_aru);
    // A4. Apply prepare to data structures
    Update_Data_Structures((char*) &prepare);
    // A5. data list ← Construct DataList(Local Aru)
    auto data_list = Construct_Data_List(local_aru);
    // A6. prepare ok ← Construct Prepare OK(Last Installed, data list)
    auto prepare_ok = Construct_Prepare_OK(last_installed, data_list);
    // A7. Prepare OK[My Server id] ← prepare ok
    oks.insert(std::make_pair(my_server_id, prepare_ok));
    // A8. Clear Last Enqueued[]
    for(int i = 0; i < MAX_CLIENTS; i++)
        Last_Enqueued[i] = 0;
    // A9. **Sync to disk
    LOG(DEBUG, "syncing to disk");
    // A10. SEND to all servers: prepare
    std::vector<char> packed_msg;
    paxos::pack_Prepare(prepare, packed_msg);
    //unicast->sendMessage(packed_msg);
    unicast->sendMessage(packed_msg);
}

// B1. Upon receiving Prepare(server id, view, aru)
void Upon_Receiving_Prepare(Prepare_t p)
{
// B2. if State = leader election /* Install the view */
    if(State == LEADER_ELECTION)
    {
//        B3. Apply Prepare to data structures
        Update_Data_Structures((char*) &p);
//        B4. data list ← Construct DataList(aru)
        auto data_list = Construct_Data_List(p.local_aru);
//        B5. prepare ok ← Construct Prepare OK(view, data list)
        auto prepare_ok = Construct_Prepare_OK(p.view, data_list);
//        B6. Prepare OK[My server id] ← prepare ok
        oks.insert(std::make_pair(my_server_id, prepare_ok));
//        B7. Shift to Reg Non Leader()
        Shift_To_Reg_Non_Leader();
//        B8. SEND to leader: prepare ok
        std::vector<char> packed_msg;
        paxos::pack_Prepare_OK(prepare_ok, packed_msg);
        unicast->reliableSend(Get_Leader(), packed_msg);

    }
    else
    {
//    B9. else /* Already installed the view */
//        B10. SEND to leader: Prepare OK[My server id]
        std::vector<char> packed_msg;
        paxos::pack_Prepare_OK(oks[my_server_id], packed_msg);
        unicast->reliableSend(Get_Leader(), packed_msg);

    }
}


//C1. Upon receiving Prepare OK(server id, view, data list)
void Upon_Receiving_Prepare_Ok(Prepare_OK_t p)
{
//    C2. Apply to data structures
    Update_Data_Structures((char*) &p);
//    C3. if View Prepared Ready(view)
    if(View_Prepared_Ready(p.view))
    {
//        C4. Shift to Reg Leader()
        Shift_To_Reg_Leader();
    }
}



struct datalist_t Construct_Data_List(int aru)
{
    log(TRACE, "Construcing data list for aru %d\n", aru);

//        A2. datalist ← ∅
    struct datalist_t datalist;
    datalist.total_globally_ordered_updates = 0;
    datalist.total_proposals = 0;

//        A3. for each sequence number i, i > aru, where Global History[i] is not empty

    for(auto iter : global_history)
    {
        auto i = iter.first;

        if(i <= aru)
            continue;

        log(TRACE, "Sequence: %d\n", i);

        auto hist = iter.second;

//            A4. if Global History[i].Ordered contains a Globally Ordered Update, G
//                A5. datalist ← datalist ∪ G
        if(hist.has_update)
        {
            datalist.globally_ordered_updates[datalist.total_globally_ordered_updates] = hist.update;
            datalist.total_globally_ordered_updates++;
        }
//            A6. else
//                A7. datalist ← datalist ∪ Global History[i].Proposal
        else
        {
            if(hist.has_proposal)
            {
                datalist.proposals[datalist.total_proposals] = hist.prop;
                datalist.total_proposals++;
            }
        }
    }
//    A8. return datalist
    return datalist;
}

// B1. bool View Prepared Ready(int view)
bool View_Prepared_Ready(int view)
{
    //     B2. if Prepare oks[] contains ⌊N/2⌋ + 1 entries, p, with p.view = view
    int entrycount = 0;
    for(auto ent : oks)
    {
            if(ent.second.view == (uint32_t)view)
                entrycount++;
    }

    //         B3. return TRUE
    //     B4. else
    //         B5. return FALSE
    return (entrycount >= num_servers / 2.0 + 1);
}

// A1. Shift to Reg Leader()

void Shift_To_Reg_Leader()
{
    log(INFO, "Shifting to REG Leader\n");

        progress_timer.setAlarm(DEFAULT_PROGRESS_TIMER_MS); // we're done with leader election

//     A2. State ← reg leader
        State = REG_LEADER;
//     A3. Enqueue Unbound Pending Updates()
        Enqueue_Unbound_Pending_Updates();
//     A4. Remove Bound Updates From Queue()
        Remove_Bound_Updates_From_Queue();
//     A5. Last Proposed ← Local Aru
        last_proposed = local_aru;
//     A6. Send Proposal()
        Send_Proposal();
}


//
// B1. Shift to Reg Non Leader()
void Shift_To_Reg_Non_Leader()
{
    log(INFO, "Shifting to REG non Leader\n");
    progress_timer.setAlarm(DEFAULT_PROGRESS_TIMER_MS); // we're done with leader election

//     B2. State ← reg nonleader
        State = REG_NONLEADER;
//     B3. Last Installed ← Last Attempted
        last_installed = last_attempted;

        std::cout << my_server_id << ": Server " << Get_Leader() << " is the new leader of view " << last_attempted << std::endl;
        LOG(WARN, "New Leader!");
//     B4. Clear Update Queue
        update_queue.clear();
//     B5. **Sync to disk
        LOG(DEBUG, "Syncing to disk");
}

// Global Ordering Protocol:


//     A1. Upon receiving Client Update(client id, server id, timestamp, update), U:
void Upon_Receiving_Client_Update(Client_Update_t U)
{
//         A2. Client Update Handler(U)
    Client_Update_Handler(U);
}

//
// B1. Upon receiving Proposal(server id, view, seq, update):
void Upon_Receiving_Proposal(Proposal_t p)
{
//     B2. Apply Proposal to data structures
        Update_Data_Structures((char*) &p);
//     B3. accept ← Construct Accept(My server id, view, seq)
        auto accept = Construct_Accept(my_server_id, p.view, p.seq);
//     B4. **Sync to disk
        LOG(DEBUG, "Syncing to disk");
//     B5. SEND to all servers: accept
        std::vector<char> packed_msg;
        paxos::pack_Accept(accept, packed_msg);
        //unicast->sendMessage(packed_msg);
        unicast->sendMessage(packed_msg);
}



// C1. Upon receiving Accept(server id, view, seq):
void Upon_Receiving_Accept(Accept_t a)
{
    //assert(a.type == ACCEPT);

//     C2. Apply Accept to data structures
        Update_Data_Structures((char*) &a);
//     C3. if Globally Ordered Ready(seq)
        if(Globally_Ordered_Ready(a.seq))
        {

//         C4. globally ordered update ← Construct Globally Ordered Update(seq)
            auto globally_ordered_update = Construct_Globally_Ordered_Update(a.seq);

            Upon_Executing_A_Client_Update(globally_ordered_update.update); // assume this goes here.

//         C5. Apply globally ordered update to data structures
            Update_Data_Structures((char*) &globally_ordered_update);
//         C6. Advance Aru()
            Advance_Aru();

            // stop sending proposals
            for(uint32_t i = 0; i < Proposal_Retransmit_Queue.size(); i++)
            {
                auto prop = Proposal_Retransmit_Queue[i];

                if(prop.seq == a.seq)
                {
                    Proposal_Retransmit_Queue.erase(Proposal_Retransmit_Queue.begin() + i);
                    i--;
                }
            }

        }
}

//
// D1. Upon executing a Client Update(client id, server id, timestamp, update), U:
void Upon_Executing_A_Client_Update(Client_Update_t U)
{
        if(U.timestamp <= Last_Executed[U.client_id])
            return;
        
//     D2. Advance Aru()
        //Advance_Aru();
        
      
        std::cout << my_server_id
                    << ": Executed update " << U.update
                    << " from client " << U.client_id
                    << " with seq " << U.timestamp
                    << " and view " << last_installed << std::endl;
                

//     D3. if server id = My server id
        if(U.server_id == my_server_id)
        {
//         D4. Reply to client
            reply_to_client(U);

           
//         D5. if U is in Pending Updates[client id]
            if( Pending_Updates_Set[U.client_id] &&
                client_update_equal(Pending_Updates[U.client_id],U))
            {
//             D6. Cancel Update Timer(client id)
                update_timer[U.client_id].stopAlarm();
//             D7. Remove U from Pending Updates[]
                Pending_Updates_Set[U.client_id] = false;
            }
        }

//     D8. Last Executed[client id] ← timestamp
        Last_Executed[U.client_id] = U.timestamp;
//     D9. if State != leader election
        if(State != LEADER_ELECTION)
        {
//         D10. Restart Progress Timer
            progress_timer.setAlarm(DEFAULT_PROGRESS_TIMER_MS);
        }
//     D11. if State = reg leader
        if(State == REG_LEADER)
        {
//         D12. Send Proposal()
            Send_Proposal();
        }
}




// A1. Send Proposal()
void Send_Proposal()
{
//     A2. seq ← Last Proposed + 1
        auto seq = last_proposed + 1;
//     A3. if Global History[seq].Globally Ordered Update is not empty
        if(global_history.find(seq) != global_history.end())
        {
//         A4. Last Proposed++
            last_proposed++;
//         A5. Send Proposal()
            Send_Proposal();
            return;
        }

        Client_Update_t u;

//     A6. if Global History[seq].Proposal contains a Proposal P
        if(global_history[seq].has_proposal)
        {
            auto P = global_history[seq].prop;
            //         A7. u ← P.update
            u = P.update;
        }
//     A8. else if Update Queue is empty
        else if(update_queue.size() == 0)
        {
            //         A9. return
            return;
        }
//     A10. else
        else
        {
//         A11. u ← Update Queue.pop()
            u =  update_queue.front();
            update_queue.pop_front();
        }

//     A12. proposal ← Construct Proposal(My server id, view, seq, u)
        auto proposal = Construct_Proposal(my_server_id, last_installed, seq, u);
//     A13. Apply proposal to data structures
        Update_Data_Structures((char*) &proposal);

        Proposal_Retransmit_Queue.push_back(proposal);

//     A14. Last Proposed ← seq
        last_proposed = seq;
//     A15. **Sync to disk
        LOG(DEBUG, "Syncing to disk");
//     A16. SEND to all servers: proposal
        std::vector<char> packed_msg;
        paxos::pack_Proposal(proposal, packed_msg);
        //unicast->sendMessage(packed_msg);
        unicast->sendMessage(packed_msg);
}


bool Globally_Ordered_Ready(int seq)
{
    LOG(TRACE, "Globally_Ordered_Ready");
// B1. bool Globally Ordered Ready(int seq)
    auto gh = global_history[seq];

    if(!gh.has_proposal)
    {
        LOG(TRACE, "\tNo, no proposal.");
        return false;
    }
//     B2. if Global History[seq] contains a Proposal and ⌊N/2⌋ Accepts from the same view

    if(gh.has_enough_accepts_for_proposal())
    {
        return true;
    }

    return false;
//         B3. return TRUE
//     B4. else
//         B5. return FALSE
}


// C1. Advance Aru()
void Advance_Aru()
{
//     C2. i ← Local Aru +1
        int i = local_aru + 1;
//     C3. while (1)
        while( true )
        {
//         C4. if Global History[i].Ordered is not empty
            if(global_history[i].has_update)
            {
                local_aru++;
                i++;
            } else {
                return;
            }
        }
}

// Client Update Handler(Client Update U):
void Client_Update_Handler(Client_Update_t U)
{
    if(U.type != CLIENT_UPDATE)
    {
        LOG(ERROR, "This is not a client update message");
        prettyPrint((char*) &U);
        return;
    }

    LOG(INFO, "Client Update Handler");
//     A1. if(State = leader election)
    if(State == LEADER_ELECTION)
    {
        LOG(INFO, "in leader election");

//     A2. if(U.server id != My server id)
        if(U.server_id != my_server_id)
        {
            LOG(INFO, "ignoring");
//         A3. return
            return;
        }
        // A4. if(Enqueue Update(U))
        if(Enqueue_Update(U))
        {
            LOG(INFO, "enqueueing update");
    //     A5. Add to Pending Updates(U)
            Add_To_Pending_Updates(U);
        }

    }

// A6. if(State = reg nonleader)
    if(State == REG_NONLEADER)
    {
        LOG(INFO, "not leader");

//     A7. if(U.server id = My server id)
        if(U.server_id == my_server_id)
        {
            LOG(INFO, "adding to pending updates");

//         A8. Add to Pending Updates(U)
            Add_To_Pending_Updates(U);
//         A9. SEND to leader: U
            std::vector<char> packed_msg;
            paxos::pack_Client_Update(U, packed_msg);
            unicast->reliableSend(Get_Leader(), packed_msg);
        }
    }
// A10. if(State = reg leader)
    if(State == REG_LEADER)
    {
        LOG(INFO, "am the leader");

//     A11. if(Enqueue Update(U))
        if(Enqueue_Update(U))
        {
//         A12. if U.server id = My server id
            if(U.server_id == my_server_id)
            {
//             A13. Add to Pending Updates(U)
                Add_To_Pending_Updates(U);
            }
            LOG(INFO, "sending proposal");
//         A14. Send Proposal()
            Send_Proposal();
        }
    }
}

//
//
// B1. Upon expiration of Update Timer(client id):
void Upon_Expiration_Of_Update_Timer(uint32_t client_id)
{

//     B2. Restart Update Timer(client id)
        update_timer[client_id].setAlarm(DEFAULT_UPDATE_TIMER_MS);
//     B3. if(State = reg nonleader)
        if(State == REG_NONLEADER)
        {
//         B4. SEND to leader: Pending Updates[client id]
            std::vector<char> packed_msg;
            paxos::pack_Client_Update(Pending_Updates[client_id], packed_msg);
            unicast->reliableSend(Get_Leader(), packed_msg);
        }
}

// A1. bool Enqueue Update(Client Update U)
bool Enqueue_Update(Client_Update_t U)
{
    //assert(U.type == CLIENT_UPDATE);

    LOG(TRACE, "Enqueue_Update");
    prettyPrint((char*) &U);
//     A2. if(U.timestamp ≤ Last Executed[U.client id])
    if(U.timestamp <= Last_Executed[U.client_id])
    {
        log(TRACE, "update timestamp <= last executed (%d)\n", Last_Executed[U.client_id]);
        //         A3. return false
        return false;
    }
//     A4. if(U.timestamp ≤ Last Enqueued[U.client id])
    if(U.timestamp <= Last_Enqueued[U.client_id])
        {
        //         A5. return false
        log(TRACE, "update timestamp <= last enqueued (%d)\n", Last_Enqueued[U.client_id]);

            return false;
        }
//     A6. Add U to Update Queue
    update_queue.push_back(U);
//     A7. Last Enqueued[U.client id] ← U.timestamp
    Last_Enqueued[U.client_id] = U.timestamp;
//     A8. return true
    return true;
}


// B1. Add to Pending Updates(Client Update U)
void Add_To_Pending_Updates(Client_Update_t U)
{
//     B2. Pending Updates[U.client id] ← U
    Pending_Updates[U.client_id] = U;
    Pending_Updates_Set[U.client_id] = true;
//     B3. Set Update Timer(U.client id)
    update_timer[U.client_id].setAlarm(DEFAULT_UPDATE_TIMER_MS);
//     B4. **Sync to disk
    LOG(DEBUG, "Syncing To Disk");
}

// C1. Enqueue Unbound Pending Updates()
void Enqueue_Unbound_Pending_Updates()
{
//     C2. For each Client Update U in Pending Updates[]
    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(!Pending_Updates_Set[i])
            continue;

        auto U = Pending_Updates[i];

//         C3. if U is not bound and U is not in Update Queue
        if(! isBound(U) && !inProposalQueue(U))
        {
//             C4. Enqueue Update(U)
            Enqueue_Update(U);
        }
    }
}


// D1. Remove Bound Updates From Queue()
void Remove_Bound_Updates_From_Queue()
{
//     D2. For each Client Update U in Update Queue
    std::deque<Client_Update_t> afterQueue;

    for(auto U : update_queue)
    {
//         D3. if U is bound or U.timestamp ≤ Last Executed[U.client id] or
//             (U.timestamp ≤ Last Enqueued[U.client id] and U.server id != My server id)

        if( isBound(U) ||
            U.timestamp <= Last_Executed[U.client_id] ||
            (U.timestamp <= Last_Enqueued[U.client_id] && U.server_id != my_server_id))
        {
//               D4. Remove U from Update Queue
            //update_queue.remove(U);
            // instead of doing this we just don't push it to the after queue.
//              D5. if U.timestamp > Last Enqueued[U.client id]
            if(U.timestamp > Last_Enqueued[U.client_id])
            {
//                 D6. Last Enqueued[U.client id] ← U.timestamp
                Last_Enqueued[U.client_id] = U.timestamp;
            }
        }
        else
        {
            afterQueue.push_back(U);
        }
    }

    update_queue = afterQueue;
}

bool Conflict(const char* message)
{

    switch(MSG_TYPE(message))
    {
        case VIEW_CHANGE:
            {
            /**
            A1. View Change VC(server id, attempted):
            A2. if server id = My server id
                A3. return TRUE
            A4. if State != leader election
                A5. return TRUE
            A6. if Progress Timer is set
                A7. return TRUE
            A8. if attempted ≤ Last Installed
                A9. return TRUE
            A10. return FALSE
            **/

            View_Change_t* VC = (View_Change_t*) message;
            if(//VC->server_id == my_server_id ||
                State != LEADER_ELECTION ||
                //progress_timer.alarmSet() ||
                VC->attempted <= last_installed)
                return true;

            return false;
            }
            break;
        case VC_PROOF:
            {
            VC_Proof_t* V = (VC_Proof_t*) message;
            if( V->server_id == my_server_id ||
                State  != LEADER_ELECTION)
                return true;

            return false;
            }
            break;
        case PREPARE:
            {
                auto p = (Prepare_t*) message;
                if(p->server_id == my_server_id)
                    return true;
                if(p->view != last_attempted)
                    return true;
                return false;
            }
            break;

        case PREPARE_OK:
            {
                auto pok = (Prepare_OK_t*) message;

                if(State != LEADER_ELECTION)
                    return true;
                if(pok->view != last_attempted)
                    return true;

                return false;
            }
            break;

        case PROPOSAL:
            {
                Proposal_t* prop = (Proposal_t*) message;
                if(prop->server_id == my_server_id)
                    return true;
                if(State != REG_NONLEADER)
                    return true;
                if(prop->view != last_installed)
                    return true;
                return false;
            }
            break;

        case ACCEPT:
            {
                auto accept = (Accept_t*) message;

                if(accept->server_id == my_server_id)
                    return true;

                if(accept->view != last_installed)
                    return true;

                /**
                F6. if Global History[seq] does not contain a Proposal from view
                    F7. return TRUE
                **/
                if(!global_history[accept->seq].has_proposal ||
                    global_history[accept->seq].prop.view != accept->view)
                    return true;
                return false;

            }
            break;
        case CLIENT_UPDATE:
            return false;

        case 0:
            return false;

        default:
            LOG(ERROR, "We should not reach this!");
    }

    return false;
}



void Recovery()
{
    /** init b/c we don't have stable storage **/
    last_attempted = 0;
    last_installed = 0;
    local_aru = 0;
    last_proposed = 0;
    progress_timer.setAlarm(DEFAULT_PROGRESS_TIMER_MS);

    Shift_To_Leader_Election(last_attempted + 1);
    proof_timer.setAlarm(DEFAULT_VC_PROOF_TIMER_MS);

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        Last_Executed[i] = 0;
        Last_Enqueued[i] = 0;
        Pending_Updates_Set[i] = false;
    }

    proposal_timer.setAlarm(DEFAULT_PROPOSAL_TIMER_MS);
}

void Check_Timers()
{
    log(TRACE, "Paxos, state: %s (%d) last_ins: %d last_att: %d\n", STATENAMES[State], State, last_installed, last_attempted);

    if(proof_timer.alarmIsRinging())
    {
        proof_timer.setAlarm(DEFAULT_VC_PROOF_TIMER_MS);

        if(State != LEADER_ELECTION)
        {
            VC_Proof_t vcp = {};
            vcp.type = VC_PROOF;
            vcp.server_id = my_server_id;
            vcp.installed = last_installed;

            std::vector<char> packed_msg;
            paxos::pack_VC_Proof(vcp, packed_msg);
            //unicast->sendMessage(packed_msg);
            unicast->sendMessage(packed_msg);
        }
    }

    if(progress_timer.alarmSet() && progress_timer.alarmIsRinging())
    {
        progress_timer.stopAlarm();
        log(DEBUG, "expiration of progress timer\n");
        Shift_To_Leader_Election(last_attempted + 1);
    }

    for(int i = 0; i < MAX_CLIENTS; i++)
    {
        if(update_timer[i].alarmSet() && update_timer[i].alarmIsRinging())
        {
            log(WARN, "expiration of update timer for client %d\n", i);
            Upon_Expiration_Of_Update_Timer(i);
        } 
    }

    if(prepare_timer.alarmSet() && prepare_timer.alarmIsRinging())
    {
        prepare_timer.setAlarm(DEFAULT_PREPARE_TIMER_MS);

        std::vector<char> packed_msg;
        paxos::pack_Prepare(Prepare, packed_msg);
        //unicast->sendMessage(packed_msg);
        unicast->sendMessage(packed_msg);
    }

    if(proposal_timer.alarmSet() && proposal_timer.alarmIsRinging())
    {
        proposal_timer.setAlarm(DEFAULT_PROPOSAL_TIMER_MS);

        for(auto proposal : Proposal_Retransmit_Queue)
        {
            log(DEBUG, "retransmitting proposals\n");
            std::vector<char> packed;
            paxos::pack_Proposal(proposal, packed);
            unicast->sendMessage(packed);
        }
    }
}



// Definitions from Paxos that we need:

void paxos::handle_Client_Update(Client_Update_t var) // User supplied
{
    if(Conflict((char*) &var))
    {
        LOG(INFO, "bad client update");
        return;
    }
    LOG(INFO, "Got Client Update");
    Upon_Receiving_Client_Update(var);
}
void paxos::handle_View_Change(View_Change_t var)
{
    if(Conflict((char*) &var))
    {
        LOG(INFO, "bad view change");
        return;
    }
    
    LOG(TRACE, "Got View Change");
    Upon_Receiving_View_Change(var);
} // User supplied
void paxos::handle_VC_Proof(VC_Proof_t var)
{    
    if(Conflict((char*) &var))
    {
        LOG(INFO, "bad vc proof");
        return;
    }
    
    LOG(TRACE, "Got VC Proof");
    Upon_Receiving_VC_Proof(var);
} // User supplied
void paxos::handle_Prepare(Prepare_t var)
{
    if(Conflict((char*) &var))
    {
        LOG(INFO, "bad prepare");
        return;
    }
    
    LOG(TRACE, "Got Prepare");
    Upon_Receiving_Prepare(var);
} // User supplied
void paxos::handle_Proposal(Proposal_t var)
{
    if(Conflict((char*) &var))
    {
        LOG(INFO, "bad proposal");
        return;
    }
    
    LOG(TRACE, "Got Proposal");
    Upon_Receiving_Proposal(var);
} // User supplied
void paxos::handle_Accept(Accept_t var)
{
    if(Conflict((char*) &var))
    {
        LOG(INFO, "bad update");
        return;
    }

    LOG(TRACE, "Got Accept");
    Upon_Receiving_Accept(var);
} // User supplied
void paxos::handle_Globally_Ordered_Update(Globally_Ordered_Update_t var)
{
    LOG(ERROR, "Should never be called!");
} // User supplied
void paxos::handle_Prepare_OK(Prepare_OK_t var)
{
    LOG(TRACE, "Got Prepare Ok");
    Upon_Receiving_Prepare_Ok(var);
} // User supplied
void paxos::handle_invalid_message(const char* message)
{
    log(ERROR, "Could not parse message: '%s'\n", message);
} // usesupplied, when the parser encounters an error

int lastSender = 0;
void paxos::handle_UnivAck(paxos::UnivAck_t var)
{
    unicast->handleAck(&var, lastSender);
}


// Client interaction things.
void initPaxos(Unicast& caster)
{
    unicast = &caster;
    my_server_id = caster.localhost();
    num_servers = caster.getNumberOfHosts();

    log(INFO, "Number of hosts: %d\n", num_servers);

    Recovery();
}

void setLastSender(int ls)
{
    lastSender = ls;
}


void parse_message(char* buffer, int bufsize)
{
    switch(MSG_TYPE(buffer))
    {
    case VIEW_CHANGE:
        {
            View_Change_t message;
            memcpy(&message, (void *) buffer, sizeof(View_Change_t));
            
            paxos::handle_View_Change(message);
        }
        break;
    case CLIENT_UPDATE:
        {
            Client_Update_t message;
            memcpy(&message, (void *) buffer, sizeof(Client_Update_t));
            
            paxos::handle_Client_Update(message);
        }
        break;
    case VC_PROOF:
        {
            VC_Proof_t message;
            memcpy(&message, (void *) buffer, sizeof(VC_Proof_t));
            
            paxos::handle_VC_Proof(message);
        }
        break;
    case PREPARE:
        {
            Prepare_t message;
            memcpy(&message, (void *) buffer, sizeof(Prepare_t));
            
            paxos::handle_Prepare(message);
        }
        break;
    case PROPOSAL:
        {
            Proposal_t message;
            memcpy(&message, (void *) buffer, sizeof(Proposal_t));
            
            paxos::handle_Proposal(message);
        }
        break;
    case ACCEPT:
        {
            Accept_t message;
            memcpy(&message, (void *) buffer, sizeof(Accept_t));
            
            paxos::handle_Accept(message);
        }
        break;
    case GLOBALLY_ORDERED_UPDATE:
        {
            // This should never come up
            LOG(ERROR, "this message globally ordered update should never cross a wire");
        }
        break;
    case PREPARE_OK:
        {
            Prepare_OK_t message;
            Prepare_OK_t *working = (Prepare_OK_t*) buffer;
            message.type = working->type;
            message.server_id = working->server_id;
            message.view = working->view;
            message.total_proposals = working->total_proposals;
            memcpy(&message.proposals[0], (void *) &working->proposals, sizeof(Proposal_t) * message.total_proposals);
            
            uint32_t* global_updates = (uint32_t*) &buffer[sizeof(uint32_t) * 4 + sizeof(Proposal_t) * message.total_proposals];
            message.total_globally_ordered_updates = global_updates[0];
            log(TRACE, "parsing prepare OK");
            log(TRACE, "PrepareOK: server: %d view: %d #prop: %d #glob: %d\n", message.server_id, message.view, message.total_proposals, message.total_globally_ordered_updates);
            
            int global_bytes = sizeof(Globally_Ordered_Update_t) * message.total_globally_ordered_updates;
            
            if(bufsize - global_bytes > 0)
                memcpy(&message.globally_ordered_updates[0], (void *) &buffer[bufsize - global_bytes], global_bytes);
            else
            {
                LOG(ERROR, "buffsize < global updates");
                
                uint32_t* msg = (uint32_t*) buffer;
                for(int i = 0; i < bufsize / sizeof(uint32_t); i++)
                    std::cerr << "\t" << msg[i] << std::endl;
            }
            
            
            
            std::vector<char> preppack;
            
            paxos::pack_Prepare_OK(message, preppack);
            
            log(WARN, "PREPARE OK correct size? %d, expecting %d got %d\n", preppack.size() == bufsize, bufsize, preppack.size());
            
            paxos::handle_Prepare_OK(message);
        }
        break;
    default:
        {
            LOG(ERROR, "This should be unreachable!");
        }
        break;
    }
}















////////////////////////////////////////////////////////////////////////////////

void prettyPrint(const char* message)
{
    switch(MSG_TYPE(message))
    {
        case VIEW_CHANGE:
            {
                auto m = (View_Change_t*) message;
                log(TRACE, "View Change: server: %d attempted: %d\n", m->server_id, m->attempted);
            }
            break;
        case VC_PROOF:
            {
                auto m = (VC_Proof_t*) message;
                log(TRACE, "VC Proof: server: %d installed: %d\n", m->server_id, m->installed);
            }
            break;
        case PREPARE:
            {
                auto m = (Prepare_t*) message;
                log(TRACE, "Prepare: server: %d view: %d localaru: %d\n", m->server_id, m->view, m->local_aru);
            }
            break;

        case PREPARE_OK:
            {
                auto m = (Prepare_OK_t*) message;
                log(TRACE, "PrepareOK: server: %d view: %d #prop: %d #glob: %d\n", m->server_id, m->view, m->total_proposals, m->total_globally_ordered_updates);
            }
            break;

        case PROPOSAL:
            {

                auto m = (Proposal_t*) message;
                log(TRACE, "Proposal: server: %d view: %d seq: %d \n",
                    m->server_id, m->view, m->seq);
            }
            break;

        case ACCEPT:
            {
                auto m = (Accept_t*) message;
                log(TRACE, "Accept: server: %d view: %d seq: %d \n",
                    m->server_id, m->view, m->seq);
            }
            break;
        case CLIENT_UPDATE:
            {
                auto m = (Client_Update_t*) message;
                log(TRACE, "Client Update: server: %d client: %d timestamp: %d update: %d \n",
                    m->server_id, m->client_id, m->timestamp, m->update);
            }
            break;
        default:
            log(ERROR, "We should not reach this! Type: %d\n", ((uint32_t*)message)[0]);
    }
}

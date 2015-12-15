#include "paxos.hpp"
#include <cstdio>

void print_mem(void const *vp, size_t n)
{
    unsigned char const *p = (unsigned char const *) vp;
    for (size_t i=0; i<n; i++)
        printf("%02x ", p[i]);
    putchar('\n');
};


void paxos::handle_Client_Update(Client_Update_t var){
	printf("Got client update!\n");
	printf("type %d, client_id %d, server_id %d, timestamp %d, update %d\n",
	        var.type, var.client_id, var.server_id, var.timestamp, var.update);
    
    std::vector<char> packed;
    paxos::pack_Client_Update(var, packed);
    
    print_mem(&packed[0], packed.size());
    
};

void paxos::handle_View_Change(View_Change_t var){
		printf("Got vc!\n");
};

void paxos::handle_VC_Proof(VC_Proof_t var){
		printf("Got vcp!\n");
};

void paxos::handle_Prepare(Prepare_t var){
		printf("Got prepare!\n");
}

void paxos::handle_Proposal(Proposal_t var){
		printf("Got proposal!\n");
}

void paxos::handle_Accept(Accept_t var){
		printf("Got accept!\n");
}

void paxos::handle_Globally_Ordered_Update(Globally_Ordered_Update_t var){
		printf("Got global ordered upate!\n");
		printf("Client: type %d, server_id %d, seq %d\n",
		    var.type, var.server_id, var.seq);
		
		Client_Update_t cut = var.update;
		printf("Client: type %d, client_id %d, server_id %d, timestamp %d, update %d\n",
        cut.type, cut.client_id, cut.server_id, cut.timestamp, cut.update);
        
        std::vector<char> packed;
        paxos::pack_Globally_Ordered_Update(var, packed);
        print_mem(&packed[0], packed.size());
}

void paxos::handle_Prepare_OK(Prepare_OK_t var){
		printf("Got client update!\n");
}

void paxos::handle_invalid_message(char* message)
{
	printf("invalid message!\n");
}

/** Output:

Got client update!
type 1, client_id 1, server_id 2, timestamp 3, update 4
01 00 00 00 01 00 00 00 02 00 00 00 03 00 00 00 04 00 00 00 
01 00 00 00 01 00 00 00 02 00 00 00 03 00 00 00 04 00 00 00 
Got global ordered upate!
Client: type 7, server_id 1, seq 2
Client: type 3, client_id 4, server_id 5, timestamp 6, update 7
07 00 00 00 01 00 00 00 02 00 00 00 03 00 00 00 04 00 00 00 05 00 00 00 06 00 00 00 07 00 00 00 
07 00 00 00 01 00 00 00 02 00 00 00 03 00 00 00 04 00 00 00 05 00 00 00 06 00 00 00 07 00 00 00 

you may want to make sure the packing works too on the buffers, never tried this before
**/
int main()
{
    uint32_t buffer[255];
    
    // for testing
    for(int i = 1; i < 255; i++)
        buffer[i] = i;
    
    buffer[0] = 1; // client udpate
	paxos::update(sizeof(uint32_t) * 5, (char*) buffer);
    print_mem(buffer, sizeof(uint32_t) * 5);
	
	paxos::clear();
	
	buffer[0] = 7;
	paxos::update(sizeof(paxos::Globally_Ordered_Update_t), (char*) buffer);
    print_mem(buffer, sizeof(paxos::Globally_Ordered_Update_t));
	return 0;
}




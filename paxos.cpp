
#ifndef paxos_PARSER_HPP
#define paxos_PARSER_HPP

#include <deque>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "paxos.h"

namespace paxos
{

// User supplied typdefs



std::deque<char> _buffer;

int _state = 0;
View_Change_t _View_Change_t_working;
prefix_t _prefix_t_working;
UnivAck_t _UnivAck_t_working;
Client_Update_t _Client_Update_t_working;
Accept_t _Accept_t_working;
VC_Proof_t _VC_Proof_t_working;
Prepare_OK_t _Prepare_OK_t_working;
Proposal_t _Proposal_t_working;
Globally_Ordered_Update_t _Globally_Ordered_Update_t_working;
Prepare_t _Prepare_t_working;

void _reset()
{
    _state = 0;
    _Client_Update_t_working = (const struct Client_Update_t){ 0 };
_View_Change_t_working = (const struct View_Change_t){ 0 };
_VC_Proof_t_working = (const struct VC_Proof_t){ 0 };
_Prepare_t_working = (const struct Prepare_t){ 0 };
_Proposal_t_working = (const struct Proposal_t){ 0 };
_Accept_t_working = (const struct Accept_t){ 0 };
_Globally_Ordered_Update_t_working = (const struct Globally_Ordered_Update_t){ 0 };
_Prepare_OK_t_working = (const struct Prepare_OK_t){ 0 };
_UnivAck_t_working = (const struct UnivAck_t){ 0 };
}

void _die(const char* message)
{
    _reset();
    handle_invalid_message(message);
}

void _push_back(int length, char* buffer)
{
    for(int i = 0; i < length; i++)
    {
        _buffer.push_back(buffer[i]);
    }
}


void _push_back_generic(int length, char* buffer, std::vector<char> &outputbuf)
{
    for(int i = 0; i < length; i++)
    {
        outputbuf.push_back(buffer[i]);
    }
}

int _push_front_amt = 0;
void _push_front(int length, char* buffer)
{
    _push_front_amt += length;
    for(int i = length - 1; i >= 0; i--)
    {
        _buffer.push_front(buffer[i]);
    }
}

bool _read_front(int length, char* outbuffer)
{
    if(_buffer.size() < (uint32_t) length)
        return false;



    for(int i = 0; i < length; i++)
    {

        outbuffer[i] = _buffer.front();
        _buffer.pop_front();
    }

    if(_push_front_amt > 0)
        _push_front_amt -= length;

    return true;
}

void _process()
{
    // do cleanup of a possible remaining useless variables.
    char a;
    for(int i = 0; i < _push_front_amt; i++)
        _read_front(1, &a);

    //printf("Current state: %d\n", _state);
    switch(_state)
    {
        case 0: // initial state
            _reset(); // reset all variables
            _state = 2;
            break;
        case 3:
{
	_Client_Update_t_working.type = _prefix_t_working.type;
	handle_Client_Update(_Client_Update_t_working);
	_reset();
	break;
}
case 4:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Client_Update_t_working.client_id))) break;
	_state = 5;
	break;
}
case 5:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Client_Update_t_working.server_id))) break;
	_state = 6;
	break;
}
case 6:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Client_Update_t_working.timestamp))) break;
	_state = 7;
	break;
}
case 7:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Client_Update_t_working.update))) break;
	_state = 3;
	break;
}
case 8:
{
	_View_Change_t_working.type = _prefix_t_working.type;
	handle_View_Change(_View_Change_t_working);
	_reset();
	break;
}
case 9:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _View_Change_t_working.server_id))) break;
	_state = 10;
	break;
}
case 10:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _View_Change_t_working.attempted))) break;
	_state = 8;
	break;
}
case 11:
{
	_VC_Proof_t_working.type = _prefix_t_working.type;
	handle_VC_Proof(_VC_Proof_t_working);
	_reset();
	break;
}
case 12:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _VC_Proof_t_working.server_id))) break;
	_state = 13;
	break;
}
case 13:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _VC_Proof_t_working.installed))) break;
	_state = 11;
	break;
}
case 14:
{
	_Prepare_t_working.type = _prefix_t_working.type;
	handle_Prepare(_Prepare_t_working);
	_reset();
	break;
}
case 15:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Prepare_t_working.server_id))) break;
	_state = 16;
	break;
}
case 16:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Prepare_t_working.view))) break;
	_state = 17;
	break;
}
case 17:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Prepare_t_working.local_aru))) break;
	_state = 14;
	break;
}
case 18:
{
	_Proposal_t_working.type = _prefix_t_working.type;
	handle_Proposal(_Proposal_t_working);
	_reset();
	break;
}
case 19:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Proposal_t_working.server_id))) break;
	_state = 20;
	break;
}
case 20:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Proposal_t_working.view))) break;
	_state = 21;
	break;
}
case 21:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Proposal_t_working.seq))) break;
	_state = 22;
	break;
}
case 22:
{
	if (!_read_front((sizeof(Client_Update_t)), ((char*) & _Proposal_t_working.update))) break;
	_state = 18;
	break;
}
case 23:
{
	_Accept_t_working.type = _prefix_t_working.type;
	handle_Accept(_Accept_t_working);
	_reset();
	break;
}
case 24:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Accept_t_working.server_id))) break;
	_state = 25;
	break;
}
case 25:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Accept_t_working.view))) break;
	_state = 26;
	break;
}
case 26:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Accept_t_working.seq))) break;
	_state = 23;
	break;
}
case 27:
{
	_Globally_Ordered_Update_t_working.type = _prefix_t_working.type;
	handle_Globally_Ordered_Update(_Globally_Ordered_Update_t_working);
	_reset();
	break;
}
case 28:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Globally_Ordered_Update_t_working.server_id))) break;
	_state = 29;
	break;
}
case 29:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Globally_Ordered_Update_t_working.seq))) break;
	_state = 30;
	break;
}
case 30:
{
	if (!_read_front((sizeof(Client_Update_t)), ((char*) & _Globally_Ordered_Update_t_working.update))) break;
	_state = 27;
	break;
}
case 31:
{
	_Prepare_OK_t_working.type = _prefix_t_working.type;
	handle_Prepare_OK(_Prepare_OK_t_working);
	_reset();
	break;
}
case 32:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Prepare_OK_t_working.server_id))) break;
	_state = 33;
	break;
}
case 33:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Prepare_OK_t_working.view))) break;
	_state = 34;
	break;
}
case 34:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Prepare_OK_t_working.total_proposals))) break;
	_state = 35;
	break;
}
case 35:
{
	if (!_read_front(_Prepare_OK_t_working.total_proposals, ((char*) & _Prepare_OK_t_working.proposals))) break;
	_state = 36;
	break;
}
case 36:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _Prepare_OK_t_working.total_globally_ordered_updates))) break;
	_state = 37;
	break;
}
case 37:
{
	if (!_read_front(_Prepare_OK_t_working.total_globally_ordered_updates, ((char*) & _Prepare_OK_t_working.globally_ordered_updates))) break;
	_state = 31;
	break;
}
case 38:
{
	_UnivAck_t_working.type = _prefix_t_working.type;
	handle_UnivAck(_UnivAck_t_working);
	_reset();
	break;
}
case 39:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _UnivAck_t_working.size))) break;
	_state = 40;
	break;
}
case 40:
{
	if (!_read_front(_UnivAck_t_working.size, ((char*) & _UnivAck_t_working.packet))) break;
	_state = 38;
	break;
}
case 1:
{
	_state = 4;
	if(_prefix_t_working.type == 1)
		_state = 4;
	if(_prefix_t_working.type == 2)
		_state = 9;
	if(_prefix_t_working.type == 3)
		_state = 12;
	if(_prefix_t_working.type == 4)
		_state = 15;
	if(_prefix_t_working.type == 5)
		_state = 19;
	if(_prefix_t_working.type == 6)
		_state = 24;
	if(_prefix_t_working.type == 7)
		_state = 28;
	if(_prefix_t_working.type == 8)
		_state = 32;
	if(_prefix_t_working.type == 1024)
		_state = 39;
	break;
}
case 2:
{
	if (!_read_front((sizeof(uint32_t)), ((char*) & _prefix_t_working.type))) break;
	_state = 1;
	break;
}


        default:
            _die("could not parse the given message, invalid state reached");
            _state = 0; // reset to initial state
    }
}

void update(int length, char* buffer)
{
    _push_back(length, buffer);

    // Keep processing until done.
    int laststate = _state;
    do
    {
        laststate = _state;
        _process();
    } while(laststate != _state);
}

void clear()
{
    _push_front_amt = 0;
    _buffer.clear();
    _reset();
}

void pack_Client_Update(Client_Update_t input, std::vector<char> &message)
{
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.type), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.client_id), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.server_id), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.timestamp), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.update), message);
}

void pack_View_Change(View_Change_t input, std::vector<char> &message)
{
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.type), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.server_id), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.attempted), message);
}

void pack_VC_Proof(VC_Proof_t input, std::vector<char> &message)
{
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.type), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.server_id), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.installed), message);
}

void pack_Prepare(Prepare_t input, std::vector<char> &message)
{
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.type), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.server_id), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.view), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.local_aru), message);
}

void pack_Proposal(Proposal_t input, std::vector<char> &message)
{
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.type), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.server_id), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.view), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.seq), message);
	_push_back_generic((sizeof(Client_Update_t)), ((char*) & input.update), message);
}

void pack_Accept(Accept_t input, std::vector<char> &message)
{
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.type), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.server_id), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.view), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.seq), message);
}

void pack_Globally_Ordered_Update(Globally_Ordered_Update_t input, std::vector<char> &message)
{
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.type), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.server_id), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.seq), message);
	_push_back_generic((sizeof(Client_Update_t)), ((char*) & input.update), message);
}

void pack_Prepare_OK(Prepare_OK_t input, std::vector<char> &message)
{

	_push_back_generic((sizeof(uint32_t)), ((char*) & input.type), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.server_id), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.view), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.total_proposals), message);
	for(int i = 0; i < input.total_proposals; i++)
	{
	    pack_Proposal(input.proposals[i], message);
	}
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.total_globally_ordered_updates), message);
	for(int i = 0; i < input.total_globally_ordered_updates; i++)
	{
	    pack_Globally_Ordered_Update(input.globally_ordered_updates[i], message);
	}
}

void pack_UnivAck(UnivAck_t input, std::vector<char> &message)
{
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.type), message);
	_push_back_generic((sizeof(uint32_t)), ((char*) & input.size), message);
	_push_back_generic((input.size * sizeof(char) ), ((char*) & input.packet), message);
}


} // end namespace
#endif //paxos_PARSER_HPP

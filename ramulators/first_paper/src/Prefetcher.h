#ifndef __PREFETCHER_H
#define __PREFETCHER_H

#include "Coding.h"
#include "Request.h"
#include <queue>
#include <list>
#include <vector>
#include <algorithm>
#include <cstdlib>

namespace coding
{

template <typename T>
class Prefetcher{
private:
	unsigned window_length = 0;
	unsigned data_length = 20;
	list<Request> recent_requests;
	list<long> downloaded_codes;

public:
    Prefetcher(){}

    Prefetcher(unsigned window_length, unsigned data_length):
		window_length(window_length),
		data_length(data_length)
    {}

	void get_request(const Request& req)
	{
		if(window_length == 0)
			return;
		if(recent_requests.size() >= window_length)
			recent_requests.pop_back();
		recent_requests.push_front(req);
	}

	// FIXME: The prefetcher actually more powerful than it should be - It assumes it can get an unencoded piece of data from the parity
	bool get_fetch(ParityBank<T>& parity)
	{
		// Grab requests for this bank
		vector<long> bank_requests;
		for(auto req : recent_requests)
			if(parity.contains(req))
				bank_requests.push_back(req.addr);
		std::sort(bank_requests.begin(), bank_requests.end());
		
		// Grab candidate addresses
		long prev_addr = 0;
		bool chain = false;
		vector<long> candidate_addresses;
		for(auto addr : bank_requests)
		{
			if(addr - prev_addr == 8) // Chain detected
				chain = true;
			else
			{
				if(chain)
					candidate_addresses.push_back(prev_addr + 8);
				chain = false;
			}
			prev_addr = addr;
		}
		
		// Exit if no chains found
		if(candidate_addresses.size() == 0)
			return false;

		// Choose from the candidate addresses at random
		if(downloaded_codes.size() > data_length)
			downloaded_codes.pop_back();
		downloaded_codes.push_front(candidate_addresses[std::rand() % candidate_addresses.size()]);
		parity.lock();
		return true;
	}

	bool find_code(long addr)
	{
		for(long code : downloaded_codes)
			if(addr == code)
				return true;
		return false;
	}

};

}

#endif

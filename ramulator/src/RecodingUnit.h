#ifndef __RECODINGUNIT_H
#define __RECODINGUNIT_H

#include "Coding.h"
#include <set>
#include <queue>

namespace coding
{

// Note that "Uncoded" isn't a value stored in the map, it merely indicates that a row isn't present in the bank.
enum CodeStatus {Updated, FreshData, FreshParity, Uncoded, MAX}; 


template <typename T>
class RecodeRequest
{
protected:
const T* spec;
public:

std::set<unsigned int> banks_needed; // data banks accesses needed to complete the recode
unsigned long row;              // The row of the request which triggered the recode
unsigned int bank;              // The bank of the request which triggered the recode
Request req;                    // The request which triggered the recode
unsigned int ticks_till_clear;  // Starts decreasing once all data for recoding has been downloaded. 
CodeStatus req_type;

	RecodeRequest(Request& req, ParityBankTopology<T>& topology, const T* spec, CodeStatus recode_type) : 
		spec(spec),
		row(req.addr_vec[static_cast<int>(T::Level::Row)]),
		bank(req.addr_vec[static_cast<int>(T::Level::Bank)]),
		req(req),
		ticks_till_clear(spec->read_latency),
		req_type(recode_type)
	{
		if(recode_type == CodeStatus::FreshData)
		{
			// Find all banks which have been xor'd with the bank receiving new data. Data from these matching banks must
			// Be downloaded before the recoder unit can rebuild the codes
			for(auto xor_regions : topology.xor_regions_for_parity_bank)
				for(auto xor_region : xor_regions)
					if(xor_region.contains(row))
						for(auto memory_region : xor_region.regions)
						{
							if(memory_region.contains(row))
								continue;
							unsigned int bank = row_index_to_addr_vec(spec, memory_region.start_row_index, 0)[static_cast<int>(T::Level::Bank)];
							banks_needed.insert(bank);
						}
		}else
		{
			//TODO After a FreshParity Recode is completed, A FreshData recode must be enacted to restore the parity.
			banks_needed.insert(bank); // FreshPartiy, only needs the data bank to complete.
			ticks_till_clear = 1;      // No write latency
		}
	}

	bool receive_row(unsigned long row)
	{
		if(this->req_type == CodeStatus::FreshParity)
			return false;
		unsigned int bank = row_index_to_addr_vec(spec, row, 0)[static_cast<int>(T::Level::Bank)];
		unsigned int bank_row = row_index_to_addr_vec(spec, row, 0)[static_cast<int>(T::Level::Row)];
		if(this->row != bank_row)
			return false;
		if(banks_needed.find(bank) != banks_needed.end())
		{
			banks_needed.erase(bank);
			return true;
		}
		return false;
	}

	// Only call this when a full bank is reserved for recoding.
	bool receive_bank(unsigned int bank)
	{
		if(banks_needed.find(bank) != banks_needed.end())
		{
			banks_needed.erase(bank);
			return true;
		}
		return false;
	}

	bool dec_tick() 
	{
		if(banks_needed.size() > 0)
			return false;
		ticks_till_clear--;
		return ticks_till_clear == 0;
	}

	bool operator ==(const RecodeRequest& req) { return this->row == req.row && this->bank == req.bank;}

};

template <typename T>
class RecodingUnit {
public:
    deque<RecodeRequest<T>> update_queue;

private:
    const T *spec;
	unsigned int num_banks;
	vector<CodeStatus> new_entry;
	// TODO add bank dimension to the code status map
    std::map<int,  /* row index */ vector<CodeStatus>> map; // Code status map
	
public:
    RecodingUnit(const T *spec) :
        spec(spec),
		num_banks(spec->org_entry.count[static_cast<int>(T::Level::Bank)])
    {
		for(unsigned int bank = 0; bank < num_banks; bank++)
			new_entry.push_back(CodeStatus::Updated);
	}

    ~RecodingUnit() {}

	// TODO shouldn't need to pass all topologies, really all the information that is needed is the structure of the 
	// parity banks.
    void set(Request& req, const CodeStatus& status, unsigned long serve_time, const vector<ParityBankTopology<T>>& topologies )
    {
		unsigned int row = req.addr_vec[static_cast<int>(T::Level::Row)];
		unsigned int absolute_row = request_to_row_index(spec, req);
		unsigned int bank = req.addr_vec[static_cast<int>(T::Level::Bank)];

		// Row unencoded, nothing to do
        if(map.find(row) == map.end())
			return;
		map.at(row)[bank] = status;

		// Construct recode request
		for(auto topology : topologies)
		{
			if(topology.contains(absolute_row))
			{
				RecodeRequest<T> recode_req = RecodeRequest<T>(req, topology, spec, status);
				for(auto old_req = update_queue.begin();
					old_req != update_queue.end();
					old_req++)
					if(recode_req == *old_req)
					{
						update_queue.erase(old_req);
						break;
					}
				update_queue.push_back(recode_req);
				return;
			}
		}
    }

    void clear(const unsigned long& row_index, unsigned int bank)
    {
        map[row_index][bank] = CodeStatus::Updated;
    }

    CodeStatus get(const unsigned long& row_index, const unsigned int bank) const
    {
        if(map.find(row_index) != map.end())
            return map.at(row_index)[bank];
        else
            return CodeStatus::Uncoded;
    }

    inline CodeStatus get(const Request& req) const
    {
        return get(request_to_row_index(spec, req), req.addr_vec[static_cast<int>(T::Level::Bank)]);
    }

    void init(std::set<ParityBankTopology<T>>& new_regions)
    {
		// Update map with encoded rows, update queues.
		for(auto new_region : new_regions)
			emplace_region(new_region);
        return;
    }

	void evict_region(ParityBankTopology<T>& evict_region)
	{
		for(auto row_region : evict_region.row_regions)
			for(int row_in_bank = 0; row_in_bank < row_region.second; row_in_bank++)
				map.erase(row_region.first + row_in_bank);
	}

	void emplace_region(ParityBankTopology<T>& emplace_region)
	{
		for(auto row_region : emplace_region.row_regions)
			for(int row_in_bank = 0; row_in_bank < row_region.second; row_in_bank++)
					map.emplace(row_region.first + row_in_bank, new_entry);
	}

	// ASSUMPTION: the recoder stores the data for the parity bank until the parity bank is free to store it. 
	//			   This functionality is not present in the ramulator
	void tick(vector<DataBank>& data_banks, vector<ParityBank<T>>& parity_banks)
	{
		for(auto req = update_queue.begin(); req != update_queue.end();)
		{
			if(req->banks_needed.size() == 0)
			{
				if(req->dec_tick())
				{
					clear(req->row, req->bank);
					req = update_queue.erase(req);
				}
			}else
			{
				req++;
			}
		}
	}

	void receive_row(unsigned long row)
	{
		for(auto req = update_queue.begin(); req != update_queue.end(); req++)
			req->receive_row(row);
	}
};
}

#endif

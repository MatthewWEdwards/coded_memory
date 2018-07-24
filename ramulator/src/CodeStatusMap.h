#ifndef __CODESTATUSMAP_H
#define __CODESTATUSMAP_H

#include "Coding.h"
#include <set>
#include <queue>

namespace coding
{

template <typename T>
class RecodeRequest
{
protected:
const T* spec;
public:

set<unsigned int> banks_needed; // data banks accesses needed
unsigned long row;

	RecodeRequest(unsigned long row, ParityBankTopology<T>& topology, const T* spec) :
		spec(spec),
		row(row)
	{
		for(auto xor_regions : topology.xor_regions_for_parity_bank)
		{
			for(auto xor_region : xor_regions)
			{
				if(xor_region.contains(row))
				{
					for(auto memory_region : xor_region.regions)
					{
						if(memory_region.contains(row))
							continue;
						unsigned int bank = row_index_to_addr_vec(spec, row, 0)[static_cast<int>(T::Level::Bank)];
						banks_needed.insert(bank);
					}
				}
			}
		}
	}

	bool receive_row(unsigned long row)
	{
		unsigned int bank = row_index_to_addr_vec(spec, row, 0)[static_cast<int>(T::Level::Bank)];
		if(banks_needed.find(bank) != banks_needed.end())
		{
			banks_needed.erase(bank);
			return true;
		}
		return false;
	}

	bool receive_bank(unsigned int bank)
	{
		if(banks_needed.find(bank) != banks_needed.end())
		{
			banks_needed.erase(bank);
			return true;
		}
		return false;
	}

	bool operator ==(const RecodeRequest& req) { return this->row == req.row; }

};

template <typename T>
class CodeStatusMap {
public:
    enum Status { Updated, FreshData, FreshParity, MAX};
    deque<RecodeRequest<T>> update_queue;

private:
    const T *spec;
    const int n_rows;
    const int update_interval = 1;
    std::map<int,  /* row index */ Status> map;
	int cur_topology_idx;
	
public:
    CodeStatusMap(const T *spec) :
        spec(spec),
        n_rows(spec->org_entry.count[static_cast<int>(T::Level::Rank)]*
               spec->org_entry.count[static_cast<int>(T::Level::Bank)]*
               spec->org_entry.count[static_cast<int>(T::Level::Row)])
    {}

    ~CodeStatusMap() {}

    void set(const unsigned long& row_index, const Status& status, unsigned long serve_time, const vector<ParityBankTopology<T>> topologies )
    {
        assert(row_index >= 0 && row_index < n_rows);
        assert(status != Status::Updated);
        map[row_index] = status;

		// Construct recode request
		for(auto topology : topologies)
		{
			if(topology.contains(row_index))
			{
				auto addr = row_index_to_addr_vec(spec, row_index, 0);
				RecodeRequest<T> req = RecodeRequest<T>(row_index, topology, spec);
				for(auto old_req = update_queue.begin();
					old_req != update_queue.end();
					old_req++)
					if(req == *old_req)
					{
						update_queue.erase(old_req);
						break;
					}
				update_queue.push_back(req);
				return;
			}
		}
    }

    void clear(const unsigned long& row_index)
    {
        assert(row_index >= 0 && row_index < n_rows);
        map[row_index] = Status::Updated;
    }

    Status get(const unsigned long& row_index) const
    {
        assert(row_index >= 0 && row_index < n_rows);
        if(map.find(row_index) != map.end())
            return map.at(row_index);
        // FIXME: This should never happen but it does
        else
            return Status::FreshData;
    }

    inline Status get(const Request& req) const
    {
        return get(request_to_row_index(spec, req));
    }


    void topology_reset(vector<ParityBankTopology<T>>& new_topologies, long clk, bool first_encoding)
    {
		Status queue_status = Status::Updated;
		if(!first_encoding)
			queue_status = Status::FreshData;

        map.clear(); // FIXME: This causes memory leaks(?)

		// Update map with encoded rows, update queues.
		for(auto new_topology : new_topologies)
		{
			for(int row_region_idx = 0; row_region_idx < new_topology.row_regions.size(); row_region_idx++)
			{
				auto row_region = new_topology.row_regions[row_region_idx];
				for(int row_in_bank = 0; row_in_bank < row_region.second; row_in_bank++)
				{
					map.emplace(row_region.first + row_in_bank, queue_status);
				}
			}
		}
        return;
    }

	// ASSUMPTION: the recoder stores the data for the parity bank until the parity bank is free to store it. 
	//			   This functionality is not present in the ramulator
	void tick(vector<DataBank>& data_banks, vector<ParityBank<T>>& parity_banks)
	{
		for(auto req = update_queue.begin(); req != update_queue.end();)
		{
			if(req->banks_needed.size() == 0)
			{
				clear(req->row);
				req = update_queue.erase(req);
			}
			else
				req++;
		}
	}
};

}

#endif

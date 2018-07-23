#ifndef __CODESTATUSMAP_H
#define __CODESTATUSMAP_H

#include "Coding.h"

namespace coding
{

template <typename T>
class CodeStatusMap {
public:
    enum Status { Updated, FreshData, FreshParity, MAX};
    vector<deque<pair<unsigned long /* row index */, unsigned long/* min_cycle_to_update_on */>>> update_queues;

private:
    const T *spec;
    const int n_rows;
    const int update_interval = 1;
    std::map<int,  /* row index */ Status> map;
    std::map<int, /* region_idx */ std::map<int, Status>> stored_code_maps;
	int cur_topology_idx;
	
public:
    CodeStatusMap(const T *spec) :
        spec(spec),
        n_rows(spec->org_entry.count[static_cast<int>(T::Level::Rank)]*
               spec->org_entry.count[static_cast<int>(T::Level::Bank)]*
               spec->org_entry.count[static_cast<int>(T::Level::Row)])
    {
        for(int bank = 0;
                bank < spec->org_entry.count[static_cast<int>(T::Level::Bank)];
                bank++) {
            deque<pair<unsigned long, unsigned long>> new_queue;
            update_queues.push_back(new_queue);
        }
    }
    ~CodeStatusMap() {}

    void set(const unsigned long& row_index, const Status& status, unsigned long serve_time)
    {
        assert(row_index >= 0 && row_index < n_rows);
        assert(status != Status::Updated);
        map[row_index] = status;
        auto addr = row_index_to_addr_vec(spec, row_index, 0);
        update_queues[addr[static_cast<int>(T::Level::Bank)]].push_back(
            pair<unsigned long, unsigned long>(row_index, serve_time + update_interval));
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

	//TODO: Store codes
    void topology_reset(vector<ParityBankTopology<T>>& new_topologies, long clk, bool first_encoding)
    {
		Status queue_status = Status::Updated;
		if(!first_encoding)
			queue_status = Status::FreshData;

        map.clear(); // FIXME: This causes memory leaks(?)
		// Clear queues
        for(int bank_idx = 0; bank_idx < new_topologies[0].row_regions.size(); bank_idx++)
            update_queues[bank_idx].clear(); // FIXME: This causes memory leaks(?)

		// Update map with encoded rows, update queues.
		for(auto new_topology : new_topologies)
		{
			for(int row_region_idx = 0; row_region_idx < new_topology.row_regions.size(); row_region_idx++)
			{
				auto row_region = new_topology.row_regions[row_region_idx];
				for(int row_in_bank = 0; row_in_bank < row_region.second; row_in_bank++)
				{
					map.emplace(row_region.first + row_in_bank, queue_status);
					if(!first_encoding)
						update_queues[row_region_idx].push_back(std::pair<unsigned long, unsigned long>(row_region.first + row_in_bank, clk));
				}
			}
		}
        return;
    }
};

}

#endif

#ifndef __CODESTATUSMAP_H
#define __CODESTATUSMAP_H

#include "Coding.h"

namespace coding
{

template <typename T>
class CodeStatusMap {
public:
    enum Status { Updated, FreshData, FreshParity };
    vector<deque<pair<unsigned long /* row index */, unsigned long/* min_cycle_to_update_on */>>> update_queues;

private:
    const T *spec;
    const int n_rows;
    const int update_interval = 1;
    std::map<int,  /* row index */ Status> map;
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

        for (int r {0}; r < n_rows; r++) {
            auto addr = row_index_to_addr_vec(spec, r, 0);
            update_queues[addr[static_cast<int>(T::Level::Bank)]].push_back(
                pair<unsigned long, unsigned long>(r, 0));
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

    void topology_reset(ParityBankTopology<T>& new_topology, long clk)
    {
        uint32_t n_rows = 0;
        map.clear(); // FIXME: This causes memory leaks(?)
        for(int row_region_idx= 0; row_region_idx < new_topology.row_regions.size(); row_region_idx++)
        {
            auto row_region = new_topology.row_regions[row_region_idx];
            n_rows += row_region.second;
            update_queues[row_region_idx].clear(); // FIXME: This causes memory leaks(?)
            for(int row_in_bank = 0; row_in_bank < row_region.second; row_in_bank++)
            {
                map.emplace(row_region.first + row_in_bank, Status::FreshData);
                update_queues[row_region_idx].push_back(
                    std::pair<unsigned long, unsigned long>(row_region.first + row_in_bank, clk));
            }
        }
        return;
    }
};

}

#endif

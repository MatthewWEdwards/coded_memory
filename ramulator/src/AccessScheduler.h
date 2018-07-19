#ifndef __ACCESS_SCHEDULER_H
#define __ACCESS_SCHEDULER_H

#include "Coding.h"
#include "Request.h"

namespace coding
{

//XXX: NOTE parity read latency is identical to data bank read latency
template <typename T>
class AccessScheduler
{

using ParityBank = coding::ParityBank<T>;
using ParityBankTopology = coding::ParityBankTopology<T>;
using XorCodedRegions = coding::XorCodedRegions<T>;
using MemoryRegion = coding::MemoryRegion<T>;
using CodeStatus = typename coding::CodeStatusMap<T>::Status;

private:
	int num_banks = 8; // Default is 8, updated according to the DRAM spec
	long clk = 0;
	DRAM<T>* channel;
	vector<coding::ParityBank<T>> parity_banks;

	// ReCoding Controller Variables
	coding::CodeStatusMap<T> * code_status;
	int coding_region_counter = 0; // Counts how many memory ticks since last recoding check
	const int coding_region_reschedule_ticks = 1e3; // TODO: Discuss how to choose this value

	// Dynamic Coding Variables
	vector<coding::ParityBankTopology<T>> topologies; // TODO: This doesn't need to be a pointer
	vector<unsigned long> topology_hits;
	unsigned int active_topology_idx = 0; 


public:
	AccessScheduler(int memory_coding, double alpha, DRAM<T>* channel)
	{
		this->channel = channel;
		this->num_banks = channel->spec->org_entry.count[static_cast<int>(T::Level::Bank)];
		this->code_status = new coding::CodeStatusMap<T>(channel->spec);

		/* build a list of possible memory topologies that can be selected for
		 * coding based on the number of hits */
		const int ranks {
			channel->spec->org_entry.count[static_cast<int>(T::Level::Rank)]
		};
		const int banks_per_rank {channel->spec->org_entry.count[static_cast<int>(T::Level::Bank)]};
		const int rows_per_bank {channel->spec->org_entry.count[static_cast<int>(T::Level::Row)]};
		const int col_per_row {channel->spec->org_entry.count[static_cast<int>(T::Level::Column)]};
		const int rows_per_region {rows_per_bank*alpha};
		const int code_regions_per_bank = ceil(1 / alpha);

		/* divide memory into subregions for coding */
		for (int c {0}; c < code_regions_per_bank; c++) {
			vector<MemoryRegion> regions;
			int this_bank {0};
			for (int r {0}; r < ranks; r++) {
				for (int b {0}; b < banks_per_rank; b++) {
					int start_row {c*rows_per_region};
					vector<int> this_addr_vec {location_addr_vec(r, b, start_row)};
					unsigned long this_row_index {coding::addr_vec_to_row_index(channel->spec,
												  this_addr_vec)};
					int remaining_rows = rows_per_bank - c*rows_per_region;
					MemoryRegion bank_region {channel->spec,
											  this_row_index,
											  std::min(remaining_rows, rows_per_region)};
					regions.push_back(bank_region);

					this_bank++;

					if (this_bank >= num_banks)
					{
						/* we've collected enough for the coding scheme; build a complete topology */
						ParityBankTopology topology =
							coding::ParityBankTopologyConstructor(regions, memory_coding);
						this->topologies.push_back(topology);
						regions.clear();
						this_bank = 0;
					}
				}
			}
		}

		assert(this->topologies.size() > 0);

		/* init parity banks */
		this->parity_banks.resize(topologies[0].n_parity_banks, {channel->spec->read_latency});

		/* init coding region controller */
		this->topology_hits.resize(topologies.size(), 0);

		/* init active topology */
		switch_coding_regions(active_topology_idx);
	}

	~AccessScheduler() 
	{
		delete code_status;
	}

    void read_pattern_builder(long clk, coding::BankQueue* readq, list<Request>* pending)
    {
		this->clk = clk;
        for (ParityBank& bank : parity_banks)
            /* match queued reads to pending reads */
            if (!bank.busy())
                schedule_queued_read_for_parity_bank(bank, readq, pending);
    }

    void write_pattern_builder(long clk, coding::BankQueue* writeq, list<Request>* pending)
    {
		this->clk = clk;
        for (ParityBank& bank : parity_banks)
            /* find queued writes to serve with parity banks instead
             * of main memory */
            if (!bank.busy())
                schedule_queued_write_for_parity_bank(bank, writeq, pending);
    }

	void tick(long clk, coding::BankQueue* readq, coding::BankQueue* writeq, list<Request>* pending)
	{
		for (ParityBank& bank : parity_banks)
			/* update internal state */
			bank.tick();
		read_pattern_builder(clk, readq, pending);
		write_pattern_builder(clk, writeq, pending);
		coding_region_controller();
	}

private:
    vector<int> location_addr_vec(const int& rank, const int& bank, const int& row)
    {
        vector<int> addr_vec(static_cast<int>(T::Level::MAX));
        addr_vec[static_cast<int>(T::Level::Channel)] = channel->id;
        addr_vec[static_cast<int>(T::Level::Rank)] = rank;
        addr_vec[static_cast<int>(T::Level::Bank)] = bank;
        addr_vec[static_cast<int>(T::Level::Row)] = row;
        return addr_vec;
    }


//=========Read and Write Pattern Builders Helper Functions==============================================
    void schedule_queued_read_for_parity_bank(ParityBank& bank, coding::BankQueue* readq, list<Request>* pending)
    {
        /* select all queued reads that could be served by this parity
         * bank */
        vector<reference_wrapper<Request>> candidate_reads;	
		for (unsigned int bank_idx = 0; bank_idx < num_banks; bank_idx++)
			for (Request& req : readq->queues[bank_idx])
				if (bank.contains(req))
					candidate_reads.push_back(req);
        /* try to schedule each candidate read */
        for (Request& read : candidate_reads) {
            pair<bool, long> schedulable_result
            {can_schedule_queued_read_for_parity_bank(read, bank, pending)};
            bool schedulable {schedulable_result.first};
            long depart {schedulable_result.second};
            if (schedulable) {
                bank.lock();
                schedule_served_request(read, depart, pending);
                remove_read_from_readq(read, readq);
                break;
            }
        }
    }

    pair<bool, long> can_schedule_queued_read_for_parity_bank(Request& read,
            ParityBank& bank, list<Request>* pending)
    {
        /* select all pending reads that could be used by this parity
         * bank and were not previously scheduled by us */
        vector<reference_wrapper<Request>> candidate_pending;
        for (Request& req : *pending)
            if (code_status->get(req) == CodeStatus::Updated && bank.contains(req) )
                candidate_pending.push_back(req);
        /* get a list of all the XOR regions needed to complete the parity */
        const XorCodedRegions& xor_regions {
            bank.request_xor_regions(read)
        };
        const MemoryRegion& read_region {xor_regions.request_region(read)};
        vector<reference_wrapper<const MemoryRegion>> other_regions;
        vector<Request> requests_to_schedule;
        for (const MemoryRegion& region : xor_regions.regions)
            if (region != read_region)
                other_regions.push_back(region);
        /* find a pending read in the same row number for every other XOR region */
        long depart {clk + channel->spec->read_latency};
        for (const MemoryRegion& other_region : other_regions) {
            // Check pending reads
            auto can_use {[this, xor_regions, read, other_region](Request& req)
            {   return code_status->get(req) == CodeStatus::Updated &&
                       xor_regions.request_region(req) == other_region &&
                       xor_regions.same_request_row_numbers(read, req);
            }};
            vector<reference_wrapper<Request>>::iterator
                                            row_pending {find_if(begin(candidate_pending),
                                                    end(candidate_pending),
                                                    can_use)};
            if (row_pending != end(candidate_pending)) {
                depart = max<long>(depart, row_pending->get().depart);
                continue;
            }

            /* couldn't find a match for a region, * this read request can't be scheduled */
            return {false, -1};
        }
        /* we were able to complete the parity, return
         * max(pending request(s) time(s), parity bank read latency) */
        return {true, depart};
    }

    void schedule_queued_write_for_parity_bank(ParityBank& bank, coding::BankQueue* writeq, list<Request>* pending)
    {
        /* select a queued write that could be served by this parity
         * bank */
        auto can_serve {[bank](Request& req) {
            return bank.contains(req);
        }};
		for(unsigned int bank_idx = 0; bank_idx < num_banks; bank_idx++)
		{
			auto write_it {find_if(begin(writeq->queues[bank_idx]), end(writeq->queues[bank_idx]), can_serve)};
			/* if one exists, serve it */
			//TODO: Why only one write?
			if (write_it != end(writeq->queues[bank_idx])) {
				bank.lock();
				unsigned long serve_time = clk + channel->spec->read_latency;
				schedule_served_request(*write_it, serve_time, pending);
				const auto row_index {coding::request_to_row_index(channel->spec, *write_it)};
				writeq->queues[bank_idx].erase(write_it);
				code_status->set(row_index, CodeStatus::FreshParity, serve_time);
				return;
			}
		}
    }

	void remove_read_from_readq(const Request& req, coding::BankQueue* readq)
    {
        unsigned int bank_num = req.addr_vec[static_cast<int>(T::Level::Bank)];
        for (auto read_it {std::begin(readq->queues[bank_num])};
                read_it != std::end(readq->queues[bank_num]); ++read_it) {
            if (&(*read_it) == &req) { // FIXME: implement comparison?
                readq->queues[bank_num].erase(read_it);
                return;
            }
        }
        assert(false);
    }

    void schedule_served_request(Request& req, const long& depart, list<Request>* pending)
    {
        req.bypass_dram = true;
        req.depart = depart;
        pending->push_back(req);
        /* inserting in nonlinear order, so resort the pending queue */
        pending->sort([](const Request& a, const Request& b)
        {
            return a.depart < b.depart;
        });
    }

//=========ReCoding Scheduler===============================================
public:
    void recoding_controller(unsigned long bank_busy_flags)
    {
        // Attempt one recode per bank
        for(int bank = 0;
                bank < num_banks;
                bank++) {

            // If the bank is busy, abort attempt
            // TODO: randomly select a bank?
            if((bank_busy_flags & (0x1 << bank)) == 0 )
                continue;

            // TODO: Only recode the current encoded rows in the topology
            for(auto recode_req = code_status->update_queues[bank].begin();
                    recode_req != code_status->update_queues[bank].end();
                    recode_req++) {
                bool to_recode = false;
                bool can_recode = true;

                for(int parity_bank = 0; parity_bank < parity_banks.size(); parity_bank++) {
                    if(topologies[active_topology_idx].row_regions[bank].first < recode_req->first &&
                            topologies[active_topology_idx].row_regions[bank].first + topologies[active_topology_idx].row_regions[bank].second > recode_req->first) {
                        to_recode = true;
                    }
                }

                if(to_recode && parity_banks[bank].busy()) {
                    // Cannot schedule recode at this time
                    can_recode = false;
                    break;
                }

                if(can_recode) {
                    main_memory_recode(recode_req->first);
                    code_status->update_queues[bank].erase(recode_req);
                    //TODO: Lock Bank? how long does the recode take?
                    // Is this basically a write request?
                    break;
                }
            }
        }
    }

	//FIXME: Unused, may be obsolete (I manually track data bank usage per tick via "bank_busy_flag" or some such variable)
    bool memory_bank_is_free(const unsigned long& row_index)
    {
        /* a memory bank is free if ramulator's decode returns the read
         * command given a read command */
        auto addr_vec {coding::row_index_to_addr_vec<T>(channel->spec,
                       row_index,
                       channel->id)};
        return channel->decode(T::Command::RD, addr_vec.data()) ==
               T::Command::RD;
    }

private:
	//TODO: Store codes on disk
    inline void main_memory_recode(const unsigned long& row_index)
    {
        code_status->clear(row_index);
    }

//=========Dynamic Coding Controller===============================================
//TODO: Record number of topology switches
public:
    void coding_region_controller()
    {
		coding_region_counter++;
        if (coding_region_counter >= coding_region_reschedule_ticks) {
            /* find the coding topology with the most hits */
            auto most_hits_index {std::max_element(topology_hits.begin(),
                                                   topology_hits.end())
                                  - topology_hits.begin()};
            /* switch the parity banks to it */
            if(&topologies[active_topology_idx] != &topologies[most_hits_index]) // TODO: implement topology comparison so I don't need to compare their addresses
            {
                active_topology_idx = most_hits_index;
                switch_coding_regions(active_topology_idx);
            }
            /* reset tracking counters */
            topology_hits.assign(topology_hits.size(), 0);
            coding_region_counter = 0;
        }
    }

    void coding_region_hit(const Request& req)
    {
        for (int i {0}; i < topologies.size(); i++) {
            if (topologies[i].contains(req)) {
                topology_hits[i]++;
                return;
            }
        }
        /* should theoretically never happen, but there are some
         * nonsensical requests where addr_vec = {0, 0, -1, -1, -1} */
        //assert(false);
    }

private:
    void switch_coding_regions(unsigned int active_topology_idx)
    {
        /* copy the parity bank config of the new topology into the
         * active one */
		auto topology_switch = topologies[active_topology_idx];
        size_t n_parity_banks {topologies[0].n_parity_banks};
        for (int b {0}; b < n_parity_banks; b++) {
            auto regions_list {topology_switch.xor_regions_for_parity_bank[b]};
            /* convoluted workaround since the copy-assignment
             * operator is implicitly deleted for XorCodedRegions */
            parity_banks[b].xor_regions.clear();
            for (int xr {0}; xr < regions_list.size(); xr++) {
                vector<coding::MemoryRegion<T>> regions
                {regions_list[xr].regions};
                parity_banks[b].xor_regions.push_back({regions});
            }
        }
        code_status->topology_reset(topology_switch, clk);
    }
};

}

#endif

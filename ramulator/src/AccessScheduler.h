#ifndef __ACCESS_SCHEDULER_H
#define __ACCESS_SCHEDULER_H

#include "Coding.h"
#include "Request.h"
#include "Statistics.h"
#include "CodeStatusMap.h"


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

protected:
	// Stats
	ScalarStat topology_switches;

private:
	int num_banks = 8; // Default is 8, updated according to the DRAM spec
	long clk = 0;
	DRAM<T>* channel;
	vector<coding::ParityBank<T>> parity_banks;

	// ReCoding Controller Variables
	coding::CodeStatusMap<T> * code_status;
	int coding_region_counter = 0; // Counts how many memory ticks since last recoding check
	const int coding_region_reschedule_ticks = 1e3; // TODO: Discuss how to choose this value
	double coding_region_length = .001; //TODO Make this a config option
	double alpha = 1;

	// Dynamic Coding Variables
	vector<coding::ParityBankTopology<T>> topologies; 
	vector<unsigned long> topology_hits;
	unsigned int active_topology_idx = 0; 
	vector<int> active_topologies;

public:
	AccessScheduler(int memory_coding, double alpha, double coding_region_length, DRAM<T>* channel)
	{
		this->channel = channel;
		this->alpha = alpha;
		this->coding_region_length = coding_region_length;
		this->num_banks = channel->spec->org_entry.count[static_cast<int>(T::Level::Bank)];
		this->code_status = new coding::CodeStatusMap<T>(channel->spec);

		/* build a list of possible memory topologies that can be selected for
		 * coding based on the number of hits */
		const int ranks {
			channel->spec->org_entry.count[static_cast<int>(T::Level::Rank)]
		};
		const int banks_per_rank {channel->spec->org_entry.count[static_cast<int>(T::Level::Bank)]};
		const int rows_per_bank {channel->spec->org_entry.count[static_cast<int>(T::Level::Row)]};
		const int rows_per_region {rows_per_bank*coding_region_length};
		const int code_regions_per_bank = ceil(1 / coding_region_length);

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
	
		/* get init coding regions */
		for(unsigned int tops = 0; tops < alpha/coding_region_length; tops++)
			active_topologies.push_back(tops);

		/* init active topology */
		switch_coding_regions(active_topologies, true);
	
		/* Set up stats */
		topology_switches
		.name("topology_switches_"+to_string(channel->id))
		.desc("Number of switches made by the dynamic coding unit")
		.precision(0)
		;
	}

	~AccessScheduler() 
	{
		delete code_status;
	}

    void read_pattern_builder(long clk, coding::BankQueue* cur_queue, vector<coding::DataBank>* data_banks, list<Request>& reqs_scheduled)
    {
		this->clk = clk;

		BankQueue * readq = new BankQueue(*cur_queue);

		// Grab read requests from data banks
		for(DataBank& data_bank : *data_banks)
		{
			auto req = readq->queues[data_bank.index].begin();
			while(req != readq->queues[data_bank.index].end())
			{
				auto coding_status = code_status->get(*req);
				if(coding_status != CodeStatus::FreshData && coding_status != CodeStatus::Updated)
				{
					req++;
					continue;  // Data bank has stale data for the row requested
				}
				data_bank.read();
				reqs_scheduled.push_back(*req);
				req = readq->queues[data_bank.index].erase(req);
				break;
			}
		}

		// Go through readq and attempt to serve requests using parities and downloaded DataBank reads
		for(auto bank_queue = readq->queues.begin();
			bank_queue != readq->queues.end();
			bank_queue++)
		{
			auto req = bank_queue->begin();
			while(req != bank_queue->end())
			{
				// Attempt to use each parity
				bool parity_success = false;
				for(auto parity_bank = parity_banks.begin();
					parity_bank != parity_banks.end() && !parity_success;
					parity_bank++)
				{
					if(parity_bank->busy() || !parity_bank->contains(*req))
						continue;
					auto coding_status = code_status->get(*req);
					//if(coding_status == CodeStatus::FreshParity) 
					//	// It may be possible to serve the request using fresh data in the parity. TODO
					if(coding_status != CodeStatus::Updated) 
						continue;  // Parity bank has stale data for the row requested
					/* get a list of all the XOR regions needed to complete the parity */
					const XorCodedRegions& xor_regions { parity_bank->request_xor_regions(*req) };
					const MemoryRegion& read_region {xor_regions.request_region(*req)};
					vector<reference_wrapper<const MemoryRegion>> other_regions;
					vector<Request> requests_to_schedule;
					for (const MemoryRegion& region : xor_regions.regions)
						if (region != read_region)
							other_regions.push_back(region);
					/* find a read in the same row number or read from an idle data bank for every other XOR region */
					bool read_flag = true;
					vector<int> data_banks_to_read;
					for(const MemoryRegion& other_region : other_regions) 
					{
						// Check if a scheduled read matches the xor region
						bool matched_read = false;
						for(auto read : reqs_scheduled)
						{
							if(xor_regions.request_region(*req) == other_region && 
						       xor_regions.same_request_row_numbers(read, *req))
							{
								matched_read = true;
								break;
							}
						}
						if(matched_read)
							continue;

						// Check if an idle data bank can be used to satisfy and xor region
						long region_bank_num = other_region.get_bank();
						auto region_addr_needed = req->addr_vec;
						region_addr_needed[static_cast<int>(T::Level::Bank)] = region_bank_num;
						long region_line_needed = addr_vec_to_row_index(channel->spec, region_addr_needed);

						auto coding_status = code_status->get(region_line_needed);
						if(data_banks->at(region_bank_num).is_free() && 
						   (coding_status == CodeStatus::FreshData || coding_status == CodeStatus::Updated))
						{
							data_banks_to_read.push_back(region_bank_num);
							continue;
						}
						read_flag = false;	// Memory region could not be decoded
						data_banks_to_read.clear();
						break;
					}

					if(read_flag)
					{
						/* we were able to complete the parity, return
						 * max(pending request(s) time(s), parity bank read latency) */
						parity_bank->lock();
						for(auto read_bank_idx : data_banks_to_read)
							data_banks->at(read_bank_idx).read();
						reqs_scheduled.push_back(*req);
					    parity_success = true;
						break;
					}
				}
				if(parity_success)	
					req = bank_queue->erase(req);
				else
					req++;
			}
		}
		delete readq;
    }

    void write_pattern_builder(long clk, coding::BankQueue* writeq, vector<coding::DataBank>* data_banks, list<Request>& reqs_scheduled)
    {
		// Grab requests from queues
		for(unsigned int bank_idx = 0; bank_idx < num_banks; bank_idx++)
		{
			auto req = writeq->queues[bank_idx].begin();
			if(req == writeq->queues[bank_idx].end())	
				continue;
			data_banks->at(bank_idx).read();
			unsigned long serve_time = clk + channel->spec->read_latency;
			const auto row_index {coding::request_to_row_index(channel->spec, *req)};
			reqs_scheduled.push_back(*req);
			code_status->set(row_index, CodeStatus::FreshData, serve_time);
		}
    }

	void tick(long clk, coding::BankQueue* readq, coding::BankQueue* writeq, vector<coding::DataBank>* data_banks, list<Request>& reqs_scheduled, bool write_mode)
	{
		/* refresh banks */
		for (ParityBank& bank : parity_banks)
			bank.tick();
		if(!write_mode)
			read_pattern_builder(clk, readq, data_banks, reqs_scheduled);
		else
			write_pattern_builder(clk, writeq, data_banks, reqs_scheduled);
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


//=========ReCoding Scheduler===============================================
public:

	// TODO: Rewrite
    void recoding_controller(unsigned long bank_busy_flags)
    {
        // Attempt one recode per bank
        for(int bank = 0;
                bank < num_banks;
                bank++) {

            // If the bank is busy, abort attempt
            if((bank_busy_flags & (0x1 << bank)) == 0 )
                continue;

            // TODO: Only recode the current encoded rows in the topology
            for(auto recode_req = code_status->update_queues[bank].begin();
                    recode_req != code_status->update_queues[bank].end();
                    recode_req++) {
                bool to_recode = true;
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
//TODO: Allow unadjacent subregions to be coded.
public:
    void coding_region_controller()
    {
		coding_region_counter++;
        if (coding_region_counter >= coding_region_reschedule_ticks) {
            /* find the coding topologies with the most hits */
			unsigned int num_topologies_to_select = alpha/coding_region_length;
			vector<int> topologies_selected;
			for(unsigned int num_top = 0; num_top < num_topologies_to_select; num_top++)
			{
				int max_idx = distance(topology_hits.begin(), max_element(topology_hits.begin(), topology_hits.end()));
				if(topology_hits[max_idx] == 0)
					break;
				topologies_selected.push_back(max_idx);
				topology_hits[max_idx] = 0;
			}
			for(int top_sel_idx : topologies_selected)
			{
				if(find(active_topologies.begin(), active_topologies.end(), top_sel_idx) == active_topologies.end())
				{
					/* switch the parity banks to it */
					active_topologies = topologies_selected;
					switch_coding_regions(active_topologies, false);
					break;
				}
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
    void switch_coding_regions(const vector<int>& active_topologies, bool first_encoding)
    {
		// Grab active topologies
		vector<ParityBankTopology> new_tops;
		for(unsigned long top_idx = 0; top_idx < active_topologies.size(); top_idx++)
			new_tops.push_back(topologies[top_idx]);
		
		// Prepare parity bank structure
        size_t n_parity_banks {topologies[0].n_parity_banks};
		for (int b {0}; b < n_parity_banks; b++) 
		{
			parity_banks[b].xor_regions.clear();
			parity_banks[b].xor_regions.resize(new_tops.size());
		}
		for(unsigned long top_idx = 0; top_idx < active_topologies.size(); top_idx++)
		{
			for (int b {0}; b < n_parity_banks; b++) 
			{
				auto regions_list {topologies[top_idx].xor_regions_for_parity_bank[b]};
				for (int xr {0}; xr < regions_list.size(); xr++) 
				{
					vector<coding::MemoryRegion<T>> regions
					{regions_list[xr].regions};
					parity_banks[b].xor_regions[top_idx].push_back({regions});
				}
			}
		}
        code_status->topology_reset(new_tops, clk, first_encoding);
		topology_switches++;
    }

	/* Returns true if topologies need to be switched
	 * OUT:
	 *   regions_to_encode: New regions that need to be encoded by the recoding unit
	 *   regions_to_evict: regions which are replaced by regions to encode due to size limitations
     */
	bool get_new_regions(vector<int>& regions_to_encode, vector<int>& regions_to_evict)
	{
		unsigned int num_topologies_to_select = alpha/coding_region_length;
		vector<int> topologies_selected;
		for(unsigned int num_top = 0; num_top < num_topologies_to_select; num_top++)
		{
			int max_idx = distance(topology_hits.begin(), max_element(topology_hits.begin(), topology_hits.end()));
			if(topology_hits[max_idx] == 0)
				break;
			topologies_selected.push_back(max_idx);
			topology_hits[max_idx] = 0;
		}
		for(int top_sel_idx : topologies_selected)
		{
			if(find(active_topologies.begin(), active_topologies.end(), top_sel_idx) == active_topologies.end())
			{
				/* switch the parity banks to it */
				active_topologies = topologies_selected;
				switch_coding_regions(active_topologies, false);
				break;
			}
		}

		for(auto top_sel : topologies_selected)
		{
			if(find(active_topologies.begin(), active_topologies.end(), top_sel))
				continue;
			regions_to_encode.push_back(top_sel);
		}
		for(int evict_idx = active_topologies.size() - 1; 
			evict_idx > active_topologies.size() - 1 - topologies_selected.size();
			evict_idx--)
		{

		}

	} 
};

}

#endif /* AccessScheduler.h */

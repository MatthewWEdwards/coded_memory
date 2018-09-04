#ifndef __ACCESS_SCHEDULER_H
#define __ACCESS_SCHEDULER_H

#include "Coding.h"
#include "Request.h"
#include "Statistics.h"
#include "RecodingUnit.h"
#include "DynamicEncoder.h"

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

protected:
	// Stats
	ScalarStat topology_switches;

private:
	int num_banks = 8; // Default is 8, updated according to the DRAM spec
	long clk = 0;
	DRAM<T>* channel;
	vector<coding::ParityBank<T>> parity_banks;

	// ReCoding Controller Variables
	coding::RecodingUnit<T> * recoder_unit;
	int coding_region_counter = 0; // Counts how many memory ticks since last recoding check
	const int coding_region_reschedule_ticks = 1e3; // TODO: Discuss how to choose this value
	double coding_region_length = .01; 
	double alpha = 1;

	// Dynamic Coding Variables
	DynamicEncoder<T> * dynamic_encoder;
	vector<unsigned long> topology_hits;
	unsigned int active_topology_idx = 0; 
	set<unsigned int> selected_regions;
	set<unsigned int> active_regions;
	vector<coding::ParityBankTopology<T>> topologies; 

public:
	AccessScheduler(int memory_coding, double alpha, double coding_region_length, DRAM<T>* channel)
	{
		this->channel = channel;
		this->alpha = alpha;
		this->coding_region_length = coding_region_length;
		this->num_banks = channel->spec->org_entry.count[static_cast<int>(T::Level::Bank)];
		this->recoder_unit = new coding::RecodingUnit<T>(channel->spec);

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
			selected_regions.insert(tops);

		/* init active topology */
		init_coding_regions(selected_regions);
		
		/* assume the default coded regions are encoded before execution */
		active_regions = selected_regions;
	
		/* init Dynamic Encoder */
		this->dynamic_encoder = new coding::DynamicEncoder<T>(topologies, active_regions);

		/* Set up stats */
		topology_switches
		.name("topology_switches_"+to_string(channel->id))
		.desc("Number of switches made by the dynamic coding unit")
		.precision(0)
		;
	}

	~AccessScheduler() 
	{
		delete recoder_unit;
		delete dynamic_encoder;
	}

	void tick(long clk, coding::BankQueue* readq, coding::BankQueue* writeq, list<Request>* pending_queue, vector<coding::DataBank>* data_banks, list<Request>& reqs_scheduled, bool write_mode)
	{
		/* refresh banks */
		for (ParityBank& bank : parity_banks)
			bank.tick();
		if(!write_mode)
			read_pattern_builder(clk, readq, pending_queue, data_banks, reqs_scheduled);
		else
			write_pattern_builder(clk, writeq, data_banks, reqs_scheduled);
	}

	void rewrites(vector<coding::DataBank>& data_banks, list<Request>& reqs_scheduled)
	{
		recoding_controller(data_banks, reqs_scheduled);      // ReCoding Unit
		coding_region_controller(data_banks, reqs_scheduled); // Dynmaic Recoder
		//TODO: Add code prefetcher
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

public:

//=========Read Build Pattern===============================================
	//TODO Prioritize data bank queues which are longer
    void read_pattern_builder(long clk, coding::BankQueue* cur_queue, list<Request>* pending_queue, vector<coding::DataBank>* data_banks, list<Request>& reqs_scheduled)
    {
		this->clk = clk;

		BankQueue * readq = new BankQueue(*cur_queue);

		// Grab read requests from data banks
		for(auto data_bank = data_banks->begin(); data_bank != data_banks->end(); data_bank++)
		{
			auto req = readq->queues[data_bank->index].begin();
			while(req != readq->queues[data_bank->index].end())
			{
				auto coding_status = recoder_unit->get(*req);
				if(coding_status == CodeStatus::FreshParity)
				{
					req++;
					continue;  // Data bank has stale data for the row requested
					//TODO It may be possible to use a parity bank to recover the data, hardware complexity problems?
				}
				data_bank->lock();
				recoder_unit->receive_row(req->addr);
				reqs_scheduled.push_back(*req);
				Request ref_req = *req; // XXX
				req = readq->queues[data_bank->index].erase(req);
				serve_with_parity(ref_req, reqs_scheduled, readq);
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
				bool parity_success = attempt_parity_read(*req, data_banks, reqs_scheduled, *pending_queue);
				if(parity_success)	
					req = bank_queue->erase(req);
				else
					req++;
			}
		}
		delete readq;
    }

	bool attempt_parity_read(const Request& req, vector<DataBank>* data_banks, list<Request>& reqs_scheduled, list<Request>& pending_queue)
	{
		// This function attempts to used each parity banks as the "base" decoding bank
		for(auto parity_bank = parity_banks.begin();
		parity_bank != parity_banks.end();
		parity_bank++)
		{
			if(parity_bank->busy() || !parity_bank->contains(req))
				continue;
			auto coding_status = recoder_unit->get(req);
			if(coding_status == CodeStatus::FreshParity)
				{}// TODO:It may be possible to serve the request using fresh data in the parity.
			if(coding_status != CodeStatus::Updated)
				continue;  // Parity bank has stale data (or no data) for the row requested
			/* get a list of all the XOR regions needed to complete the parity */
			const XorCodedRegions& xor_regions { parity_bank->request_xor_regions(req) };
			const MemoryRegion& read_region {xor_regions.request_region(req)};
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
					if(xor_regions.request_region(req) == other_region &&
					   xor_regions.same_request_row_numbers(read, req))
					{
						matched_read = true;
						break;
					}
				}
				if(matched_read)
					continue;

				// Check if requests in the pending queue can be used to satisfy an xor region
				for(auto pending_req : pending_queue)
				{
					if(other_region.contains(pending_req) && xor_regions.same_request_row_numbers(pending_req, req))
					{
						matched_read = true;
						break;
					}
				}
				if(matched_read)
					continue;

				// Check if an idle data bank can be used to satisfy an xor region
				long region_bank_num = other_region.get_bank();
				auto region_addr_needed = req.addr_vec;
				region_addr_needed[static_cast<int>(T::Level::Bank)] = region_bank_num;
				long region_line_needed = addr_vec_to_row_index(channel->spec, region_addr_needed);

				auto coding_status = recoder_unit->get(region_line_needed, region_bank_num);
				if(!data_banks->at(region_bank_num).busy() && coding_status != CodeStatus::FreshParity)
				{
					data_banks_to_read.push_back(region_bank_num);
					continue;
				}
				read_flag = false;  // Memory region could not be decoded
				data_banks_to_read.clear();
				break;
			}

			if(read_flag)
			{
				/* we were able to complete the parity, return
				 * max(pending request(s) time(s), parity bank read latency) */
				parity_bank->lock();
				recoder_unit->receive_row(req.addr);
				for(auto read_bank_idx : data_banks_to_read)
					data_banks->at(read_bank_idx).lock();
				reqs_scheduled.push_back(req);
				return true;
			}
		}
		return false;
	}

   /* Searches the read queue for a request with the same relative row as the input request and
    * attempts to the request using a parity bank.
	*
	* input_req: The request to match with a parity read.
	* reqs_scheduled: Requests scheduled by the Access Scheduler. This function pushes another request 
	*				  to this list if it succeeds.
	* readq: The readq which contains all not scheduled reads.
	*
	* Returns: whether a request was served using a parity bank
	*/
	bool serve_with_parity(const Request& input_req, list<Request>& reqs_scheduled, coding::BankQueue* readq)
	{
		unsigned int input_req_bank = input_req.addr_vec[static_cast<int>(T::Level::Bank)];
		unsigned int input_req_row = input_req.addr_vec[static_cast<int>(T::Level::Row)];

		for(auto queue_idx = 0; queue_idx < num_banks; queue_idx++)
		{
			if(queue_idx == input_req_bank)
				continue;
			for(auto req = readq->queues[queue_idx].begin(); req != readq->queues[queue_idx].end(); req++)
				if(input_req_row == req->addr_vec[static_cast<int>(T::Level::Row)])
					for(auto parity_bank = parity_banks.begin(); parity_bank != parity_banks.end(); parity_bank++)
						if(parity_bank->contains(input_req) && parity_bank->contains(*req) && !parity_bank->busy())
						{
							parity_bank->lock();
							recoder_unit->receive_row(req->addr);
							reqs_scheduled.push_back(*req);
							//XXX The order of these two recursive calls will effect performance
							Request ref_req = *req;
							readq->queues[queue_idx].erase(req);
							serve_with_parity(ref_req, reqs_scheduled, readq);
							serve_with_parity(input_req, reqs_scheduled, readq);
							return true;
						}
		}
		return false;
	}

//=========Write Build Pattern==============================================
    void write_pattern_builder(long clk, coding::BankQueue* cur_queue, vector<coding::DataBank>* data_banks, list<Request>& reqs_scheduled)
    {
		this->clk = clk;
		
		BankQueue * writeq = new BankQueue(*cur_queue);

		// Grab requests from queues
		for(unsigned int bank_idx = 0; bank_idx < num_banks; bank_idx++)
		{
			auto req = writeq->queues[bank_idx].begin();
			if(req == writeq->queues[bank_idx].end())	
				continue;
			data_banks->at(bank_idx).lock();
			unsigned long serve_time = clk + channel->spec->read_latency;
			reqs_scheduled.push_back(*req);
		
			for(auto active_region_index : active_regions)
				if(topologies[active_region_index].contains(*req))
					recoder_unit->set(*req, CodeStatus::FreshData, serve_time, topologies); 
			writeq->queues[bank_idx].erase(req);
		}
	
		for(auto write_queue = writeq->queues.begin(); write_queue != writeq->queues.end(); write_queue++)
			for(auto req = write_queue->begin(); req != write_queue->end(); req++)
			{
				bool successful_write = false;
				for(auto parity_bank = parity_banks.begin(); parity_bank != parity_banks.end(); parity_bank++)
					if(parity_bank->contains(*req) && !parity_bank->busy())
					{
						recoder_unit->receive_row(req->addr);
						recoder_unit->set(*req, CodeStatus::FreshParity, clk, topologies);
						parity_bank->lock();
						reqs_scheduled.push_back(*req);
						req = write_queue->erase(req);
						successful_write = true;
						break;
					}
				if(successful_write)
					break;
			}
		
		delete writeq;
    }


//=========ReCoding Scheduler===============================================
public:
    void recoding_controller(vector<DataBank>& data_banks, list<Request>& reqs_scheduled)
    {
		// Utilize idle data banks to reconstruct codes
		for(auto bank = data_banks.begin(); bank != data_banks.end(); bank++)
		{
			if(bank->busy())
				continue;
			for(auto recode_req = recoder_unit->update_queue.begin();
				recode_req != recoder_unit->update_queue.end();
				recode_req++)
			{
				if(recode_req->receive_bank(bank->index))
				{
					// XXX
					bank->lock();
					Request dummy_req = recode_req->req;
					dummy_req.addr_vec[static_cast<int>(T::Level::Bank)] = bank->index;
					reqs_scheduled.push_back(dummy_req);
					break;
				}
			}
		}

		// Utilize parity banks to reconstruct codes
		for(auto recode_req = recoder_unit->update_queue.begin();
			recode_req != recoder_unit->update_queue.end();
			recode_req++)
		{
			vector<int> banks_satisfied;
			for(auto bank_needed : recode_req->banks_needed)
			{
				// XXX
				Request dummy_req = recode_req->req;
				dummy_req.addr_vec[static_cast<int>(T::Level::Bank)] = bank_needed;
				list<Request> dummy_pending_queue;
				bool parity_read = attempt_parity_read(dummy_req, &data_banks, reqs_scheduled, dummy_pending_queue);
				if(parity_read)
					banks_satisfied.push_back(bank_needed);
			}
			for(auto bank_satisfied : banks_satisfied)
					recode_req->receive_bank(bank_satisfied);
			}

			// Attempt rewrites
			recoder_unit->tick(data_banks, parity_banks);
		}

//=========Dynamic Encoding Unit============================================
public:
	void coding_region_controller(vector<coding::DataBank>& data_banks, list<Request>& reqs_scheduled)
	{
		coding_region_counter++;
		if (coding_region_counter >= coding_region_reschedule_ticks) {
			/* find the coding topologies with the most hits */
			unsigned int num_topologies_to_select = alpha/coding_region_length;
			set<unsigned int> regions_selected;
			for(unsigned int num_top = 0; num_top < num_topologies_to_select; num_top++)
			{
				unsigned int max_idx = distance(topology_hits.begin(), max_element(topology_hits.begin(), topology_hits.end()));
				if(topology_hits[max_idx] == 0)
					break;
				regions_selected.insert(max_idx);
				topology_hits[max_idx] = 0;
			}
			for(auto top_sel_idx : regions_selected)
			{
				if(find(selected_regions.begin(), selected_regions.end(), top_sel_idx) == selected_regions.end())
				{
					selected_regions = regions_selected;
					switch_coding_regions(regions_selected);
					break;
				}
			}
			/* reset tracking counters */
			topology_hits.assign(topology_hits.size(), 0);
			coding_region_counter = 0;
		}
		// Use idle banks and current reads to prepare region switches
		dynamic_encoder->fill_data(data_banks, parity_banks, reqs_scheduled);
		
		// Check we can swap an encoded regions
		dynamic_encoder->replace_regions(*recoder_unit);
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
    void init_coding_regions(set<unsigned int>& regions_selected)
	{
		// Grab active topologies
		set<ParityBankTopology> new_tops;
		for(auto top_idx : regions_selected)
			new_tops.insert(topologies[top_idx]);
		size_t n_parity_banks {topologies[0].n_parity_banks};
		for (int b {0}; b < n_parity_banks; b++) 
		{
			parity_banks[b].xor_regions.clear();
			parity_banks[b].xor_regions.resize(new_tops.size());
		}
		for(auto top_idx : regions_selected)
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
		recoder_unit->init(new_tops); 
	}

    void switch_coding_regions(set<unsigned int>& regions_selected)
	{
		// Grab active topologies
		topology_switches++;
		dynamic_encoder->switch_regions(channel->spec, regions_selected);
    }

};

}

#endif /* AccessScheduler.h */



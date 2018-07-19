#ifndef __ACCESS_SCHEDULER_H
#define __ACCESS_SCHEDULER_H

#include "Coding.h"
#include "Request.h"

namespace coding
{

template <typename T>
class AccessScheduler
{

using ParityBank = coding::ParityBank<T>;
using XorCodedRegions = coding::XorCodedRegions<T>;
using MemoryRegion = coding::MemoryRegion<T>;
using CodeStatus = typename coding::CodeStatusMap<T>::Status;

public:
	AccessScheduler(coding::BankQueue* readq,
					coding::BankQueue* writeq,
					coding::CodeStatusMap<T>* code_status,
					list<Request>* pending,
					int parity_bank_latency,
					T* spec
					)
	{
		this->readq = readq;
		this->writeq = writeq;
		this->code_status = code_status;
		this->pending = pending;
		this->parity_bank_latency = parity_bank_latency;
		this->spec = spec;
	}

	~AccessScheduler() {}

    void read_pattern_builder(vector<ParityBank>&  parity_banks, long int cur_clock)
    {
		this->clk = cur_clock;
        for (ParityBank& bank : parity_banks)
            /* match queued reads to pending reads */
            if (!bank.busy())
                schedule_queued_read_for_parity_bank(bank);
    }

    void write_pattern_builder(vector<ParityBank>& parity_banks, long int cur_clock)
    {
		this->clk = cur_clock;
        for (ParityBank& bank : parity_banks)
            /* find queued writes to serve with parity banks instead
             * of main memory */
            if (!bank.busy())
                schedule_queued_write_for_parity_bank(bank);
    }

private:
int num_banks = 8; // Number of data banks. TODO: Make dynamic
long int clk = 0;
int parity_bank_latency = 11;
T* spec;
coding::BankQueue* readq;
coding::BankQueue* writeq;
coding::CodeStatusMap<T> * code_status;
list<Request>* pending;

    void schedule_queued_read_for_parity_bank(ParityBank& bank)
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
            {can_schedule_queued_read_for_parity_bank(read, bank)};
            bool schedulable {schedulable_result.first};
            long depart {schedulable_result.second};
            if (schedulable) {
                bank.lock();
                schedule_served_request(read, depart);
                remove_read_from_readq(read);
                break;
            }
        }
    }

    pair<bool, long> can_schedule_queued_read_for_parity_bank(Request& read,
            ParityBank& bank)
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
        long depart {clk + parity_bank_latency};
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

    void schedule_queued_write_for_parity_bank(ParityBank& bank)
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
				unsigned long serve_time = clk + parity_bank_latency;
				schedule_served_request(*write_it, serve_time);
				const auto row_index {coding::request_to_row_index(spec, *write_it)};
				writeq->queues[bank_idx].erase(write_it);
				code_status->set(row_index, CodeStatus::FreshParity, serve_time);
				return;
			}
		}
    }

	void remove_read_from_readq(const Request& req)
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

    void schedule_served_request(Request& req, const long& depart)
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

};

}

#endif

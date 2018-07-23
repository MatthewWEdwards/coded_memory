#ifndef __CONTROLLER_H
#define __CONTROLLER_H

#include <cassert>
#include <cstdio>
#include <deque>
#include <fstream>
#include <list>
#include <string>
#include <vector>

#include "Config.h"
#include "DRAM.h"
#include "Refresh.h"
#include "Request.h"
#include "Scheduler.h"
#include "Statistics.h"

#include "ALDRAM.h"
#include "SALP.h"
#include "TLDRAM.h"

#include "Coding.h"
#include "AccessScheduler.h"
#include "Debug.h"
#include <algorithm>
#include <utility>

using namespace std;

namespace ramulator
{

template <typename T>
class Controller
{
protected:
    // For counting bandwidth
    ScalarStat read_transaction_bytes;
    ScalarStat write_transaction_bytes;

    ScalarStat row_hits;
    ScalarStat row_misses;
    ScalarStat row_conflicts;
    VectorStat read_row_hits;
    VectorStat read_row_misses;
    VectorStat read_row_conflicts;
    VectorStat write_row_hits;
    VectorStat write_row_misses;
    VectorStat write_row_conflicts;

    ScalarStat read_latency_avg;
    ScalarStat read_latency_sum;

    ScalarStat req_queue_length_avg;
    ScalarStat req_queue_length_sum;
    ScalarStat read_req_queue_length_avg;
    ScalarStat read_req_queue_length_sum;
    ScalarStat write_req_queue_length_avg;
    ScalarStat write_req_queue_length_sum;

#ifndef INTEGRATED_WITH_GEM5
    VectorStat record_read_hits;
    VectorStat record_read_misses;
    VectorStat record_read_conflicts;
    VectorStat record_write_hits;
    VectorStat record_write_misses;
    VectorStat record_write_conflicts;
#endif

public:
    /* Member Variables */
    long clk = 0;
	unsigned int num_banks = 8; // Default is 8, altered in constructor 
    DRAM<T>* channel;

    Scheduler<T>* scheduler;  // determines the highest priority request whose commands will be issued
    RowPolicy<T>* rowpolicy;  // determines the row-policy (e.g., closed-row vs. open-row)
    RowTable<T>* rowtable;  // tracks metadata about rows (e.g., which are open and for how long)
    Refresh<T>* refresh;
	coding::AccessScheduler<T> * access_scheduler;
	unsigned int memory_coding;

//===Request Holders==============================================================================//
    coding::BankQueue readq; 
    coding::BankQueue writeq;
    coding::BankQueue otherq;
    list<Request> pending;  // read requests that are about to receive data from DRAM
    bool write_mode = false;  // whether write requests should be prioritized over reads

	vector<coding::DataBank> data_banks;

//===Debug Variables==============================================================================//
    /* Command trace for DRAMPower 3.1 */
    string cmd_trace_prefix = "cmd-trace-";
    vector<ofstream> cmd_trace_files;
    bool record_cmd_trace = false;
    /* Commands to stdout */
    bool print_cmd_trace = false;
    bool print_queues = false;

//===Constructor==================================================================================//
    Controller(const Config& configs, DRAM<T>* channel) :
        channel(channel),
        scheduler(new Scheduler<T>(this)),
        rowpolicy(new RowPolicy<T>(this)),
        rowtable(new RowTable<T>(this)),
        //TODO: Create a seperate rowtable for parity banks? Remove RowTable altogether?
        refresh(new Refresh<T>(this)),
        cmd_trace_files(channel->children.size())
    {
		num_banks = channel->spec->org_entry.count[static_cast<int>(T::Level::Bank)];
		for(unsigned int bank = 0; bank < num_banks; bank++)
			data_banks.push_back(coding::DataBank(bank));
		readq = coding::BankQueue(channel->spec->org_entry.count[static_cast<int>(T::Level::Bank)]);
		writeq = coding::BankQueue(channel->spec->org_entry.count[static_cast<int>(T::Level::Bank)]);
        otherq = coding::BankQueue(1);

        record_cmd_trace = configs.record_cmd_trace();
        print_cmd_trace = configs.print_cmd_trace();
        print_queues = configs.print_queues();
        if (record_cmd_trace) {
            if (configs["cmd_trace_prefix"] != "") {
                cmd_trace_prefix = configs["cmd_trace_prefix"];
            }
            string prefix = cmd_trace_prefix + "chan-" + to_string(channel->id) + "-rank-";
            string suffix = ".cmdtrace";
            for (unsigned int i = 0; i < channel->children.size(); i++)
                cmd_trace_files[i].open(prefix + to_string(i) + suffix);
        }

		// Prepare access scheduler
		memory_coding = configs.get_memory_coding();
        if(memory_coding) {
			access_scheduler = new coding::AccessScheduler<T>(memory_coding, 
				configs.get_alpha(), channel);
        }

//===Statistics===================================================================================//
        row_hits
        .name("row_hits_channel_"+to_string(channel->id) + "_core")
        .desc("Number of row hits per channel per core")
        .precision(0)
        ;
        row_misses
        .name("row_misses_channel_"+to_string(channel->id) + "_core")
        .desc("Number of row misses per channel per core")
        .precision(0)
        ;
        row_conflicts
        .name("row_conflicts_channel_"+to_string(channel->id) + "_core")
        .desc("Number of row conflicts per channel per core")
        .precision(0)
        ;

        read_row_hits
        .init(configs.get_core_num())
        .name("read_row_hits_channel_"+to_string(channel->id) + "_core")
        .desc("Number of row hits for read requests per channel per core")
        .precision(0)
        ;
        read_row_misses
        .init(configs.get_core_num())
        .name("read_row_misses_channel_"+to_string(channel->id) + "_core")
        .desc("Number of row misses for read requests per channel per core")
        .precision(0)
        ;
        read_row_conflicts
        .init(configs.get_core_num())
        .name("read_row_conflicts_channel_"+to_string(channel->id) + "_core")
        .desc("Number of row conflicts for read requests per channel per core")
        .precision(0)
        ;

        write_row_hits
        .init(configs.get_core_num())
        .name("write_row_hits_channel_"+to_string(channel->id) + "_core")
        .desc("Number of row hits for write requests per channel per core")
        .precision(0)
        ;
        write_row_misses
        .init(configs.get_core_num())
        .name("write_row_misses_channel_"+to_string(channel->id) + "_core")
        .desc("Number of row misses for write requests per channel per core")
        .precision(0)
        ;
        write_row_conflicts
        .init(configs.get_core_num())
        .name("write_row_conflicts_channel_"+to_string(channel->id) + "_core")
        .desc("Number of row conflicts for write requests per channel per core")
        .precision(0)
        ;

        read_transaction_bytes
        .name("read_transaction_bytes_"+to_string(channel->id))
        .desc("The total byte of read transaction per channel")
        .precision(0)
        ;
        write_transaction_bytes
        .name("write_transaction_bytes_"+to_string(channel->id))
        .desc("The total byte of write transaction per channel")
        .precision(0)
        ;

        read_latency_sum
        .name("read_latency_sum_"+to_string(channel->id))
        .desc("The memory latency cycles (in memory time domain) sum for all read requests in this channel")
        .precision(0)
        ;
        read_latency_avg
        .name("read_latency_avg_"+to_string(channel->id))
        .desc("The average memory latency cycles (in memory time domain) per request for all read requests in this channel")
        .precision(6)
        ;

        req_queue_length_sum
        .name("req_queue_length_sum_"+to_string(channel->id))
        .desc("Sum of read and write queue length per memory cycle per channel.")
        .precision(0)
        ;
        req_queue_length_avg
        .name("req_queue_length_avg_"+to_string(channel->id))
        .desc("Average of read and write queue length per memory cycle per channel.")
        .precision(6)
        ;

        read_req_queue_length_sum
        .name("read_req_queue_length_sum_"+to_string(channel->id))
        .desc("Read queue length sum per memory cycle per channel.")
        .precision(0)
        ;
        read_req_queue_length_avg
        .name("read_req_queue_length_avg_"+to_string(channel->id))
        .desc("Read queue length average per memory cycle per channel.")
        .precision(6)
        ;

        write_req_queue_length_sum
        .name("write_req_queue_length_sum_"+to_string(channel->id))
        .desc("Write queue length sum per memory cycle per channel.")
        .precision(0)
        ;
        write_req_queue_length_avg
        .name("write_req_queue_length_avg_"+to_string(channel->id))
        .desc("Write queue length average per memory cycle per channel.")
        .precision(6)
        ;

#ifndef INTEGRATED_WITH_GEM5
        record_read_hits
        .init(configs.get_core_num())
        .name("record_read_hits")
        .desc("record read hit count for this core when it reaches request limit or to the end")
        ;

        record_read_misses
        .init(configs.get_core_num())
        .name("record_read_misses")
        .desc("record_read_miss count for this core when it reaches request limit or to the end")
        ;

        record_read_conflicts
        .init(configs.get_core_num())
        .name("record_read_conflicts")
        .desc("record read conflict count for this core when it reaches request limit or to the end")
        ;

        record_write_hits
        .init(configs.get_core_num())
        .name("record_write_hits")
        .desc("record write hit count for this core when it reaches request limit or to the end")
        ;

        record_write_misses
        .init(configs.get_core_num())
        .name("record_write_misses")
        .desc("record write miss count for this core when it reaches request limit or to the end")
        ;

        record_write_conflicts
        .init(configs.get_core_num())
        .name("record_write_conflicts")
        .desc("record write conflict for this core when it reaches request limit or to the end")
        ;
#endif
    }

    ~Controller() {
        delete scheduler;
        delete rowpolicy;
        delete rowtable;
        delete channel;
        delete refresh;
		delete access_scheduler;
        for (auto& file : cmd_trace_files)
            file.close();
        cmd_trace_files.clear();
    }

    void finish(long read_req, long dram_cycles) {
        read_latency_avg = read_latency_sum.value() / read_req;
        req_queue_length_avg = req_queue_length_sum.value() / dram_cycles;
        read_req_queue_length_avg = read_req_queue_length_sum.value() / dram_cycles;
        write_req_queue_length_avg = write_req_queue_length_sum.value() / dram_cycles;
        // call finish function of each channel
        channel->finish(dram_cycles);
    }

    coding::BankQueue& get_queue(Request::Type type)
    {
        switch (int(type)) {
			case int(Request::Type::READ): return readq;
			case int(Request::Type::WRITE): return writeq;
			default: return otherq;
        }
    }

    bool enqueue(Request& req)
    {
        coding::BankQueue& queue = get_queue(req.type);
		unsigned int bank = req.addr_vec[static_cast<int>(T::Level::Bank)];
		if(req.type == Request::Type::REFRESH)
			bank = 0;
        if (queue.max == queue.queues[bank].size())
            return false;

        req.arrive = clk;
        queue.queues[bank].push_back(req);
        // shortcut for read requests, if a write to same addr exists
        // necessary for coherence
        if (req.type == Request::Type::READ && find_if(writeq.queues[bank].begin(), writeq.queues[bank].end(),
        [req](Request& wreq) {
        return req.addr == wreq.addr;
    }) != writeq.queues[bank].end()) {
			req.depart = clk + 1;
			pending.push_back(req);
			readq.queues[bank].pop_back();
        }
        return true;
    }

//===Controller==========================================================================//
	//TODO Should the controller handle reads AND writes in a single memory tick?
    void tick()
    {
        clk++;
        req_queue_length_sum += readq.size() + writeq.size() + pending.size();
        read_req_queue_length_sum += readq.size() + pending.size();
        write_req_queue_length_sum += writeq.size();

        /*** 1. Serve completed reads ***/
        if (pending.size()) {
            for(auto req = pending.begin(); req->depart <= clk; req = pending.begin()){
                if (!req->bypass_dram &&
                        req->depart - req->arrive > 1) { // this request really accessed a row
                    read_latency_sum += req->depart - req->arrive;
                    channel->update_serving_requests(
                        req->addr_vec.data(), -1, clk);
                }
                req->callback(*req);
                pending.pop_front(); 
				if(!pending.size())
					break;
            }
        }

        /*** 2. Refresh scheduler ***/
        refresh->tick_ref();

		/*** 2.1 Refresh banks ***/
		for(auto bank = data_banks.begin(); bank != data_banks.end(); bank++)
			bank->tick();

		/*** 2.3 Print Bank Queue Traces ***/
        if(print_queues)
            Debug::print_bank_queues(clk, num_banks, readq, writeq, pending);

        /*** 3. Should we schedule writes? ***/
        if (!write_mode) {
            // yes -- write queue is almost full or read queue is empty
            if (writeq.size() >= int(0.8 * writeq.max) || readq.size() == 0 )
                write_mode = true;
        }
        else {
            // no -- write queue is almost empty and read queue is not empty
            if (writeq.size() <= int(0.2 * writeq.max) && readq.size() != 0)
                write_mode = false;
        }

        /*** 4. Find the best command to schedule, if any ***/
        coding::BankQueue* queue = !write_mode ? &readq : &writeq;
        if (otherq.size())
            queue = &otherq;  // "other" requests are rare, so we give them precedence over reads/writes

		/*** 5. Get requests***/ 
		list<Request> reqs_scheduled;
        if(memory_coding) 
		{
			if(queue == &otherq){
				reqs_scheduled.push_back(queue->queues[0].front());
				queue->queues[0].pop_front();
			}else{
				access_scheduler->tick(clk, &readq, &writeq, &data_banks, reqs_scheduled, write_mode); // TODO: mode messy
			}
		}else{
			// Grab requests from data banks
			if(queue == &otherq){
				reqs_scheduled.push_back(queue->queues[0].front());
				queue->queues[0].pop_front();
			}else{
				for(unsigned int bank_queue_idx = 0; bank_queue_idx < num_banks; bank_queue_idx++)
				{
					if(queue->queues[bank_queue_idx].size() && data_banks[bank_queue_idx].is_free())
					{
						auto req = queue->queues[bank_queue_idx].begin();
						if(req == queue->queues[bank_queue_idx].end())
							continue;
						data_banks[bank_queue_idx].read();
						reqs_scheduled.push_back(*req);
					}
				}
			}
        }

		/*** 6. Schedule Requests ***/
        if (!reqs_scheduled.size())
		{
            // we couldn't find a command to schedule -- let's try to be speculative
            auto cmd = T::Command::PRE;
            vector<int> victim = rowpolicy->get_victim(cmd);
            if (!victim.empty()){
                issue_cmd(cmd, victim);
            }
            return;  // nothing more to be done this cycle
        }

		for(auto req = reqs_scheduled.begin(); req != reqs_scheduled.end(); req++) {
			req->is_first_command = false;
			int coreid = req->coreid;
			if (req->type == Request::Type::READ || req->type == Request::Type::WRITE) {
				channel->update_serving_requests(req->addr_vec.data(), 1, clk);
			}
			int tx = (channel->spec->prefetch_size * channel->spec->channel_width / 8);
			if (req->type == Request::Type::READ) {
				if (is_row_hit(req)) {
					++read_row_hits[coreid];
					++row_hits;
				} else if (is_row_open(req)) {
					++read_row_conflicts[coreid];
					++row_conflicts;
				} else {
					++read_row_misses[coreid];
					++row_misses;
				}
				read_transaction_bytes += tx;
			} else if (req->type == Request::Type::WRITE) {
				if (is_row_hit(req)) {
					++write_row_hits[coreid];
					++row_hits;
				} else if (is_row_open(req)) {
					++write_row_conflicts[coreid];
					++row_conflicts;
				} else {
					++write_row_misses[coreid];
					++row_misses;
				}
				write_transaction_bytes += tx;
			}
			if(memory_coding) 
			{
				access_scheduler->coding_region_hit(*req);
			}

			// issue command on behalf of request
			auto cmd = get_first_cmd(req);
			issue_cmd(cmd, get_addr_vec(cmd, req)); 

			// check whether this is the last command (which finishes the request)
			if (cmd != channel->spec->translate[int(req->type)]) {
				continue;
			}

			// set a future completion time for read requests
			if (req->type == Request::Type::READ) {
				req->depart = clk + channel->spec->read_latency;
				pending.push_back(*req);
			}

			if (req->type == Request::Type::WRITE) {
				channel->update_serving_requests(req->addr_vec.data(), -1, clk);
			}

			// Find matching req, erase
			int req_bank = req->addr_vec[static_cast<int>(T::Level::Bank)];
			auto erase_req = queue->queues[req_bank].begin(); 
			while(true)
			{
				if(erase_req == queue->queues[req_bank].end())
					assert(false);
				if(*req == *erase_req)
				{
					queue->queues[req_bank].erase(erase_req);
					break;
				}
				erase_req++;
			}
		}

		/*** 7. Recode using idle banks ***/
		if(memory_coding)
		{
			//TODO Pass banks instead of bank busy flags
			unsigned int bank_busy_flags = 0;
			for(unsigned int data_bank_idx = 0; data_bank_idx < num_banks; data_bank_idx++)
				bank_busy_flags |= (data_banks[data_bank_idx].is_free() << data_bank_idx);
			access_scheduler->recoding_controller(bank_busy_flags);
		}
    }

    bool is_ready(list<Request>::iterator req)
    {
		return true; //FIXME: Workaround
        typename T::Command cmd = get_first_cmd(req);
        return channel->check(cmd, req->addr_vec.data(), clk);
    }

    bool is_ready(typename T::Command cmd, const vector<int>& addr_vec)
    {
		return true; //FIXME: Workaround
        return channel->check(cmd, addr_vec.data(), clk);
    }

    bool is_row_hit(list<Request>::iterator req)
    {
        // cmd must be decided by the request type, not the first cmd
        typename T::Command cmd = channel->spec->translate[int(req->type)];
        return channel->check_row_hit(cmd, req->addr_vec.data());
    }

    bool is_row_hit(typename T::Command cmd, const vector<int>& addr_vec)
    {
        return channel->check_row_hit(cmd, addr_vec.data());
    }

    bool is_row_open(list<Request>::iterator req)
    {
        // cmd must be decided by the request type, not the first cmd
        typename T::Command cmd = channel->spec->translate[int(req->type)];
        return channel->check_row_open(cmd, req->addr_vec.data());
    }

    bool is_row_open(typename T::Command cmd, const vector<int>& addr_vec)
    {
        return channel->check_row_open(cmd, addr_vec.data());
    }

    void update_temp(ALDRAM::Temp current_temperature)
    {
    }

    // For telling whether this channel is busying in processing read or write
    bool is_active() {
        return (channel->cur_serving_requests > 0);
    }

    // For telling whether this channel is under refresh
    bool is_refresh() {
        return clk <= channel->end_of_refreshing;
    }

    void record_core(int coreid) {
#ifndef INTEGRATED_WITH_GEM5
        record_read_hits[coreid] = read_row_hits[coreid];
        record_read_misses[coreid] = read_row_misses[coreid];
        record_read_conflicts[coreid] = read_row_conflicts[coreid];
        record_write_hits[coreid] = write_row_hits[coreid];
        record_write_misses[coreid] = write_row_misses[coreid];
        record_write_conflicts[coreid] = write_row_conflicts[coreid];
#endif
    }

private:
    typename T::Command get_first_cmd(list<Request>::iterator req)
    {
        typename T::Command cmd = channel->spec->translate[int(req->type)];
        return channel->decode(cmd, req->addr_vec.data());
    }

    void issue_cmd(typename T::Command cmd, const vector<int>& addr_vec)
    {
        assert(is_ready(cmd, addr_vec));
        channel->update(cmd, addr_vec.data(), clk);
        rowtable->update(cmd, addr_vec, clk);
        if (record_cmd_trace) {
            // select rank
            auto& file = cmd_trace_files[addr_vec[1]];
            string& cmd_name = channel->spec->command_name[int(cmd)];
            file<<clk<<','<<cmd_name;
            // TODO bad coding here
            if (cmd_name == "PREA" || cmd_name == "REF")
                file<<endl;
            else {
                int bank_id = addr_vec[int(T::Level::Bank)];
                if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                    bank_id += addr_vec[int(T::Level::Bank) - 1] * num_banks;
                file<<','<<bank_id<<endl;
            }
        }
        if (print_cmd_trace) {
            printf("%5s %10ld:", channel->spec->command_name[int(cmd)].c_str(), clk);
            for (int lev = 0; lev < int(T::Level::MAX); lev++)
                printf(" %5d", addr_vec[lev]);
            printf("\n");
        }
    }
    vector<int> get_addr_vec(typename T::Command cmd, list<Request>::iterator req) {
        return req->addr_vec;
    }
};

template <>
vector<int> Controller<SALP>::get_addr_vec(
    SALP::Command cmd, list<Request>::iterator req);

template <>
bool Controller<SALP>::is_ready(list<Request>::iterator req);

template <>
void Controller<ALDRAM>::update_temp(ALDRAM::Temp current_temperature);

template <>
void Controller<TLDRAM>::tick();

} /*namespace ramulator*/

#endif /*__CONTROLLER_H*/

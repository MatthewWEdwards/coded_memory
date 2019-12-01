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
#include "ROB.cpp"
#include "Bank.h"
#include "DynamicEncoder.h"

#include "ALDRAM.h"
#include "SALP.h"
#include "TLDRAM.h"
#include <set>

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
	int num_banks = 8;
    DRAM<T>* channel;

    Scheduler<T>* scheduler;  // determines the highest priority request whose commands will be issued
    RowPolicy<T>* rowpolicy;  // determines the row-policy (e.g., closed-row vs. open-row)
    RowTable<T>* rowtable;  // tracks metadata about rows (e.g., which are open and for how long)
    Refresh<T>* refresh;
	ROB<T> rob;
	DynamicEncoder<T> * dynamic_encoder;

    BankQueue readq = BankQueue(channel->spec->org_entry.count[static_cast<int>(T::Level::Bank)]);  // queue for read requests
    BankQueue writeq = BankQueue(channel->spec->org_entry.count[static_cast<int>(T::Level::Bank)]);  // queue for write requests
    BankQueue otherq = BankQueue(channel->spec->org_entry.count[static_cast<int>(T::Level::Bank)]); // queue for all "other" requests (e.g., refresh)

    deque<Request> pending;  // read requests that are about to receive data from DRAM
    bool write_mode = false;  // whether write requests should be prioritized over reads
    //long refreshed = 0;  // last time refresh requests were generated

	/* Dynamic Encoder variables */
	double alpha = .03; // TODO: Config
	double region_length = .01; // TODO Config
	unsigned int num_regions = 1 / region_length;  // TODO: Round up
	unsigned int num_regions_to_select = alpha / region_length; // TODO Round down
	unsigned int num_rows_per_region = channel->spec->org_entry.count[static_cast<int>(T::Level::Row)] * region_length;
	set<unsigned int> active_regions;
	set<unsigned int> selected_regions;
	vector<unsigned int> region_hits = vector<unsigned int>(num_regions, 0);
	unsigned int coding_region_counter = 0;
	unsigned int coding_region_reschedule_ticks = 1e3;

    /* Command trace for DRAMPower 3.1 */
    string cmd_trace_prefix = "cmd-trace-";
    vector<ofstream> cmd_trace_files;
    bool record_cmd_trace = false;
    /* Commands to stdout */
    bool print_cmd_trace = false;
	bool print_bank_queues_flag = false;

    /* Constructor */
    Controller(const Config& configs, DRAM<T>* channel) :
        channel(channel),
        scheduler(new Scheduler<T>(this)),
        rowpolicy(new RowPolicy<T>(this)),
        rowtable(new RowTable<T>(this)),
        refresh(new Refresh<T>(this)),
		rob(ROB<T>(configs)),
        cmd_trace_files(channel->children.size())
    {
		print_bank_queues_flag = configs.print_bank_queues();
        record_cmd_trace = configs.record_cmd_trace();
        print_cmd_trace = configs.print_cmd_trace();
        if (record_cmd_trace){
            if (configs["cmd_trace_prefix"] != "") {
              cmd_trace_prefix = configs["cmd_trace_prefix"];
            }
            string prefix = cmd_trace_prefix + "chan-" + to_string(channel->id) + "-rank-";
            string suffix = ".cmdtrace";
            for (unsigned int i = 0; i < channel->children.size(); i++)
                cmd_trace_files[i].open(prefix + to_string(i) + suffix);
        }

		/* Dynamic Encoding Preparation */
		for(unsigned int select = 0; select < num_regions_to_select; select++)
			active_regions.insert(select);
		selected_regions = active_regions;
		dynamic_encoder = new DynamicEncoder<T>(active_regions, alpha, region_length, num_rows_per_region);
		rob.alpha = alpha;
		rob.code_region_length = region_length;
		rob.num_rows_per_region = num_rows_per_region;
		rob.active_regions = active_regions;
		

        // regStats

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

    ~Controller(){
        delete scheduler;
        delete rowpolicy;
        delete rowtable;
        delete channel;
        delete refresh;
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

    /* Member Functions */
    BankQueue& get_queue(Request::Type type)
    {
        switch (int(type)) {
            case int(Request::Type::READ): return readq;
            case int(Request::Type::WRITE): return writeq;
            default: return otherq;
        }
    }

    bool enqueue(Request& req)
    {
        BankQueue& queue = get_queue(req.type);
        int bank = req.addr_vec[static_cast<int>(T::Level::Bank)];
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


    void tick()
    {
        clk++;
        req_queue_length_sum += readq.size() + writeq.size() + pending.size();
        read_req_queue_length_sum += readq.size() + pending.size();
        write_req_queue_length_sum += writeq.size();

        /*** 1. Serve completed reads ***/
        if (pending.size()) {
            for(auto req = pending.begin(); req->depart <= clk; req = pending.begin()) {
                if (req->isRobValid && req->depart - req->arrive > 1) { // this request really accessed a row TODO Why req->isRobValid here???
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

        /*** 2. Refresh scheduler and banks***/
        refresh->tick_ref();
		rob.bank_arch.refresh();
		rob.parity_arch.refresh();

        /*** 3. Should we schedule writes? ***/
        if (!write_mode) {
            // yes -- write queue is almost full or read queue is empty
            if (writeq.size() >= int(0.8 * writeq.max) || readq.size() == 0)
                write_mode = true;
        }
        else {
            // no -- write queue is almost empty and read queue is not empty
            if (writeq.size() <= int(0.2 * writeq.max) && readq.size() != 0)
                write_mode = false;
        }

        /*** 4. Find the best command to schedule, if any ***/
		/*** Modification: try to schedule each request ***/
		/*** FIXME: Only read or writing is done in a single memory cycle ***/
        BankQueue* queue = !write_mode ? &readq : &writeq;
		if (otherq.size())
		{
			queue = &otherq;  // "other" requests are rare, so we give them precedence over reads/writes
		}
			
		if(print_bank_queues_flag)
			print_bank_queues();
		for(auto bank_queue = queue->queues.begin(); bank_queue != queue->queues.end(); bank_queue++){
			auto req = bank_queue->begin();
			while(req != bank_queue->end())
			{
				rob.Writeback();
				bool read_from_rob = false;
				if(!(req->type == Request::Type::REFRESH) && !rob.is_full()) // FIXME: Shouldn't be necessary
				{
					read_from_rob = true;
					bool bank_read = req->type == Request::Type::READ ? rob.Read(clk, req) : rob.Write(clk, req);
					if(!bank_read){
						req++;
						continue; // Banks busy
					}
					if(req->isRobValid){ //FIXME: This isn't accurate for write requests
						req->depart = clk + 1;//channel->spec->read_latency; // FIXME probably should be clk + 1
						channel->update_serving_requests(req->addr_vec.data(), 1, clk); 
						pending.push_back(*req);
						req = bank_queue->erase(req);
						continue;
					}
				}
				if(!(req->type == Request::Type::REFRESH) && !read_from_rob){
					int bank_num = req->addr_vec[static_cast<int>(T::Level::Bank)];
					if(rob.bank_arch.is_free(bank_num)){ 
						rob.bank_arch.read(bank_num); // TODO: Refactor into rob.cpp
					}else{
						req++;
						continue;
					}
				}

				/* record hit */
				if (req->type == Request::Type::READ || req->type == Request::Type::WRITE) {
					unsigned int region = req->addr_vec[static_cast<int>(T::Level::Row)] / num_rows_per_region;
					region_hits[region]++;
				}


				if (req == bank_queue->end() || !is_ready(req)) {
					// we couldn't find a command to schedule -- let's try to be speculative
					auto cmd = T::Command::PRE;
					vector<int> victim = rowpolicy->get_victim(cmd);
					if (!victim.empty()){
						issue_cmd(cmd, victim);
					}
					break;
				}

		

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

				// issue command on behalf of request
				auto cmd = get_first_cmd(req);
				issue_cmd(cmd, get_addr_vec(cmd, req));

				// check whether this is the last command (which finishes the request)
				if (cmd != channel->spec->translate[int(req->type)])
					break;

				// set a future completion time for read requests
				if (req->type == Request::Type::READ) {
					req->depart = clk + channel->spec->read_latency; //FIXME should be read_latency
					pending.push_back(*req);
				}

				if (req->type == Request::Type::WRITE) {
					channel->update_serving_requests(req->addr_vec.data(), -1, clk);
				}

				// remove request from queue
				req = bank_queue->erase(req);
			}
		}
		/*** 5 Complete write requests and use idle banks to fill the ROB **/
		rob.issue_cmds();
		coding_region_controller(rob.bank_arch.banks);

    }

    void coding_region_controller(vector<Bank>& data_banks)
    {
        coding_region_counter++;
        if (coding_region_counter >= coding_region_reschedule_ticks) {
            /* find the coding regions with the most hits */
            set<unsigned int> regions_selected;
            for(unsigned int num_region = 0; num_region < num_regions_to_select; num_region++)
            {
                unsigned int max_idx = distance(region_hits.begin(), max_element(region_hits.begin(), region_hits.end()));
                if(region_hits[max_idx] == 0)
                    break;
                regions_selected.insert(max_idx);
                region_hits[max_idx] = 0;
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
            region_hits.assign(region_hits.size(), 0);
            coding_region_counter = 0;
        }
        // Use idle banks and current reads to prepare region switches
        dynamic_encoder->fill_data_using_banks(data_banks);
		dynamic_encoder->fill_data_using_rob(rob.robMap);

        // Check we can swap an encoded regions
        active_regions = dynamic_encoder->replace_regions();
		rob.active_regions = active_regions;
    }

    void switch_coding_regions(set<unsigned int>& regions_selected)
    {
        // Grab active topologies
        //topology_switches++; FIXME implement this stat recording
        dynamic_encoder->switch_regions(channel->spec, regions_selected);
    }



    bool is_ready(list<Request>::iterator req)
    {
		return true; //XXX: Workaround
        typename T::Command cmd = get_first_cmd(req);
        return channel->check(cmd, req->addr_vec.data(), clk);
    }

    bool is_ready(typename T::Command cmd, const vector<int>& addr_vec)
    {
		return true; //XXX: Workaround
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

    void print_bank_queues() {
        cout << "Pending Reads, CLK = " << clk << ",  size = " << pending.size() << endl;
        for(auto pending_read : pending) {
            cout << "\t\t{"; 
            for(auto num = pending_read.addr_vec.begin();
                    num != pending_read.addr_vec.end();
                    num++) {
                cout << *num; 
                if(num + 1 != pending_read.addr_vec.end())
                    cout << ", ";
            }
            cout << "}" << " Arrive = " << pending_read.arrive
                 << ", Depart = " << pending_read.depart 
                 << ", CoreID = " << pending_read.coreid << endl;
        }
        for(unsigned int bank = 0; bank < readq.queues.size(); bank++)
        {
            cout << "\tReadq" << ", bank = " << bank << ", size = " << readq.queues[bank].size() << endl;
            for(auto read_queue = readq.queues[bank].begin();
                    read_queue != readq.queues[bank].end();
                    read_queue++) {
                cout << "\t\t{"; 
                for(auto num = read_queue->addr_vec.begin();
                        num != read_queue->addr_vec.end();
                        num++) {
                    cout << *num; 
                    if(num + 1 != read_queue->addr_vec.end())
                        cout << ", ";
                }
                cout << "}" << " Arrive = " << read_queue->arrive
                     << ", Is Ready = " << is_ready(read_queue)
                     << ", CoreID = " << read_queue->coreid << endl;
            }

            cout << "\tWriteq" << ", bank = " << bank << ", size = " << writeq.queues[bank].size() << endl;
            for(auto write_queue = writeq.queues[bank].begin();
                    write_queue != writeq.queues[bank].end();
                    write_queue++) {
                cout << "\t\t{"; 
                for(auto num = write_queue->addr_vec.begin();
                        num != write_queue->addr_vec.end();
                        num++) {
                    cout << *num; 
                    if(num + 1 != write_queue->addr_vec.end())
                        cout << ", ";
                }
                cout << "}" << " Arrive = " << write_queue->arrive
                     << ", Is Ready = " << is_ready(write_queue)
                     << ", CoreID = " << write_queue->coreid << endl;
            }
        }
    }


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
        if (record_cmd_trace){
            // select rank
            auto& file = cmd_trace_files[addr_vec[1]];
            string& cmd_name = channel->spec->command_name[int(cmd)];
            file<<clk<<','<<cmd_name;
            // TODO bad coding here
            if (cmd_name == "PREA" || cmd_name == "REF")
                file<<endl;
            else{
                int bank_id = addr_vec[int(T::Level::Bank)];
		//std::cout << "bank id: "<<bank_id<<std::endl;
              
		 if (channel->spec->standard_name == "DDR4" || channel->spec->standard_name == "GDDR5")
                    bank_id += addr_vec[int(T::Level::Bank) - 1] * channel->spec->org_entry.count[int(T::Level::Bank)];
                file<<','<<bank_id<<endl;
            }
        }
        if (print_cmd_trace){
            printf("%5s %10ld:", channel->spec->command_name[int(cmd)].c_str(), clk);
            for (int lev = 0; lev < int(T::Level::MAX); lev++)
                printf(" %5d", addr_vec[lev]);
            printf("\n");
        }
    }
    vector<int> get_addr_vec(typename T::Command cmd, list<Request>::iterator req){
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

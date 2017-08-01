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

#define MEMORY_CODING
#define CODING_SCHEME 1

#ifdef MEMORY_CODING
#include "Coding.h"
#include <algorithm>
#include <utility>
#endif

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
    DRAM<T>* channel;

    Scheduler<T>* scheduler;  // determines the highest priority request whose commands will be issued
    RowPolicy<T>* rowpolicy;  // determines the row-policy (e.g., closed-row vs. open-row)
    RowTable<T>* rowtable;  // tracks metadata about rows (e.g., which are open and for how long)
    Refresh<T>* refresh;

    struct Queue {
        list<Request> q;
        unsigned int max = 32;
        unsigned int size() {return q.size();}
    };

    Queue readq;  // queue for read requests
    Queue writeq;  // queue for write requests
    Queue otherq;  // queue for all "other" requests (e.g., refresh)

    deque<Request> pending;  // read requests that are about to receive data from DRAM
    bool write_mode = false;  // whether write requests should be prioritized over reads
    //long refreshed = 0;  // last time refresh requests were generated

    /* Command trace for DRAMPower 3.1 */
    string cmd_trace_prefix = "cmd-trace-";
    vector<ofstream> cmd_trace_files;
    bool record_cmd_trace = false;
    /* Commands to stdout */
    bool print_cmd_trace = false;

#ifdef MEMORY_CODING
    vector<coding::ParityBank<T>> parity_banks;
    long parity_bank_latency;
#endif

    /* Constructor */
    Controller(const Config& configs, DRAM<T>* channel) :
        channel(channel),
        scheduler(new Scheduler<T>(this)),
        rowpolicy(new RowPolicy<T>(this)),
        rowtable(new RowTable<T>(this)),
        refresh(new Refresh<T>(this)),
        cmd_trace_files(channel->children.size())
    {
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

#ifdef MEMORY_CODING
        // coding
        using XorCodedRegions = coding::XorCodedRegions<T>;

        /* for now, parity bank latency = main memory latency */
        parity_bank_latency = channel->spec->read_latency;

        static constexpr int rows {(1 << 15)/16};
        static constexpr int half_rows {1 << 14};
        coding::CodedRegion<T> top_regions[8] {{half_rows - rows, rows, 0},
                                               {half_rows - rows, rows, 1},
                                               {half_rows - rows, rows, 2},
                                               {half_rows - rows, rows, 3},
                                               {half_rows - rows, rows, 4},
                                               {half_rows - rows, rows, 5},
                                               {half_rows - rows, rows, 6},
                                               {half_rows - rows, rows, 7}};
        coding::CodedRegion<T> btm_regions[8] {{half_rows, rows, 0},
                                               {half_rows, rows, 1},
                                               {half_rows, rows, 2},
                                               {half_rows, rows, 3},
                                               {half_rows, rows, 4},
                                               {half_rows, rows, 5},
                                               {half_rows, rows, 6},
                                               {half_rows, rows, 7}};

#if CODING_SCHEME==1
        /* coding design I */
        vector<XorCodedRegions> bank1_xor {{{top_regions[0], top_regions[1]}},
                                           {{btm_regions[0], btm_regions[1]}}};
        parity_banks.push_back({bank1_xor, parity_bank_latency});

        vector<XorCodedRegions> bank2_xor {{{top_regions[2], top_regions[3]}},
                                           {{btm_regions[2], btm_regions[3]}}};
        parity_banks.push_back({bank2_xor, parity_bank_latency});

        vector<XorCodedRegions> bank3_xor {{{top_regions[0], top_regions[3]}},
                                           {{btm_regions[0], btm_regions[3]}}};
        parity_banks.push_back({bank3_xor, parity_bank_latency});

        vector<XorCodedRegions> bank4_xor {{{top_regions[1], top_regions[2]}},
                                           {{btm_regions[1], btm_regions[2]}}};
        parity_banks.push_back({bank4_xor, parity_bank_latency});

        vector<XorCodedRegions> bank5_xor {{{top_regions[1], top_regions[3]}},
                                           {{btm_regions[1], btm_regions[3]}}};
        parity_banks.push_back({bank5_xor, parity_bank_latency});

        vector<XorCodedRegions> bank6_xor {{{top_regions[0], top_regions[2]}},
                                           {{btm_regions[0], btm_regions[2]}}};
        parity_banks.push_back({bank6_xor, parity_bank_latency});

        vector<XorCodedRegions> bank7_xor {{{top_regions[4], top_regions[5]}},
                                           {{btm_regions[4], btm_regions[5]}}};
        parity_banks.push_back({bank7_xor, parity_bank_latency});

        vector<XorCodedRegions> bank8_xor {{{top_regions[6], top_regions[7]}},
                                           {{btm_regions[6], btm_regions[7]}}};
        parity_banks.push_back({bank8_xor, parity_bank_latency});

        vector<XorCodedRegions> bank9_xor {{{top_regions[4], top_regions[7]}},
                                           {{btm_regions[4], btm_regions[7]}}};
        parity_banks.push_back({bank9_xor, parity_bank_latency});

        vector<XorCodedRegions> bank10_xor {{{top_regions[5], top_regions[6]}},
                                            {{btm_regions[5], btm_regions[6]}}};
        parity_banks.push_back({bank10_xor, parity_bank_latency});

        vector<XorCodedRegions> bank11_xor {{{top_regions[5], top_regions[7]}},
                                            {{btm_regions[5], btm_regions[7]}}};
        parity_banks.push_back({bank11_xor, parity_bank_latency});

        vector<XorCodedRegions> bank12_xor {{{top_regions[4], top_regions[6]}},
                                            {{btm_regions[4], btm_regions[6]}}};
        parity_banks.push_back({bank12_xor, parity_bank_latency});
#endif

#endif

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
    Queue& get_queue(Request::Type type)
    {
        switch (int(type)) {
            case int(Request::Type::READ): return readq;
            case int(Request::Type::WRITE): return writeq;
            default: return otherq;
        }
    }

    bool enqueue(Request& req)
    {
        Queue& queue = get_queue(req.type);
        if (queue.max == queue.size())
            return false;

        req.arrive = clk;
        queue.q.push_back(req);
        // shortcut for read requests, if a write to same addr exists
        // necessary for coherence
        if (req.type == Request::Type::READ && find_if(writeq.q.begin(), writeq.q.end(),
                [req](Request& wreq){ return req.addr == wreq.addr;}) != writeq.q.end()){
            req.depart = clk + 1;
            pending.push_back(req);
            readq.q.pop_back();
        }
        return true;
    }

#ifdef MEMORY_CODING
        using ParityBank = coding::ParityBank<T>;

        void coding_scheduler()
        {
                for (ParityBank& bank : parity_banks) {
                        /* update internal state */
                        bank.tick();
                        /* match queued reads to pending reads */
                        if (!bank.busy())
                                schedule_queued_read_for_parity_bank(bank);
                }
        }

        void schedule_queued_read_for_parity_bank(ParityBank& bank)
        {
                /* select all queued reads that could be served by this parity
                 * bank */
                vector<reference_wrapper<Request>> candidate_reads;
                for (Request& req : readq.q)
                        if (bank.contains(req))
                                candidate_reads.push_back(req);
                /* try to schedule each candidate read */
                for (Request& read : candidate_reads) {
                        pair<bool, long> schedulable_result
                                {can_schedule_queued_read_for_parity_bank(read, bank)};
                        bool schedulable {schedulable_result.first};
                        long depart {schedulable_result.second};
                        if (schedulable) {
                                bank.lock_for_read();
                                schedule_served_read(read, depart);
                                remove_read_from_readq(read);
                                break;
                        }
                }
        }
        pair<bool, long> can_schedule_queued_read_for_parity_bank(Request& read,
                                                                  ParityBank& bank)
        {
                using CodedRegion = coding::CodedRegion<T>;
                using XorCodedRegions = coding::XorCodedRegions<T>;

                /* select all pending reads that could be used by this parity
                 * bank and were not previously scheduled by this function */
                vector<reference_wrapper<Request>> candidate_pending;
                for (Request& req : pending)
                        if (bank.contains(req) && req.hit_dram)
                                candidate_pending.push_back(req);
                /* get a list of all the XOR regions needed to complete
                 * the parity */
                const XorCodedRegions& xor_regions {bank.request_xor_regions(read)};
                const CodedRegion& read_region {xor_regions.request_region(read)};
                vector<reference_wrapper<const CodedRegion>> other_regions;
                for (const CodedRegion& region : xor_regions.regions)
                        if (region != read_region)
                                other_regions.push_back(region);
                /* find a pending read in the same row number for every
                 * other XOR region */
                long depart {clk + parity_bank_latency};
                for (const CodedRegion& other_region : other_regions) {
                        auto can_use {[xor_regions, read, other_region](Request& req)
                                      { return xor_regions.request_region(req) == other_region &&
                                               xor_regions.same_request_row_numbers(read, req); }};
                        vector<reference_wrapper<Request>>::iterator
                                row_pending {find_if(begin(candidate_pending),
                                                     end(candidate_pending),
                                                     can_use)};
                        if (row_pending != end(candidate_pending))
                                depart = max<long>(depart, row_pending->get().depart);
                        else
                                /* couldn't find a match for a region,
                                 * this read request can't be scheduled */
                                return {false, -1};
                }
                /* we were able to complete the parity, return
                 * max(pending request(s) time(s), parity bank read latency) */
                return {true, depart};
        }

        void schedule_served_read(Request& req, const long& depart)
        {
                req.hit_dram = false;
                req.depart = depart;
                pending.push_back(req);
        }

        void remove_read_from_readq(const Request& req)
        {
                for (auto read_it {std::begin(readq.q)};
                     read_it != std::end(readq.q); ++read_it) {
                        if (&(*read_it) == &req) { // FIXME: implement comparison?
                                readq.q.erase(read_it);
                                return;
                        }
                }
                assert(false);
        }
#endif

    void tick()
    {
        clk++;
        req_queue_length_sum += readq.size() + writeq.size() + pending.size();
        read_req_queue_length_sum += readq.size() + pending.size();
        write_req_queue_length_sum += writeq.size();

        /*** 1. Serve completed reads ***/
        if (pending.size()) {
            Request& req = pending[0];
            if (req.depart <= clk) {
                if (req.hit_dram &&
                    req.depart - req.arrive > 1) { // this request really accessed a row
                  read_latency_sum += req.depart - req.arrive;
                  channel->update_serving_requests(
                      req.addr_vec.data(), -1, clk);
                }
                req.callback(req);
                pending.pop_front();
            }
        }

        /*** 2. Refresh scheduler ***/
        refresh->tick_ref();

#ifdef MEMORY_CODING
        coding_scheduler();
#endif

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
        Queue* queue = !write_mode ? &readq : &writeq;
        if (otherq.size())
            queue = &otherq;  // "other" requests are rare, so we give them precedence over reads/writes

        auto req = scheduler->get_head(queue->q);
        if (req == queue->q.end() || !is_ready(req)) {
            // we couldn't find a command to schedule -- let's try to be speculative
            auto cmd = T::Command::PRE;
            vector<int> victim = rowpolicy->get_victim(cmd);
            if (!victim.empty()){
                issue_cmd(cmd, victim);
            }
            return;  // nothing more to be done this cycle
        }

        if (req->is_first_command) {
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
        }

        // issue command on behalf of request
        auto cmd = get_first_cmd(req);
        issue_cmd(cmd, get_addr_vec(cmd, req));

        // check whether this is the last command (which finishes the request)
        if (cmd != channel->spec->translate[int(req->type)])
            return;

        // set a future completion time for read requests
        if (req->type == Request::Type::READ) {
            req->depart = clk + channel->spec->read_latency;
            pending.push_back(*req);
        }

        if (req->type == Request::Type::WRITE) {
            channel->update_serving_requests(req->addr_vec.data(), -1, clk);
        }

        // remove request from queue
        queue->q.erase(req);
    }

    bool is_ready(list<Request>::iterator req)
    {
        typename T::Command cmd = get_first_cmd(req);
        return channel->check(cmd, req->addr_vec.data(), clk);
    }

    bool is_ready(typename T::Command cmd, const vector<int>& addr_vec)
    {
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

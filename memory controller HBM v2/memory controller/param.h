#pragma once
using namespace std;
#include <string>
#include <unordered_map>



/* Bitmap for which parity banks a data bank is coded in */
//int parity_bitmap[NUM_BANKS / 2][3] = {
//	{ 0, 1, 2 },
//	{ 0, 3, 4 },
//	{ 1, 3, 5 },
//	{ 2, 4, 5 } };
//
//int bank_bitmap[NUM_BANKS][3] = {
//	{ 1, 2, 3 },
//	{ 0, 2, 3 },
//	{ 0, 1, 3 },
//	{ 0, 1, 2 },
//	{ 5, 6, 7 },
//	{ 4, 6, 7 },
//	{ 4, 5, 7 },
//	{ 4, 5, 6 }
//};


/* GLOBALS */
//vector<beatLevelRequestInBank> bankReadQueue[NUM_BANKS]; //Queue of read requests for each bank
//vector<beatLevelRequestInBank> bankWriteQueue[NUM_BANKS]; //Queue of write requests for each bank
////vector<transLevelRequest> request_queue[NUM_TRACES];
//vector<beatLevelRequestInBank> overwritten_parity_rows; //Keep track which parities are busy due to writes
//vector<beatLevelRequestInBank> pending_parity_writes; //The data that is waiting to be recoded to the parity banks
////vector<transLevelRequest> core_queues[NUM_TRACES]; //These queues hold the requests from the cores
//int current_time = 0; //Current time in ns
//int TRACE_TO_SERVE = 0;
//long long int read_cr_word_latency = 0;
//long long int write_cr_word_latency = 0;
//long long int read_last_word_latency = 0;
//long long int write_last_word_latency = 0;
//long long int num_reads = 0;
//long long int num_writes = 0;
//long long int reads_served_from_write = 0;
//int num_idle_cycles = 0;
//int num_parity_conflicts = 0;
//int mem_stall;
//int parity_hit = 0;
//bool parity_stall[2][NUM_PARITY_BANKS];
//bool dataBankStatus[NUM_BANKS];
//int NUM_REQUESTS = 0; //Number of requests we have served
//unordered_map<int, beatLevelRequestInBank> previously_served_reads;
//unordered_map<int, beatLevelRequestInBank> previously_served_writes;
//int PASS = 0; // 2 passes must be made over the data and parity banks to complete recodingf
//

/* Keep track of which locations are currently coded */
//int region_hits[NUM_REGIONS]; //Holds the number of hits a region has had
//int coded_regions[NUM_ACTIVE_REGIONS];
//vector<int> previously_read[NUM_REGIONS];
//int highAddress = 0;
//int lowAddress = 0x7FFFFFFF;
//int region_size = 0;
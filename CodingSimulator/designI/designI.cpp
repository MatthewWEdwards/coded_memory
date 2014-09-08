#include "systemc.h"
#include <string>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream> 
#include <unordered_map>

using namespace std;


/* Some definitions to change simulation parameters */
#define NUM_BANKS 8
#define NUM_PARITY_BANKS 6 //Not currently used. Num parity banks per 4 data banks
#define NUM_TRACES 6
#define WR_QUEUE_BUILDUP 10
#define CORE_QUEUE_MAX 8
#define MAX_BANK_QUEUE_LENGTH 10
#define NUM_REGIONS 8
#define NUM_ACTIVE_REGIONS 3
#define BANK_FREE 0
#define BANK_BUSY 1
string TRACE_LOCATION;//("../traces/LTE/dsp_0_trace.txt");

/* Input Parameters */
int MEM_DELAY; 
int MAX_LOOKAHEAD;
int WRITE_REPAIR_TIME;

/* Struct for the input requests from the processors */
typedef struct transLevelRequest {

	int address;
	int priority;
	int length;
	int time;
	int queue_time;
	int core_number; //What core the request came from 
	bool read;

} transLevelRequest;

/* Struct for the queue request */
typedef struct beatLevelRequestInBank {

	int address;
	int priority;
	int time;
	int queue_time;
	int core_number;
	bool read;
	bool critical;
	bool last;
	int length;
	int numServed;
	bool inParity; //If the data was written to parity
	int parityNumber; //The number of the parity bank it was written to
	int orderNumber; //The order in which the data should be served
	int requestNumber; //The unique number of the request

} beatLevelRequestInBank;

/* Bitmap for which parity banks a data bank is coded in */
int parity_bitmap[NUM_BANKS/2][3] = {
	{0,1,2},
	{0,3,4},
	{1,3,5},
	{2,4,5} };

int bank_bitmap[NUM_BANKS][3] = {
	{1,2,3},
	{0,2,3},
	{0,1,3},
	{0,1,2},
	{5,6,7},
	{4,6,7},
	{4,5,7},
	{4,5,6}	
};


/* GLOBALS */
vector<beatLevelRequestInBank> bankReadQueue[NUM_BANKS]; //Queue of read requests for each bank
vector<beatLevelRequestInBank> bankWriteQueue[NUM_BANKS]; //Queue of write requests for each bank
vector<transLevelRequest> request_queue[NUM_TRACES]; 
vector<beatLevelRequestInBank> overwritten_parity_rows; //Keep track which parities are busy due to writes
vector<beatLevelRequestInBank> pending_parity_writes; //The data that is waiting to be recoded to the parity banks
vector<transLevelRequest> core_queues[NUM_TRACES]; //These queues hold the requests from the cores
int current_time = 0; //Current time in ns
int TRACE_TO_SERVE = 0;
long long int read_cr_word_latency = 0;
long long int write_cr_word_latency = 0;
long long int read_last_word_latency = 0;
long long int write_last_word_latency = 0;
long long int num_reads = 0;
long long int num_writes = 0;
long long int reads_served_from_write = 0;
int num_idle_cycles = 0;
int num_parity_conflicts = 0;
int mem_stall;
int parity_hit = 0;
bool parity_stall[2][NUM_PARITY_BANKS];
bool dataBankStatus[NUM_BANKS];
int NUM_REQUESTS = 0; //Number of requests we have served
unordered_map<int, beatLevelRequestInBank> previously_served_reads;
unordered_map<int, beatLevelRequestInBank> previously_served_writes;
int PASS = 0; // 2 passes must be made over the data and parity banks to complete recodingf


/* Keep track of which locations are currently coded */
int region_hits[NUM_REGIONS]; //Holds the number of hits a region has had
int coded_regions[NUM_ACTIVE_REGIONS];
vector<int> previously_read[NUM_REGIONS];
int highAddress = 0;
int lowAddress = 0x7FFFFFFF;
int region_size = 0;
// Defining functions here 
void serve_request(beatLevelRequestInBank request);

/**
 * This function takes an input line from the trace file, and populates the 
 * passed in struct object with the appropriate data.
 *
 * INPUTS: 	input_command -> string representation of a single line from the trace
 * 							 file
 * OUTPUT:	Request object with each field populated 
 */
transLevelRequest parse_input(string input_command) {

	/* First break the input line into substrings */
	vector<string> token;
	size_t pos = 0;
	while((pos = input_command.find(" ")) != string::npos) {
		token.push_back(input_command.substr(0,pos));
		input_command.erase(0, pos + 1);
	}
	token.push_back(input_command.substr(0, string::npos)); //Make sure to get the last string

	/* Now grab the important information from the line */
	transLevelRequest temp;
	temp.address = strtoul(&token[7][7], NULL, 16)/32; //Convert hex input string to an address
	if(temp.address > highAddress)
		highAddress = temp.address + 27;
	if(temp.address < lowAddress)
		lowAddress = temp.address;
	temp.priority = strtoul(&token[8][6], NULL, 10); //Determine priority
	temp.length = atoi(&token[5][6]) + 1; //Length of request. Add one to convert from hex
	temp.time = atoi(&token[0][5]); //Grab the time of the request
	if(token[2].compare("RNW:Read") == 0) { //Determine if it is a read or write
		temp.read = true;
		num_reads += 1;
	}
	else {
		temp.read = false;
		num_writes += 1;
	}

	/* DEBUG */
	/*cout << temp.address << endl;
	  cout << temp.priority << endl;
	  cout << temp.length << endl;
	  cout << temp.time << endl;
	  cout << temp.read << endl;*/

	return temp;
} 


void get_requests() {

	/* Continue to read in commands until the end of file */
	ifstream inputFile;
	for (int i = 0; i < NUM_TRACES; i++) {

		/* Open the file */
		string command;
		inputFile.open(TRACE_LOCATION.c_str());
		if(!inputFile.is_open()) {
			cout << "ERROR OPENING INPUT FILE. SIMULATION HALTING\n";
			exit(-1);	  
		}

		/* Read the file */	
		while(getline(inputFile, command)) {
			transLevelRequest current_request = parse_input(command);
			current_request.core_number = i;
			request_queue[i].push_back(current_request);
		}

		/* Move to the next file name */
		inputFile.close();
		TRACE_LOCATION[TRACE_LOCATION.size() - 11] += 1;
	}

	/* Finally determine the range of memory (for dynamic coding) */
	//region_size = ((highAddress - lowAddress) + (NUM_REGIONS/2))/NUM_REGIONS; 
	region_size = (highAddress - lowAddress)/NUM_REGIONS; 
	//if(((highAddress - lowAddress)%NUM_REGIONS) != 0) {
	//	region_size += 5;
	//}
}

bool queue_empty() {

	for(int i = 0; i < NUM_TRACES; i++) {
		if(request_queue[i].size() != 0) {
			//cout << "Size: " << request_queue[i].size() << endl;
			return false;
		}
	}

	for(int i = 0; i < NUM_BANKS; i++) {
		if(bankReadQueue[i].size() != 0 || bankWriteQueue[i].size() != 0)
			return false;
	}

	return true;
}

bool check_write_queue(beatLevelRequestInBank beatLevelRequest) {

	if(beatLevelRequest.read == true) {
		for(int n = 0; n < NUM_BANKS; n++) {
			for(int i = 0; i < bankWriteQueue[n].size(); i++) {

				/* If the address is the same and they weren't issued 
				 * at the same time, serve from the write queue */
				if(beatLevelRequest.address == bankWriteQueue[n][i].address && beatLevelRequest.time != bankWriteQueue[n][i].time) {
					serve_request(beatLevelRequest); // To account for when the request is partially served from write queue
					reads_served_from_write++;
					return true;
				}
			}
		}
	}
	else {
		for(int n = 0; n < NUM_BANKS; n++) {
			for(int i = 0; i < bankWriteQueue[n].size(); i++) {

				/* If it's a write, replace the old write with the 
				 * new one */
				if(beatLevelRequest.address == bankWriteQueue[n][i].address){
					bankWriteQueue[n].erase(bankWriteQueue[n].begin() + i);
					serve_request(beatLevelRequest);
					}
			}
		}
	}
}

void input_controller(vector<transLevelRequest> request_queue[]) {

	/* Check to see which requests need to be served */
	for(int i = 0; i < NUM_TRACES; i++) {
		/* If it's time to serve the request, add it to the pending request queue */
		if(request_queue[i][0].time <= current_time && !request_queue[i].empty() && core_queues[i].size() < CORE_QUEUE_MAX) {
			request_queue[i][0].time = current_time;
			core_queues[i].push_back(request_queue[i][0]); //Add the request to the core queue
			request_queue[i].erase(request_queue[i].begin()); //Remove the request from the request queue
		}
	}

	/* Add the requests from the core queues to a temp to be ranked by priority and distributed to the banks.
	 * This is only temporary since we still need to see if the bank queues can hold the request */
	vector<transLevelRequest> temp_requests;
	for(int i = 0; i < NUM_TRACES; i++) {

		if(TRACE_TO_SERVE >= NUM_TRACES)
			TRACE_TO_SERVE = 0;
		
		if(core_queues[TRACE_TO_SERVE].size() > 0){
			temp_requests.push_back(core_queues[TRACE_TO_SERVE][0]);
		}
		TRACE_TO_SERVE++;
	}
	if(TRACE_TO_SERVE >= NUM_TRACES)
			TRACE_TO_SERVE = 0;
	TRACE_TO_SERVE++; //Make sure we start at the next trace the next round

	/* Now sort all of the pending requests by priority */
	vector<transLevelRequest> pending_requests;
	int numRequests = temp_requests.size();
	for(int i = 0; i < numRequests; i++) {
		int max = -1, index = 0;
		for(int n = 0; n < temp_requests.size(); n++) {
			if(temp_requests[n].priority > max) {
				max = temp_requests[n].priority;
				index = n;
			}
		}
		pending_requests.push_back(temp_requests[index]);
		temp_requests.erase(temp_requests.begin() + index);
	}
	/*for(int i = 0; i < temp_requests.size(); i++) {
		if(temp_requests[i].address != -1)
			pending_requests.push_back(temp_requests[i]);
	}*/


	/* Now distribute the requests to the banks */
	for(int i = 0; i < pending_requests.size(); i++) {

		/* First create the first queue object that will populate the bank queues */
		beatLevelRequestInBank next_request;
		next_request.core_number = pending_requests[i].core_number;
		next_request.time = pending_requests[i].time;
		next_request.queue_time = pending_requests[i].queue_time;
		next_request.address = pending_requests[i].address;
		next_request.length = pending_requests[i].length;
		next_request.critical = true;
		next_request.last = false;
		next_request.orderNumber = 0;
		next_request.requestNumber = NUM_REQUESTS;

		/* Determine which bank it falls into */
		int bank = (pending_requests[i].address) % NUM_BANKS;

		/* Make sure we can serve the request. If we can't wait until the next cycle and try again */
		bool stop_serving = false;
		for(int n = 0; n < pending_requests[i].length; n++) {
			if(pending_requests[i].read) {
				if(bankReadQueue[(bank + n) % NUM_BANKS].size() > MAX_BANK_QUEUE_LENGTH) 
					stop_serving = true;
			}
			else {
				if(bankWriteQueue[(bank + n) % NUM_BANKS].size() > MAX_BANK_QUEUE_LENGTH) 
					stop_serving = true;
			}
		}
		if(stop_serving == true)
			break;

		core_queues[next_request.core_number].erase(core_queues[next_request.core_number].begin());		
		/* Determine if read/write queue entry */
		if(pending_requests[i].read) {
			/* Check and see if the request can be served from the write queue first */
			if(!check_write_queue(next_request)) 
				bankReadQueue[bank].push_back(next_request);
		}
		else {
			check_write_queue(next_request);
			bankWriteQueue[bank].push_back(next_request);
		}

		/* Now populate the next bank queues */
		for(int n = 1; n < pending_requests[i].length; n++) {

			/* Update the next bank and make the next bank queue object */
			if((++bank) >= NUM_BANKS)
				bank = 0;
			next_request.address = pending_requests[i].address + n;
			next_request.time = pending_requests[i].time;
			next_request.queue_time = pending_requests[i].queue_time;			
			next_request.read = pending_requests[i].read;
			next_request.length = pending_requests[i].length;
			next_request.critical = false;
			next_request.orderNumber = n;
			next_request.requestNumber = NUM_REQUESTS;
			if(n == pending_requests[i].length - 1)
				next_request.last = true;
			else
				next_request.last = false;

			/* Determine if read/write queue entry */
			if(pending_requests[i].read) {
				/* Check and see if the request can be served from the write queue first */
				if(!check_write_queue(next_request)) 
					bankReadQueue[bank].push_back(next_request);
			}
			else {
				check_write_queue(next_request);
				bankWriteQueue[bank].push_back(next_request);
			}
		}
		NUM_REQUESTS++;
	}
}


bool parity_overwritten(int address) {

	/* Cycle through each of the currently wiped out parity banks and search for the
	 * current address */
	for(int i = 0; i < overwritten_parity_rows.size(); i++) {

		if(overwritten_parity_rows[i].address == address) {
			num_parity_conflicts++;
			return true;
		}
	}
	return false;
}


bool codePresent(int address1, int address2) {

	bool address1Found = false, address2Found = false;

	/* First see if the region is even coded */
	int region = (address1 - lowAddress)/region_size;
	bool regionFound = false;
	for(int i = 0; i < NUM_ACTIVE_REGIONS; i++) {
		if(coded_regions[i] == region)
			regionFound = true;
	}
	if(!regionFound)
		return false;
	
	/* Now, check to see if the code is present */
	for(int n = 0; n < NUM_ACTIVE_REGIONS; n++) {	
		for(int i = 0; i < previously_read[coded_regions[n]].size(); i++) {
			if(previously_read[coded_regions[n]][i] == address1)
				address1Found = true;
			if(previously_read[coded_regions[n]][i] == address2)
				address2Found = true;
		}
	}

	if(address1Found && address2Found)
		return true;
	else
		return false;
}


void serve_request(beatLevelRequestInBank request) {
		
	/* Determine if we're dealing with read or write */
	unordered_map<int, beatLevelRequestInBank> *request_list;
	if(request.read == true) 
		request_list = &previously_served_reads;
	else
		request_list = &previously_served_writes;

	/* Serve the request */
	auto it = request_list->find(request.requestNumber);
	if(it == request_list->end()) {
		request.numServed = 1;
		request_list->insert({request.requestNumber, request});
	}
	else {
		it->second.numServed += 1;
		/* If the request has completely been served, take the transactional latency and delete it */
		if(it->second.numServed == it->second.length) { 
			if(it->second.read == true)
				read_last_word_latency += (current_time - it->second.time);
			else
				write_last_word_latency += (current_time - it->second.time);
			request_list->erase(it);	
		}
	}
}


FILE* dump = fopen("./dump.txt", "w");
void access_scheduler() {

	vector<beatLevelRequestInBank> past_requests;
	int region; //The bank's coded region

	/* Serve a request from each bank */
	for(int i = 0; i < NUM_BANKS; i++) {

		/* Serve a request from the greater queue */
		if((dataBankStatus[i]==BANK_FREE) && bankWriteQueue[i].size() < WR_QUEUE_BUILDUP && bankReadQueue[i].size() != 0 && (current_time % MEM_DELAY) == 0) {

			/* Check to see if the request can be served from the parity banks */
		/* Currently, we're only looking at past reads, not in the future */
			if(!parity_overwritten(bankReadQueue[i][0].address)) { //Make sure a write didn't wipe out the parity
				for(int n = 0; n < 3; n++) {
					int lookahead = bankReadQueue[bank_bitmap[i][n]].size();
					if(lookahead > MAX_LOOKAHEAD)
						lookahead = MAX_LOOKAHEAD;
					if(parity_stall[i/4][parity_bitmap[i%4][n]] == BANK_FREE){  // Check if the parity is available.
						/* Make sure the bank we're checking has requests in the queue */
						for(int z = 0; z < lookahead; z++) {
							/* Check if using the parity bank is possible */
							if(bank_reads[bank_bitmap[i][n]][z].address/8 == bank_reads[i][0].address/8) {

								/* DEBUG */
								/*fprintf(dump, "Parity Array:\n");
								  for(int t = 0; t < 12; t++)
								  fprintf(dump, "%d ", parity_stall[t/6][t%6]);
								  fprintf(dump, "\nDelay: %d	Address: %x	Time: %d	Current Time: %d	n: %d	i: %d\n", current_time - bank_reads[i][0].time, bank_reads[bank_bitmap[i][n]][0].address, bank_reads[i][0].time, current_time, n, i);*/

								/* Serve the request if the bank is free */
								if(parity_stall[i/4][parity_bitmap[i%4][n]] == BANK_FREE) {
									if(!parity_overwritten(bank_reads[bank_bitmap[i][n]][z].address)) {
										if(codePresent(bank_reads[i][0].address, bank_reads[bank_bitmap[i][n]][z].address)) {
											if(bank_reads[bank_bitmap[i][n]][z].critical == true) {

												fprintf(dump, "Delay: %d\t Address: %d	Time: %d P\n", current_time - bank_reads[bank_bitmap[i][n]][z].time, bank_reads[bank_bitmap[i][n]][z].address, current_time);
												read_cr_word_latency += (current_time) - bank_reads[bank_bitmap[i][n]][z].time;
											}
											serve_request(bank_reads[bank_bitmap[i][n]][z]);
											parity_hit++;
											bank_reads[bank_bitmap[i][n]].erase(bank_reads[bank_bitmap[i][n]].begin() + z);
											parity_stall[i/4][parity_bitmap[i%4][n]] = BANK_BUSY;
										}
									}
								}
							}
						}
					}
				}
			}
			/* Serving the head of the queue here */
			if(bank_reads[i][0].critical == true) {
				read_cr_word_latency += (current_time) - bank_reads[i][0].time;
				fprintf(dump, "Delay: %d\t Address: %d 	Time: %d\n", current_time - bank_reads[i][0].time, bank_reads[i][0].address, current_time);
			}
			past_requests.push_back(bank_reads[i][0]);
			region = (bank_reads[i][0].address - lowAddress)/region_size;
			if(region >= NUM_REGIONS) {
				cout << lowAddress << " " << bank_reads[i][0].address << " " << highAddress << endl;
				exit(0);
			}
			previously_read[region].push_back(bank_reads[i][0].address);
			serve_request(bank_reads[i][0]);			
			bank_reads[i].erase(bank_reads[i].begin());
		}
		else if((dataBankStatus[i]==BANK_FREE) && bank_writes[i].size() != 0 && (current_time % MEM_DELAY) == 0) {
			/* First serve the request in the data bank */
			if(bank_writes[i][0].critical == true) {
				write_cr_word_latency += (current_time) - bank_writes[i][0].time;
			}
			serve_request(bank_writes[i][0]);
			bank_writes[i][0].inParity = false; //Mark that the write was not written to parity
			overwritten_parity_rows.push_back(bank_writes[i][0]);
			bank_writes[i].erase(bank_writes[i].begin());	

			/* If another request is waiting behind it, try to use the parity banks */
			if(bank_writes[i].size() != 0) {
				/* Check the appropriate parity bank for storing the write */
				int parity_bank_num = 0;
				switch(i) {
					case 0:
						parity_bank_num = 0;
						break;
					case 1:
						parity_bank_num = 3;
						break;
					case 2:
						parity_bank_num = 4;
						break;
					case 3:
						parity_bank_num = 5;
						break;
				}

				/* Make sure the bank isn't busy serving a read */
				if(parity_stall[i/4][parity_bank_num] == BANK_FREE) {
					parity_stall[i/4][parity_bank_num] = BANK_BUSY; //Mark the bank as busy
					
					/* Serve the write the same way as above */
					if(bank_writes[i][0].critical == true) { //We'll serve the request now if it's critical
						write_cr_word_latency += (current_time) - bank_writes[i][0].time;
					}
					serve_request(bank_writes[i][0]);
					bank_writes[i][0].inParity = true; //Mark that the write was written to parity
					bank_writes[i][0].parityNumber = parity_bank_num;
					overwritten_parity_rows.push_back(bank_writes[i][0]);
					bank_writes[i].erase(bank_writes[i].begin());
				}
			}
		}
	}

	/* Set all the parity banks as free */
	for(int z = 0; z < 2; z++) {
		for(int i = 0; i < NUM_PARITY_BANKS; i++)
			parity_stall[z][i] = BANK_FREE;
	}

}
bool check_recode() {
	/* First, see if we need to finish the coding process for any data that's waiting to be recoded in the parity banks */
	for(int i = 0; i < pending_parity_writes.size(); i++) {
		/* Mark all the corresponding parity banks as busy */
		int bank = pending_parity_writes[i].address % 8;
		for(int n = 0; n < 3; n++)
			parity_stall[bank/4][parity_bitmap[bank % 4][n]] = BANK_BUSY;
	}
	pending_parity_writes.clear();
	/* Now, perform reads to banks to begin the recode process. Check if the oldest element has reached it's limit */
	if(overwritten_parity_rows.size() != 0) {
		if((current_time - overwritten_parity_rows[0].time) > WRITE_REPAIR_TIME) {
			/* Repair the entire row */
			int row = overwritten_parity_rows[0].address/8;
			
			/* Search through the entire queue, and remove all requests that fall on
			 * the same row */
			for(int i = 0; i < overwritten_parity_rows.size(); i++) {
				if(overwritten_parity_rows[i].address/8 == row) {
					/* Bookkeeping to make sure correct banks are marked as busy */
					int bank = overwritten_parity_rows[i].address % 8;
					//for(int n = 0; n < 3; n++)
					//	parity_stall[bank/4][parity_bitmap[bank % 4][n]] = BANK_BUSY;
					/* Mark either the parity bank or data bank as busy, depending on where the data was written */
					if(overwritten_parity_rows[i].inParity == true) {
						parity_stall[bank/4][overwritten_parity_rows[i].parityNumber] = BANK_BUSY;
					}
					else
						dataBankStatus[bank] = BANK_BUSY;

					pending_parity_writes.push_back(overwritten_parity_rows[i]); //We still need to update the parities in the next cycle
					overwritten_parity_rows.erase(overwritten_parity_rows.begin() + i);
					i--;
					if(overwritten_parity_rows.size() == 0)
						break;
				}

			}
			return true;
		}
	}

	/* Only recode if there are no pending queue requests */
	bool no_pending_requests = true;
	for(int i = 0; i < NUM_BANKS; i++) {
		if(bank_reads[i].size() != 0 || bank_writes[i].size() != 0) {
			return false;
		}
	}
	/* Remove the oldest request */
	if(no_pending_requests && overwritten_parity_rows.size() != 0) {
		overwritten_parity_rows.erase(overwritten_parity_rows.begin());		
		num_idle_cycles++;
	}
	return true;
}


void update_codes() {
	/* Make a copy of the data */
	int temp_copy[NUM_REGIONS];
	int top_hits[NUM_ACTIVE_REGIONS];
	for(int i = 0; i < NUM_REGIONS; i++) {
		temp_copy[i] = region_hits[i];
		//cout << region_hits[i] << " ";
	}
	//cout << endl;

	/* Sort the regions by number of hits */
	for(int i = 0; i < NUM_ACTIVE_REGIONS; i++) {
		int hits = temp_copy[i];
		int region_num = i;
		for(int n = 0; n < NUM_REGIONS; n++) {
			if(n != i) {
				if(hits < temp_copy[n]) {
					hits = temp_copy[n];
					region_num = n;
				}
			}
		}
		top_hits[i] = region_num;
		temp_copy[region_num] = -1;
	}

	/* Clear all coding regions that are not in the top */
	for(int i = 0; i < NUM_REGIONS; i++) {
		bool clear_region = true;
		for(int n = 0; n < NUM_ACTIVE_REGIONS; n++) {
			//cout << top_hits[n] << " ";
			if(i == top_hits[n])
				clear_region = false;
		}
		if(clear_region)
			previously_read[i].clear();

	}	

	/* Update the coded regions */
	for(int i = 0; i < NUM_ACTIVE_REGIONS; i++) {
		coded_regions[i] = top_hits[i];
	}
	
	//cout << endl;
}

int sc_main(int argc, char* argv[]) {

	/* Take in the ratio between processor and memory clock speeds */
	MEM_DELAY = atoi(argv[1]);
	MAX_LOOKAHEAD = atoi(argv[2]);
	WRITE_REPAIR_TIME = atoi(argv[3]);
	TRACE_LOCATION = argv[4];

	/* Initializing parity stall to BANK_FREE */
	for(int z = 0; z < 2; z++) {
		for(int i = 0; i < NUM_PARITY_BANKS; i++)
			parity_stall[z][i] = BANK_FREE;
	}

	/* Make all coded regions 0 */
	for(int i = 0; i < NUM_REGIONS; i++) {
		region_hits[i] = 0;
	}

	/* First populate the request queues with all requests from banks */
	get_requests();

	/* Execute the main loop which will service all requests */
	while(!queue_empty()) {
		input_controller(request_queue);
		check_recode(); 
		access_scheduler();

		/* Reset the busy banks for the next memory cycle */
		for(int i = 0; i < NUM_BANKS; i++)
			dataBankStatus[i] = BANK_FREE;
	
		/* Cycle the clock and dump out info to the screen */
		current_time += 1; 
		if(current_time % 5000 == 0) {
			cout << current_time << "\t";
			for(int i = 0; i < NUM_TRACES; i++)
				cout << core_queues[i].size() << " ";
			cout << endl;
		}

		update_codes();
	}

	/* Display the results */
	cout << "Number of reads: " << num_reads << " Number of writes: " << num_writes << endl;
	cout << "Average read critical word latency: " << (float) read_cr_word_latency/num_reads << endl;
	cout << "Average read last word latency: " << (float) read_last_word_latency/num_reads << endl;
	cout << "Average write critical word latency: " << (float) write_cr_word_latency/num_writes << endl;
	cout << "Average write last word latency: " << (float) write_last_word_latency/num_writes << endl;
	cout << "Reads served from write queue: " << reads_served_from_write << endl;
	cout << "Parity hits: " << parity_hit << endl;
	cout << "Number of idle clock cycles: " << num_idle_cycles << endl;
	cout << "Number of parity conflicts: " << num_parity_conflicts << endl;

	cout << "Total clock cycles: " << current_time << endl;

	FILE* results = fopen("coding_results.txt", "a");
	fprintf(results, "%d\t%f\t%f\t%f\t%d\t%d\t%d\n", MEM_DELAY, (float)read_cr_word_latency/num_reads, (float)read_last_word_latency/num_reads, (float)write_last_word_latency/num_writes, current_time, parity_hit, num_parity_conflicts);

	fclose(results);
	fclose(dump);
}




















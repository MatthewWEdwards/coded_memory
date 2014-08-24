#include "systemc.h"
#include <string>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream> 

using namespace std;
enum {
BANK_A = 0,
BANK_B,
BANK_C, 
BANK_D, 
BANK_E, 
BANK_F, 
BANK_G, 
BANK_H,
TOTAL_BANKS
};
/* Some definitions to change simulation parameters */
#define NUM_BANKS 8
#define NUM_PARITY_BANKS 10 //Not currently used. Num parity banks per 4 data banks
#define NUM_TRACES 6
#define WR_QUEUE_BUILDUP 5
#define CORE_QUEUE_MAX 8
#define MAX_BANK_QUEUE_LENGTH 10
string TRACE_LOCATION;//("../traces/LTE/dsp_0_trace.txt");

#define BANK_FREE 0
#define BANK_BUSY 1
/* Input Parameters */
int MEM_DELAY; 
int MAX_LOOKAHEAD;
int WRITE_REPAIR_TIME;

/* Struct for the input requests from the processors */
typedef struct request {

	int address;
	int priority;
	int length;
	int time;
	int queue_time;
	int core_number; //What core the request came from 
	bool read;

} request;

/* Struct for the queue request */
typedef struct bank_request {

	int address;
	int priority;
	int time;
	int queue_time;
	int core_number;
	bool read;
	bool critical;
	bool last;

} bank_request;
/* Bitmap for which parity banks a data bank is coded in */
int parity_bitmap[NUM_BANKS][3] = {
	{0,3,6},
	{0,4,7},
	{0,5,8},
	{1,3,8},
	{1,4,6},
	{1,5,7},
	{2,3,7},
	{2,4,8},
	//{2,5,6}
};
/*
int bank_bitmap[NUM_BANKS][6] = {
	{1,2,3,6,0,0},
	{0,2,4,7,5,6},
	{0,1,2,2,3,7},
	{4,5,0,6,2,7},
	{3,5,1,7,4,4},
	{3,4,5,5,1,6},
	{6,6,0,3,1,5},
	{7,7,1,4,2,3},
	//{6,7,2,5,0,4}	
};
int bank_bitmap2[NUM_BANKS][6] = {
	{2,1,6,3,4,4},
	{2,0,7,4,6,5},
	{1,0,5,5,7,3},
	{5,4,6,0,7,2},
	{5,3,7,1,0,0},
	{4,3,2,2,6,1},
	{7,7,3,0,5,1},
	{6,6,4,1,3,2},
	//{7,6,5,2,4,0}
};
*/
#define CODE_LOCALITY	3
int s32auxiliaryBankinParity[NUM_BANKS][CODE_LOCALITY] = {
		{1,2,3},
		{0,2,3},
		{0,1,3},
		{0,1,2},
		{5,6,7},
		{4,6,7},
		{4,5,7},
		{4,5,6}
};
int s32banks2Parity[NUM_BANKS][NUM_BANKS] = {
	//Region 1
	{0xffff,     0,     4,     2,0xffff,0xffff,0xffff,0xffff},
	{     0,0xffff,     3,     4,0xffff,0xffff,0xffff,0xffff},
	{     4,     3,0xffff,     1,0xffff,0xffff,0xffff,0xffff},
	{     2,     4,     1,0xffff,0xffff,0xffff,0xffff,0xffff},
	// Region 2
	{0xffff,0xffff,0xffff,0xffff,   0xffff,	 5, 	9,     7},
	{0xffff,0xffff,0xffff,0xffff,        5,0xffff, 	8,     9},
	{0xffff,0xffff,0xffff,0xffff,        9,	 8,0xffff,     6},
	{0xffff,0xffff,0xffff,0xffff,        7,	 9, 	6,0xffff},

};
/* GLOBALS */
vector<bank_request> readQueueBank[NUM_BANKS]; //Queue of read requests for each bank
vector<bank_request> writeQueueBank[NUM_BANKS]; //Queue of write requests for each bank
vector<request> request_queue[NUM_TRACES];
vector<bank_request> overwritten_parity_rows; //Keep track which parities are busy due to writes
vector<request> core_queues[NUM_TRACES]; //These queues hold the requests from the cores
vector<int> previously_read;
int current_time = 0; //Current time in ns
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
int parityBankStatus[NUM_PARITY_BANKS];
bool dataBankStatus[NUM_BANKS];
unsigned int gu32writeParityMap[8] = {0,3,1,2,5,8,6,7};
// Function Declarations 
void serveByLookInReadQueue(unsigned int u32bankNum,unsigned int u32address,unsigned int u32maxLookAhead,unsigned int u32OriginalBank);
/**
 * This function takes an input line from the trace file, and populates the 
 * passed in struct object with the appropriate data.
 *
 * INPUTS: 	input_command -> string representation of a single line from the trace
 * 							 file
 * OUTPUT:	Request object with each field populated 
 */
request parse_input(string input_command) {

	/* First break the input line into substrings */
	vector<string> token;
	size_t pos = 0;
	while((pos = input_command.find(" ")) != string::npos) {
		token.push_back(input_command.substr(0,pos));
		input_command.erase(0, pos + 1);
	}
	token.push_back(input_command.substr(0, string::npos)); //Make sure to get the last string

	/* Now grab the important information from the line */
	request temp;
	temp.address = strtoul(&token[7][7], NULL, 16)/32; //Convert hex input string to an address
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
//			cout << "ERROR OPENING INPUT FILE. SIMULATION HALTING\n";
			exit(-1);	  
		}

		/* Read the file */	
		while(getline(inputFile, command)) {
			request current_request = parse_input(command);
			current_request.core_number = i;
			request_queue[i].push_back(current_request);
		}

		/* Move to the next file name */
		inputFile.close();
		TRACE_LOCATION[TRACE_LOCATION.size() - 11] += 1;
	}
}


bool queue_empty() {

	for(int i = 0; i < NUM_TRACES; i++) {
		if(request_queue[i].size() != 0) {
			//cout << "Size: " << request_queue[i].size() << endl;
			return false;
		}
	}

	for(int i = 0; i < NUM_BANKS; i++) {
		if(readQueueBank[i].size() != 0 || writeQueueBank[i].size() != 0)
			return false;
	}

	return true;
}


void input_controller(vector<request> request_queue[]) {

	/* Check to see which requests need to be served */
	for(int i = 0; i < NUM_TRACES; i++) {
		/* First make sure the request can't be served from the write queue */
		for(int y = 0; y < NUM_BANKS; y++) {
			for(int z = 0; z < writeQueueBank[y].size(); z++) {
				if(request_queue[i].size() > 0) {
					if(writeQueueBank[y][z].address == request_queue[i][0].address && request_queue[i][0].time <= current_time) {
						reads_served_from_write++;
						request_queue[i].erase(request_queue[i].begin());
					}
				}
			}
		}

		/* If it's time to serve the request, add it to the pending request queue */
		if(request_queue[i][0].time <= current_time && !request_queue[i].empty() && core_queues[i].size() < CORE_QUEUE_MAX) {
			request_queue[i][0].time = current_time;
			core_queues[i].push_back(request_queue[i][0]); //Add the request to the core queue
			request_queue[i].erase(request_queue[i].begin()); //Remove the request from the request queue
		}
	}

	/* Add the requests from the core queues to a temp to be ranked by priority and distributed to the banks.
	 * This is only temporary since we still need to see if the bank queues can hold the request */
	vector<request> temp_requests;
	for(int i = 0; i < NUM_TRACES; i++) {
		if(core_queues[i].size() > 0){
			temp_requests.push_back(core_queues[i][0]);
		}
	}

	/* Now sort all of the pending requests by priority */
	vector<request> pending_requests;
	int numRequests = temp_requests.size();
	for(int i = 0; i < numRequests; i++) {
		int max = 100, index = 0;
		for(int n = 0; n < temp_requests.size(); n++) {
			if(temp_requests[n].priority < max) {
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
		bank_request next_request;
		next_request.core_number = pending_requests[i].core_number;
		next_request.time = pending_requests[i].time;
		next_request.queue_time = pending_requests[i].queue_time;
		next_request.address = pending_requests[i].address;
		next_request.critical = true;
		next_request.last = false;

		/* Determine which bank it falls into */
		int bank = (pending_requests[i].address) % NUM_BANKS;

		/* Make sure we can serve the request. If we can't wait until the next cycle and try again */
		bool stop_serving = false;
		for(int n = 0; n < pending_requests[i].length; n++) {
			if(pending_requests[i].read) {
				if(readQueueBank[(bank + n) % NUM_BANKS].size() > MAX_BANK_QUEUE_LENGTH) 
					stop_serving = true;
			}
			else {
				if(writeQueueBank[(bank + n) % NUM_BANKS].size() > MAX_BANK_QUEUE_LENGTH) 
					stop_serving = true;
			}
		}
		if(stop_serving == true)
			break;

		core_queues[next_request.core_number].erase(core_queues[next_request.core_number].begin());		
		/* Determine if read/write queue entry */
		if(pending_requests[i].read) 
			readQueueBank[bank].push_back(next_request);
		else
			writeQueueBank[bank].push_back(next_request);

		/* Now populate the next bank queues */
		for(int n = 1; n < pending_requests[i].length; n++) {

			/* Update the next bank and make the next bank queue object */
			if((++bank) >= NUM_BANKS)
				bank = 0;
			next_request.address = pending_requests[i].address + n;
			next_request.time = pending_requests[i].time;
			next_request.queue_time = pending_requests[i].queue_time;			
			next_request.read = pending_requests[i].read;
			next_request.critical = false;
			if(n == pending_requests[i].length - 1)
				next_request.last = true;
			else
				next_request.last = false;

			/* Determine if read/write queue entry */
			if(pending_requests[i].read)
				readQueueBank[bank].push_back(next_request);
			else
				writeQueueBank[bank].push_back(next_request);
		}
	}
	
	/* DEBUG */
	/*if(current_time == 5) {
		for(int n = 0; n < 6; n++) {
			for(int i = 0; i < 8; i++) {
				if(readQueueBank[i][n].critical)
					cout << readQueueBank[i][n].address << "C ";
				else
					cout << readQueueBank[i][n].address << '\t';
				
			}
			cout << endl;
		}
	}*/
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

/*
bool codePresent(int address1, int address2, int address3) {

	bool address1Found = false, address2Found = false, address3Found = false;
	
	for(int i = 0; i < previously_read.size(); i++) {
		if(previously_read[i] == address1)
			address1Found = true;
		if(previously_read[i] == address2)
			address2Found = true;
		if(previously_read[i] == address3)
			address3Found = true;
	}

	if(address1Found && address2Found && address3Found)
		return true;
	else
		return false;
}
*/


FILE* dump = fopen("./dump.txt", "w");
void access_scheduler() {

	vector<bank_request> past_requests;

	/* Serve a request from each bank */
	for(int i = 0; i < NUM_BANKS; i++) {
		/* Serve a request from the greater queue */
		if((dataBankStatus[i] == BANK_FREE) && writeQueueBank[i].size() < WR_QUEUE_BUILDUP && readQueueBank[i].size() != 0 && (current_time % MEM_DELAY) == 0) {
//cout << "In Read Section "<< i << endl;
			if(!parity_overwritten(readQueueBank[i][0].address)){ // Make sure a write didn't wipe out the parity
					for(int n=0;n<CODE_LOCALITY;n++){ // Go across all the parity banks
						if(parityBankStatus[s32banks2Parity[i][s32auxiliaryBankinParity[i][n]]] ==BANK_FREE){
							int lookahead = readQueueBank[s32auxiliaryBankinParity[i][n]].size();
							if(lookahead > MAX_LOOKAHEAD)
								lookahead=MAX_LOOKAHEAD;
							// Lets Look in the queue now
							serveByLookInReadQueue(s32auxiliaryBankinParity[i][n],readQueueBank[i][0].address,MAX_LOOKAHEAD,i);
							}
					}
				}
			// Make sure we didn't serve all the requests in the queue using the
			// parity banks 
			if(readQueueBank[i][0].critical == true) {
				read_cr_word_latency += (current_time) - readQueueBank[i][0].time;
				fprintf(dump, "Delay: %d\t Address: %d 	Time: %d\n", current_time - readQueueBank[i][0].time, readQueueBank[i][0].address, current_time);
				//fprintf(dump, "Delay: %d	Address: %d	Time: %d\n", current_time - readQueueBank[i][0].time, readQueueBank[i][0].address, readQueueBank[i][0].time);
			}
			if(readQueueBank[i][0].last == true){
				read_last_word_latency += (current_time) - readQueueBank[i][0].time;
			}
			past_requests.push_back(readQueueBank[i][0]);
			previously_read.push_back(readQueueBank[i][0].address); //Record that the memory location is now coded
			readQueueBank[i].erase(readQueueBank[i].begin());
		}
		else if((dataBankStatus[i] == BANK_FREE) && writeQueueBank[i].size() != 0 && (current_time % MEM_DELAY) == 0) {
			/* First serve the request in the data bank */
			if(writeQueueBank[i][0].critical == true) {
				write_cr_word_latency += (current_time) - writeQueueBank[i][0].time;
			}
			if(writeQueueBank[i][0].last == true) {
				write_last_word_latency += (current_time) - writeQueueBank[i][0].time;
			}	
			overwritten_parity_rows.push_back(writeQueueBank[i][0]);
			writeQueueBank[i].erase(writeQueueBank[i].begin());	

			/* If another request is waiting behind it, try to use the parity banks */
			if(writeQueueBank[i].size() != 0) {
				/* Check the appropriate parity bank for storing the write */
				int parity_bank_num = 0;
				parity_bank_num = gu32writeParityMap[i];

				/* Make sure the bank isn't busy serving a read */
				if(parityBankStatus[parity_bank_num] == BANK_FREE) {
					parityBankStatus[parity_bank_num] = BANK_BUSY; //Mark the bank as busy
					
					/* Serve the write the same way as above */
					if(writeQueueBank[i][0].critical == true) {
						write_cr_word_latency += (current_time) - writeQueueBank[i][0].time;
					}
					if(writeQueueBank[i][0].last == true) {
						write_last_word_latency += (current_time) - writeQueueBank[i][0].time;
					}	
					overwritten_parity_rows.push_back(writeQueueBank[i][0]);
					writeQueueBank[i].erase(writeQueueBank[i].begin());
				}
			}
		}
	}

	/* Set all the parity banks as free */
	for(int i = 0; i < NUM_PARITY_BANKS; i++)
		parityBankStatus[i] = BANK_FREE;

}


bool check_recode() {

	/* First check if the oldest element has reached it's limit */
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
					for(int n = 0; n < 3; n++)
						parityBankStatus[parity_bitmap[bank % 4][n]] = 0;
					dataBankStatus[bank] = BANK_BUSY;

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
		if(readQueueBank[i].size() != 0 || writeQueueBank[i].size() != 0) {
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

int sc_main(int argc, char* argv[]) {

	/* Take in the ratio between processor and memory clock speeds */
	MEM_DELAY = atoi(argv[1]);
	MAX_LOOKAHEAD = atoi(argv[2]);
	WRITE_REPAIR_TIME = atoi(argv[3]);
	TRACE_LOCATION = argv[4];
        //cout << TRACE_LOCATION << endl;
	for(int i = 0; i < NUM_PARITY_BANKS; i++)
		parityBankStatus[i] = BANK_FREE;

	/* First populate the request queues with all requests from banks */
	get_requests();

	/* Execute the main loop which will service all requests */
	while(!queue_empty()) {
		//cout << "time is " << current_time << endl;
		input_controller(request_queue);
		//cout << "After Input Controller"<< endl;
		check_recode(); 
		//cout << "After Check Recode "<< endl;
		access_scheduler();
		//cout << "After Access Scheduler" << endl;
		/* Reset the busy banks for the next memory cycle */
		for(int i = 0; i < NUM_BANKS; i++)
			dataBankStatus[i] = BANK_FREE;

		current_time += 1; //Cycle the clock 
		if(current_time % 5000 == 0) {
			cout << current_time << "\t";
			for(int i = 0; i < NUM_TRACES; i++)
				cout << core_queues[i].size() << " ";
			cout << endl;
		}
		string temp;
		//cin >> temp;
	}

	/* Dispaly the results */
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



void serveByLookInReadQueue(unsigned int u32bankNum,unsigned int u32address,unsigned int u32maxLookAhead,unsigned int u32OriginalBank){
	unsigned int u32lookahead,u32itrCount;
	u32lookahead = readQueueBank[u32bankNum].size();
//	cout << " the program was here for original Bank "<< u32bankNum << " " << u32OriginalBank << endl;
	// Restrict the look ahead size to max look ahead
	if(u32lookahead > u32maxLookAhead)
		u32lookahead = u32maxLookAhead;
	// Check the address in the queue
	for(u32itrCount=0;u32itrCount<u32lookahead;u32itrCount++){
			if((readQueueBank[u32bankNum][u32itrCount].address/8 == u32address/8) && (parityBankStatus[s32banks2Parity[u32OriginalBank][u32bankNum]]==BANK_FREE)){
				if(readQueueBank[u32bankNum][u32itrCount].critical == true) {
					read_cr_word_latency += (current_time) - readQueueBank[u32bankNum][u32itrCount].time;
				}
				if(readQueueBank[u32bankNum][u32itrCount].last == true) {
					read_last_word_latency += (current_time) - readQueueBank[u32bankNum][u32itrCount].time;
				}
				parity_hit++;
				
				readQueueBank[u32bankNum].erase(readQueueBank[u32bankNum].begin() + u32itrCount);
//				cout << s32banks2Parity[u32OriginalBank][u32bankNum] << endl;
				parityBankStatus[s32banks2Parity[u32OriginalBank][u32bankNum]] = BANK_BUSY;
}
		}
}

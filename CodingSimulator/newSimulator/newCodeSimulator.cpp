#include "systemc.h"
#include <string>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream> 

using namespace std;


/* Some definitions to change simulation parameters */
#define NUM_BANKS 9
#define NUM_PARITY_BANKS 9 //Not currently used. Num parity banks per 4 data banks
#define NUM_TRACES 6
#define WR_QUEUE_BUILDUP 5
#define CORE_QUEUE_MAX 8
#define MAX_BANK_QUEUE_LENGTH 10
string TRACE_LOCATION;//("../traces/LTE/dsp_0_trace.txt");

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
	{2,5,6}
};

int bank_bitmap[NUM_BANKS][6] = {
	{1,2,3,6,4,8},
	{0,2,4,7,5,6},
	{0,1,5,8,3,7},
	{4,5,0,6,2,7},
	{3,5,1,7,0,8},
	{3,4,2,8,1,6},
	{7,8,0,3,1,5},
	{6,8,1,4,2,3},
	{6,7,2,5,0,4}	
};

int bank_bitmap2[NUM_BANKS][6] {

	{2,1,6,3,8,4},
	{2,0,7,4,6,5},
	{1,0,8,5,7,3},
	{5,4,6,0,7,2},
	{5,3,7,1,8,0},
	{4,3,8,2,6,1},
	{8,7,3,0,5,1},
	{8,6,4,1,3,2},
	{7,6,5,2,4,0}

};

/* GLOBALS */
vector<bank_request> bank_reads[NUM_BANKS]; //Queue of read requests for each bank
vector<bank_request> bank_writes[NUM_BANKS]; //Queue of write requests for each bank
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
int parity_stall[NUM_PARITY_BANKS];
bool bank_busy[NUM_BANKS];


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
			cout << "ERROR OPENING INPUT FILE. SIMULATION HALTING\n";
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


int previous_size = 0;
bool queue_empty() {


	/*if(request_queue[0].size() != previous_size && request_queue[0].size() != 0) {
		cout << "Size: " << previous_size << endl;
		previous_size = request_queue[0].size();
		cout << "Time: " << request_queue[0][0].time << endl;
	}*/

	for(int i = 0; i < NUM_TRACES; i++) {
		if(request_queue[i].size() != 0) {
			//cout << "Size: " << request_queue[i].size() << endl;
			return false;
		}
	}

	for(int i = 0; i < NUM_BANKS; i++) {
		if(bank_reads[i].size() != 0 || bank_writes[i].size() != 0)
			return false;
	}

	return true;
}


void input_controller(vector<request> request_queue[]) {

	/* Check to see which requests need to be served */
	for(int i = 0; i < NUM_TRACES; i++) {
		/* First make sure the request can't be served from the write queue */
		for(int y = 0; y < NUM_BANKS; y++) {
			for(int z = 0; z < bank_writes[y].size(); z++) {
				if(request_queue[i].size() > 0) {
					if(bank_writes[y][z].address == request_queue[i][0].address && request_queue[i][0].time <= current_time) {
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
				if(bank_reads[(bank + n) % NUM_BANKS].size() > MAX_BANK_QUEUE_LENGTH) 
					stop_serving = true;
			}
			else {
				if(bank_writes[(bank + n) % NUM_BANKS].size() > MAX_BANK_QUEUE_LENGTH) 
					stop_serving = true;
			}
		}
		if(stop_serving == true)
			break;

		core_queues[next_request.core_number].erase(core_queues[next_request.core_number].begin());		
		/* Determine if read/write queue entry */
		if(pending_requests[i].read) 
			bank_reads[bank].push_back(next_request);
		else
			bank_writes[bank].push_back(next_request);

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
				bank_reads[bank].push_back(next_request);
			else
				bank_writes[bank].push_back(next_request);
		}
	}
	
	/* DEBUG */
	/*if(current_time == 5) {
		for(int n = 0; n < 6; n++) {
			for(int i = 0; i < 8; i++) {
				if(bank_reads[i][n].critical)
					cout << bank_reads[i][n].address << "C ";
				else
					cout << bank_reads[i][n].address << '\t';
				
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


bool codePresent(int address1, int address2) {

	bool address1Found = false, address2Found = false;
	
	for(int i = 0; i < previously_read.size(); i++) {
		if(previously_read[i] == address1)
			address1Found = true;
		if(previously_read[i] == address2)
			address2Found = true;
	}

	if(address1Found && address2Found)
		return true;
	else
		return false;
}



FILE* dump = fopen("./dump.txt", "w");
void access_scheduler() {

	/* DEBUG */
	/*if(1) {
		for(int n = 0; n < 1; n++) {
			for(int i = 0; i < 8; i++) {
				if(bank_reads[i].size() != 0) {
					if(bank_reads[i][n].critical)
						cout << bank_reads[i][n].address << "C ";
					else
						cout << bank_reads[i][n].address << '\t';
				}
			}
			cout << endl;
		}
	}*/

	vector<bank_request> past_requests;

	/* Serve a request from each bank */
	for(int i = 0; i < NUM_BANKS; i++) {

		/* Serve a request from the greater queue */
		if(!bank_busy[i] && bank_writes[i].size() < WR_QUEUE_BUILDUP && bank_reads[i].size() != 0 && (current_time % MEM_DELAY) == 0) {

			/* Check to see if the request can be served from the parity banks */
			/* Currently, we're only looking at past reads, not in the future */
			if(!parity_overwritten(bank_reads[i][0].address)) { //Make sure a write didn't wipe out the parity
				for(int n = 0; n < 6; n++) {
					for(int y = 0; y < 6; y++) {
						int lookahead = bank_reads[bank_bitmap[i][n]].size();
						if(lookahead > MAX_LOOKAHEAD)
							lookahead = MAX_LOOKAHEAD;
						/* Make sure the bank we're checking has requests in the queue */
						for(int z = 0; z < lookahead; z++) { //Right now, only consider the head of the second bank
							/* Check if using the parity bank is possible */
							if(bank_reads[bank_bitmap[i][n]][0].address/8 == bank_reads[i][0].address/8 && bank_reads[bank_bitmap2[i][y]][z].address/8 == bank_reads[i][0].address/8) {
	
								/* DEBUG */
								/*fprintf(dump, "Parity Array:\n");
								  for(int t = 0; t < 12; t++)
								  fprintf(dump, "%d ", parity_stall[t/6][t%6]);
								  fprintf(dump, "\nDelay: %d	Address: %x	Time: %d	Current Time: %d	n: %d	i: %d\n", current_time - bank_reads[i][0].time, bank_reads[bank_bitmap[i][n]][0].address, bank_reads[i][0].time, current_time, n, i);*/
	
								/* Serve the request if the bank is free */
								if(parity_stall[parity_bitmap[i][n/2]] == -1 && !parity_overwritten(bank_reads[bank_bitmap[i][n]][0].address) && !parity_overwritten(bank_reads[bank_bitmap2[i][y]][z].address) && codePresent(bank_reads[i][0].address, bank_reads[bank_bitmap[i][n]][0].address, bank_reads[bank_bitmap2[i][y]][z].address)) {
									if(bank_reads[bank_bitmap2[i][y]][z].critical == true) {
	
										fprintf(dump, "Delay: %d\t Address: %d	Time: %d P\n", current_time - bank_reads[bank_bitmap2[i][y]][z].time, bank_reads2[bank_bitmap[i][y]][z].address, current_time);
										read_cr_word_latency += (current_time) - bank_reads[bank_bitmap2[i][y]][z].time;
									}
									if(bank_reads[bank_bitmap2[i][y]][z].last == true) {
										
										read_last_word_latency += (current_time) - bank_reads[bank_bitmap2[i][y]][z].time;
									}
									//past_requests.push_back(bank_reads[i][0]);
									parity_hit++;
									bank_reads[bank_bitmap[i][n]].erase(bank_reads[bank_bitmap2[i][y]].begin() + z);
									parity_stall[parity_bitmap[i][n/2]] = 0;
								}
							}
						}
					}
				}
			}

			/* Make sure we didn't serve all the requests in the queue using the
			 * parity banks */
			if(bank_reads[i][0].critical == true) {
				read_cr_word_latency += (current_time) - bank_reads[i][0].time;
				fprintf(dump, "Delay: %d\t Address: %d 	Time: %d\n", current_time - bank_reads[i][0].time, bank_reads[i][0].address, current_time);
				//fprintf(dump, "Delay: %d	Address: %d	Time: %d\n", current_time - bank_reads[i][0].time, bank_reads[i][0].address, bank_reads[i][0].time);
			}
			if(bank_reads[i][0].last == true) {
				read_last_word_latency += (current_time) - bank_reads[i][0].time;
			}
			past_requests.push_back(bank_reads[i][0]);
			previously_read.push_back(bank_reads[i][0].address); //Record that the memory location is now coded
			bank_reads[i].erase(bank_reads[i].begin());
		}
		else if(!bank_busy[i] && bank_writes[i].size() != 0 && (current_time % MEM_DELAY) == 0) {
			/* First serve the request in the data bank */
			if(bank_writes[i][0].critical == true) {
				write_cr_word_latency += (current_time) - bank_writes[i][0].time;
			}
			if(bank_writes[i][0].last == true) {
				write_last_word_latency += (current_time) - bank_writes[i][0].time;
			}	
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
						parity_bank_num = 4;
						break;
					case 2:
						parity_bank_num = 5;
						break;
					case 3:
						parity_bank_num = 1;
						break;
					case 4:
						parity_bank_num = 6;
						break;
					case 5:
						parity_bank_num = 7;
						break;
					case 6:
						parity_bank_num = 3;
						break;
					case 7:
						parity_bank_num = 8;
						break;
					case 8:
						parity_bank_num = 2;
				}

				/* Make sure the bank isn't busy serving a read */
				if(parity_stall[parity_bank_num] == -1) {
					parity_stall[parity_bank_num] = 0; //Mark the bank as busy
					
					/* Serve the write the same way as above */
					if(bank_writes[i][0].critical == true) {
						write_cr_word_latency += (current_time) - bank_writes[i][0].time;
					}
					if(bank_writes[i][0].last == true) {
						write_last_word_latency += (current_time) - bank_writes[i][0].time;
					}	
					overwritten_parity_rows.push_back(bank_writes[i][0]);
					bank_writes[i].erase(bank_writes[i].begin());
				}
			}
		}
	}

	/* Set all the parity banks as free */
	for(int z = 0; z < 2; z++) {
		for(int i = 0; i < NUM_PARITY_BANKS; i++)
			parity_stall[z][i] = -1;
	}

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
						parity_stall[bank/4][parity_bitmap[bank % 4][n]] = 0;
					bank_busy[bank] = true;

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

int sc_main(int argc, char* argv[]) {

	/* Take in the ratio between processor and memory clock speeds */
	MEM_DELAY = atoi(argv[1]);
	MAX_LOOKAHEAD = atoi(argv[2]);
	WRITE_REPAIR_TIME = atoi(argv[3]);
	TRACE_LOCATION = argv[4];

	
	for(int z = 0; z < 2; z++) {
		for(int i = 0; i < NUM_PARITY_BANKS; i++)
			parity_stall[z][i] = -1;
	}

	/* First populate the request queues with all requests from banks */
	get_requests();
	previous_size = request_queue[0].size();

	/* Execute the main loop which will service all requests */
	while(!queue_empty()) {

		input_controller(request_queue);
		check_recode(); 
		access_scheduler();
		/* Reset the busy banks for the next memory cycle */
		for(int i = 0; i < NUM_BANKS; i++)
			bank_busy[i] = false;

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




















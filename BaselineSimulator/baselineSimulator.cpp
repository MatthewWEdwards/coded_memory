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
#define NUM_TRACES 6
#define WR_QUEUE_BUILDUP 100
#define CORE_QUEUE_MAX 8
#define MAX_BANK_QUEUE_LENGTH 10
string TRACE_LOCATION("/home/casen/Huawei/traces/LTE/dsp_0_trace.txt");
int MEM_DELAY; //Taken as an input parameter

/* Struct for the input requests from the processors */
typedef struct request {

	int address;
	int priority;
	int length;
	int time;
	int queue_time;
	int core_number;
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
	int length;
	int numServed;
	int orderNumber;
	int requestNumber;

} bank_request;

/* GLOBALS */
vector<bank_request> bank_reads[NUM_BANKS]; //Queue of read requests for each bank
vector<bank_request> bank_writes[NUM_BANKS]; //Queue of write requests for each bank
vector<request> request_queue[NUM_TRACES]; 
vector<request> core_queues[NUM_TRACES]; //These queues hold the requests from the cores
int current_time = 0; //Current time in ns
long long int read_cr_word_latency = 0;
long long int write_cr_word_latency = 0;
long long int read_last_word_latency = 0;
long long int write_last_word_latency = 0;
long long int num_reads = 0;
long long int num_writes = 0;
long long int reads_served_from_write = 0;
int mem_stall = 0;
int NUM_REQUESTS = 0;
unordered_map<int, bank_request> previously_served_reads;
unordered_map<int, bank_request> previously_served_writes;



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
			next_request.length = pending_requests[i].length;
			next_request.critical = false;
			next_request.orderNumber = n;
			next_request.requestNumber = NUM_REQUESTS;
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
		NUM_REQUESTS++;
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

void serve_request(bank_request request) {
	
	/* Determine if we're dealing with read or write */
	unordered_map<int, bank_request> *request_list;
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

	/* Serve a request from each bank */
	for(int i = 0; i < NUM_BANKS; i++) {


		/* Serve a request from the greater queue */
		if(bank_writes[i].size() < WR_QUEUE_BUILDUP && bank_reads[i].size() != 0 && (current_time % MEM_DELAY) == 0) {
			if(bank_reads[i][0].critical == true) {
				read_cr_word_latency += (current_time) - bank_reads[i][0].time;
				fprintf(dump, "Delay: %d	Address: %d	Time: %d\n", current_time - bank_reads[i][0].time, bank_reads[i][0].address, bank_reads[i][0].time);
			}
			serve_request(bank_reads[i][0]);
			bank_reads[i].erase(bank_reads[i].begin());
		}
		else if(bank_writes[i].size() != 0 && (current_time % MEM_DELAY) == 0) {
			if(bank_writes[i][0].critical == true) {
				write_cr_word_latency += (current_time) - bank_writes[i][0].time;
			}
			serve_request(bank_writes[i][0]);
			bank_writes[i].erase(bank_writes[i].begin());	
		}
	}
}





int sc_main(int argc, char* argv[]) {

	/* Take in the ratio between processor and memory */
	MEM_DELAY = atoi(argv[1]);

	/* First populate the request queues with all requests from banks */
	get_requests();
	previous_size = request_queue[0].size();

	/* Execute the main loop which will service all requests */
	while(!queue_empty()) {

		input_controller(request_queue);
		access_scheduler();

		current_time += 1; //Cycle the clock 
		if(current_time % 5000 == 0)
			cout << current_time << endl;
	}

	/* Dispaly the results */
	cout << "Number of reads: " << num_reads << " Number of writes: " << num_writes << endl;
	cout << "Average read critical word latency: " << (float) read_cr_word_latency/num_reads << endl;
	cout << "Average read last word latency: " << (float) read_last_word_latency/num_reads << endl;
	cout << "Average write critical word latency: " << (float) write_cr_word_latency/num_writes << endl;
	cout << "Average write last word latency: " << (float) write_last_word_latency/num_writes << endl;
	cout << "Reads served from write queue: " << reads_served_from_write << endl;

	cout << "Total time: " << current_time << endl;

	FILE* results = fopen("baseline_results.txt", "a");
	fprintf(results, "%d\t%f\t%f\t%f\t%d\n", MEM_DELAY, (float)read_cr_word_latency/num_reads, (float)read_last_word_latency/num_reads, (float)write_last_word_latency/num_writes, current_time);

	fclose(results);
	fclose(dump);
}




















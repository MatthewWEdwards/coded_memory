#include "systemc.h"
#include <string>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream> 

using namespace std;


/* Some definitions to change simulation parameters */
#define NUM_BANKS 8
#define NUM_TRACES 6
#define MEM_DELAY 3
string TRACE_LOCATION("/home/casen/Huawei/traces/LTE/dsp_0_trace.txt");


/* Struct for the input requests from the processors */
typedef struct request {

	int address;
	int priority;
	int length;
	int time;
	bool read;

} request;

/* Struct for the queue request */
typedef struct bank_request {

	int address;
	int priority;
	int time;
	bool read;
	bool critical;
	bool last;

} bank_request;

/* GLOBALS */
vector<bank_request> bank_reads[NUM_BANKS]; //Queue of read requests for each bank
vector<bank_request> bank_writes[NUM_BANKS]; //Queue of write requests for each bank
vector<request> request_queue[NUM_TRACES]; 
int current_time = 0; //Current time in ns
long long int critical_word_latency = 0;
long long int last_word_latency = 0;
long long int num_requests = 0;
int mem_stall[NUM_BANKS];


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
	temp.address = strtoul(&token[7][7], NULL, 16); //Convert hex input string to an address
	temp.priority = strtoul(&token[8][6], NULL, 10); //Determine priority
	temp.length = atoi(&token[5][6]) + 1; //Length of request. Add one to convert from hex
	temp.time = atoi(&token[0][5]); //Grab the time of the request
	if(token[2].compare("RNW:Read") == 0) //Determine if it is a read or write
		temp.read = true;
	else
		temp.read = false;

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
			request_queue[i].push_back(current_request);
			num_requests++;
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
	vector<request> temp_requests;
	bool roll_back_time = false;
	for(int i = 0; i < NUM_TRACES; i++) {

		//cout << "Request: " << request_queue[i][0].time << endl;
		/* If it's time to serve the request, add it to the pending request queue */
		if(request_queue[i][0].time <= current_time && !request_queue[i].empty()) {
			temp_requests.push_back(request_queue[i][0]);
			request_queue[i].erase(request_queue[i].begin()); //Remove the request from the queue
			//roll_back_time = true;
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

	/* Now distribute the requests to the banks */
	for(int i = 0; i < pending_requests.size(); i++) {

		/* First create the first queue object that will populate the bank queues */
		bank_request next_request;
		next_request.time = pending_requests[0].time;
		next_request.critical = true;
		next_request.last = false;

		/* Determine which bank it falls into */
		int bank = (pending_requests[0].address/32) % NUM_BANKS;

		/* Determine if read/write queue entry */
		if(pending_requests[0].read)
			bank_reads[bank].push_back(next_request);
		else
			bank_writes[bank].push_back(next_request);

		/* Now populate the next bank queues */
		for(int n = 1; n < pending_requests[0].length; n++) {

			/* Update the next bank and make the next bank queue object */
			if((++bank) >= NUM_BANKS)
				bank = 0;
			next_request.time = pending_requests[0].time;			
			next_request.read = pending_requests[0].read;
			next_request.critical = false;
			if(n == pending_requests[0].length - 1)
				next_request.last = true;
			else
				next_request.last = false;

			/* Determine if read/write queue entry */
			if(pending_requests[0].read)
				bank_reads[bank].push_back(next_request);
			else
				bank_writes[bank].push_back(next_request);
		}
	}

	if(roll_back_time == true)
		current_time--;
}



void access_scheduler() {

	/* Serve a request from each bank */
	for(int i = 0; i < NUM_BANKS; i++) {

		/* Serve a request from the greater queue */
		if(bank_reads[i].size() >= bank_writes[i].size() && bank_reads[i].size() != 0 && mem_stall[i] == -1) {
			if(bank_reads[i][0].critical == true) {
				critical_word_latency += (current_time+0) - bank_reads[i][0].time;
			}
			if(bank_reads[i][0].last == true) {
				last_word_latency += (current_time+0) - bank_reads[i][0].time;
			}
			bank_reads[i].erase(bank_reads[i].begin());
			mem_stall[i] = 0;
		}
		else if(bank_writes[i].size() > bank_reads[i].size() && mem_stall[i] == -1) {
			if(bank_reads[i][0].critical == true) {
				critical_word_latency += (current_time+0) - bank_writes[i][0].time;
			}
			if(bank_reads[i][0].last == true) {
				last_word_latency += (current_time+0) - bank_writes[i][0].time;
			}	
			bank_writes[i].erase(bank_writes[i].begin());	
			mem_stall[i] = 0;
		}
	}

	/* Add one clock cycle for each bank latency */
	for(int i = 0; i < NUM_BANKS; i++) {

		/* Reset the counter if the latency is reached */
		if(mem_stall[i] == MEM_DELAY)
			mem_stall[i] = -1;
		/* Add one to the clock cycle if the memory is stalling */
		if(mem_stall[i] >= 0)
			mem_stall[i]++;
	}
}


int sc_main(int argc, char* argv[]) {

	/* Make sure the stalls start at 0 */
	for(int i = 0; i < NUM_BANKS; i++) {
		mem_stall[i] = 0;
	}

	/* First populate the request queues with all requests from banks */
	get_requests();
	previous_size = request_queue[0].size();

	/* Execute the main loop which will service all requests */
	while(!queue_empty()) {

		input_controller(request_queue);
		access_scheduler();

		current_time += 1; //Cycle the clock 
		cout << current_time << endl;
	}

	/* Dispaly the results */
	cout << "Average critical word latency: " << critical_word_latency/num_requests << endl;
	cout << "Average last word latency: " << last_word_latency/num_requests << endl;	
}




















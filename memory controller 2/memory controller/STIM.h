#pragma once
#include "systemc"
#include <iostream>
#include <string>
#include "struct.h"
#include "variable.h"
using namespace sc_core;

//void get_requests();
core_request parse_input(string input_command);


SC_MODULE(STIM)
{
	vector<core_request> stim_queue[NUM_TRACES];


	vector<core_request>::iterator b[NUM_TRACES];
	vector<core_request>::iterator e[NUM_TRACES];


	//port
	sc_in<bool> clk;
	sc_fifo_out<core_request> request_out;
	

	SC_CTOR(STIM) {

		SC_METHOD(action);
		sensitive << clk.pos();

		//get_requests();
		//construct these input information vector
		ifstream inputFile;
		stringstream input(TRACE_LOCATION);
		string s;

		for (int i = 0; i < NUM_TRACES; i++) {

			/* Open the file */
			string command;
			/*cout << "TRACE_LOCATION" << TRACE_LOCATION << endl;
			cout << ".c_str()" << TRACE_LOCATION.c_str() <<endl;*/
			//inputFile.open(TRACE_LOCATION.c_str());
			//cout << "read in file" << endl;
			 getline(input, s, ' ');
			inputFile.open(s);
			cout << "filename" << s<<endl;
			if (!inputFile.is_open()) {
				cout << "ERROR OPENING INPUT FILE. SIMULATION HALTING\n";
				exit(-1);
			}
			

			/* Read the file */
			while (getline(inputFile, command)) {
				core_request current_request = parse_input(command);
				current_request.core_number = i;
				stim_queue[i].push_back(current_request);
			}

			/* Move to the next file name */
			inputFile.close();


			//initialize the iterator
			for (int i = 0; i < NUM_TRACES; i++)
			{
				b[i] = stim_queue[i].begin();
				e[i] = stim_queue[i].end();
			}
		
		}

		/* Finally determine the range of memory (for dynamic coding) */
		//region_size = ((highAddress - lowAddress) + (NUM_REGIONS/2))/NUM_REGIONS; 
		//region_size = (highAddress - lowAddress) / NUM_REGIONS;
		
		/*auto b = core_queue[0].begin();
		auto e = core_queue[0].end();
		int j = 0;
		do
		{b = core_queue[j].begin();
			e = core_queue[j].end();
			for (int i = 0; i < 20; i++)
			{
				cout << "address" << b->address << "core_number" << b->core_number << "id" << b->id << "length" << b->length << "time<<" << b->time << "W/R" << b->read << endl;
				b++;
			}
			j++;
			

		} while (j<6);
		*/
		
	}

	void action(void);   //output the request on correct time
};


#pragma once
#include "systemc.h"
#include <iostream>
#include <fstream>
#include <string>
#include "struct.h"
#include "variable.h"
#include <deque>

#define NUM_ROWS 0x400000

#define prewrite 3
#define coded 0
#define direct 2
#define invalid 1

SC_MODULE(ACCSDULER){
	sc_fifo_in<queue_request> read_bankin[NUM_CHANNELS];

	sc_fifo_in<queue_request> write_bankin[NUM_CHANNELS];

	sc_in<bool> schclk;

	//vector<deque<list_request>*> readlist[NUM_CHANNELS];
	//vector<deque<list_request>*> writelist[NUM_CHANNELS];

	deque<list_request> readlist[NUM_CHANNELS];
	deque<list_request> writelist[NUM_CHANNELS];

	int current_cycle;		//indicate if current cycle is read(TRUE) or write(FALSE)
	int next_cycle;		//indicate if current cycle is read(TRUE) or write(FALSE), or not determined

	vector<int> read_prio;// (NUM_CHANNELS, 0);
	vector<int> write_prio;// (NUM_CHANNELS, 0);
	
	
	
	vector<list_request>* served_request;		//request which have been served in current cycle, this vector will be cleaned in the end of this cycle

	SC_CTOR(ACCSDULER){
		SC_METHOD(scheduler_action);
		sensitive << schclk.pos();


		ofstream myfile;

		myfile.open("dump file 0.txt");
		myfile.close();
		myfile.open("dump file 1.txt");
		myfile.close();
		myfile.open("dump file 2.txt");
		myfile.close();
		myfile.open("dump file 3.txt");
		myfile.close();
		myfile.open("dump file 4.txt");
		myfile.close();
		myfile.open("dump file 5.txt");
		myfile.close();
		myfile.open("dump file 6.txt");
		myfile.close();

	/*	myfile.open("bank 0.txt");
		myfile.close();
		myfile.open("bank 1.txt");
		myfile.close();
		myfile.open("bank 2.txt");
		myfile.close();
		myfile.open("bank 3.txt");
		myfile.close();
		myfile.open("bank 4.txt");
		myfile.close();
		myfile.open("bank 5.txt");
		myfile.close();
		myfile.open("bank 6.txt");
		myfile.close();
		myfile.open("bank 7.txt");
		myfile.close();*/


		//initial the priority of four type of cycle
		read_prio = vector<int>(NUM_CHANNELS, 0);
		write_prio = vector<int>(NUM_CHANNELS, 0);



		current_cycle =0;//start with tollread cycle
		next_cycle = 0;
		for (int i = 0; i < NUM_CHANNELS; i++)
		{
			readlist[i] = deque<list_request>();
			writelist[i] = deque<list_request>();
			/*
			for (int j = 0; j < NUM_BANKS; j++){
				(readlist[i])[j] = new deque < list_request >() ;
				(writelist[i])[j] = new deque < list_request >() ;
			}*/
			
		}

		//served_request = new vector<list_request>;

	}

	virtual ~ACCSDULER(){
		
		delete served_request;
		
		/*for (int i = 0; i < NUM_CHANNELS; i++)
		{
			delete readlist[i];
			
			delete writelist[i];
		}*/

		/*for (int  i = 0; i < NUM_PARITY_BANKS; i++)
		{
			delete invalid_parity[i];

		}
	*/
	}


	void scheduler_action(void);

	void test_action(void);

	void channelserve(int channelID);

	/*member function used to recover available invalid parity bank data*/
	/*void recover_parity(int parity_bankid);

	bool check_first_bank(int parity_id, int data_id);
	bool check_second_bank(int parity_id, int data_id);

	int tollreadserve(void);
	int tollwriteserve(void);
	int localreadserve(void);
	int localwriteserve(void);
	*/
};

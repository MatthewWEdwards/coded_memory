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

extern int codingstatusmap[NUM_ROWS][NUM_PARITY_BANKS];

SC_MODULE(ACCSDULER){
	sc_fifo_in<queue_request> tollread_bankin[NUM_BANKS];
	sc_fifo_in<queue_request> localread_bankin[NUM_BANKS];
	sc_fifo_in<queue_request> tollwrite_bankin[NUM_BANKS];
	sc_fifo_in<queue_request> localwrite_bankin[NUM_BANKS];
	sc_in<bool> schclk;

	/*list<queue_request>* readlist[NUM_BANKS];
	list<queue_request>* writelist[NUM_BANKS];
*/
	deque<list_request>* tollreadlist[NUM_BANKS];
	deque<list_request>* tollwritelist[NUM_BANKS];

	deque<list_request>* localreadlist[NUM_BANKS];
	deque<list_request>* localwritelist[NUM_BANKS];

	int current_cycle;		//indicate if current cycle is read(TRUE) or write(FALSE)
	int next_cycle;		//indicate if current cycle is read(TRUE) or write(FALSE), or not determined

	int tollread_prio = 0;
	int tollwrite_prio = 0;
	int localread_prio = 0;
	int localwrite_prio = 0;
	
	
	vector<list_request>* served_request;		//request which have been served in current cycle, this vector will be cleaned in the end of this cycle

	vector<invalid_record>* invalid_parity[NUM_PARITY_BANKS];//save invalid data in each parity bank, waiting for repair

	SC_CTOR(ACCSDULER){
		SC_METHOD(scheduler_action);
		sensitive << schclk.pos();
		//sensitive << schclk.neg();
	
		/*SC_METHOD(test_action);
		sensitive << schclk.pos();*/

		for (int k = 0; k < NUM_PARITY_BANKS; k++)
		{
			codingstatusmap[0][k] = invalid;
		}		

		ofstream myfile;
		
		//codingstatusmap[NUM_ROWS][NUM_PARITY_BANKS] = { 0 };
		
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
		tollread_prio = 0;
		tollwrite_prio = 0;
		localread_prio = 0;
		localwrite_prio = 0;


		current_cycle =0;//start with tollread cycle
		next_cycle = 0;
		for (int i = 0; i < NUM_BANKS; i++)
		{
			tollreadlist[i] = new deque<list_request>();
			localreadlist[i] = new deque<list_request>();
			tollwritelist[i] = new deque<list_request>();
			localwritelist[i] = new deque<list_request>();
		}

		served_request = new vector<list_request>;

		for (int i = 0; i < NUM_PARITY_BANKS; i++)
		{
			invalid_parity[i] = new vector<invalid_record>;
		}
	}

	virtual ~ACCSDULER(){
		
		delete served_request;

		for (int i = 0; i < NUM_BANKS; i++)
		{
			delete tollreadlist[i];
			delete localreadlist[i];
			delete localwritelist[i];
			delete tollwritelist[i];
		}

		for (int  i = 0; i < NUM_PARITY_BANKS; i++)
		{
			delete invalid_parity[i];

		}
	
	}


	void scheduler_action(void);

	void test_action(void);


	/*member function used to recover available invalid parity bank data*/
	void recover_parity(int parity_bankid);

	bool check_first_bank(int parity_id, int data_id);
	bool check_second_bank(int parity_id, int data_id);

	int tollreadserve(void);
	int tollwriteserve(void);
	int localreadserve(void);
	int localwriteserve(void);
};

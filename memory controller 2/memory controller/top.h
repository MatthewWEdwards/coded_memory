#pragma once
#include "systemc"
#include "STIM.h"
#include "core_arbiter.h"
#include "struct.h"
#include "acc_scheduler.h"
//#include "memcontroller.h"
using namespace sc_core;

SC_MODULE(TOP){

	//channel signals
	sc_fifo<core_request> core_queue;
	sc_clock testclk;

	sc_fifo<queue_request>* bank_toll_read_queue[NUM_BANKS];					//haven't indentify the depth of read and write queue yet
	sc_fifo<queue_request>* bank_toll_write_queue[NUM_BANKS];

	sc_fifo<queue_request>* bank_local_read_queue[NUM_BANKS];
	sc_fifo<queue_request>* bank_local_write_queue[NUM_BANKS];
	/*sc_buffer<queue_request> bank_read_queue[NUM_BANKS];
	sc_buffer<queue_request> bank_write_queue[NUM_BANKS];*/


	

	//models
	STIM* stim;
	core_arbiter* arbiter;
	ACCSDULER* scheduler;


	//
	SC_CTOR(TOP):testclk("testclk",1,SC_NS),core_queue("core_queue",16) {

		for (int i = 0; i < NUM_BANKS; i++)
		{
			bank_toll_read_queue[i] = new sc_fifo<queue_request>("toll read_queue", 32);
			bank_local_read_queue[i] = new sc_fifo<queue_request>("local read_queue", 32);
			bank_toll_write_queue[i] = new sc_fifo<queue_request>("toll write_queue", 32);
			bank_local_write_queue[i] = new sc_fifo<queue_request>("local write_queue",32);	//NAME WILL CAUSE WARING BUT DOESN'T MATTER
		}
	

		stim = new STIM("stim");
		arbiter= new core_arbiter("arbiter");
		scheduler = new ACCSDULER("scheduler");

		stim->clk.bind(testclk);
		stim->request_out(core_queue);

		arbiter->arbiter_clk(testclk);
		arbiter->request_in(core_queue);
		
		scheduler->schclk(testclk);

		for (int i = 0; i < NUM_BANKS; i++)
		{
			arbiter->arbiter_local_read_out[i](*bank_local_read_queue[i]);
			arbiter->arbiter_local_write_out[i].bind(*bank_local_write_queue[i]);
			arbiter->arbiter_toll_read_out[i](*bank_toll_read_queue[i]);
			arbiter->arbiter_toll_write_out[i].bind(*bank_toll_write_queue[i]);
			

			scheduler->localread_bankin[i](*bank_local_read_queue[i]);
			scheduler->localwrite_bankin[i](*bank_local_write_queue[i]);
			scheduler->tollread_bankin[i](*bank_toll_read_queue[i]);
			scheduler->tollwrite_bankin[i](*bank_toll_write_queue[i]);
		}


		
	
	}


};
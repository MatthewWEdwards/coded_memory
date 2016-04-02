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

	sc_fifo<queue_request>* bank_read_queue[NUM_CHANNELS];					//haven't indentify the depth of read and write queue yet
	sc_fifo<queue_request>* bank_write_queue[NUM_CHANNELS];

	//sc_fifo<queue_request>* bank_local_read_queue[NUM_CHANNELS];
	//sc_fifo<queue_request>* bank_local_write_queue[NUM_CHANNELS];
	/*sc_buffer<queue_request> bank_read_queue[NUM_BANKS];
	sc_buffer<queue_request> bank_write_queue[NUM_BANKS];*/


	

	//models
	STIM* stim;
	core_arbiter* arbiter;
	ACCSDULER* scheduler;


	//
	SC_CTOR(TOP):testclk("testclk",1,SC_NS),core_queue("core_queue",16) {

		for (int i = 0; i < NUM_CHANNELS; i++)
		{
			bank_read_queue[i] = new sc_fifo<queue_request>("read_queue", 32);
			bank_write_queue[i] = new sc_fifo<queue_request>("read_queue", 32);
				//NAME WILL CAUSE WARING BUT DOESN'T MATTER
		}
		//cout <<"in constructor"<< bank_read_queue[0]->num_free()<<endl;
		//assert(false);
		stim = new STIM("stim");
		arbiter= new core_arbiter("arbiter");
		scheduler = new ACCSDULER("scheduler");

		stim->clk.bind(testclk);
		stim->request_out(core_queue);

		arbiter->arbiter_clk(testclk);
		arbiter->request_in(core_queue);
		
		scheduler->schclk(testclk);

		for (int i = 0; i < NUM_CHANNELS; i++)
		{
			arbiter->arbiter_read_out[i].bind(*bank_read_queue[i]);
			arbiter->arbiter_write_out[i].bind(*bank_write_queue[i]);
			
			

			scheduler->read_bankin[i].bind(*bank_read_queue[i]);
			scheduler->write_bankin[i].bind(*bank_write_queue[i]);
			
		}


		
	
	}


};
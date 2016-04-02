#include "systemc"
#include "struct.h"
#include "variable.h"

using namespace std;
using namespace sc_core;

SC_MODULE(core_arbiter){
	
	sc_in<bool> arbiter_clk;
	
	sc_fifo_in<core_request> request_in;
	sc_fifo_out<queue_request> arbiter_toll_read_out[NUM_BANKS];
	sc_fifo_out<queue_request> arbiter_local_read_out[NUM_BANKS];

	sc_fifo_out<queue_request> arbiter_toll_write_out[NUM_BANKS];
	sc_fifo_out<queue_request> arbiter_local_write_out[NUM_BANKS];

	//haven't decided yet
	vector<queue_request> temp_store;//temp store the read in data from request_in


	SC_CTOR(core_arbiter){
		SC_METHOD(action);
		sensitive << arbiter_clk.pos();
		sensitive<< arbiter_clk.neg();

		/*ofstream myfile;
		myfile.open("bank in 0.txt");
		myfile.close();
		myfile.open("bank in 1.txt");
		myfile.close();
		myfile.open("bank in 2.txt");
		myfile.close();
		myfile.open("bank in 3.txt");
		myfile.close();
		myfile.open("bank in 4.txt");
		myfile.close();
		myfile.open("bank in 5.txt");
		myfile.close();
		myfile.open("bank in 6.txt");
		myfile.close();
		myfile.open("bank in 7.txt");
		myfile.close();*/



	}

	void action(void);
};
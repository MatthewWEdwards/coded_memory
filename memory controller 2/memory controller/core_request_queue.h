#include "systemc"
#include "struct.h"
#include "variable.h"
using namespace std;
using namespace sc_core;

SC_MODULE(core_request_queue){

	sc_clock testclk;
	sc_fifo<core_request> core_fifo[NUM_TRACES];
}
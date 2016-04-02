#pragma once
#include <string>
#include "struct.h"
using namespace std;

//const int NUM_BANKS = 8;
//const int NUM_PARITY_BANKS = 6;
//const int NUM_TRACES = 6; //number of cores
const int WR_QUEUE_BUILDUP = 10;
const int CORE_QUEUE_MAX = 8;
const int MAX_BANK_QUEUE_LENGTH = 10;
const int NUM_REGIONS = 8;
const int NUM_ACTIVE_REGIONS = 3;
//const int BANK_FREE = 0;
//const int BANK_BUSY = 1;

int MEM_DELAY;
int MAX_LOOKAHEAD;
int WRITE_REPAIR_TIME;
string TRACE_LOCATION;//("../traces/LTE/dsp_0_trace.txt");

std::ostream& operator << (std::ostream& a, const core_request& aa) {
	a << "address" << std::hex << aa.address;
	a << " id" << aa.id;
	a << " time" << std::dec << aa.time;
	a << " core_number" << aa.core_number;
	a << " length" << aa.length;

	return a;
	//return a;
};

std::ostream& operator << (std::ostream& a, const queue_request& aa) {
	a << "address" << std::hex << aa.addr;
	a << " id" << aa.id;
	a << " valid" << aa.valid;
	a << " core_number" << aa.core_number;
	//a << " length" << aa.length;

	return a;
	//return a;
};

ostream& operator<<(ostream& old, const list_request& that){
	//old << endl;
	old << "addr:   " << std::hex << that.addr << '\t';
	old << "row addr;  " << std::hex << that.row_addr << '\t';
	old << "critical: " << that.critical << '\t';
	old << "time:   " <<std::dec << that.time << endl;

	return old;
}

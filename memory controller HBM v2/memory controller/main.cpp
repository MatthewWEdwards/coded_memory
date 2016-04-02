#include "systemc"
#include <iostream>
#include "top.h"

#include "variable.h"
using namespace std;
using namespace sc_core;

//#include "head.h"

/* Some definitions to change simulation parameters */
//#define NUM_BANKS 8
//#define NUM_PARITY_BANKS 6 
//#define NUM_TRACES 6 //number of cores
//#define WR_QUEUE_BUILDUP 10
//#define CORE_QUEUE_MAX 8
//#define MAX_BANK_QUEUE_LENGTH 10
//#define NUM_REGIONS 8
//#define NUM_ACTIVE_REGIONS 3
//#define BANK_FREE 0
//#define BANK_BUSY 1
//string TRACE_LOCATION;//("../traces/LTE/dsp_0_trace.txt");

/* Input Parameters */
extern int MEM_DELAY;
extern int MAX_LOOKAHEAD;
extern int WRITE_REPAIR_TIME;
extern string TRACE_LOCATION;//("../traces/LTE/dsp_0_trace.txt");

extern long double critical_time;
extern int critical_count;
extern long double transactional_time;
extern int transactional_count;

extern long double write_time;
extern int write_count;

//
//extern long double critical_time;
//extern int critical_count;
//extern long double tolltransactional_time;
//extern int tolltransactional_count;
//
//extern long double localcritical_time;
//extern int localcritical_count;
//extern long double localtransactional_time;
//extern int localtransactional_count;
//
//extern long double localwrite_time;
//extern int localwrite_count;
//extern long double tollwrite_time;
//extern int tollwrite_count;
extern int bank_frequency;

int sc_main(int argc, char* argv[]){
	MEM_DELAY = 2;// 
	bank_frequency = 1;// atoi(argv[1]);
	MAX_LOOKAHEAD = 4;// atoi(argv[2]);
	WRITE_REPAIR_TIME = 2;// atoi(argv[3]);
	
	
	//TRACE_LOCATION = "../traces/trace4/dsp_0_trace.txt ../traces/trace4/dsp_1_trace.txt ../traces/trace4/dsp_2_trace.txt ../traces/trace4/dsp_3_trace.txt ../traces/trace4/dsp_4_trace.txt ../traces/trace4/dsp_5_trace.txt";// argv[4];

	TRACE_LOCATION = "../traces/trace2/dsp_0_trace.txt ../traces/trace2/dsp_1_trace.txt ../traces/trace2/dsp_2_trace.txt ../traces/trace2/dsp_3_trace.txt ../traces/trace2/dsp_4_trace.txt ../traces/trace2/dsp_5_trace.txt";// argv[4];

	//TRACE_LOCATION = "../traces/trace3/dsp_0_trace.txt ../traces/trace3/dsp_1_trace.txt ../traces/trace3/dsp_2_trace.txt ../traces/trace3/dsp_3_trace.txt ../traces/trace3/dsp_4_trace.txt ../traces/trace3/dsp_5_trace.txt";// argv[4];



	//TRACE_LOCATION = "../traces/case4/dsp_0_trace.txt ../traces/case4/dsp_1_trace.txt ../traces/case4/dsp_2_trace.txt ../traces/case4/dsp_3_trace.txt ../traces/case4/dsp_4_trace.txt ../traces/case4/dsp_5_trace.txt";// argv[4];

	//TRACE_LOCATION = "../traces/trace1/dsp_0_trace.txt ../traces/trace1/dsp_1_trace.txt ../traces/trace1/dsp_2_trace.txt ../traces/trace1/dsp_3_trace.txt ../traces/trace1/dsp_4_trace.txt ../traces/trace1/dsp_5_trace.txt";// argv[4];
	//TRACE_LOCATION = "../traces/UMTS/dsp_0_trace.txt ../traces/UMTS/dsp_1_trace.txt ../traces/UMTS/dsp_2_trace.txt ../traces/UMTS/dsp_3_trace.txt ../traces/UMTS/dsp_4_trace.txt ../traces/UMTS/dsp_5_trace.txt";// argv[4];	
	//TRACE_LOCATION = "../traces/LTE/dsp_0_trace.txt ../traces/LTE/dsp_1_trace.txt ../traces/LTE/dsp_2_trace.txt ../traces/LTE/dsp_3_trace.txt ../traces/LTE/dsp_4_trace.txt ../traces/LTE/dsp_5_trace.txt";// argv[4];
	//TRACE_LOCATION = "../traces/LTE/dsp_0.txt ../traces/LTE/dsp_1.txt ../traces/LTE/dsp_2.txt ../traces/LTE/dsp_3.txt ../traces/LTE/dsp_4.txt ../traces/LTE/dsp_5.txt";
	

	TOP top1("top1");
	cout << "hello word" <<endl;
	sc_core::sc_start();
	cout << "LTE" << endl;
	cout << "bank frequency" << '\t' << bank_frequency << endl;
	
	cout << "execution time" << '\t' << sc_time_stamp()<<endl;
	
	
	/*cout << "toll critical read count" << '\t' <<tollcritical_count << endl;
	cout << "toll transactional read count" << '\t' << tolltransactional_count << endl;
	cout << "toll critical read latency(ns)" << '\t' << tollcritical_time / tollcritical_count << endl;
	cout << "toll transactional read latency(ns)" << '\t' << tolltransactional_time / tolltransactional_count << endl;

	cout << "local critical read count" << '\t' << localcritical_count << endl;
	cout << "local transactional read count" << '\t' << localtransactional_count << endl;
	cout << "local critical read latency(ns)" << '\t' << localcritical_time / localcritical_count << endl;
	cout << "local transactional read latency(ns)" << '\t' << localtransactional_time / localtransactional_count << endl;

	cout << "toll write count" << '\t' << tollwrite_count<<endl;
	cout << "toll write latency(ns)" << '\t' << (tollwrite_time / tollwrite_count)+3 << endl;

	cout << "local write time" << '\t' << localwrite_time << endl;
	cout << "local write count" << '\t' << localwrite_count << endl; 
	cout << "local write latency(ns)" << '\t' << (localwrite_time / localwrite_count) << endl;*/

	/************IN TOTAL**********************/
	cout << "critial count" << '\t' << critical_count << endl;
	cout << "transactional_count" << '\t' << transactional_count << endl;
	cout << "critical latency(ns)" <<'\t'<< (critical_time ) / (critical_count) << endl;
	cout << "transactional latency(ns)" << '\t' << (transactional_time) / (transactional_count) << endl;
	cout << endl;
	cout << "read latency(ns)" << '\t' << (transactional_time + critical_time) / (transactional_count + critical_count) << endl;
	cout << "write latency" << '\t' << (write_time) / (write_count)+3 << endl;
	return 0;
}
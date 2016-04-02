#include <string>
using namespace std;
#define NUM_TRACES 6
#define NUM_BANKS 8
#define NUM_PARITY_BANKS 12
#define BANK_FREE 0
#define BANK_BUSY 1
//extern const int NUM_BANKS;
//extern const int NUM_PARITY_BANKS;

//extern const int NUM_TRACES; //number of cores
extern const int WR_QUEUE_BUILDUP;
extern const int CORE_QUEUE_MAX;
extern const int MAX_BANK_QUEUE_LENGTH ;
extern const int NUM_REGIONS ;
extern const int NUM_ACTIVE_REGIONS;
//extern const int BANK_FREE ;
//extern const int BANK_BUSY;

extern int MEM_DELAY;
extern int MAX_LOOKAHEAD;
extern int WRITE_REPAIR_TIME;
extern string TRACE_LOCATION;//("../traces/LTE/dsp_0_trace.txt");

std::ostream& operator << (std::ostream& a, const core_request& aa);
std::ostream& operator << (std::ostream& a, const queue_request& aa);
ostream& operator<<(ostream& old, const list_request& that);
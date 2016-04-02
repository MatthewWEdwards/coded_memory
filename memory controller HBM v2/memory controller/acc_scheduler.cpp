#include "acc_scheduler.h"
#include "variable.h"
#include <valarray>

const unsigned int listmaxsize = 8;
int dataBankStatus[NUM_CHANNELS][NUM_BANKS];		//free or busy
int dataBankStatusConuter[NUM_CHANNELS][NUM_BANKS];		//tells how many memory cycles the bank will remain busy
int dataBankActivatedRow[NUM_CHANNELS][NUM_BANKS];														// increment by precharge and row activation
//PRECHARGE TAKES 3 CYCLE, ROW A
//CTIVATION TAKES 3 CYCLES
//READ AND WRITE TAKES ONE CYCLE

//int parityBankStatus[NUM_PARITY_BANKS];

//int codingstatusmap[NUM_ROWS][NUM_PARITY_BANKS];


long double critical_time;
int critical_count;
long double transactional_time;
int transactional_count;




long double write_time;
int write_count;



int bank_frequency;//ratio = memory period/core period

#define read true
#define write false

//different cycle name and value
#define tollread 0
#define tollwrite 1
#define localread 2
#define localwrite 3

/*
calculate the paritybank number 

*/

void record_bankout(int bankid, queue_request& bankre){
	ofstream myfile;
	switch (bankid)
	{
	case 0:myfile.open("bank 0.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	case 1:myfile.open("bank 1.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	case 2:myfile.open("bank 2.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	case 3:myfile.open("bank 3.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	case 4:myfile.open("bank 4.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	case 5:myfile.open("bank 5.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	case 6:myfile.open("bank 6.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	case 7:myfile.open("bank 7.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	default:
		break;
	}

}

int corresponding_bank(int x, int y){
	if (x>y) {
		int t = x;
		x = y; y = t;
	}
	//x<y
	if (x == y){
		cout << "error";
		assert(0);
	}
	if (y - x > 3) assert(false);
	//x,y should be in the same region

	switch (x)
	{
	case 0:return y-1; break;
	case 1:return y + 1; break;
	case 2: return y + 2; break;
	case 4:return y + 1; break;
	case 5:return y + 3; break;
	case 6:return y + 4; break;
	default:
		cout << "x,y" << x << "  " << y<<endl;
		assert(false);
		break;
	}
};


//set status of all banks to free, when all actions have been taken, cycle ends
void free_all_banks(void);

int trans2list(sc_fifo_in<queue_request>& inport, deque<list_request>* list, int cycle_priority);

int find_max(valarray<int> array, int start,int end){
	int max=start;
	for (int i = start; i < end; i++)
	{
		if (array[i]>array[max]) max = i;
	}

	return max;
};



int find_first_bank(int parity_id);
int find_second_bank(int parity_id);

//
//void ACCSDULER::test_action(void){
//
//	//cout << sc_time_stamp() << endl;
//	queue_request tollread_r;
//	queue_request tollwrite_r;
//	queue_request localread_r;
//	queue_request localwrite_r;
//	
//	queue_request readinfo;
//
//	bool successread;
//	for (int i = 0; i < NUM_BANKS; i++)
//	{
//		//local read
//		successread = localread_bankin[i]->nb_read(readinfo);
//		if (successread){
//			if (readinfo.time - localread_r.time < -10) {
//				assert(false);
//			}
//		}
//		localread_r = readinfo;
//		
//
//	//local write
//	successread = localwrite_bankin[i]->nb_read(readinfo);
//	if (successread){
//		if (readinfo.time - localwrite_r.time < -10) {
//			assert(false);
//		}
//	}
//	localwrite_r = readinfo;
//	
//	//toll read
//	successread = tollread_bankin[i]->nb_read(readinfo);
//	if (successread){
//		if (readinfo.time - tollread_r.time < -10) {
//			assert(false);
//		}
//	}
//	tollread_r = readinfo;
//	
//	//toll write
//
//	successread = tollwrite_bankin[i]->nb_read(readinfo);
//	if (successread){
//		if (readinfo.time - tollwrite_r.time < -10) {
//			assert(false);
//		}
//	}
//	tollwrite_r = readinfo;
//	}
//};
//

void ACCSDULER::channelserve(int channelID){
	//cout << read_prio[channelID] << endl;
	//cout << &(readlist[channelID]) << endl;

	queue_request readinfo;
	//cout << read_bankin[channelID]->nb_read(readinfo) << endl;
	

	read_prio[channelID] = trans2list(read_bankin[channelID], &(readlist[channelID]), read_prio[channelID]);
	write_prio[channelID] = trans2list(write_bankin[channelID], &writelist[channelID], write_prio[channelID]);
	//cout << "Channel ID " << channelID << "in channel serve" << endl;
	
	if (read_prio[channelID] > write_prio[channelID]){
		read_prio[channelID] /= 2;
		//read cycle
		auto bi = readlist[channelID].begin();
		if (bi == readlist[channelID].end()) return;
		while ( dataBankStatus[channelID][bi->bank_id] == BANK_BUSY){
			bi++;
			if (bi == readlist[channelID].end()) return;
		}//wait to bank free request
		
		if (bi->critical == 1){
			critical_count++;
			sc_time ctime = sc_time_stamp();

			if (dataBankActivatedRow[channelID][bi->bank_id] == (bi->row_addr >> 7)){
				dataBankStatusConuter[channelID][bi->bank_id] += 1;
				critical_time += ctime.to_default_time_units() - bi->time + bank_frequency;
			}
			else{
				dataBankActivatedRow[channelID][bi->bank_id] = (bi->row_addr >> 7);
				dataBankStatusConuter[channelID][bi->bank_id] += 6;
				critical_time += ctime.to_default_time_units() - bi->time + bank_frequency*6;
			}
		//myfile <<"toll critical data read: issue time" << '\t' << bi->time << "serve time" << '\t' << ctime << endl;
		}
		else
		{			
		transactional_count++;
		sc_time ctime = sc_time_stamp();
		if (dataBankActivatedRow[channelID][bi->bank_id] == (bi->row_addr >> 7)){
			dataBankStatusConuter[channelID][bi->bank_id] += 1;
			transactional_time += ctime.to_default_time_units() - bi->time + bank_frequency;
		}
		else{
			dataBankActivatedRow[channelID][bi->bank_id] = (bi->row_addr >> 7);
			dataBankStatusConuter[channelID][bi->bank_id] += 6;
			transactional_time += ctime.to_default_time_units() - bi->time + bank_frequency * 6;
		}
		//myfile << "toll transactional data read: issue time" << '\t' << bi->time << "serve time" << '\t' << ctime << endl;
		}

		dataBankStatus[channelID][bi->bank_id] = BANK_BUSY;
		

		readlist[channelID].erase(bi);
	}
	else{
		write_prio[channelID] /= 2;
		//write cycle
		auto bi = writelist[channelID].begin();
		if (bi == writelist[channelID].end()) return;
		while (dataBankStatus[channelID][bi->bank_id] == BANK_BUSY){
			bi++;
			if (bi == writelist[channelID].end()) return;
		}//get to bank free request
		
		write_count++;
		sc_time ctime = sc_time_stamp();
		if (dataBankActivatedRow[channelID][bi->bank_id] == (bi->row_addr >> 7)){
			dataBankStatusConuter[channelID][bi->bank_id] += 1;
			write_time += ctime.to_default_time_units() - bi->time + bank_frequency;
		}
		else{
			dataBankActivatedRow[channelID][bi->bank_id] = (bi->row_addr >> 7);
			dataBankStatusConuter[channelID][bi->bank_id] += 6;
			write_time += ctime.to_default_time_units() - bi->time + bank_frequency * 6;
		}

		 dataBankStatus[channelID][bi->bank_id] = BANK_BUSY;
		 writelist[channelID].erase(bi);


	}
	//cout << "Channel ID " << channelID << "leave channel serve" << endl;
};



void ACCSDULER::scheduler_action(void){
	queue_request readinfo;
	queue_request writeinfo;
	bool successread;
	bool successwrite;
	bool bankbusyflag;
	ofstream myfile;
	
	//independently serve channel request
	//cout << sc_time_stamp() << endl;
	for (int i = 0; i < NUM_CHANNELS; i++){
		channelserve(i);
	}

	/*if (sc_time_stamp() < sc_time(20000, SC_NS))
	{
	myfile.open("dump file 0.txt", ios::app);
	}
	else if (sc_time_stamp() < sc_time(40000, SC_NS))
	{
	myfile.open("dump file 1.txt", ios::app);
	}
	else if (sc_time_stamp() < sc_time(60000, SC_NS))
	{
	myfile.open("dump file 2.txt", ios::app);
	}
	else if (sc_time_stamp() < sc_time(80000, SC_NS))
	{
	myfile.open("dump file 3.txt", ios::app);
	}
	else if (sc_time_stamp() < sc_time(100000, SC_NS))
	{
	myfile.open("dump file 4.txt", ios::app);
	}
	else if (sc_time_stamp() < sc_time(120000, SC_NS))
	{
	myfile.open("dump file 5.txt", ios::app);
	}
	else {
	myfile.open("dump file 6.txt", ios::app);
	}

	if (!myfile)
	{
	cerr << "can't open file";
	assert(false);
	}*/


	//inport to all the bank list and decide work for next cycle

	if (sc_time_stamp()>sc_time(605000,SC_NS))
	{
		sc_stop();
	}
	free_all_banks();
}

//
//int ACCSDULER::tollreadserve(){
//	ofstream myfile;
//	int row_addr;
//	int served=0;
//
//	//step 1: open dump file
//	
//
//	if (sc_time_stamp() < sc_time(20000, SC_NS))
//	{
//		myfile.open("dump file 0.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(40000, SC_NS))
//	{
//		myfile.open("dump file 1.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(60000, SC_NS))
//	{
//		myfile.open("dump file 2.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(80000, SC_NS))
//	{
//		myfile.open("dump file 3.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(100000, SC_NS))
//	{
//		myfile.open("dump file 4.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(120000, SC_NS))
//	{
//		myfile.open("dump file 5.txt", ios::app);
//	}
//	else {
//		myfile.open("dump file 6.txt", ios::app);
//	}
//
//	if (!myfile)
//	{
//		cerr << "can't open file";
//		assert(false);
//	}
//
//	//step 2:
//	for (int i = 0; i < NUM_BANKS; i++)
//	{
//		if (tollreadlist[i]->empty()) continue;			//if readlist i is empty then check bank i+1
//		if (dataBankStatus[i] == BANK_BUSY) continue;
//		auto bi = tollreadlist[i]->begin();				//the first valid request in the list
//
//		/*save dump file*/
//		if (bi->critical == 1)
//		{
//			tollcritical_count++;
//			sc_time ctime = sc_time_stamp();
//			tollcritical_time += ctime.to_default_time_units() - bi->time;
//			
//			myfile <<"toll critical data read: issue time" << '\t' << bi->time << "serve time" << '\t' << ctime << endl;
//		}
//		else
//		{
//			tolltransactional_count++;
//			sc_time ctime = sc_time_stamp();
//			tolltransactional_time += ctime.to_default_time_units() - bi->time;
//			myfile << "toll transactional data read: issue time" << '\t' << bi->time << "serve time" << '\t' << ctime << endl;
//		}
//		/*********************************************/
//
//
//		dataBankStatus[i] = BANK_BUSY;
//		served_request->push_back(*bi);
//		
//		row_addr = bi->row_addr;
//		tollreadlist[i]->erase(bi);
//		//check other tollread list in the same region to match the toll read pattern
//		for (int j = (i + 1) > 4 ? (i + 1) % (NUM_BANKS / 2) + 4 : (i + 1) % (NUM_BANKS / 2); j != i; j = j > 3 ? (++j) % (NUM_BANKS / 2) + 4 : (++j) % (NUM_BANKS / 2))
//		{
//			for (int t = 0; t < tollreadlist[j]->size(); t++)
//			{
//				//actually i still need to implement the read from direct data in parity banks, but i haven't done yet
//				if ((*tollreadlist[j])[t].row_addr==row_addr)		//try to find the data request in the same row
//				{
//					int parity_num = corresponding_bank(i, j);
//					if ((parityBankStatus[parity_num]==BANK_FREE)&&(codingstatusmap[row_addr][parity_num]==coded))
//					{
//						
//						/*save dump file*/
//						if ((*tollreadlist[j])[t].critical == 1)
//						{
//							tollcritical_count++;
//							sc_time ctime = sc_time_stamp();
//							tollcritical_time += ctime.to_default_time_units() - (*tollreadlist[j])[t].time;
//							myfile << "toll critical parity read: issue time" << '\t' << (*tollreadlist[j])[t].time << "serve time" << '\t' << ctime << endl;
//						}
//						else
//						{
//							tolltransactional_count++;
//							sc_time ctime = sc_time_stamp();
//							tolltransactional_time += ctime.to_default_time_units() - (*tollreadlist[j])[t].time;
//							myfile << "toll transactional parity read: issue time" << '\t' << (*tollreadlist[j])[t].time << "serve time" << '\t' << ctime << endl;
//						}
//						/*********************************************/
//
//						
//						
//						parityBankStatus[parity_num] = BANK_BUSY;
//						served_request->push_back((*tollreadlist[j])[t]);
//						auto bj = tollreadlist[j]->begin();
//						tollreadlist[j]->erase(bj + t);
//						served++;
//					}
//					break;//if found one data in the same row,then since there won't be two same request, we are done
//				}				
//			}	
//		}
//		
//		served++;
//	}
//
//	//step 3: close file, return the total served number, and verify it, should be 10
//	myfile.close();
//	/*cout << served<<endl;
//	assert((served > 8) || (served < 2));*/
//	return served;
//};
//
//
//int ACCSDULER::tollwriteserve(){
//	ofstream myfile;
//	int row_addr;
//	int served = 0;
//	bool get_request[NUM_BANKS];
//	//step 1: open dump file
//	
//
//	if (sc_time_stamp() < sc_time(20000, SC_NS))
//	{
//		myfile.open("dump file 0.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(40000, SC_NS))
//	{
//		myfile.open("dump file 1.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(60000, SC_NS))
//	{
//		myfile.open("dump file 2.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(80000, SC_NS))
//	{
//		myfile.open("dump file 3.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(100000, SC_NS))
//	{
//		myfile.open("dump file 4.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(120000, SC_NS))
//	{
//		myfile.open("dump file 5.txt", ios::app);
//	}
//	else {
//		myfile.open("dump file 6.txt", ios::app);
//	}
//	
//	if (!myfile)
//	{
//		cerr << "can't open file";
//		assert(false);
//	}
//
//	//step 2: serve toll write
//	for (int i = 0; i < NUM_BANKS; i+=4)		//hope i can do a round and robin,two region
//	{
//		if (tollwritelist[i]->empty()) continue;
//		if (dataBankStatus[i] == BANK_BUSY) continue;
//		
//		for (int k = 0; k < NUM_BANKS;k++)
//		{
//			get_request[k] = false;
//		}
//		auto bi = tollwritelist[i]->begin();
//		//dataBankStatus[i] = BANK_BUSY;
//		get_request[i] = true;
//		row_addr = bi->row_addr;
//		served_request->push_back(*bi);
//		/*save dump file*/
//
//		{tollwrite_count++;
//		sc_time ctime=sc_time_stamp();
//		tollwrite_time += ctime.to_default_time_units() - bi->time;
//		myfile << "toll write:row address " << std::hex << bi->row_addr<<"bank id  "<<bi->addr%8 << "  issue time" << '\t' << std::dec << bi->time << " serve time" << '\t' << ctime << endl;
//		}
//		/*************************************/
//		tollwritelist[i]->erase(bi);
//		
//		//try to find out the same row address in other bank queue, with the get_request
//		for (int j = (i + 1) > 4 ? (i + 1) % (NUM_BANKS / 2) + 4 : (i + 1) % (NUM_BANKS / 2); j != i; j = j > 3 ? (++j) % (NUM_BANKS / 2) + 4 : (++j) % (NUM_BANKS / 2))
//		{
//			for (int t = 0; t < tollwritelist[j]->size(); t++){
//				if ((*tollwritelist[j])[t].row_addr==row_addr)
//				{
//					int parity_num = corresponding_bank(i, j);
//					if ((dataBankStatus[j]==BANK_FREE))
//					{
//						//parityBankStatus[j] = BANK_BUSY;		should not be here
//						get_request[j] = true;
//						//codingstatusmap[row_addr][parity_num] = coded;
//						served_request->push_back((*tollwritelist[j])[t]);
//						auto bj = tollwritelist[j]->begin();
//
//						/*save dump file*/
//
//						{tollwrite_count++;
//						sc_time ctime = sc_time_stamp();
//						tollwrite_time += ctime.to_default_time_units() - (bj+t)->time;
//						myfile << "toll write:row address " << std::hex << (bj + t)->row_addr << "bank id  " << (bj + t)->addr % 8 << " issue time" << '\t' << std::dec << (bj + t)->time << " serve time" << '\t' << ctime << endl;
//						}
//						/*************************************/
//						tollwritelist[j]->erase(bj + t);
//						//served++;
//					}
//					break;
//				}
//			}
//		}
//		
//		for (int m = 0; m < NUM_BANKS/2; m++)
//			{
//				int x = m + i;
//				//if (get_request[x] == false) assert(false);
//				dataBankStatus[x] = BANK_BUSY;
//			}
//
//		for (int m = 0; m < NUM_PARITY_BANKS/2; m++)
//		{
//			int x = m + 3*i/2;
//			//all the banks in the region should be free
//			//if (parityBankStatus[x] == BANK_BUSY) assert(false);
//
//			parityBankStatus[x] = BANK_BUSY;
//			codingstatusmap[row_addr][x] = coded;
//		}
//		served += 4;
//	}
//	
//
//	//step 3: close file and return with the served number
//	myfile.close();
//	return served;
//};
//
//int ACCSDULER::localreadserve(){
//	ofstream myfile;
//	int row_addr;
//	int served = 0;
//	static int round_robin_bank;
//	invalid_record temp;
//	invalid_record inv;
//	bool valid_parity[NUM_PARITY_BANKS] = {false};
//		
//	std::valarray<int> needed_elem(0, NUM_BANKS);	// store the summary of element neededd 
//	bool gotten_elem[NUM_BANKS] = { false };
//	/*int f_inv;
//	int s_inv;
//*/
//	int start_bank;
//
//
//	//step 1: open dump file
//	
//	if (sc_time_stamp() < sc_time(20000, SC_NS))
//	{
//		myfile.open("dump file 0.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(40000, SC_NS))
//	{
//		myfile.open("dump file 1.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(60000, SC_NS))
//	{
//		myfile.open("dump file 2.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(80000, SC_NS))
//	{
//		myfile.open("dump file 3.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(100000, SC_NS))
//	{
//		myfile.open("dump file 4.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(120000, SC_NS))
//	{
//		myfile.open("dump file 5.txt", ios::app);
//	}
//	else {
//		myfile.open("dump file 6.txt", ios::app);
//	}
//	
//	if (!myfile)
//	{
//		cerr << "can't open file";
//		assert(false);
//	}
//
//	//need to consider the empty case
//	for (int tt = 0; tt < 2;tt++)
//	{
//		start_bank = -1;
//		//step 2:repair one row per region, from invalid list
//		do
//		{
//			//delete those data who have actually be updated
//			if (invalid_parity[round_robin_bank + tt * 6]->empty()){ 
//				temp = invalid_record(0, 0);
//		}
//			else { 
//				temp = (*invalid_parity[round_robin_bank + tt * 6])[0]; 
//			auto b = invalid_parity[round_robin_bank + tt * 6]->begin();
//			invalid_parity[round_robin_bank + tt * 6]->erase(b);
//			}
//		} while (codingstatusmap[temp.row_addr][round_robin_bank + tt * 6] != invalid);
//	//now temp point to an invalid parity data then we can start to repair its row
//
//		inv = temp;
//		if (inv.row_addr == 0) continue;		// to next region
//		row_addr = inv.row_addr;
//
//		for (int k = 0 + tt * 6; k < NUM_PARITY_BANKS / 2 + tt * 6; k++)
//		{
//			if (codingstatusmap[row_addr][k]==invalid)
//			{
//				int f = find_first_bank(k);
//				int s = find_second_bank(k);
//				needed_elem[f] ++;
//				needed_elem[s] ++;
//				
//			}
//		}
//		//collect all the data needed
//		for (int i = 0 + tt * 4; i < (NUM_BANKS / 2+tt * 4); i++)
//		{
//			if (needed_elem[i] == 0)
//			{
//				start_bank = i;
//				break;
//			}
//		}
//		if (start_bank==-1)
//		{
//			do
//			{
//				start_bank = find_max(needed_elem, tt*4,tt*4+4);				
//				dataBankStatus[start_bank] = BANK_BUSY;
//				needed_elem[start_bank] = 0;
//				for (int x = 0 + tt * 4; x < (NUM_BANKS / 2 + tt * 4); x++){
//					if (needed_elem[x] > 0){
//						//assert(parityBankStatus[corresponding_bank(x, start_bank)] == BANK_FREE);
//						if (parityBankStatus[corresponding_bank(x, start_bank)] == BANK_FREE){
//						parityBankStatus[corresponding_bank(x, start_bank)] = BANK_BUSY;
//						needed_elem[x] = 0;				//we will verify the neededelem to make sure no request won't be served
//						}
//					}
//
//				}
//			} while (needed_elem.sum()>0);
//
//		}
//
//		else{										//2,3 is needed
//			assert(dataBankStatus[start_bank] = BANK_FREE);
//			dataBankStatus[start_bank] = BANK_BUSY;
//
//			needed_elem[start_bank] = 0;			//we will verify the neededelem to make sure no request won't be served
//			
//			for (int x = 0 + tt * 4; x < (NUM_BANKS / 2 + tt * 4); x++)
//			{
//				if (needed_elem[x] > 0){
//					assert(parityBankStatus[corresponding_bank(x, start_bank)] == BANK_FREE);
//					parityBankStatus[corresponding_bank(x, start_bank)] = BANK_BUSY;
//
//					needed_elem[x] = 0;				//we will verify the neededelem to make sure no request won't be served
//				}
//			}
//			
//
//		}
//
//
//		//for (int bank = (round_robin_bank + 1) > 4 ? (round_robin_bank + 1) % (NUM_BANKS / 2) + 4 : (round_robin_bank + 1) % (NUM_BANKS / 2); bank != round_robin_bank; bank = bank > 3 ? (++bank) % (NUM_BANKS / 2) + 4 : (++bank) % (NUM_BANKS / 2)){
//
//		
//		//}
//
//
//		/*not optimal yet need more improvement*/
//
//		//if (dataBankStatus[f_inv] == BANK_BUSY) assert(false);
//		//if (dataBankStatus[s_inv] == BANK_BUSY) assert(false);
//
//
//		//
//
//
//	}
//
////after the repairing part, serve the normal read part
//		for (int j = 0; j < NUM_BANKS; j++)
//		{
//			if (localreadlist[j]->empty()) continue;
//			if (dataBankStatus[j] == BANK_BUSY) continue;
//
//			auto bj = localreadlist[j]->begin();
//
//			/*save dump file*/
//			if (bj->critical == 1)
//			{
//				localcritical_count++;
//				sc_time ctime = sc_time_stamp();
//				localcritical_time += ctime.to_default_time_units() - bj->time;
//				myfile << "critical read:row address " << std::hex << bj->row_addr << " issue time" << '\t' << std::dec << bj->time << " serve time" << '\t' << ctime << endl;
//
//			}
//			else
//			{
//				localtransactional_count++;
//				sc_time ctime = sc_time_stamp();
//				localtransactional_time += ctime.to_default_time_units() - bj->time;
//				myfile << "transactional read: issue time" << '\t' << bj->time << "serve time" << '\t' << ctime << endl;
//			}
//			/*********************************************/
//
//
//			localreadlist[j]->erase(bj);
//			dataBankStatus[j] = BANK_BUSY;
//			
//			served++;
//		}
//		int ss = 0;
//		while (ss<NUM_PARITY_BANKS)
//		{
//			if (parityBankStatus[ss]==BANK_FREE)
//			{
//				parityBankStatus[ss] = BANK_BUSY;
//			}
//			ss++;
//		}
//
//
//	//step 3:close file return serve
//	round_robin_bank = (round_robin_bank + 1) % (NUM_PARITY_BANKS/2);
//	myfile.close();
//	return served;
//};

//int ACCSDULER::localwriteserve(){
//	ofstream myfile;
//	//int row_addr;
//	int served = 0;
//	int row_addr[NUM_BANKS] = { 0 };
//
//
//	//step 1: open dump file
//	
//	if (sc_time_stamp() < sc_time(20000, SC_NS))
//	{
//		myfile.open("dump file 0.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(40000, SC_NS))
//	{
//		myfile.open("dump file 1.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(60000, SC_NS))
//	{
//		myfile.open("dump file 2.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(80000, SC_NS))
//	{
//		myfile.open("dump file 3.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(100000, SC_NS))
//	{
//		myfile.open("dump file 4.txt", ios::app);
//	}
//	else if (sc_time_stamp() < sc_time(120000, SC_NS))
//	{
//		myfile.open("dump file 5.txt", ios::app);
//	}
//	else {
//		myfile.open("dump file 6.txt", ios::app);
//	}
//	
//	if (!myfile)
//	{
//		cerr << "can't open file";
//		assert(false);
//	}
//
//	//step 2: serve local write
//	//fetch first local write from each bank queue and remember the row address for comparing later
//	for (int i = 0; i < NUM_BANKS; i++)
//	{
//		if (localwritelist[i]->empty()) continue;
//		if (dataBankStatus[i] == BANK_BUSY) continue;
//
//		auto bi = localwritelist[i]->begin();
//		row_addr[i] = bi->row_addr;
//		//cout << "core number: " << bi->core_number << endl;
//		dataBankStatus[i] = BANK_BUSY;
//
//		/*save dump*/
//		{localwrite_count++;
//		sc_time ctime = sc_time_stamp();
//		localwrite_time += ctime.to_default_time_units() - bi->time;
//		myfile << "local write: row address " << std::hex << bi->row_addr << " issue time" << '\t' << std::dec << bi->time << " serve time" << '\t' << ctime << endl;
//		
//		}
//		/*********************************/
//
//
//		localwritelist[i]->erase(bi);
//		served++;
//	}
//
//	for (int i = 0; i < NUM_BANKS; i++)
//	{
//		//each bank i write can invalidate at most 3 parity data, so the total maximum number can be 12
//
//		//forget the region issue!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
//		
//		for (int j = i+1; j < ((i / 4) * 4 + 4); j++){		//j from i to end, 4 or 8
//			if ((row_addr[i] == row_addr[j]) && (row_addr[i] != 0))	//match a pattern
//			{
//				int parity_num = corresponding_bank(i, j);
//				if (parityBankStatus[parity_num] == BANK_FREE){
//				parityBankStatus[parity_num] = BANK_BUSY;
//				codingstatusmap[row_addr[i]][parity_num] = coded;
//				}
//				else codingstatusmap[row_addr[i]][parity_num] = invalid;
//				
//			}
//			else if(row_addr[i]!=row_addr[j])
//			{
//				int parity_num = corresponding_bank(i, j);
//				//solve these two separately 
//				
//				//solve from bank i
//				if (codingstatusmap[row_addr[i]][parity_num]==coded)
//				{
//					codingstatusmap[row_addr[i]][parity_num] = invalid;
//					invalid_record that{row_addr[i],i};
//					invalid_parity[parity_num]->push_back(that);		//find out the related parity data and check their status,if coded then push to invalid list and change to invalid,
//				}
//				else{		//redundant									//if invalid then do nothing(means it's already in the invalid list and will be repair later)
//					codingstatusmap[row_addr[i]][parity_num] = invalid;
//				}
//
//				//solve from bank j
//				if (codingstatusmap[row_addr[j]][parity_num] == coded)
//				{
//					codingstatusmap[row_addr[j]][parity_num] = invalid;
//					invalid_record that{ row_addr[j], j };
//					invalid_parity[parity_num]->push_back(that);
//				}
//				else{
//					codingstatusmap[row_addr[j]][parity_num] = invalid;
//				}
//			}
//			else{//redundant			//rowaddr[i]==rowaddr[j]==0. which means no write request in bank i and bank j, so do nothing
//			
//			}
//		}
//	}
//
//	int ss = 0;
//	while (ss<NUM_PARITY_BANKS)
//	{
//		if (parityBankStatus[ss] == BANK_FREE)
//		{
//			parityBankStatus[ss] = BANK_BUSY;
//		}
//		ss++;
//	}
//	ss = 0;
//	while (ss<NUM_BANKS)
//	{
//		if (dataBankStatus[ss] == BANK_FREE)
//		{
//			dataBankStatus[ss] = BANK_BUSY;
//		}
//		ss++;
//	}
//	//step 3; close file return served
//	myfile.close();
//	return served;
//};

int trans2list(sc_fifo_in<queue_request>& inport, deque<list_request>* list, int cycle_priority){
	//inport should one of read_bankin[channelID] write_bankin[channelID]
	//list should be one of readlist writelist
	// this function is called independently by each channel 
	//so it only do bussiness within channel

	//return tells read or write priority
	bool successread;
	queue_request readinfo;
	int total=0;

	assert(cycle_priority > -10);
	
	//If any one bank queue is full then stop insert into any bank queue

		assert(list->size() <= listmaxsize);
		while(list->size()<listmaxsize)		
		{
			successread = inport->nb_read(readinfo);
			if (successread)
			{
				list_request listinfo(readinfo);
				int bankid = listinfo.bank_id;
				list->push_back(listinfo);
			}
			else break;				//avoid infinite loop
		}


	if (list->size()==0) return 0;
	return cycle_priority+list->size();
	
	
};


class bank_invalid{
public:
	int bank_id;
	bool find_bank;

};
//
//void ACCSDULER::recover_parity(int parity_bank_id){
//	bank_invalid first;
//	bank_invalid second;
//	for (int i = 0; i < invalid_parity[parity_bank_id]->size(); i++)
//	{
//		int row_address = invalid_parity[parity_bank_id]->operator[](i).row_addr;
//		auto b = served_request->begin();
//		auto e = served_request->end();
//		first.find_bank = false;
//		second.find_bank = false;
//
//		while (b < e){
//			if (b->row_addr==row_address) 
//			{
//				if (!first.find_bank)		//lock it when it's true
//				{
//					first.find_bank = check_first_bank(parity_bank_id, b->addr % 8);
//					first.bank_id = b->addr % 8;
//				}
//				if (!second.find_bank)		//lock it when it's true
//				{
//					second.find_bank = check_second_bank(parity_bank_id, b->addr % 8);
//					second.bank_id = b->addr % 8;
//				}
//			}
//			b++;
//		}
//		
//		if (first.find_bank&&second.find_bank)//coded
//		{
//			codingstatusmap[row_address][parity_bank_id] = coded;
//		}
//		else if (first.find_bank)
//		{
//			codingstatusmap[row_address][parity_bank_id] = direct;
//		}
//	}
//};


//
//bool ACCSDULER::check_first_bank(int parity_id,int data_id){
//	int need_bankid;
//	switch (parity_id)
//	{
//	case 0:need_bankid = 0; break;
//	case 1:need_bankid = 0; break;
//	case 2:need_bankid = 3; break;
//	case 3:need_bankid = 1; break;
//	case 4:need_bankid = 1; break;
//	case 5:need_bankid = 2; break;
//	case 6:need_bankid = 4; break;
//	case 7:need_bankid = 4; break;
//	case 8:need_bankid = 7; break;
//	case 9:need_bankid = 5; break;
//	case 10:need_bankid = 5; break;
//	case 11:need_bankid = 6; break;
//	default:
//		assert(false);
//		break;
//	}
//
//	if (need_bankid == data_id) return true;
//	else return false;
//};
//
//int find_first_bank(int parity_id){
//	int need_bankid;
//	switch (parity_id)
//	{
//	case 0:need_bankid = 0; break;
//	case 1:need_bankid = 0; break;
//	case 2:need_bankid = 3; break;
//	case 3:need_bankid = 1; break;
//	case 4:need_bankid = 1; break;
//	case 5:need_bankid = 2; break;
//	case 6:need_bankid = 4; break;
//	case 7:need_bankid = 4; break;
//	case 8:need_bankid = 7; break;
//	case 9:need_bankid = 5; break;
//	case 10:need_bankid = 5; break;
//	case 11:need_bankid = 6; break;
//	default:
//		assert(false);
//		break;
//	}
//	return need_bankid;
//
//};
//

//
//bool ACCSDULER::check_second_bank(int parity_id, int data_id){
//	int need_bankid;
//	switch (parity_id)
//	{
//	case 0:need_bankid = 1; break;
//	case 1:need_bankid = 2; break;
//	case 2:need_bankid = 0; break;
//	case 3:need_bankid = 2; break;
//	case 4:need_bankid = 3; break;
//	case 5:need_bankid = 3; break;
//	case 6:need_bankid = 5; break;
//	case 7:need_bankid = 6; break;
//	case 8:need_bankid = 4; break;
//	case 9:need_bankid = 6; break;
//	case 10:need_bankid = 7; break;
//	case 11:need_bankid = 7; break;
//	default:
//		assert(false);
//		break;
//	}
//
//	if (need_bankid == data_id) return true;
//	else return false;
//
//};
//
//int find_second_bank(int parity_id){
//
//	int need_bankid;
//	switch (parity_id)
//	{
//	case 0:need_bankid = 1; break;
//	case 1:need_bankid = 2; break;
//	case 2:need_bankid = 0; break;
//	case 3:need_bankid = 2; break;
//	case 4:need_bankid = 3; break;
//	case 5:need_bankid = 3; break;
//	case 6:need_bankid = 5; break;
//	case 7:need_bankid = 6; break;
//	case 8:need_bankid = 4; break;
//	case 9:need_bankid = 6; break;
//	case 10:need_bankid = 7; break;
//	case 11:need_bankid = 7; break;
//	default:
//		assert(false);
//		break;
//	}
//
//	return need_bankid;
//};

//set status of all banks to free, when all actions have been taken, cycle ends
void free_all_banks(void){
	static int databanks_frequency[NUM_CHANNELS][NUM_BANKS];
	for (int i = 0; i < NUM_CHANNELS;i++)
	for (int j = 0; j < NUM_BANKS; j++)
	{
		if (dataBankStatus[i][j]==BANK_BUSY){
			if (bank_frequency==++databanks_frequency[i][j])
			{	
				
				if (dataBankStatusConuter[i][j] == 0){
					dataBankStatus[i][j] = BANK_FREE;
				}
				else dataBankStatusConuter[i][j]--;
				databanks_frequency[i][j] = 0;
			}
		}
	}

	return;

}

#include "STIM.h"
#include "systemc"
#include <string>
#include "variable.h"

/**
* This function takes an input line from the trace file, and populates the
* passed in struct object with the appropriate data.
*
* INPUTS: 	input_command -> string representation of a single line from the trace
* 							 file
* OUTPUT:	Request object with each field populated
*/
/*same*/
core_request parse_input(string input_command) {

	/* First break the input line into substrings */
	vector<string> token;
	size_t pos = 0;
	while ((pos = input_command.find(" ")) != string::npos) {
		token.push_back(input_command.substr(0, pos));
		input_command.erase(0, pos + 1);
	}
	token.push_back(input_command.substr(0, string::npos)); //Make sure to get the last string
	auto a =token[1];
	/* Now grab the important information from the line */
	core_request t;
	t.address = stoi(token[7].substr(7),NULL,16);
	t.id = stoi(token[3].substr(5), NULL, 16);
	t.length = stoi(token[5].substr(6), NULL, 16)+1;
	t.time = stoi(token[0].substr(5),NULL, 10);
	t.size = stoi(token[6].substr(7), NULL, 16);
	t.qos = strtoul(&token[8][6], NULL, 16); //Determine priority
	if (token[4].compare("TYPE:INCR") == 0)
	{
		t.type = 0;  //type is incr
	}
	else t.type = 1; // type is wrap

	if (token[2].compare("RNW:Read") == 0) { //Determine if it is a read or write
		t.read = true;
		//num_reads += 1;
	}
	else {
		t.read = false;
		//num_writes += 1;
	}

	return t;


}


void STIM::action(void){
	//cout << "in action" << endl;
	static int count;
	static int round;
	bool write_success;
	int min;
	int min_index = 0;
	min = b[0]->time;

	//find out the first min issuetime request from all cores
	for (int i = 0; i < NUM_TRACES; i++)
	{
		if (min>b[i]->time) {
			min = b[i]->time;
			min_index = i;
		}
	}


	//if min<current time then issue out
	 while (sc_time(min,SC_NS) <= sc_time_stamp()){
		 write_success = request_out->nb_write(*(b[min_index]));
		 if (write_success) b[min_index]++;
		 else break;
		 
		 if (b[min_index] >= e[min_index]) sc_stop();
		 //b[min_index]++;
		
		 min = b[0]->time;
		 min_index = 0;
		 for (int i = 0; i < NUM_TRACES; i++)
		 {
			 if (b[i] >= e[i]) { 
				 sc_stop();
				 break;
			 }
			 if (min>b[i]->time) {
				 min = b[i]->time;
				 min_index = i;
			 }
		 }
	}
	 if (b[0] == e[0]) sc_stop();






	//for (int i = 0; i < NUM_TRACES; i++)
	//{
	//	int index = (round + i) % NUM_TRACES;
	//	while (b[index]<e[index])
	//	{
	//		sc_time acttime(b[index]->time, SC_NS);
	//				if (acttime<=sc_time_stamp())
	//				{	
	//					write_success = request_out->nb_write(*(b[index]));
	//					//cout<<"core out request:core#"<<b[i]->core_number<<"addr"<<std::hex<<b[i]->address<<endl;
	//					if (write_success) b[index]++;
	//					else break;
	//				}
	//				else break;
	//	}
	//	
	//	if (b[index] == e[index]) sc_stop();

	//}
	//round = (round + 1) % NUM_TRACES;
}

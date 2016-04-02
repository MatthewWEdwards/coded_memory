#include "core_arbiter.h"
#include <math.h>
/*
the model is based on SRAM, so each "bank" in SRAM is like a sram Instantiation, there is no bank conflict in SRAM,
but the problem is because that it's single port,  so only one data can be accessed each cycle



*/

void record_bankin( int bankid, queue_request& bankre);

void core_arbiter::action(void){
	//cout << "in core arbiter action" << endl;
	
	bool flag=true;
	ofstream myfile;
	core_request info;

	//check all the  bank queues about freespace,if anyone don't have freespace then don't read from core queue
	for (int k = 0; k < NUM_BANKS; k++)
	{
		if (arbiter_toll_read_out[k]->num_free()==0)
		{
			flag = false;
			break;
		}
		if (arbiter_local_read_out[k]->num_free() == 0)
		{
			flag = false;
			break;
		}
		if (arbiter_toll_write_out[k]->num_free() == 0)
		{
			flag = false;
			break;
		}
		if (arbiter_local_write_out[k]->num_free() == 0)
		{
			flag = false;
			break;
		}
	}

	//cout << "in core action" <<endl;
	if (!flag) return;

	bool getinfo= request_in->nb_read(info);		//fetch one burst per cycle
	if (!getinfo)	return;
	//assert(false);
	//int addr = info.address / pow(2, info.size);
	
	//since the trace file might have problem, we fix the size heres
	int addr = info.address / pow(2, 5);

	switch (info.type)		//two type incr and wrap
	{
	case 0: 	//size is the size of word here usually is 256bits ==32B	
			for (int i = 0; i < info.length; i++,addr++){			//INCR
				
				//assert(info.length < 3);
				
				int bank = addr%8;				//suppose it 16-3-5
				queue_request bank_request(addr, info.id, info.core_number, info.time);
			
				if (info.read==true)
				{	
					if (i==0)
					{
						bank_request.critical = true;
					}//the first read is critical read

					//cout << "read request: bank" << bank << "	address:" << std::hex<<bank_request.addr << "	id" << info.id << "	core_num" << info.core_number <<"critical read or not"<< bank_request.critical << endl;
					arbiter_local_read_out[bank]->write(bank_request);
					//assert(false);
					//int j = arbiter_read_out[bank]->num_free();
				}
				else
				{
					//cout << "write request: bank" << bank << "	address:" << std::hex << bank_request.addr << "	id" << info.id << "	core_num" << info.core_number << endl;
					
					arbiter_local_write_out[bank]->write(bank_request);
					//assert(false);
				}				
			}	
		break;
	case 1:	
		//int addr = info.address / pow(2, info.size);
		//size is the size of word here usually is 256bits
		int cutoff = log2(info.length);		//cutoff should be 5 because 2^5=32byte=256bits
		int bound = ((addr>>cutoff)<<cutoff) + info.length;

		assert(info.length > 3);
		
		if ((info.length==4)||(info.length==8))
		{													//toll WRAP	
			for (int i = 0; i < info.length; i++){		
				int bank = addr% 8;				
				queue_request bank_request(addr, info.id, info.core_number,info.time);
				if (info.read == true)
				{
					if (i == 0){	bank_request.critical = true;	}//the first read is critical read
							//cout << "read request: bank" << bank << "	address:" << std::hex << bank_request.addr << "	id" << info.id << "	core_num" << info.core_number<<"critical read or not" << bank_request.critical  << endl;
					arbiter_toll_read_out[bank]->write(bank_request);
					
					//dump the request in bank
					//record_bankin(bank, bank_request);
				}
				else
				{
							//cout << "write request: bank" << bank << "	address:" << std::hex << bank_request.addr << "	id" << info.id << "	core_num" << info.core_number << endl;
					arbiter_toll_write_out[bank]->write(bank_request);
							//assert(false);
				}
				addr++;
				if (addr>=bound)
				{
					addr = bound - info.length;
				}

			}
		}
		else
		{
			assert(false);		//actually there is no local wrap in trace file
			for (int i = 0; i < info.length; i++){		//local WRAP	
						int bank = addr% 8;				
						queue_request bank_request(addr, info.id, info.core_number,info.time);
						if (info.read == true)
						{
							if (i == 0)
							{
								bank_request.critical = true;
							}//the first read is critical read

							//cout << "read request: bank" << bank << "	address:" << std::hex << bank_request.addr << "	id" << info.id << "	core_num" << info.core_number<<"critical read or not" << bank_request.critical  << endl;
							//arbiter_read_out[bank]
							arbiter_local_read_out[bank]->write(bank_request);
						}
						else
						{
							//cout << "write request: bank" << bank << "	address:" << std::hex << bank_request.addr << "	id" << info.id << "	core_num" << info.core_number << endl;
							arbiter_local_write_out[bank]->write(bank_request);
							//assert(false);
						}
						addr++;
						if (addr>=bound)
						{
							addr = bound - info.length;
						}

					}
		}
		
		break;									

	}

};


void record_bankin( int bankid,queue_request& bankre){
	ofstream myfile;
	switch (bankid)
	{
	case 0:myfile.open("bank in 0.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time<<'\t'<<"row addr"<<bankre.addr/8 << endl;
		myfile.close(); break;
	case 1:myfile.open("bank in 1.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	case 2:myfile.open("bank in 2.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	case 3:myfile.open("bank in 3.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	case 4:myfile.open("bank in 4.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	case 5:myfile.open("bank in 5.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	case 6:myfile.open("bank in 6.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	case 7:myfile.open("bank in 7.txt", ios::app);
		myfile << "core num" << '\t' << bankre.core_number << '\t' << "issue time" << "\t" << bankre.time << '\t' << "row addr" << bankre.addr / 8 << endl;
		myfile.close(); break;
	default:
		break;
	}

}
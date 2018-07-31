#ifndef __DEBUG_H
#define __DEBUG_H

#include "Coding.h"
#include "Request.h"
#include <iostream>
#include <list>

namespace ramulator
{

class Debug
{
public:
	static void print_bank_queues(long clk, unsigned int num_banks, const coding::BankQueue& readq, const coding::BankQueue& writeq, const list<Request>& pending) 
	{
		cout << "Pending Reads, CLK = " << clk << ",  size = " << pending.size() << endl;
		for(auto pending_read : pending) {
			cout << "{";
			for(auto num = pending_read.addr_vec.begin();
					num != pending_read.addr_vec.end();
					num++) {
				cout << *num;
				if(num + 1 != pending_read.addr_vec.end())
					cout << ", ";
			}
			cout << "}" << " Arrive = " << pending_read.arrive
				 << ", Depart = " << pending_read.depart
				 << ", CoreID = " << pending_read.coreid << endl;
		}
		for(unsigned int bank = 0; bank < num_banks; bank++)
		{
			cout << "Readq" << ", bank = " << bank << ", size = " << readq.queues[bank].size() << endl;
			for(auto read_queue = readq.queues[bank].begin();
					read_queue != readq.queues[bank].end();
					read_queue++) {
				cout << "{";
				for(auto num = read_queue->addr_vec.begin();
						num != read_queue->addr_vec.end();
						num++) {
					cout << *num;
					if(num + 1 != read_queue->addr_vec.end())
						cout << ", ";
				}
				cout << "}" << " Arrive = " << read_queue->arrive
					 << ", CoreID = " << read_queue->coreid << endl;
			}

			cout << "Writeq" << ", bank = " << bank << ", size = " << writeq.queues[bank].size() << endl;
			for(auto write_queue = writeq.queues[bank].begin();
					write_queue != writeq.queues[bank].end();
					write_queue++) {
				cout << "{";
				for(auto num = write_queue->addr_vec.begin();
						num != write_queue->addr_vec.end();
						num++) {
					cout << *num;
					if(num + 1 != write_queue->addr_vec.end())
						cout << ", ";
				}
				cout << "}" << " Arrive = " << write_queue->arrive
					 << ", CoreID = " << write_queue->coreid << endl;
			}
		}
	}
};
}


#endif /* Debug.h */



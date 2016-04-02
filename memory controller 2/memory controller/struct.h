#pragma once

#include <string>
using namespace std;

/* Struct for the input requests from the processors */
class core_request{

public:
	int address;		//the read address including the bit on bus
	int id;
	int qos;//priority
	int length;
	int time;		//issue time
	//int queue_time;
	bool type;			//wrap or incr
	int core_number; //What core the request came from 
	bool read;
	int size;

	//bool critical;		//tag bit for critical read



};// core_request;

class queue_request{
public:	
	int addr;
	int id;
	bool valid;
	int core_number;

	int time;			//issue time, used to caculate the latency when the request got served
	bool critical;

	queue_request(){}
	queue_request(int ad, int index, int num,int start_time):addr(ad), id(index),core_number(num),valid(true),critical(false),time(start_time){
		
	}


};
class invalid_record{
public:
	int addr;
	int row_addr;

	invalid_record(){};
	invalid_record(const int& rowaddress,const int& bank){
		row_addr = rowaddress;
		addr = row_addr << 3 + bank;
	}
	invalid_record(const invalid_record& that) : addr(that.addr), row_addr(that.row_addr){
	
}
};

class list_request{
public:
	int addr;
	int row_addr;
	int id;
	bool valid;
	int core_number;

	int time;			//issue time, used to caculate the latency when the request got served
	bool critical;


	list_request(){}
	list_request(queue_request that) :addr(that.addr), id(that.id), core_number(that.core_number), valid(that.valid),critical(that.critical),time(that.time){
		row_addr = addr >> 3;
	}

};


class prewrite_list_request{
public:
	int addr;
	int row_addr;
	int id;
	bool valid;
	int core_number;
	int prewrite_paritybank;

	int time;			//issue time, used to caculate the latency when the request got served

	prewrite_list_request(){}
	prewrite_list_request(list_request that,int prewrite_parity) :addr(that.addr), id(that.id), core_number(that.core_number), valid(that.valid),row_addr(that.row_addr),time(that.time){
		prewrite_paritybank = prewrite_parity;
	}



};


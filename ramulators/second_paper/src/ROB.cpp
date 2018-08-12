#ifndef __ROB_H
#define __ROB_H
#include <iostream>
#include <cstdio>
#include <map>
#include <vector>
#include <iterator>
#include <algorithm>
#include <queue>
#include <list>
#include <functional>
#include <set>

#include "Request.h"
#include "Bank.h"
#include "DRAM.h"
#include "Config.h"

using namespace std;
namespace ramulator{

//TODO: Consider implications of ignoring read_latency 
template <typename T>
class ROB{
public:

	map<int, vector<int>> robMap;
	map<int, int> robRecencyQueue;
	list<pair<int /*row*/, int /*bank*/>> data_write_queue;
	list<int /*row*/> parity_write_queue;
	ParityArchitecture parity_arch;
	BankArchitecture bank_arch = BankArchitecture();
	
	/* Dynamic Encoding variables */
	double alpha = 1;
	double code_region_length = 1;
	unsigned int num_rows_per_region;
	set<unsigned int> active_regions;

	enum class Entry: int
	{
		Done, ReadWrite, DValid, PValid, DB, PB, MAX
	};

	enum class RW: int
	{
		Read = 0,
		Write = 1
	};

private: 
	uint32_t max_map_size = 0;
	float writeback_trigger_threshold = 0.8;
	float map_reduce_threshold = 0.5;
	

public:
	ROB(const Config& configs, double alpha, double code_region_length, unsigned int num_rows_per_region):
		alpha(alpha),
		code_region_length(code_region_length),
		num_rows_per_region(num_rows_per_region)
	{
		parity_arch = ParityArchitecture(configs.get_parity_architecture());
		max_map_size = configs.get_rob_length();
	}
	ROB(const Config& configs)
	{
		parity_arch = ParityArchitecture(configs.get_parity_architecture());
		max_map_size = configs.get_rob_length();
	}

	/*
	The read algorithm as described in the paper. Updates the ROB and "reads" from 
	memory banks (marks them as busy)	

	Returns: If the req is to-be scheduled by the controller
	*/	
	bool Read(int clk, list<Request>::iterator req){
		int row = req->addr_vec[int(T::Level::Row)];
		int bank = req->addr_vec[int(T::Level::Bank)];
		auto it = robMap.find(row);
		robRecencyQueue[row] = clk;
		if(it == robMap.end() || !req->just_arrived)
		{
			if(robMap.size() >= max_map_size)
			{
				req->isRobValid = false;
				if(bank_arch.is_free(bank))
				{
					bank_arch.read(bank);
					return true;
				}
				return false;
			}
			vector<int> temp(int(Entry::MAX), 0);
			temp[int(Entry::ReadWrite)] = int(RW::Read);
			robMap[row] = temp;
			req->isRobValid = false;
			return read_from_memory(req);
		}else
		{
			req->just_arrived = false;
			robMap[row][int(Entry::ReadWrite)] = int(RW::Read);
			if((robMap[row][(int(Entry::DB))] >> bank & 0x1) == 0x1)//(robMap[row][int(Entry::DValid)]))
			{ 
				if(robMap[row][int(Entry::Done)] == 0)
				{
					if(parity_arch.use_all_banks())
						robMap[row][int(Entry::PValid)] = 1;
				}
				req->isRobValid = true;
				return true; 
			}else
			{
				if(robMap[row][int(Entry::PValid)] == 1)
				{
					if(robMap[row][int(Entry::Done)] == 0)
					{
						req->isRobValid = false;
						if(parity_read(row, bank))
							req->isRobValid = true;
						return true;
					}
				}else 
				{
					if(robMap[row][int(Entry::Done)] == 0)
					{
						int hamming_weight = hammingWeight(robMap[row][int(Entry::DB)]);
						if(hamming_weight == bank_arch.size())
						{
							robMap[row][int(Entry::DValid)] = 1;
						}
						req->isRobValid = false;
						return read_from_memory(req);
					}
				}	
			}
		}
		req->isRobValid = false;
		return false; // should never get here
	}

	bool Write(int clk, list<Request>::iterator req){
		int row = req->addr_vec[int(T::Level::Row)];
		int bank = req->addr_vec[int(T::Level::Bank)];
		auto it = robMap.find(row);
		if(it == robMap.end()){
			if(robMap.size() >= max_map_size)
			{
				req->isRobValid = false;
				if(bank_arch.is_free(bank))
				{
					bank_arch.read(bank);
					return true;
				}
				return false;
			}
			vector<int> temp(int(Entry::MAX), 0);
			temp[int(Entry::ReadWrite)] = int(RW::Write);
			robMap[row] = temp;
		}
		robRecencyQueue[row] = clk;
		robMap[row][int(Entry::DValid)] = 0;
		robMap[row][int(Entry::DB)] &= ~(1 << bank); // clear DB bit for Bank to be written do
		robMap[row][int(Entry::PValid)] = 0; 
		robMap[row][int(Entry::PB)] = 0; 
		auto write_entry = pair<int, int>(row, bank);
		if(find(data_write_queue.begin(), data_write_queue.end(), write_entry) == data_write_queue.end())
		{
			data_write_queue.push_back(write_entry);
			robMap[row][int(Entry::Done)] = 1; // FIXME: According to the paper, this should be set later, but I think it makes more sense here
		}
		req->isRobValid = false;
		if(bank_arch.is_free(bank))
		{
			bank_arch.read(bank); // FIXME: Actually write, change name
			return true;
		}
		return false;
	}

	// Hamming Weight: The number of non-zero bits (in the binary number)
	int hammingWeight(uint32_t n) {
        n = n - ((n>>1)&0x55555555);
        n = (n&0x33333333)+((n>>2)&0x33333333);
        n = (n+(n>>4))&0x0F0F0F0F;
        n = n+(n>>8);
        n = n+(n>>16);
        return n&0x3F;
    }
	
	void Writeback()
	{
		if(robMap.size() > max_map_size * writeback_trigger_threshold)
			CleanMap();
		
	}

	// Return true if successful
	bool read_from_memory(list<Request>::iterator req)
	{
		int bank = req->addr_vec[int(T::Level::Bank)];
		int row = req->addr_vec[int(T::Level::Row)];

		bool parity_read_flag = parity_read(row, bank);
		if(!parity_read_flag)
		{
			if(bank_arch.is_free(bank))
			{
				bank_arch.banks[bank].read();
				robMap[row][int(Entry::DB)] |= 1 << bank;
				return true;
			}
		}
		return parity_read_flag;
	}

	// Attempt to download data from memory using free memory banks
	void issue_cmds()
	{
		// Update ROB with newly written data
		for(auto write = data_write_queue.begin(); write != data_write_queue.end(); )
		{
			int row = write->first;

			if(robMap.find(row) == robMap.end()) // FIXME: queue management should not be necessary here, but there are errors without this
			{	
				write = data_write_queue.erase(write);
				continue;
			}	

			// Fill out ROB	DB entries
			for(int bank = 0; bank < bank_arch.size(); bank++)
			{
				if(!((robMap[row][int(Entry::DB)] >> bank) & 1) && bank_arch.is_free(bank))
				{
					robMap[row][int(Entry::DB)] |= 1 << bank;
					bank_arch.read(bank);
				}
			}
			if(hammingWeight(robMap[row][int(Entry::DB)]) == bank_arch.size())
			{
				robMap[row][int(Entry::DValid)] = 1;
				for(int parity_bank = 0; parity_bank < parity_arch.size(); parity_bank++)
				{
					robMap[row][int(Entry::PB)] |= 1 << parity_bank;
				}
				robMap[row][int(Entry::PValid)] = 1;
				robMap[row][int(Entry::Done)] = 1;
				parity_write_queue.push_back(write->first);
				write = data_write_queue.erase(write);
				continue;
			}
			write++;
		}

		// Writeback algorithm
		// Write modified parity data if all parities are idle
		if(parity_write_queue.size()){
			auto parity_write = parity_write_queue.begin();
			auto row = *parity_write;
			while(parity_write != parity_write_queue.end() && robMap.find(row) == robMap.end()) // FIXME: should not be necessary but there are errors without this
			{	
				parity_write = parity_write_queue.erase(parity_write);
				continue;
			}	
			if(parity_write == parity_write_queue.end())
				return;
			if(parity_arch.use_all_banks()) 
			{
				parity_write_queue.erase(parity_write);
				robMap[row][int(Entry::Done)] = 0;
				robMap[row][int(Entry::ReadWrite)] = int(RW::Read);
			}
		}

//		// Use idle banks to fillout the ROB
//		auto row_recency= robRecencyQueue.begin();
//		while(bank_arch.any_free() && row_recency != robRecencyQueue.end())
//		{
//			int row = row_recency->first;
//			for(int bank = 0; bank < bank_arch.banks.size(); bank++)
//			{
//				if((robMap[row][int(Entry::DB)] & (1 << bank)) == 0x1)
//				{
//					bool parity_read_flag = parity_read(row, bank);
//					if(!parity_read_flag)
//					{
//						if(bank_arch.is_free(bank))
//						{
//							bank_arch.banks[bank].read();
//							robMap[row][int(Entry::DB)] |= 1 << bank;
//						}
//					}
//				}
//			}
//			row_recency++;
//		} 
						
	}

	inline bool is_full() {return robMap.size() >= max_map_size;}

	
private:
	void CleanMap()
	{
		auto reverse_entries = flip_map(robRecencyQueue);
		uint32_t cur_map_size = robMap.size();
		for(auto reverse_pairs_it = reverse_entries.begin(); 
			reverse_pairs_it != reverse_entries.end() && cur_map_size > max_map_size * map_reduce_threshold;
			reverse_pairs_it++)
		{
			auto row_to_remove = reverse_pairs_it->first;
			if(robMap[row_to_remove][int(Entry::Done)] == 0)
			{
				if(!row_in_write_queue(row_to_remove)) // Should not be necessary
				{
					robMap.erase(row_to_remove);
					robRecencyQueue.erase(row_to_remove);
					cur_map_size--;
				}
			}
		}
		
	}
	
	vector<pair<int, int>> flip_map(map<int,int> & map)
	{
		vector<pair<int, int>> pairs;
		for (auto itr = map.begin(); itr != map.end(); ++itr)
			pairs.push_back(*itr);

		sort(pairs.begin(), pairs.end(), [=](pair<int, int>& a, pair<int, int>& b)
		{
			return a.second < b.second;
		}
		);
		
		return pairs;
	}

	/*
	req_bank: Index of the bank to read/write to
	parities: output references to parity banks	valid for req_bank
	*/
	void get_free_parities(vector<reference_wrapper<ParityBank>>&  parities, int req_bank)
	{
		for(auto parity = parity_arch.parity_banks.begin();
			parity != parity_arch.parity_banks.end();
			parity++)
		{
			if(parity->is_free() && parity->contains(req_bank))
				parities.push_back(*parity);
		}
	}

	/*
	Attempt to read bank data using parity banks. Will not utilize the bank index passed.
	Also update ROB DB bits with the data banks read.
	*/
	bool parity_read(int row, int bank_num)
	{
		unsigned int row_region = (row / num_rows_per_region) / code_region_length;
		if(active_regions.find(row_region) == active_regions.end())
			return false;
		vector<reference_wrapper<ParityBank>> parities_available;
		vector<reference_wrapper<ParityBank>> parities_to_use;
		vector<reference_wrapper<Bank>> memory_banks_to_use;
		get_free_parities(parities_available, bank_num);
		bool read_flag = false;

		for(auto parity : parities_available)
		{
			// Get candiate parity banks which match the current parity bank
			parities_to_use.clear();
			memory_banks_to_use.clear();
			int equivalent_parities = 0;
			for(auto parity_to_compare : parities_available)
			{
				if(parity.get() == parity_to_compare.get())
				{
					equivalent_parities++;
					parities_to_use.push_back(parity_to_compare);
				}
			}
			int needed_data_banks = parity.get().get_components().size() - equivalent_parities;

			// Decode
			auto components = parity.get().get_components();
			for(auto bank = bank_arch.banks.begin();
				bank != bank_arch.banks.end();
				bank++)
			{
				if(std::find(components.begin(), components.end(), bank->index) != components.end())
				{
					memory_banks_to_use.push_back(*bank);
					needed_data_banks--;
				}
				if(needed_data_banks == 0)
					break;
			}
			if(needed_data_banks == 0) 
			{
				// Perform read
				for(auto bank = parities_to_use.begin();
					bank != parities_to_use.end();
					bank++)
					bank->get().read();
				for(auto bank = memory_banks_to_use.begin();
					bank != memory_banks_to_use.end();
					bank++)
					bank->get().read();
				read_flag = true;
				break;
			}
		}

		if(read_flag)
		{
			//Update ROB
			for(auto bank : memory_banks_to_use)
				robMap[row][int(Entry::DB)] |= 1 << bank.get().index;
			robMap[row][int(Entry::DB)] |= 1 << bank_num;
			return true;
		}
		return false;

	}

	bool row_in_write_queue(int row)
	{
		for(auto data_write = data_write_queue.begin();
			data_write != data_write_queue.end();
			data_write++)
			if(data_write->first == row)
				return false;
		if(find(parity_write_queue.begin(), parity_write_queue.end(), row) == parity_write_queue.end())
			return false;
		return true;
	}
};
}

#endif /* __ROB_H */

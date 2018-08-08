#ifndef __BANK_H
#define __BANK_H

#include "Config.h"
#include "Request.h"
#include <list>
#include <vector>


using namespace std;
namespace ramulator{

class ParityBank
{
	private:
		vector<int> component_banks;
		bool busy = true;

	public: 
		ParityBank(vector<int> components)
		{
			for(auto bank : components)	
				this->component_banks.push_back(bank);
			busy = true;
		}
		
		inline vector<int>& get_components() {return component_banks;}

		bool contains(int bank)
		{
			for(auto component : component_banks)
				if(component == bank)
					return true;
			return false;
		}

		inline bool is_free() {return !busy;}

		inline void free() {busy = false;}

		inline void read() {busy = true;}

		static bool can_banks_decode(vector<ParityBank> parities, vector<int> banks)
		{
			//TODO (Implemented in ROB.cpp, might not refactor)
			return false;
		}

		bool operator==(const ParityBank &other)
		{
			return other.component_banks == this->component_banks;

		}
		
};

class ParityArchitecture
{
	public: 
		// Parity Architecture Types
		enum PARCH {
			EIGHT_PARITY,
			FOUR_PARITY,
			NONE,
			EIGHT_DUPLICATE,
			MAX
		};

		int num_banks = 8;
		vector<ParityBank> parity_banks;

		ParityArchitecture()
		{
			for(int i = 0; i < 4; i++)	
			{
				parity_banks.push_back(ParityBank(parity_types[i]));
				parity_banks.push_back(ParityBank(parity_types[i]));
			}
		}
	
		ParityArchitecture(const int type)
		{
			switch(type){
			case 1:	
				for(int i = 0; i < 4; i++)	
				{
					parity_banks.push_back(ParityBank(parity_types[i]));
					parity_banks.push_back(ParityBank(parity_types[i]));
				}
				break;
			case 2:
				for(int i = 0; i < 4; i++)
					parity_banks.push_back(ParityBank(parity_types[i]));
				break;
			case 3:
				break;
			case 4:
				for(int i = 0; i < 8; i++)
					parity_banks.push_back(ParityBank(trivial_parity_types[i]));
				break;
			case 5:
				for(int i = 0; i < dual_parity_types.size(); i++)
					parity_banks.push_back(ParityBank(dual_parity_types[i]));
				break;
			default:
				return;
			}
		}

		ParityArchitecture(vector<vector<int>> parity_components)
		{
			for(auto components : parity_components)
				parity_banks.push_back(ParityBank(components));
		}

		void refresh()
		{
			for(auto bank = parity_banks.begin(); bank != parity_banks.end(); bank++)
				bank->free();
		}

		inline int size() {return parity_banks.size(); }

		bool use_all_banks()
		{
			for(auto bank : parity_banks)
				if(!bank.is_free())
					return false;
			for(auto bank = parity_banks.begin(); bank != parity_banks.end(); bank++)
				bank->read();
			return true;
		}

	private:
		vector<vector<int>> parity_types { {0,1,2,3}, {4,5,6,7}, {0,2,4,6}, {1,3,5,7} };
		vector<vector<int>> dual_parity_types { {0,1}, {0,2}, {0,3}, {1,2}, {1,3}, {2,3}, {4,5}, {4,6}, {4,7}, {5,6}, {5,7}, {6,7} };
		vector<vector<int>> trivial_parity_types { {0}, {1}, {2}, {3}, {4}, {5}, {6}, {7} };
};


class Bank {
	public:
		int index;

	private:
		bool busy;

	public:

		Bank(int index)
		{
			this->index = index;
			busy = true;
		}
		
		inline void free() {busy = false;}
	
		inline bool is_free() {return !busy;}

		inline void read() {busy = true;}
};


class BankArchitecture
{
	public: 
		vector<Bank> banks;
	
		BankArchitecture(int num_banks = 8)
		{
			for(int bank_index = 0; bank_index < num_banks; bank_index++)
				banks.push_back(Bank(bank_index));
		}

		void refresh()
		{
			for(auto bank = banks.begin(); bank != banks.end(); bank++)
				bank->free();
		}

		inline bool is_free(int bank) {return banks[bank].is_free();}
		
		bool any_free()
		{
			for(auto bank : banks)
				if(bank.is_free())
					return true;
			return false;
		}

		inline void read(int bank) {banks[bank].read();}

		inline int size() {return banks.size(); }
};

class BankQueue {
public:
		vector<list<Request>> queues;
		unsigned int max = 10;
		
		BankQueue() {}

		BankQueue(uint32_t num_banks)
		{
			queues.resize(num_banks);
		}

		unsigned int size() 
		{
			unsigned int sz = 0;
			for(auto queue : queues)
			{
				sz  += queue.size();
			}
			return sz;
		}

		unsigned int size(unsigned int queue_idx)
		{
			return queues[queue_idx].size();
		}

		unsigned int get_num_queues()
		{
			return queues.size();
		}
};

}


#endif /* __BANK_H */


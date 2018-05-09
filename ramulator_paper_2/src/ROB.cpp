#ifndef __ROB_H
#define __ROB_H
#include <iostream>
#include <cstdio>
#include <map>
#include <vector>
#include <iterator>
#include <algorithm>

using namespace std;
namespace ramulator{

class ROB{
public:

	map<int, vector<int>> robMap;
	map<int, int> robRecencyQueue;

	enum class Entry: int
	{
		Done, DValid, PValid, MAX
	};

private: 
	
	uint32_t max_map_size = 1024;
	float writeback_trigger_threshold = 0.8;
	float map_reduce_threshold = 0.5;

public:
	
	ROB(){}
	ROB(uint32_t max_map_size) {this->max_map_size = max_map_size;}

	bool Read(int row, int bank, int clk){
		cout << "Read Row: "<<row << "Bank: "<<bank<<endl;
		auto it = robMap.find(row);
		if(it == robMap.end()){
			vector<int> temp(int(Entry::MAX), 0);
			temp[int(Entry::DValid)] = 1 << bank;
			robMap[row] = temp;
			robRecencyQueue[row] = clk;
			return false; //read from memory
		}else{
			robRecencyQueue[row] = clk;
			if((robMap[row][int(Entry::DValid)] >> bank ) & 1){
				return true; //DValid
			} else{
				if(hammingWeight(robMap[row][int(Entry::DValid)]) <= 2){
					robMap[row][int(Entry::DValid)] += 1 << bank;
					robMap[row][int(Entry::PValid)] = 1; //PValid
					return false;
				} else {
					robMap[row][int(Entry::DValid)] += 1 << bank;
					return (robMap[row][int(Entry::PValid)]);
				}	
					
			}
		}
		return false; //invalid

	}

	bool Write(int row, int bank, int clk){
		cout << "Write Row: "<<row << "Bank: "<<bank<<endl;
		auto it = robMap.find(row);
		if(it == robMap.end()){
			vector<int> temp(int(Entry::MAX), 0);
			temp[int(Entry::DValid)] = 1 << bank;
			robMap[row] = temp;
			robRecencyQueue[row] = clk;
			return true;
		}else{
			robRecencyQueue[row] = clk;
			robMap[row][int(Entry::DValid)] += 1 << bank;
			robMap[row][int(Entry::PValid)] = 0; //invalidate PValid
			if(hammingWeight(robMap[row][int(Entry::DValid)]) >= 4){
				robMap[row][int(Entry::PValid)] = 1;
			}
			if(hammingWeight(robMap[row][int(Entry::DValid)]) == 8){
				return false; //write back
			}
			return true;
		}
		return false; //invalid

	}

	int hammingWeight(uint32_t n) {
        n = n - ((n>>1)&0x55555555);
        n = (n&0x33333333)+((n>>2)&0x33333333);
        n = (n+(n>>4))&0x0F0F0F0F;
        n = n+(n>>8);
        n = n+(n>>16);
        return n&0x3F;
    }
	
	void Writeback();
	{
		if(robMap.size() > max_map_size * writeback_trigger_threshold)
			CleanMap();
	}
	
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
			robMap.erase(row_to_remove);
			robRecencyQueue.erase(row_to_remove);
			cur_map_size--;
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


};
}

#endif

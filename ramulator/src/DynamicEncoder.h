#ifndef __DYNAMICENCODER_H
#define __DYNAMICENCODER_H

#include "Coding.h"
#include "Request.h"
#include "RecodingUnit.h"
#include <queue>
#include <list>

namespace coding
{

// All the constructors/operators are needed to satisfy rule-of-five
template <typename T>
class EncodedRegion{
public:
    T *spec;
	unsigned int region_index;
    unsigned int start_row; // Row in bank, not absolute row
    unsigned int num_rows;  // Rows in bank, not absolute rows
    unsigned int last_row;  // Row in bank, not absolute row
    unsigned int num_banks;
	vector<unsigned int> downloaded_codes; // bitwise, each entry is a row and bits 0-N represent the rows

    EncodedRegion(T* spec, unsigned int start_row, unsigned int num_rows, unsigned int region_index) :
        spec(spec),
		region_index(region_index),
        start_row(start_row),
		num_rows(num_rows),
		last_row(start_row + num_rows),
        num_banks(spec->org_entry.count[static_cast<int>(T::Level::Bank)])
    {
        for(unsigned int row = 0; row < num_rows; row++)
			downloaded_codes.push_back(0);

    }

    bool is_ready()
    {
        for(unsigned int bank = 0; bank < num_banks; bank++)
            for(unsigned int row = 0; row < num_rows; row++)
                if(((downloaded_codes[row] >> bank) & 0x1) == 0x0)
                    return false;
        return true;
    }

	// Receive data used by other parts of the access scheduler
	void receive_data(unsigned int row, unsigned int bank)
	{
		if(this->contains(row))
			downloaded_codes[row - start_row] &= (0x1 << bank);
	}
	
	// Use an idle data bank to fill out encoding
	bool receive_bank(unsigned int bank)
	{
		for(unsigned int row = 0; row < num_rows; row++)
			for(unsigned int bank = 0; bank < num_banks; bank++)
			if(((downloaded_codes[row] >> bank )& 0x1) == 0x0)
			{
				downloaded_codes[row] &= (0x1 << bank);
				return true;
			}
		return false;
	}
	

    bool contains(unsigned int row) {return row >= start_row && row <= last_row;}

    // Returns bank/row pair
    pair<unsigned int, unsigned int> request()
    {
        for(unsigned int bank = 0; bank < num_banks; bank++)
            for(unsigned int row = 0; row < num_rows; row++)
				if((downloaded_codes[row] >> bank & 0x1) == 0x0)
                    return pair<unsigned int, unsigned int>(bank, start_row + row);

        return pair<unsigned int, unsigned int>(0,0);
    }


};
template <typename T>
bool operator==(const EncodedRegion<T>& lhs, const EncodedRegion<T>& rhs)
{
	return lhs.start_row == rhs.start_row &&
		   lhs.last_row  == rhs.last_row;
}

//TODO: Ensure that memory usage stays less than alpha
// ---> More generally, add memory usage checks and stats here 
//TODO: Store codes
template <typename T>
class DynamicEncoder{
public:
	vector<EncodedRegion<T>> regions_to_encode;
	set<unsigned int> encoded_regions;   // Indicies of encoded regions
	set<unsigned int> selected_regions;  // Indicies of regions meant to be encoded
	set<unsigned int> regions_to_evict;  // Indicies of encoded regions to evict once a new region is fully encoded
	vector<coding::ParityBankTopology<T>> region_list; 

	DynamicEncoder(vector<coding::ParityBankTopology<T>> region_list, set<unsigned int>& active_regions):
		encoded_regions(active_regions),
		selected_regions(active_regions),
		region_list(region_list)
	{}

	void switch_regions(T* spec, const set<unsigned int>& new_regions)
	{
		selected_regions = new_regions;
		set<unsigned int> create_regions = set<unsigned int>(new_regions);

		// Remove regions currently being encoded which are no longer selected
		for(auto region = regions_to_encode.begin(); region != regions_to_encode.end();)
			if(create_regions.find(region->region_index) == create_regions.end())
				region = regions_to_encode.erase(region);
			else
				region++;

		// Ignore selected regions which are already encoded or are being encoded - no action to be done for these
		for(auto region = create_regions.begin(); region != create_regions.end();)
			if(encoded_regions.find(*region) != encoded_regions.end())
				region = create_regions.erase(region);
			else
				region++;
		for(auto region_to_encode : regions_to_encode)
			if(create_regions.find(region_to_encode.region_index) != create_regions.end())
				create_regions.erase(region_to_encode.region_index);

		for(auto selected_region : create_regions)
		{
			unsigned int start_row = region_list[selected_region].row_regions[0].first;
			unsigned int num_rows  = region_list[selected_region].row_regions[0].second;
			auto new_encoded_region = EncodedRegion<T>(spec, start_row, num_rows, selected_region);
			regions_to_encode.push_back(new_encoded_region);
		}

		// Choose regions to evict, and remove regions_to_encode which are no longer selected
		for(auto encoded_region : encoded_regions)
			if(selected_regions.find(encoded_region) == selected_regions.end()) // TODO: Don't evict regions if there is space to keep them, even if they aren't selected
				regions_to_evict.insert(encoded_region);
		for(auto region_to_encode = regions_to_encode.begin(); region_to_encode != regions_to_encode.end();)
			if(selected_regions.find(region_to_encode->region_index) == selected_regions.end())
				region_to_encode = regions_to_encode.erase(region_to_encode);
			else
				region_to_encode++;
	}

	void fill_data(vector<DataBank>& data_banks, vector<ParityBank<T>>& parity_banks, list<Request>& reqs_scheduled)
	{
		// Fill out region switches using reqs_scheduled
		for(auto req : reqs_scheduled)
		{
			unsigned int row = req.addr_vec[static_cast<int>(T::Level::Row)];
			unsigned int bank = req.addr_vec[static_cast<int>(T::Level::Bank)];
			for(auto encoded_region = regions_to_encode.begin();
				encoded_region != regions_to_encode.end();	
				encoded_region++)
				encoded_region->receive_data(row, bank);
		}

		// Fill out region switches using idle data banks
		for(auto bank = data_banks.begin(); bank != data_banks.end(); bank++)
			if(bank->is_free())
				for(auto to_encode = regions_to_encode.begin(); to_encode != regions_to_encode.end(); to_encode++)
					if(to_encode->receive_bank(bank->index))
						bank->read();

	}

	void replace_regions(RecodingUnit<T>& recoding_unit)
	{
		for(auto region_to_encode = regions_to_encode.begin();
			region_to_encode != regions_to_encode.end();)
		{
			if(region_to_encode->is_ready())
			{	
				unsigned int to_evict = *regions_to_evict.begin(); // TODO: Select this by number of region hits, not randomly
				recoding_unit.emplace_region(region_list[region_to_encode->region_index]);
				recoding_unit.evict_region(region_list[to_evict]);
				regions_to_evict.erase(to_evict);
				region_to_encode = regions_to_encode.erase(region_to_encode);
				continue;
			}
			region_to_encode++;
		}
	}
};


}

#endif

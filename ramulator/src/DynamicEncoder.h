#ifndef __DYNAMICENCODER_H
#define __DYNAMICENCODER_H

#include "Coding.h"
#include "RecodingUnit.h"
#include <queue>

namespace coding
{

template <typename T>
class EncodedRegion{
public:
    const T *spec;
    const unsigned int num_banks;
    const unsigned int start_row; // Row in bank, not absolute row
    const unsigned int last_row;  // Row in bank, not absolute row
    const unsigned int num_rows;  // Rows in bank, not absolute rows
	unsigned int priority;
    bool ** downloaded_codes;

    EncodedRegion(T* spec, unsigned int start_row, unsigned int last_row, bool first_encoding, unsigned int priority) :
        spec(spec),
        num_banks(spec->org_entry.count[static_cast<int>(T::Level::Row)]),
        start_row(start_row),
        last_row(last_row),
        num_rows(last_row - start_row + 1),
		priority(priority)
    {
        downloaded_codes = new bool *[num_banks];
        for(unsigned int bank = 0; bank < num_banks; bank++)
        {
            downloaded_codes[bank] = new bool[num_rows];
            for(unsigned int row = 0; row < num_rows; row++)
                downloaded_codes[bank][row] = first_encoding;
        }
    }

    ~EncodedRegion()
    {
        for(unsigned int bank = 0; bank < num_banks; bank++)
            delete downloaded_codes[bank];
        delete downloaded_codes;
    }

    bool is_ready()
    {
        for(unsigned int bank = 0; bank < num_banks; bank++)
            for(unsigned int row = 0; row < num_rows; row++)
                if(downloaded_codes[bank][row] == 0)
                    return false;
        return true;
    }

    bool contains(unsigned int row) {return row >= start_row && row <= last_row;}

    // Returns bank/row pair
    pair<unsigned int, unsigned int> request()
    {
        for(unsigned int bank = 0; bank < num_banks; bank++)
            for(unsigned int row = 0; row < num_rows; row++)
                if(downloaded_codes[bank][row] == 0)
                    return pair<unsigned int, unsigned int>(bank, start_row + row);

        return pair<unsigned int, unsigned int>(0,0);
    }

    bool operator==(const EncodedRegion& er)
    {
        return this->start_row == er.start_row &&
               this->last_row  == er.last_row;
    }

};

//TODO: Ensure that memory usage stays less than alpha
//TODO: Store codes
template <typename T>
class DynamicEncoder{
public:

	vector<EncodedRegion<T>> regions_to_encode;
	vector<unsigned int>     encoded_regions;  // Indicies of encoded regions
	vector<unsigned int> 	 regions_to_evict; // Indicies of encoded regions to evict once a new region is fully encoded
	const vector<coding::ParityBankTopology<T>> region_list; 

	DynamicEncoder(vector<coding::ParityBankTopology<T>> region_list):
		region_list(region_list)
	{
	}

	void tick(const DataBank * data_banks, const ParityBank<T> * parity_banks, RecodingUnit<T>& recoder_unit)
	{
		// Prepare encoded regions switches
	}


};


}

#endif

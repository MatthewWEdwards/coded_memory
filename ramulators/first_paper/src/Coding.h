#ifndef __CODING_H
#define __CODING_H

#include "Request.h"
#include <cassert>
#include <utility>
#include <vector>
#include <deque>
#include <map>
#include <list>

namespace coding
{
using Request = ramulator::Request;

/* Note: ramulator addresses are composed of (Channel, Rank, Bank, Row, Column)
 * tuples. We address all rows with one composite index of Rank, Bank, and Row. */

inline int calc_log2(const int& val)
{
    int n {0};
    int v {val};
    while ((v >>= 1))
        n++;
    return n;
}

template <typename T>
unsigned long addr_vec_to_row_index(const T *spec, const vector<int>& addr_vec)
{
    const int rank {addr_vec[static_cast<int>(T::Level::Rank)]};
    const int bank {addr_vec[static_cast<int>(T::Level::Bank)]};
    const int row {addr_vec[static_cast<int>(T::Level::Row)]};
    const int bank_bits {calc_log2(spec->org_entry.count
                                       [static_cast<int>(T::Level::Bank)])};
    const int row_bits {calc_log2(spec->org_entry.count
                                      [static_cast<int>(T::Level::Row)])};
    return (rank << (bank_bits + row_bits)) +
           (bank << row_bits) +
           row;
}

template <typename T>
inline unsigned long request_to_row_index(const T *spec, const Request& req)
{
    return addr_vec_to_row_index<T>(spec, req.addr_vec);
}

template <typename T>
vector<int> row_index_to_addr_vec(const T *spec,
                                  const unsigned long& row_index, const int& channel)
{
    const int bank_bits {calc_log2(spec->org_entry.count
                                       [static_cast<int>(T::Level::Bank)])};
    const int row_bits {calc_log2(spec->org_entry.count
                                      [static_cast<int>(T::Level::Row)])};
    const unsigned long rank {row_index >> (bank_bits + row_bits)};
    const unsigned long bank {(row_index >> row_bits) & ((1 << (bank_bits + 1)) - 1)};
    const unsigned long row {row_index & ((1 << (row_bits + 1)) - 1)};
    vector<int> addr_vec(static_cast<int>(T::Level::MAX));
    addr_vec[static_cast<int>(T::Level::Channel)] = channel;
    addr_vec[static_cast<int>(T::Level::Rank)] = rank;
    addr_vec[static_cast<int>(T::Level::Bank)] = bank;
    addr_vec[static_cast<int>(T::Level::Row)] = row;
    return addr_vec;
}

class BankQueue {
public:
        vector<list<Request>> queues;
        unsigned int max = 10;
		
		BankQueue() {}

		BankQueue(uint32_t num_banks, unsigned int depth = 10)
		{
			queues.resize(num_banks);
			max = depth;
		}
		
		BankQueue(const BankQueue& q)
		{
			for(auto cpy_q : q.queues)
			{
				this->queues.push_back(cpy_q);
			}
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

template <typename T>
class MemoryRegion {
private:
    const T *spec;
public:
    const int start_row_index;
    const int n_rows;
    unsigned long hits;

    MemoryRegion(const T *spec, const int& start_row_index,
                 const int& n_rows) :
        spec(spec),
        start_row_index(start_row_index),
        n_rows(n_rows),
        hits(0) {}

    bool operator ==(const MemoryRegion<T>& other) const
    {
        return spec == other.spec && hits == other.hits &&
               start_row_index == other.start_row_index && n_rows == other.n_rows;
    }
    bool operator !=(const MemoryRegion<T>& other) const
    {
        return !(*this == other);
    }
    inline bool contains(const Request& req) const
    {
        const unsigned long row_index {request_to_row_index<T>(spec, req)};
        return row_index >= start_row_index &&
               row_index < start_row_index + n_rows;
    }
    inline bool contains(const unsigned long row_index) const
    {
        return row_index >= start_row_index &&
               row_index < start_row_index + n_rows;
    }

    inline int row_number(const Request& req) const
    {
        const unsigned long row_index {request_to_row_index<T>(spec, req)};
        const unsigned long row_number {row_index - start_row_index};
        assert(row_number >= 0 && row_number < n_rows);
        return row_number;
    }

	inline int get_bank() const
	{
		return row_index_to_addr_vec(spec, start_row_index, 0)[static_cast<int>(T::Level::Bank)];
	}
};

template <typename T>
class XorCodedRegions {
public:
    const vector<MemoryRegion<T>> regions;
    const MemoryRegion<T> NO_REGION {nullptr, -1, -1}; /* sentinel */

    XorCodedRegions(const vector<MemoryRegion<T>>& regions) :
        regions(regions) {}

    const MemoryRegion<T>& request_region(const Request& req) const
    {
        for (const MemoryRegion<T>& region : regions)
            if (region.contains(req))
                return region;
        return NO_REGION;
    }
    inline bool contains(const Request& req) const
    {
        return request_region(req) != NO_REGION;
    }
    inline bool contains(const unsigned long row_index) const
    {
        for(auto region : regions) {
            if(region.contains(row_index)) {
                return true;
            }
        }
        return false;
    }

    bool same_request_row_numbers(const Request& a, const Request& b) const
    {
        const MemoryRegion<T>& a_region {request_region(a)};
        const MemoryRegion<T>& b_region {request_region(b)};
        return a_region != NO_REGION && b_region != NO_REGION &&
               a_region.row_number(a) == b_region.row_number(b);
    }
};

template <typename T>
class ParityBankTopology {
public:
    vector<vector<XorCodedRegions<T>>> xor_regions_for_parity_bank;
    vector<pair<unsigned int, unsigned int>> row_regions;
    unsigned int n_parity_banks;

    virtual ~ParityBankTopology() {}

    const bool contains(const Request& req) const
    {
        for (int b {0}; b < n_parity_banks; b++) {
            auto bank_regions {xor_regions_for_parity_bank[b]};
            for (XorCodedRegions<T>& regions : bank_regions)
                if (regions.contains(req))
                    return true;
        }
        return false;
    }

    const bool contains(const unsigned long& row) const
    {
        for (int b {0}; b < n_parity_banks; b++) {
            auto bank_regions {xor_regions_for_parity_bank[b]};
            for (XorCodedRegions<T>& regions : bank_regions)
                if (regions.contains(row))
                    return true;
        }
        return false;
    }
	
};

template <typename T>
bool operator<(const ParityBankTopology<T>& lhs, const ParityBankTopology<T>& rhs)
{
	if(lhs.row_regions.size() == 0 || rhs.row_regions.size() == 0)
		return lhs.row_regions.size() == rhs.row_regions.size(); // Empty case
	return lhs.row_regions[0].first < rhs.row_regions[0].first;
}

template <typename T>
bool operator>(const ParityBankTopology<T>& lhs, const ParityBankTopology<T>& rhs)
{
	if(lhs.row_regions.size() == 0 || rhs.row_regions.size() == 0)
		return lhs.row_regions.size() == rhs.row_regions.size(); // Empty case
	return lhs.row_regions[0].first > rhs.row_regions[0].first;
}

template <typename T>
bool operator==(const ParityBankTopology<T>& lhs, const ParityBankTopology<T>& rhs)
{
	if(lhs.row_regions.size() == 0 || rhs.row_regions.size() == 0)
		return lhs.row_regions.size() == rhs.row_regions.size(); // Empty case
	return lhs.row_regions[0].first == rhs.row_regions[0].first;
}

/* Macros refer to notation used in "Achieving Multi-Port Memory Performance on Single-Port
 * Memory with Coding Techniques."
 */
#define A1 regions[0]
#define B1 regions[1]
#define C1 regions[2]
#define D1 regions[3]
#define E1 regions[4]
#define F1 regions[5]
#define G1 regions[6]
#define H1 regions[7]

template <typename T>
class ParityBankTopology_Scheme1 : public ParityBankTopology<T> {
public:
    ParityBankTopology_Scheme1(const vector<MemoryRegion<T>>& regions)
    {
        vector<XorCodedRegions<T>> bank1_xor {{{A1, B1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank1_xor);

        vector<XorCodedRegions<T>> bank2_xor {{{C1, D1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank2_xor);

        vector<XorCodedRegions<T>> bank3_xor {{{A1, D1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank3_xor);

        vector<XorCodedRegions<T>> bank4_xor {{{B1, C1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank4_xor);

        vector<XorCodedRegions<T>> bank5_xor {{{B1, D1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank5_xor);

        vector<XorCodedRegions<T>> bank6_xor {{{A1, C1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank6_xor);

        vector<XorCodedRegions<T>> bank7_xor {{{E1, F1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank7_xor);

        vector<XorCodedRegions<T>> bank8_xor {{{G1, H1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank8_xor);

        vector<XorCodedRegions<T>> bank9_xor {{{E1, H1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank9_xor);

        vector<XorCodedRegions<T>> bank10_xor {{{F1, G1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank10_xor);

        vector<XorCodedRegions<T>> bank11_xor {{{F1, H1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank11_xor);

        vector<XorCodedRegions<T>> bank12_xor {{{E1, G1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank12_xor);

        this->n_parity_banks = 12;

        for(auto region : regions) {
            this->row_regions.push_back(
                pair<unsigned int, unsigned int>(region.start_row_index, region.n_rows));
        }
    }
};

template <typename T>
class ParityBankTopology_Scheme2 : public ParityBankTopology<T> {
public:
    ParityBankTopology_Scheme2(const vector<MemoryRegion<T>>& regions)
    {
        vector<XorCodedRegions<T>> bank1_xor {{{A1, B1}},
            {{E1}}};
        this->xor_regions_for_parity_bank.push_back(bank1_xor);

        vector<XorCodedRegions<T>> bank2_xor {{{A1, C1}},
            {{F1}}};
        this->xor_regions_for_parity_bank.push_back(bank2_xor);

        vector<XorCodedRegions<T>> bank3_xor {{{A1, D1}},
            {{G1}}};
        this->xor_regions_for_parity_bank.push_back(bank3_xor);

        vector<XorCodedRegions<T>> bank4_xor {{{B1, C1}},
            {{H1}}};
        this->xor_regions_for_parity_bank.push_back(bank4_xor);

        vector<XorCodedRegions<T>> bank5_xor {{{B1, D1}},
            {{G1, H1}}};
        this->xor_regions_for_parity_bank.push_back(bank5_xor);

        vector<XorCodedRegions<T>> bank6_xor {{{E1, F1}},
            {{A1}}};
        this->xor_regions_for_parity_bank.push_back(bank6_xor);

        vector<XorCodedRegions<T>> bank7_xor {{{E1, G1}},
            {{B1}}};
        this->xor_regions_for_parity_bank.push_back(bank7_xor);

        vector<XorCodedRegions<T>> bank8_xor {{{E1, H1}},
            {{C1}}};
        this->xor_regions_for_parity_bank.push_back(bank8_xor);

        vector<XorCodedRegions<T>> bank9_xor {{{F1, G1}},
            {{D1}}};
        this->xor_regions_for_parity_bank.push_back(bank9_xor);

        vector<XorCodedRegions<T>> bank10_xor {{{F1, H1}},
            {{C1, D1}}};
        this->xor_regions_for_parity_bank.push_back(bank10_xor);

        this->n_parity_banks = 10;

        for(auto region : regions) {
            this->row_regions.push_back(
                pair<unsigned int, unsigned int>(region.start_row_index, region.n_rows));
        }
    }
};

template <typename T>
class ParityBankTopology_Scheme3 : public ParityBankTopology<T> {
public:
    ParityBankTopology_Scheme3(const vector<MemoryRegion<T>>& regions)
    {
        vector<XorCodedRegions<T>> bank1_xor {{{A1, B1, C1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank1_xor);

        vector<XorCodedRegions<T>> bank2_xor {{{A1, D1, G1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank2_xor);

        vector<XorCodedRegions<T>> bank3_xor {{{A1, E1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank3_xor);

        vector<XorCodedRegions<T>> bank4_xor {{{B1, E1, H1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank4_xor);

        vector<XorCodedRegions<T>> bank5_xor {{{B1, F1, G1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank5_xor);

        vector<XorCodedRegions<T>> bank6_xor {{{C1, F1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank6_xor);

        vector<XorCodedRegions<T>> bank7_xor {{{C1, D1, H1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank7_xor);

        vector<XorCodedRegions<T>> bank8_xor {{{D1, E1, F1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank8_xor);

        vector<XorCodedRegions<T>> bank9_xor {{{G1, H1}},
            {{}}};
        this->xor_regions_for_parity_bank.push_back(bank9_xor);

        this->n_parity_banks = 9;

        for(auto region : regions) {
            this->row_regions.push_back(
                pair<unsigned int, unsigned int>(region.start_row_index, region.n_rows));
        }
    }
};


template <typename T>
ParityBankTopology<T> ParityBankTopologyConstructor(const vector<MemoryRegion<T>>& regions, int coding_scheme)
{
    switch(coding_scheme)
    {
    case 1:
        return ParityBankTopology_Scheme1<T>(regions);
    case 2:
        return ParityBankTopology_Scheme2<T>(regions);
    case 3:
        return ParityBankTopology_Scheme3<T>(regions);
    }
    return ParityBankTopology<T>(); // Default, empty
}

template <typename T>
class ParityBank {
private:
    unsigned long clock {0};
    unsigned long will_finish {0};
    bool is_busy = false;
    unsigned long access_latency;
public:
    vector<vector<XorCodedRegions<T>>> xor_regions;
    const XorCodedRegions<T> NO_XOR_REGIONS {{}}; /* sentinel */

    ParityBank(const unsigned long& latency) :
        access_latency(latency) {}
    ParityBank& operator=(const ParityBank& other)
    {
        access_latency = other.access_latency;
        return *this;
    }

    bool lock()
    {
        if (!is_busy) {
            is_busy = true;
            will_finish = clock + access_latency;
            return true;
        } else {
            return false;
        }
    }
    bool busy() const
    {
        return is_busy;
    }
    void tick()
    {
        if (is_busy && clock >= will_finish)
            is_busy = false;
        clock++;
    }
    const bool contains(const Request& req) const
    {
        for (const vector<XorCodedRegions<T>>& xor_region_partition : xor_regions)
			for(const XorCodedRegions<T>& xor_region : xor_region_partition)
				if (xor_region.contains(req))
					return true;
        return false;
    }
    const XorCodedRegions<T>& request_xor_regions(const Request& req) const
    {
        for (const vector<XorCodedRegions<T>>& xor_region_partition : xor_regions)
			for(const XorCodedRegions<T>& xor_region : xor_region_partition)
				if (xor_region.request_region(req) != xor_region.NO_REGION)
					return xor_region;
        return NO_XOR_REGIONS;
    }
};

class DataBank {
    public:
        int index;

    private:
        bool is_busy;

    public:

        DataBank(int index)
        {
            this->index = index;
            is_busy = true;
        }

		inline void tick() {this->free();}
		
        inline void free() {is_busy = false;}

		inline bool busy() {return is_busy;}

		inline void lock() {is_busy = true;}
};


}
#endif /*__CODING_H*/

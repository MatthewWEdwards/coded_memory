#ifndef __CODING_H
#define __CODING_H

#include "Request.h"
#include <cassert>
#include <utility>
#include <vector>
#include <deque>

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

template <typename T>
class CodeStatusMap {
public:
        enum Status { Updated, FreshData, FreshParity };
		deque<pair<unsigned long /* row index */, unsigned long/* cycle_to_update_on */>> update_queue {};
private:
        const T *spec;
        const int n_rows;
		const int update_interval = 1;
        Status *map;
public:
        CodeStatusMap(const T *spec) :
                spec(spec),
                n_rows(spec->org_entry.count[static_cast<int>(T::Level::Rank)]*
                       spec->org_entry.count[static_cast<int>(T::Level::Bank)]*
                       spec->org_entry.count[static_cast<int>(T::Level::Row)])
        {
                map = new Status[n_rows];
                for (int r {0}; r < n_rows; r++)
                        map[r] = Status::FreshData;
        }
        ~CodeStatusMap()
        {
                delete[] map;
        }

        void set(const unsigned long& row_index, const Status& status, unsigned long serve_time)
        {
                assert(row_index >= 0 && row_index < n_rows);
				assert(status != Status::Updated);
                map[row_index] = status;
				update_queue.push_back(
					pair<unsigned long, unsigned long>(row_index, serve_time + update_interval));
        }

		void clear(const unsigned long& row_index)
		{
                assert(row_index >= 0 && row_index < n_rows);
                map[row_index] = Status::Updated;
		}

        Status get(const unsigned long& row_index) const
        {
                assert(row_index >= 0 && row_index < n_rows);
                return map[row_index];
        }

        inline Status get(const Request& req) const
        {
                return get(request_to_row_index(spec, req));
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
        inline int row_number(const Request& req) const
        {
                const unsigned long row_index {request_to_row_index<T>(spec, req)};
                const unsigned long row_number {row_index - start_row_index};
                assert(row_number >= 0 && row_number < n_rows);
                return row_number;
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
};

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
        }
};

template <typename T>
class ParityBank {
private:
        unsigned long clock {0};
        unsigned long will_finish {0};
        bool is_busy = false;
        unsigned long access_latency;
public:
        vector<XorCodedRegions<T>> xor_regions;
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
                for (const XorCodedRegions<T>& xor_region : xor_regions)
                        if (xor_region.contains(req))
                                return true;
                return false;
        }
        const XorCodedRegions<T>& request_xor_regions(const Request& req) const
        {
                for (const XorCodedRegions<T>& xor_region : xor_regions)
                        if (xor_region.request_region(req) != xor_region.NO_REGION)
                                return xor_region;
                return NO_XOR_REGIONS;
        }
};

}
#endif /*__CODING_H*/

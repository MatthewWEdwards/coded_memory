#ifndef __CODING_H
#define __CODING_H

#include "Request.h"
#include <cassert>
#include <utility>
#include <vector>

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
private:
        const T *spec;
        const int n_rows;
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

        void set(const unsigned long& row_index, const Status& status)
        {
                assert(row_index >= 0 && row_index < n_rows);
                map[row_index] = status;
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

        virtual ~ParityBankTopology() {}
};

template <typename T>
class ParityBankTopology_SchemeI : public ParityBankTopology<T> {
public:
#define A1 lower_regions[0]
#define A2 upper_regions[0]
#define B1 lower_regions[1]
#define B2 upper_regions[1]
#define C1 lower_regions[2]
#define C2 upper_regions[2]
#define D1 lower_regions[3]
#define D2 upper_regions[3]
#define E1 lower_regions[4]
#define E2 upper_regions[4]
#define F1 lower_regions[5]
#define F2 upper_regions[5]
#define G1 lower_regions[6]
#define G2 upper_regions[6]
#define H1 lower_regions[7]
#define H2 upper_regions[7]
#define I1 lower_regions[8]
#define I2 upper_regions[8]
        ParityBankTopology_SchemeI(const vector<MemoryRegion<T>>& lower_regions,
                                   const vector<MemoryRegion<T>>& upper_regions)
        {
                vector<XorCodedRegions<T>> bank1_xor {{{A1, B1}},
                                                      {{A2, B2}}};
                this->xor_regions_for_parity_bank.push_back(bank1_xor);

                vector<XorCodedRegions<T>> bank2_xor {{{C1, D1}},
                                                      {{C2, D2}}};
                this->xor_regions_for_parity_bank.push_back(bank2_xor);

                vector<XorCodedRegions<T>> bank3_xor {{{A1, D1}},
                                                      {{A2, D2}}};
                this->xor_regions_for_parity_bank.push_back(bank3_xor);

                vector<XorCodedRegions<T>> bank4_xor {{{B1, C1}},
                                                      {{B2, C2}}};
                this->xor_regions_for_parity_bank.push_back(bank4_xor);

                vector<XorCodedRegions<T>> bank5_xor {{{B1, D1}},
                                                      {{B2, D2}}};
                this->xor_regions_for_parity_bank.push_back(bank5_xor);

                vector<XorCodedRegions<T>> bank6_xor {{{A1, C1}},
                                                      {{A2, C2}}};
                this->xor_regions_for_parity_bank.push_back(bank6_xor);
                /*********************************************************/
                vector<XorCodedRegions<T>> bank7_xor {{{E1, F1}},
                                                      {{E2, F2}}};
                this->xor_regions_for_parity_bank.push_back(bank7_xor);

                vector<XorCodedRegions<T>> bank8_xor {{{G1, H1}},
                                                      {{G2, H2}}};
                this->xor_regions_for_parity_bank.push_back(bank8_xor);

                vector<XorCodedRegions<T>> bank9_xor {{{E1, H1}},
                                                      {{E2, H2}}};
                this->xor_regions_for_parity_bank.push_back(bank9_xor);

                vector<XorCodedRegions<T>> bank10_xor {{{F1, G1}},
                                                       {{F2, G2}}};
                this->xor_regions_for_parity_bank.push_back(bank10_xor);

                vector<XorCodedRegions<T>> bank11_xor {{{F1, H1}},
                                                       {{F2, H2}}};
                this->xor_regions_for_parity_bank.push_back(bank11_xor);

                vector<XorCodedRegions<T>> bank12_xor {{{E1, G1}},
                                                       {{E2, G2}}};
                this->xor_regions_for_parity_bank.push_back(bank12_xor);
        }
};

template <typename T>
class ParityBank {
private:
        unsigned long clock {0};
        unsigned long will_finish {0};
        bool is_busy = false;
        const unsigned long access_latency;
public:
        const vector<XorCodedRegions<T>> xor_regions;
        const XorCodedRegions<T> NO_XOR_REGIONS {{}}; /* sentinel */

        ParityBank(const vector<XorCodedRegions<T>>& xor_regions,
                   const unsigned long& latency) :
                access_latency(latency),
                xor_regions(xor_regions) {}

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

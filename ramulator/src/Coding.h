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
 * tuples. We address all rows with one compositse index of Rank, Bank, and Row. */

int calc_log2(const int& val);

template <typename T>
unsigned long request_to_row_index(const T& spec, const Request& req)
{
        const int rank {req.addr_vec[static_cast<int>(T::Level::Rank)]};
        const int bank {req.addr_vec[static_cast<int>(T::Level::Bank)]};
        const int row {spec.org_entry.count[static_cast<int>(T::Level::Row)]};
        const int bank_bits {calc_log2(spec.org_entry.count
                                       [static_cast<int>(T::Level::Bank)])};
        const int row_bits {calc_log2(spec.org_entry.count
                                       [static_cast<int>(T::Level::Row)])};
        return (rank << (bank_bits + row_bits)) +
               (bank << row_bits) +
               row;
}

template <typename T>
vector<int> row_index_to_addr_vec(const T& spec,
                                  const unsigned long& row_index, const int& channel)
{
        const int bank_bits {calc_log2(spec.org_entry.count
                                       [static_cast<int>(T::Level::Bank)])};
        const int row_bits {calc_log2(spec.org_entry.count
                                       [static_cast<int>(T::Level::Row)])};
        const int rank {row_index >> (bank_bits + row_bits)};
        const int bank {(row_index >> row_bits) & ((1 << (bank_bits + 1)) - 1)};
        const int row {row_index & ((1 << (row_bits + 1)) - 1)};
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
        const T& spec;
        const int n_rows;
        Status *map;
public:
        CodeStatusMap(const T& spec) :
                spec(spec),
                n_rows(spec.org_entry.count[static_cast<int>(T::Level::Rank)]*
                       spec.org_entry.count[static_cast<int>(T::Level::Bank)]*
                       spec.org_entry.count[static_cast<int>(T::Level::Row)])
        {
                map = new Status*[n_rows];
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
public:
        const int start_row;
        const int n_rows;
        const int bank;
        unsigned long hits;

        MemoryRegion(const int& start_row, const int& n_rows, const int& bank) :
                start_row(start_row),
                n_rows(n_rows),
                bank(bank),
                hits(0) {}

        bool operator ==(const MemoryRegion<T>& other) const
        {
                return start_row == other.start_row && n_rows == other.n_rows &&
                       bank == other.bank;
        }
        bool operator !=(const MemoryRegion<T>& other) const
        {
                return !(*this == other);
        }
        inline bool contains(const Request& req) const
        {
                bool same_bank {req.addr_vec[(int)T::Level::Bank] == bank};
                int req_row {req.addr_vec[(int)T::Level::Row]};
                bool row_in_range {req_row >= start_row &&
                                   req_row < start_row + n_rows};
                return same_bank && row_in_range;
        }
        inline int row_number(const Request& req) const
        {
                return req.addr_vec[(int)T::Level::Row] - start_row;
        }
};

template <typename T>
class XorCodedRegions {
public:
        const vector<MemoryRegion<T>> regions;
        const MemoryRegion<T> NO_REGION {-1, -1, -1}; /* sentinel */

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

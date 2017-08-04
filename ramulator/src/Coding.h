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
 * tuples. To address all possible banks, we address coded memory locations as
 * (Rank*#Banks+Bank, Row). */

template <typename T>
class CodeLocation {
private:
        const T& spec;
public:
        const int bank;
        const int row;

        CodeLocation(const T& spec, const int& bank, const int& row) :
                spec(spec),
                bank(bank),
                row(row) {}
        CodeLocation(const T& spec, const Request& req) :
                spec(spec),
                bank(req.addr_vec[static_cast<int>(T::Level::Rank)]*
                     spec.org_entry.count[static_cast<int>(T::Level::Bank)] +
                     req.addr_vec[static_cast<int>(T::Level::Bank)]),
                row(req.addr_vec[static_cast<int>(T::Level::Row)]) {}

        bool operator ==(const CodeLocation<T>& other) const
        {
                return &spec == &other.spec && // bad hack but it works
                       bank == other.bank && row == other.row;
        }
        bool operator !=(const CodeLocation<T>& other) const
        {
                return !(*this == other);
        }
        vector<int> addr_vec() const
        {
                vector<int> addr_vec(static_cast<int>(T::Level::MAX));
                const int max_banks {spec.org_entry.count[static_cast<int>(T::Level::Bank)]};
                addr_vec[static_cast<int>(T::Level::Rank)] = bank/max_banks;
                addr_vec[static_cast<int>(T::Level::Bank)] = bank % max_banks;
                addr_vec[static_cast<int>(T::Level::Row)] = row;
                addr_vec[static_cast<int>(T::Level::Column)] = 0;
                return addr_vec;
        }
};

template <typename T>
class CodeStatusMap {
public:
        enum Status { Updated, FreshData, FreshParity };
private:
        const T& spec;
        const int banks;
        const int rows;
        Status **map;
public:
        CodeStatusMap(const T& spec) :
                spec(spec),
                banks(spec.org_entry.count[static_cast<int>(T::Level::Rank)]*
                      spec.org_entry.count[static_cast<int>(T::Level::Bank)]),
                rows(spec.org_entry.count[static_cast<int>(T::Level::Row)])
        {
                map = new Status*[banks];
                for (int b {0}; b < banks; b++) {
                        map[b] = new Status[rows];
                        for (int r = 0; r < rows; r++)
                                map[b][r] = Status::FreshData;
                }
        }
        ~CodeStatusMap()
        {
                for (int b {0}; b < banks; b++)
                        delete[] map[b];
                delete[] map;
        }

        void set(const CodeLocation<T>& location, const Status status)
        {
                const int bank {location.bank};
                const int row {location.row};
                assert(bank >= 0 && bank < banks && row >= 0 && row < rows);
                map[bank][row] = status;
        }
        void set_row(const int& row, const Status status)
        {
                assert(row >= 0 && row < rows);
                for (int b {0}; b < banks; b++)
                        map[b][row] = status;
        }
        Status get(const CodeLocation<T>& location) const
        {
                const int bank {location.bank};
                const int row {location.row};
                assert(bank >= 0 && bank < banks && row >= 0 && row < rows);
                return map[bank][row];
        }
        inline Status get(const Request& req) const
        {
                CodeLocation<T> location {spec, req};
                return get(location);
        }
};

template <typename T>
class CodedRegion {
public:
        const int start_row;
        const int n_rows;
        const int bank;

        CodedRegion(const int& start_row, const int& n_rows, const int& bank) :
                start_row(start_row),
                n_rows(n_rows),
                bank(bank) {}

        bool operator ==(const CodedRegion<T>& other) const
        {
                return start_row == other.start_row && n_rows == other.n_rows &&
                       bank == other.bank;
        }
        bool operator !=(const CodedRegion<T>& other) const
        {
                return !(*this == other);
        }
        inline bool contains(const Request& req) const // FIXME
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
        const vector<CodedRegion<T>> regions;
        const CodedRegion<T> NO_REGION {-1, -1, -1}; /* sentinel */

        XorCodedRegions(const vector<CodedRegion<T>>& regions) :
                regions(regions) {}

        const CodedRegion<T>& request_region(const Request& req) const
        {
                for (const CodedRegion<T>& region : regions)
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
                const CodedRegion<T>& a_region {request_region(a)};
                const CodedRegion<T>& b_region {request_region(b)};
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

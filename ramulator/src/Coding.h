#ifndef __CODING_H
#define __CODING_H

#include "Request.h"
#include <vector>

using Request = ramulator::Request;

namespace coding
{

template <typename T>
class CodedRegion {
private:
        const int start_row;
        const int n_rows;
        const int bank;
public:
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

        bool lock_for_read()
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

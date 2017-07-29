#ifndef __CODING_H
#define __CODING_H

#include "Request.h"
#include <vector>

using Request = ramulator::Request;

namespace coding
{

template <typename T, int n_rows>
class CodedRegion {
private:
        const int start_row;
        const int bank;
public:
        CodedRegion(const int& start_row, const int& bank) :
                start_row(start_row),
                bank(bank) {}

        bool operator ==(const CodedRegion<T, n_rows>& other) const
        {
                return start_row == other.start_row && bank == other.bank;
        }
        bool operator !=(const CodedRegion<T, n_rows>& other) const
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
        int row_number(const Request& req) const
        {
                if (contains(req))
                        return req.addr_vec[(int)T::Level::Row] - start_row;
                else
                        return -1;
        }
};

template <typename T, int n_rows>
class ParityBank {
        using Region = CodedRegion<T, n_rows>;

private:
        unsigned long clock {0};
        unsigned long will_finish {0};
        bool is_busy = false;
        const unsigned long access_latency;
public:
        const vector<Region> xor_regions;
        const Region NO_REGION {-1, -1}; /* sentinel */

        ParityBank(const vector<Region>& regions,
                   const unsigned long& latency) :
                access_latency(latency),
                xor_regions(regions) {}

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
        const Region& request_region(const Request& req) const
        {
                for (const Region& region : xor_regions)
                        if (region.contains(req))
                                return region;
                return NO_REGION;
        }
        bool same_request_row_numbers(const Request& a, const Request& b) const
        {
                const Region& a_region {request_region(a)};
                const Region& b_region {request_region(b)};
                return a_region != NO_REGION && b_region != NO_REGION &&
                       a_region.row_number(a) == b_region.row_number(b);
        }
};

}
#endif /*__CODING_H*/

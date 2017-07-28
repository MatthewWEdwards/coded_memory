#ifndef __CODING_H
#define __CODING_H

#include "Request.h"
#include <bitset>
#include <vector>

namespace coding
{

template <typename T, std::size_t max_rows>
class CodedRegion {
private:
        const std::bitset<max_rows> rows;
        const int bank;
public:
        CodedRegion(const std::bitset<max_rows> &rows, const int &bank) :
                rows(rows),
                bank(bank) {}
        inline bool contains_request_data(const ramulator::Request &req) const
        {
                int row { req.addr_vec[(int)T::Level::Row] };
                return req.addr_vec[(int)T::Level::Bank] == bank && rows[row] == 1;
        }
};

template <typename T, std::size_t max_rows>
class ParityBank {
private:
        const vector<CodedRegion<T, max_rows>> regions;
        unsigned long clock = 0;
        unsigned long will_finish = 0;
        bool is_busy = false;
        const unsigned long access_latency;
public:
        ParityBank(const vector<CodedRegion<T, max_rows>> regions,
                   const unsigned long latency) :
                regions(regions), access_latency(latency) {}
        bool can_serve_request(const ramulator::Request &primary,
                               const ramulator::Request &secondary) const
        {
                /* "primary" will be served by main memory. "secondary" might be
                 * serviceable by this parity bank. */
                /* FIXME: only supports parity banks with two XOR'd regions */
                bool compatible = (regions[0].contains_request_data(primary) &&
                                   regions[1].contains_request_data(secondary)) ||
                                  (regions[1].contains_request_data(primary) &&
                                   regions[0].contains_request_data(secondary));
                return !is_busy && compatible;
        }
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
        void tick()
        {
                if (is_busy && clock >= will_finish)
                        is_busy = false;
                clock++;
        }
};

}
#endif /*__CODING_H*/

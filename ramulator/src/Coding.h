#ifndef __CODING_H
#define __CODING_H

#include "Request.h"
#include <bitset>
#include <vector>

namespace coding
{

template <typename T, std::size_t R>
class CodedRegion {
private:
        const std::bitset<R> rows;
        const int bank;
public:
        CodedRegion(const std::bitset<R> &rows, const int &bank) :
                rows(rows), bank(bank) {}
        inline bool contains_request_data(const ramulator::Request &req) const
        {
                int row { req.addr_vec[(int)T::Level::Row] };
                return req.addr_vec[(int)T::Level::Bank] == bank && rows[row] == 1;
        }
};

template <typename T, std::size_t R>
class ParityBank {
private:
        const vector<CodedRegion<T, R>> regions;
        bool is_busy;
public:
        ParityBank(const vector<CodedRegion<T, R>> regions) : regions(regions) {}
        bool can_serve_request(const ramulator::Request &have,
                               const ramulator::Request &want) const
        {
                /* "have" will be served by main memory. "want" might be
                 * serviceable by this parity bank. */
                /* FIXME: only supports parity banks with two XOR'd regions */
                bool compatible = (regions[0].contains_request_data(have) &&
                                   regions[1].contains_request_data(want)) ||
                                  (regions[1].contains_request_data(have) &&
                                   regions[0].contains_request_data(want));
                return !is_busy && compatible;
        }
        bool lock()
        {
                if (!is_busy) {
                        is_busy = true;
                        return true;
                } else {
                        return false;
                }
        }
        void free()
        {
                is_busy = false;
        }
};

}
#endif /*__CODING_H*/

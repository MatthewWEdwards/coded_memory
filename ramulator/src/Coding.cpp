#include "Coding.h"

namespace coding
{

template <typename T, std::size_t R>
bool ParityBank<T, R>::lock()
{
        if (!is_busy) {
                is_busy = true;
                return true;
        } else {
                return false;
        }
}
template <typename T, std::size_t R>
void ParityBank<T, R>::free()
{
        is_busy = false;
}

}

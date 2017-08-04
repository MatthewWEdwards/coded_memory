#include "Coding.h"

int calc_log2(const int& val)
{
        int n {0};
        int v {val};
        while ((v >>= 1))
                n++;
        return n;
}


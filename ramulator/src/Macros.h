#ifndef __MACROS_H
#define __MACROS_H

#define MEMORY_CODING
#define DEBUG
#define CODING_SCHEME 1

#define NUM_BANKS 8

#define CAT_I(a,b) a##b
#define CAT(a,b) CAT_I(a, b)
#define ParityBankTopologyConstructor(regions) \
	CAT(coding::ParityBankTopology_Scheme, CODING_SCHEME<T>)(regions)

#endif /* __Macros_H */

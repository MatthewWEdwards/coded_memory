#ifndef __MACROS_H
#define __MACROS_H

#define MEMORY_CODING
#define CODING_SCHEME 1


#define NUM_BANKS 8
#if CODING_SCHEME==2
	#undef NUM_BANKS
	#define NUM_BANKS 7
#endif

#define CAT_I(a,b) a##b
#define CAT(a,b) CAT_I(a, b)
#define ParityBankTopologyConstructor(regions) \
	CAT(coding::ParityBankTopology_Scheme, CODING_SCHEME<T>)(regions)

#endif

/* A test whether rdtsc, rdtscp, and cpuid can be used on the given processor.
   Please use this test program to trouble shoot problems with rdtsc.
   Note: the macros need a x86_64 architecture. Additionally, problems have
   been reported when used with some virtual machine monitors / hypervisors.

   2016-01-02 / 2016-01-02 Pirmin Schmid
*/

#include <inttypes.h>
#include <stdio.h>  
#include "rdtsc.h"  

int main() {
	uint64_t stop = 0;
	uint64_t start = 0;
	
	RDTSC_START(start); 
	// nothing
	RDTSC_STOP(stop);

	printf("\nTime measurement baseline: %" PRIu64 "\n.", stop - start);
	return 0;
}

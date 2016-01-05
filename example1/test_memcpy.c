/* A simple demo program co illustrate the use of this library.
   memcpy() is compared to a loop of copying the data.

   Note: While memcpy() is much faster than the loop when compiled with -O0,
   the results are very close when compiled with -O3 -march=native
   on Haswell (4th generation i7). memcpy() is still faster.

   8 KB of data (data2), after outlier removal
   -O0: loop    6723.2 ± 117.2 cycles (mean ± sd), 95% CI for the mean [6690.8, 6755.5], n=53
        memcpy   239.7 ± 19.0 cycles (mean ± sd), 95% CI for the mean [234.5, 244.9], n=54

   -O3: loop     243.5 ± 1.7 cycles (mean ± sd), 95% CI for the mean [243.1, 243.9], n=61
        memcpy   242.6 ± 3.3 cycles (mean ± sd), 95% CI for the mean [241.7, 243.4], n=60


   v1.0 2016-01-05 / 2016-01-05 Pirmin Schmid
*/

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "benchmark.h"

//--- data set 1 - random values, normal distribution --------------------------
//    generator settings: mean = 1'000'000, sd = 100'000, n = 101
//    from testing/test_stat_functions/test_stat_functions.c

static uint64_t data1[] = {
996741,
1042651,
757072,
1078921,
919322,
1038198,
935586,
837703,
874305,
1058255,
1060602,
945072,
811022,
984377,
1009921,
917695,
1111104,
1160768,
986824,
1088920,
955952,
1196703,
1018870,
916257,
907630,
1040466,
1069042,
918638,
997844,
1052655,
855711,
1074501,
1072637,
898349,
997692,
1155499,
1040669,
1017868,
1226173,
891234,
1067356,
1043179,
872030,
1047991,
1066673,
974536,
1073497,
1218791,
964708,
1055225,
1089842,
995410,
740516,
1011374,
1024122,
1121446,
919776,
1069853,
1045024,
1007487,
1023407,
1163792,
959350,
1049170,
1094754,
938595,
942773,
885211,
811808,
952822,
968111,
1122784,
1149973,
1114145,
1110608,
792954,
1008669,
925160,
1018784,
970606,
1114745,
1138732,
1017553,
965294,
1094759,
989196,
1035290,
952470,
857766,
910864,
819845,
991630,
878751,
766477,
963790,
1084276,
1002248,
1155900,
1012169,
1090662,
1057084
};

#define DATA1_N (sizeof(data1) / sizeof(*data1))

// second larger data set (8 KB)
#define DATA2_N 1024

static uint64_t data2[DATA2_N];


static uint64_t dest_loop[DATA2_N];
static uint64_t dest_memcpy[DATA2_N];


#define N 64

//--- reset --------------------------------------------------------------------

void init_memory(uint64_t *dest, int n_values) {
	for(int i = 0; i < n_values; i++) {
		dest[i] = i * 3;
	}
}

void reset_memory(uint64_t *dest, int n_values) {
	memset(dest, 0, n_values * sizeof(*dest));
}

bool cmp_memory(uint64_t *values1, uint64_t *values2, int n_values) {
	for(int i = 0; i < n_values; i++) {
		if(values1[i] != values2[i]) {
			return false;
		}
	}
	return true;	
}

//--- test functions -----------------------------------------------------------

typedef void (*testfunction)(uint64_t *, int);

void copy_with_loop(uint64_t *values, int n_values) {
	for(int i = 0; i < n_values; i++) {
		dest_loop[i] = values[i];
	}
}

void copy_with_memcpy(uint64_t *values, int n_values) {
	memcpy(dest_memcpy, values, n_values * sizeof(*values));
}

void test_function(testfunction f, char *title, uint64_t *values, int n_values, uint64_t *dest) {
	uint64_t stop = 0;
	uint64_t start = 0;
	reset_testbench();
	for(int i = 0; i < N; i++) {
		reset_memory(dest, n_values);

		RDTSC_START(start);
		f(values, n_values);
		RDTSC_STOP(stop);
		add_measurement(start, stop);
		if(!cmp_memory(values, dest, n_values)) {
			printf("Mismatch in copied values in test %s. Probably an optimization error.", title);
			exit(1);
		}
	}

	struct testbench_statistics stat = testbench_get_statistics();
	print_testbench_statistics(title, stat);
	print_histogram(title, stat);	
}

//--- main ---------------------------------------------------------------------

int main() {
	// init
	if( !create_testbench(N) ) {
		exit(1);
	}
	init_memory(data2, DATA2_N);

	// 1) loop
	test_function(copy_with_loop, "1) loop", data1, DATA1_N, dest_loop);

	// 2) memcpy
	test_function(copy_with_memcpy, "2) memcpy", data1, DATA1_N, dest_memcpy);

	// 3) loop, again (see/exclude potential caching benefit for memcpy)
	test_function(copy_with_loop, "3) loop, again", data1, DATA1_N, dest_loop);

	// 4) loop, more data
	test_function(copy_with_loop, "4) loop, more data", data2, DATA2_N, dest_loop);

	// 5) loop, more data
	test_function(copy_with_memcpy, "5) memcpy, more data", data2, DATA2_N, dest_memcpy);

	// 6) loop, more data, again (see/exclude potential caching benefit for memcpy)
	test_function(copy_with_loop, "6) loop, more data, again", data2, DATA2_N, dest_loop);

	// cleanup
	delete_testbench();
	return 0;
}

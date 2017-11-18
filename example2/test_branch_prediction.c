/* A test program to measure branch prediction by the CPU.
   compiled and tested on Intel i7 Haswell 2.5 GHz, OSX 10.11 using Clang 7.0.0

   v1.0.1 2015-11-24 / 2017-11-18 Pirmin Schmid
*/

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>   
#include "benchmark.h"  

// number of measurements -> results go into histogram
#define N 64

// number of inner loops (= denominator in the benchmark library)
// allows bringing short instruction blocks to a better measurable time
// (see: already the naked timing machinery needs about 24 cycles)

//#define N_inner_loop 32

//#define N_inner_loop 4

#define N_inner_loop 64   

// two different sets of operations for different questions
// leave one set uncommented and the other commented

// 1) uses one source variable for the assignments
// -> some sync associated but allows test whether counting actually works properly

//*
#define UP_OP (++updown)
#define DWN_OP (--updown)
#define SHOW_UPDOWN   
//*/

// 2) two different source variable that may allow some instruction level parallelism (addition)
//    these were also included in the original example on the slice
//    currently no check possible whether sum at the end is correct

/*
#define UP_OP ++cnt_gt
#define DWN_OP ++cnt_le
//*/

//--- global helper variables -------------------------------------------------- 
// deliberately not static to remove some optimization option for compiler
int64_t cnt_gt = 0;
int64_t cnt_le = 0;
int64_t updown = 0;

int64_t global_result_updown = 0;
volatile int64_t volatile_result_updown = 0;


//--- test routines -------------------------------------------------------------

// stores value in local variable on stack, no interdependence
uint64_t choose_cond(int x, int y) {
	uint64_t result;
	if(x < y) {
		result = UP_OP;
		// note: this assignment is independent of earlier assignments to result
	}
	else {
		result = DWN_OP;
		// note: this assignment is independent of earlier assignments to result
	}
	return result;
}


// stores value in global non-volatile variable
uint64_t choose_cond_modified(int x, int y) {
	if(x < y) {
		// make sure that this is dependent on earlier result
		//global_result = ++cnt_gt;
		global_result_updown = UP_OP;
	}
	else {
		// make sure that this is dependent on earlier result
		//global_result = ++cnt_le;
		global_result_updown = DWN_OP;
	}
	return global_result_updown;
}


// stores value in global volatile variable
uint64_t choose_cond_blocking(int x, int y) {
	if(x < y) {
		// make sure that this is dependent on earlier result
		//global_volatile_result = ++cnt_gt;
		volatile_result_updown = UP_OP;
	}
	else {
		// make sure that this is dependent on earlier result
		//global_volatile_result = ++cnt_le;
		volatile_result_updown = DWN_OP;
	}
	return volatile_result_updown;
}


uint64_t nothing(int x, int y) {
	return 0;
}

uint64_t no_branch(int x, int y) {
	uint64_t result = UP_OP;
	return result;
}

uint64_t no_branch_modified(int x, int y) {
	global_result_updown = UP_OP;
	return global_result_updown;
}

uint64_t no_branch_blocking(int x, int y) {
	volatile_result_updown = UP_OP;
	return volatile_result_updown;
}

//--- data generation ----------------------------------------------------------
//    step by step

enum options {
	ONE,
	TWO,
	THREE,
	FOUR,
	ALTERNATING,
	RANDOM
};

#define N_RANDOM 4

static int old_plusminus = 1;
static int old_multi = 0;
static int random_count[N_RANDOM];

void reset_data_generation() {
	old_plusminus = 1;
	old_multi = 0;
	for(int i=0; i < N_RANDOM; i++) {
		random_count[i] = 0;
	}
}

int next_plusminus(enum options which) {
	int r = 0;
	switch(which) {
		case ONE:
			return 1;
		case TWO:
			return -1;
		case ALTERNATING:
			old_plusminus = -old_plusminus;
			return old_plusminus;
		case RANDOM:
			r = rand() % 2;
			random_count[r]++;
			return r == 1 ? 1 : -1;
		default:
			fprintf(stderr, "invalid option in next_plusminus\n");
			return 0;
	}
}

int next_multi(enum options which) {
	int r = 0;
	switch(which) {
		case ONE:
			return 0;
		case TWO:
			return 1;
		case THREE:
			return 2;
		case FOUR:
			return 3;	
		case ALTERNATING:
			old_multi = (old_plusminus + 1) % 4;
			return old_multi;
		case RANDOM:
			r = rand() % 4;
			random_count[r]++;
			return r;
		default:
			fprintf(stderr, "invalid option in next_multi\n");
			return 0;
	}
}

// small array of data for inner loops

#if N > N_inner_loop
static int xs[N];
static int ys[N];
#else
static int xs[N_inner_loop];
static int ys[N_inner_loop];
#endif
//--- timing using rdtsc -------------------------------------------------------
//    limitation: has its own problems on multicore machines
//    beware of strange outliers. preemptive multitasking and interrupts are
//    *not* deactivated here as they should be (recommended by G. Paolini in
//    http://www.intel.com/content/www/us/en/embedded/training/ia-32-ia-64-benchmark-code-execution-paper.html )
//    (no Kernel module on OSX)

//--- specific test cases ------------------------------------------------------

typedef uint64_t (*cond_function)(int, int);
typedef int (*value_generator)(enum options);

void prepare_cond_data(enum options which, int n) {
	reset_data_generation();
	for(int i=0; i < n; i++) {
		xs[i] = 0;
		ys[i] = next_plusminus(which);
	}
}

void warmup_cond_test(cond_function f, int n, int n_inner) {
	uint64_t result = 0;
	uint64_t stop = 0;
	uint64_t start = 0;

	prepare_cond_data(RANDOM, n);

	reset_testbench();
	for(int i = 0; i < n; i++) {
		RDTSC_START(start);
		for(int j=0; j < n_inner; j++) { 
			result = f(xs[j], ys[j]);
		}
		RDTSC_STOP(stop);
		add_measurement(start, stop); 		
	}		
}

// one function call per measurement
// parameters determined programmatically freshly for each call
void run_cond_test_A(char *title, cond_function f, enum options which) {
	uint64_t result = 0;
	uint64_t stop = 0;
	uint64_t start = 0;

	int x = 0;
	int y = 0;

	updown = 0;
	global_result_updown = 0;
	volatile_result_updown = 0;

	reset_data_generation();
	reset_testbench();
	for(int i = 0; i < N; i++) {
		y = next_plusminus(which);
		RDTSC_START(start);
		result = f(x, y); 
		RDTSC_STOP(stop);
		add_measurement(start, stop); 		
	}

	global_result_updown = result;
	// just make sure that the result is used and not optimized away

#ifdef SHOW_UPDOWN
	printf("\n%s: updown %" PRId64 " result_updown %" PRId64 " volatile_result_updown %" PRId64 "\n",
		title, updown, global_result_updown, volatile_result_updown);
#endif

	struct testbench_statistics stat = testbench_get_statistics();
	print_testbench_statistics(title, &stat, NULL);
	if(which == RANDOM) {
		printf("+1 / -1 ratio = %f\n", (double)random_count[0] / (double)random_count[1]);
	}
	set_outlier_detection_mode(TESTBENCH_OUTLIER_DETECTION_HISTOGRAM);
	print_histogram(title, &stat, NULL);
}

// inner loop
void run_cond_test_B(char *title, cond_function f, enum options which) {
	uint64_t result = 0;
	uint64_t stop = 0;
	uint64_t start = 0;

	prepare_cond_data(which, N_inner_loop);

	updown = 0;
	global_result_updown = 0;
	volatile_result_updown = 0;

	reset_testbench();
	for(int i = 0; i < N; i++) {
		RDTSC_START(start); 
		for(int j=0; j < N_inner_loop; j++) { 
			result = f(xs[j], ys[j]);
		}
		RDTSC_STOP(stop);
		add_measurement(start, stop); 		
	}

	global_result_updown = result;
	// just make sure that the result is used and not optimized away

#ifdef SHOW_UPDOWN
	printf("\n%s: updown %" PRId64 " result_updown %" PRId64 " volatile_result_updown %" PRId64 "\n", title, updown, global_result_updown, volatile_result_updown);
#endif

	struct testbench_statistics stat = testbench_get_statistics();
	print_testbench_statistics(title, &stat, NULL);
	if(which == RANDOM) {
		printf("+1 / -1 ratio = %f\n", (double)random_count[0] / (double)random_count[1]);
	}
	set_outlier_detection_mode(TESTBENCH_OUTLIER_DETECTION_HISTOGRAM);
	print_histogram(title, &stat, NULL);
}

void run_cond_test_local(char *title, enum options which, int n, int n_inner) {
	uint64_t result = 0;
	uint64_t stop = 0;
	uint64_t start = 0;

	int x = 0;
	int y = 0;

	updown = 0;
	global_result_updown = 0;
	volatile_result_updown = 0;
	cnt_gt = 0;
	cnt_le = 0;

	reset_testbench();
	if(n_inner > 1) {
		prepare_cond_data(which, n_inner);
		for(int i = 0; i < n; i++) {
			RDTSC_START(start); 
			for(int j=0; j < n_inner; j++) { 
				if(xs[j] < ys[j]) {
					result = UP_OP;
				}
				else {
					result = DWN_OP;
				}
			}
			RDTSC_STOP(stop);
			add_measurement(start, stop); 		
		}
	}
	else {
		reset_data_generation();
		for(int i = 0; i < n; i++) {
			y = next_plusminus(which);
			RDTSC_START(start); 
			if(x < y) {
				result = UP_OP;
			}
			else {
				result = DWN_OP;
			}
			RDTSC_STOP(stop);
			add_measurement(start, stop); 		
		}		
	}

	global_result_updown = result;
	// just make sure that the result is used and not optimized away

#ifdef SHOW_UPDOWN
	printf("\n%s: updown %" PRId64 " result_updown %" PRId64 " volatile_result_updown %" PRId64 "\n", title, updown, global_result_updown, volatile_result_updown);
#endif

	struct testbench_statistics stat = testbench_get_statistics();
	print_testbench_statistics(title, &stat, NULL);
	if(which == RANDOM) {
		printf("+1 / -1 ratio = %f\n", (double)random_count[0] / (double)random_count[1]);
	}
	set_outlier_detection_mode(TESTBENCH_OUTLIER_DETECTION_HISTOGRAM);
	print_histogram(title, &stat, NULL);
}

void run_cond_test_global(char *title, enum options which, int n, int n_inner) {
	uint64_t stop = 0;
	uint64_t start = 0;

	int x = 0;
	int y = 0;

	updown = 0;
	global_result_updown = 0;
	volatile_result_updown = 0;
	cnt_gt = 0;
	cnt_le = 0;

	reset_testbench();
	if(n_inner > 1) {
		prepare_cond_data(which, n_inner);
		for(int i = 0; i < n; i++) {
			RDTSC_START(start); 
			for(int j=0; j < n_inner; j++) { 
				if(xs[j] < ys[j]) {
					global_result_updown = UP_OP;
				}
				else {
					global_result_updown = DWN_OP;
				}
			}
			RDTSC_STOP(stop);
			add_measurement(start, stop); 		
		}
	}
	else {
		reset_data_generation();
		for(int i = 0; i < n; i++) {
			y = next_plusminus(which);
			RDTSC_START(start); 
			if(x < y) {
				global_result_updown = UP_OP;
			}
			else {
				global_result_updown = DWN_OP;
			}
			RDTSC_STOP(stop);
			add_measurement(start, stop); 		
		}		
	}

#ifdef SHOW_UPDOWN
	printf("\n%s: updown %" PRId64 " result_updown %" PRId64 " volatile_result_updown %" PRId64 "\n", title, updown, global_result_updown, volatile_result_updown);
#endif

	struct testbench_statistics stat = testbench_get_statistics();
	print_testbench_statistics(title, &stat, NULL);
	if(which == RANDOM) {
		printf("+1 / -1 ratio = %f\n", (double)random_count[0] / (double)random_count[1]);
	}
	set_outlier_detection_mode(TESTBENCH_OUTLIER_DETECTION_HISTOGRAM);
	print_histogram(title, &stat, NULL);
}

void run_cond_test_volatile(char *title, enum options which, int n, int n_inner) {
	uint64_t stop = 0;
	uint64_t start = 0;

	int x = 0;
	int y = 0;

	updown = 0;
	global_result_updown = 0;
	volatile_result_updown = 0;
	cnt_gt = 0;
	cnt_le = 0;

	reset_testbench();
	if(n_inner > 1) {
		prepare_cond_data(which, n_inner);
		for(int i = 0; i < n; i++) {
			RDTSC_START(start); 
			for(int j=0; j < n_inner; j++) { 
				if(xs[j] < ys[j]) {
					volatile_result_updown = UP_OP;
				}
				else {
					volatile_result_updown = DWN_OP;
				}
			}
			RDTSC_STOP(stop);
			add_measurement(start, stop);
		} 		
	}
	else {
		reset_data_generation();
		for(int i = 0; i < n; i++) {
			y = next_plusminus(which);
			RDTSC_START(start); 
			if(x < y) {
				volatile_result_updown = UP_OP;
			}
			else {
				volatile_result_updown = DWN_OP;
			}
			RDTSC_STOP(stop);
			add_measurement(start, stop);
		} 		
	}

#ifdef SHOW_UPDOWN
	printf("\n%s: updown %" PRId64 " result_updown %" PRId64 " volatile_result_updown %" PRId64 "\n", title, updown, global_result_updown, volatile_result_updown);
#endif

	struct testbench_statistics stat = testbench_get_statistics();
	print_testbench_statistics(title, &stat, NULL);
	if(which == RANDOM) {
		printf("+1 / -1 ratio = %f\n", (double)random_count[0] / (double)random_count[1]);
	}
	set_outlier_detection_mode(TESTBENCH_OUTLIER_DETECTION_HISTOGRAM);
	print_histogram(title, &stat, NULL);
}

// tests 1-3 use a function pointer to pass the actual function to be tested
// thus, the compiler has no option of inlining the function during optimization

// A series: just one function call per measurement, parameters generated programmatically every time.
// B series: inner loop of multiple calls, uses short array of arguments to have equal timing for ALT and RND, too
// 01: store result in stack variable
// 02: store result in global variable
// 03: store result in global volatile variable

void test_A01() {
	printf("\nA-01: call a conditional function with local return value (no inline).\n");
	warmup_cond_test(nothing, N, N);
	run_cond_test_A("A-01: nothing", nothing, ONE);
	warmup_cond_test(no_branch, N, N);
	run_cond_test_A("A-01: no branch", no_branch, ONE);

	warmup_cond_test(choose_cond, N, N);
	run_cond_test_A("A-01: Seq  +1", choose_cond, ONE);
	run_cond_test_A("A-01: Seq  -1", choose_cond, TWO);
	run_cond_test_A("A-01: Seq ALT", choose_cond, ALTERNATING);
	run_cond_test_A("A-01: Seq RND", choose_cond, RANDOM);
}

void test_A02() {
	printf("\nA-02: call a conditional function with global non-volatile return value (no inline).\n");
	warmup_cond_test(nothing, N, N);
	run_cond_test_A("A-02: nothing", nothing, ONE);
	warmup_cond_test(no_branch_modified, N, N);
	run_cond_test_A("A-02: no branch", no_branch_modified, ONE);

	warmup_cond_test(choose_cond_modified, N, N);
	run_cond_test_A("A-02: Seq  +1", choose_cond_modified, ONE);
	run_cond_test_A("A-02: Seq  -1", choose_cond_modified, TWO);
	run_cond_test_A("A-02: Seq ALT", choose_cond_modified, ALTERNATING);
	run_cond_test_A("A-02: Seq RND", choose_cond_modified, RANDOM);
}

void test_A03() {
	printf("\nA-03: call a conditional function with global volatile return value (no inline)\n");
	warmup_cond_test(nothing, N, N);
	run_cond_test_A("A-03: nothing", nothing, ONE);
	warmup_cond_test(no_branch_blocking, N, N);
	run_cond_test_A("A-03: no branch", no_branch_blocking, ONE);

	warmup_cond_test(choose_cond_blocking, N, N);
	run_cond_test_A("A-03: Seq  +1", choose_cond_blocking, ONE);
	run_cond_test_A("A-03: Seq  -1", choose_cond_blocking, TWO);
	run_cond_test_A("A-03: Seq ALT", choose_cond_blocking, ALTERNATING);
	run_cond_test_A("A-03: Seq RND", choose_cond_blocking, RANDOM);
}

void test_B01() {
	printf("\nB-01: call a conditional function with local return value (no inline).\n");
	warmup_cond_test(nothing, N, N_inner_loop);
	run_cond_test_A("B-01: nothing", nothing, ONE);
	warmup_cond_test(no_branch, N, N_inner_loop);
	run_cond_test_A("B-01: no branch", no_branch, ONE);

	warmup_cond_test(choose_cond, N, N_inner_loop);
	run_cond_test_B("B-01: Seq  +1", choose_cond, ONE);
	run_cond_test_B("B-01: Seq  -1", choose_cond, TWO);
	run_cond_test_B("B-01: Seq ALT", choose_cond, ALTERNATING);
	run_cond_test_B("B-01: Seq RND", choose_cond, RANDOM);
}

void test_B02() {
	printf("\nB-02: call a conditional function with global non-volatile return value (no inline).\n");
	warmup_cond_test(nothing, N, N_inner_loop);
	run_cond_test_A("B-02: nothing", nothing, ONE);
	warmup_cond_test(no_branch_modified, N, N_inner_loop);
	run_cond_test_A("B-02: no branch", no_branch_modified, ONE);

	warmup_cond_test(choose_cond_modified, N, N_inner_loop);
	run_cond_test_B("B-02: Seq  +1", choose_cond_modified, ONE);
	run_cond_test_B("B-02: Seq  -1", choose_cond_modified, TWO);
	run_cond_test_B("B-02: Seq ALT", choose_cond_modified, ALTERNATING);
	run_cond_test_B("B-02: Seq RND", choose_cond_modified, RANDOM);
}

void test_B03() {
	printf("\nB-03: call a conditional function with global volatile return value (no inline)\n");
	warmup_cond_test(nothing, N, N_inner_loop);
	run_cond_test_A("B-03: nothing", nothing, ONE);
	warmup_cond_test(no_branch_blocking, N, N_inner_loop);
	run_cond_test_A("B-03: no branch", no_branch_blocking, ONE);

	warmup_cond_test(choose_cond_blocking, N, N_inner_loop);
	run_cond_test_B("B-03: Seq  +1", choose_cond_blocking, ONE);
	run_cond_test_B("B-03: Seq  -1", choose_cond_blocking, TWO);
	run_cond_test_B("B-03: Seq ALT", choose_cond_blocking, ALTERNATING);
	run_cond_test_B("B-03: Seq RND", choose_cond_blocking, RANDOM);
}

// tests 4-6 test the same things as tests 1-3, but manual inlining of the test function
// guarantees that the function is indeed inlined.
// comparison of both ways may reveal differences as seen in preliminary testing.

// A series: just one inlined if condition per measurement
// B series: inner loop of multiple calls
// 01: store result in stack variable
// 02: store result in global variable
// 03: store result in global volatile variable

void test_A04(int n) {
	printf("\nA-04: inline call a conditional function with local return value (inlined).\n");
	run_cond_test_local("A-04: Warmup", TWO, n, 1);
	run_cond_test_local("A-04: Seq  +1", ONE, n, 1);
	run_cond_test_local("A-04: Seq  -1", TWO, n, 1);
	run_cond_test_local("A-04: Seq ALT", ALTERNATING, n, 1);
	run_cond_test_local("A-04: Seq RND", RANDOM, n, 1);
}

void test_A05(int n) {
	printf("\nA-05: inline call a conditional function with global non-volatile return value (inlined).\n");
	run_cond_test_global("A-05: Warmup", TWO, n, 1);
	run_cond_test_global("A-05: Seq  +1", ONE, n, 1);
	run_cond_test_global("A-05: Seq  -1", TWO, n, 1);
	run_cond_test_global("A-05: Seq ALT", ALTERNATING, n, 1);
	run_cond_test_global("A-05: Seq RND", RANDOM, n, 1);
}

void test_A06(int n) {
	printf("\nA-06: inline call a conditional function with global volatile return value (inlined).\n");
	run_cond_test_volatile("A-06: Warmup", TWO, n, 1);
	run_cond_test_volatile("A-06: Seq  +1", ONE, n, 1);
	run_cond_test_volatile("A-06: Seq  -1", TWO, n, 1);
	run_cond_test_volatile("A-06: Seq ALT", ALTERNATING, n, 1);
	run_cond_test_volatile("A-06: Seq RND", RANDOM, n, 1);
}

void test_B04(int n, int n_inner) {
	printf("\nB-04: inline call a conditional function with local return value (inlined).\n");
	run_cond_test_local("B-04: Warmup", TWO, n, n_inner);
	run_cond_test_local("B-04: Seq  +1", ONE, n, n_inner);
	run_cond_test_local("B-04: Seq  -1", TWO, n, n_inner);
	run_cond_test_local("B-04: Seq ALT", ALTERNATING, n, n_inner);
	run_cond_test_local("B-04: Seq RND", RANDOM, n, n_inner);
}

void test_B05(int n, int n_inner) {
	printf("\nB-05: inline call a conditional function with global non-volatile return value (inlined).\n");
	run_cond_test_global("B-05: Warmup", TWO, n, n_inner);
	run_cond_test_global("B-05: Seq  +1", ONE, n, n_inner);
	run_cond_test_global("B-05: Seq  -1", TWO, n, n_inner);
	run_cond_test_global("B-05: Seq ALT", ALTERNATING, n, n_inner);
	run_cond_test_global("B-05: Seq RND", RANDOM, n, n_inner);
}

void test_B06(int n, int n_inner) {
	printf("\nB-06: inline call a conditional function with global volatile return value (inlined).\n");
	run_cond_test_volatile("B-06: Warmup", TWO, n, n_inner);
	run_cond_test_volatile("B-06: Seq  +1", ONE, n, n_inner);
	run_cond_test_volatile("B-06: Seq  -1", TWO, n, n_inner);
	run_cond_test_volatile("B-06: Seq ALT", ALTERNATING, n, n_inner);
	run_cond_test_volatile("B-06: Seq RND", RANDOM, n, n_inner);
}

// a workaround against "too much optimization using vector operations"
int global_n = 0;
int global_n_inner = 0;

int main() {
	// initialization
	if( !create_testbench(TESTBENCH_STD_N) ) {
		fprintf(stderr, "Error: could not open testbench (memory?).\n");
		exit(1);
	}

	srand(time(NULL));
	global_n = N;
	global_n_inner = N_inner_loop;

	// tests
	// A series: no inner loops
	set_denominator(1);
	test_A01();
	test_A02();
	test_A03();

	test_A04(global_n);
	test_A05(global_n);
	test_A06(global_n);

	// B series: inner loops
	set_denominator(N_inner_loop);
	test_B01();
	test_B02();
	test_B03();

	test_B04(global_n, global_n_inner);
	test_B05(global_n, global_n_inner);
	test_B06(global_n, global_n_inner);

	// cleanup
	delete_testbench();
}

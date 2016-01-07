/* Helper library for benchmarking small pieces of code
   Uses rdtsc to get the clock counter. Note potential problems on multicore systems.
   This timing method is intended for short stretches of code only.
   The statistics functionality increased quite a bit during development.

   Features:
   - overhead by timing machinery is subtracted automatically (baseline)
   - allows additional statistical information
	 * robust: median, min/max, 1st and 3rd quartiles
	 * parametric, assuming normal distribution: mean +/- SD, 95% confidence interval of the mean
	   (the user has to check herself/himself whether parametric values make sense)
	 * simple histogram
	 * outlier removal (based on 1 to few times occurence only of one measurement)
	 * export of all values

   Potential problem: Storage of all values needs some space (a few cache lines).
   If this is a problem for the system to be tested, see the module
   tiny_benchmark.h, which needs less memory/cache, but offers only mean, min, max.
  
   The interfaces of both modules are kept as similar together as possible to allow
   a smooth transition back and forth.

   References:
   - Paoloni G. http://www.intel.com/content/www/us/en/embedded/training/ia-32-ia-64-benchmark-code-execution-paper.html
   - Press et al. Numerical recipes in C++ 2nd ed. Cambridge University Press
   - Schoonjans F. https://www.medcalc.org/manual

   v1.0 2015-11-25 / 2016-01-07 Pirmin Schmid, MIT License
*/

#ifndef _BENCHMARK_H_
#define _BENCHMARK_H_

#include "rdtsc.h"

#include <stdbool.h>
#include <stdint.h>   

// IMPORTANT: do not forget to re-compile benchmark.c if you have changed these
// macro values below.   

// defines the standard number of tests that can be hold by the test bench
// the actual capacity is not limited by this number but by the value that
// is passed as parameter in create_testbench()
//   
#define TESTBENCH_STD_N 128

// note: (N * 8) / 64 + 1 cache lines/blocks will be needed to store the data of this module
//       during the actual testing
//       Total memory requirements for post-processing are about 3x this value (outlier management).
//       However, post-processing will not be a problem for the needs of the test code during testing    
// N       cache lines
//  16         3   
//  32         5   
//  64         9
// 128        17   

#define TESTBENCH_STD_DENOMINATOR 1 

// During outlier detection, only values are kept that have more values than this defined cutoff
// value in the histogram for individual values   
#define TESTBENCH_STD_CUTOFF 1

// Outlier detection using the histogram method needs a minimum number of n to make sense
// again an arbitrary limit here
#define MIN_N_FOR_OUTLIER_DETECTION (20 * TESTBENCH_STD_CUTOFF)


// Defines the maximum number of bins used in histogram
#define MAX_BINS 16

struct testbench_statistics {
	int count;
	int denominator;
	uint64_t baseline;
	uint64_t absMin; // note: raw values, not yet divided by denominator
	uint64_t absMax;
	// robust
	double min;
	double q1;
	double median;
	double q3;
	double max;
	// parametric (assume normal distribution)
	double mean;
	double sd;     // mean +/- sd
	double ci95_a; // 95% confidence interval [a,b] for the mean
	double ci95_b;
};

// initializes the test bench for a maximum of n measurements
// measures the baseline (timing overhead of the RDTSC macros)
// returns true if successful, or false otherwise
// sets all values to standard/default values
// determines the baseline for the system   
bool create_testbench(int capacity);

// option:
// you can set a denominator here if you had to use an inner
// loop in your test program to get to reasonable time values
// see: very short stretches of code of about 3-4 cycles
// compared to the timing system that takes about 25 cycles
// important: you need to set this value again if
// you create a new testbench in the same program
void set_denominator(int d);

// can start again testing using the same space
// a fresh baseline is calculated
void reset_testbench();

// frees the allocated memory
void delete_testbench();

// adds a new measurement to the list
// no range checking here to have as little interruption as possible
// input: start and stop == raw values gained by using RDTSC_START(start) and RDTSC_STOP(stop)
// note: the overhead of these macros "zero" / baseline is subtracted automatically within this function
void add_measurement(uint64_t start, uint64_t stop);

// statistics 
struct testbench_statistics testbench_get_statistics();

// prints the values if you like to import them into a statistics program
void print_testbench_values();

// prints the struct
// mainly to keep an indentical interface for this module and the tiny module
// NULL is allowed for title, if you do not want to print one
void print_testbench_statistics(char *title, struct testbench_statistics stat);

// prints a simple text histogram
// NULL is allowed for title, if you do not want to print one
// available histogram data is used to calculate a new statistic without outliers
// thie new statistic is printed with the same title and returned by the function
// for later use (optional)
struct testbench_statistics print_histogram(char *title, struct testbench_statistics stat);

// loads an array of raw values into the data store; baseline is not subtracted.
// This function is used to compare the statistics functions of this library with
// the results of a reference statistics software.
// returns true in case of success and false in case of error (i.e. not enough
// memory in the testbench).
bool development_load_raw_values(uint64_t *values, int n_values);

#endif // _BENCHMARK_H_   
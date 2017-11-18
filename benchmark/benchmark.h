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
     * 2 modes of outlier detection (based on histogram or on SD); see comment below on caveat
     * printing: conversion to other units
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

   Note: see improved StatisticsHelper library in C++14 that offers various additional features
   - generic use; not associated to rdtsc
   - works with doubles in a numerically robust implementation
   - added: harmonic and geometric mean and 95% CI
   - more detailed t-distribution table

   Link: https://github.com/pirminschmid/CppToolbox


   v1.1 2015-11-25 / 2017-11-18 Pirmin Schmid, MIT License
*/

#ifndef BENCHMARK_BENCHMARK_H_
#define BENCHMARK_BENCHMARK_H_

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

// Defines the maximum number of bins used in histogram
#define TESTBENCH_MAX_BINS 16

// Please note: outlier detection always comes with pitfalls and should be avoided in general for reporting.
// However, it can be useful in specific situations such as this microbenchmarking here on a machine that
// runs also many other processes, which introduce noise. You must be aware of pitfalls. Thus: Outlier detection
// and removal is always associated in this library with printing both histograms, before and after.
//
// There are currently 2 modes implemented for outlier detection:
// - histogram: can remove any kind of value even within the [min, max] range based on very low occurence
// - standard deviation: removes outliers that are far from mean; there are much better statistical methods
//   for outlier detection (e.g. Grubbs, Tukey or generalized ESD test) than SD used here. They are not
//   implemented here.
// and OFF: of course, it's best to work without outlier removal
// default is OFF
enum testbench_outlier_detection_mode {
    TESTBENCH_OUTLIER_DETECTION_OFF,
    TESTBENCH_OUTLIER_DETECTION_HISTOGRAM,
    TESTBENCH_OUTLIER_DETECTION_SD
};

// Outlier detection using the histogram method needs a minimum number of n to make sense
// again an arbitrary limit here.
// Additionally, it only makes sense as long as measurements are expected to be in a close range,
// i.e. not being spread over a very large range with single occurence of each meassured value
#define TESTBENCH_OUTLIER_DETECTION_HISTOGRAM_MIN_N (20 * TESTBENCH_STD_CUTOFF)
#define TESTBENCH_OUTLIER_DETECTION_HISTOGRAM_MAX_BIN_SIZE 10

// configuration for SD mode
// SD limit should not be below 3 here
#define TESTBENCH_OUTLIER_DETECTION_SD_MIN_N 20
#define TESTBENCH_OUTLIER_DETECTION_SD_MIN_SD 3


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

// these units are system dependent
struct testbench_time_unit {
    const char *name;
    uint64_t cycles_per_unit;
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
// compared to the timing system with RDTSC that takes about 25 cycles
// important: you need to set this value again if
// you create a new testbench in the same program
// note: can be set after data collection
void set_denominator(int d);

// option: activate outlier detection (default is OFF)
// note: can be set after data collection & analysis, just before printing the histogram
void set_outlier_detection_mode(enum testbench_outlier_detection_mode mode);

// can start again testing using the same space
// a fresh baseline is calculated
void reset_testbench(void);

// frees the allocated memory
void delete_testbench(void);

// adds a new measurement to the list
// no range checking here to have as little interruption as possible
// input: start and stop == raw values gained by using RDTSC_START(start) and RDTSC_STOP(stop)
// note: the overhead of these macros "zero" / baseline is subtracted automatically within this function
void add_measurement(uint64_t start, uint64_t stop);

// statistics 
struct testbench_statistics testbench_get_statistics(void);

// prints the values if you like to import them into a statistics program
void print_testbench_values(void);

// prints the struct
// mainly to keep an indentical interface for this module and the tiny module
// NULL is allowed for title, if you do not want to print one
// NULL is allowed for unit to use default unit cycles
void print_testbench_statistics(const char *title, const struct testbench_statistics *stat,
                                const struct testbench_time_unit *unit);

// prints a simple text histogram
// NULL is allowed for title, if you do not want to print one
// NULL is allowed for unit to use default unit cycles
// available histogram data is used to calculate a new statistic without outliers
// thie new statistic is printed with the same title and returned by the function
// for later use (optional)
struct testbench_statistics print_histogram(const char *title, const struct testbench_statistics *stat,
                                            const struct testbench_time_unit *unit);

// loads an array of raw values into the data store; baseline is not subtracted.
// This function is used to compare the statistics functions of this library with
// the results of a reference statistics software.
// returns true in case of success and false in case of error (i.e. not enough
// memory in the testbench).
bool development_load_raw_values(const uint64_t *values, int n_values);

#endif // BENCHMARK_BENCHMARK_H_

/**
 * Helper library for benchmarking small pieces of code
 *  Uses rdtsc to get the clock counter. Note potential problems on multicore systems.
 *  This timing method is intended for short stretches of code only.
 *  The statistics functionality increased quite a bit during development.
 *
 *  Features:
 *  - overhead by timing machinery is subtracted automatically (baseline)
 *  - allows additional statistical information
 *    * robust: median, min/max, 1st and 3rd quartiles
 *    * parametric, assuming normal distribution: mean +/- SD, 95% confidence interval of the mean
 *      (the user has to check herself/himself whether parametric values make sense)
 *    * simple histogram
 *    * 2 modes of outlier detection (based on histogram or on SD); see comment below on caveat
 *    * printing: conversion to other units
 *    * export of all values
 *
 *  Potential problem: Storage of all values needs some space (a few cache lines).
 *  If this is a problem for the system to be tested, see the module
 *  tiny_benchmark.h, which needs less memory/cache, but offers only mean, min, max.
 *
 *  The interfaces of both modules are kept as similar together as possible to allow
 *  a smooth transition back and forth.
 *
 *  References:
 *  - Lentner C (ed). Geigy Scientific Tables, 8th edition, Volume 2. Basel: Ciba-Geigy Limited, 1982
 *  - Paoloni G. http://www.intel.com/content/www/us/en/embedded/training/ia-32-ia-64-benchmark-code-execution-paper.html
 *  - Press et al. Numerical recipes in C++ 2nd ed. Cambridge University Press
 *  - Schoonjans F. https://www.medcalc.org/manual
 *  - Schoonjans F, De Bacquer D, Schmid P. Estimation of population percentiles. Epidemiology 2011;22:750-751.
 *
 *  Note: see improved StatisticsHelper library in C++14 that offers various additional features
 *  - generic use; not associated to rdtsc
 *  - works with doubles in a numerically robust implementation
 *  - added: harmonic and geometric mean and 95% CI
 *  - more detailed t-distribution table
 *
 *  Link: https://github.com/pirminschmid/CppToolbox
 *
 *
 *  v1.2 2015-11-25 / 2017-11-21 Pirmin Schmid, MIT License
 */

#ifndef BENCHMARK_BENCHMARK_H_
#define BENCHMARK_BENCHMARK_H_

#include "rdtsc.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// IMPORTANT: do not forget to re-compile benchmark.c if you have changed these
// macro values below.

/**
 * defines the standard number of tests that can be hold by the test bench
 * the actual capacity is not limited by this number but by the value that
 * is passed as parameter in create_testbench()
 */
#define TESTBENCH_STD_N 128

/**
 * note: (N * 8) / 64 + 1 cache lines/blocks will be needed to store the data of this module
 *       during the actual testing
 *       Total memory requirements for post-processing are about 3x this value (outlier management).
 *       However, post-processing will not be a problem for the needs of the test code during testing
 *   N     cache lines
 *  16         3
 *  32         5
 *  64         9
 * 128        17
 */
#define TESTBENCH_STD_DENOMINATOR 1

/**
 * During outlier detection, only values are kept that have more values than this defined cutoff
 * value in the histogram for individual values
 */
#define TESTBENCH_STD_CUTOFF 1

/**
 *  Defines the maximum number of bins used in histogram
 */
#define TESTBENCH_MAX_BINS 16

/**
 * Please note: outlier detection always comes with pitfalls and should be avoided in general for reporting.
 * However, it can be useful in specific situations such as this microbenchmarking here on a machine that
 * runs also many other processes, which introduce noise. You must be aware of pitfalls. Thus: Outlier detection
 * and removal is always associated in this library with printing both histograms, before and after.
 *
 * There are currently 2 modes implemented for outlier detection:
 * - histogram: can remove any kind of value even within the [min, max] range based on very low occurence
 * - standard deviation: removes outliers that are far from mean; there are much better statistical methods
 *   for outlier detection (e.g. Grubbs, Tukey or generalized ESD test) than SD used here. They are not
 *   implemented here.
 * and OFF: of course, it's best to work without outlier removal
 * default is OFF
 */
enum testbench_outlier_detection_mode {
    TESTBENCH_OUTLIER_DETECTION_OFF,
    TESTBENCH_OUTLIER_DETECTION_HISTOGRAM,
    TESTBENCH_OUTLIER_DETECTION_SD
};

/**
 * Outlier detection using the histogram method needs a minimum number of n to make sense
 * again an arbitrary limit here.
 * Additionally, it only makes sense as long as measurements are expected to be in a close range,
 * i.e. not being spread over a very large range with single occurence of each meassured value
 */
#define TESTBENCH_OUTLIER_DETECTION_HISTOGRAM_MIN_N (20 * TESTBENCH_STD_CUTOFF)
#define TESTBENCH_OUTLIER_DETECTION_HISTOGRAM_MAX_BIN_SIZE 10

/**
 * configuration for SD mode
 * SD limit should not be below 3 here
 */
#define TESTBENCH_OUTLIER_DETECTION_SD_MIN_N 20
#define TESTBENCH_OUTLIER_DETECTION_SD_MIN_SD 3


struct testbench_statistics {
    size_t count;
    size_t denominator;
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

/**
 * These units are system dependent.
 * notes:
 * - even throughput units (e.g. MiB/s) can be built using a function that takes the
 *   total size of transmitted data as argument
 * - the units are only applied while printing the data / histogram
 */
struct testbench_time_unit {
    const char *name;
    uint64_t cycles_per_unit;
};

/**
 * \param capacity  maximum number of measurements
 * \return          true if successful; false otherwise
 *
 * Initializes the test bench for a maximum of capacity measurements.
 * Determines the baseline (timing overhead of the RDTSC macros);
 * the statistics on this baseline is reported.
 * Sets all values to standard/default values.
 */
bool create_testbench(size_t capacity);

/**
 * \param denominator  denomainator; must be >= 1; default TESTBENCH_STD_DENOMINATOR
 *
 * This is an option:
 * If the number of instructions under test is very small, and only few cycles
 * are to be expected in comparison to the RDTSC measurement that takes about
 * 24 cycles, a loop can be used to run the test multiple times while
 * measuring the time. The denominator needs to be set to this loop size.
 * Please note: this will warm up the caches. Thus, it should only be done
 * with pre-warmed caches. It may not make sense to use this in all situations.
 * notes:
 * - needs to be set again if a new test bench is created, but remains
 *   active after reset()
 * - can be set after data collection
 */
void set_denominator(size_t denominator);

/**
 * \param mode  outlier detection mode; default TESTBENCH_OUTLIER_DETECTION_OFF
 *
 * note: can be set after data collection & analysis, just before printing the histogram
 */
void set_outlier_detection_mode(enum testbench_outlier_detection_mode mode);

/**
 * storage space is reset to allow new measurment data
 * notes:
 * - baseline is NOT determined again
 * - options (denominator and outlier detection mode are kept)
 */
void reset_testbench(void);

/**
 *  frees the allocated memory
 */
void delete_testbench(void);

/**
 * \param start  raw value as determined with RDTSC_START
 * \param stop   raw value as determined with RDTSC_STOP
 *
 * notes:
 * - no range checking here to have as little interruption as possible
 * - the overhead of these macros "zero" line / baseline is subtracted automatically within this function
 */
void add_measurement(uint64_t start, uint64_t stop);

/**
 * calculates the descriptive statistics values
 */
struct testbench_statistics testbench_get_statistics(void);

/**
 * \param stream  FILE object
 * \param title   title written as a comment (# prefix)
 *                note: additionally, the baseline will be printed as an additional comment
 * \param unit    optional; cycles are used if NULL
 * \return        true if successful without I/O errors; false otherwise
 *
 * Prints the values for e.g. import into a statistics program
 */
bool fprint_testbench_values(FILE *stream, const char *title, const struct testbench_time_unit *unit);

/**
 * No return value (mainly for compatibility with former interface)
 */
static inline void print_testbench_values(const char *title, const struct testbench_time_unit *unit)
{
    fprint_testbench_values(stdout, title, unit);
}

/**
 * \param stream  FILE object
 * \param title   optional; none is used if NULL
 * \param stat    calculated descriptive statistics
 * \param unit    optional; cycles are used if NULL
 * \return        true if successful without I/O errors; false otherwise
 *
 * Prints descriptive statistic values.
 */

bool fprint_testbench_statistics(FILE *stream, const char *title,
                                 const struct testbench_statistics *stat,
                                 const struct testbench_time_unit *unit);

/**
 * No return value (mainly for compatibility with former interface)
 */
static inline void print_testbench_statistics(const char *title,
                                              const struct testbench_statistics *stat,
                                              const struct testbench_time_unit *unit)
{
    fprint_testbench_statistics(stdout, title, stat, unit);
}

/**
 * \param stream  FILE object
 * \param title   optional; none is used if NULL
 * \param stat    calculated descriptive statistics
 * \param unit    optional; cycles are used if NULL
 * \param ret_ok  true if successful without I/O errors; false otherwise
 * \return        statistics without outliers for potential later use
 *
 * Notes:
 * - The receiving variable must be different than the provided stat argument.
 * - C like, the function can also be called without this receiving variable.
 * - Histogram: currently fixed size (width 50 == 100%, i.e. 2% / char)
 *   currently used: * for 2% and . for additional 1%
 */
struct testbench_statistics fprint_histogram(FILE *stream, const char *title,
                                             const struct testbench_statistics *stat,
                                             const struct testbench_time_unit *unit,
                                             bool *ret_ok);

/**
 * No ret_ok argument (mainly for compatibility with former interface)
 */
static inline struct testbench_statistics print_histogram(const char *title,
                                             const struct testbench_statistics *stat,
                                             const struct testbench_time_unit *unit)
{
    bool ret_ok;
    return fprint_histogram(stdout, title, stat, unit, &ret_ok);
}

/**
 * \param values    an array of uint64_t values
 * \param n_values  array size (must be <= capacity)
 * \return          true in case of success; false otherwise
 *
 * Loads an array of raw values into the data store; baseline is not subtracted here.
 */
bool development_load_raw_values(const uint64_t *values, size_t n_values);

/**
 * \param values_buffer       a buffer to store the values
 * \param n_values_capacity   array capacity
 * \param ret_n_values        returns number of actual values that were copied
 * \return                    true in case of success; false otherwise
 *
 * Copies the raw values from data store into the values_buffer.
 * Note: NO copy operation if n_values_capacity < effective available values
 * best use capacity used for testbench init to allocate the buffer.
 */
bool development_get_raw_values(uint64_t *values_buffer, size_t n_values_capacity, size_t *ret_n_values);

/**
 * \param lambda
 *
 * Maps all values to different values. This can be used to run additional
 * transformations, e.g. needed for simple throughput calculations using
 * the statistics functions provided in this library.
 *
 * Notes:
 * - input values may contain 0; protect against DIV/0
 * - scale lambda function to have again a wide range of values in the uint64_t space:
 *   values should not be compressed around 0 after lambda function.
 */
typedef uint64_t (*testbench_lambda_function_t)(uint64_t value);

void development_map_values(testbench_lambda_function_t lambda);

#endif // BENCHMARK_BENCHMARK_H_

/* Helper library for benchmarking small pieces of code
   Uses rdtsc to get the clock counter. Note potential problems on multicore systems.

   References:
   - Paoloni G. http://www.intel.com/content/www/us/en/embedded/training/ia-32-ia-64-benchmark-code-execution-paper.html
   - Press et al. Numerical recipes in C++ 2nd ed. Cambridge University Press
   - Schoonjans F. https://www.medcalc.org/manual

   v0.9 2015-11-25 / 2016-01-02 Pirmin Schmid, MIT License
*/

#include "benchmark.h"

#include <assert.h>
#include <float.h>
#include <inttypes.h>
#include <math.h> 
#include <stdio.h>   
#include <stdlib.h> 

//--- private data -------------------------------------------------------------
  
// some internal variables

static uint64_t baseline = 0;

// raw data stored from measurement
static uint64_t *data = NULL;

// temporary internal data for outlier removal
// allocated at the beginning to avoid lots of mallocs() later
static uint64_t *data_without_outliers = NULL;

// additional temporary internal data for outlier removal
// allocated at the beginning to avoid lots of mallocs() later
static uint64_t *data_working_temp = NULL;

static int cap = 0;
static int count = 0;
static int denominator = 1;   

// values of t-distribution (two-tailed) for 100*(1-alpha)% = 95% (alpha level 0.05)
// from https://www.medcalc.org/manual/t-distribution.php
// abbreviated use here
struct distr {
	int df;
	double t_value;
};

// arbitrarily set
#define T_TABLE_INFINITY 1024

static struct distr t_table[] = {
	{T_TABLE_INFINITY, 1.960},
	{300, 1.968},
	{100, 1.984},
	{80, 1.990},
	{60, 2.000},
	{50, 2.009},
	{40, 2.021},
	{30, 2.042},
	{20, 2.086},
	{18, 2.101},
	{16, 2.120},
	{14, 2.145},
	{12, 2.179},
	{10, 2.228},
	{9, 2.262},
	{8, 2.306},
	{7, 2.365},
	{6, 2.447},
	{5, 2.571},
	{4, 2.776},
	{3, 3.182},
	{2, 4.303},
	{1, 12.706}
};

static int t_table_n = sizeof(t_table) / sizeof(t_table[0]);

//--- private helpers ----------------------------------------------------------

static double get_t_value(int n) {
	assert(n > 1);

	int df = n - 1;
	for(int i = 0; i < t_table_n; i++) {
		if(df >= t_table[i].df) {
			return t_table[i].t_value;
		}
	}

	// will not happen
	return 0.0;
}

static int cmp_uint64_t(const void *a, const void *b) {
	uint64_t a2 = *((uint64_t *)a);
	uint64_t b2 = *((uint64_t *)b);
	if(a2 < b2) {
		return -1;
	}
	if(a2 > b2) {
		return 1;
	}
	return 0;
}

#define PERC_ATOL 0.001
// note, the expected fractional parts for r_frac will be 0.0, 0.25, 0.5, 0.75
// thus, no need to test for machine precision if( r_frac <= (DBL_EPSILON * r_p) )
// PERC_ATOL is currently chosen to allow for some "reserve" if get_percentile is chosen
// to be used for smaller percentiles, too. The conditional is kept to avoid rounding errors
// with calculations close to 0.0.

// calculates percentile value as described in
// https://www.medcalc.org/manual/summary_statistics.php
// see Lentner C (ed). Geigy Scientific Tables, 8th edition, Volume 2. Basel: Ciba-Geigy Limited, 1982
//     Schoonjans F, De Bacquer D, Schmid P. Estimation of population percentiles. Epidemiology 2011;22:750-751.
// percentile must be in range (0,1) -- note: not (0,100) here.
// modified to include the denominator, too.
static double get_percentile(uint64_t *sorted_values, int n_values, double percentile, int denominator) {
	double n = (double)n_values;
	assert(1.0/n <= percentile && percentile <= (n - 1.0)/n);
	double r_p = 0.5 + percentile * n;
	// note: only valid if 1/n <= percentile <= (n-1)/n
	// i.e. n must be at very least 4 for percentiles == 0.25 (quartile 1) and 0.75 (quartile 3)
	//                              2 for percentile == 0.5 (median)

	double r_floor = floor(r_p);
	double r_frac = r_p - r_floor; // in range [0,1); also == abs(r_floor - r_p)

	double result = 0.0;
	if(r_frac < PERC_ATOL) {
		// integer number, use (r_floor - 1) as array index and return value
		int r_ind = (int)r_floor - 1;
		assert(0 <= r_ind && r_ind < n_values);
		result = (double)sorted_values[r_ind];
	}
	else {
		// use linear interpolation for fractional part
		// benefits and limitations -> see mentioned papers
		int r_ind = (int)r_floor - 1;
		int r_ind2 = r_ind + 1;
		assert(0 <= r_ind && r_ind2 < n_values);
		result = (1.0 - r_frac) * (double)sorted_values[r_ind];  // the closer to 0.0 the more weight
		result += r_frac * (double)sorted_values[r_ind2];        // the closer to 1.0 the more weight
	}

	return result / ((double)denominator);
}

static struct testbench_statistics calc_statistics(uint64_t *values, int n_values);

static void print_testbench_statistics_including_outliers(char *title, struct testbench_statistics stat, int removed_outliers);

static struct testbench_statistics print_histogram_and_remove_outliers(char *title, struct testbench_statistics stat,
	                                                           uint64_t *values, int n_values, bool test_for_outliers);

//--- implementation of the public API -----------------------------------------
// see header file for information about the functions

bool create_testbench(int capacity) {
	uint64_t start = 0;
	uint64_t stop = 0;
	
	if(capacity < 1) {
		return false;
	}

	if(data) {
		delete_testbench();
	}

	data = malloc(capacity * sizeof(*data));
	if(!data) {
		return false;
	}

	data_without_outliers = malloc(capacity * sizeof(*data_without_outliers));
	if(!data_without_outliers) {
		free(data);
		return false;
	}

	data_working_temp = malloc(capacity * sizeof(*data_working_temp));
	if(!data_working_temp) {
		free(data_without_outliers);
		free(data);
		return false;
	}

	cap = capacity;
	denominator = 1;

	// establish baseline
	for(int i = 0; i < 3; i++) {
		// have 2 dry runs (warming up) and then one measurement
		for(int j = 0; j < cap; j++) {
			RDTSC_START(start);
			// nothing
			RDTSC_STOP(stop);
			data[j] = stop - start;
		}
	}

	count = cap;
	struct testbench_statistics baseline_stat = calc_statistics(data, count);
	print_testbench_statistics("baseline", baseline_stat);
	baseline_stat = print_histogram_and_remove_outliers("baseline", baseline_stat, data, count, true);
	baseline = baseline_stat.absMin;
	count = 0;
	denominator = TESTBENCH_STD_DENOMINATOR;
	return true;
}

void set_denominator(int d) {
	if(d < 1) {
		return;
	}

	denominator = d;
}

void reset_testbench() {
	count = 0;
}

void delete_testbench() {
	if(data_working_temp) {
		free(data_working_temp);
		data_working_temp = NULL;
	}

	if(data_without_outliers) {
		free(data_without_outliers);
		data_without_outliers = NULL;
	}

	if(data) {
		free(data);
		data = NULL;
		cap = 0;
		count = 0;
		baseline = 0;
		denominator = TESTBENCH_STD_DENOMINATOR;
	}
}

void add_measurement(uint64_t start, uint64_t stop) {
	// no array index check here
	uint64_t delta = stop - start - baseline;
	data[count++] = ((int64_t)delta) < 0 ? 0 : delta;
}


//--- testbench_get_statistics() with associated private function --------------

// calculates the statistics for an array values[] of size n_values
// this internal function is used by testbench_get_statistics() and print_histogram()
static struct testbench_statistics calc_statistics(uint64_t *values, int n_values) {
	// note: min/max deliberately not stored while adding measurements to avoid any
	// unnecessary cache interruption of the program to be measured
	struct testbench_statistics result = {0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
	result.denominator = denominator;
	result.baseline = baseline;
	if(n_values == 0) {
		return result;
	}

	result.count = n_values;

	// mean, min, max
	uint64_t sum = 0;
	uint64_t min = UINT64_MAX;
	uint64_t max = 0;
	for(int i = 0; i < n_values; i++) {
		sum += values[i];

		if(values[i] < min) {
			min = values[i];
		}
		
		if(values[i] > max) {
			max = values[i];
		}
	}

	double mean = (double)sum / ((double)denominator * (double)n_values);
	result.mean = mean;
	result.absMin = min;
	result.min = (double)min / (double)denominator;
	result.absMax = max;
	result.max = (double)max / (double)denominator;

	// robust
	if(n_values > 1) {
		qsort(values, n_values, sizeof(*values), cmp_uint64_t);
		result.median = get_percentile(values, n_values, 0.5, denominator);

		if(n_values > 3) {
			// just the bare minimum to work
			// of course these quartiles will have large 95% confidence intervals by themselves
			// thus: choose larger n_values for meaningful results, of course
			result.q1 = get_percentile(values, n_values, 0.25, denominator);
			result.q3 = get_percentile(values, n_values, 0.75, denominator);
		}
		else {
			result.q1 = result.min;
			result.q3 = result.max;			
		}
	}
	else {
		result.median = result.min;
		result.q1 = result.min;
		result.q3 = result.max;
	}

	// parametric (assuming normal distribution)
	if(n_values > 1) {
		// this is just the barely minimum to avoid div/0
		// of course, one should use a meaningful count / n_values
		double delta = 0.0;
		double s2 = 0.0;
		for(int i = 0; i < n_values; i++) {
			delta = (double)values[i] / (double)denominator - mean;
			s2 += delta * delta;
		}

		s2 = s2 / (double)(n_values - 1);
		s2 = sqrt(s2);
		result.sd = s2;
		double sem = s2 / sqrt((double)n_values);

		double ci95_delta = get_t_value(n_values) * sem;
		result.ci95_a = mean - ci95_delta;
		result.ci95_b = mean + ci95_delta;
	}

	return result;
}

struct testbench_statistics testbench_get_statistics() {
	// note: min/max deliberately not stored while adding measurements to avoid any
	// unnecessary cache interruption of the program to be measured
	return calc_statistics(data, count);
}


//--- print_testbench_values() -------------------------------------------------
void print_testbench_values() {
	for(int i=0; i < count; i++) {
		printf("%" PRIu64 "\n", data[i]);
	}
}


//--- print_testbench_statistics() with associated private function ------------

static void print_testbench_statistics_including_outliers(char *title, struct testbench_statistics stat, int removed_outliers) {
	if(title) {
		printf("\n%s:\n", title);
	}

	if(stat.count > 3) {
		// just the bare minimum to somewhat make sense
		// better use larger counts, of course
		printf("- robust:       median %.1f cycles, IQR [%.1f, %.1f], min %.1f, max %.1f, n=%d [%d outlier(s) removed], denominator=%d, baseline=%d\n",
			stat.median, stat.q1, stat.q3, stat.min, stat.max, stat.count, removed_outliers, stat.denominator, (int)stat.baseline);
		printf("- normal dist.: %.1f ± %.1f cycles (mean ± sd), 95%% CI [%.1f, %.1f], min %.1f, max %.1f, n=%d [%d outlier(s) removed], denominator=%d, baseline=%d\n",
			stat.mean, stat.sd, stat.ci95_a, stat.ci95_b, stat.min, stat.max, stat.count, removed_outliers, stat.denominator, (int)stat.baseline);
	}
	else {
		// there is not much that should be reported with such low counts
		printf("mean %.1f cycles, median %.1f, min %.1f, max %.1f, n=%d [%d outlier(s) removed], denominator=%d, baseline=%d; use n >= 4 for more detailed descriptive statistics.\n",
			stat.mean, stat.median, stat.min, stat.max, stat.count, removed_outliers, stat.denominator, (int)stat.baseline);
	}
}

void print_testbench_statistics(char *title, struct testbench_statistics stat) {
	if(title) {
		printf("\n%s:\n", title);
	}

	if(stat.count > 3) {
		// just the bare minimum to somewhat make sense
		// better use larger counts, of course
		printf("- robust:       median %.1f cycles, IQR [%.1f, %.1f], min %.1f, max %.1f, n=%d, denominator=%d, baseline=%d\n",
			stat.median, stat.q1, stat.q3, stat.min, stat.max, stat.count, stat.denominator, (int)stat.baseline);
		printf("- normal dist.: %.1f ± %.1f cycles (mean ± sd), 95%% CI [%.1f, %.1f], min %.1f, max %.1f, n=%d, denominator=%d, baseline=%d\n",
			stat.mean, stat.sd, stat.ci95_a, stat.ci95_b, stat.min, stat.max, stat.count, stat.denominator, (int)stat.baseline);
	}
	else {
		// there is not much that should be reported with such low counts
		printf("mean %.1f cycles, median %.1f, min %.1f, max %.1f, n=%d, denominator=%d, baseline=%d; use n >= 4 for more detailed descriptive statistics.\n",
			stat.mean, stat.median, stat.min, stat.max, stat.count, stat.denominator, (int)stat.baseline);
	}
}


//--- print_histogram() with associated private function -----------------------

static struct testbench_statistics print_histogram_and_remove_outliers(char *title, struct testbench_statistics stat,
	                                                           uint64_t *values, int n_values, bool test_for_outliers) {
	// currently fixed size (width 50, i.e. 2% / char)
	// currently used: * for 2% and . for additional 1%

	// some checks
	if(stat.max < stat.min) {
		return stat;
	}

	if(stat.count < 1) {
		return stat;
	}

	uint64_t min = stat.absMin / denominator;
	uint64_t max = stat.absMax / denominator;

	int delta = (int)(max - min);
	int bins = delta + 1;
	int size = 1;
	while(bins > MAX_BINS) {
		// arbitrary limit
		size <<= 1;
		bins = delta/size + 1;
	}

	if(title) {
		printf("%s (%d bins of size %d)\n", title, bins, size);
	}
	else {
		printf("(%d bins of size %d)\n", bins, size);		
	}

	int histogram[MAX_BINS];
	for(int i = 0; i < bins; i++) {
		histogram[i] = 0;
	}

	for(int i = 0; i < n_values; i++) {
		histogram[ ((int)(values[i] / denominator - min)) / size ]++;
	}

	int hist_max = 0;
	for(int i = 0; i < bins; i++) {
		if(histogram[i] > hist_max) {
			hist_max = histogram[i];
		}
	}

	for(int i=0; i < bins; i++) {
		int j = (histogram[i] * 100) / hist_max;
		if(size == 1) {
			printf("%4" PRIu64 " [%3d]: ", i + min, histogram[i]);
		}
		else {
			uint64_t offset = (i*size) + min;
			printf("%4" PRIu64 " - %4" PRIu64 " [%3d]: ", offset, offset + size - 1, histogram[i]);
		}

		while(j >= 2) {
			printf("*");
			j-=2;
		}
		if(j==1) {
			printf(".");
		}
		printf("\n");
	}

	if(test_for_outliers && n_values >= MIN_N_FOR_OUTLIER_DETECTION) {
		// since we have all this data, we can remove rare outliers
		int count_without_outliers = 0;
		int cutoff = TESTBENCH_STD_CUTOFF;

		// the following algorithm is a compromise of speed and performance
		// outlier detection is currently defined as:
		// keep only the values that have been measured more often than defined by cutoff,
		// typically set to 1. An easy question to answer for histograms with bin size 1.
		// Since some test results needed bins of wider size for reasonable optical representation
		// of the histogram, a separate test is needed. Potentially a large amount of memory
		// could be needed to build this array. Thus an alternative method is used that
		// runs in O(n^2) for n=number of tests, which is fine for typically small n
		// Stable memory requirement that can be allocated already during initialization
		// of the bench for all tests.
		// And after introduction of the denominator, it is also easier to use the alternative
		// version for histograms of bin size 1 to avoid back and forth calculation of
		// denominators with rounding errors.

		// alternative version as described
		// 1) copy data
		for(int i = 0; i < n_values; i++) {
			data_working_temp[i] = values[i];
		}

		// 2) walk thru the list and check the # of occurences of a value
		//    if > cutoff -> copy all of them
		//    otherwise ignore
		//    note: all checked positions will be set to 0 to avoid re-checking
		//    of these values again and again
		// Advantage:    no additional memory needed;
		// Disadvantage: run time O(n^2) as discussed above
		uint64_t v = 0;
		int counter = 0;
		for(int i = 0; i < n_values; i++) {
			v = data_working_temp[i];
			if(v == 0) {
				continue;
			}

			counter = 1;
			for(int j = i+1; j < n_values; j++) {
				if(data_working_temp[j] == v) {
					data_working_temp[j] = 0;
					counter++;
				}
			}

			// 3) add values to the correct list, if needed
			if(counter > cutoff) {
				for(int j = 0; j < counter; j++) {
					data_without_outliers[count_without_outliers++] = v;
				}					
			}
		}

		struct testbench_statistics no_outliers = calc_statistics(data_without_outliers, count_without_outliers);
		printf("\nAfter outlier removal: ");
		print_testbench_statistics_including_outliers(title, no_outliers, stat.count - count_without_outliers);
		print_histogram_and_remove_outliers(NULL, no_outliers, data_without_outliers, count_without_outliers, false);
		printf("\n");
		return no_outliers;
	}

	// default return
	return stat; 
}

struct testbench_statistics print_histogram(char *title, struct testbench_statistics stat) {
	return print_histogram_and_remove_outliers(title, stat, data, count, true); 
}

//--- development helpers ------------------------------------------------------

bool development_load_raw_values(uint64_t *values, int n_values) {
	if(n_values > cap) {
		return false;
	}

	for(int i = 0; i < n_values; i++) {
		data[i] = values[i];
	}
	count = n_values;
	return true;
}

//------------------------------------------------------------------------------
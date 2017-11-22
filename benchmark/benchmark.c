/**
 * Helper library for benchmarking small pieces of code
 * Uses rdtsc to get the clock counter. Note potential problems on multicore systems.
 *
 * See header file for details.
 *
 * v1.2 2015-11-25 / 2017-11-21 Pirmin Schmid, MIT License
 */

#include "benchmark.h"

#include <assert.h>
#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//--- private data ---------------------------------------------------------------------------------
//    some internal variables

static uint64_t baseline_ = 0;

// raw data stored from measurement
static uint64_t *data_ = NULL;

// temporary internal data for outlier removal
// allocated at the beginning to avoid lots of mallocs() later
static uint64_t *data_without_outliers_ = NULL;

// additional temporary internal data for outlier removal (histogram method)
// allocated at the beginning to avoid mallocs() later
static uint64_t *data_working_temp_ = NULL;

static size_t cap_ = 0;
static size_t count_ = 0;
static size_t denominator_ = 1;

static enum testbench_outlier_detection_mode outlier_detection_mode_ = TESTBENCH_OUTLIER_DETECTION_OFF;

// default unit
static struct testbench_time_unit cycles_ = {
    .name = "cycles",
    .cycles_per_unit = 1
};


// values of t-distribution (two-tailed) for 100*(1-alpha)% = 95% (alpha level 0.05)
// from https://www.medcalc.org/manual/t-distribution.php
// abbreviated use here
struct distr {
    size_t df;
    double t_value;
};

// arbitrarily set
#define T_TABLE_INFINITY 1024

static struct distr t_table_[] = {
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

static size_t t_table_n_ = sizeof(t_table_) / sizeof(t_table_[0]);

//--- private helpers ------------------------------------------------------------------------------

static double get_t_value(size_t n)
{
    assert(n > 1);

    size_t df = n - 1;
    for (size_t i = 0; i < t_table_n_; i++) {
        if (df >= t_table_[i].df) {
            return t_table_[i].t_value;
        }
    }

    // will not happen
    assert(false);
    return 0.0;
}

static int cmp_uint64_t(const void *a, const void *b)
{
    uint64_t a2 = *((uint64_t *)a);
    uint64_t b2 = *((uint64_t *)b);
    if (a2 < b2) {
        return -1;
    }
    if (a2 > b2) {
        return 1;
    }
    return 0;
}


/**
 * note, the expected fractional parts for r_frac will be 0.0, 0.25, 0.5, 0.75
 * thus, no need to test for machine precision if( r_frac <= (DBL_EPSILON * r_p) )
 * PERC_ATOL is currently chosen to allow for some "reserve" if get_percentile is chosen
 * to be used for smaller percentiles, too. The conditional is kept to avoid rounding errors
 * with calculations close to 0.0.
 */
#define PERC_ATOL 0.001


/**
 * calculates percentile value as described in
 * https://www.medcalc.org/manual/summary_statistics.php
 * see Lentner C (ed). Geigy Scientific Tables, 8th edition, Volume 2. Basel: Ciba-Geigy Limited, 1982
 *     Schoonjans F, De Bacquer D, Schmid P. Estimation of population percentiles. Epidemiology 2011;22:750-751.
 * percentile must be in range (0,1) here -- note: not (0,100).
 * modified to include the denominator, too.
 */
static double get_percentile(uint64_t *sorted_values, size_t n_values, double percentile, size_t denominator)
{
    double n = (double)n_values;
    assert(1.0/n <= percentile && percentile <= (n - 1.0)/n);
    double r_p = 0.5 + percentile * n;
    // note: only valid if 1/n <= percentile <= (n-1)/n
    // i.e. n must be at very least 4 for percentiles == 0.25 (quartile 1) and 0.75 (quartile 3)
    //                              2 for percentile == 0.5 (median)

    double r_floor = floor(r_p);
    double r_frac = r_p - r_floor; // in range [0,1); also == abs(r_floor - r_p)

    double result = 0.0;
    if (r_frac < PERC_ATOL) {
        // integer number, use (r_floor - 1) as array index and return value
        size_t r_ind = (size_t)r_floor - 1;
        assert(r_ind < n_values);
        result = (double)sorted_values[r_ind];
    }
    else {
        // use linear interpolation for fractional part
        // benefits and limitations -> see mentioned papers
        size_t r_ind = (size_t)r_floor - 1;
        size_t r_ind2 = r_ind + 1;
        assert(r_ind2 < n_values);
        result = (1.0 - r_frac) * (double)sorted_values[r_ind];  // the closer to 0.0 the more weight
        result += r_frac * (double)sorted_values[r_ind2];        // the closer to 1.0 the more weight
    }

    return result / ((double)denominator);
}

static struct testbench_statistics calc_statistics(uint64_t *values, size_t n_values);

static bool fprint_testbench_statistics_including_outliers(FILE *stream, const char *title,
                                                           const struct testbench_statistics *stat,
                                                           const struct testbench_time_unit *unit,
                                                           size_t removed_outliers);

static struct testbench_statistics fprint_histogram_and_remove_outliers(FILE *stream, const char *title,
                                                                        const struct testbench_statistics *stat,
                                                                        const struct testbench_time_unit *unit,
                                                                        uint64_t *values, size_t n_values,
                                                                        bool test_for_outliers,
                                                                        bool *ret_ok);

//--- implementation of the public API -------------------------------------------------------------
//    see header file for information about the functions

bool create_testbench(size_t capacity)
{
    uint64_t start = 0;
    uint64_t stop = 0;

    if (capacity < 1) {
        goto error_wrong_capacity;
    }

    if (data_) {
        delete_testbench();
    }

    data_ = malloc(capacity * sizeof(*data_));
    if (!data_) {
        goto error_malloc_data;
    }

    data_without_outliers_ = malloc(capacity * sizeof(*data_without_outliers_));
    if (!data_without_outliers_) {
        goto error_malloc_data_without_outliers;
    }

    data_working_temp_ = malloc(capacity * sizeof(*data_working_temp_));
    if (!data_working_temp_) {
        goto error_malloc_data_working_temp;
    }

    cap_ = capacity;
    denominator_ = 1;

    // establish baseline
    // have 2 full dry runs of size cap_ (warming up) and then one measurement
    for (size_t i = 0; i < 3; i++) {
        for (size_t j = 0; j < cap_; j++) {
            RDTSC_START(start);
            // nothing
            RDTSC_STOP(stop);
            data_[j] = stop - start;
        }
    }

    count_ = cap_;
    struct testbench_statistics baseline_stat = calc_statistics(data_, count_);
    print_testbench_statistics("baseline", &baseline_stat, NULL);
    print_histogram("baseline", &baseline_stat, NULL);
    baseline_ = baseline_stat.absMin;
    printf("Benchmark library: %" PRIu64 " cycles will be used as baseline.\n", baseline_);
    count_ = 0;
    denominator_ = TESTBENCH_STD_DENOMINATOR;
    return true;

    // error handling
//error_next:
    free(data_working_temp_);
    data_working_temp_ = NULL;
error_malloc_data_working_temp:
    free(data_without_outliers_);
    data_without_outliers_ = NULL;
error_malloc_data_without_outliers:
    free(data_);
    data_ = NULL;
error_malloc_data:
error_wrong_capacity:
    return false;
}

void set_denominator(size_t denominator)
{
    if (denominator < 1) {
        return;
    }

    denominator_ = denominator;
}

void set_outlier_detection_mode(enum testbench_outlier_detection_mode mode)
{
    outlier_detection_mode_ = mode;
}

void reset_testbench(void)
{
    count_ = 0;
}

void delete_testbench(void)
{
    if (data_working_temp_) {
        free(data_working_temp_);
        data_working_temp_ = NULL;
    }

    if (data_without_outliers_) {
        free(data_without_outliers_);
        data_without_outliers_ = NULL;
    }

    if (data_) {
        free(data_);
        data_ = NULL;
        cap_ = 0;
        count_ = 0;
        baseline_ = 0;
        denominator_ = TESTBENCH_STD_DENOMINATOR;
    }
}

void add_measurement(uint64_t start, uint64_t stop)
{
    // no array index check here
    uint64_t delta = stop - start - baseline_;
    data_[count_++] = ((int64_t)delta) < 0 ? 0 : delta;
}


//--- testbench_get_statistics() with associated private function ----------------------------------

/**
 * calculates the statistics for an array values[] of size n_values
 * this internal function is used by testbench_get_statistics() and print_histogram()
 */
static struct testbench_statistics calc_statistics(uint64_t *values, size_t n_values)
{
    // note: min/max deliberately not stored while adding measurements to avoid any
    // unnecessary cache interruption of the program to be measured
    struct testbench_statistics result = {0, 0, 0, 0, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    result.denominator = denominator_;
    result.baseline = baseline_;
    if (n_values == 0) {
        return result;
    }

    result.count = n_values;

    // mean, min, max
    uint64_t sum = 0;
    uint64_t min = UINT64_MAX;
    uint64_t max = 0;
    for (size_t i = 0; i < n_values; i++) {
        const uint64_t value = values[i];
        sum += value;

        if (value < min) {
            min = value;
        }

        if (value > max) {
            max = value;
        }
    }

    const double mean = (double)sum / ((double)denominator_ * (double)n_values);
    result.mean = mean;
    result.absMin = min;
    result.min = (double)min / (double)denominator_;
    result.absMax = max;
    result.max = (double)max / (double)denominator_;

    // robust
    if (n_values > 1) {
        qsort(values, n_values, sizeof(*values), cmp_uint64_t);
        result.median = get_percentile(values, n_values, 0.5, denominator_);

        if (n_values > 3) {
            // just the bare minimum to work
            // of course these quartiles will have large 95% confidence intervals by themselves
            // thus: choose larger n_values for meaningful results, of course
            result.q1 = get_percentile(values, n_values, 0.25, denominator_);
            result.q3 = get_percentile(values, n_values, 0.75, denominator_);
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
    if (n_values > 1) {
        // this is just the barely minimum to avoid div/0
        // of course, one should use a meaningful count / n_values
        double s2 = 0.0;
        for (size_t i = 0; i < n_values; i++) {
            const double delta = (double)values[i] / (double)denominator_ - mean;
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

struct testbench_statistics testbench_get_statistics(void)
{
    // note: min/max deliberately not stored while adding measurements to avoid any
    // unnecessary cache interruption of the program to be measured
    return calc_statistics(data_, count_);
}


//--- fprint_testbench_values() --------------------------------------------------------------------

bool fprint_testbench_values(FILE *stream, const char *title, const struct testbench_time_unit *unit)
{
    assert(stream);
    assert(title);
    // unit is optional

    int ret = fprintf(stream, "# %s (n=%zu)\n", title, count_);
    if (ret < 0) {
        return false;
    }

    if (unit) {
        ret = fprintf(stream, "# unit: %s with %" PRIu64 " cycles / unit\n", unit->name, unit->cycles_per_unit);
        if (ret < 0) {
            return false;
        }

        const double cpu = unit->cycles_per_unit;
        for (size_t i = 0; i < count_; i++) {
            const double value = (double)data_[i] / cpu;
            ret = fprintf(stream, "%f\n", value);
            if (ret < 0) {
                return false;
            }
        }
    }
    else {
        ret = fprintf(stream, "# unit: cycles\n");
        if (ret < 0) {
            return false;
        }

        for (size_t i = 0; i < count_; i++) {
            ret = fprintf(stream, "%" PRIu64 "\n", data_[i]);
            if (ret < 0) {
                return false;
            }
        }
    }

    return true;
}


//--- print_testbench_statistics() with associated private function --------------------------------

static struct testbench_statistics convert_stats(const struct testbench_statistics *stat, const struct testbench_time_unit *unit)
{
    uint64_t cpu_i = unit->cycles_per_unit;
    double cpu_d = (double)unit->cycles_per_unit;

    struct testbench_statistics s;
    s.count = stat->count;
    s.denominator = stat->denominator;
    s.baseline = stat->baseline / cpu_i;
    s.absMin = stat->absMin / cpu_i;
    s.absMax = stat->absMax / cpu_i;
    s.min = stat->min / cpu_d;
    s.q1 = stat->q1 / cpu_d;
    s.median = stat->median / cpu_d;
    s.q3 = stat->q3 / cpu_d;
    s.max = stat->max / cpu_d;
    s.mean = stat->mean / cpu_d;
    s.sd = stat->sd / cpu_d;
    s.ci95_a = stat->ci95_a / cpu_d;
    s.ci95_b = stat->ci95_b / cpu_d;
    return s;
}

static bool fprint_testbench_statistics_including_outliers(FILE *stream, const char *title,
                                                           const struct testbench_statistics *stat,
                                                           const struct testbench_time_unit *unit,
                                                           size_t removed_outliers)
{
    int ret = 0;
    if (title) {
        ret = fprintf(stream, "\n%s:\n", title);
        if (ret < 0) {
            return false;
        }
    }

    struct testbench_statistics s;
    if (unit) {
        s = convert_stats(stat, unit);
    }
    else {
        unit = &cycles_;
        s = *stat;
    }

    if (stat->count > 3) {
        // just the bare minimum to somewhat make sense
        // better use larger counts, of course
        ret = fprintf(stream, "- robust:       median %.1f %s, IQR [%.1f, %.1f], min %.1f, max %.1f, n=%zu [%zu outlier(s) removed], denominator=%zu, baseline=%" PRIu64 "\n",
                      s.median, unit->name, s.q1, s.q3, s.min, s.max, s.count, removed_outliers, s.denominator, s.baseline);
        if (ret < 0) {
            return false;
        }

        ret = fprintf(stream, "- normal dist.: %.1f ± %.1f %s (mean ± sd), 95%% CI for the mean [%.1f, %.1f], min %.1f, max %.1f, n=%zu [%zu outlier(s) removed], denominator=%zu, baseline=%" PRIu64 "\n",
                      s.mean, s.sd, unit->name, s.ci95_a, s.ci95_b, s.min, s.max, s.count, removed_outliers, s.denominator, s.baseline);
        if (ret < 0) {
            return false;
        }
    }
    else {
        // there is not much that should be reported with such low counts
        ret = fprintf(stream, "mean %.1f %s, median %.1f, min %.1f, max %.1f, n=%zu [%zu outlier(s) removed], denominator=%zu, baseline=%" PRIu64 "; use n >= 4 for more detailed descriptive statistics.\n",
                      s.mean, unit->name, s.median, s.min, s.max, s.count, removed_outliers, s.denominator, s.baseline);
        if (ret < 0) {
            return false;
        }
    }

    return true;
}

bool fprint_testbench_statistics(FILE *stream, const char *title,
                                 const struct testbench_statistics *stat,
                                 const struct testbench_time_unit *unit)
{
    assert(stream);
    assert(stat);
    // title and unit are optional

    int ret = 0;
    if (title) {
        ret = fprintf(stream, "\n%s:\n", title);
        if (ret < 0) {
            return false;
        }
    }

    struct testbench_statistics s;
    if (unit) {
        s = convert_stats(stat, unit);
    }
    else {
        unit = &cycles_;
        s = *stat;
    }

    if (stat->count > 3) {
        // just the bare minimum to somewhat make sense
        // better use larger counts, of course
        ret = fprintf(stream, "- robust:       median %.1f %s, IQR [%.1f, %.1f], min %.1f, max %.1f, n=%zu, denominator=%zu, baseline=%" PRIu64 "\n",
                      s.median, unit->name, s.q1, s.q3, s.min, s.max, s.count, s.denominator, s.baseline);
        if (ret < 0) {
            return false;
        }

        ret = fprintf(stream, "- normal dist.: %.1f ± %.1f %s (mean ± sd), 95%% CI for the mean [%.1f, %.1f], min %.1f, max %.1f, n=%zu, denominator=%zu, baseline=%" PRIu64 "\n",
                      s.mean, s.sd, unit->name, s.ci95_a, s.ci95_b, s.min, s.max, s.count, s.denominator, s.baseline);
        if (ret < 0) {
            return false;
        }
    }
    else {
        // there is not much that should be reported with such low counts
        ret = fprintf(stream, "mean %.1f %s, median %.1f, min %.1f, max %.1f, n=%zu, denominator=%zu, baseline=%" PRIu64 "; use n >= 4 for more detailed descriptive statistics.\n",
                      s.mean, unit->name, s.median, s.min, s.max, s.count, s.denominator, s.baseline);
        if (ret < 0) {
            return false;
        }
    }

    return true;
}


//--- fprint_histogram() with associated private function ------------------------------------------

static struct testbench_statistics fprint_histogram_and_remove_outliers(FILE *stream, const char *title,
                                                                        const struct testbench_statistics *stat,
                                                                        const struct testbench_time_unit *unit,
                                                                        uint64_t *values, size_t n_values,
                                                                        bool test_for_outliers,
                                                                        bool *ret_ok)
{
    *ret_ok = true;

    // some checks
    if (stat->max < stat->min) {
        return *stat;
    }

    if (stat->count < 1) {
        return *stat;
    }

    uint64_t d = denominator_;
    if (unit) {
        d *= unit->cycles_per_unit;
    }

    uint64_t min = stat->absMin / d;
    uint64_t max = stat->absMax / d;

    size_t delta = (size_t)(max - min);
    size_t bins = delta + 1;
    size_t size = 1;
    while (bins > TESTBENCH_MAX_BINS) {
        // arbitrary limit
        size <<= 1;
        bins = delta/size + 1;
    }

    int ret = 0;
    if (title) {
        ret = fprintf(stream, "%s (%zu bins of size %zu)\n", title, bins, size);
        if (ret < 0) {
            goto fprintf_error_return;
        }
    }
    else {
        ret = fprintf(stream, "(%zu bins of size %zu)\n", bins, size);
        if (ret < 0) {
            goto fprintf_error_return;
        }
    }

    size_t histogram[TESTBENCH_MAX_BINS];
    for (size_t i = 0; i < bins; i++) {
        histogram[i] = 0;
    }

    for (size_t i = 0; i < n_values; i++) {
        histogram[ ((size_t)(values[i] / d - min)) / size ]++;
    }

    for (size_t i = 0; i < bins; i++) {
        size_t j = (histogram[i] * 100) / n_values;
        if (size == 1) {
            ret = fprintf(stream, "%4" PRIu64 " [%3zu]: ", i + min, histogram[i]);
            if (ret < 0) {
                goto fprintf_error_return;
            }
        }
        else {
            uint64_t offset = (i * size) + min;
            ret = fprintf(stream, "%4" PRIu64 " - %4" PRIu64 " [%3zu]: ", offset, offset + size - 1, histogram[i]);
            if (ret < 0) {
                goto fprintf_error_return;
            }
        }

        while (j >= 2) {
            ret = fprintf(stream, "*");
            if (ret < 0) {
                goto fprintf_error_return;
            }
            j-=2;
        }
        if (j == 1) {
            ret = fprintf(stream, ".");
            if (ret < 0) {
                goto fprintf_error_return;
            }
        }
        ret = fprintf(stream, "\n");
        if (ret < 0) {
            goto fprintf_error_return;
        }
    }

    if (outlier_detection_mode_ == TESTBENCH_OUTLIER_DETECTION_OFF) {
        return *stat;
    }

    if (!test_for_outliers) {
        return *stat;
    }

    // start outlier detection
    size_t count_without_outliers = 0;

    if (outlier_detection_mode_ == TESTBENCH_OUTLIER_DETECTION_SD) {
        if (n_values < TESTBENCH_OUTLIER_DETECTION_SD_MIN_N) {
            return *stat;
        }

        double diff = TESTBENCH_OUTLIER_DETECTION_SD_MIN_SD * stat->sd;
        double low = stat->mean - diff;
        double high = stat->mean + diff;
        for (size_t i = 0; i < n_values; i++) {
            uint64_t vi = values[i];
            double vd = (double)vi;
            if (vd < low) {
                continue;
            }
            if (vd > high) {
                continue;
            }
            data_without_outliers_[count_without_outliers++] = vi;
        }
    }
    else if (outlier_detection_mode_ == TESTBENCH_OUTLIER_DETECTION_HISTOGRAM) {
        if (n_values < TESTBENCH_OUTLIER_DETECTION_HISTOGRAM_MIN_N) {
            return *stat;
        }

        size_t cutoff = TESTBENCH_STD_CUTOFF;

        // the following algorithm is a compromise of speed and performance
        // outlier detection using the histogram is currently defined as:
        // keep only the values that have been measured more often than defined by cutoff,
        // typically set to 1. An easy question to answer for histograms with bin size 1.
        // Since some test results needed bins of wider size for reasonable optical representation
        // of the histogram, a separate test is needed. Potentially a large amount of memory
        // could be needed to build this array with bins of size 1 then. Thus an alternative method is used that
        // runs in O(n^2) for n=number of value, which is fine for typically small n
        // Stable memory requirement that can be allocated already during initialization
        // of the bench for all tests.
        // And after introduction of the denominator, it is also easier to use the alternative
        // version for histograms of bin size 1 to avoid back and forth calculation of
        // denominators with rounding errors.

        // alternative histogram version as described
        // 1) copy data
        memcpy(data_working_temp_, values, n_values * sizeof(*values));

        // 2) walk thru the list and check the # of occurences of a value
        //    if > cutoff -> copy all of them
        //    otherwise ignore
        //    note: all checked positions will be set to 0 to avoid re-checking
        //    of these values again and again
        // Advantage:    no additional memory needed;
        // Disadvantage: run time O(n^2) as discussed above
        for (size_t i = 0; i < n_values; i++) {
            uint64_t v = data_working_temp_[i];
            if (v == 0) {
                continue;
            }

            size_t counter = 1;
            for (size_t j = i+1; j < n_values; j++) {
                if (data_working_temp_[j] == v) {
                    data_working_temp_[j] = 0;
                    counter++;
                }
            }

            // 3) add values to the correct list, if needed
            if (counter > cutoff) {
                for (size_t j = 0; j < counter; j++) {
                    data_without_outliers_[count_without_outliers++] = v;
                }
            }
        }
    }
    else {
        assert(false);
    }

    struct testbench_statistics no_outliers = calc_statistics(data_without_outliers_, count_without_outliers);
    ret = fprintf(stream, "\nAfter outlier removal (method ");
    if (ret < 0) {
        goto fprintf_error_return;
    }
    switch (outlier_detection_mode_) {
        case TESTBENCH_OUTLIER_DETECTION_SD: {
            ret = fprintf(stream, "standard deviation, cutoff at %d SD):", TESTBENCH_OUTLIER_DETECTION_SD_MIN_SD);
            if (ret < 0) {
                goto fprintf_error_return;
            }
            break;
        }

        case TESTBENCH_OUTLIER_DETECTION_HISTOGRAM: {
            ret = fprintf(stream, "histogram, cutoff %d):", TESTBENCH_STD_CUTOFF);
            if (ret < 0) {
                goto fprintf_error_return;
            }
            break;
        }

        default:
            assert(false);
    }
    if (!fprint_testbench_statistics_including_outliers(stream, title, &no_outliers, unit, stat->count - count_without_outliers)) {
        goto fprintf_error_return;
    }
    fprint_histogram_and_remove_outliers(stream, NULL, &no_outliers, unit, data_without_outliers_, count_without_outliers, false, ret_ok);
    if (!*ret_ok) {
        goto fprintf_error_return;
    }
    ret = fprintf(stream, "\n");
    if (ret < 0) {
        goto fprintf_error_return;
    }

    // all OK
    return no_outliers;

fprintf_error_return:
    *ret_ok = false;
    return *stat;
}

struct testbench_statistics fprint_histogram(FILE *stream, const char *title,
                                             const struct testbench_statistics *stat,
                                             const struct testbench_time_unit *unit,
                                             bool *ret_ok)
{
    assert(stream);
    assert(stat);
    assert(ret_ok);
    // title and unit are optional

    return fprint_histogram_and_remove_outliers(stream, title, stat, unit, data_, count_, true, ret_ok);
}

//--- development helpers ------------------------------------------------------

bool development_load_raw_values(const uint64_t *values, size_t n_values)
{
    assert(values);

    if (n_values > cap_) {
        return false;
    }

    memcpy(data_, values, n_values * sizeof(*values));
    count_ = n_values;
    return true;
}

bool development_get_raw_values(uint64_t *values_buffer, size_t n_values_capacity, size_t *ret_n_values)
{
    assert(values_buffer);
    assert(ret_n_values);

    if (n_values_capacity < count_) {
        return false;
    }

    *ret_n_values = count_;
    memcpy(values_buffer, data_, count_ * sizeof(*values_buffer));
    return true;
}


void development_map_values(testbench_lambda_function_t lambda)
{
    assert(lambda);

    for (size_t i = 0; i < count_; i++) {
        data_[i] = lambda(data_[i]);
    }
}


//------------------------------------------------------------------------------

/* This program compares the results of the builtin statistics functions with
   reference results of a commercial statistics software (MedCalc, www.medcalc.org)

   data generated and analyzed with MedCalc v15.11.4 (see folder reference_data)

   v1.0 2015-12-31 / 2016-01-02 Pirmin Schmid
*/

#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "benchmark.h"

// note for all test data sets:
// since benchmark.c has only a very limited number of the Student t test values stored
// and not the entire table, make sure that n is chosen for all tests here that the
// degree of freedom (= n-1) is actually available as a value in benchmark.c's
// table (see 95% CI estimation). Otherwise, no real comparison is possible.
// Due to the selection of specific t test values, the estimation of the 95% CI
// works well in benchmark.c even for n that do not have an exact value
// stored (implemented err towards wider 95% CI, which makes sense, of course).   

//--- data set 1 - random values, normal distribution --------------------------
//    generator settings: mean = 1'000'000, sd = 100'000, n = 101
//    use a denominator = 1

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

static int data1_n = sizeof(data1) / sizeof(*data1);

static int denominator1 = 1;

// see PDF file with reference values
static struct testbench_statistics reference1 = {
	.count       =     101,
	.denominator =       1,
	.baseline    =       0,
	.absMin      =  740516,
	.absMax      = 1226173,

	/* robust */
	.min         =  740516.0000,
	.q1          =  937842.7500,
	.median      = 1011374.0000,
	.q3          = 1070549.0000,
	.max         = 1226173.0000,

	/* parametric (assume normal distribution) */
	.mean        = 1002289.7228,
	.sd          =  102380.3052,
	.ci95_a      =  982078.5662,
	.ci95_b      = 1022500.8793
};


//--- data set 2 - random values, normal distribution --------------------------
//    generator settings: mean = 1'000'000, sd = 300'000, n = 31
//    use a denominator = 32

static uint64_t data2[] = {
816470,
1238486,
966711,
977648,
606973,
1183548,
742549,
918595,
1283970,
1100167,
960535,
982734,
1119218,
1028509,
1014213,
1255995,
783184,
715697,
1176160,
847037,
603338,
1057617,
327444,
931031,
914510,
1036230,
1120600,
894320,
1219739,
962894,
702271
};

static int data2_n = sizeof(data2) / sizeof(*data2);

static int denominator2 = 32;

// see PDF file with reference values
static struct testbench_statistics reference2 = {
	.count       =      31,
	.denominator =      32,
	.baseline    =       0,
	.absMin      =  327444,
	.absMax      = 1283970,

	/* robust */
	.min         =   10232.6250,
	.q1          =   25753.4922,
	.median      =   30209.7188,
	.q3          =   34826.7266,
	.max         =   40124.0625,

	/* parametric (assume normal distribution) */
	.mean        =   29726.2026,
	.sd          =    6811.9025,
	.ci95_a      =   27227.5766,
	.ci95_b      =   32224.8286
};

//--- data set 3 - a corner case -----------------------------------------------
//    use a denominator = 1

static uint64_t data3[] = {
1,
2,
3,
4
};

static int data3_n = sizeof(data3) / sizeof(*data3);

static int denominator3 = 1;

// see PDF file with reference values
static struct testbench_statistics reference3 = {
	.count       =       4,
	.denominator =       1,
	.baseline    =       0,
	.absMin      =       1,
	.absMax      =       4,

	/* robust */
	.min         =       1.0000,
	.q1          =       1.5000,
	.median      =       2.5000,
	.q3          =       3.5000,
	.max         =       4.0000,

	/* parametric (assume normal distribution) */
	.mean        =       2.5000,
	.sd          =       1.2910,
	.ci95_a      =       0.4457,
	.ci95_b      =       4.5543
};

//--- compare data -------------------------------------------------------------

// narrow relative tolerance 0.00001 (check for 0.001 % difference)
// arithmetics should work nicely
#define RTOL_narrow 0.00001

// wide relative tolerance 0.001 (check for 0.1 % difference)
// see potential difference in Student t values (rounding in table)
#define RTOL_wide 0.001

static void print_double(char *title, double value, double reference, double rtol) {
	char *ok_str = NULL;
	if(fabs(value - reference) < rtol * reference) {
		ok_str = " OK  ";
	}
	else {
		ok_str = "WRONG";
	}

	printf("val = %.4f ref = %.4f %s (rtol = %.1e) :: %s\n", value, reference, ok_str, rtol, title);
}

static void print_uint64_t(char *title, uint64_t value, uint64_t reference) {
	char *ok_str = NULL;
	if(value == reference) {
		ok_str = " OK  ";
	}
	else {
		ok_str = "WRONG";
	}

	printf("val = %" PRIu64 " ref = %" PRIu64 " %s :: %s\n", value, reference, ok_str, title);
}

static void print_int(char *title, int value, int reference) {
	char *ok_str = NULL;
	if(value == reference) {
		ok_str = " OK  ";
	}
	else {
		ok_str = "WRONG";
	}

	printf("val = %d ref = %d %s :: %s\n", value, reference, ok_str, title);
}

static void run_comparison(char *title, uint64_t *values, int values_n, int denominator, struct testbench_statistics *ref) {
	printf("\nRunning test: %s\n", title);
	reset_testbench();
	set_denominator(denominator);

	if(!development_load_raw_values(values, values_n)) {
		printf("Error while loading raw values for test %s.\n", title);
		delete_testbench();
		exit(1);
	}

	// standard output
	struct testbench_statistics stat = testbench_get_statistics();
	print_testbench_statistics("Results", stat);

	// actual comparison
	printf("\nComparison:\n");
	print_int("count", stat.count, ref->count);
	print_int("denominator", stat.denominator, ref->denominator);
	printf("baseline is not used in these tests.\n");
	print_uint64_t("absMin", stat.absMin, ref->absMin);
	print_uint64_t("absMax", stat.absMax, ref->absMax);
	printf("robust\n");
	print_double("min", stat.min, ref->min, RTOL_narrow);
	print_double("q1", stat.q1, ref->q1, RTOL_narrow);
	print_double("median", stat.median, ref->median, RTOL_narrow);
	print_double("q3", stat.q3, ref->q3, RTOL_narrow);
	print_double("max", stat.max, ref->max, RTOL_narrow);
	printf("parametric\n");
	print_double("mean", stat.mean, ref->mean, RTOL_narrow);
	print_double("sd", stat.sd, ref->sd, RTOL_narrow);
	print_double("ci95_a (wider RTOL)", stat.ci95_a, ref->ci95_a, RTOL_wide);
	print_double("ci95_b (wider RTOL)", stat.ci95_b, ref->ci95_b, RTOL_wide);
}

//--- main ---------------------------------------------------------------------

int main() {
	// init
	int max_n = (data1_n > data2_n) ? data1_n : data2_n;
	if( !create_testbench(max_n) ) {
		exit(1);
	}

	// tests
	run_comparison("Test 1. denominator=1.", data1, data1_n, denominator1, &reference1);
	run_comparison("Test 2. denominator=32, wider SD, fewer values.", data2, data2_n, denominator2, &reference2);
	run_comparison("Test 3. corner case n=4.", data3, data3_n, denominator3, &reference3);

	// cleanup
	delete_testbench();
	return 0;
}
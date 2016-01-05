/*  Given tamplate in class for matrix multiplication
	extended by:
	- benchmark routine using my own external library
	- modified output channels to be better pipeable
	- various modified multiplication methods (hopefully faster)
	- compilation using Clang 7 on OSX on a Haswell processor
	  -> tested with varous optimization flags on
	  -> check for automatic vectorization

	future:  
	- test script that collects the data of each run

	- currently, data is piped into a result file that actually holds
	  the header and one row of data
	  -> can be used to copy into Excel and create figure there.
	  (not done at the moment since result collection not finished)

	- results are summarized in README.txt
	- raw data are shown in various results.txt files  

	2015-12-04  / 2015-12-05 Pirmin Schmid  
*/

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>  

#include "benchmark.h"	

//--- given routines -----------------------------------------------------------

static int *randmatrix(int size) {
	int *matrix = malloc(size * size * sizeof(*matrix));
	if(!matrix) {
		fprintf(stderr, "%s: memory allocation error.\n", __func__);
		exit(1);
	}

	for (int row = 0; row < size; row++) {
		for (int col = 0; col < size; col++) {
			matrix[row * size + col] = random() / (RAND_MAX / 100);
		}
	}

	return matrix;
}

static void printmatrix(int size, int *matrix, char name) {
	printf("Matrix %c:\n", name);
	for (int row = 0; row < size; row++) {
		for (int col = 0; col < size; col++) {
			printf("%10d", matrix[row * size + col]);
		}
		printf("\n");
	}
}

static int *mmul(int size, int *A, int *B) {
	int *result = malloc(size * size * sizeof(*result));
	if(!result) {
		fprintf(stderr, "%s: memory allocation error.\n", __func__);
		exit(1);
	}

	for (int i = 0; i < size; i++) {
		for (int j = 0; j < size; j++) {
			int sum = 0;
			for (int k = 0; k < size; k++) {
				sum += A[i * size + k] * B[k * size + j];
			}
			result[i * size + j] = sum;
		}
	}

/*  // for debug purpose
	printmatrix(size, A, 'A');
	printmatrix(size, B, 'B');
	printmatrix(size, result, 'C');
//*/

	return result;
}

//--- optimized algorithms -----------------------------------------------------

// just improve index calculation (avoid multiplications)
// no other improvements
// about 1.5x as fast
static int *mmul_betterIndexCalculation(int size, int *A, int *B) {
	int *result = malloc(size * size * sizeof(*result));
	if(!result) {
		fprintf(stderr, "%s: memory allocation error.\n", __func__);
		exit(1);
	}

	int a_row = 0;
	for (int i = 0; i < size; i++) {
		for (int j = 0; j < size; j++) {
			int sum = 0;
			int b_column = 0;
			for (int k = 0; k < size; k++) {
				sum += A[a_row + k] * B[b_column + j];
				b_column += size;
			}
			result[a_row + j] = sum;
		}
		a_row += size;
	}

	return result;
}

// C = A * B' instead of B allows access to elements of B along the cache lines
// for testing purpose, no additional optimizations
// note: contents of A and B will not be modified
// runs about 2x as fast as native
static int *mmul_transposedB(int size, int *A, int *B) {
	int *result = malloc(size * size * sizeof(*result));
	int *B_t = malloc(size * size * sizeof(*B_t));
	if(!result || !B_t) {
		fprintf(stderr, "%s: memory allocation error.\n", __func__);
		exit(1);
	}

	// transposed B (no swapping to avoid modifications in B)
	int s_row = 0; 
	for (int i = 0; i < size; i++) {
		for (int j = 0; j < size; j++) {
			B_t[size * j + i] = B[s_row + j];
		}
		s_row += size;
	}	

	// multiply
	for (int i = 0; i < size; i++) {
		for (int j = 0; j < size; j++) {
			int sum = 0;
			for (int k = 0; k < size; k++) {
				sum += A[i * size + k] * B_t[j * size + k];
			}
			result[i * size + j] = sum;
		}
	}

	return result;
}


// C = A * B' as above AND better index calculation
// runs...
static int *mmul_transposedB_and_betterIndexCalculation(int size, int *A, int *B) {
	int *result = malloc(size * size * sizeof(*result));
	int *B_t = malloc(size * size * sizeof(*B_t));
	if(!result || !B_t) {
		fprintf(stderr, "%s: memory allocation error.\n", __func__);
		exit(1);
	}

	// transposed B (no swapping to avoid modifications in B)
	int s_row = 0; 
	for (int i = 0; i < size; i++) {
		for (int j = 0; j < size; j++) {
			B_t[size * j + i] = B[s_row + j];
		}
		s_row += size;
	}	

	// multiply
	int a_row = 0;
	int bt_row = 0;
	for (int i = 0; i < size; i++) {
		for (int j = 0; j < size; j++) {
			int sum = 0;
			for (int k = 0; k < size; k++) {
				sum += A[a_row + k] * B_t[bt_row + k];
			}
			result[a_row + j] = sum;
			bt_row += size;
		}
		a_row += size;
		bt_row = 0;
	}

	return result;
}

// the next implementations will keep transposedB and better index calculation as a basis
// but will use additional techniques

static int block_sizes[] = {
	1024,
	512,
	256,
	64,
	16
};

static int block_sizes_n = sizeof(block_sizes) / sizeof(block_sizes[0]);

static int block_sizes_counter = 0;

static int *mmul_blocks(int size, int *A, int *B) {
	// memory for B^T and result; note: result must be zeroed!
	int *B_t = malloc(size * size * sizeof(*B_t));
	int *result = calloc(1, size * size * sizeof(*result));
	if(!result || !B_t) {
		fprintf(stderr, "%s: memory allocation error.\n", __func__);
		exit(1);
	}

	// get current block size
	assert(block_sizes_counter < block_sizes_n);
	int block = block_sizes[block_sizes_counter++];

	// transposed B (no swapping to avoid modifications in B)
	// using blocks here (see very large matrices)
	int s_row = 0;
	int s_row_base = 0;
	int s_block_step = block * size;
	for (int i = 0; i < size; i+=block) {
		int end_i = i+block;
		if(end_i > size) {
			end_i = size;
		}
		for (int j = 0; j < size; j+=block) {
			int end_j = j+block;
			if(end_j > size) {
				end_j = size;
			}
			s_row = s_row_base;
			for(int i1 = i; i1 < end_i; i1++) {
				for(int j1 = j; j1 < end_j; j1++) {
					B_t[size * j1 + i1] = B[s_row + j1];					
				}
				s_row += size;
			}
		}
		s_row_base += s_block_step;
	}

	// multiply
	// also blocks here, since large matrices may not fit into the cache.
	int a_row = 0;
	int a_row_base = 0;
	int bt_row = 0;
	int bt_row_base = 0;
	for (int i = 0; i < size; i+=block) {
		int end_i = i+block;
		if(end_i > size) {
			end_i = size;
		}

		for (int j = 0; j < size; j+=block) {
			int end_j = j+block;
			if(end_j > size) {
				end_j = size;
			}

			for (int k = 0; k < size; k+=block) {
				int end_k = k+block;
				if(end_k > size) {
					end_k = size;
				}

				// loops inside of the blocks
				a_row = a_row_base;
				for(int i1 = i; i1 < end_i; i1++) {
					bt_row = bt_row_base;
					for(int j1 = j; j1 < end_j; j1++) {
						int sum = 0;
						for(int k1 = k; k1 < end_k; k1++) {
							sum += A[a_row + k1] * B_t[bt_row + k1];
						} // k1
						result[a_row + j1] += sum; // add
						bt_row += size;
					} // j1
					a_row += size;
				} // i1

			} // k
			bt_row_base += s_block_step;
		} // j
		a_row_base += s_block_step;
		bt_row_base = 0;
	} // i

/*  // for debug purpose
	printmatrix(size, A, 'A');
	printmatrix(size, B, 'B');
	printmatrix(size, result, 'C');
//*/

	return result;
}

static int block_sizes_acc4[] = {
	1024,
	512,
	256,
	64,
	16
};

static int block_sizes_acc4_n = sizeof(block_sizes_acc4) / sizeof(block_sizes_acc4[0]);

static int block_sizes_acc4_counter = 0;

// currently 4 accumulators in parallel in the center of the loops
// note: block sizes must be dividable by 4!
// this destroys automatic vectorization by the compiler
// is about 3x slower than fast blocks version above
// could be pottentially helpful only on processors that do not allow vetorization at all
static int *mmul_blocks_multiple_accumulators_naive_1st_try(int size, int *A, int *B) {
	// memory for B^T and result; note: result must be zeroed!
	int *B_t = malloc(size * size * sizeof(*B_t));
	int *result = calloc(1, size * size * sizeof(*result));
	if(!result || !B_t) {
		fprintf(stderr, "%s: memory allocation error.\n", __func__);
		exit(1);
	}

	// get current block size
	assert(block_sizes_acc4_counter < block_sizes_acc4_n);
	int block = block_sizes_acc4[block_sizes_acc4_counter++];

	// transposed B (no swapping to avoid modifications in B)
	// using blocks here (see very large matrices)
	int s_row = 0;
	int s_row_base = 0;
	int s_block_step = block * size;
	for (int i = 0; i < size; i+=block) {
		int end_i = i+block;
		if(end_i > size) {
			end_i = size;
		}
		for (int j = 0; j < size; j+=block) {
			int end_j = j+block;
			if(end_j > size) {
				end_j = size;
			}
			s_row = s_row_base;
			for(int i1 = i; i1 < end_i; i1++) {
				for(int j1 = j; j1 < end_j; j1++) {
					B_t[size * j1 + i1] = B[s_row + j1];					
				}
				s_row += size;
			}
		}
		s_row_base += s_block_step;
	}

	// multiply
	// also blocks here, since large matrices may not fit into the cache.
	int a_row = 0;
	int a_row_base = 0;
	int bt_row = 0;
	int bt_row_base = 0;
	for (int i = 0; i < size; i+=block) {
		int end_i = i+block;
		if(end_i > size) {
			end_i = size;
		}

		for (int j = 0; j < size; j+=block) {
			int end_j = j+block;
			if(end_j > size) {
				end_j = size;
			}

			for (int k = 0; k < size; k+=block) {
				int end_k = k+block;
				if(end_k > size) {
					end_k = size;
				}

				// loops inside of the blocks
				a_row = a_row_base;
				for(int i1 = i; i1 < end_i; i1++) {
					bt_row = bt_row_base;
					for(int j1 = j; j1 < end_j; j1++) {
						int sum_a = 0;
						int sum_b = 0;
						int sum_c = 0;
						int sum_d = 0;
						int k1 = k;
						// avoid overshooting without using an expensive mod operation
						int end_k1 = end_k - 4;
						for( ; k1 < end_k1; k1 += 4) {
							int a_base = a_row + k1;
							int bt_base = bt_row + k1;
							sum_a += A[a_base] * B_t[bt_base];
							sum_b += A[a_base + 1] * B_t[bt_base + 1];
							sum_c += A[a_base + 2] * B_t[bt_base + 2];
							sum_d += A[a_base + 3] * B_t[bt_base + 3];
						}

						sum_a += sum_b;
						sum_c += sum_d;
						sum_a += sum_c;

						int sum = 0;
						for( ; k1 < end_k; k1++) {
							sum += A[a_row + k1] * B_t[bt_row + k1];
						} // k1

						result[a_row + j1] += sum + sum_a; // add
						bt_row += size;
					} // j1
					a_row += size;
				} // i1

			} // k
			bt_row_base += s_block_step;
		} // j
		a_row_base += s_block_step;
		bt_row_base = 0;
	} // i

/*  // for debug purpose
	printmatrix(size, A, 'A');
	printmatrix(size, B, 'B');
	printmatrix(size, result, 'C');
//*/

	return result;
}

// second hopefully better approach that partitions the block differently
// it should only kick in for blocks of certain size.
// currently set to partitions of at least 64 values
// i.e. block sizes of at least 256 values
// for testing purpose also blocks allowed of min size 16 values
static int *mmul_blocks_multiple_accumulators(int size, int *A, int *B) {
	// memory for B^T and result; note: result must be zeroed!
	int *B_t = malloc(size * size * sizeof(*B_t));
	int *result = calloc(1, size * size * sizeof(*result));
	if(!result || !B_t) {
		fprintf(stderr, "%s: memory allocation error.\n", __func__);
		exit(1);
	}

	// get current block size
	assert(block_sizes_acc4_counter < block_sizes_acc4_n);
	int block = block_sizes_acc4[block_sizes_acc4_counter++];

	// transposed B (no swapping to avoid modifications in B)
	// using blocks here (see very large matrices)
	int s_row = 0;
	int s_row_base = 0;
	int s_block_step = block * size;
	for (int i = 0; i < size; i+=block) {
		int end_i = i+block;
		if(end_i > size) {
			end_i = size;
		}
		for (int j = 0; j < size; j+=block) {
			int end_j = j+block;
			if(end_j > size) {
				end_j = size;
			}
			s_row = s_row_base;
			for(int i1 = i; i1 < end_i; i1++) {
				for(int j1 = j; j1 < end_j; j1++) {
					B_t[size * j1 + i1] = B[s_row + j1];					
				}
				s_row += size;
			}
		}
		s_row_base += s_block_step;
	}

	// multiply
	// also blocks here, since large matrices may not fit into the cache.
	int a_row = 0;
	int a_row_base = 0;
	int bt_row = 0;
	int bt_row_base = 0;

	// for using partitions.
	// limitation: currently only available for entire blocks to have nice quadratic blocks
	// (easier to implement) -> if worthwhile, extended general version may follow
	// currently: just pick nice block sizes that allow a good balance of wins by partitioning
	// and caching blocks
	int partition_size = block / 4;
	
	for (int i = 0; i < size; i+=block) {
		int end_i = i+block;
		bool full_i_block = true;
		if(end_i > size) {
			end_i = size;
			full_i_block = false;
		}

		for (int j = 0; j < size; j+=block) {
			int end_j = j+block;
			bool full_j_block = true;
			if(end_j > size) {
				end_j = size;
				full_j_block = false;
			}

			for (int k = 0; k < size; k+=block) {
				int end_k = k+block;
				bool full_k_block = true;
				if(end_k > size) {
					end_k = size;
					full_k_block = false;
				}

				// loops inside of the blocks
				a_row = a_row_base;

				bool use_partitioning = full_i_block && full_j_block && full_k_block;
				
				for(int i1 = i; i1 < end_i; i1++) {
					int a_base_a = a_row + k;
					int a_base_b = a_base_a + partition_size;
					int a_base_c = a_base_b + partition_size;
					int a_base_d = a_base_c + partition_size;

					bt_row = bt_row_base;

					for(int j1 = j; j1 < end_j; j1++) {
						int bt_base_a = bt_row + k;
						int bt_base_b = bt_base_a + partition_size;
						int bt_base_c = bt_base_b + partition_size;
						int bt_base_d = bt_base_c + partition_size;
						
						// currently a very simple heuristic to decide whether partitioning shall
						// be used. current simple decision: use it only on full sized quadratic blocks 
						if(use_partitioning) {
							int sum_a = 0;
							int sum_b = 0;
							int sum_c = 0;
							int sum_d = 0;
							
							for(int offset = 0; offset < partition_size; offset++) {
								sum_a += A[a_base_a + offset] * B_t[bt_base_a + offset];
								sum_b += A[a_base_b + offset] * B_t[bt_base_b + offset];
								sum_c += A[a_base_c + offset] * B_t[bt_base_c + offset];
								sum_d += A[a_base_d + offset] * B_t[bt_base_d + offset];
							}

							sum_a += sum_b;
							sum_c += sum_d;
							result[a_row + j1] += sum_a + sum_c; // add to result
						}
						else {
							int sum = 0;
							
							for(int k1 = k; k1 < end_k; k1++) {
								sum += A[a_row + k1] * B_t[bt_row + k1];
							} // k1
							
							result[a_row + j1] += sum; // add to result							
						}

						bt_row += size;
					} // j1
					a_row += size;
				} // i1

			} // k
			bt_row_base += s_block_step;
		} // j
		a_row_base += s_block_step;
		bt_row_base = 0;
	} // i

/*  // for debug purpose
	printmatrix(size, A, 'A');
	printmatrix(size, B, 'B');
	printmatrix(size, result, 'C');
//*/

	return result;
}

//--- additional things --------------------------------------------------------

// simple, not optimized version
bool compare_matrices(int size, int *C, int *ref) {
	int row = 0;
	for (int i = 0; i < size; i++) {
		for (int j = 0; j < size; j++) {
			int index = row + j;
			if(C[index] != ref[index]) {
				return false;
			}
		}
		row += size;
	}
	return true;
}

typedef int *(*matrix_multiplier)(int, int *, int *);

// to avoid side effects by caching, testing the algorithms for correctness
// and timing them has been split into 2 separate routines
// Note: fresh matrices A and B are created each time from scratch for timing

// tests a given algorithm mm on correctness
bool check_algorithm(int size, int *A, int *B, int *ref, matrix_multiplier mm, char *name) {
	fprintf(stderr, "checking: %s... ", name);
	int *C = mm(size, A, B);

	if(!C) {
		fprintf(stderr, "Memory error!\n");
		return false;
	}

	if(!compare_matrices(size, C, ref)) {
		fprintf(stderr, "FAILED. Wrong result.\n");
		free(C);
		C = NULL;
		return false;
	}

	fprintf(stderr, "RESULT OK.\n");
	free(C);
	C = NULL;
	return true;
}

// banchmarks the algorithms
bool time_algorithm(int size, matrix_multiplier mm, char *name) {
	uint64_t stop = 0;
	uint64_t start = 0;

	fprintf(stderr, "preparing matrices... ");
	reset_testbench();
	int *A = randmatrix(size);
	if(!A) {
		fprintf(stderr, "Memory error!\n");
		return false;
	}
	int *B = randmatrix(size);
	if(!B) {
		fprintf(stderr, "Memory error!\n");
		free(A);
		A = NULL;
		return false;
	}

	fprintf(stderr, "running: %s... ", name);	
	RDTSC_START(start);
	int *C = mm(size, A, B);
	RDTSC_STOP(stop);
	add_measurement(start, stop);

	free(B);
	B = NULL;
	free(A);
	A = NULL;

	if(!C) {
		fprintf(stderr, "Memory error!\n");
		return false;
	}

	free(C);
	C = NULL;

	struct testbench_statistics stat = testbench_get_statistics();
	//print_testbench_statistics(title, stat); // no need for n only 1
	//print_histogram(title, stat); // not possible with n only 1

	uint64_t size3 = (uint64_t)size;
	size3 = size3 * size3 * size3;
	double cpi = stat.mean / (double)(size3);
	fprintf(stderr, "%e cycles, %f cycles / iteration (size^3)\n", stat.mean, cpi);
	printf("\t%e", cpi);

	return true;
}


static matrix_multiplier tests[] = {
	mmul,
	mmul_betterIndexCalculation,
	mmul_transposedB,
	mmul_transposedB_and_betterIndexCalculation,
	mmul_blocks,
	mmul_blocks,
	mmul_blocks,
	mmul_blocks,
	mmul_blocks,
	mmul_blocks_multiple_accumulators,
	mmul_blocks_multiple_accumulators,
	mmul_blocks_multiple_accumulators,
	mmul_blocks_multiple_accumulators,
	mmul_blocks_multiple_accumulators		
};

static char *names[] = {
	"default",
	"betterIndexCalculation",
	"transposedB",
	"transposedAndBetterIndex",
	"blocks_1024",
	"blocks_512",
	"blocks_256",
	"blocks_64",
	"blocks_16",
	"blocks_1024_accumulators_4",
	"blocks_512_accumulators_4",
	"blocks_256_accumulators_4",
	"blocks_64_accumulators_4",
	"blocks_16_accumulators_4"	
};

int main(int argc, char **argv) {
	if (argc != 2) {
		fprintf(stderr, "USAGE: mmul <matrix_size> >result.txt\n");
		return 1;
	}
	int size = atoi(argv[1]);
	fprintf(stderr, "CASP Simple Matrix Multiplicator. Matrix size: %d\n", size);

	// initialization
	if( !create_testbench(TESTBENCH_STD_N) ) {
		fprintf(stderr, "Error: could not open testbench (memory?).\n");
		exit(1);
	}
	set_denominator(1);

	srand(time(NULL));

	int *A = randmatrix(size);
	//printmatrix(size, A, 'A');

	int *B = randmatrix(size);
	//printmatrix(size, B, 'B');

	// reference result
	fprintf(stderr, "calculating reference solution for comparison.\n");
	int *ref = mmul(size, A, B);

	// prepare table header
	int n_tests = sizeof(tests) / sizeof(tests[0]);
	printf("\n\nsize");
	for(int i = 0; i < n_tests; i++) {
		printf("\t%s", names[i]);
	}
	printf("\n%d", size);

	// check algorithms to be tested:
	for(int i = 0; i < n_tests; i++) {
		if(!check_algorithm(size, A, B, ref, tests[i], names[i])) {
			free(B);
			B = NULL;
			free(A);
			A = NULL;
			exit(1);
		}
	}
	fprintf(stderr, "\n");
	free(B);
	B = NULL;
	free(A);
	A = NULL;

	// benchmark algorithms to be tested:
	block_sizes_counter = 0;
	block_sizes_acc4_counter = 0;
	for(int i = 0; i < n_tests; i++) {
		if(!time_algorithm(size, tests[i], names[i])) {
			break;
		}
	}

	printf("\n");
	delete_testbench();
	return 0;
}

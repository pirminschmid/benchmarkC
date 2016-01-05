#!/bin/sh
./test_rdtsc_main
./test_stat_functions_main
./test_memcpy
./main
./mmul 100 >result.txt
# low n=100 only to avoid strain on the server
# use more interesting n=1000, 2000, .... for testing

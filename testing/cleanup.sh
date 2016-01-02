#!/bin/sh
cd test_rdtsc
make clean
./rm_library.sh
cd ..
#
cd test_stat_functions
make clean
./rm_library.sh
cd ..
#
cd ../example1
make clean
./rm_library.sh
cd ../testing
#
cd ../example2
make clean
./rm_library.sh
cd ../testing
#
rm test_rdtsc_main test_stat_functions_main main mmul result.txt

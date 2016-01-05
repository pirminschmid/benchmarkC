#!/bin/sh
cd test_rdtsc
./get_library.sh
make
cp test_rdtsc_main ..
make clean
cd ..
#
cd test_stat_functions
./get_library.sh
make
cp test_stat_functions_main ..
make clean
cd ..
#
cd ../example1
./get_library.sh
make
cp test_memcpy ../testing
make clean
cd ../testing
#
cd ../example2
./get_library.sh
make
cp main ../testing
make clean
cd ../testing
#
cd ../example3
./get_library.sh
make
cp mmul ../testing
make clean
cd ../testing

benchmarkC
==========
[![Build Status](https://travis-ci.org/pirminschmid/benchmarkC.svg?branch=master)](https://travis-ci.org/pirminschmid/benchmarkC)

A toolbox for (micro)benchmarking of C programs on x86_64.

Performance matters a lot. There are lots of pitfalls when the performance of a code is assessed. This toolbox here is not even trying to compete with available professional profiling tools. Also for microbenchmarking, I recommend/use [google/benchmark][google_benchmark] that was showcased in this excellent [presentation by Chandler Carruth][microbenchmarking] about microbenchmarking at CppCon 2015. Nevertheless, this toolbox here works well for exercises and to learn first-hand about some of the pitfalls associated with benchmarking and learn about some interesting/surprising results while testing programs on current processors (no spoilers here). It uses rdtsc.h from [here][rdtsc.h] based on a paper by G. Paoloni ([link][intel_paper]). It was written for OSX (Clang) and also tested on Fedora (GCC). And, it comes with some convenient statistics goodies that may be useful to you in other applications as well.

Current version v0.92 (2016-01-05). [Feedback][feedback] welcome.


Usage
-----
You will need the 3 files in the [folder benchmark][files] for your C projects. See the source codes and Makefiles of [example 1][example1] (memcpy() vs copy data by loop), [example 2][example2] (branch misprediction penalty), and [example 3][example3] (classic matrix multiplication) as examples how the library can be used. Use get_library.sh to copy the library files before compilation of the examples. You can build all tests and examples quite simply:
```sh
cd testing
./build_all.sh
# all executables are in the testing folder
```
Use cleanup.sh to remove all built files.

License
-------

Copyright (c) 2015 Pirmin Schmid, [MIT license][license].

[google_benchmark]:https://github.com/google/benchmark
[microbenchmarking]:https://www.youtube.com/watch?v=nXaxk27zwlk
[rdtsc.h]:https://idea.popcount.org/2013-01-28-counting-cycles---rdtsc/
[intel_paper]:http://www.intel.com/content/www/us/en/embedded/training/ia-32-ia-64-benchmark-code-execution-paper.html
[files]:benchmark/
[example1]:example1/
[example2]:example2/
[example3]:example3/
[license]:LICENSE
[feedback]:mailto:mailbox@pirmin-schmid.ch?subject=benchmarkC

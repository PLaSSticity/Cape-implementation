# Cape-implementation
Cape is a compiler analysis and transformation that soundly and automatically protects programs from cache side-channel attacks using commodity
hardware transactional memory (HTM). 
It consists of program analysis
and instrumentation techniques that identify sensitive data
and code, delimit transactions, and insert code to preload
sensitive data and code at the beginning of a transaction.
More information can be found in our CC 2022 paper [*Cape: Compiler-Aided Program Transformation for HTM-Based Cache Side-Channel Defense*](https://mdbond.github.io/cape-cc-2022.pdf).

This repository provides source code of the Cape implementation, which uses dependence analysis from [*dg*](https://github.com/mchalupa/dg).
To build the code, **install llvm-6.0**, **enable tsx support** on your machine and run the following commands (root directory of the code is denoted as `CAPE_ROOT`)
```Bash
cd CAPE_ROOT
mkdir build && cd build
cmake -DLLVM_DIR=/usr/lib/llvm-6.0/cmake .. # assuming llvm-6.0 has been installed in the default location
make -j
```
The directory `CAPE_ROOT/samples` contains five sample programs that can be used to show Cape's capability.
We have marked in each program its secret variable using `__attribute__((annotate("secret")))`.

To use Cape to analyze and transform a sample program (for example, the array-based decison tree implementation `dtree`), run the following commands
```Bash
cd CAPE_ROOT/samples
clang++-6.0 -emit-llvm -c dtree.c -mrtm -O3 -DUSE_TX -fno-use-cxa-atexit -o dtree.bc
CAPE_ROOT/build/tools/llvm-dg-dump dtree.bc 
clang++-6.0 dtree.bc_ac.ll -O3 -o dtree_cape
```
We also provide a script `analyze.sh` to ease the above procedure. To use the script to analyze and transform one or more programs, run
```Bash
./analyze.sh aes dtree bsearch
```

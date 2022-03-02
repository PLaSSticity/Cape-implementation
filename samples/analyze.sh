#!/bin/bash

CMD="rm -f out.txt"
echo $CMD
eval $CMD

for b in "$@"
do       
            CMD="clang++ -emit-llvm -c $b.c -mrtm -O3 -DUSE_TX -fno-use-cxa-atexit -o $b\_tx.bc";
            echo $CMD;
            eval $CMD;

            CMD="../build/tools/llvm-dg-dump $b\_tx.bc > $b\_ac\_tx.ll 2> $b\_ac\_tx.err";
            echo $CMD;
            eval $CMD;

            CMD="clang++ $b\_tx.bc_ac.ll -O3 -o $b\_cape";
            echo $CMD;
            eval $CMD;

            CMD="./$b\_cape infile.txt out.txt";
            echo $CMD;
            eval $CMD;
done

#include "common.h"
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

volatile size_t x = 3;
__attribute__((annotate("secret"))) volatile size_t k;

extern "C" __attribute__((noinline)) size_t square(size_t res, size_t x) {
    for (register size_t j = 0; j < SLOWDOWN; ++j)
        asm("" ::"a"(j)
            : "memory");
    res = res * res;
    return res;
}

extern "C" __attribute__((noinline)) size_t multiply(size_t res, size_t x) {
    for (register size_t j = 0; j < SLOWDOWN; ++j)
        asm("" ::"a"(j)
            : "memory");
    res = x * res;
    return res;
}

size_t exp() {
    unsigned status = 0;
    size_t res = 1;
    for (ssize_t i = 63; i >= 0; --i) {
        res = square(res, x);
        // SENSITIVE: the conditional statement is sensitive since it is dependent on the sercet 'k'.
        if (((k >> i) & 1) == 1)
            res = multiply(res, x);
    }
    return res;
}

int main(int argc, char **argv) {
    FILE *fp = fopen(argv[1], "r");

    loadInput(fp);

    int *secs = new int[numSecs];
    generateSecrets(numSecs, secs, size);

    double time = 0;
    volatile int sum = 0;

    auto start_t = __parsec_roi_begin();

    for (int i = 0; i < iters; i++) {
        k = (size_t)secs[i % numSecs];
        volatile size_t res = exp();
    }

    auto end_t = __parsec_roi_end();
    auto time_span = end_t - start_t;
    time += time_span;

    printCycles(time, argv[2]);
    return 0;
}

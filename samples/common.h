//
// Created by ruiz on 6/16/19.
//

#ifndef DG_COMMON_H
#define DG_COMMON_H

#include <chrono>
#include <immintrin.h>
#include <map>
#include <set>
#include <stdio.h>
#include <string.h>
#include <sys/types.h> /* size_t */
#include <utility>     // std::pair, std::get
#include <vector>
#include <x86intrin.h>

using namespace std;
using namespace std::chrono;

int size, iters;

uintptr_t start, length;

const int MAX_RETRIES = 200;

const long lineOffMask = 63;

const int numSecs = 100;

char prog[255] = {0};

#ifdef USE_TX
// const int RUNS = 5000000;
int txAttempts = 0;
int txCommitted = 0;
#else
// const int RUNS = 5;
#endif

#ifndef USE_TX
__attribute__((noinline))
#endif
void
preloadInstAddr() {
#ifndef NO_PRELD
    // printf("starting addr and length: %lx, %lx\n", start, length);
    uintptr_t addr = (uintptr_t)(start & (~lineOffMask));
    uintptr_t uend = start + length;
    // printf("to touch addr range: %p - %p\n", addr, uend);
    volatile int sum;
    for (; addr < uend; addr += 64) {
        sum = *((int *)addr);
        // addr++;
        // printf("touched addr: %p ", addr);
    }
    // printf("\n");
    // printf("\nending addr: %p\n\n", uend);
#endif
}

struct CompareCStrings {
    bool operator()(char const *lhs, char const *rhs) const {
        return strcmp(lhs, rhs) < 0;
    }
};

map<char *, pair<uintptr_t, uintptr_t>, CompareCStrings> funcMap;

#ifndef USE_TX
__attribute__((noinline))
#endif
void
preloadInstAddr(char *fname) {
#ifndef NO_PRELD
    // printf("preloading for %s: ", fname);
    if (funcMap.find(fname) != funcMap.end()) {
        start = funcMap[fname].first;
        length = funcMap[fname].second;
    } else {
        return;
    }
    uintptr_t addr = (uintptr_t)(start & (~lineOffMask));
    uintptr_t uend = start + length;
    // printf(", to touch addr range: %lu - %lu\n", addr, uend);
    volatile int sum;
    // bool f = false;
    // if (strcmp(fname, "_fp_exptmod") == 0) {
    //     f = true;
    // }
    for (; addr < uend; addr += 64) {
        sum = *((int *)addr);
        // if (f)
        //     printf("touched addr: %lu \n", addr);
    }
    // if (f)
    //     exit(-1);
    // printf("\n");
    // printf("\nending addr: %p\n\n", uend);
#endif
}

#ifndef USE_TX
__attribute__((noinline))
#endif
void
preloadInstAddr(uintptr_t start, uintptr_t length) {
#ifndef NO_PRELD
    // printf("starting addr and length: %lx, %lx\n", start, length);
    uintptr_t addr = (uintptr_t)(start & (~lineOffMask));
    uintptr_t uend = start + length;
    // printf("to touch addr range: %p - %p\n", addr, uend);
    volatile int sum;
    for (; addr < uend; addr += 64) {
        sum = *((int *)addr);
        // addr++;
        // printf("touched addr: %p ", addr);
    }
    // printf("\n");
    // printf("\nending addr: %p\n\n", uend);
#endif
}

#ifndef USE_TX
__attribute__((noinline))
#endif
void
preloadInstAddrForCloak() {
#ifndef NO_PRELD
    for (auto e : funcMap) {
        // printf("preloading %s: ", e.first);
        start = e.second.first;
        length = e.second.second;
        uintptr_t addr = (uintptr_t)(start & (~lineOffMask));
        uintptr_t uend = start + length;
        // printf("%lu, %lu.\n", addr, uend);
        volatile int sum;
        for (; addr < uend; addr += 64) {
            sum = *((int *)addr);
        }
    }
#endif
}

#define SLOWDOWN 512

#ifdef USE_TX
__attribute__((always_inline)) void startTransaction() {
#ifndef NO_TX
    // printf("startTransaction\n");
    unsigned status;
    int retries = 0;
    txAttempts += 1;
    while ((status = _xbegin()) != _XBEGIN_STARTED) {
        txAttempts++;
        if (retries++ >= MAX_RETRIES) {
            fprintf(stderr, "Terminate the program since transactions failed with status: %x.\n", status);
            exit(status);
        }
        /*
        for (register size_t j = 0; j < SLOWDOWN; ++j)
            asm("" ::"a"(j)
                : "memory");
        */
        // fprintf(stderr, "Retrying transacstion: %d...\n", retries);
    }
#endif
}

__attribute__((always_inline)) void endTransaction() {
#ifndef NO_TX
    if (_xtest()) {
        _xend();
        txCommitted += 1;
    }
#endif
}
#else

__attribute__((noinline)) void startTransaction() { asm(""); }

__attribute__((noinline)) void endTransaction() { asm(""); }

#endif

struct allocInst {
    int size;
    vector<void *> stack;
};

struct mallocInst {
    int size;
    int len = 0;
    void *array[20000];
};

std::map<int, allocInst> allocMap;
std::map<int, mallocInst> mallocMap;

// mallocInst mallocMap[10];

#ifndef USE_TX
__attribute__((noinline))
#endif
void
insertMallocSet(int idx, int size, void *pt) {
    // auto *E = &mallocMap[idx];
    // E->size = size;
    // (E->array)[E->len++] = pt;
}

#ifndef USE_TX
__attribute__((noinline))
#endif
void
iterateMallocSet(int idx) {
#ifndef NO_PRELD
    auto *E = &mallocMap[idx];
    volatile int sum;
    auto array = E->array;
    int l = E->len;
    for (int i = 0; i < l; ++i) {
        auto ptr = array[i];
        uintptr_t ustart = (uintptr_t)((uintptr_t)(ptr) & (~lineOffMask));
        uintptr_t uend = (uintptr_t)(ptr) + E->size;
        for (; ustart < uend; ustart += 64) {
            sum = *(int *)(ustart);
            //printf("loading allocMap[%d] at addr: %d\t", idx, ustart);
        }
        //printf("\n");
    }
#endif
}

bool eraseMallocSet(int idx, void *pt) { /*
    mallocInst *E = &mallocMap[idx];
    uint32_t num;
    if ((num = E->set.erase(pt)) > 0) {
        printf("removing mallocMap[%d] data of num %u: %d\n", idx, num, *((int *) pt));
    }
    return (num > 0);*/
    return true;
}

#ifndef USE_TX
__attribute__((noinline))
#endif
void
iterateGlobal(int size, void *pt) {
#ifndef NO_PRELD
    volatile int sum;
    uintptr_t ustart = (uintptr_t)((uintptr_t)((int *)pt) & (~lineOffMask));
    uintptr_t uend = (uintptr_t)((int *)pt) + size;
    for (; ustart < uend; ustart += 64) {
        sum = *(int *)(ustart);
        // printf("loading allocMap[%d] at addr: %d\t", idx, ustart);
    }
    // printf("\n");
    // printf("glob size: %d\n", E->size);
#endif
}

/*
bool eraseGlobal(int idx) {
    globVal* E = &globMap[idx];
    uint32_t num;
    if ((num = E->set.erase(pt)) > 0){
        printf("removing mallocMap[%d] data of num %u: %d\n", idx, num, *((int*)pt));
    }
    return (num > 0);
}
*/

#ifndef USE_TX
__attribute__((noinline))
#endif
void
pushAllocStack(int idx, long a_size, int e_size, void *pt) {
    allocInst *E = &allocMap[idx];
    E->size = e_size * a_size;
    // printf("e_size = %d; a_size = %ld\n", e_size, a_size);
    E->stack.push_back(pt);
}

#ifndef USE_TX
__attribute__((noinline))
#endif
void
iterateAllocStack(int idx) {
#ifndef NO_PRELD
    allocInst *E = &allocMap[idx];
    volatile int sum;
    for (auto i = E->stack.begin();
         i != E->stack.end();
         ++i) {
        uintptr_t ustart = (uintptr_t)((uintptr_t)(*i) & (~lineOffMask));
        uintptr_t uend = (uintptr_t)(*i) + E->size;
        for (; ustart < uend; ustart += 64) {
            sum = *(int *)(ustart);
            // printf("loading allocMap[%d] at addr: %d\t", idx, ustart);
        }
        // printf("\n");
    }
    // printf("alloc size: %d\n", E->size);
#endif
}

#ifndef USE_TX
__attribute__((noinline))
#endif
void
popAllocStack(int idx) {
    allocInst *E = &allocMap[idx];
    if (!E->stack.empty()) {
        // void* cur = E->stack.back();
        E->stack.pop_back();
        // printf("removing allocMap[%d] data: %d\t", idx, *((int*)cur));
    }
    // printf("\n");
}

#ifndef USE_TX
__attribute__((noinline))
#else
__attribute__((always_inline))
#endif
double
__parsec_roi_begin() {
    asm("");
    return 0;
    // auto current_time = high_resolution_clock::now();
    // auto duration_in_seconds = std::chrono::duration<double>(current_time.time_since_epoch());
    // return duration_in_seconds.count();
}

#ifndef USE_TX
__attribute__((noinline))
#else
__attribute__((always_inline))
#endif
double
__parsec_roi_end() {
    asm("");
    return 0;
    // auto current_time = high_resolution_clock::now();
    // auto duration_in_seconds = std::chrono::duration<double>(current_time.time_since_epoch());
    // return duration_in_seconds.count();
}

void printCycles(double time, char *dstFile) {
#ifdef USE_TX
    FILE *fp;
    fp = fopen(dstFile, "a");
    float r = 0.0;

    if (txCommitted != 0)
        r = txAttempts * 1.0 / txCommitted;

    fprintf(fp, "%f %f ", time, r);
    fclose(fp);
    printf("txAttempts: %d; txCommitted: %d; ratio: %f\n", txAttempts, txCommitted, r);
#else
    printf("It took me %f seconds.\n", time);
#endif
}

void generateSecrets(int iters, int *secs, int dsize) {
    srand(time(NULL));
    for (int i = 0; i < iters; i++) {
        secs[i] = rand() % (2 * dsize);
    }
}

void generateSecretsForAES(int iters, int **secs) {
    srand(time(NULL));
    for (int i = 0; i < iters; i++) {
        for (int j = 0; j < 44; j++) {
            secs[i][j] = rand();
            // printf("%d ", secs[i][j]);
        }
        // printf("\n");
    }
}

void loadInput(FILE *fp) {
    fscanf(fp, "%d", &size);
    fscanf(fp, "%d", &iters);
    fscanf(fp, "%lx", &start);
    fscanf(fp, "%lx", &length);
}

void loadInputForSig(FILE *fp) {
    char *fname = new char[255];
    while (fscanf(fp, "%s", fname) == 1) {
        if (fscanf(fp, "%lx", &start) != 1) {
            fprintf(stderr, "error loading func info\n.");
            exit(-1);
        }
        if (fscanf(fp, "%lx", &length) != 1) {
            fprintf(stderr, "error loading func info\n.");
            exit(-1);
        }
        funcMap[fname] = pair<uintptr_t, uintptr_t>(start, length);
        fname = new char[255];
    }
    if (feof(fp)) {
        fprintf(stderr, "finish loading func info (map size: %lu).\n", funcMap.size());
    } else {
        fprintf(stderr, "error loading func info.\n");
    }
}

#endif //DG_COMMON_H

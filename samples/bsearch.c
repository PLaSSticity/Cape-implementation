#include "common.h"

__attribute__((annotate("secret"))) int g;

/*
 * Perform a binary search.
 *
 * The code below is a bit sneaky.  After a comparison fails, we
 * divide the work in half by moving either left or right. If lim
 * is odd, moving left simply involves halving lim: e.g., when lim
 * is 5 we look at item 2, so we change lim to 2 so that we will
 * look at items 0 & 1.  If lim is even, the same applies.  If lim
 * is odd, moving right again involes halving lim, this time moving
 * the base up one item past p: e.g., when lim is 5 we change base
 * to item 3 and make lim 2 so that we will look at items 3 and 4.
 * If lim is even, however, we have to shrink it by one before
 * halving: e.g., when lim is 4, we still looked at item 2, so we
 * have to make lim 3, then halve, obtaining 1, so that we will only
 * look at item 3.
 */
__attribute__((noinline)) void *
bsearch(const void *key, const void *base0,
        size_t nmemb, size_t size,
        int (*compar)(const int, const int)) {
    const char *base = (const char *)base0;
    // printf("base addr: %p\n", base);
    int lim, cmp;
    const void *p, *ret;
    
    // SENSITIVE: the whole loop is sensitive since its exit is dependent on the sercet 'key'.
    for (lim = nmemb; lim != 0; lim >>= 1) {
        p = base + (lim >> 1) * size;
        cmp = *(int *)key - *(int *)p;
        if (cmp == 0) {
            break;
        }
        if (cmp > 0) { /* key > p: move right */
            base = (const char *)p + size;
            lim--;
        } /* else move left */
    }
    if (lim != 0) {
        ret = p;
    } else {
        ret = NULL;
    }

    return (void *)ret;
}

// lead to nested transactions
/*
int cmpfunc(const void * a, const void * b) {
   return ( *(int*)a - *(int*)b );
}
*/

int cmpfunc(const int a, const int b) {
    return (a - b);
}

// int values[] = { 5, 20, 29, 32, 63 };

void constructArray(int a[], int n) {
    for (int i = 0; i < n; i++) {
        a[i] = rand() % n;
    }

    for (int i = 0; i < n; i++) //Loop for ascending ordering
    {
        for (int j = 0; j < n; j++) //Loop for comparing other values
        {
            if (a[j] > a[i]) //Comparing other array elements
            {
                int tmp = a[i]; //Using temporary variable for storing last value
                a[i] = a[j];    //replacing value
                a[j] = tmp;     //storing last value
            }
        }
    }

    /*printf("\n\nAscending : ");                     //Printing message
    for (int i = 0; i < n; i++)                     //Loop for printing array data after sorting
    {
        printf(" %d ", a[i]);
    }*/
}

int main(int argc, char *argv[]) {

    FILE *fp = fopen(argv[1], "r");

    loadInput(fp);

    int *secs = new int[numSecs];
    generateSecrets(numSecs, secs, size);

    int values[size];
    constructArray(values, size);

    double time = 0;
    volatile int sum = 0;

    auto start_t = __parsec_roi_begin();

    for (int i = 0; i < iters; i++) {
        g = secs[i % numSecs];
        int *item = (int *)bsearch(&g, values, size, sizeof(int), cmpfunc);
        sum += (item == NULL ? 0 : 1);
    }

    auto end_t = __parsec_roi_end();
    auto time_span = end_t - start_t;
    time += time_span;

    printCycles(time, argv[2]);

    return (0);
}

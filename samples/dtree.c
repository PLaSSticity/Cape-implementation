#include "common.h"

volatile int __attribute__((annotate("secret"))) sec;

struct nelem_t {
    size_t left, right_or_leafid;
    int fdim;
    float fthresh;
};

typedef struct nelem_t *Nodes;
typedef uint16_t *LeafIds;

// void lookup_leafids(Nodes& nodes, Queries& queries, LeafIds& leafids) {
//for (auto size_t i = 0; i < = queries.entries(); i++) {
__attribute__((noinline)) void lookup_leafids(Nodes nodes, LeafIds leafids) {
    size_t node = 0;
    size_t left, right;
    //if (sec > 43)
    //      if (sec < 100)
    // startTransaction();
    // SENSITIVE: the whole loop is sensitive since its exit is dependent on the sercet 'sec'.
    while (node != -1) {
        struct nelem_t _node;
        _node.left = nodes[node].left;
        _node.right_or_leafid = nodes[node].right_or_leafid;
        _node.fdim = nodes[node].fdim;
        _node.fthresh = nodes[node].fthresh;
        left = _node.left;
        right = _node.right_or_leafid;
        // printf("node is %d\n", node);
        if (left == node) {
            leafids[0] = (uint16_t)right;
            break;
        }

        if (sec <= _node.fthresh) {
            node = left;
        } else {
            node = right;
        }
    }
    // endTransaction();
    //}
}

void constructTree(Nodes ns, int size) {

    // int thred = (int)pow(2, floor(log(size) / log(2)));
    // printf("thred = %d at size %d\n", thred, size);
    for (int i = 0; i < size; i++) {

        if (2 * i + 1 < size) {
            ns[i].left = 2 * i + 1;
            if (2 * i + 2 < size)
                ns[i].right_or_leafid = 2 * i + 2;
            else
                ns[i].right_or_leafid = -1;
        } else
            ns[i].left = i;

        ns[i].fdim = 0;
        ns[i].fthresh = rand() % size;
        // printf("ns[%d] = %f\n", i, ns[i].fthresh);
    }
}

int main(int argc, char *argv[]) {
    FILE *fp = fopen(argv[1], "r");

    loadInput(fp);

    int *secs = new int[numSecs];
    generateSecrets(numSecs, secs, size);

    struct nelem_t nodes[size];
    // printf("nodes heads addr\n"); // lead to weird trace differences
    uint16_t leafids[1];
    constructTree(nodes, size);

    double time = 0;
    auto start_t = __parsec_roi_begin();

    for (int i = 0; i < iters; i++) {
        sec = secs[i % numSecs];
        lookup_leafids(nodes, leafids);
    }
    auto end_t = __parsec_roi_end();
    auto time_span = end_t - start_t;
    time += time_span;

    printCycles(time, argv[2]);
}

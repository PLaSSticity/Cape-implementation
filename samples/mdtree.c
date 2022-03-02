#include "common.h"
#include <queue>

using namespace std;

volatile int __attribute__((annotate("secret"))) sec;

struct nelem_t {
    nelem_t *left, *right_or_leafid;
    int fdim;
    int fthresh;
};

typedef struct nelem_t *node_p;

__attribute__((noinline)) void lookup_leafids(node_p root, int *leaf) {
    node_p cur = root;
    // SENSITIVE: the whole loop is sensitive since its exit is dependent on the sercet 'sec'.
    while (cur != NULL) {
        // printf("cur id is %u, %d\n", cur->fdim, cur->fthresh);
        if (cur->left == cur) {
            *leaf = cur->fdim;
            break;
        }
        // if (queries.item(i, _node.fdim) <= _node.fthresh) {
        if (sec <= cur->fthresh) {
            cur = cur->left;
        } else {
            cur = cur->right_or_leafid;
        }
    }
}

node_p constructTree(int id, int size) {
    node_p root = (node_p)malloc(sizeof(nelem_t));
    root->fdim = id;
    root->fthresh = rand() % size;

    if (2 * id + 1 < size) {
        root->left = constructTree(2 * id + 1, size);
        if (2 * id + 2 < size)
            root->right_or_leafid = constructTree(2 * id + 2, size);
        else
            root->right_or_leafid = NULL;
    } else {
        root->left = root;
    }

    return root;
}

int main(int argc, char *argv[]) {
    FILE *fp = fopen(argv[1], "r");

    loadInput(fp);

    int *secs = new int[numSecs];
    generateSecrets(numSecs, secs, size);

    // printf("nodes heads addr\n"); // lead to weird trace differences
    int leafids;
    // float queries[3][1];
    node_p nodes[size];
    node_p root = constructTree(0, size);

    double time = 0;
    auto start_t = __parsec_roi_begin();

    for (int i = 0; i < iters; i++) {
        sec = secs[i % numSecs];
        lookup_leafids(root, &leafids);
        //printf("%d\n", leafids);
    }

    auto end_t = __parsec_roi_end();
    auto time_span = end_t - start_t;
    time += time_span;

    printCycles(time, argv[2]);
}

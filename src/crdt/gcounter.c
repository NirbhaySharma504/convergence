// gcounter.c — grow-only counter implementation (§6.2).
//
// Each node only ever writes its own slot (counts[my_id]). Merge takes the
// element-wise max per slot: since counts only grow, max can never lose an
// increment, which makes merge commutative, associative, and idempotent.
#include "gcounter.h"
#include <string.h>

void gcounter_init(gcounter_t *gc, int my_id, int num_nodes) {
    memset(gc->counts, 0, sizeof(gc->counts));
    gc->my_id = my_id;
    gc->num_nodes = num_nodes;
}

void gcounter_increment(gcounter_t *gc) {
    gc->counts[gc->my_id]++;
}

uint64_t gcounter_value(const gcounter_t *gc) {
    uint64_t sum = 0;
    for (int i = 0; i < MAX_NODES; i++)
        sum += gc->counts[i];
    return sum;
}

void gcounter_merge(gcounter_t *dst, const gcounter_t *src) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (src->counts[i] > dst->counts[i])
            dst->counts[i] = src->counts[i];
    }
}

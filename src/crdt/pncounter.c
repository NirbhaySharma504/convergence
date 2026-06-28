// pncounter.c — PN-Counter implementation (§6.3).
//
// A single G-Counter can only grow (each node writes its own slot), so it
// cannot represent decrements. We keep a second G-Counter N for decrements;
// the net value is P - N, computed only at read time. Both halves are
// G-Counters, so both merge correctly with element-wise max.
#include "pncounter.h"

void pncounter_init(pncounter_t *pc, int my_id, int num_nodes) {
    gcounter_init(&pc->P, my_id, num_nodes);
    gcounter_init(&pc->N, my_id, num_nodes);
}

void pncounter_increment(pncounter_t *pc) { gcounter_increment(&pc->P); }
void pncounter_decrement(pncounter_t *pc) { gcounter_increment(&pc->N); }

int64_t pncounter_value(const pncounter_t *pc) {
    return (int64_t)gcounter_value(&pc->P) - (int64_t)gcounter_value(&pc->N);
}

void pncounter_merge(pncounter_t *dst, const pncounter_t *src) {
    gcounter_merge(&dst->P, &src->P);
    gcounter_merge(&dst->N, &src->N);
}

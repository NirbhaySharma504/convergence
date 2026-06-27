// gcounter.h — grow-only counter (§6.2).
#ifndef CONVERGENCE_GCOUNTER_H
#define CONVERGENCE_GCOUNTER_H

#include "common.h"

typedef struct {
    uint64_t counts[MAX_NODES]; // counts[i] = total increments from node i
    int      num_nodes;
    int      my_id;
} gcounter_t;

void     gcounter_init      (gcounter_t *gc, int my_id, int num_nodes);
void     gcounter_increment (gcounter_t *gc);                         // counts[my_id]++
uint64_t gcounter_value     (const gcounter_t *gc);                   // sum of counts
void     gcounter_merge     (gcounter_t *dst, const gcounter_t *src); // element-wise max

#endif

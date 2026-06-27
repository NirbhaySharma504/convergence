// vclock.h — vector clock for causality tracking (§6.1).
#ifndef CONVERGENCE_VCLOCK_H
#define CONVERGENCE_VCLOCK_H

#include "common.h"

typedef struct {
    uint64_t ticks[MAX_NODES]; // ticks[i] = number of events seen from node i
    int      num_nodes;        // how many nodes are in the system
    int      my_id;            // this node's index into ticks[]
} vclock_t;

typedef enum {
    VC_BEFORE,      // a happened-before b
    VC_AFTER,       // a happened-after b
    VC_EQUAL,       // identical clocks
    VC_CONCURRENT   // neither dominates the other
} vc_order_t;

void       vclock_init    (vclock_t *vc, int my_id, int num_nodes);
void       vclock_tick    (vclock_t *vc);                          // ticks[my_id]++
void       vclock_merge   (vclock_t *dst, const vclock_t *src);    // element-wise max
vc_order_t vclock_compare (const vclock_t *a, const vclock_t *b);  // ordering
void       vclock_copy    (vclock_t *dst, const vclock_t *src);

#endif

// vclock.c — vector clock implementation (§6.1).
#include "vclock.h"
#include <string.h>

void vclock_init(vclock_t *vc, int my_id, int num_nodes) {
    memset(vc->ticks, 0, sizeof(vc->ticks));
    vc->my_id = my_id;
    vc->num_nodes = num_nodes;
}

void vclock_tick(vclock_t *vc) {
    vc->ticks[vc->my_id]++;
}

void vclock_merge(vclock_t *dst, const vclock_t *src) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (src->ticks[i] > dst->ticks[i])
            dst->ticks[i] = src->ticks[i];
    }
}

vc_order_t vclock_compare(const vclock_t *a, const vclock_t *b) {
    int a_greater = 0, b_greater = 0;
    for (int i = 0; i < MAX_NODES; i++) {
        if (a->ticks[i] > b->ticks[i]) a_greater = 1;
        else if (a->ticks[i] < b->ticks[i]) b_greater = 1;
    }
    if (a_greater && b_greater) return VC_CONCURRENT;
    if (a_greater)              return VC_AFTER;
    if (b_greater)              return VC_BEFORE;
    return VC_EQUAL;
}

void vclock_copy(vclock_t *dst, const vclock_t *src) {
    memcpy(dst, src, sizeof(vclock_t));
}

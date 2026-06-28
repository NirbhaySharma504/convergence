// lww_register.c — last-writer-wins register (§6.4).
//
// Ordering uses the vector clock, NOT wall time. Wall clocks drift, so a node
// with a fast clock would always "win"; vector clocks capture real causality.
// On concurrent writes (neither clock dominates) we tie-break deterministically
// by the higher writer_id — arbitrary but identical on every node, which is all
// convergence requires.
#include "lww_register.h"
#include <string.h>
#include <stdio.h>

void lww_register_init(lww_register_t *r, int my_id, int num_nodes) {
    memset(r, 0, sizeof(*r));
    vclock_init(&r->timestamp, my_id, num_nodes);
    r->writer_id = my_id;
    r->has_value = 0;
}

void lww_register_set(lww_register_t *r, const char *value, vclock_t *node_clock) {
    vclock_tick(node_clock);                 // this write is a new event
    vclock_copy(&r->timestamp, node_clock);  // stamp it with the node's clock
    r->writer_id = node_clock->my_id;
    snprintf(r->value, MAX_VALUE_LEN, "%s", value);
    r->has_value = 1;
}

void lww_register_get(const lww_register_t *r, char *out, size_t cap) {
    snprintf(out, cap, "%s", r->has_value ? r->value : "");
}

void lww_register_merge(lww_register_t *dst, const lww_register_t *src) {
    if (!src->has_value) return;
    if (!dst->has_value) { *dst = *src; return; }

    vc_order_t ord = vclock_compare(&dst->timestamp, &src->timestamp);
    int take_src = (ord == VC_BEFORE) ||
                   (ord == VC_CONCURRENT && src->writer_id > dst->writer_id);
    if (take_src) *dst = *src;
    // otherwise dst is causally later (or equal, or wins the tie): keep it.
}

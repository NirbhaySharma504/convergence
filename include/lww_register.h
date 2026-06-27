// lww_register.h — last-writer-wins register ordered by vector clock (§6.4).
#ifndef CONVERGENCE_LWW_REGISTER_H
#define CONVERGENCE_LWW_REGISTER_H

#include "vclock.h"

#define MAX_VALUE_LEN 512

typedef struct {
    char     value[MAX_VALUE_LEN];
    vclock_t timestamp;  // vector clock of the write that produced this value
    int      writer_id;  // node that wrote it (tie-break for concurrent writes)
    int      has_value;  // 0 until first set
} lww_register_t;

void lww_register_init  (lww_register_t *r, int my_id, int num_nodes);
// set ticks node_clock, then stores the new clock + value + writer.
void lww_register_set   (lww_register_t *r, const char *value, vclock_t *node_clock);
void lww_register_get   (const lww_register_t *r, char *out, size_t cap);
void lww_register_merge (lww_register_t *dst, const lww_register_t *src);

#endif

// orset.h — observed-remove set with add-wins semantics (§6.5).
#ifndef CONVERGENCE_ORSET_H
#define CONVERGENCE_ORSET_H

#include "common.h"

#define MAX_TAGS     1024   // per-set add/remove capacity (demo-sized)
#define MAX_ELEM_LEN 64

// Unique identifier for a single add operation.
typedef struct {
    uint32_t node_id;
    uint64_t seq;
} tag_t;

typedef struct {
    char  elem[MAX_ELEM_LEN];
    tag_t tag;
} orset_entry_t;

typedef struct {
    orset_entry_t added[MAX_TAGS];         // every (elem, tag) ever added
    int           add_count;
    tag_t         removed[MAX_TAGS];       // tombstoned tags
    int           remove_count;
    uint64_t      seq_counters[MAX_NODES]; // per-node sequence counter for new tags
    int           my_id;
    int           num_nodes;
} orset_t;

void orset_init     (orset_t *s, int my_id, int num_nodes);
void orset_add      (orset_t *s, const char *elem);       // fresh unique tag
void orset_remove   (orset_t *s, const char *elem);       // tombstone live tags
int  orset_contains (const orset_t *s, const char *elem); // any live tag?
void orset_list     (const orset_t *s, char out[][MAX_ELEM_LEN], int *count, int max);
void orset_merge    (orset_t *dst, const orset_t *src);

#endif

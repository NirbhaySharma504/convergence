// orset.c — observed-remove set, add-wins (§6.5).
//
// Core invariant: an element is in the set iff at least one of its add-tags is
// NOT in the removed set. Each add mints a fresh unique tag (node_id, seq). A
// remove can only tombstone tags it has already observed; a concurrent add
// creates a tag the remove never saw, so that add survives the merge — this is
// what "add-wins" means.
#include "orset.h"
#include <string.h>
#include <stdio.h>

static int tag_eq(tag_t a, tag_t b) {
    return a.node_id == b.node_id && a.seq == b.seq;
}

static int tag_is_removed(const orset_t *s, tag_t t) {
    for (int i = 0; i < s->remove_count; i++)
        if (tag_eq(s->removed[i], t)) return 1;
    return 0;
}

static int entry_exists(const orset_t *s, orset_entry_t e) {
    for (int i = 0; i < s->add_count; i++)
        if (tag_eq(s->added[i].tag, e.tag)) return 1; // tag is globally unique
    return 0;
}

void orset_init(orset_t *s, int my_id, int num_nodes) {
    memset(s, 0, sizeof(*s));
    s->my_id = my_id;
    s->num_nodes = num_nodes;
}

void orset_add(orset_t *s, const char *elem) {
    if (s->add_count >= MAX_TAGS) return; // bounds guard
    s->seq_counters[s->my_id]++;
    orset_entry_t e;
    memset(&e, 0, sizeof(e));
    snprintf(e.elem, MAX_ELEM_LEN, "%s", elem);
    e.tag.node_id = (uint32_t)s->my_id;
    e.tag.seq     = s->seq_counters[s->my_id];
    s->added[s->add_count++] = e;
}

void orset_remove(orset_t *s, const char *elem) {
    // Tombstone every currently-visible add-tag for this element.
    for (int i = 0; i < s->add_count; i++) {
        if (strcmp(s->added[i].elem, elem) == 0 && !tag_is_removed(s, s->added[i].tag)) {
            if (s->remove_count >= MAX_TAGS) return; // bounds guard
            s->removed[s->remove_count++] = s->added[i].tag;
        }
    }
}

int orset_contains(const orset_t *s, const char *elem) {
    for (int i = 0; i < s->add_count; i++)
        if (strcmp(s->added[i].elem, elem) == 0 && !tag_is_removed(s, s->added[i].tag))
            return 1;
    return 0;
}

void orset_list(const orset_t *s, char out[][MAX_ELEM_LEN], int *count, int max) {
    int n = 0;
    for (int i = 0; i < s->add_count; i++) {
        if (tag_is_removed(s, s->added[i].tag)) continue;
        // skip duplicates already emitted (same elem via a different live tag)
        int seen = 0;
        for (int j = 0; j < n; j++)
            if (strcmp(out[j], s->added[i].elem) == 0) { seen = 1; break; }
        if (seen) continue;
        if (n >= max) break;
        snprintf(out[n++], MAX_ELEM_LEN, "%s", s->added[i].elem);
    }
    *count = n;
}

void orset_merge(orset_t *dst, const orset_t *src) {
    // Union the added entries (dedup by globally-unique tag).
    for (int i = 0; i < src->add_count; i++) {
        if (!entry_exists(dst, src->added[i])) {
            if (dst->add_count >= MAX_TAGS) break;
            dst->added[dst->add_count++] = src->added[i];
        }
    }
    // Union the removed tags (dedup).
    for (int i = 0; i < src->remove_count; i++) {
        if (!tag_is_removed(dst, src->removed[i])) {
            if (dst->remove_count >= MAX_TAGS) break;
            dst->removed[dst->remove_count++] = src->removed[i];
        }
    }
    // seq_counters -> element-wise max so future tags stay unique.
    for (int i = 0; i < MAX_NODES; i++)
        if (src->seq_counters[i] > dst->seq_counters[i])
            dst->seq_counters[i] = src->seq_counters[i];
}

// node.c — node state aggregation, serialization, and CLI dispatch.
//
// Serialization uses explicit big-endian field encoding (NOT raw struct
// memcpy): we send only merge-relevant data and never a peer's my_id/num_nodes,
// keeping the wire format portable and the merge correct.
#include "node.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ---- little serialization helpers (big-endian) ----------------------------

static void put_u32(uint8_t **p, uint32_t v) {
    (*p)[0] = (v >> 24) & 0xff; (*p)[1] = (v >> 16) & 0xff;
    (*p)[2] = (v >> 8) & 0xff;  (*p)[3] = v & 0xff;
    *p += 4;
}
static void put_u64(uint8_t **p, uint64_t v) {
    for (int i = 7; i >= 0; i--) { (*p)[7 - i] = (v >> (i * 8)) & 0xff; }
    *p += 8;
}
static uint32_t get_u32(const uint8_t **p) {
    uint32_t v = ((uint32_t)(*p)[0] << 24) | ((uint32_t)(*p)[1] << 16) |
                 ((uint32_t)(*p)[2] << 8)  | (uint32_t)(*p)[3];
    *p += 4; return v;
}
static uint64_t get_u64(const uint8_t **p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | (*p)[i];
    *p += 8; return v;
}

// ---- init -----------------------------------------------------------------

void node_init(node_t *n, int id, int num_nodes, int port) {
    memset(n, 0, sizeof(*n));
    n->id = id;
    n->num_nodes = num_nodes;
    n->port = port;
    vclock_init(&n->clock, id, num_nodes);
    pthread_mutex_init(&n->lock, NULL);
}

void node_add_peer(node_t *n, const char *ip, int port) {
    if (n->peer_count >= MAX_NODES) return;
    peer_t *p = &n->peers[n->peer_count++];
    snprintf(p->ip, sizeof(p->ip), "%s", ip);
    p->port = port;
    p->out_fd = -1;
    p->alive = 0;
    p->last_seen = 0;
}

// ---- per-type find-or-create (lock must be held by caller) -----------------

static gcounter_t *gc_get(node_t *n, const char *key) {
    for (int i = 0; i < n->gc_count; i++)
        if (strncmp(n->gcounters[i].key, key, MAX_KEY) == 0) return &n->gcounters[i].gc;
    if (n->gc_count >= MAX_KEYS) return NULL;
    gc_entry_t *e = &n->gcounters[n->gc_count++];
    snprintf(e->key, MAX_KEY, "%s", key);
    gcounter_init(&e->gc, n->id, n->num_nodes);
    return &e->gc;
}
static pncounter_t *pn_get(node_t *n, const char *key) {
    for (int i = 0; i < n->pn_count; i++)
        if (strncmp(n->pncounters[i].key, key, MAX_KEY) == 0) return &n->pncounters[i].pc;
    if (n->pn_count >= MAX_KEYS) return NULL;
    pn_entry_t *e = &n->pncounters[n->pn_count++];
    snprintf(e->key, MAX_KEY, "%s", key);
    pncounter_init(&e->pc, n->id, n->num_nodes);
    return &e->pc;
}
static lww_register_t *reg_get(node_t *n, const char *key) {
    for (int i = 0; i < n->reg_count; i++)
        if (strncmp(n->registers[i].key, key, MAX_KEY) == 0) return &n->registers[i].reg;
    if (n->reg_count >= MAX_KEYS) return NULL;
    reg_entry_t *e = &n->registers[n->reg_count++];
    snprintf(e->key, MAX_KEY, "%s", key);
    lww_register_init(&e->reg, n->id, n->num_nodes);
    return &e->reg;
}
static orset_t *set_get(node_t *n, const char *key) {
    for (int i = 0; i < n->set_count; i++)
        if (strncmp(n->sets[i].key, key, MAX_KEY) == 0) return &n->sets[i].set;
    if (n->set_count >= MAX_KEYS) return NULL;
    set_entry_t *e = &n->sets[n->set_count++];
    snprintf(e->key, MAX_KEY, "%s", key);
    orset_init(&e->set, n->id, n->num_nodes);
    return &e->set;
}

// ---- G-Counter convenience (locking; used by tests) ------------------------

void node_gcounter_inc(node_t *n, const char *key) {
    pthread_mutex_lock(&n->lock);
    gcounter_t *gc = gc_get(n, key);
    if (gc) gcounter_increment(gc);
    pthread_mutex_unlock(&n->lock);
}
uint64_t node_gcounter_get(node_t *n, const char *key) {
    pthread_mutex_lock(&n->lock);
    gcounter_t *gc = gc_get(n, key);
    uint64_t v = gc ? gcounter_value(gc) : 0;
    pthread_mutex_unlock(&n->lock);
    return v;
}

// ---- write path: log-before-apply (lock held by caller) -------------------
// apply_record mutates memory only; do_write logs durably first, then applies.
// Replay uses apply_record directly (no re-logging).

static void apply_record(node_t *n, const wal_record_t *r) {
    switch (r->crdt_type) {
    case WAL_GCOUNTER: {
        gcounter_t *g = gc_get(n, r->key);
        if (g) gcounter_increment(g);
        break;
    }
    case WAL_PNCOUNTER: {
        pncounter_t *p = pn_get(n, r->key);
        if (p) { if (r->op_type == WAL_OP_DEC) pncounter_decrement(p);
                 else                            pncounter_increment(p); }
        break;
    }
    case WAL_LWW: {
        lww_register_t *g = reg_get(n, r->key);
        if (g) lww_register_set(g, r->payload, &n->clock);
        break;
    }
    case WAL_ORSET: {
        orset_t *s = set_get(n, r->key);
        if (s) { if (r->op_type == WAL_OP_RM) orset_remove(s, r->payload);
                 else                          orset_add(s, r->payload); }
        break;
    }
    default: break;
    }
}

static void do_write(node_t *n, uint8_t crdt, uint8_t op,
                     const char *key, const char *payload) {
    wal_record_t r;
    memset(&r, 0, sizeof(r));
    r.crdt_type = crdt;
    r.op_type   = op;
    snprintf(r.key, MAX_KEY, "%s", key);
    if (payload)
        r.payload_len = (uint16_t)snprintf(r.payload, sizeof(r.payload), "%s", payload);
    if (n->wal) wal_append(n->wal, &r); // durable on disk before we touch memory
    apply_record(n, &r);
}

void node_set_wal(node_t *n, wal_t *w) {
    pthread_mutex_lock(&n->lock);
    n->wal = w;
    pthread_mutex_unlock(&n->lock);
}

void node_apply_record(node_t *n, const wal_record_t *r) {
    pthread_mutex_lock(&n->lock);
    apply_record(n, r);
    pthread_mutex_unlock(&n->lock);
}

static void replay_cb(void *ctx, const wal_record_t *r) {
    node_apply_record((node_t *)ctx, r);
}

long node_replay(node_t *n, const char *path) {
    return wal_replay(path, replay_cb, n);
}

// ---- serialization --------------------------------------------------------

size_t node_serialize(node_t *n, uint8_t *buf, size_t cap) {
    pthread_mutex_lock(&n->lock);
    uint8_t *p = buf;
    uint8_t *end = buf + cap;

    // Capacity check up front, sized to actual contents.
    size_t need =
        1 + 4 + (size_t)n->gc_count * (MAX_KEY + MAX_NODES * 8)
              + 4 + (size_t)n->pn_count * (MAX_KEY + 2 * MAX_NODES * 8)
              + 4 + (size_t)n->reg_count * (MAX_KEY + MAX_VALUE_LEN + 4 + 1 + MAX_NODES * 8)
              + 4;
    for (int i = 0; i < n->set_count; i++) {
        orset_t *s = &n->sets[i].set;
        need += MAX_KEY + 4 + (size_t)s->add_count * (MAX_ELEM_LEN + 12)
                        + 4 + (size_t)s->remove_count * 12 + MAX_NODES * 8;
    }
    if (need > cap) { pthread_mutex_unlock(&n->lock); return 0; }
    (void)end;

    *p++ = (uint8_t)n->id;

    // G-Counters
    put_u32(&p, (uint32_t)n->gc_count);
    for (int i = 0; i < n->gc_count; i++) {
        memcpy(p, n->gcounters[i].key, MAX_KEY); p += MAX_KEY;
        for (int s = 0; s < MAX_NODES; s++) put_u64(&p, n->gcounters[i].gc.counts[s]);
    }
    // PN-Counters
    put_u32(&p, (uint32_t)n->pn_count);
    for (int i = 0; i < n->pn_count; i++) {
        memcpy(p, n->pncounters[i].key, MAX_KEY); p += MAX_KEY;
        for (int s = 0; s < MAX_NODES; s++) put_u64(&p, n->pncounters[i].pc.P.counts[s]);
        for (int s = 0; s < MAX_NODES; s++) put_u64(&p, n->pncounters[i].pc.N.counts[s]);
    }
    // LWW Registers
    put_u32(&p, (uint32_t)n->reg_count);
    for (int i = 0; i < n->reg_count; i++) {
        lww_register_t *r = &n->registers[i].reg;
        memcpy(p, n->registers[i].key, MAX_KEY); p += MAX_KEY;
        memcpy(p, r->value, MAX_VALUE_LEN); p += MAX_VALUE_LEN;
        put_u32(&p, (uint32_t)r->writer_id);
        *p++ = (uint8_t)r->has_value;
        for (int s = 0; s < MAX_NODES; s++) put_u64(&p, r->timestamp.ticks[s]);
    }
    // OR-Sets
    put_u32(&p, (uint32_t)n->set_count);
    for (int i = 0; i < n->set_count; i++) {
        orset_t *s = &n->sets[i].set;
        memcpy(p, n->sets[i].key, MAX_KEY); p += MAX_KEY;
        put_u32(&p, (uint32_t)s->add_count);
        for (int a = 0; a < s->add_count; a++) {
            memcpy(p, s->added[a].elem, MAX_ELEM_LEN); p += MAX_ELEM_LEN;
            put_u32(&p, s->added[a].tag.node_id);
            put_u64(&p, s->added[a].tag.seq);
        }
        put_u32(&p, (uint32_t)s->remove_count);
        for (int r = 0; r < s->remove_count; r++) {
            put_u32(&p, s->removed[r].node_id);
            put_u64(&p, s->removed[r].seq);
        }
        for (int s2 = 0; s2 < MAX_NODES; s2++) put_u64(&p, s->seq_counters[s2]);
    }

    pthread_mutex_unlock(&n->lock);
    return (size_t)(p - buf);
}

void node_deserialize_merge(node_t *n, const uint8_t *buf, size_t len) {
    if (len < 1) return;
    const uint8_t *p = buf;
    p++; // sender id

    pthread_mutex_lock(&n->lock);

    // G-Counters
    uint32_t gc_n = get_u32(&p);
    for (uint32_t i = 0; i < gc_n; i++) {
        char key[MAX_KEY]; memcpy(key, p, MAX_KEY); p += MAX_KEY; key[MAX_KEY - 1] = 0;
        gcounter_t in; gcounter_init(&in, n->id, n->num_nodes);
        for (int s = 0; s < MAX_NODES; s++) in.counts[s] = get_u64(&p);
        gcounter_t *loc = gc_get(n, key);
        if (loc) gcounter_merge(loc, &in);
    }
    // PN-Counters
    uint32_t pn_n = get_u32(&p);
    for (uint32_t i = 0; i < pn_n; i++) {
        char key[MAX_KEY]; memcpy(key, p, MAX_KEY); p += MAX_KEY; key[MAX_KEY - 1] = 0;
        pncounter_t in; pncounter_init(&in, n->id, n->num_nodes);
        for (int s = 0; s < MAX_NODES; s++) in.P.counts[s] = get_u64(&p);
        for (int s = 0; s < MAX_NODES; s++) in.N.counts[s] = get_u64(&p);
        pncounter_t *loc = pn_get(n, key);
        if (loc) pncounter_merge(loc, &in);
    }
    // LWW Registers
    uint32_t reg_n = get_u32(&p);
    for (uint32_t i = 0; i < reg_n; i++) {
        char key[MAX_KEY]; memcpy(key, p, MAX_KEY); p += MAX_KEY; key[MAX_KEY - 1] = 0;
        lww_register_t in; lww_register_init(&in, n->id, n->num_nodes);
        memcpy(in.value, p, MAX_VALUE_LEN); p += MAX_VALUE_LEN;
        in.value[MAX_VALUE_LEN - 1] = 0;
        in.writer_id = (int)get_u32(&p);
        in.has_value = *p++;
        for (int s = 0; s < MAX_NODES; s++) in.timestamp.ticks[s] = get_u64(&p);
        lww_register_t *loc = reg_get(n, key);
        if (loc) lww_register_merge(loc, &in);
        // Advance node clock so future local writes are causally after what we saw.
        vclock_merge(&n->clock, &in.timestamp);
    }
    // OR-Sets
    uint32_t set_n = get_u32(&p);
    for (uint32_t i = 0; i < set_n; i++) {
        char key[MAX_KEY]; memcpy(key, p, MAX_KEY); p += MAX_KEY; key[MAX_KEY - 1] = 0;
        orset_t in; orset_init(&in, n->id, n->num_nodes);
        uint32_t ac = get_u32(&p);
        for (uint32_t a = 0; a < ac && in.add_count < MAX_TAGS; a++) {
            orset_entry_t e; memset(&e, 0, sizeof(e));
            memcpy(e.elem, p, MAX_ELEM_LEN); p += MAX_ELEM_LEN; e.elem[MAX_ELEM_LEN - 1] = 0;
            e.tag.node_id = get_u32(&p);
            e.tag.seq     = get_u64(&p);
            in.added[in.add_count++] = e;
        }
        uint32_t rc = get_u32(&p);
        for (uint32_t r = 0; r < rc && in.remove_count < MAX_TAGS; r++) {
            tag_t t; t.node_id = get_u32(&p); t.seq = get_u64(&p);
            in.removed[in.remove_count++] = t;
        }
        for (int s = 0; s < MAX_NODES; s++) in.seq_counters[s] = get_u64(&p);
        orset_t *loc = set_get(n, key);
        if (loc) orset_merge(loc, &in);
    }

    pthread_mutex_unlock(&n->lock);
}

// ---- CLI dispatch ---------------------------------------------------------
// Acquires the lock once and uses the non-locking *_get helpers throughout.

void node_handle_cli(node_t *n, const char *cmd, char *resp, size_t resp_cap) {
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", cmd);

    char *type = strtok(buf, " ");
    char *op   = strtok(NULL, " ");
    char *key  = strtok(NULL, " ");
    char *rest = strtok(NULL, "");   // remainder of line (value / element)

    snprintf(resp, resp_cap, "ERR unknown command");
    if (!type) return;

    pthread_mutex_lock(&n->lock);

    if (strcmp(type, "gctr") == 0 && op && key) {
        if (strcmp(op, "inc") == 0) { do_write(n, WAL_GCOUNTER, WAL_OP_INC, key, NULL); snprintf(resp, resp_cap, "OK"); }
        else if (strcmp(op, "get") == 0) { gcounter_t *gc = gc_get(n, key); snprintf(resp, resp_cap, "%llu", gc ? (unsigned long long)gcounter_value(gc) : 0); }

    } else if (strcmp(type, "pctr") == 0 && op && key) {
        if (strcmp(op, "inc") == 0) { do_write(n, WAL_PNCOUNTER, WAL_OP_INC, key, NULL); snprintf(resp, resp_cap, "OK"); }
        else if (strcmp(op, "dec") == 0) { do_write(n, WAL_PNCOUNTER, WAL_OP_DEC, key, NULL); snprintf(resp, resp_cap, "OK"); }
        else if (strcmp(op, "get") == 0) { pncounter_t *pc = pn_get(n, key); snprintf(resp, resp_cap, "%lld", pc ? (long long)pncounter_value(pc) : 0); }

    } else if (strcmp(type, "reg") == 0 && op && key) {
        if (strcmp(op, "set") == 0 && rest) { do_write(n, WAL_LWW, WAL_OP_SET, key, rest); snprintf(resp, resp_cap, "OK"); }
        else if (strcmp(op, "get") == 0) { lww_register_t *r = reg_get(n, key); char out[MAX_VALUE_LEN]; if (r) { lww_register_get(r, out, sizeof(out)); snprintf(resp, resp_cap, "%s", out); } }

    } else if (strcmp(type, "set") == 0 && op && key) {
        orset_t *s = set_get(n, key);
        if (strcmp(op, "add") == 0 && rest) { do_write(n, WAL_ORSET, WAL_OP_ADD, key, rest); snprintf(resp, resp_cap, "OK"); }
        else if (strcmp(op, "rm") == 0 && rest) { do_write(n, WAL_ORSET, WAL_OP_RM, key, rest); snprintf(resp, resp_cap, "OK"); }
        else if (s && strcmp(op, "list") == 0) {
            char items[256][MAX_ELEM_LEN]; int cnt = 0;
            orset_list(s, items, &cnt, 256);
            size_t off = 0; resp[0] = '\0';
            for (int i = 0; i < cnt; i++)
                off += (size_t)snprintf(resp + off, off < resp_cap ? resp_cap - off : 0,
                                        "%s%s", i ? " " : "", items[i]);
            if (cnt == 0) snprintf(resp, resp_cap, "(empty)");
        }

    } else if (strcmp(type, "admin") == 0 && op) {
        if (strcmp(op, "partition") == 0 && key) {
            n->partitioned = (strcmp(key, "on") == 0);
            snprintf(resp, resp_cap, "partition %s", n->partitioned ? "on" : "off");
        }
    }

    pthread_mutex_unlock(&n->lock);
}

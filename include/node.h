// node.h — aggregates all CRDT state for one node; gossip + CLI dispatch.
#ifndef CONVERGENCE_NODE_H
#define CONVERGENCE_NODE_H

#include "common.h"
#include "vclock.h"
#include "gcounter.h"
#include "pncounter.h"
#include "lww_register.h"
#include "orset.h"
#include "wal.h"
#include <pthread.h>
#include <time.h>

// Scratch buffer size for a serialized state snapshot. Generous for the demo.
#define STATE_BUF_CAP (1u << 22)

typedef struct { char key[MAX_KEY]; gcounter_t     gc;  } gc_entry_t;
typedef struct { char key[MAX_KEY]; pncounter_t    pc;  } pn_entry_t;
typedef struct { char key[MAX_KEY]; lww_register_t reg; } reg_entry_t;
typedef struct { char key[MAX_KEY]; orset_t        set; } set_entry_t;

typedef struct {
    char     ip[64];
    int      port;
    int      out_fd;       // outbound connection we dial (gossip send), -1 if down
    int      alive;        // heartbeat-based liveness
    time_t   last_seen;
} peer_t;

typedef struct {
    int  id;
    int  num_nodes;
    int  port;

    vclock_t clock;        // node-level clock, used to stamp LWW writes

    gc_entry_t  gcounters[MAX_KEYS];  int gc_count;
    pn_entry_t  pncounters[MAX_KEYS]; int pn_count;
    reg_entry_t registers[MAX_KEYS];  int reg_count;
    set_entry_t sets[MAX_KEYS];       int set_count;

    peer_t peers[MAX_NODES];
    int    peer_count;

    int partitioned;      // when set: drop outbound gossip and incoming state sync

    wal_t *wal;           // optional write-ahead log (NULL = in-memory only)

    pthread_mutex_t lock; // guards all of the above
} node_t;

void node_init(node_t *n, int id, int num_nodes, int port);
void node_add_peer(node_t *n, const char *ip, int port);

// CLI command handling: parse `cmd`, mutate state, write a response into `resp`.
void node_handle_cli(node_t *n, const char *cmd, char *resp, size_t resp_cap);

// Serialize/merge full state for gossip (explicit field encoding, §7).
size_t node_serialize(node_t *n, uint8_t *buf, size_t cap);
void   node_deserialize_merge(node_t *n, const uint8_t *buf, size_t len);

// G-Counter convenience used by tests (take the lock internally).
void     node_gcounter_inc(node_t *n, const char *key);
uint64_t node_gcounter_get(node_t *n, const char *key);

// WAL wiring.
void node_set_wal(node_t *n, wal_t *w);                       // attach a log
void node_apply_record(node_t *n, const wal_record_t *r);    // apply, no logging
long node_replay(node_t *n, const char *path);              // replay WAL at startup

#endif

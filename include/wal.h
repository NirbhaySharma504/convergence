// wal.h — write-ahead log: durability across restarts (§9 Phase 5).
#ifndef CONVERGENCE_WAL_H
#define CONVERGENCE_WAL_H

#include "common.h"

// CRDT type tags stored in each record.
enum { WAL_GCOUNTER = 0, WAL_PNCOUNTER = 1, WAL_LWW = 2, WAL_ORSET = 3 };
// Operation tags.
enum { WAL_OP_INC = 0, WAL_OP_DEC = 1, WAL_OP_SET = 2, WAL_OP_ADD = 3, WAL_OP_RM = 4 };

typedef struct {
    uint8_t  crdt_type;          // WAL_GCOUNTER ..
    uint8_t  op_type;            // WAL_OP_INC ..
    char     key[MAX_KEY];       // CRDT key
    char     payload[512];       // operation argument (value/element); may be empty
    uint16_t payload_len;
} wal_record_t;

typedef struct wal wal_t;

wal_t *wal_open  (const char *path);          // O_RDWR|O_CREAT|O_APPEND
int    wal_append(wal_t *w, const wal_record_t *r); // write then fsync(); 0 on success
void   wal_close (wal_t *w);

// Replay every record in order, invoking apply() for each. Used at startup
// before threads start. Returns number of records replayed, or -1 on error.
typedef void (*wal_apply_fn)(void *ctx, const wal_record_t *r);
long   wal_replay(const char *path, wal_apply_fn apply, void *ctx);

#endif

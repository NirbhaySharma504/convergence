// common.h — shared constants and wire-protocol message types.
#ifndef CONVERGENCE_COMMON_H
#define CONVERGENCE_COMMON_H

#include <stdint.h>
#include <stddef.h>

#define MAX_NODES   16    // fixed cluster size, baked into every CRDT slot array
#define MAX_KEY     64    // max key length (incl. NUL)
#define MAX_KEYS    256   // max distinct keys per CRDT type per node

// Wire protocol message types (see §7 of the design doc).
#define MSG_HEARTBEAT  0x01  // payload: [1 byte sender id]
#define MSG_STATE_SYNC 0x02  // payload: full serialized node state
#define MSG_SYNC_ACK   0x03  // payload: [1 byte sender id]
#define MSG_CLI_CMD    0x04  // payload: NUL-terminated command string
#define MSG_CLI_RESP   0x05  // payload: NUL-terminated response string

#define GOSSIP_INTERVAL_SEC 2
#define PEER_DEAD_SEC       6

#endif

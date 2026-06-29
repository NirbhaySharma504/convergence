# Convergence — Engineering Design Document

> A distributed key-value store where nodes sync conflict-free after network partitions.  
> Written in C. No external dependencies. No central coordinator. Mathematically guaranteed convergence.

---

## Table of Contents

1. [Project Summary](#1-project-summary)
2. [Functional Requirements](#2-functional-requirements)
3. [Non-Functional Requirements](#3-non-functional-requirements)
4. [System Architecture](#4-system-architecture)
5. [Tech Stack & Rationale](#5-tech-stack--rationale)
6. [Data Structure Design](#6-data-structure-design)
7. [Wire Protocol Design](#7-wire-protocol-design)
8. [Repository Structure](#8-repository-structure)
9. [Phase-by-Phase Implementation Plan](#9-phase-by-phase-implementation-plan)
10. [Testing Strategy](#10-testing-strategy)
11. [Benchmark Targets](#11-benchmark-targets)
12. [Demo Script](#12-demo-script)
13. [Interview Preparation](#13-interview-preparation)
14. [Hour Estimates](#14-hour-estimates)

---

## 1. Project Summary

### What You Are Building

**Convergence** is a distributed key-value store written in C where multiple nodes can go offline, independently accept writes, reconnect to each other, and arrive at an identical state — with no central coordinator, no conflict, and no data loss.

This is achieved by representing all stored state as **Conflict-free Replicated Data Types (CRDTs)** — data structures with mathematically proven merge semantics. Two nodes can independently modify a CRDT and, when they reconnect, merge their states into a single correct result regardless of the order operations were applied.

### The Core Demo (30 seconds, wins interviews)

```
1. Start Node A and Node B on the same machine (different ports).
2. Simulate a network partition — A and B can no longer talk.
3. Write to both nodes independently: increment a counter on A, add a tag on B.
4. Heal the partition.
5. Both nodes converge to the same state within one gossip cycle (~2 seconds).
```

This single demo surfaces: network partitions, CAP theorem, eventual consistency, vector clocks, and conflict-free merging — the exact topics every company on your list asks about in system design rounds.

### Why This Is Resume-Worthy

- Sits at the intersection of systems programming (C, sockets, WAL) and distributed theory (CAP, CRDTs, vector clocks)
- Pure Storage, Rubrik, Cohesity, Netapp all work on distributed storage — this is their domain
- Google, Amazon, Flipkart, Swiggy ask CAP + eventual consistency in every system design round
- Natural "chapter 2" from your existing database project: first you stored data, now you replicated it
- Almost no campus candidate has this — it is genuinely uncommon

### Resume Line

> "Built a distributed key-value store in C where nodes sync conflict-free after network partitions — implementing G-Counter, PN-Counter, LWW Register, and OR-Set CRDTs with vector clock causality tracking and a write-ahead log for crash durability."

---

## 2. Functional Requirements

### FR-1: CRDT Data Types

The store supports four CRDT types, each accessible by a string key:

| Type | Commands | Behaviour |
|------|----------|-----------|
| **G-Counter** | `gctr inc <key>` / `gctr get <key>` | Increment-only counter. Merge = element-wise max per node slot. |
| **PN-Counter** | `pctr inc <key>` / `pctr dec <key>` / `pctr get <key>` | Counter supporting increment and decrement. Built on two G-Counters (P and N). Value = P − N. |
| **LWW Register** | `reg set <key> <val>` / `reg get <key>` | Last-writer-wins register. Merge uses vector clock ordering, not wall time. Concurrent writes break ties by node ID. |
| **OR-Set** | `set add <key> <elem>` / `set rm <key> <elem>` / `set list <key>` | Set with add-wins semantics. Each add generates a unique tag. Concurrent add and remove on the same element — the element stays. |

### FR-2: Node Configuration

- Each node has a unique integer ID assigned at startup: `--id 0`
- Each node listens for peer connections and CLI commands on a configurable TCP port: `--port 7000`
- Peers are declared at startup: `--peer 127.0.0.1:7001`
- All state is held in memory. A write-ahead log on disk ensures durability across restarts.

### FR-3: Sync Protocol

- Every 2 seconds, each node sends its full serialized CRDT state to all connected peers (gossip / anti-entropy).
- On receiving a peer's state, the node merges it with its own. Merge must be commutative, associative, and idempotent.
- A heartbeat is sent every 2 seconds alongside gossip. If no heartbeat is received from a peer within 6 seconds, the peer is marked offline. On reconnect, an immediate full-state sync is triggered.

### FR-4: Offline Durability

- A node that loses peer connectivity continues accepting local writes normally.
- All writes are appended to the WAL and fsynced to disk before being applied to in-memory state.
- On peer reconnect, the next gossip cycle carries the full state (including all writes made during the partition). No separate "replay to peer" mechanism is needed — CRDT merge handles it.
- On node restart (crash or deliberate), the WAL is replayed to restore in-memory state before accepting connections.

### FR-5: CLI Interface

A separate `convergence-cli` binary sends commands to a running node over TCP:

```bash
./convergence-cli --port 7000 gctr inc page_views
./convergence-cli --port 7000 gctr get page_views
./convergence-cli --port 7000 reg set username alice
./convergence-cli --port 7000 reg get username
./convergence-cli --port 7000 set add tags "distributed"
./convergence-cli --port 7000 set add tags "systems"
./convergence-cli --port 7000 set rm  tags "distributed"
./convergence-cli --port 7000 set list tags
```

---

## 3. Non-Functional Requirements

| Requirement | Target | Why It Matters |
|-------------|--------|----------------|
| Write throughput | ≥ 50,000 ops/sec (single node) | Credible benchmark for a systems project |
| Sync latency after partition | < 3 seconds | One gossip cycle + margin |
| Memory for 100,000 keys | < 50 MB | Shows you care about footprint |
| Implementation language | C (C11, gcc) | Ties to your LFX and personal DB — consistent, not trend-chasing |
| External dependencies | None (POSIX only) | Stronger signal than wrapping a library |
| Test coverage | Every CRDT merge has at least one commutativity test, one idempotency test, and one real-scenario test | Signals engineering maturity |
| Build | `make` builds everything; `make test` runs all tests | Recruiter should be able to clone and run in 30 seconds |
| Demo | `make demo` runs the full partition-heal-converge scenario | Shows confidence in correctness |

---

## 4. System Architecture

### High-Level Diagram

```
┌────────────────────────────────────────────┐     ┌────────────────────────────────────────────┐
│                  Node A                    │     │                  Node B                    │
│                                            │     │                                            │
│  ┌────────────────┐   ┌─────────────────┐  │     │  ┌────────────────┐   ┌─────────────────┐  │
│  │   CLI Handler  │   │  Gossip Thread  │  │     │  │   CLI Handler  │   │  Gossip Thread  │  │
│  │  (TCP server)  │   │  (every 2s)     │  │     │  │  (TCP server)  │   │  (every 2s)     │  │
│  └───────┬────────┘   └────────┬────────┘  │     │  └───────┬────────┘   └────────┬────────┘  │
│          │                     │            │     │          │                     │            │
│          └──────────┬──────────┘            │     │          └──────────┬──────────┘            │
│                     ▼                       │     │                     ▼                       │
│         ┌───────────────────────────────┐   │     │         ┌───────────────────────────────┐   │
│         │         Node State            │   │     │         │         Node State            │   │
│         │  ┌──────────┬──────────────┐  │   │     │         │  ┌──────────┬──────────────┐  │   │
│         │  │ G-Counter│  PN-Counter  │  │   │◄───►│         │  │ G-Counter│  PN-Counter  │  │   │
│         │  ├──────────┴──────────────┤  │   │     │         │  ├──────────┴──────────────┤  │   │
│         │  │ LWW Reg  │  OR-Set      │  │   │     │         │  │ LWW Reg  │  OR-Set      │  │   │
│         │  ├──────────┴──────────────┤  │   │     │         │  ├──────────┴──────────────┤  │   │
│         │  │      Vector Clock       │  │   │     │         │  │      Vector Clock       │  │   │
│         │  └─────────────────────────┘  │   │     │         │  └─────────────────────────┘  │   │
│         │  pthread_mutex_t node_lock    │   │     │         │  pthread_mutex_t node_lock    │   │
│         └───────────────┬───────────────┘   │     │         └───────────────┬───────────────┘   │
│                         ▼                   │     │                         ▼                   │
│         ┌───────────────────────────────┐   │     │         ┌───────────────────────────────┐   │
│         │      Write-Ahead Log          │   │     │         │      Write-Ahead Log          │   │
│         │   node_0.wal  (disk)          │   │     │         │   node_1.wal  (disk)          │   │
│         └───────────────────────────────┘   │     │         └───────────────────────────────┘   │
└────────────────────────────────────────────┘     └────────────────────────────────────────────┘
                       TCP — full state exchange every 2 seconds (gossip)
```

### Thread Model

Each running node has exactly three threads:

| Thread | Purpose |
|--------|---------|
| **Main thread** | Startup, WAL replay, argument parsing |
| **Listener thread** | TCP accept loop — accepts peer connections and CLI connections |
| **Gossip thread** | Every 2 seconds: send full state to all peers, send heartbeat, check for dead peers |

All access to node state is protected by a single `pthread_mutex_t node_lock`. Lock before any read or write. Unlock immediately after. Keep critical sections short.

### Component Responsibilities

| Component | Files | Responsibility |
|-----------|-------|----------------|
| Vector Clock | `vclock.c / .h` | Causality tracking. Tick on write. Merge with peer clocks. Compare for ordering. |
| G-Counter | `src/crdt/gcounter.c / .h` | Grow-only counter. One slot per node. Merge = element-wise max. |
| PN-Counter | `src/crdt/pncounter.c / .h` | Inc/dec counter. Two G-Counters internally. |
| LWW Register | `src/crdt/lww_register.c / .h` | Last-write-wins. Ordered by vector clock. Tie-break by node ID. |
| OR-Set | `src/crdt/orset.c / .h` | Add-wins set. Unique per-add tags. Tombstone on remove. |
| Networking | `net.c / .h` | TCP server, client connect, framed message send/recv, gossip loop, heartbeat. |
| WAL | `wal.c / .h` | Append-before-apply. fsync on every write. Replay on startup. |
| Node | `node.c / .h` | Aggregates all CRDT state. Serialise/deserialise full state for gossip. |
| CLI | `cli.c` | Separate binary. Connects to a running node. Sends command, prints response. |
| Main | `main.c` | Entry point. Parse args. Replay WAL. Start threads. |

---

## 5. Tech Stack & Rationale

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Language | C (C11, gcc) | Consistent with your LFX kernel work and personal DB project. Strongest possible systems signal. No abstraction hiding what's happening. |
| Networking | POSIX sockets (`sys/socket.h`) | No library. You understand every byte on the wire. This is what interviewers expect from someone with your background. |
| Concurrency | pthreads + mutex | Standard POSIX. One mutex protecting all node state is correct and simple. Do not prematurely optimise with fine-grained locking. |
| Persistence | Custom binary WAL | Same pattern as PostgreSQL and SQLite. You can explain exactly what's on disk. No library dependency. |
| Key storage | Open-addressing hashmap (implement yourself) | ~100 lines of C. Shows you can. For MVP, start with a fixed-size array of structs and upgrade to hashmap in v2. |
| Testing | Unity (single-header C test framework) | Drop in `unity.h` and `unity.c`. No installation needed. Lightweight. |
| Build | Makefile | `make`, `make test`, `make demo`. One command each. Recruiter-friendly. |
| Demo tooling | iptables or SIGSTOP | iptables for realistic partition simulation. SIGSTOP is simpler for a quick demo. Both work. |

**Why not Rust?**  
Rust would be a valid choice and would look good. However, given your existing C background (LFX, personal DB), C is more consistent and you can go deeper faster. If you have remaining time after finishing the project, writing a Rust port of just the CRDT types is a strong bonus — you can mention it in interviews.

---

## 6. Data Structure Design

Read this section carefully. Every field has a reason. You must be able to explain each one.

### 6.1 Vector Clock

```c
// include/vclock.h

#define MAX_NODES 16

typedef struct {
    uint64_t ticks[MAX_NODES]; // ticks[i] = number of events seen from node i
    int      num_nodes;        // how many nodes are in the system (set at startup)
    int      my_id;            // this node's index into ticks[]
} vclock_t;

typedef enum {
    VC_BEFORE,      // a happened-before b
    VC_AFTER,       // a happened-after b
    VC_EQUAL,       // identical clocks
    VC_CONCURRENT   // neither is before the other
} vc_order_t;

void       vclock_init    (vclock_t *vc, int my_id, int num_nodes);
void       vclock_tick    (vclock_t *vc);                            // increment ticks[my_id]
void       vclock_merge   (vclock_t *dst, const vclock_t *src);     // element-wise max
vc_order_t vclock_compare (const vclock_t *a, const vclock_t *b);   // return ordering
void       vclock_copy    (vclock_t *dst, const vclock_t *src);
```

**`vclock_compare` logic — implement exactly this:**

```
a < b (BEFORE) if:
  for all i: a.ticks[i] <= b.ticks[i]
  AND at least one i where a.ticks[i] < b.ticks[i]

a > b (AFTER) if:
  for all i: a.ticks[i] >= b.ticks[i]
  AND at least one i where a.ticks[i] > b.ticks[i]

a == b (EQUAL) if:
  for all i: a.ticks[i] == b.ticks[i]

CONCURRENT otherwise (neither dominates)
```

**Interview explanation:** After node B receives node A's state and merges A's clock, B's clock is causally after A's. This means any write B makes after that merge is provably later than everything A had done. This is what lets LWW Register pick the correct winner without any clock synchronisation.

### 6.2 G-Counter

```c
// include/gcounter.h

typedef struct {
    uint64_t counts[MAX_NODES]; // counts[i] = total increments from node i
    int      num_nodes;
    int      my_id;
} gcounter_t;

void     gcounter_init      (gcounter_t *gc, int my_id, int num_nodes);
void     gcounter_increment (gcounter_t *gc);                          // counts[my_id]++
uint64_t gcounter_value     (const gcounter_t *gc);                    // sum of all counts[i]
void     gcounter_merge     (gcounter_t *dst, const gcounter_t *src);  // element-wise max
```

**Why element-wise max works:** Each node only ever writes to its own slot. The max of each slot represents the most up-to-date count seen from that node. Since counts only go up, max is always safe — it can never decrease a slot's value. Convergence is guaranteed.

**Mental model:** Node 0 has `[3, 0]`, Node 1 has `[0, 2]`. Merge → `[3, 2]`. Value = 5. Regardless of which direction you merge (0→1 or 1→0), you get `[3, 2]`. That's commutativity. Apply merge twice: still `[3, 2]`. That's idempotency.

### 6.3 PN-Counter

```c
// include/pncounter.h

typedef struct {
    gcounter_t P; // tracks all increments
    gcounter_t N; // tracks all decrements
} pncounter_t;

void    pncounter_init      (pncounter_t *pc, int my_id, int num_nodes);
void    pncounter_increment (pncounter_t *pc);                           // P.increment()
void    pncounter_decrement (pncounter_t *pc);                           // N.increment()
int64_t pncounter_value     (const pncounter_t *pc);                     // P.value() - N.value()
void    pncounter_merge     (pncounter_t *dst, const pncounter_t *src);  // merge P; merge N
```

**Key insight:** A G-Counter can only go up (each node writes its own slot). You cannot represent decrement with a single G-Counter. Solution: track decrements in a separate G-Counter N. Net value = total increments − total decrements. Both P and N are G-Counters, so both merge correctly. The subtraction happens only at read time.

**Edge case to know:** The value can go negative if decrements outpace increments. The CRDT itself has no opinion on this — enforcing non-negativity (like a stock count) is the application's job, not the CRDT's.

### 6.4 LWW Register

```c
// include/lww_register.h

#define MAX_VALUE_LEN 512

typedef struct {
    char     value[MAX_VALUE_LEN];
    vclock_t timestamp;  // vector clock of the write that produced this value
    int      writer_id;  // node ID of the writer (for tie-breaking concurrent writes)
} lww_register_t;

void lww_register_init  (lww_register_t *r, int my_id, int num_nodes);
void lww_register_set   (lww_register_t *r, const char *value, vclock_t *node_clock);
void lww_register_get   (const lww_register_t *r, char *out);
void lww_register_merge (lww_register_t *dst, const lww_register_t *src);
```

**Merge rule for `lww_register_merge`:**

```c
vc_order_t ord = vclock_compare(&dst->timestamp, &src->timestamp);
if (ord == VC_BEFORE || ord == VC_CONCURRENT && src->writer_id > dst->writer_id) {
    // src is causally later, OR they are concurrent and src has higher priority
    *dst = *src;
}
// otherwise: dst is later or equal, keep dst
```

**Why vector clock, not wall time?** System clocks drift. Two servers can differ by milliseconds. A node with a slightly fast clock always "wins" concurrent writes — not because it wrote last, but because its clock is fast. Vector clocks capture causality: if B wrote after receiving A's state, B's vector clock dominates A's. No synchronisation needed. This is fundamentally correct; wall-time LWW is fundamentally broken.

### 6.5 OR-Set

This is the hardest CRDT. Take extra time here. Understand every field before writing code.

```c
// include/orset.h

#define MAX_TAGS     4096
#define MAX_ELEM_LEN 64

// A unique identifier for a single "add" operation
typedef struct {
    uint32_t node_id; // which node performed this add
    uint64_t seq;     // monotonically increasing sequence number for that node
} tag_t;

// One entry in the "added" log
typedef struct {
    char  elem[MAX_ELEM_LEN];
    tag_t tag;
} orset_entry_t;

typedef struct {
    orset_entry_t added[MAX_TAGS];          // every (elem, tag) pair ever added
    int           add_count;
    tag_t         removed[MAX_TAGS];        // tags that have been tombstoned
    int           remove_count;
    uint64_t      seq_counters[MAX_NODES];  // per-node sequence counter for generating tags
    int           my_id;
    int           num_nodes;
} orset_t;

void orset_init     (orset_t *s, int my_id, int num_nodes);
void orset_add      (orset_t *s, const char *elem);          // generates a fresh unique tag
void orset_remove   (orset_t *s, const char *elem);          // tombstones all live tags for elem
int  orset_contains (const orset_t *s, const char *elem);    // true if any live tag exists
void orset_list     (const orset_t *s, char out[][MAX_ELEM_LEN], int *count);
void orset_merge    (orset_t *dst, const orset_t *src);
```

**Core invariant:** An element is in the set if and only if at least one of its add-tags is not in the removed set.

**`orset_add` logic:**

```c
void orset_add(orset_t *s, const char *elem) {
    s->seq_counters[s->my_id]++;
    tag_t t = { .node_id = s->my_id, .seq = s->seq_counters[s->my_id] };
    orset_entry_t e;
    strncpy(e.elem, elem, MAX_ELEM_LEN - 1);
    e.tag = t;
    s->added[s->add_count++] = e;
}
```

**`orset_remove` logic:**

```c
void orset_remove(orset_t *s, const char *elem) {
    // Tombstone every currently-visible add-tag for this element
    for (int i = 0; i < s->add_count; i++) {
        if (strcmp(s->added[i].elem, elem) == 0 && !tag_is_removed(s, s->added[i].tag)) {
            s->removed[s->remove_count++] = s->added[i].tag;
        }
    }
}
```

**`orset_merge` logic:**

```c
void orset_merge(orset_t *dst, const orset_t *src) {
    // Union the added arrays (skip duplicates by tag)
    for (int i = 0; i < src->add_count; i++) {
        if (!entry_exists_in(dst, src->added[i])) {
            dst->added[dst->add_count++] = src->added[i];
        }
    }
    // Union the removed arrays (skip duplicate tags)
    for (int i = 0; i < src->remove_count; i++) {
        if (!tag_removed_in(dst, src->removed[i])) {
            dst->removed[dst->remove_count++] = src->removed[i];
        }
    }
    // Update seq_counters to max
    for (int i = 0; i < MAX_NODES; i++) {
        if (src->seq_counters[i] > dst->seq_counters[i])
            dst->seq_counters[i] = src->seq_counters[i];
    }
}
```

**The add-wins scenario — draw this on a whiteboard:**

```
Timeline:
  Node A: add("x") → generates tag (A, 1)
  Node B: remove("x") → tombstones tag (A, 1) [B saw the earlier add]
  Node A: add("x") → generates tag (A, 2)   ← concurrent with B's remove

After merge:
  added:   [(x, A1), (x, A2)]
  removed: [(A1)]

Is "x" in the set?
  Check tag A1: in removed → dead
  Check tag A2: NOT in removed → alive
  Result: YES. "x" is present.
```

This is why the semantics are called **add-wins**: a concurrent add creates a fresh tag that the concurrent remove didn't know about and therefore couldn't tombstone.

---

## 7. Wire Protocol Design

No external serialisation library. Every byte is yours to explain.

### Message Framing

```
┌────────────────────┬───────────────────┬──────────────────────────┐
│  4 bytes (uint32)  │  1 byte (uint8)   │  N bytes                 │
│  payload_length    │  message_type     │  payload                 │
└────────────────────┴───────────────────┴──────────────────────────┘
```

Read exactly 5 bytes to get header. Then read exactly `payload_length` bytes for the payload. Never assume message boundaries align with TCP segments.

### Message Types

| Byte | Type | Payload |
|------|------|---------|
| `0x01` | `MSG_HEARTBEAT` | `[1 byte: sender node_id]` |
| `0x02` | `MSG_STATE_SYNC` | Full serialised node state (see below) |
| `0x03` | `MSG_SYNC_ACK` | `[1 byte: sender node_id]` |
| `0x04` | `MSG_CLI_CMD` | Null-terminated command string |
| `0x05` | `MSG_CLI_RESP` | Null-terminated response string |

### State Serialisation Layout

```
[1 byte]   sender node_id
[4 bytes]  num_gcounters
  for each: [64 bytes key] [sizeof(gcounter_t) bytes struct]
[4 bytes]  num_pncounters
  for each: [64 bytes key] [sizeof(pncounter_t) bytes struct]
[4 bytes]  num_registers
  for each: [64 bytes key] [sizeof(lww_register_t) bytes struct]
[4 bytes]  num_orsets
  for each: [64 bytes key] [sizeof(orset_t) bytes struct]
```

Keep it flat and simple. No compression. The gossip interval is 2 seconds and the data fits easily in a single TCP write for thousands of keys.

---

## 8. Repository Structure

```
convergence/
├── include/
│   ├── vclock.h
│   ├── gcounter.h
│   ├── pncounter.h
│   ├── lww_register.h
│   ├── orset.h
│   ├── net.h
│   ├── wal.h
│   └── node.h
│
├── src/
│   ├── crdt/
│   │   ├── vclock.c
│   │   ├── gcounter.c
│   │   ├── pncounter.c
│   │   ├── lww_register.c
│   │   └── orset.c
│   ├── net.c          # TCP server, gossip loop, heartbeat, peer management
│   ├── wal.c          # Append-before-apply, fsync, replay
│   ├── node.c         # Aggregates all state; serialise/deserialise; merge incoming state
│   ├── cli.c          # Separate binary: connect to node, send cmd, print response, exit
│   └── main.c         # Entry point: parse args, replay WAL, start listener + gossip threads
│
├── tests/
│   ├── unity.h            # Unity test framework — single header, drop-in
│   ├── unity.c
│   ├── test_vclock.c
│   ├── test_gcounter.c
│   ├── test_pncounter.c
│   ├── test_lww.c
│   ├── test_orset.c
│   └── test_integration.c # Two node instances on real TCP; write to one; verify other
│
├── bench/
│   └── bench.c            # Write throughput, sync latency, memory footprint
│
├── demo/
│   └── partition_demo.sh  # The interview demo — runs everything automatically
│
├── Makefile
└── README.md
```

### Makefile Targets

```makefile
all:         builds convergence (server) and convergence-cli
make test:   builds and runs all unit + integration tests
make bench:  runs benchmark and prints results
make demo:   runs partition_demo.sh
make clean:  removes all build artifacts
```

---

## 9. Phase-by-Phase Implementation Plan

> **Rule before moving phases:** You must be able to explain the current phase's core concept out loud without looking at notes. If you cannot, you are not ready to move on. The interview will probe exactly this.

---

### Phase 0 — Concept Foundations

**Estimated time:** ~20 hours  
**No code in this phase.** Pure understanding.

**Topic 1: CAP Theorem**

Study until you can answer this without notes:

- C (Consistency): every read sees the most recent write, across all nodes
- A (Availability): every request receives a response (not necessarily the latest data)
- P (Partition Tolerance): the system keeps running even when the network splits nodes

Why you cannot have all three: during a network partition, node A and node B cannot communicate. If a write comes into A, node B does not know about it. Now if a read comes into B:
- To be **consistent**, B must wait for A (but A is unreachable → not **available**)
- To be **available**, B must answer with what it has (but it may be stale → not **consistent**)

You must choose. Convergence chooses **AP**: always available, eventually consistent. This is the correct choice for a distributed store where writes must never be refused.

**Topic 2: Eventual Consistency**

Operational definition: if writes stop and all partitions heal, all nodes will converge to the same state. There is no guarantee about *when* — only that it *will* happen. In our system, the bound is roughly one gossip cycle (~2 seconds) after healing.

**Topic 3: Why Wall-Time Timestamps Are Wrong**

Two servers have clocks that differ by 2 milliseconds. Node A writes at wall-time 1000ms. Node B writes at wall-time 999ms. B's write happened after A's in real life (due to network latency), but its timestamp is earlier. Using timestamps, we'd wrongly pick A's write as the "later" one. This is called **clock skew** and it is unavoidable.

**Topic 4: Vector Clocks**

A vector clock is an array of counters, one per node. Each node tracks:
- How many events it has generated itself
- The maximum number of events it has seen from each other node

When node A sends a message to B, it attaches its vector clock. B merges (element-wise max) and now B knows everything A knew. If B then makes a write, B's clock provably dominates A's — B is causally after A. No wall-time synchronisation needed.

**Topic 5: What is a CRDT, mathematically?**

A CRDT's state space forms a **join-semilattice**: a partial order where every pair of elements has a least upper bound (the "join" or "merge"). The merge operation must be:

- **Commutative:** `merge(A, B) = merge(B, A)` — order doesn't matter
- **Associative:** `merge(merge(A, B), C) = merge(A, merge(B, C))` — grouping doesn't matter
- **Idempotent:** `merge(A, A) = A` — applying merge twice gives the same result

These three properties guarantee that no matter how many times a node receives and merges state from peers, the result is always correct. No deduplication needed. No sequence numbers needed.

**Deliverable for Phase 0:**

Write these three paragraphs in your own words (not copied):
1. Why you cannot have all three of CAP
2. Why wall-time timestamps fail for ordering concurrent events
3. Why CRDT merge is guaranteed to converge

If you can write those paragraphs cleanly, you are ready for Phase 1.

---

### Phase 1 — Vector Clock

**Estimated time:** ~8 hours

Build the vector clock module in isolation before any CRDT code. Every CRDT depends on it.

**Implementation steps:**

1. Create `include/vclock.h` with the struct and all function signatures from §6.1.
2. Implement `src/crdt/vclock.c`:
   - `vclock_init`: zero all ticks, set `my_id` and `num_nodes`
   - `vclock_tick`: `vc->ticks[vc->my_id]++`
   - `vclock_merge`: `for each i: dst->ticks[i] = max(dst->ticks[i], src->ticks[i])`
   - `vclock_compare`: implement the four-case logic from §6.1 exactly
   - `vclock_copy`: `memcpy`
3. Write `tests/test_vclock.c`:

**Tests to implement:**

```
Test 1: basic tick
  - Init clock for node 0 in a 2-node system
  - Tick 3 times
  - Assert ticks[0] == 3, ticks[1] == 0

Test 2: merge gives element-wise max
  - Clock A = [3, 0], Clock B = [0, 2]
  - merge(A, B) should give [3, 2]
  - merge(B, A) should also give [3, 2]  ← commutativity

Test 3: concurrent detection
  - Node 0 ticks independently, node 1 ticks independently
  - Neither has seen the other
  - vclock_compare should return VC_CONCURRENT

Test 4: causal ordering
  - Node 0 ticks to [1, 0]
  - Node 1 merges node 0's clock, then ticks → [1, 1]
  - vclock_compare(A, B) should return VC_BEFORE (A happened-before B)

Test 5: idempotency
  - merge(A, merge(A, B)) should equal merge(A, B)
```

**Deliverable:** `make test_vclock` passes all five tests. You can draw on paper how two concurrent clocks look different from two causally-ordered clocks.

---

### Phase 2 — G-Counter & PN-Counter

**Estimated time:** ~15 hours

**G-Counter implementation steps:**

1. Create `include/gcounter.h` with struct and signatures from §6.2.
2. Implement `src/crdt/gcounter.c`. All four functions are simple.
3. Tests:

```
Test 1: basic increment and value
  - Node 0 increments 3 times
  - gcounter_value should return 3

Test 2: two-node offline scenario
  - Node 0 increments 3x (offline from node 1)
  - Node 1 increments 2x (offline from node 0)
  - merge(node0_state, node1_state) → value == 5
  - merge(node1_state, node0_state) → value == 5  ← same result

Test 3: commutativity
  - A = [3, 0], B = [0, 2]
  - merge(A, B) == merge(B, A) in every slot

Test 4: associativity
  - Three nodes A, B, C each increment independently
  - merge(merge(A, B), C) == merge(A, merge(B, C))

Test 5: idempotency
  - merge(A, merge(A, B)) == merge(A, B)
```

**PN-Counter implementation steps:**

1. Create `include/pncounter.h`.
2. Implement `src/crdt/pncounter.c`. Delegate increment/decrement to P and N respectively. Value = P.value() - N.value().
3. Tests:

```
Test 1: basic inc and dec
  - increment 5x, decrement 2x → value == 3

Test 2: offline scenario
  - Node A increments 5x (offline)
  - Node B decrements 2x (offline)
  - merge → value == 3

Test 3: value can go negative
  - Decrement 3x on empty counter → value == -3
  - (This is correct CRDT behaviour — application layer enforces non-negativity if needed)

Test 4: commutativity and idempotency (same logic as G-Counter)
```

**Deliverable:** `make test_gcounter && make test_pncounter` pass all tests. You can explain out loud why `max` merge on each slot is safe and sufficient.

---

### Phase 3 — LWW Register & OR-Set

**Estimated time:** ~25 hours

Do LWW Register first. It is simpler. OR-Set second. Give OR-Set extra time.

**LWW Register implementation steps:**

1. Create `include/lww_register.h`.
2. `lww_register_set`: tick the node clock, store the new clock and value and writer_id.
3. `lww_register_merge`: compare clocks → keep the one that is causally later → on `VC_CONCURRENT`, keep the higher `writer_id`.
4. Tests:

```
Test 1: basic set and get
  - Set "alice", get → "alice"

Test 2: later write wins (causal)
  - Node A sets "alice" (clock [1,0])
  - A sends state to B; B merges (B's clock becomes [1,0] or more)
  - B sets "charlie" (clock [1,1])
  - merge(A, B) → "charlie"  ← B is causally after A

Test 3: concurrent writes — higher node ID wins
  - Node 0 sets "alice" (clock [1,0], writer_id=0)
  - Node 1 sets "bob" (clock [0,1], writer_id=1)
  - These are concurrent (neither happens-before the other)
  - merge → "bob" (writer_id 1 > writer_id 0)
  - This is arbitrary but it is deterministic — that's all that matters

Test 4: idempotency
  - merge(A, merge(A, B)) == merge(A, B)
```

**OR-Set implementation steps:**

Read §6.5 again before writing a single line. Then:

1. Create `include/orset.h`.
2. Implement `orset_add` using sequence counters.
3. Implement `orset_remove` by tombstoning currently-visible tags.
4. Implement `orset_contains` by checking for any live tag.
5. Implement `orset_merge` by unioning both `added` and `removed` arrays.
6. Tests:

```
Test 1: basic add and contains
  - add("x") → contains("x") returns true

Test 2: basic remove
  - add("x"), remove("x") → contains("x") returns false

Test 3: ADD-WINS (the most important test)
  - Node A: add("x") → tag (A,1)
  - Node B: remove("x") → tombstones (A,1)
  - Node A: add("x") again → tag (A,2)   ← this is concurrent with B's remove
  - merge(A, B):
      added = [(x,A1), (x,A2)]
      removed = [(A1)]
  - contains("x") → TRUE  ← (A,2) is alive
  - This is the core test. If this passes, your OR-Set is correct.

Test 4: causal remove wins
  - Node A: add("x") → (A,1)
  - A sends state to B → B knows about (A,1)
  - B: remove("x") → tombstones (A,1)  ← this is causally after A's add
  - B sends state to A → A merges
  - contains("x") on both → FALSE  ← (A,1) tombstoned, no other add

Test 5: idempotency
  - merge(S, merge(S, T)) == merge(S, T)

Test 6: commutativity
  - merge(S, T) contains the same elements as merge(T, S)
```

**Deliverable:** `make test_lww && make test_orset` pass all tests. You can draw the OR-Set add-wins scenario on paper and explain why `(A,2)` being alive is the correct answer.

---

### Phase 4 — Networking Layer

**Estimated time:** ~20 hours

Start with a working echo server before adding any CRDT logic.

**Step 1: Basic TCP server and client (~4 hours)**

```c
// In net.c:
// net_start_listener(port): bind, listen, accept loop in a thread
// On each accepted fd: spawn a reader thread
// net_connect(ip, port): connect, return fd
// net_send(fd, type, payload, len): write length-prefixed framed message
// net_recv(fd, *type, buf, *len): read one complete framed message
```

Test: CLI binary connects to server. Server echoes the message back. You can see it with `strace` or just print both sides.

**Step 2: Message dispatch (~4 hours)**

In the reader thread spawned per connection, loop on `net_recv`. Dispatch by message type:
- `MSG_HEARTBEAT` → update peer's last-seen timestamp
- `MSG_STATE_SYNC` → call `node_deserialize_and_merge(payload)`
- `MSG_CLI_CMD` → parse the command, execute on node state, send `MSG_CLI_RESP`

**Step 3: Gossip thread (~4 hours)**

```c
void *gossip_thread(void *arg) {
    node_t *node = (node_t *)arg;
    while (1) {
        sleep(2);

        // Send heartbeat to all live peers
        for each peer in node->peers:
            net_send(peer.fd, MSG_HEARTBEAT, &node->id, 1);

        // Check for dead peers
        for each peer in node->peers:
            if (now() - peer.last_seen > 6 seconds):
                peer.alive = false;

        // Send full state to all live peers
        uint8_t buf[1 << 20]; // 1MB scratch buffer
        size_t len;
        pthread_mutex_lock(&node->lock);
        node_serialize(node, buf, &len);
        pthread_mutex_unlock(&node->lock);

        for each live peer:
            net_send(peer.fd, MSG_STATE_SYNC, buf, len);
    }
}
```

**Step 4: Serialisation (~4 hours)**

Implement `node_serialize` and `node_deserialize` following the layout from §7. Pack each CRDT struct directly — no fancy encoding. Use `memcpy`. Document the layout in comments.

**Step 5: Integration test (~4 hours)**

Write `tests/test_integration.c`:

```
1. Start node 0 on port 7000 (in a thread or forked process)
2. Start node 1 on port 7001, with node 0 as a peer
3. Wait 1 second for connection
4. Increment a G-Counter on node 0 via the CLI
5. Sleep 3 seconds (one gossip cycle + margin)
6. Read the G-Counter from node 1 via the CLI
7. Assert the values are equal
```

**Deliverable:** Two separate processes sync correctly after one gossip cycle. The integration test passes.

---

### Phase 5 — WAL & Offline Mode

**Estimated time:** ~25 hours

**Step 1: WAL format and append (~8 hours)**

WAL record format:

```
[8 bytes] uint64_t monotonic_seq     // ever-increasing sequence number (not wall time)
[1 byte]  uint8_t  crdt_type         // 0=gcounter 1=pncounter 2=lww 3=orset
[1 byte]  uint8_t  op_type           // 0=inc 1=dec 2=set 3=add 4=remove
[2 bytes] uint16_t key_len
[key_len] char     key[]
[4 bytes] uint32_t payload_len
[payload_len] uint8_t payload[]      // serialised operation argument (e.g., value for LWW set)
```

Implement:

```c
wal_t *wal_open   (const char *path);        // open in O_RDWR | O_CREAT | O_APPEND
int    wal_append (wal_t *w, wal_record_t r); // write record, then fsync()
void   wal_close  (wal_t *w);
```

**fsync is mandatory.** Without `fsync`, the OS may buffer the write in its page cache and lose it if the machine loses power or the process crashes before the flush. `fsync` blocks until the data is physically on disk. This is the same guarantee PostgreSQL provides.

**Step 2: WAL replay (~4 hours)**

```c
void wal_replay(wal_t *w, node_t *node);
// Read the WAL from the beginning
// For each record: re-apply the operation to node state
// At the end: node state matches what it was before the crash
```

Call this in `main.c` before starting listener and gossip threads.

**Step 3: Integrate WAL into every write path (~6 hours)**

Every operation that modifies node state must follow exactly this pattern:

```c
pthread_mutex_lock(&node->lock);
wal_append(node->wal, record);    // write to disk first
apply_operation_to_memory(node);  // then update in-memory state
pthread_mutex_unlock(&node->lock);
```

The write-to-disk-first guarantee is what makes the WAL work. If you update memory first and then crash before writing to disk, the in-memory change is lost and the WAL cannot replay it.

**Step 4: The partition demo (~7 hours)**

Offline mode is not a special state. When the gossip thread fails to reach a peer (connection refused, timeout), it marks the peer as offline and continues. Local writes still come in, get WAL'd, and get applied to in-memory state. When the peer returns, the next gossip cycle carries the full current state — which includes everything written during the partition.

Write `demo/partition_demo.sh`:

```bash
#!/bin/bash
set -e

echo "=== Starting Node 0 on port 7000 ==="
./convergence --id 0 --port 7000 --peer 127.0.0.1:7001 --wal node0.wal &
NODE0_PID=$!

echo "=== Starting Node 1 on port 7001 ==="
./convergence --id 1 --port 7001 --peer 127.0.0.1:7000 --wal node1.wal &
NODE1_PID=$!

sleep 2
echo "=== Nodes connected. Syncing for 2 seconds. ==="

echo "=== Simulating partition: blocking port 7001 with iptables ==="
sudo iptables -A INPUT -p tcp --dport 7001 -s 127.0.0.1 -j DROP
sudo iptables -A OUTPUT -p tcp --dport 7001 -d 127.0.0.1 -j DROP

echo "=== Writing to Node 0 (2 increments to 'visits') ==="
./convergence-cli --port 7000 gctr inc visits
./convergence-cli --port 7000 gctr inc visits

echo "=== Writing to Node 1 (1 increment to 'visits') — nodes are PARTITIONED ==="
./convergence-cli --port 7001 gctr inc visits

echo "=== Node 0 sees: $(./convergence-cli --port 7000 gctr get visits) ==="
echo "=== Node 1 sees: $(./convergence-cli --port 7001 gctr get visits) ==="
echo "    ^ These should be different. The partition is working."

echo "=== Healing partition ==="
sudo iptables -D INPUT -p tcp --dport 7001 -s 127.0.0.1 -j DROP
sudo iptables -D OUTPUT -p tcp --dport 7001 -d 127.0.0.1 -j DROP

echo "=== Waiting one gossip cycle (3 seconds) ==="
sleep 3

echo "=== Node 0 sees: $(./convergence-cli --port 7000 gctr get visits) ==="
echo "=== Node 1 sees: $(./convergence-cli --port 7001 gctr get visits) ==="
echo "    ^ Both should show 3. Convergence achieved."

kill $NODE0_PID $NODE1_PID
sudo rm -f node0.wal node1.wal
```

**Deliverable:** `make demo` runs the script cleanly and both nodes show `3` at the end. This is your interview demo. Record it as a GIF for the README.

---

### Phase 6 — Demo, Benchmarks & README

**Estimated time:** ~15 hours

**Benchmarks (bench/bench.c):**

Write a program that measures and prints:

| Benchmark | Method |
|-----------|--------|
| Write throughput | Time 1,000,000 `gcounter_increment` calls. Divide. |
| Sync latency | Timestamp write on node 0. Poll node 1 every 10ms until value appears. Record the time. |
| Memory footprint | Populate 100,000 keys. Read `/proc/self/status` for `VmRSS`. |
| WAL throughput | Time 100,000 `wal_append` + `fsync` calls. |

Put all four numbers in the README under a "Benchmarks" section. Imperfect numbers are better than no numbers.

**README Structure:**

```markdown
# Convergence

> A distributed key-value store where nodes sync conflict-free after network partitions.

## The Problem
[1 paragraph: what happens when two nodes disagree and there's no coordinator]

## Demo
[GIF of the partition demo here — record with asciinema or peek]
[How to run: clone, make, make demo]

## How It Works
[4 sentences explaining CRDTs at an intuitive level]

## Architecture
[ASCII diagram from §4]

## Data Types
[One paragraph per CRDT: what it does, what the merge does, what the guarantee is]

## Design Decisions

**Why C?**
Consistent with my systems background (Linux kernel contributions, storage engine in C).
Full control, no abstractions obscuring what's happening.

**Why vector clocks instead of wall-time timestamps?**
[2 sentences: clock skew, causal ordering guarantee]

**Why OR-Set add-wins instead of remove-wins?**
The common intuition is "the delete should win." But in a distributed system, a concurrent add
creates a tag that the concurrent remove cannot know about. Treating that unknown add as already
removed would silently discard data — unacceptable for a data store. Add-wins is safer.

**Why gossip (anti-entropy) instead of push-on-write?**
Gossip is simpler and self-healing. A push-on-write system needs to handle failed deliveries,
retries, and deduplication. Gossip retries automatically every 2 seconds. The full-state exchange
means even a node that was offline for 10 minutes catches up in one cycle.

**CAP Theorem choice: AP**
This system is Available + Partition-tolerant. During a partition, nodes continue accepting writes
(Available) but may temporarily disagree (not Consistent). On healing, they converge. This is the
correct choice for a store where refusing writes is worse than temporarily stale reads.

## What I Would Improve
- **Delta-CRDTs:** send only the changes since last sync, not the full state. Reduces gossip bandwidth.
- **Tombstone GC for OR-Set:** removed tags accumulate forever. A stable-state GC pass would reclaim memory.
- **Dynamic membership:** currently nodes are configured statically at startup. A CRDT-based membership protocol (like SWIM) would allow dynamic join and leave.
- **Delta compression on WAL:** WAL grows unboundedly. A checkpoint mechanism would compact it.

## Benchmarks
[Your actual numbers here]

## Building
make         # builds convergence and convergence-cli
make test    # runs all unit and integration tests
make demo    # runs the partition demo (requires sudo for iptables)
make bench   # runs benchmarks and prints results
```

**Deliverable:** A repo that tells the full engineering story before you say a word. A recruiter reading the README for 2 minutes should understand what you built, why it is hard, and what tradeoffs you made.

---

## 10. Testing Strategy

| Test | Location | What It Proves |
|------|----------|----------------|
| `vclock_compare` correctness | `test_vclock.c` | Concurrent, before, after, equal all correctly detected |
| `gcounter` commutativity | `test_gcounter.c` | `merge(A,B) == merge(B,A)` |
| `gcounter` idempotency | `test_gcounter.c` | `merge(A, merge(A,B)) == merge(A,B)` |
| `gcounter` real scenario | `test_gcounter.c` | 3 + 2 offline increments merge to 5 |
| `pncounter` offline dec+inc | `test_pncounter.c` | Offline 5 increments + offline 2 decrements merge to 3 |
| `lww` concurrent tie-break | `test_lww.c` | Deterministic winner on concurrent writes |
| `lww` causal ordering | `test_lww.c` | Causally later write always wins |
| `orset` add-wins | `test_orset.c` | Concurrent add + remove keeps element |
| `orset` causal remove | `test_orset.c` | Causally later remove removes element |
| `orset` idempotency | `test_orset.c` | Merge with yourself gives same state |
| Integration sync | `test_integration.c` | Two real TCP nodes sync within 3 seconds |
| WAL replay | `test_integration.c` | Node restart restores state from WAL correctly |

Run everything with: `make test`

---

## 11. Benchmark Targets

These are the numbers to aim for. Record your actuals in the README.

| Metric | Target | Method |
|--------|--------|--------|
| Single-node write throughput | ≥ 50,000 ops/sec | Time 1,000,000 `gcounter_increment` calls |
| Gossip sync latency (steady state) | < 2.5 seconds | Time from write on A to observation on B |
| Gossip sync latency (after 30s partition) | < 3 seconds | Heal partition, time to convergence |
| Memory footprint (100k keys) | < 50 MB | Check `VmRSS` in `/proc/self/status` |
| WAL throughput | ≥ 20,000 ops/sec | Time 100,000 `wal_append` + `fsync` calls |

---

## 12. Demo Script

What to say and show in an interview or demo:

```
"I'll show you the partition-heal-converge scenario.

I start two nodes. They connect via gossip.
[run: make demo — iptables blocks node 1]

Both nodes are now isolated. I write 2 increments to node 0
and 1 increment to node 1. Neither can see the other.

[show: node 0 shows 2, node 1 shows 1]

Now I heal the partition.
[iptables rule deleted]

After one gossip cycle — 2 seconds — both nodes show 3.

The CRDT merge is what makes this work. A G-Counter gives each node
its own slot in an array. Merge takes the element-wise max.
Node 0 had [2,0], node 1 had [0,1]. Max → [2,1]. Sum = 3.
No coordinator. No conflict. Mathematically guaranteed."
```

Practice saying this until it is fluent. The demo is 30 seconds. The explanation is another 30 seconds. That 1-minute moment gets you more interview credit than anything else in the project.

---

## 13. Interview Preparation

These are the questions you will get. Know the answers cold before the interview.

**Q: Why can't you have all three of CAP simultaneously?**

During a network partition, two nodes cannot communicate. If a write arrives at node A, node B does not know. If a read arrives at node B: to be consistent it must refuse until it can verify with A — losing availability. To be available it must answer with potentially stale data — losing consistency. The partition is the forcing function. You must pick two.

**Q: What does eventual consistency mean in your system?**

If writes stop and all partitions heal, all nodes converge to the same state within one gossip cycle — roughly 2 seconds. During that window they may disagree. We accept this temporary disagreement to stay available during partitions. The CAP choice is AP.

**Q: Why vector clocks instead of timestamps for LWW?**

Wall clocks drift. Two machines can differ by milliseconds. A node with a slightly fast clock always wins concurrent writes — not because it wrote later, but because its clock is ahead. Vector clocks capture causality: if node B writes after merging node A's state, B's clock provably dominates A's. That is the correct ordering. No time synchronisation required.

**Q: Why does G-Counter merge with element-wise max?**

Each node only ever increments its own slot. So the max of each slot is always the latest known count from that node. Max is monotonic — it can only increase, never decrease. This ensures that once a count is known, it is never forgotten. Taking max across all slots gives the union of all increments without any double-counting.

**Q: Explain OR-Set add-wins with a concrete example.**

Node A adds "x" → generates tag (A,1). Node B removes "x" → tombstones (A,1). Concurrently, A adds "x" again → generates tag (A,2). After merge: `added = [(x,A1),(x,A2)]`, `removed = [(A1)]`. Is "x" in the set? Tag (A,1) is tombstoned — dead. Tag (A,2) is not tombstoned — alive. Result: yes. "x" is present. Add wins because each add generates a fresh tag that the concurrent remove had no way to know about.

**Q: What happens if you apply the same gossip message twice?**

Nothing bad. Merge is idempotent: `merge(A, merge(A,B)) = merge(A,B)`. Receiving the same state twice gives the same result as receiving it once. This is a deliberate property of the design — it means we do not need sequence numbers, deduplication, or exactly-once delivery. Best-effort delivery is sufficient.

**Q: How does the WAL guarantee no data loss?**

Every write is appended to the WAL and fsynced to disk before being applied to in-memory state. If the node crashes after fsync but before updating memory, the WAL is replayed on restart to recover the operation. Fsync blocks until the data is physically on disk. This is the same pattern PostgreSQL uses for its WAL.

**Q: What would you improve?**

Three concrete things: First, delta-CRDTs — send only the changes since the last sync cycle rather than the full state. Reduces bandwidth from O(total state) to O(recent writes). Second, tombstone garbage collection for the OR-Set — removed tags accumulate forever; a periodic GC pass would reclaim memory once a remove is known to all nodes. Third, a membership CRDT for dynamic node join and leave — currently all nodes must be declared at startup.

---

## 14. Hour Estimates

| Phase | Scope | Estimated Hours |
|-------|-------|----------------|
| 0 | Concept foundations — no code | ~20 |
| 1 | Vector clock | ~8 |
| 2 | G-Counter & PN-Counter | ~15 |
| 3 | LWW Register & OR-Set | ~25 |
| 4 | Networking layer | ~20 |
| 5 | WAL & offline mode + partition demo | ~25 |
| 6 | Benchmarks, README, polish | ~15 |
| **Total** | | **~128 hours** |

At **4 focused hours/day** → ~5 weeks.  
At **6 focused hours/day** → ~3.5 weeks.

**One firm rule:** DSA every day before this project. The DSA round gates the system design round. Do not compromise DSA to finish this faster. A project that impresses in system design does nothing if you do not clear the coding round first.

---

*Document version 1.0 — follow phases in order, do not skip testing steps, and do not move on until the deliverable is solid.*

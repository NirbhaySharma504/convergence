// test_lww.c — LWW Register unit tests (§9 Phase 3).
#include "test.h"
#include "lww_register.h"
#include <string.h>

static void test_basic_set_get(void) {
    vclock_t clk; vclock_init(&clk, 0, 2);
    lww_register_t r; lww_register_init(&r, 0, 2);
    lww_register_set(&r, "alice", &clk);
    char out[MAX_VALUE_LEN]; lww_register_get(&r, out, sizeof(out));
    CHECK(strcmp(out, "alice") == 0);
}

static void test_causal_later_wins(void) {
    // Node 0 sets "alice"; node 1 learns it, then sets "charlie" (causally after).
    vclock_t clk0; vclock_init(&clk0, 0, 2);
    vclock_t clk1; vclock_init(&clk1, 1, 2);

    lww_register_t a; lww_register_init(&a, 0, 2);
    lww_register_set(&a, "alice", &clk0);          // a.ts = [1,0]

    lww_register_t b; lww_register_init(&b, 1, 2);
    lww_register_merge(&b, &a);                     // b learns a
    vclock_merge(&clk1, &a.timestamp);             // node 1 clock learns a
    lww_register_set(&b, "charlie", &clk1);        // b.ts = [1,1] (after a)

    lww_register_t m = a; lww_register_merge(&m, &b);
    char out[MAX_VALUE_LEN]; lww_register_get(&m, out, sizeof(out));
    CHECK(strcmp(out, "charlie") == 0);
}

static void test_concurrent_tiebreak(void) {
    // Concurrent writes: higher writer_id wins, deterministically.
    vclock_t clk0; vclock_init(&clk0, 0, 2);
    vclock_t clk1; vclock_init(&clk1, 1, 2);

    lww_register_t a; lww_register_init(&a, 0, 2);
    lww_register_set(&a, "alice", &clk0);  // [1,0], writer 0

    lww_register_t b; lww_register_init(&b, 1, 2);
    lww_register_set(&b, "bob", &clk1);    // [0,1], writer 1 (concurrent with a)

    lww_register_t ab = a; lww_register_merge(&ab, &b);
    lww_register_t ba = b; lww_register_merge(&ba, &a);
    char o1[MAX_VALUE_LEN], o2[MAX_VALUE_LEN];
    lww_register_get(&ab, o1, sizeof(o1));
    lww_register_get(&ba, o2, sizeof(o2));
    CHECK(strcmp(o1, "bob") == 0);  // writer 1 > writer 0
    CHECK(strcmp(o2, "bob") == 0);  // same result regardless of merge direction
}

static void test_idempotency(void) {
    vclock_t clk0; vclock_init(&clk0, 0, 2);
    lww_register_t a; lww_register_init(&a, 0, 2);
    lww_register_set(&a, "x", &clk0);
    lww_register_t b; lww_register_init(&b, 1, 2);

    lww_register_t once = a; lww_register_merge(&once, &b);
    lww_register_t twice = once; lww_register_merge(&twice, &b);
    char o1[MAX_VALUE_LEN], o2[MAX_VALUE_LEN];
    lww_register_get(&once, o1, sizeof(o1));
    lww_register_get(&twice, o2, sizeof(o2));
    CHECK(strcmp(o1, o2) == 0);
}

int main(void) {
    printf("test_lww:\n");
    RUN(test_basic_set_get);
    RUN(test_causal_later_wins);
    RUN(test_concurrent_tiebreak);
    RUN(test_idempotency);
    printf("test_lww: all passed\n");
    return 0;
}

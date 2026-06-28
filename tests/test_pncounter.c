// test_pncounter.c — PN-Counter unit tests (§9 Phase 3).
#include "test.h"
#include "pncounter.h"

static void test_basic_inc_dec(void) {
    pncounter_t p; pncounter_init(&p, 0, 2);
    for (int i = 0; i < 5; i++) pncounter_increment(&p);
    for (int i = 0; i < 2; i++) pncounter_decrement(&p);
    CHECK_EQ(pncounter_value(&p), 3);
}

static void test_offline_merge(void) {
    pncounter_t a; pncounter_init(&a, 0, 2);
    pncounter_t b; pncounter_init(&b, 1, 2);
    for (int i = 0; i < 5; i++) pncounter_increment(&a); // node 0
    for (int i = 0; i < 2; i++) pncounter_decrement(&b); // node 1
    pncounter_t ab = a; pncounter_merge(&ab, &b);
    pncounter_t ba = b; pncounter_merge(&ba, &a);
    CHECK_EQ(pncounter_value(&ab), 3);
    CHECK_EQ(pncounter_value(&ba), 3);
}

static void test_can_go_negative(void) {
    pncounter_t p; pncounter_init(&p, 0, 2);
    for (int i = 0; i < 3; i++) pncounter_decrement(&p);
    CHECK_EQ(pncounter_value(&p), -3); // CRDT has no opinion on non-negativity
}

static void test_idempotency(void) {
    pncounter_t a; pncounter_init(&a, 0, 2);
    pncounter_t b; pncounter_init(&b, 1, 2);
    for (int i = 0; i < 4; i++) pncounter_increment(&a);
    for (int i = 0; i < 1; i++) pncounter_decrement(&b);
    pncounter_t once = a; pncounter_merge(&once, &b);
    pncounter_t twice = once; pncounter_merge(&twice, &b);
    CHECK_EQ(pncounter_value(&once), pncounter_value(&twice));
}

int main(void) {
    printf("test_pncounter:\n");
    RUN(test_basic_inc_dec);
    RUN(test_offline_merge);
    RUN(test_can_go_negative);
    RUN(test_idempotency);
    printf("test_pncounter: all passed\n");
    return 0;
}

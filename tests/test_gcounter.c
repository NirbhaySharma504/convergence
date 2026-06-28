// test_gcounter.c — G-Counter unit tests (§9 Phase 2).
#include "test.h"
#include "gcounter.h"

static void test_basic_increment(void) {
    gcounter_t g; gcounter_init(&g, 0, 2);
    gcounter_increment(&g); gcounter_increment(&g); gcounter_increment(&g);
    CHECK_EQ(gcounter_value(&g), 3);
}

static void test_offline_merge(void) {
    gcounter_t a; gcounter_init(&a, 0, 2);
    gcounter_t b; gcounter_init(&b, 1, 2);
    for (int i = 0; i < 3; i++) gcounter_increment(&a); // node 0 offline
    for (int i = 0; i < 2; i++) gcounter_increment(&b); // node 1 offline

    gcounter_t ab = a; gcounter_merge(&ab, &b);
    gcounter_t ba = b; gcounter_merge(&ba, &a);
    CHECK_EQ(gcounter_value(&ab), 5);
    CHECK_EQ(gcounter_value(&ba), 5); // same result regardless of direction
}

static void test_commutativity(void) {
    gcounter_t a; gcounter_init(&a, 0, 2); a.counts[0] = 3;
    gcounter_t b; gcounter_init(&b, 1, 2); b.counts[1] = 2;
    gcounter_t ab = a; gcounter_merge(&ab, &b);
    gcounter_t ba = b; gcounter_merge(&ba, &a);
    for (int i = 0; i < MAX_NODES; i++) CHECK_EQ(ab.counts[i], ba.counts[i]);
}

static void test_associativity(void) {
    gcounter_t a; gcounter_init(&a, 0, 3); a.counts[0] = 1;
    gcounter_t b; gcounter_init(&b, 1, 3); b.counts[1] = 2;
    gcounter_t c; gcounter_init(&c, 2, 3); c.counts[2] = 3;
    // (a merge b) merge c
    gcounter_t l = a; gcounter_merge(&l, &b); gcounter_merge(&l, &c);
    // a merge (b merge c)
    gcounter_t bc = b; gcounter_merge(&bc, &c);
    gcounter_t r = a; gcounter_merge(&r, &bc);
    for (int i = 0; i < MAX_NODES; i++) CHECK_EQ(l.counts[i], r.counts[i]);
}

static void test_idempotency(void) {
    gcounter_t a; gcounter_init(&a, 0, 2); a.counts[0] = 3;
    gcounter_t b; gcounter_init(&b, 1, 2); b.counts[1] = 2;
    gcounter_t once = a; gcounter_merge(&once, &b);
    gcounter_t twice = once; gcounter_merge(&twice, &b); // merge(once, b) again
    for (int i = 0; i < MAX_NODES; i++) CHECK_EQ(once.counts[i], twice.counts[i]);
}

int main(void) {
    printf("test_gcounter:\n");
    RUN(test_basic_increment);
    RUN(test_offline_merge);
    RUN(test_commutativity);
    RUN(test_associativity);
    RUN(test_idempotency);
    printf("test_gcounter: all passed\n");
    return 0;
}

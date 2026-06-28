// test_vclock.c — vector clock unit tests (§9 Phase 1).
#include "test.h"
#include "vclock.h"

static void test_basic_tick(void) {
    vclock_t a; vclock_init(&a, 0, 2);
    vclock_tick(&a); vclock_tick(&a); vclock_tick(&a);
    CHECK_EQ(a.ticks[0], 3);
    CHECK_EQ(a.ticks[1], 0);
}

static void test_merge_elementwise_max(void) {
    vclock_t a, b; vclock_init(&a, 0, 2); vclock_init(&b, 1, 2);
    a.ticks[0] = 3; b.ticks[1] = 2;
    vclock_t ab = a, ba = b;
    vclock_merge(&ab, &b);  // merge(a,b)
    vclock_merge(&ba, &a);  // merge(b,a)
    CHECK_EQ(ab.ticks[0], 3); CHECK_EQ(ab.ticks[1], 2);
    CHECK_EQ(ba.ticks[0], 3); CHECK_EQ(ba.ticks[1], 2); // commutative
}

static void test_concurrent_detection(void) {
    vclock_t a, b; vclock_init(&a, 0, 2); vclock_init(&b, 1, 2);
    vclock_tick(&a); // [1,0]
    vclock_tick(&b); // [0,1]
    CHECK(vclock_compare(&a, &b) == VC_CONCURRENT);
}

static void test_causal_ordering(void) {
    vclock_t a, b; vclock_init(&a, 0, 2); vclock_init(&b, 1, 2);
    vclock_tick(&a);          // a = [1,0]
    vclock_merge(&b, &a);     // b learns a
    vclock_tick(&b);          // b = [1,1]
    CHECK(vclock_compare(&a, &b) == VC_BEFORE);
    CHECK(vclock_compare(&b, &a) == VC_AFTER);
}

static void test_idempotency(void) {
    vclock_t a, b; vclock_init(&a, 0, 2); vclock_init(&b, 1, 2);
    a.ticks[0] = 3; b.ticks[1] = 2;
    vclock_t once = a; vclock_merge(&once, &b);            // merge(a,b)
    vclock_t twice = a; vclock_merge(&twice, &b); vclock_merge(&twice, &once);
    CHECK(vclock_compare(&once, &twice) == VC_EQUAL);
}

int main(void) {
    printf("test_vclock:\n");
    RUN(test_basic_tick);
    RUN(test_merge_elementwise_max);
    RUN(test_concurrent_detection);
    RUN(test_causal_ordering);
    RUN(test_idempotency);
    printf("test_vclock: all passed\n");
    return 0;
}

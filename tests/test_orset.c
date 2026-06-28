// test_orset.c — OR-Set unit tests (§9 Phase 3). Add-wins is the key case.
#include "test.h"
#include "orset.h"

static void test_basic_add_contains(void) {
    orset_t s; orset_init(&s, 0, 2);
    orset_add(&s, "x");
    CHECK(orset_contains(&s, "x"));
}

static void test_basic_remove(void) {
    orset_t s; orset_init(&s, 0, 2);
    orset_add(&s, "x");
    orset_remove(&s, "x");
    CHECK(!orset_contains(&s, "x"));
}

static void test_add_wins(void) {
    // A: add x -> (A,1).  B: learns it, remove x -> tombstones (A,1).
    // A: add x again -> (A,2), CONCURRENT with B's remove. Merge -> x present.
    orset_t a; orset_init(&a, 0, 2);
    orset_t b; orset_init(&b, 1, 2);

    orset_add(&a, "x");          // (A,1)
    orset_merge(&b, &a);         // B sees (A,1)
    orset_remove(&b, "x");       // B tombstones (A,1)
    orset_add(&a, "x");          // (A,2) concurrent with B's remove

    orset_t m = a; orset_merge(&m, &b);
    CHECK(orset_contains(&m, "x"));  // (A,2) is alive -> add wins
}

static void test_causal_remove_wins(void) {
    // A: add x. B learns it and removes x (causally after the add). -> absent.
    orset_t a; orset_init(&a, 0, 2);
    orset_t b; orset_init(&b, 1, 2);

    orset_add(&a, "x");      // (A,1)
    orset_merge(&b, &a);     // B sees (A,1)
    orset_remove(&b, "x");   // tombstone (A,1), no concurrent re-add

    orset_t m = a; orset_merge(&m, &b);
    CHECK(!orset_contains(&m, "x"));
}

static void test_commutativity(void) {
    orset_t a; orset_init(&a, 0, 2);
    orset_t b; orset_init(&b, 1, 2);
    orset_add(&a, "x"); orset_add(&a, "y");
    orset_add(&b, "z");
    orset_t ab = a; orset_merge(&ab, &b);
    orset_t ba = b; orset_merge(&ba, &a);
    CHECK_EQ(orset_contains(&ab, "x"), orset_contains(&ba, "x"));
    CHECK_EQ(orset_contains(&ab, "y"), orset_contains(&ba, "y"));
    CHECK_EQ(orset_contains(&ab, "z"), orset_contains(&ba, "z"));
}

static void test_idempotency(void) {
    orset_t s; orset_init(&s, 0, 2);
    orset_t t; orset_init(&t, 1, 2);
    orset_add(&s, "x"); orset_add(&t, "y"); orset_remove(&t, "y");
    orset_t once = s; orset_merge(&once, &t);
    orset_t twice = once; orset_merge(&twice, &t);
    CHECK_EQ(once.add_count, twice.add_count);
    CHECK_EQ(once.remove_count, twice.remove_count);
}

int main(void) {
    printf("test_orset:\n");
    RUN(test_basic_add_contains);
    RUN(test_basic_remove);
    RUN(test_add_wins);
    RUN(test_causal_remove_wins);
    RUN(test_commutativity);
    RUN(test_idempotency);
    printf("test_orset: all passed\n");
    return 0;
}

// test_wal.c — durability: writes survive a simulated restart via WAL replay.
#include "test.h"
#include "node.h"
#include "wal.h"

#include <string.h>
#include <unistd.h>

static const char *WP = "build/test.wal";

static void cmd(node_t *n, const char *c) {
    char r[256]; node_handle_cli(n, c, r, sizeof(r));
}
static void query(node_t *n, const char *c, char *out, size_t cap) {
    node_handle_cli(n, c, out, cap);
}

static void test_replay_restores_state(void) {
    unlink(WP);

    // ---- original node: attach WAL, perform writes, then "crash" (free) ----
    node_t *a = malloc(sizeof(*a));
    node_init(a, 0, 2, 0);
    wal_t *w = wal_open(WP);
    CHECK(w != NULL);
    node_set_wal(a, w);

    cmd(a, "gctr inc visits");  cmd(a, "gctr inc visits");
    cmd(a, "pctr inc balance"); cmd(a, "pctr inc balance"); cmd(a, "pctr dec balance");
    cmd(a, "reg set user alice");
    cmd(a, "set add tags x");   cmd(a, "set add tags y"); cmd(a, "set rm tags x");

    char r[256];
    query(a, "gctr get visits", r, sizeof(r)); CHECK_EQ(atoi(r), 2);
    wal_close(w);
    free(a);

    // ---- fresh node: replay the WAL, verify every value is restored --------
    node_t *b = malloc(sizeof(*b));
    node_init(b, 0, 2, 0);
    long cnt = node_replay(b, WP);
    CHECK_EQ(cnt, 9); // 2 gctr + 3 pctr + 1 reg + 3 set

    query(b, "gctr get visits", r, sizeof(r));  CHECK_EQ(atoi(r), 2);
    query(b, "pctr get balance", r, sizeof(r)); CHECK_EQ(atoi(r), 1);
    query(b, "reg get user", r, sizeof(r));     CHECK(strcmp(r, "alice") == 0);
    query(b, "set list tags", r, sizeof(r));    CHECK(strcmp(r, "y") == 0); // x removed

    free(b);
    unlink(WP);
}

int main(void) {
    printf("test_wal:\n");
    RUN(test_replay_restores_state);
    printf("test_wal: all passed\n");
    return 0;
}

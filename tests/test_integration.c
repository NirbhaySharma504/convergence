// test_integration.c — two real node processes sync over TCP (§9 Phase 4).
//
// Spawns ./convergence twice, drives them with CLI commands over TCP, and
// verifies a G-Counter written on node 0 appears on node 1 after one gossip cycle.
#include "test.h"
#include "net.h"

#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>

static pid_t spawn_node(const char *id, const char *port, const char *peer) {
    pid_t pid = fork();
    if (pid == 0) {
        // Silence node stderr so test output stays clean.
        if (!freopen("/dev/null", "w", stderr)) { /* non-fatal */ }
        execl("./convergence", "convergence",
              "--id", id, "--port", port, "--peer", peer, "--nodes", "2", (char *)NULL);
        _exit(127);
    }
    return pid;
}

// Send one CLI command to a node and capture its response. Returns 0 on success.
static int send_cmd(int port, const char *cmd, char *out, size_t out_cap) {
    int fd = net_connect("127.0.0.1", port);
    if (fd < 0) return -1;
    if (net_send_msg(fd, MSG_CLI_CMD, (const uint8_t *)cmd, (uint32_t)strlen(cmd) + 1) < 0) {
        close(fd); return -1;
    }
    uint8_t type; uint32_t len; uint8_t buf[256];
    if (net_recv_msg(fd, &type, buf, &len, sizeof(buf)) < 0) { close(fd); return -1; }
    close(fd);
    buf[sizeof(buf) - 1] = '\0';
    snprintf(out, out_cap, "%s", (char *)buf);
    return 0;
}

static void test_two_node_sync(void) {
    pid_t n0 = spawn_node("0", "7000", "127.0.0.1:7001");
    pid_t n1 = spawn_node("1", "7001", "127.0.0.1:7000");

    sleep(1); // let both bind + connect

    char resp[256];
    CHECK(send_cmd(7000, "gctr inc visits", resp, sizeof(resp)) == 0);
    CHECK(send_cmd(7000, "gctr inc visits", resp, sizeof(resp)) == 0);

    sleep(GOSSIP_INTERVAL_SEC + 2); // one gossip cycle + margin

    CHECK(send_cmd(7001, "gctr get visits", resp, sizeof(resp)) == 0);
    CHECK_EQ(atoi(resp), 2); // node 1 converged to node 0's writes

    kill(n0, SIGKILL); kill(n1, SIGKILL);
    waitpid(n0, NULL, 0); waitpid(n1, NULL, 0);
}

int main(void) {
    printf("test_integration:\n");
    RUN(test_two_node_sync);
    printf("test_integration: all passed\n");
    return 0;
}

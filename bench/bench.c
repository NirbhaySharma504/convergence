// bench.c — measures the four headline numbers for the README.
//
//   1. Single-node write throughput   (gcounter_increment)
//   2. WAL throughput                 (wal_append + fsync)
//   3. Memory footprint               (100k G-Counter keys, VmRSS delta)
//   4. Gossip sync latency            (write on node 0 -> observed on node 1)
#include "gcounter.h"
#include "wal.h"
#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static long read_vmrss_kb(void) {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256]; long kb = -1;
    while (fgets(line, sizeof(line), f))
        if (sscanf(line, "VmRSS: %ld kB", &kb) == 1) break;
    fclose(f);
    return kb;
}

static void bench_write_throughput(void) {
    const long N = 1000000;
    gcounter_t g; gcounter_init(&g, 0, 2);
    double t0 = now_sec();
    for (long i = 0; i < N; i++) gcounter_increment(&g);
    double dt = now_sec() - t0;
    printf("  write throughput   : %.0f ops/sec  (%ld increments in %.3fs)\n",
           N / dt, N, dt);
}

static void bench_wal_throughput(void) {
    // Each append issues a real fsync(), so this is disk-flush-bound (~ms each),
    // not CPU-bound. Keep the count modest so the bench stays quick to run.
    const long N = 2000;
    const char *path = "/tmp/conv_bench.wal";
    unlink(path);
    wal_t *w = wal_open(path);
    if (!w) { printf("  wal throughput     : ERROR opening WAL\n"); return; }
    wal_record_t r;
    memset(&r, 0, sizeof(r));
    r.crdt_type = WAL_GCOUNTER; r.op_type = WAL_OP_INC;
    snprintf(r.key, MAX_KEY, "k");
    double t0 = now_sec();
    for (long i = 0; i < N; i++) wal_append(w, &r);
    double dt = now_sec() - t0;
    wal_close(w);
    unlink(path);
    printf("  wal throughput     : %.0f durable ops/sec  (%ld fsync'd appends in %.3fs, %.2f ms/fsync)\n",
           N / dt, N, dt, dt * 1000.0 / N);
}

static void bench_memory(void) {
    const long N = 100000;
    long rss0 = read_vmrss_kb();
    gcounter_t *arr = malloc((size_t)N * sizeof(gcounter_t));
    if (!arr) { printf("  memory footprint   : ERROR allocating\n"); return; }
    for (long i = 0; i < N; i++) { gcounter_init(&arr[i], 0, 2); gcounter_increment(&arr[i]); }
    long rss1 = read_vmrss_kb();
    printf("  memory footprint   : %.1f MB for %ld G-Counter keys (%zu bytes/key)\n",
           (rss1 - rss0) / 1024.0, N, sizeof(gcounter_t));
    free(arr);
}

// --- sync latency: drive two real node processes -----------------------------

static pid_t spawn_node(const char *id, const char *port, const char *peer) {
    pid_t pid = fork();
    if (pid == 0) {
        if (!freopen("/dev/null", "w", stderr)) { /* ignore */ }
        execl("./convergence", "convergence",
              "--id", id, "--port", port, "--peer", peer, "--nodes", "2", (char *)NULL);
        _exit(127);
    }
    return pid;
}

static int send_cmd(int port, const char *cmd, char *out, size_t cap) {
    int fd = net_connect("127.0.0.1", port);
    if (fd < 0) return -1;
    if (net_send_msg(fd, MSG_CLI_CMD, (const uint8_t *)cmd, (uint32_t)strlen(cmd) + 1) < 0) {
        close(fd); return -1;
    }
    uint8_t type; uint32_t len; uint8_t buf[64];
    if (net_recv_msg(fd, &type, buf, &len, sizeof(buf)) < 0) { close(fd); return -1; }
    close(fd);
    buf[sizeof(buf) - 1] = '\0';
    snprintf(out, cap, "%s", (char *)buf);
    return 0;
}

static void bench_sync_latency(void) {
    pid_t n0 = spawn_node("0", "7400", "127.0.0.1:7401");
    pid_t n1 = spawn_node("1", "7401", "127.0.0.1:7400");
    sleep(1); // let them connect

    char resp[64];
    double t0 = now_sec();
    if (send_cmd(7400, "gctr inc latkey", resp, sizeof(resp)) != 0) {
        printf("  sync latency       : ERROR writing to node 0\n");
        goto done;
    }
    // Poll node 1 every 10ms until the write is observed.
    double latency = -1;
    for (int i = 0; i < 600; i++) {
        usleep(10000);
        if (send_cmd(7401, "gctr get latkey", resp, sizeof(resp)) == 0 && atoi(resp) >= 1) {
            latency = now_sec() - t0;
            break;
        }
    }
    if (latency < 0) printf("  sync latency       : did not converge within 6s\n");
    else printf("  sync latency       : %.0f ms  (write on node 0 -> read on node 1)\n",
                latency * 1000.0);
done:
    kill(n0, SIGKILL); kill(n1, SIGKILL);
    waitpid(n0, NULL, 0); waitpid(n1, NULL, 0);
}

int main(void) {
    printf("Convergence benchmarks\n");
    printf("----------------------\n");
    bench_write_throughput();
    bench_wal_throughput();
    bench_memory();
    bench_sync_latency();
    return 0;
}

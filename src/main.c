// main.c — node entry point: parse args, start listener + gossip threads.
//
// Threads: main (this) + one listener (accept loop) + one reader per inbound
// connection + one gossip thread. All node-state access goes through node->lock.
#include "node.h"
#include "net.h"
#include "wal.h"

#include <sys/socket.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct { node_t *node; int fd; } conn_arg_t;

// Per-connection reader: dispatch framed messages until the peer closes.
static void *reader_thread(void *arg) {
    conn_arg_t *ca = (conn_arg_t *)arg;
    node_t *node = ca->node;
    int fd = ca->fd;
    free(ca);

    uint8_t *buf = malloc(STATE_BUF_CAP);
    if (!buf) { close(fd); return NULL; }

    for (;;) {
        uint8_t type; uint32_t len;
        if (net_recv_msg(fd, &type, buf, &len, STATE_BUF_CAP) < 0) break;

        if (type == MSG_STATE_SYNC) {
            pthread_mutex_lock(&node->lock);
            int dropped = node->partitioned;
            pthread_mutex_unlock(&node->lock);
            if (!dropped) node_deserialize_merge(node, buf, len);
        } else if (type == MSG_CLI_CMD) {
            char cmd[512];
            uint32_t cl = len < sizeof(cmd) - 1 ? len : sizeof(cmd) - 1;
            memcpy(cmd, buf, cl); cmd[cl] = '\0';
            char resp[1024];
            node_handle_cli(node, cmd, resp, sizeof(resp));
            net_send_msg(fd, MSG_CLI_RESP, (uint8_t *)resp, (uint32_t)strlen(resp) + 1);
        } else if (type == MSG_HEARTBEAT) {
            // Liveness tracking handled on the outbound side for the demo.
        }
    }
    free(buf);
    close(fd);
    return NULL;
}

static void *listener_thread(void *arg) {
    node_t *node = (node_t *)arg;
    int lfd = net_listen(node->port);
    if (lfd < 0) {
        fprintf(stderr, "node %d: failed to listen on port %d\n", node->id, node->port);
        exit(1);
    }
    fprintf(stderr, "node %d: listening on port %d\n", node->id, node->port);

    for (;;) {
        int cfd = accept(lfd, NULL, NULL);
        if (cfd < 0) continue;
        conn_arg_t *ca = malloc(sizeof(*ca));
        ca->node = node; ca->fd = cfd;
        pthread_t t;
        pthread_create(&t, NULL, reader_thread, ca);
        pthread_detach(t);
    }
    return NULL;
}

static void *gossip_thread(void *arg) {
    node_t *node = (node_t *)arg;
    uint8_t *buf = malloc(STATE_BUF_CAP);

    for (;;) {
        sleep(GOSSIP_INTERVAL_SEC);

        pthread_mutex_lock(&node->lock);
        int partitioned = node->partitioned;
        pthread_mutex_unlock(&node->lock);
        if (partitioned) continue; // simulated network split: send nothing

        size_t len = node_serialize(node, buf, STATE_BUF_CAP);
        if (len == 0) continue;

        for (int i = 0; i < node->peer_count; i++) {
            peer_t *p = &node->peers[i];
            if (p->out_fd < 0)
                p->out_fd = net_connect(p->ip, p->port);
            if (p->out_fd < 0) continue;

            uint8_t id = (uint8_t)node->id;
            if (net_send_msg(p->out_fd, MSG_HEARTBEAT, &id, 1) < 0 ||
                net_send_msg(p->out_fd, MSG_STATE_SYNC, buf, (uint32_t)len) < 0) {
                close(p->out_fd);
                p->out_fd = -1; // will redial next cycle
            }
        }
    }
    free(buf);
    return NULL;
}

int main(int argc, char **argv) {
    int id = 0, port = 7000, num_nodes = MAX_NODES;
    const char *wal_path = NULL;
    char peer_ips[MAX_NODES][64];
    int  peer_ports[MAX_NODES];
    int  peer_n = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--id") == 0 && i + 1 < argc) {
            id = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--nodes") == 0 && i + 1 < argc) {
            num_nodes = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--wal") == 0 && i + 1 < argc) {
            wal_path = argv[++i];
        } else if (strcmp(argv[i], "--peer") == 0 && i + 1 < argc) {
            char *spec = argv[++i];                  // "127.0.0.1:7001"
            char *colon = strrchr(spec, ':');
            if (colon && peer_n < MAX_NODES) {
                *colon = '\0';
                snprintf(peer_ips[peer_n], 64, "%s", spec);
                peer_ports[peer_n] = atoi(colon + 1);
                peer_n++;
            }
        }
    }

    // node_t is large (fixed CRDT arrays), so allocate it on the heap, not the stack.
    node_t *node = malloc(sizeof(*node));
    if (!node) { fprintf(stderr, "out of memory\n"); return 1; }
    node_init(node, id, num_nodes, port);
    for (int i = 0; i < peer_n; i++)
        node_add_peer(node, peer_ips[i], peer_ports[i]);

    // Durability: replay any existing WAL, then attach it for future writes.
    if (wal_path) {
        long n = node_replay(node, wal_path);
        if (n < 0) { fprintf(stderr, "node %d: WAL replay failed\n", id); return 1; }
        if (n > 0) fprintf(stderr, "node %d: replayed %ld WAL records\n", id, n);
        wal_t *w = wal_open(wal_path);
        if (!w) { fprintf(stderr, "node %d: could not open WAL %s\n", id, wal_path); return 1; }
        node_set_wal(node, w);
    }

    pthread_t lt, gt;
    pthread_create(&lt, NULL, listener_thread, node);
    pthread_create(&gt, NULL, gossip_thread, node);
    pthread_join(lt, NULL);
    pthread_join(gt, NULL);
    return 0;
}

// wal.c — write-ahead log with fsync-before-apply durability.
//
// On-disk record (big-endian), matching the design doc:
//   [8] seq  [1] crdt_type  [1] op_type  [2] key_len  [key_len bytes key]
//   [4] payload_len  [payload_len bytes payload]
//
// Every append is fsync()ed before the caller applies the op to memory, so a
// crash after the durable write can always be recovered by replay. This is the
// same ordering PostgreSQL's WAL guarantees.
#include "wal.h"

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <arpa/inet.h>

struct wal {
    int      fd;
    uint64_t seq;
};

static int write_all(int fd, const void *buf, size_t n) {
    const uint8_t *p = buf; size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, p + off, n - off);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        off += (size_t)w;
    }
    return 0;
}

// Read exactly n bytes. Returns 1 on success, 0 on clean EOF before any byte,
// -1 on partial/error.
static int read_exact(int fd, void *buf, size_t n) {
    uint8_t *p = buf; size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, p + off, n - off);
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (r == 0) return off == 0 ? 0 : -1;
        off += (size_t)r;
    }
    return 1;
}

wal_t *wal_open(const char *path) {
    int fd = open(path, O_RDWR | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return NULL;
    wal_t *w = calloc(1, sizeof(*w));
    if (!w) { close(fd); return NULL; }
    w->fd = fd;
    w->seq = 0;
    return w;
}

int wal_append(wal_t *w, const wal_record_t *r) {
    uint8_t hdr[16];
    uint8_t *p = hdr;
    uint64_t seq = ++w->seq;
    for (int i = 7; i >= 0; i--) *p++ = (seq >> (i * 8)) & 0xff; // 8 bytes seq
    *p++ = r->crdt_type;
    *p++ = r->op_type;
    uint16_t klen = (uint16_t)strnlen(r->key, MAX_KEY);
    *p++ = (klen >> 8) & 0xff; *p++ = klen & 0xff;              // 2 bytes key_len

    if (write_all(w->fd, hdr, (size_t)(p - hdr)) < 0) return -1;
    if (write_all(w->fd, r->key, klen) < 0) return -1;

    uint8_t plen_be[4];
    uint32_t plen = r->payload_len;
    plen_be[0] = (plen >> 24) & 0xff; plen_be[1] = (plen >> 16) & 0xff;
    plen_be[2] = (plen >> 8) & 0xff;  plen_be[3] = plen & 0xff;
    if (write_all(w->fd, plen_be, 4) < 0) return -1;
    if (plen > 0 && write_all(w->fd, r->payload, plen) < 0) return -1;

    if (fsync(w->fd) < 0) return -1; // block until physically on disk
    return 0;
}

void wal_close(wal_t *w) {
    if (!w) return;
    close(w->fd);
    free(w);
}

long wal_replay(const char *path, wal_apply_fn apply, void *ctx) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return (errno == ENOENT) ? 0 : -1; // no WAL yet is fine
    long count = 0;

    for (;;) {
        uint8_t hdr[12]; // 8 seq + 1 type + 1 op + 2 key_len
        int rc = read_exact(fd, hdr, sizeof(hdr));
        if (rc == 0) break;        // clean EOF
        if (rc < 0) { close(fd); return -1; }

        wal_record_t r;
        memset(&r, 0, sizeof(r));
        r.crdt_type = hdr[8];
        r.op_type   = hdr[9];
        uint16_t klen = ((uint16_t)hdr[10] << 8) | hdr[11];
        if (klen >= MAX_KEY) { close(fd); return -1; }
        if (read_exact(fd, r.key, klen) != 1) { close(fd); return -1; }
        r.key[klen] = '\0';

        uint8_t plen_be[4];
        if (read_exact(fd, plen_be, 4) != 1) { close(fd); return -1; }
        uint32_t plen = ((uint32_t)plen_be[0] << 24) | ((uint32_t)plen_be[1] << 16) |
                        ((uint32_t)plen_be[2] << 8) | plen_be[3];
        if (plen >= sizeof(r.payload)) { close(fd); return -1; }
        if (plen > 0 && read_exact(fd, r.payload, plen) != 1) { close(fd); return -1; }
        r.payload[plen] = '\0';
        r.payload_len = (uint16_t)plen;

        apply(ctx, &r);
        count++;
    }
    close(fd);
    return count;
}

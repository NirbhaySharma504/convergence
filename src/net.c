// net.c — POSIX TCP transport with length-prefixed framing (§7).
//
// We never assume message boundaries align with TCP segments: every send/recv
// loops until the exact number of bytes has been transferred. The 5-byte header
// is [4 bytes payload_len big-endian][1 byte type].
#include "net.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

// Write exactly n bytes. Returns 0 on success, -1 on error.
static int write_all(int fd, const uint8_t *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

// Read exactly n bytes. Returns 0 on success, -1 on error/EOF.
static int read_all(int fd, uint8_t *buf, size_t n) {
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, buf + off, n - off);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1; // peer closed
        off += (size_t)r;
    }
    return 0;
}

int net_listen(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    if (listen(fd, 16) < 0) { close(fd); return -1; }
    return fd;
}

int net_connect(const char *ip, int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) { close(fd); return -1; }

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }

    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    return fd;
}

int net_send_msg(int fd, uint8_t type, const uint8_t *payload, uint32_t len) {
    uint8_t header[5];
    uint32_t be = htonl(len);
    memcpy(header, &be, 4);
    header[4] = type;
    if (write_all(fd, header, 5) < 0) return -1;
    if (len > 0 && write_all(fd, payload, len) < 0) return -1;
    return 0;
}

int net_recv_msg(int fd, uint8_t *type, uint8_t *buf, uint32_t *len, uint32_t cap) {
    uint8_t header[5];
    if (read_all(fd, header, 5) < 0) return -1;
    uint32_t be;
    memcpy(&be, header, 4);
    uint32_t plen = ntohl(be);
    if (plen > cap) return -1; // would overflow the caller's buffer
    if (plen > 0 && read_all(fd, buf, plen) < 0) return -1;
    *type = header[4];
    *len = plen;
    return 0;
}

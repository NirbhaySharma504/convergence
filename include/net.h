// net.h — TCP transport with length-prefixed framing (§7).
#ifndef CONVERGENCE_NET_H
#define CONVERGENCE_NET_H

#include "common.h"

// Frame on the wire: [4 bytes payload_len (big-endian)][1 byte type][payload].

int net_listen  (int port);                    // bind+listen, return listen fd or -1
int net_connect (const char *ip, int port);    // connect, return fd or -1

// Write one framed message. Returns 0 on success, -1 on error.
int net_send_msg(int fd, uint8_t type, const uint8_t *payload, uint32_t len);

// Read exactly one framed message into buf (capacity cap).
// On success sets *type and *len and returns 0. Returns -1 on error/EOF.
int net_recv_msg(int fd, uint8_t *type, uint8_t *buf, uint32_t *len, uint32_t cap);

#endif

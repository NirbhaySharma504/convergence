// cli.c — convergence-cli: connect to a running node, send one command, print response.
//
// Usage: convergence-cli --port 7000 gctr inc page_views
#include "net.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    int port = 7000;
    const char *ip = "127.0.0.1";

    // Collect args; everything that isn't --port/--host becomes the command.
    char cmd[512] = {0};
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
            ip = argv[++i];
        } else {
            if (cmd[0]) strncat(cmd, " ", sizeof(cmd) - strlen(cmd) - 1);
            strncat(cmd, argv[i], sizeof(cmd) - strlen(cmd) - 1);
        }
    }
    if (!cmd[0]) {
        fprintf(stderr, "usage: convergence-cli --port N <command...>\n");
        return 2;
    }

    int fd = net_connect(ip, port);
    if (fd < 0) {
        fprintf(stderr, "error: could not connect to %s:%d\n", ip, port);
        return 1;
    }

    if (net_send_msg(fd, MSG_CLI_CMD, (uint8_t *)cmd, (uint32_t)strlen(cmd) + 1) < 0) {
        fprintf(stderr, "error: send failed\n");
        close(fd);
        return 1;
    }

    uint8_t type; uint32_t len;
    uint8_t buf[4096];
    if (net_recv_msg(fd, &type, buf, &len, sizeof(buf)) < 0) {
        fprintf(stderr, "error: no response\n");
        close(fd);
        return 1;
    }
    close(fd);

    if (type == MSG_CLI_RESP) {
        printf("%s\n", (char *)buf);
        return 0;
    }
    fprintf(stderr, "error: unexpected response type %d\n", type);
    return 1;
}

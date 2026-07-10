#include "async.h"
#include "net.h"
#include "staticdb2/sdb2.h"

#include <poll.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/socket.h>
#include <errno.h>

/* Poll timeout in milliseconds (30 s is generous for a local/LAN DB2 server). */
#define ASYNC_POLL_TIMEOUT_MS 30000

int sdb2_async_polled_send(void *ctx, const uint8_t *data, size_t len) {
    int fd = (int)(intptr_t)ctx;
    struct pollfd pfd;
    pfd.fd      = fd;
    pfd.events  = POLLOUT;
    pfd.revents = 0;

    while (len > 0) {
        ssize_t n = send(fd, data, len, 0);
        if (n > 0) {
            data += (size_t)n;
            len  -= (size_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (poll(&pfd, 1, ASYNC_POLL_TIMEOUT_MS) <= 0) return SDB2_ERR_NET;
            continue;
        }
        return SDB2_ERR_NET;
    }
    return SDB2_OK;
}

int sdb2_async_polled_recv(void *ctx, uint8_t *buf, size_t len, size_t *nread) {
    int fd = (int)(intptr_t)ctx;
    struct pollfd pfd;
    pfd.fd      = fd;
    pfd.events  = POLLIN;
    pfd.revents = 0;

    while (1) {
        ssize_t n = recv(fd, buf, len, 0);
        if (n > 0) {
            *nread = (size_t)n;
            return SDB2_OK;
        }
        if (n == 0) return SDB2_ERR_NET; /* peer closed */
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (poll(&pfd, 1, ASYNC_POLL_TIMEOUT_MS) <= 0) return SDB2_ERR_NET;
            continue;
        }
        return SDB2_ERR_NET;
    }
}

int sdb2_async_configure(drda_conn_t *conn) {
    int r = net_set_nonblocking(conn->fd);
    if (r != SDB2_OK) return r;
    conn->io_send = sdb2_async_polled_send;
    conn->io_recv = sdb2_async_polled_recv;
    /* io_ctx already holds (void*)(intptr_t)conn->fd from drda_connect(). */
    return SDB2_OK;
}

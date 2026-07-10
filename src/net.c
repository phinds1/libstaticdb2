#include "net.h"
#include "staticdb2/sdb2.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int net_connect(const char *host, int port) {
    char portstr[12];
    snprintf(portstr, sizeof(portstr), "%d", port);

    struct addrinfo hints, *res, *rp;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host, portstr, &hints, &res);
    if (rc != 0) return SDB2_ERR_NET;

    int fd = SDB2_ERR_NET;
    for (rp = res; rp != NULL; rp = rp->ai_next) {
        int s = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (s < 0) continue;
        if (connect(s, rp->ai_addr, rp->ai_addrlen) == 0) {
            /* 30-second recv/send timeout so operations don't block forever. */
            struct timeval tv = {30, 0};
            setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
            setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            fd = s;
            break;
        }
        close(s);
    }
    freeaddrinfo(res);
    return fd;
}

int net_send_all(int fd, const uint8_t *data, size_t len) {
    while (len > 0) {
        ssize_t n = send(fd, data, len, 0);
        if (n <= 0) return SDB2_ERR_NET;
        data += (size_t)n;
        len  -= (size_t)n;
    }
    return SDB2_OK;
}

int net_recv_some(int fd, uint8_t *buf, size_t len, size_t *nread) {
    ssize_t n = recv(fd, buf, len, 0);
    if (n < 0)  return SDB2_ERR_NET;
    if (n == 0) return SDB2_NEED_MORE;  /* clean EOF */
    *nread = (size_t)n;
    return SDB2_OK;
}

void net_close(int fd) {
    if (fd >= 0) close(fd);
}

int net_set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return SDB2_ERR_NET;
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) return SDB2_ERR_NET;
    return SDB2_OK;
}

/* Vtable-compatible blocking send (ctx = (void*)(intptr_t)fd). */
int net_blocking_send(void *ctx, const uint8_t *data, size_t len) {
    return net_send_all((int)(intptr_t)ctx, data, len);
}

/* Vtable-compatible blocking recv (ctx = (void*)(intptr_t)fd). */
int net_blocking_recv(void *ctx, uint8_t *buf, size_t len, size_t *nread) {
    return net_recv_some((int)(intptr_t)ctx, buf, len, nread);
}

/* Non-blocking send — returns SDB2_NEED_WRITE on EAGAIN or partial write. */
int net_nb_send(void *ctx, const uint8_t *data, size_t len) {
    int fd = (int)(intptr_t)ctx;
    ssize_t n = send(fd, data, len, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return SDB2_NEED_WRITE;
        return SDB2_ERR_NET;
    }
    if ((size_t)n < len) return SDB2_NEED_WRITE; /* partial write */
    return SDB2_OK;
}

/* Non-blocking recv — returns SDB2_NEED_MORE on EAGAIN. */
int net_nb_recv(void *ctx, uint8_t *buf, size_t len, size_t *nread) {
    int fd = (int)(intptr_t)ctx;
    ssize_t n = recv(fd, buf, len, 0);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return SDB2_NEED_MORE;
        return SDB2_ERR_NET;
    }
    if (n == 0) return SDB2_ERR_NET; /* peer closed */
    *nread = (size_t)n;
    return SDB2_OK;
}

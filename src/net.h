#ifndef SDB2_NET_H
#define SDB2_NET_H

/*
 * net.h — blocking TCP socket adapter.
 *
 * This is the ONLY place in the library that performs blocking I/O.
 * Protocol parsing code must never call net_recv_some directly — it
 * consumes bytes from a buf_t and returns SDB2_NEED_MORE when short.
 * The blocking adapter fills that buffer; the non-blocking (nginx) adapter
 * feeds it from an event loop (see Phase 14).
 */

#include <stdint.h>
#include <stddef.h>

/*
 * Connect to host:port over TCP.
 * Returns a socket file descriptor (>= 0) on success, SDB2_ERR_NET on failure.
 */
int  net_connect(const char *host, int port);

/*
 * Send exactly len bytes.  Loops over partial writes.
 * Returns SDB2_OK or SDB2_ERR_NET.
 */
int  net_send_all(int fd, const uint8_t *data, size_t len);

/*
 * Receive up to len bytes into buf; writes actual count to *nread.
 * Returns SDB2_OK, SDB2_ERR_NET on error, or SDB2_NEED_MORE on EOF.
 */
int  net_recv_some(int fd, uint8_t *buf, size_t len, size_t *nread);

/* Close the socket (safe to call with fd < 0 — no-op). */
void net_close(int fd);

/*
 * Set O_NONBLOCK on fd.
 * Returns SDB2_OK or SDB2_ERR_NET.
 */
int net_set_nonblocking(int fd);

/*
 * Vtable-compatible blocking I/O (ctx = (void*)(intptr_t)fd).
 * These are the defaults installed by drda_connect().
 */
int net_blocking_send(void *ctx, const uint8_t *data, size_t len);
int net_blocking_recv(void *ctx, uint8_t *buf, size_t len, size_t *nread);

/*
 * Vtable-compatible non-blocking I/O (ctx = (void*)(intptr_t)fd).
 * net_nb_send: returns SDB2_NEED_WRITE on EAGAIN/EWOULDBLOCK or partial write.
 * net_nb_recv: returns SDB2_NEED_MORE  on EAGAIN/EWOULDBLOCK.
 * For nginx: install these callbacks and handle NEED_WRITE/NEED_MORE in your
 * fd event handler — re-arm the event and retry the DRDA operation.
 */
int net_nb_send(void *ctx, const uint8_t *data, size_t len);
int net_nb_recv(void *ctx, uint8_t *buf, size_t len, size_t *nread);

#endif /* SDB2_NET_H */

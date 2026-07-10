#ifndef SDB2_ASYNC_H
#define SDB2_ASYNC_H

/*
 * async.h — Non-blocking / nginx-style adapter for drda_conn_t.
 *
 * QUICK START
 * -----------
 *   conn = drda_connect(...);          // blocking connect, blocking vtable
 *   sdb2_async_configure(conn);        // switch to O_NONBLOCK + poll()-retrying vtable
 *   drda_exec_immediate(conn, sql, &n); // works as before, but I/O is non-blocking
 *
 * NGINX USAGE CONTRACT
 * --------------------
 * 1. Call drda_connect() (or open the socket yourself) to get a drda_conn_t.
 * 2. Install your own I/O callbacks:
 *      conn->io_send = my_ngx_send;   // enqueue to nginx write buffer chain;
 *                                     // return SDB2_OK if all bytes accepted,
 *                                     // SDB2_NEED_WRITE if the chain is full
 *      conn->io_recv = my_ngx_recv;   // drain nginx read buffer into buf;
 *                                     // return SDB2_OK + *nread on success,
 *                                     // SDB2_NEED_MORE if no bytes available yet
 *      conn->io_ctx  = r;             // your ngx_http_request_t * (or similar)
 * 3. Call drda_* functions from your nginx read/write event handlers.
 *    On SDB2_NEED_MORE  → re-arm POLLIN  and retry from the same DRDA call.
 *    On SDB2_NEED_WRITE → re-arm POLLOUT and retry from the same DRDA call.
 *    On SDB2_OK         → advance to the next DRDA call.
 * 4. The library never calls blocking recv() or send(); all I/O routes through
 *    your callbacks.  No global state — safe for multi-worker nginx processes.
 *
 * DESIGN NOTES
 * ------------
 * drda_conn_t.io_send / io_recv are the single I/O seam.  Protocol parsing
 * code reads only from conn->recv_buf (a buf_t) and returns SDB2_NEED_MORE
 * when short; drda_recv_n() fills that buffer by calling io_recv().
 * drda_flush() serialises conn->send_buf to the wire via io_send().
 */

#include "drda.h"

/*
 * Configure conn for non-blocking I/O using poll()-based retry.
 *
 * Sets O_NONBLOCK on conn->fd and installs sdb2_async_polled_send /
 * sdb2_async_polled_recv as the I/O callbacks.  The DRDA API continues to
 * appear synchronous (poll() blocks inside the callbacks until I/O completes),
 * but the underlying socket is non-blocking — proving the non-blocking path
 * works and providing a useful adapter for thread-per-connection nginx workers.
 *
 * Returns SDB2_OK or SDB2_ERR_NET.
 */
int sdb2_async_configure(drda_conn_t *conn);

/*
 * Non-blocking send with internal poll() retry.
 * ctx = (void*)(intptr_t)fd.
 *
 * Loops: attempt send → on EAGAIN poll(POLLOUT) → retry.
 * Handles partial writes by advancing the data pointer.
 * Exported for callers that want to install it on a conn manually.
 */
int sdb2_async_polled_send(void *ctx, const uint8_t *data, size_t len);

/*
 * Non-blocking recv with internal poll() retry.
 * ctx = (void*)(intptr_t)fd.
 *
 * Loops: attempt recv → on EAGAIN poll(POLLIN) → retry.
 * Exported for callers that want to install it on a conn manually.
 */
int sdb2_async_polled_recv(void *ctx, uint8_t *buf, size_t len, size_t *nread);

#endif /* SDB2_ASYNC_H */

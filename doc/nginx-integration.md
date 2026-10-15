# nginx Integration Guide

This guide explains how to use `libstaticdb2` inside an nginx module,
hooking DB2 I/O into nginx's non-blocking event loop.

---

## How the I/O seam works

Every byte sent to or received from DB2 flows through two function pointers
on the connection object:

```c
typedef struct drda_conn {
    int   fd;          /* socket fd (you own it in nginx mode) */
    void *io_ctx;      /* forwarded opaquely to both callbacks */

    int (*io_send)(void *ctx, const uint8_t *data, size_t len);
    int (*io_recv)(void *ctx, uint8_t *buf,  size_t len, size_t *nread);

    /* … rest of struct … */
} drda_conn_t;
```

No DRDA protocol code calls `send()` / `recv()` directly.
Replacing the two callbacks is sufficient to own all I/O.

Return values your callbacks must use:

| Code            | Meaning                                        |
|-----------------|------------------------------------------------|
| `SDB2_OK`       | All bytes sent / bytes written to `*nread`     |
| `SDB2_NEED_WRITE` | Send buffer full — re-arm `POLLOUT` and retry |
| `SDB2_NEED_MORE`  | No data yet — re-arm `POLLIN` and retry       |
| `SDB2_ERR_NET`  | Unrecoverable error — close connection         |

The library propagates `NEED_MORE` / `NEED_WRITE` all the way back to your
event handler without touching the socket again.

---

## Current entry points

### Blocking (default)

```c
drda_conn_t *conn = drda_connect(host, port, db, user, pass, err, sizeof err);
```

`drda_connect()` calls `net_connect()` (blocking `getaddrinfo` + `connect()`),
installs blocking vtable callbacks, then runs the full DRDA handshake.
Good for batch jobs and CLI tools; **not suitable for nginx workers**.

### Non-blocking with internal `poll()` retry

```c
drda_conn_t *conn = drda_connect(...);
sdb2_async_configure(conn);   /* sets O_NONBLOCK, installs polled vtable */
```

This proves the non-blocking code path works and is useful for
**thread-per-connection** nginx setups (e.g. `ngx_thread_task_post`).
The socket is non-blocking but the library still blocks internally via
`poll()` — it does not yield to the nginx event loop.

### Full nginx event-loop integration *(planned — see below)*

For a true non-blocking nginx module the TCP connect itself must be
non-blocking and owned by nginx.  This requires a new entry point:

```c
drda_conn_t *drda_connect_fd(int fd,
                             const char *db,
                             const char *user, const char *pass,
                             char *errmsg, size_t errmsg_len);
```

`drda_connect_fd()` would skip `net_connect()` entirely, accepting an
already-connected (and optionally `O_NONBLOCK`) file descriptor, install the
blocking vtable by default, then run the DRDA handshake.  The caller swaps
the vtable to nginx callbacks immediately after.

> **Status:** `drda_connect_fd()` is not yet implemented.
> The section [Implementing `drda_connect_fd`](#implementing-drda_connect_fd)
> below describes what the change entails.

---

## Step-by-step nginx integration

### 1. Non-blocking TCP connect

nginx manages the socket.  Create it in the standard nginx way and connect
to DB2's port (default 50000):

```c
/* In your module's connect phase */
ngx_peer_connection_t  pc;
ngx_memzero(&pc, sizeof(pc));
pc.sockaddr    = db2_sockaddr;         /* filled from upstream resolver */
pc.socklen     = db2_socklen;
pc.name        = &db2_server_name;
pc.get         = ngx_event_get_peer;
pc.log         = r->connection->log;

ngx_int_t rc = ngx_event_connect_peer(&pc);
/* rc == NGX_OK   → connected immediately (rare)   */
/* rc == NGX_AGAIN → in progress; wait for POLLOUT */
```

Store `pc.connection->fd` for use as the DB2 socket.

### 2. Allocate the connection object without connecting

Once the TCP connection is established call `drda_connect_fd()`:

```c
char errmsg[256];
drda_conn_t *db2 = drda_connect_fd(pc.connection->fd,
                                    "MYDB", "db2inst1", "secret",
                                    errmsg, sizeof errmsg);
if (!db2) { /* handle error */ }
```

The DRDA handshake (EXCSAT → ACCSEC → SECCHK → ACCRDB) runs over the fd
using the blocking vtable.  For a true non-blocking handshake you would
install the nginx callbacks **before** the handshake, but that requires the
DRDA state machine to be re-entrant (future work).

Practical shortcut for most nginx deployments: run `drda_connect_fd()` in an
`ngx_thread_task_post` worker thread, then hand the ready `drda_conn_t *`
back to the event loop.

### 3. Install nginx I/O callbacks

```c
typedef struct {
    ngx_connection_t *c;   /* nginx connection */
    ngx_buf_t        *in;  /* current read buffer   */
    ngx_chain_t      *out; /* current write chain   */
} ngx_db2_io_ctx_t;

/* Send: write to nginx output chain */
static int ngx_db2_send(void *ctx, const uint8_t *data, size_t len) {
    ngx_db2_io_ctx_t *io = ctx;
    /* copy data into an ngx_buf_t and append to io->out */
    ngx_buf_t *b = ngx_create_temp_buf(io->c->pool, len);
    if (!b) return SDB2_ERR_NET;
    ngx_memcpy(b->pos, data, len);
    b->last = b->pos + len;
    /* flush chain; return NEED_WRITE if EAGAIN */
    ssize_t n = io->c->send(io->c, b->pos, len);
    if (n == NGX_AGAIN) return SDB2_NEED_WRITE;
    if (n == NGX_ERROR) return SDB2_ERR_NET;
    return SDB2_OK;
}

/* Recv: drain nginx read buffer */
static int ngx_db2_recv(void *ctx, uint8_t *buf, size_t len, size_t *nread) {
    ngx_db2_io_ctx_t *io = ctx;
    ssize_t n = io->c->recv(io->c, buf, len);
    if (n == NGX_AGAIN) return SDB2_NEED_MORE;
    if (n == NGX_ERROR || n == 0) return SDB2_ERR_NET;
    *nread = (size_t)n;
    return SDB2_OK;
}

/* Install */
static ngx_db2_io_ctx_t io_ctx = { .c = pc.connection };
db2->io_ctx  = &io_ctx;
db2->io_send = ngx_db2_send;
db2->io_recv = ngx_db2_recv;
```

### 4. Call DRDA functions from your event handlers

```c
/* nginx read event handler */
static void ngx_db2_read_handler(ngx_event_t *ev) {
    my_request_ctx_t *ctx = ev->data;
    drda_conn_t      *db2 = ctx->db2_conn;

    /* Retry the pending DRDA operation */
    int r = drda_query(db2, ctx->stmt, ctx->sql, NULL);

    if (r == SDB2_NEED_MORE) {
        /* More data needed — nginx will call us again when fd is readable */
        return;
    }
    if (r == SDB2_NEED_WRITE) {
        /* Need to flush — re-arm write event */
        ngx_handle_write_event(ev->data, 0);
        return;
    }
    if (r != SDB2_OK) {
        ngx_log_error(NGX_LOG_ERR, ev->log, 0,
                      "db2 query error: %s", db2->diag_msg);
        /* finalise request with 502 */
        return;
    }

    /* Success — fetch rows */
    sdb2_value_t val;
    while (drda_fetch(db2, ctx->stmt) == SDB2_OK) {
        sdb2_stmt_get_col(ctx->stmt, 0, &val);
        /* ... build response ... */
    }
    ngx_http_finalize_request(ctx->r, NGX_HTTP_OK);
}
```

The key contract: when a DRDA function returns `NEED_MORE` or `NEED_WRITE`,
**call it again with the same arguments** after the fd becomes ready.
The library resumes from where it left off (the internal `recv_buf` retains
its partially-filled state).

### 5. Connection pooling

`drda_conn_t` is not thread-safe.  For nginx:

- One `drda_conn_t` per nginx worker process, **or**
- A simple per-worker free-list pool protected by nginx's single-threaded
  event loop (no locking needed within one worker).

---

## Retry state requirement

The DRDA functions are **not yet fully re-entrant** across a
`NEED_MORE`/`NEED_WRITE` return.  Some functions rebuild the send buffer from
scratch on retry, which is safe.  Others (multi-DSS receive paths such as
`parse_reply_chain`) partially consume `recv_buf` and rely on the buffer
state persisting across calls.

**Practical guidance:**

| DRDA call                | Re-entrant on NEED_MORE? |
|--------------------------|--------------------------|
| `drda_exec_immediate`    | ✅ safe (short send + recv)   |
| `drda_query` (open)      | ✅ safe                       |
| `drda_fetch`             | ⚠️ safe only if recv_buf is not reset between retries |
| `drda_connect_fd` (DRDA handshake) | ❌ not yet — run in a thread |

Always retry the **exact same function pointer + same arguments**.
Do not free or reset any buffers between attempts.

---

## Implementing `drda_connect_fd`

When you are ready to add it, the change is small:

**`src/drda.c`** — extract a new function from `drda_connect()`:

```c
drda_conn_t *drda_connect_fd(int fd,
                              const char *db,
                              const char *user, const char *pass,
                              char *errmsg, size_t errmsg_len)
{
    drda_conn_t *conn = calloc(1, sizeof *conn);
    if (!conn) return NULL;

    conn->fd       = fd;
    conn->io_ctx   = (void *)(intptr_t)fd;
    conn->io_send  = net_blocking_send;
    conn->io_recv  = net_blocking_recv;
    conn->autocommit = 1;

    buf_init(&conn->send_buf);
    buf_init(&conn->recv_buf);

    if (drda_handshake(conn, db, user, pass, errmsg, errmsg_len) != SDB2_OK) {
        drda_disconnect(conn);
        return NULL;
    }
    return conn;
}
```

`drda_connect()` becomes a one-liner:

```c
drda_conn_t *drda_connect(const char *host, int port, ...) {
    int fd = net_connect(host, port);
    if (fd < 0) { /* fill errmsg */ return NULL; }
    return drda_connect_fd(fd, db, user, pass, errmsg, errmsg_len);
}
```

**`src/drda.h`** — add the declaration:

```c
/*
 * Like drda_connect() but uses an already-connected socket fd.
 * The caller is responsible for the TCP connect (blocking or non-blocking).
 * The blocking vtable is installed by default; replace io_send/io_recv/io_ctx
 * after this call to switch to non-blocking I/O.
 */
drda_conn_t *drda_connect_fd(int fd,
                              const char *db,
                              const char *user, const char *pass,
                              char *errmsg, size_t errmsg_len);
```

---

## Summary of work remaining for full nginx integration

| Task | Status |
|------|--------|
| IO vtable (io_send / io_recv / io_ctx) | ✅ Done |
| Non-blocking vtable (`sdb2_async_configure`) | ✅ Done |
| `drda_connect_fd()` — accept pre-connected fd | ❌ Not yet implemented |
| DRDA handshake re-entrant on NEED_MORE | ❌ Needs state machine refactor |
| `drda_fetch` safe across NEED_MORE retries | ⚠️ Partially safe |
| Connection pool helper (free-list per worker) | ❌ Not yet implemented |

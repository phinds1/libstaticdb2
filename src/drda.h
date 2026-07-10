#ifndef SDB2_DRDA_H
#define SDB2_DRDA_H

/*
 * drda.h — DRDA connection object and handshake API.
 *
 * The connection goes through these states in drda_connect():
 *   EXCSAT+ACCSEC → server replies EXCSATRD + ACCSECRD
 *   SECCHK        → server replies SECCHKRM
 *   ACCRDB        → server replies ACCRDBRM  → connection ready
 *
 * Each drda_* function that communicates with the server uses the
 * connection's send_buf (build request) + net_send_all (flush) +
 * recv_exact_into (read response) + local parse logic.
 *
 * Blocking I/O only: net_send_all / net_recv_some are the sole I/O calls.
 */

#include <stdint.h>
#include "buf.h"
#include "staticdb2/sdb2.h"

/* Maximum size of a diagnostic message stored on the connection. */
#define DRDA_DIAG_MSG_LEN  512

typedef struct drda_conn {
    int      fd;            /* socket file descriptor, or -1 if closed */
    buf_t    send_buf;      /* request build buffer */
    buf_t    recv_buf;      /* response accumulation buffer */
    uint16_t corr_id;       /* auto-incrementing correlation id */

    /* Negotiated manager levels (filled by EXCSATRD parse) */
    int mgrlvl_agent;
    int mgrlvl_sqlam;
    int mgrlvl_rdb;
    int mgrlvl_secmgr;

    /* Server product info (filled by ACCRDBRM parse) */
    char srv_prdid[16];

    /* SECMEC accepted by server (filled by ACCSECRD parse) */
    int srv_secmec;

    /* Autocommit mode: 1 = on (RDBCMTOK=F1), 0 = off (RDBCMTOK=F0). */
    int autocommit;

    /* RDBNAM encoded as EBCDIC, 18 bytes, stored during connect for use in PKGNAMCSN */
    uint8_t rdbnam_ebcdic[18];

    /* Last diagnostic: human-readable message + sqlcode */
    char diag_msg[DRDA_DIAG_MSG_LEN];
    int  diag_sqlcode;

    /* Section counter for prepared statements (PKGNAMCSN section field). */
    uint16_t next_section;

    /*
     * I/O vtable — set by drda_connect() (blocking defaults) or by
     * sdb2_async_configure() (non-blocking poll-retrying adapters).
     *
     * For nginx integration, replace with your own callbacks:
     *   conn->io_send = my_ngx_send;   // returns SDB2_NEED_WRITE on EAGAIN
     *   conn->io_recv = my_ngx_recv;   // returns SDB2_NEED_MORE  on EAGAIN
     *   conn->io_ctx  = my_ngx_ctx;    // any context pointer you need
     *
     * The library propagates NEED_MORE/NEED_WRITE back to the caller; your
     * event handler re-arms the fd and retries the DRDA operation.
     *
     * ctx  : opaque context forwarded to both callbacks (default: (void*)(intptr_t)fd)
     * send : send len bytes of data; returns SDB2_OK, SDB2_NEED_WRITE, or SDB2_ERR_NET
     * recv : receive up to len bytes into buf; sets *nread; returns SDB2_OK,
     *        SDB2_NEED_MORE (EAGAIN), or SDB2_ERR_NET
     */
    void *io_ctx;
    int  (*io_send)(void *ctx, const uint8_t *data, size_t len);
    int  (*io_recv)(void *ctx, uint8_t *buf, size_t len, size_t *nread);
} drda_conn_t;

/*
 * Allocate and initialise a connection object.
 * Establishes a TCP connection to host:port, runs the full four-step
 * DRDA handshake (EXCSAT+ACCSEC → SECCHK → ACCRDB).
 *
 * Returns a pointer to the open connection on success,
 * or NULL on failure (sets errno or fills diag on partially-constructed conn).
 * On failure, any partially-allocated connection is freed internally.
 *
 * db       : relational database name (sent as RDBNAM, must be ASCII/EBCDIC-safe)
 * user     : user ID (sent as USRID in SECCHK)
 * pass     : password (sent as PASSWORD in SECCHK — plaintext; SECMEC=USRIDPWD)
 * errmsg   : optional buffer to receive a human-readable failure message on NULL return.
 *            May be NULL if the caller does not need the message.
 * errmsg_len : size of errmsg in bytes (including NUL terminator).
 */
drda_conn_t *drda_connect(const char *host, int port,
                          const char *db,
                          const char *user, const char *pass,
                          char *errmsg, size_t errmsg_len);

/*
 * Close the connection and free all associated memory.
 * Safe to call with conn == NULL.
 */
void drda_disconnect(drda_conn_t *conn);

/*
 * Execute a non-query SQL statement (INSERT / UPDATE / DELETE / DDL) immediately.
 *
 * sql          : NUL-terminated UTF-8 SQL text.
 * row_count    : if non-NULL, receives the number of rows affected (from SQLCA sqlerrd[2]).
 *               For DDL, this is typically 0.
 *
 * Returns SDB2_OK if the statement executed without error (SQLCODE 0).
 * Returns SDB2_ERR_SQL (negative) if the server returned a non-zero SQLCODE;
 * conn->diag_sqlcode is set to the SQLCODE.
 * Returns SDB2_ERR_NET or SDB2_ERR_PROTO on communication failure.
 */
int drda_exec_immediate(drda_conn_t *conn, const char *sql, int32_t *row_count);

/* ---- Phase 9 — query execution ---- */

/*
 * Prepare a SQL statement (PRPSQLSTT + SQLSTT).
 * Assigns stmt->section from the connection's next_section counter.
 * Column descriptors are NOT set here; drda_open_query fills them from QRYDSC.
 *
 * On error, stmt->diag_msg / stmt->diag_sqlcode are set.
 * Returns SDB2_OK or a negative sdb2_err_t.
 */
int drda_prepare(sdb2_stmt_t *stmt, const char *sql);

/*
 * Open a query cursor (OPNQRY).
 * Parses the reply chain: OPNQRYRM → QRYDSC (fills stmt->cols) → first QRYDTA block(s).
 * Stores the QRYINSID from OPNQRYRM in stmt->qryinsid.
 * After return, call drda_fetch() to iterate rows.
 *
 * Returns SDB2_OK, SDB2_ERR_NODATA (empty result), or error.
 */
int drda_open_query(sdb2_stmt_t *stmt);

/*
 * Fetch the next row from an open cursor.
 *
 * *out_row is set to point into stmt->row_buf at the start of this row's
 * column data (after the DA null indicator byte).
 * *out_row_len is the number of bytes for all column values in this row.
 *
 * The pointer remains valid until the next drda_fetch() call.
 * When the current QRYDTA block is exhausted, sends CNTQRY automatically.
 *
 * Returns SDB2_OK (row available), SDB2_ERR_NODATA (cursor exhausted), or error.
 */
int drda_fetch(sdb2_stmt_t *stmt, const uint8_t **out_row, size_t *out_row_len);

/*
 * Close the cursor on the server (CLSQRY) and reset stmt cursor state.
 * Safe to call even if no cursor is open (returns SDB2_OK silently).
 */
void drda_close_cursor(sdb2_stmt_t *stmt);

/* ---- Phase 12 — parameterised execute ---- */

/*
 * Request the input parameter descriptor from the server (DSCSQLSTT).
 * Fills stmt->param_desc on success.  stmt->section must already be set by
 * a prior drda_prepare() call.  On failure, stmt->diag_msg is set.
 * Returns SDB2_OK or a negative sdb2_err_t.
 */
int drda_describe_params(sdb2_stmt_t *stmt);

/*
 * Execute a prepared statement with bound parameters (EXCSQLSTT + SQLDTA).
 *
 * Internally calls drda_describe_params() the first time (if param_desc is
 * not yet populated) to know the exact wire encoding for each parameter.
 * If no parameters are bound, sends a bare EXCSQLSTT (no SQLDTA).
 *
 * row_count receives the affected-row count from SQLCA.sqlerrd[2]; may be NULL.
 * Returns SDB2_OK or a negative sdb2_err_t (conn->diag_* set on error).
 */
int drda_execute_prepared(sdb2_stmt_t *stmt, int32_t *row_count);

/* ---- Phase 13 — commit and rollback ---- */

/*
 * Send RDBCMM (commit) to the server; parse ENDUOWRM reply.
 * Returns SDB2_OK or negative error.
 */
int drda_commit(drda_conn_t *conn);

/*
 * Send RDBRLLBCK (rollback) to the server; parse ENDUOWRM reply.
 * Returns SDB2_OK or negative error.
 */
int drda_rollback(drda_conn_t *conn);

/* ---- Internal helpers used by higher-level phases ---- */

/*
 * Flush conn->send_buf to the socket, then reset the buffer.
 * Returns SDB2_OK or SDB2_ERR_NET.
 */
int drda_flush(drda_conn_t *conn);

/*
 * Read exactly n bytes from the socket, appending to conn->recv_buf.
 * (recv_buf must be in write mode — pos advances, limit == cap.)
 */
int drda_recv_n(drda_conn_t *conn, size_t n);

/*
 * Read one complete DSS frame (header + payload) into conn->recv_buf,
 * then flip the buffer to read mode.
 * Returns SDB2_OK, SDB2_ERR_NET, or SDB2_ERR_PROTO.
 */
int drda_recv_dss(drda_conn_t *conn);

#endif /* SDB2_DRDA_H */

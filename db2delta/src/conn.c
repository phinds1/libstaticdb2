#include "conn.h"
#include "../../include/staticdb2/sdb2.h"
#include <stdio.h>
#include <string.h>

/* ── diagnostics ─────────────────────────────────────────────────────────── */

void conn_print_diag(SQLSMALLINT htype, SQLHANDLE h, const char *ctx) {
    SQLCHAR state[6] = {0}, msg[512] = {0};
    SQLINTEGER native = 0;
    SQLSMALLINT mlen  = 0;
    SQLGetDiagRec(htype, h, 1, state, &native, msg, sizeof msg, &mlen);
    fprintf(stderr, "  [diag %s] state=%.5s native=%d msg=%s\n",
            ctx, state, (int)native, (char *)msg);
}

/* ── open / close ─────────────────────────────────────────────────────────── */

int conn_open(db2delta_conn_t *c, const db2delta_args_t *args) {
    memset(c, 0, sizeof(*c));
    c->henv = SQL_NULL_HANDLE;
    c->hdbc = SQL_NULL_HANDLE;

    SQLRETURN rc;

    rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &c->henv);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        fprintf(stderr, "error: SQLAllocHandle(ENV) failed\n");
        return -1;
    }

    rc = SQLSetEnvAttr(c->henv, SQL_ATTR_ODBC_VERSION,
                       (SQLPOINTER)SQL_OV_ODBC3, 0);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        SQLFreeHandle(SQL_HANDLE_ENV, c->henv); c->henv = SQL_NULL_HANDLE;
        fprintf(stderr, "error: SQLSetEnvAttr(ODBC_VERSION) failed\n");
        return -1;
    }

    rc = SQLAllocHandle(SQL_HANDLE_DBC, c->henv, &c->hdbc);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        SQLFreeHandle(SQL_HANDLE_ENV, c->henv); c->henv = SQL_NULL_HANDLE;
        fprintf(stderr, "error: SQLAllocHandle(DBC) failed\n");
        return -1;
    }

    int port = args->port ? args->port : 50000;
    char cs[1024];
    snprintf(cs, sizeof cs,
             "HOSTNAME=%s;PORT=%d;DATABASE=%s;UID=%s;PWD=%s",
             args->host, port, args->database,
             args->user, args->password);

    rc = SQLDriverConnect(c->hdbc, NULL,
                          (SQLCHAR *)cs, SQL_NTS,
                          NULL, 0, NULL, SQL_DRIVER_NOPROMPT);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        conn_print_diag(SQL_HANDLE_DBC, c->hdbc, "connect");
        conn_close(c);
        return -1;
    }
    return 0;
}

void conn_close(db2delta_conn_t *c) {
    if (!c) return;
    if (c->hdbc != SQL_NULL_HANDLE) {
        SQLDisconnect(c->hdbc);
        SQLFreeHandle(SQL_HANDLE_DBC, c->hdbc);
        c->hdbc = SQL_NULL_HANDLE;
    }
    if (c->henv != SQL_NULL_HANDLE) {
        SQLFreeHandle(SQL_HANDLE_ENV, c->henv);
        c->henv = SQL_NULL_HANDLE;
    }
}

/* ── version query ────────────────────────────────────────────────────────── */

int conn_server_version(db2delta_conn_t *c, char *out, size_t out_sz) {
    SQLHSTMT hstmt = SQL_NULL_HANDLE;
    SQLRETURN rc;

    rc = SQLAllocHandle(SQL_HANDLE_STMT, c->hdbc, &hstmt);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;

    rc = SQLExecDirect(hstmt,
        (SQLCHAR *)"SELECT SERVICE_LEVEL FROM "
                   "TABLE(sysproc.env_get_inst_info()) AS T",
        SQL_NTS);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        conn_print_diag(SQL_HANDLE_STMT, hstmt, "version query");
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    SQLLEN ind = 0;
    rc = SQLBindCol(hstmt, 1, SQL_C_CHAR, out, (SQLLEN)out_sz, &ind);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    rc = SQLFetch(hstmt);
    int result = 0;
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        out[0] = '\0';
        result = -1;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return result;
}

/* ── generic exec (DDL/DML, no result set) ───────────────────────────────── */

int conn_exec(db2delta_conn_t *c, const char *sql) {
    SQLHSTMT hstmt = SQL_NULL_HANDLE;
    SQLRETURN rc;

    rc = SQLAllocHandle(SQL_HANDLE_STMT, c->hdbc, &hstmt);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;

    rc = SQLExecDirect(hstmt, (SQLCHAR *)sql, SQL_NTS);
    int result = 0;
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        conn_print_diag(SQL_HANDLE_STMT, hstmt, sql);
        result = -1;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return result;
}

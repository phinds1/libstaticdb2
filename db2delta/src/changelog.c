#include "changelog.h"
#include "../../include/staticdb2/sql.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── SQL builders (pure, unit-testable) ─────────────────────────────────── */

void changelog_ddl_sql(char *buf, size_t buf_sz) {
    snprintf(buf, buf_sz,
        "CREATE TABLE DBDELTA_CHANGELOG ("
        "  CHANGESET  VARCHAR(255) NOT NULL, "
        "  FILENAME   VARCHAR(255) NOT NULL, "
        "  APPLIED_AT TIMESTAMP    NOT NULL WITH DEFAULT CURRENT TIMESTAMP, "
        "  SHA256     CHAR(64)     NOT NULL, "
        "  AUTHOR     VARCHAR(255), "
        "  PRIMARY KEY (CHANGESET)"
        ")");
}

void changelog_exists_sql(char *buf, size_t buf_sz) {
    /* Check SYSCAT.TABLES in CURRENT SCHEMA (uppercase names) */
    snprintf(buf, buf_sz,
        "SELECT COUNT(*) FROM SYSCAT.TABLES "
        "WHERE TABNAME = 'DBDELTA_CHANGELOG' "
        "  AND TABSCHEMA = CURRENT SCHEMA");
}

void changelog_check_sql(char *buf, size_t buf_sz) {
    snprintf(buf, buf_sz,
        "SELECT COUNT(*) FROM DBDELTA_CHANGELOG WHERE CHANGESET = ?");
}

void changelog_insert_sql(char *buf, size_t buf_sz) {
    snprintf(buf, buf_sz,
        "INSERT INTO DBDELTA_CHANGELOG "
        "(CHANGESET, FILENAME, SHA256, AUTHOR) "
        "VALUES (?, ?, ?, ?)");
}

/* ── helpers ─────────────────────────────────────────────────────────────── */

static int exec_scalar_int(db2delta_conn_t *c, const char *sql, int *out) {
    SQLHSTMT hstmt = SQL_NULL_HANDLE;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, c->hdbc, &hstmt);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;

    rc = SQLExecDirect(hstmt, (SQLCHAR *)sql, SQL_NTS);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        conn_print_diag(SQL_HANDLE_STMT, hstmt, sql);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return -1;
    }

    SQLINTEGER val = 0; SQLLEN ind = 0;
    SQLBindCol(hstmt, 1, SQL_C_LONG, &val, sizeof val, &ind);
    rc = SQLFetch(hstmt);
    *out = (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) ? (int)val : 0;
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return 0;
}

/* ── public API ──────────────────────────────────────────────────────────── */

int changelog_ensure(db2delta_conn_t *c) {
    char exists_sql[512], ddl_sql[1024];
    changelog_exists_sql(exists_sql, sizeof(exists_sql));
    changelog_ddl_sql(ddl_sql, sizeof(ddl_sql));

    int count = 0;
    if (exec_scalar_int(c, exists_sql, &count) != 0) return -1;

    if (count == 0) {
        /* Table does not exist — create it */
        if (conn_exec(c, ddl_sql) != 0) return -1;
    }
    return 0;
}

int changelog_has(db2delta_conn_t *c, const char *changeset, int *out_present) {
    char sql[512];
    changelog_check_sql(sql, sizeof(sql));

    SQLHSTMT hstmt = SQL_NULL_HANDLE;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, c->hdbc, &hstmt);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;

    SQLLEN cs_ind = SQL_NTS;
    rc = SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR,
                          SQL_VARCHAR, 255, 0,
                          (SQLPOINTER)(uintptr_t)changeset,
                          (SQLLEN)strlen(changeset), &cs_ind);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt); return -1;
    }

    rc = SQLExecDirect(hstmt, (SQLCHAR *)sql, SQL_NTS);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        conn_print_diag(SQL_HANDLE_STMT, hstmt, "changelog_has");
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt); return -1;
    }

    SQLINTEGER cnt = 0; SQLLEN cnt_ind = 0;
    SQLBindCol(hstmt, 1, SQL_C_LONG, &cnt, sizeof cnt, &cnt_ind);
    (void)SQLFetch(hstmt);
    *out_present = (cnt > 0) ? 1 : 0;
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return 0;
}

int changelog_record(db2delta_conn_t *c,
                     const char *changeset,
                     const char *filename,
                     const char *sha256_hex,
                     const char *author) {
    char sql[512];
    changelog_insert_sql(sql, sizeof(sql));

    SQLHSTMT hstmt = SQL_NULL_HANDLE;
    SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, c->hdbc, &hstmt);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;

    if (!author) author = "";

    SQLLEN cs_ind  = SQL_NTS, fn_ind = SQL_NTS;
    SQLLEN sha_ind = SQL_NTS, au_ind = SQL_NTS;

    SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 255, 0,
                     (SQLPOINTER)(uintptr_t)changeset, 0, &cs_ind);
    SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 255, 0,
                     (SQLPOINTER)(uintptr_t)filename,  0, &fn_ind);
    SQLBindParameter(hstmt, 3, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR,    64,  0,
                     (SQLPOINTER)(uintptr_t)sha256_hex, 0, &sha_ind);
    SQLBindParameter(hstmt, 4, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 255, 0,
                     (SQLPOINTER)(uintptr_t)author,    0, &au_ind);

    rc = SQLExecDirect(hstmt, (SQLCHAR *)sql, SQL_NTS);
    int result = 0;
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        conn_print_diag(SQL_HANDLE_STMT, hstmt, "changelog_record");
        result = -1;
    }
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return result;
}

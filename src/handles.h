#pragma once
/*
 * handles.h — ODBC handle structs: env / dbc / stmt.
 *
 * sdb2_env_t   : environment (Phase 11 attributes; stub for now)
 * sdb2_dbc_t   : database connection (owns the drda_conn_t)
 * sdb2_stmt_t  : statement (owns column descriptors, row buffer, cursor state)
 *
 * The typedef names (sdb2_env_t etc.) are forward-declared in sdb2.h so that
 * drda.h can reference them without a circular include.  Include THIS header
 * whenever you need the full struct definition.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "drda.h"
#include "fdoca.h"
#include "staticdb2/sdb2.h"

#define STMT_DIAG_MSG_LEN 512

/* Column binding slot (one per SELECT column, set by SQLBindCol). */
typedef struct {
    int    active;    /* 1 = this slot is bound */
    short  c_type;    /* SQL_C_* target type */
    void  *buf;       /* user buffer pointer */
    long   buf_len;   /* user buffer size in bytes */
    long  *ind_ptr;   /* output indicator/length pointer; may be NULL */
} sdb2_col_binding_t;

/* Parameter binding slot (one per ? marker, set by SQLBindParameter). */
typedef struct {
    int          active;
    short        value_type;   /* SQL_C_* source C type */
    short        sql_type;     /* SQL_* target SQL type  */
    unsigned int col_size;     /* precision (DECIMAL) or max length (char/binary) */
    short        dec_digits;   /* decimal scale */
    void        *value_ptr;    /* user value buffer pointer */
    long         buf_len;      /* value buffer size in bytes */
    long        *ind_ptr;      /* length/indicator pointer; may be NULL */
} sdb2_param_binding_t;

struct sdb2_env {
    int odbc_version;  /* SQL_OV_ODBC2 or SQL_OV_ODBC3; 0 = not set */
};

struct sdb2_dbc {
    sdb2_env_t    *env;
    drda_conn_t   *conn;   /* NULL until SQLConnect */
    uint16_t       next_section; /* 1-254, wraps; used by drda_prepare */
    int            autocommit;   /* 0 = off, 1 = on (default) */
    char           diag_msg[512]; /* last connection-level error (e.g. from SQLConnect) */
    int            diag_sqlcode;
};

struct sdb2_stmt {
    sdb2_dbc_t     *dbc;
    uint16_t        section;       /* PKGNAMCSN section number for this stmt */

    /* Column descriptors — filled by drda_open_query from QRYDSC. */
    fdoca_result_t  cols;

    /* Raw QRYDTA row data — concatenation of one QRYDTA block's payload. */
    uint8_t        *row_buf;
    size_t          row_buf_cap;   /* allocated capacity */
    size_t          row_buf_used;  /* valid bytes */
    size_t          row_pos;       /* read position (current row start) */

    bool            eof;           /* ENDQRYRM received; no more server rows */
    bool            cursor_open;   /* OPNQRY sent, not yet closed */
    uint64_t        qryinsid;      /* query instance ID from OPNQRYRM */

    /* Current row pointer — set by drda_fetch; valid until next drda_fetch call. */
    const uint8_t  *cur_row;       /* points into row_buf at current row's col data */
    size_t          cur_row_len;   /* total byte count of col data in current row */

    /* Diagnostics */
    char            diag_msg[STMT_DIAG_MSG_LEN];
    int             diag_sqlcode;

    /* Column bindings (set by SQLBindCol; cleared by SQLFreeStmt(SQL_UNBIND)). */
    sdb2_col_binding_t *bindings;  /* array [0..n_bindings-1]; NULL until first bind */
    int                 n_bindings;

    /* Rows affected by last DML (from drda_exec_immediate); -1 if not applicable. */
    int32_t             row_count;

    /* Prepared statement state (for SQLPrepare + SQLExecute). */
    char               *prepared_sql;  /* strdup'd SQL text; NULL if not prepared */
    int                 is_prepared;   /* drda_prepare was called */
    int                 is_query;      /* SQL begins with SELECT / WITH / VALUES */

    /* Parameter bindings (set by SQLBindParameter; cleared by SQL_RESET_PARAMS). */
    sdb2_param_binding_t *param_bindings;
    int                   n_param_bindings;

    /* Input parameter descriptor (from DSCSQLSTT; cached per prepare cycle). */
    fdoca_result_t        param_desc;

    /* Pending column names from DSCSQLSTT (called during prepare, before OPNQRY).
     * Applied to cols[] after QRYDSC is parsed in drda_open_query. */
    char    pending_col_names[FDOCA_MAX_COLS][FDOCA_COLNAME_MAX];
    int     pending_ncols;
};

/* ---- Lifecycle ---- */
sdb2_env_t  *sdb2_env_alloc(void);
void         sdb2_env_free(sdb2_env_t *env);

sdb2_dbc_t  *sdb2_dbc_alloc(sdb2_env_t *env);
void         sdb2_dbc_free(sdb2_dbc_t *dbc);

/* sdb2_stmt_alloc: allocates a statement bound to dbc.
 * Returns NULL on memory failure. */
sdb2_stmt_t *sdb2_stmt_alloc(sdb2_dbc_t *dbc);
void         sdb2_stmt_free(sdb2_stmt_t *stmt);

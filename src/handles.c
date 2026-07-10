#include "handles.h"
#include <stdlib.h>
#include <string.h>

sdb2_env_t *sdb2_env_alloc(void) {
    return (sdb2_env_t *)calloc(1, sizeof(sdb2_env_t));
}

void sdb2_env_free(sdb2_env_t *env) {
    free(env);
}

sdb2_dbc_t *sdb2_dbc_alloc(sdb2_env_t *env) {
    sdb2_dbc_t *dbc = (sdb2_dbc_t *)calloc(1, sizeof(sdb2_dbc_t));
    if (!dbc) return NULL;
    dbc->env          = env;
    dbc->next_section = 1;
    dbc->autocommit   = 1;  /* autocommit ON by default */
    return dbc;
}

void sdb2_dbc_free(sdb2_dbc_t *dbc) {
    if (!dbc) return;
    drda_disconnect(dbc->conn);
    dbc->conn = NULL;
    free(dbc);
}

sdb2_stmt_t *sdb2_stmt_alloc(sdb2_dbc_t *dbc) {
    sdb2_stmt_t *s = (sdb2_stmt_t *)calloc(1, sizeof(sdb2_stmt_t));
    if (!s) return NULL;
    s->dbc       = dbc;
    s->section   = 0;  /* assigned by drda_prepare */
    s->row_count = -1; /* not applicable until a DML executes */
    return s;
}

void sdb2_stmt_free(sdb2_stmt_t *stmt) {
    if (!stmt) return;
    drda_close_cursor(stmt);     /* close cursor on server if still open */
    fdoca_result_free(&stmt->cols);
    fdoca_result_free(&stmt->param_desc);
    free(stmt->row_buf);
    stmt->row_buf = NULL;
    free(stmt->bindings);
    stmt->bindings = NULL;
    free(stmt->param_bindings);
    stmt->param_bindings = NULL;
    free(stmt->prepared_sql);
    stmt->prepared_sql = NULL;
    free(stmt);
}

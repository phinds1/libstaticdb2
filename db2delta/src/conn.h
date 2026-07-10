#ifndef CONN_H
#define CONN_H

#include "../../include/staticdb2/sql.h"
#include "args.h"

typedef struct {
    SQLHENV henv;
    SQLHDBC hdbc;
} db2delta_conn_t;

/* Connect to DB2 using parsed args.  Returns 0 on success. */
int conn_open(db2delta_conn_t *c, const db2delta_args_t *args);

/* Disconnect and free handles. */
void conn_close(db2delta_conn_t *c);

/* Retrieve server SERVICE_LEVEL string.
 * out_sz must include space for NUL terminator. */
int conn_server_version(db2delta_conn_t *c, char *out, size_t out_sz);

/* Execute a single SQL statement; ignore result set (DDL/DML).
 * Returns 0 on SQL_SUCCESS / SQL_SUCCESS_WITH_INFO. */
int conn_exec(db2delta_conn_t *c, const char *sql);

/* Print diagnostics to stderr.  htype = SQL_HANDLE_DBC or SQL_HANDLE_STMT. */
void conn_print_diag(SQLSMALLINT htype, SQLHANDLE h, const char *ctx);

#endif /* CONN_H */

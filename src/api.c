/*
 * api.c — ODBC 3.x subset public entry points for libstaticdb2.
 *
 * Wires the DRDA / FD:OCA internal layers (Phases 1–10) to the ODBC
 * SQL* function surface defined in include/staticdb2/sql.h.
 *
 * Thread safety: each env/dbc/stmt handle is independent; do not share
 * handles across threads without external locking.
 */

#include "staticdb2/sql.h"
#include "handles.h"
#include "drda.h"
#include "fdoca.h"
#include "sqlca.h"
#include "staticdb2/sdb2.h"

#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp */
#include <stdio.h>
#include <stdint.h>
#include <errno.h>

/* ---- strndup substitute for strict C11 without POSIX feature macros ---- */
static char *sdb2_strndup(const char *s, size_t n) {
    char *p = (char *)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ================================================================== */
/* Internal helpers                                                     */
/* ================================================================== */

/* Map an sdb2_err_t to a 5-char SQLSTATE string. */
static const char *sdb2err_to_sqlstate(int rc) {
    switch (rc) {
    case SDB2_OK:           return "00000";
    case SDB2_ERR_NODATA:   return "02000";
    case SDB2_ERR_NET:      return "08S01";  /* communication link failure */
    case SDB2_ERR_PROTO:    return "08S01";
    case SDB2_ERR_MEM:      return "HY001";  /* memory allocation failure */
    case SDB2_ERR_BADARG:   return "HY009";  /* invalid use of null pointer */
    case SDB2_ERR_TRUNC:    return "22001";  /* string data right-truncated */
    case SDB2_ERR_UNSUPPORT: return "HYC00"; /* optional feature not implemented */
    case SDB2_ERR_SQL:      return "42000";  /* syntax error / access violation */
    default:                return "HY000";  /* general error */
    }
}

/* Derive SQLSTATE from a DB2 SQLCODE (best-effort subset). */
static const char *sqlcode_to_sqlstate(int sqlcode) {
    if (sqlcode == 0)    return "00000";
    if (sqlcode == 100)  return "02000";
    switch (sqlcode) {
    case -104: return "42601";  /* syntax error */
    case -204: return "42704";  /* undefined object */
    case -206: return "42703";  /* undefined column */
    case -302: return "22001";  /* string data right-truncated */
    case -401: return "42818";  /* incompatible types */
    case -407: return "23000";  /* null violation */
    case -530: return "23503";  /* foreign key violation */
    case -803: return "23505";  /* unique constraint violation */
    case -900: return "08003";  /* connection does not exist */
    default:
        if (sqlcode > 0) return "01000";  /* warning */
        return "42000";                   /* general SQL error */
    }
}

/* Map sdb2_ctype_t to the nearest ODBC SQL_* type. */
static SQLSMALLINT sdb2ctype_to_sql(sdb2_ctype_t t) {
    switch (t) {
    case SDB2_C_BOOL:        return SQL_BIT;
    case SDB2_C_INT16:       return SQL_SMALLINT;
    case SDB2_C_INT32:       return SQL_INTEGER;
    case SDB2_C_INT64:       return SQL_BIGINT;
    case SDB2_C_FLOAT32:     return SQL_REAL;
    case SDB2_C_FLOAT64:     return SQL_DOUBLE;
    case SDB2_C_DECIMAL:     return SQL_DECIMAL;
    case SDB2_C_CHAR:        return SQL_CHAR;
    case SDB2_C_VARCHAR:     return SQL_VARCHAR;
    case SDB2_C_LONG_VARCHAR: return SQL_LONGVARCHAR;
    case SDB2_C_BLOB:        return SQL_LONGVARBINARY;
    case SDB2_C_CLOB:        return SQL_LONGVARCHAR;
    case SDB2_C_BINARY:      return SQL_BINARY;
    case SDB2_C_VARBINARY:   return SQL_VARBINARY;
    case SDB2_C_DATE:        return SQL_DATE;
    case SDB2_C_TIME:        return SQL_TIME;
    case SDB2_C_TIMESTAMP:   return SQL_TIMESTAMP;
    default:                 return SQL_UNKNOWN_TYPE;
    }
}

/*
 * Return true if the trimmed SQL text starts with a query keyword.
 * len < 0 means NUL-terminated.
 */
static int sql_is_query(const char *sql, int len) {
    if (!sql) return 0;
    const char *p = sql;
    const char *end = (len < 0) ? NULL : sql + len;
    /* Skip leading whitespace. */
    while (*p && (end == NULL || p < end) &&
           (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n'))
        p++;
    return (strncasecmp(p, "SELECT", 6) == 0 ||
            strncasecmp(p, "WITH",   4) == 0 ||
            strncasecmp(p, "VALUES", 6) == 0);
}

/*
 * Copy a conn-level diagnostic to a stmt so SQLGetDiagRec(STMT,...) works
 * after a drda_exec_immediate failure.
 */
static void sync_conn_diag_to_stmt(sdb2_stmt_t *stmt) {
    if (!stmt || !stmt->dbc || !stmt->dbc->conn) return;
    const drda_conn_t *c = stmt->dbc->conn;
    strncpy(stmt->diag_msg, c->diag_msg, STMT_DIAG_MSG_LEN - 1);
    stmt->diag_msg[STMT_DIAG_MSG_LEN - 1] = '\0';
    stmt->diag_sqlcode = c->diag_sqlcode;
}

/*
 * Copy an sdb2_value_t into a caller-supplied C buffer.
 * c_type: SQL_C_* target type (SQL_C_DEFAULT = natural type).
 * Returns SQL_SUCCESS, SQL_SUCCESS_WITH_INFO (truncation), or SQL_ERROR.
 */
static SQLRETURN copy_value_to_c_buf(const sdb2_value_t *val,
                                     SQLSMALLINT c_type,
                                     SQLPOINTER buf, SQLLEN buf_len,
                                     SQLLEN *ind_ptr) {
    if (val->is_null) {
        if (ind_ptr) *ind_ptr = SQL_NULL_DATA;
        return SQL_SUCCESS;
    }

    /* Resolve SQL_C_DEFAULT to a natural type. */
    SQLSMALLINT ct = c_type;
    if (ct == SQL_C_DEFAULT) {
        switch (val->type) {
        case SDB2_C_BOOL:      ct = SQL_C_BIT;    break;
        case SDB2_C_INT16:     ct = SQL_C_SHORT;  break;
        case SDB2_C_INT32:     ct = SQL_C_LONG;   break;
        case SDB2_C_INT64:     ct = SQL_C_SBIGINT; break;
        case SDB2_C_FLOAT32:   ct = SQL_C_FLOAT;  break;
        case SDB2_C_FLOAT64:   ct = SQL_C_DOUBLE; break;
        case SDB2_C_BINARY:
        case SDB2_C_VARBINARY:
        case SDB2_C_BLOB:      ct = SQL_C_BINARY; break;
        case SDB2_C_CLOB:      ct = SQL_C_CHAR;   break;
        default:               ct = SQL_C_CHAR;   break;
        }
    }

    char tmp[64];
    SQLRETURN ret = SQL_SUCCESS;

    switch (ct) {

    case SQL_C_CHAR: {
        const char *s;
        int slen;
        switch (val->type) {
        case SDB2_C_BOOL:
            slen = snprintf(tmp, sizeof tmp, "%d", (int)val->v.b);
            s = tmp; break;
        case SDB2_C_INT16:
            slen = snprintf(tmp, sizeof tmp, "%d", (int)val->v.i16);
            s = tmp; break;
        case SDB2_C_INT32:
            slen = snprintf(tmp, sizeof tmp, "%d", val->v.i32);
            s = tmp; break;
        case SDB2_C_INT64:
            slen = snprintf(tmp, sizeof tmp, "%lld", (long long)val->v.i64);
            s = tmp; break;
        case SDB2_C_FLOAT32:
            slen = snprintf(tmp, sizeof tmp, "%.7g", (double)val->v.f32);
            s = tmp; break;
        case SDB2_C_FLOAT64:
            slen = snprintf(tmp, sizeof tmp, "%.15g", val->v.f64);
            s = tmp; break;
        default:
            s = val->v.str.data; slen = val->v.str.len; break;
        }
        if (buf && buf_len > 0) {
            SQLLEN copy = (slen < (int)(buf_len - 1)) ? slen : (int)(buf_len - 1);
            memcpy(buf, s, (size_t)copy);
            ((char *)buf)[copy] = '\0';
            if (slen >= (int)buf_len) ret = SQL_SUCCESS_WITH_INFO;
        }
        if (ind_ptr) *ind_ptr = (SQLLEN)slen;
        break;
    }

    case SQL_C_BIT:
    case SQL_C_SHORT: {
        SQLSMALLINT v;
        switch (val->type) {
        case SDB2_C_BOOL:    v = (SQLSMALLINT)val->v.b;   break;
        case SDB2_C_INT16:   v = val->v.i16; break;
        case SDB2_C_INT32:   v = (SQLSMALLINT)val->v.i32; break;
        case SDB2_C_INT64:   v = (SQLSMALLINT)val->v.i64; break;
        case SDB2_C_FLOAT32: v = (SQLSMALLINT)val->v.f32; break;
        case SDB2_C_FLOAT64: v = (SQLSMALLINT)val->v.f64; break;
        default: v = (SQLSMALLINT)atoi(val->v.str.data);  break;
        }
        if (buf) *(SQLSMALLINT *)buf = v;
        if (ind_ptr) *ind_ptr = (SQLLEN)sizeof(SQLSMALLINT);
        break;
    }

    case SQL_C_LONG: {
        SQLINTEGER v;
        switch (val->type) {
        case SDB2_C_BOOL:    v = (SQLINTEGER)val->v.b;   break;
        case SDB2_C_INT16:   v = (SQLINTEGER)val->v.i16; break;
        case SDB2_C_INT32:   v = val->v.i32; break;
        case SDB2_C_INT64:   v = (SQLINTEGER)val->v.i64; break;
        case SDB2_C_FLOAT32: v = (SQLINTEGER)val->v.f32; break;
        case SDB2_C_FLOAT64: v = (SQLINTEGER)val->v.f64; break;
        default: v = atoi(val->v.str.data); break;
        }
        if (buf) *(SQLINTEGER *)buf = v;
        if (ind_ptr) *ind_ptr = (SQLLEN)sizeof(SQLINTEGER);
        break;
    }

    case SQL_C_SBIGINT: {
        SQLBIGINT v;
        switch (val->type) {
        case SDB2_C_BOOL:    v = (SQLBIGINT)val->v.b;   break;
        case SDB2_C_INT16:   v = (SQLBIGINT)val->v.i16; break;
        case SDB2_C_INT32:   v = (SQLBIGINT)val->v.i32; break;
        case SDB2_C_INT64:   v = val->v.i64; break;
        case SDB2_C_FLOAT32: v = (SQLBIGINT)val->v.f32; break;
        case SDB2_C_FLOAT64: v = (SQLBIGINT)val->v.f64; break;
        default: v = (SQLBIGINT)atoll(val->v.str.data); break;
        }
        if (buf) *(SQLBIGINT *)buf = v;
        if (ind_ptr) *ind_ptr = (SQLLEN)sizeof(SQLBIGINT);
        break;
    }

    case SQL_C_FLOAT: {
        SQLREAL v;
        switch (val->type) {
        case SDB2_C_BOOL:    v = (SQLREAL)val->v.b;   break;
        case SDB2_C_INT16:   v = (SQLREAL)val->v.i16; break;
        case SDB2_C_INT32:   v = (SQLREAL)val->v.i32; break;
        case SDB2_C_INT64:   v = (SQLREAL)val->v.i64; break;
        case SDB2_C_FLOAT32: v = val->v.f32; break;
        case SDB2_C_FLOAT64: v = (SQLREAL)val->v.f64; break;
        default: v = (SQLREAL)atof(val->v.str.data); break;
        }
        if (buf) *(SQLREAL *)buf = v;
        if (ind_ptr) *ind_ptr = (SQLLEN)sizeof(SQLREAL);
        break;
    }

    case SQL_C_DOUBLE: {
        SQLDOUBLE v;
        switch (val->type) {
        case SDB2_C_BOOL:    v = (SQLDOUBLE)val->v.b;   break;
        case SDB2_C_INT16:   v = (SQLDOUBLE)val->v.i16; break;
        case SDB2_C_INT32:   v = (SQLDOUBLE)val->v.i32; break;
        case SDB2_C_INT64:   v = (SQLDOUBLE)val->v.i64; break;
        case SDB2_C_FLOAT32: v = (SQLDOUBLE)val->v.f32; break;
        case SDB2_C_FLOAT64: v = val->v.f64; break;
        default: v = atof(val->v.str.data); break;
        }
        if (buf) *(SQLDOUBLE *)buf = v;
        if (ind_ptr) *ind_ptr = (SQLLEN)sizeof(SQLDOUBLE);
        break;
    }

    case SQL_C_BINARY: {
        const char *s = val->v.str.data;
        int slen      = val->v.str.len;
        if (buf && buf_len > 0) {
            SQLLEN copy = ((SQLLEN)slen < buf_len) ? (SQLLEN)slen : buf_len;
            memcpy(buf, s, (size_t)copy);
            if ((SQLLEN)slen > buf_len) ret = SQL_SUCCESS_WITH_INFO;
        }
        if (ind_ptr) *ind_ptr = (SQLLEN)slen;
        break;
    }

    default:
        return SQL_ERROR;
    }

    return ret;
}

/* Parse "HOST:PORT/DATABASE" from an ODBC ServerName string. */
static void parse_server_name(const char *sn, int sn_len,
                               char *host, size_t host_cap,
                               int *port,
                               char *db, size_t db_cap) {
    char buf[512];
    int n = (sn_len == SQL_NTS || sn_len < 0) ? (int)strlen(sn) : sn_len;
    if ((size_t)n >= sizeof buf) n = (int)sizeof buf - 1;
    memcpy(buf, sn, (size_t)n);
    buf[n] = '\0';

    *port   = 446;  /* DB2 default */
    host[0] = '\0';
    db[0]   = '\0';

    char *slash = strchr(buf, '/');
    char *colon = strchr(buf, ':');

    if (slash) {
        *slash = '\0';
        snprintf(db, db_cap, "%s", slash + 1);
    }

    if (colon && (!slash || colon < slash)) {
        *colon = '\0';
        int p   = atoi(colon + 1);
        if (p > 0) *port = p;
    }

    snprintf(host, host_cap, "%s", buf);
}

/* Parse "KEY=VAL;..." connection string from SQLDriverConnect. */
static void parse_connection_string(const char *cs, int cs_len,
                                    char *host, size_t host_cap,
                                    int  *port,
                                    char *db,   size_t db_cap,
                                    char *uid,  size_t uid_cap,
                                    char *pwd,  size_t pwd_cap) {
    char buf[2048];
    int n = (cs_len == SQL_NTS || cs_len < 0) ? (int)strlen(cs) : cs_len;
    if ((size_t)n >= sizeof buf) n = (int)sizeof buf - 1;
    memcpy(buf, cs, (size_t)n);
    buf[n] = '\0';

    *port   = 446;
    host[0] = db[0] = uid[0] = pwd[0] = '\0';

    char *tok = buf;
    while (tok && *tok) {
        char *semi = strchr(tok, ';');
        if (semi) *semi = '\0';

        char *eq = strchr(tok, '=');
        if (eq) {
            *eq = '\0';
            const char *key = tok;
            const char *val = eq + 1;
            if (strcasecmp(key, "HOSTNAME") == 0 || strcasecmp(key, "SERVER") == 0)
                snprintf(host, host_cap, "%s", val);
            else if (strcasecmp(key, "PORT") == 0) {
                int p = atoi(val);
                if (p > 0) *port = p;
            } else if (strcasecmp(key, "DATABASE") == 0 || strcasecmp(key, "DB") == 0)
                snprintf(db, db_cap, "%s", val);
            else if (strcasecmp(key, "UID") == 0 || strcasecmp(key, "USERNAME") == 0)
                snprintf(uid, uid_cap, "%s", val);
            else if (strcasecmp(key, "PWD") == 0 || strcasecmp(key, "PASSWORD") == 0)
                snprintf(pwd, pwd_cap, "%s", val);
        }
        tok = semi ? (semi + 1) : NULL;
    }
}

/* Perform the DRDA connect given already-parsed parameters; sets dbc diag on failure. */
static SQLRETURN do_connect(sdb2_dbc_t *dbc,
                            const char *host, int port,
                            const char *db, const char *uid, const char *pwd) {
    if (!host || !*host) {
        snprintf(dbc->diag_msg, sizeof dbc->diag_msg, "HOSTNAME not specified");
        return SQL_ERROR;
    }
    if (!db || !*db) {
        snprintf(dbc->diag_msg, sizeof dbc->diag_msg, "DATABASE not specified");
        return SQL_ERROR;
    }
    dbc->diag_msg[0]  = '\0';
    dbc->diag_sqlcode = 0;

    dbc->conn = drda_connect(host, port, db,
                             uid  ? uid  : "",
                             pwd  ? pwd  : "",
                             dbc->diag_msg, sizeof dbc->diag_msg);
    if (!dbc->conn) {
        if (!dbc->diag_msg[0])
            snprintf(dbc->diag_msg, sizeof dbc->diag_msg,
                     "Connection to %s:%d failed", host, port);
        return SQL_ERROR;
    }
    return SQL_SUCCESS;
}

/* ================================================================== */
/* Handle management                                                    */
/* ================================================================== */

SQLRETURN SQLAllocHandle(SQLSMALLINT HandleType, SQLHANDLE InputHandle,
                         SQLHANDLE *OutputHandlePtr) {
    if (!OutputHandlePtr) return SQL_ERROR;

    switch (HandleType) {
    case SQL_HANDLE_ENV:
        *OutputHandlePtr = (SQLHANDLE)sdb2_env_alloc();
        return *OutputHandlePtr ? SQL_SUCCESS : SQL_ERROR;

    case SQL_HANDLE_DBC: {
        sdb2_env_t *env = (sdb2_env_t *)InputHandle;
        if (!env) return SQL_INVALID_HANDLE;
        *OutputHandlePtr = (SQLHANDLE)sdb2_dbc_alloc(env);
        return *OutputHandlePtr ? SQL_SUCCESS : SQL_ERROR;
    }

    case SQL_HANDLE_STMT: {
        sdb2_dbc_t *dbc = (sdb2_dbc_t *)InputHandle;
        if (!dbc) return SQL_INVALID_HANDLE;
        *OutputHandlePtr = (SQLHANDLE)sdb2_stmt_alloc(dbc);
        return *OutputHandlePtr ? SQL_SUCCESS : SQL_ERROR;
    }

    default:
        return SQL_ERROR;
    }
}

SQLRETURN SQLFreeHandle(SQLSMALLINT HandleType, SQLHANDLE Handle) {
    if (!Handle) return SQL_INVALID_HANDLE;

    switch (HandleType) {
    case SQL_HANDLE_ENV:  sdb2_env_free((sdb2_env_t *)Handle);  return SQL_SUCCESS;
    case SQL_HANDLE_DBC:  sdb2_dbc_free((sdb2_dbc_t *)Handle);  return SQL_SUCCESS;
    case SQL_HANDLE_STMT: sdb2_stmt_free((sdb2_stmt_t *)Handle); return SQL_SUCCESS;
    default: return SQL_ERROR;
    }
}

/* ================================================================== */
/* Environment attributes                                               */
/* ================================================================== */

SQLRETURN SQLSetEnvAttr(SQLHENV EnvironmentHandle, SQLINTEGER Attribute,
                        SQLPOINTER ValuePtr, SQLINTEGER StringLength) {
    sdb2_env_t *env = (sdb2_env_t *)EnvironmentHandle;
    if (!env) return SQL_INVALID_HANDLE;
    (void)StringLength;

    switch (Attribute) {
    case SQL_ATTR_ODBC_VERSION:
        env->odbc_version = (int)(intptr_t)ValuePtr;
        return SQL_SUCCESS;
    default:
        return SQL_SUCCESS;  /* ignore unknown env attrs */
    }
}

SQLRETURN SQLGetEnvAttr(SQLHENV EnvironmentHandle, SQLINTEGER Attribute,
                        SQLPOINTER ValuePtr, SQLINTEGER BufferLength,
                        SQLINTEGER *StringLengthPtr) {
    sdb2_env_t *env = (sdb2_env_t *)EnvironmentHandle;
    if (!env) return SQL_INVALID_HANDLE;
    (void)BufferLength; (void)StringLengthPtr;

    switch (Attribute) {
    case SQL_ATTR_ODBC_VERSION:
        if (ValuePtr) *(SQLINTEGER *)ValuePtr = (SQLINTEGER)env->odbc_version;
        return SQL_SUCCESS;
    default:
        return SQL_ERROR;
    }
}

/* ================================================================== */
/* Connection attributes                                                */
/* ================================================================== */

SQLRETURN SQLSetConnectAttr(SQLHDBC ConnectionHandle, SQLINTEGER Attribute,
                            SQLPOINTER ValuePtr, SQLINTEGER StringLength) {
    sdb2_dbc_t *dbc = (sdb2_dbc_t *)ConnectionHandle;
    if (!dbc) return SQL_INVALID_HANDLE;
    (void)StringLength;

    switch (Attribute) {
    case SQL_ATTR_AUTOCOMMIT: {
        int new_autocommit = ((intptr_t)ValuePtr == SQL_AUTOCOMMIT_OFF) ? 0 : 1;
        /* When switching from autocommit ON to OFF, issue an explicit COMMIT
         * first.  DB2 may not have committed prior RDBCMTOK=TRUE statements
         * eagerly; an explicit RDBCMM ensures they are committed before the
         * application takes manual control of the transaction boundary.
         * Only do this when actually connected (fd != -1). */
        if (dbc->conn && dbc->conn->fd != -1 &&
            dbc->autocommit == 1 && new_autocommit == 0) {
            int r = drda_commit(dbc->conn);
            if (r != SDB2_OK) return SQL_ERROR;
        }
        dbc->autocommit = new_autocommit;
        /* Sync to connection object if already connected. */
        if (dbc->conn) dbc->conn->autocommit = dbc->autocommit;
        return SQL_SUCCESS;
    }
    default:
        return SQL_SUCCESS;  /* ignore unknown conn attrs */
    }
}

SQLRETURN SQLGetConnectAttr(SQLHDBC ConnectionHandle, SQLINTEGER Attribute,
                            SQLPOINTER ValuePtr, SQLINTEGER BufferLength,
                            SQLINTEGER *StringLengthPtr) {
    sdb2_dbc_t *dbc = (sdb2_dbc_t *)ConnectionHandle;
    if (!dbc) return SQL_INVALID_HANDLE;
    (void)BufferLength; (void)StringLengthPtr;

    switch (Attribute) {
    case SQL_ATTR_AUTOCOMMIT:
        if (ValuePtr)
            *(SQLUINTEGER *)ValuePtr = dbc->autocommit
                                       ? SQL_AUTOCOMMIT_ON
                                       : SQL_AUTOCOMMIT_OFF;
        return SQL_SUCCESS;
    default:
        return SQL_ERROR;
    }
}

/* ================================================================== */
/* Connection                                                           */
/* ================================================================== */

SQLRETURN SQLConnect(SQLHDBC ConnectionHandle,
                     SQLCHAR *ServerName,    SQLSMALLINT NameLength1,
                     const SQLCHAR *UserName,      SQLSMALLINT NameLength2,
                     const SQLCHAR *Authentication, SQLSMALLINT NameLength3) {
    sdb2_dbc_t *dbc = (sdb2_dbc_t *)ConnectionHandle;
    if (!dbc) return SQL_INVALID_HANDLE;
    if (dbc->conn) return SQL_ERROR;  /* already connected */

    char host[256], db[256], uid[256], pwd[256];
    int  port;

    if (!ServerName) return SQL_ERROR;
    parse_server_name((const char *)ServerName, (int)NameLength1,
                      host, sizeof host, &port, db, sizeof db);

    /* Copy user/password; handle SQL_NTS */
    if (UserName) {
        int ulen = (NameLength2 == SQL_NTS || NameLength2 < 0)
                   ? (int)strlen((char *)UserName) : (int)NameLength2;
        snprintf(uid, sizeof uid, "%.*s", ulen, (char *)UserName);
    } else {
        uid[0] = '\0';
    }
    if (Authentication) {
        int plen = (NameLength3 == SQL_NTS || NameLength3 < 0)
                   ? (int)strlen((char *)Authentication) : (int)NameLength3;
        snprintf(pwd, sizeof pwd, "%.*s", plen, (char *)Authentication);
    } else {
        pwd[0] = '\0';
    }

    return do_connect(dbc, host, port, db, uid, pwd);
}

SQLRETURN SQLDriverConnect(SQLHDBC ConnectionHandle, SQLPOINTER WindowHandle,
                           SQLCHAR *InConnectionString, SQLSMALLINT StringLength1,
                           SQLCHAR *OutConnectionString, SQLSMALLINT BufferLength,
                           SQLSMALLINT *StringLength2Ptr,
                           SQLUSMALLINT DriverCompletion) {
    sdb2_dbc_t *dbc = (sdb2_dbc_t *)ConnectionHandle;
    if (!dbc) return SQL_INVALID_HANDLE;
    if (dbc->conn) return SQL_ERROR;  /* already connected */
    if (!InConnectionString) return SQL_ERROR;

    (void)WindowHandle; (void)DriverCompletion;

    char host[256], db[256], uid[256], pwd[256];
    int  port;
    parse_connection_string((const char *)InConnectionString, (int)StringLength1,
                            host, sizeof host, &port,
                            db,   sizeof db,
                            uid,  sizeof uid,
                            pwd,  sizeof pwd);

    SQLRETURN rc = do_connect(dbc, host, port, db, uid, pwd);

    /* Optionally echo the connection string back (without password). */
    if (rc == SQL_SUCCESS && OutConnectionString && BufferLength > 0) {
        int written = snprintf((char *)OutConnectionString, (size_t)BufferLength,
                               "HOSTNAME=%s;PORT=%d;DATABASE=%s;UID=%s;",
                               host, port, db, uid);
        if (StringLength2Ptr) *StringLength2Ptr = (SQLSMALLINT)written;
    }

    return rc;
}

SQLRETURN SQLDisconnect(SQLHDBC ConnectionHandle) {
    sdb2_dbc_t *dbc = (sdb2_dbc_t *)ConnectionHandle;
    if (!dbc) return SQL_INVALID_HANDLE;

    drda_disconnect(dbc->conn);
    dbc->conn = NULL;
    return SQL_SUCCESS;
}

/* ================================================================== */
/* Statement execution                                                  */
/* ================================================================== */

/*
 * Build an expanded SQL string by substituting ? parameter markers with
 * SQL literal values from the bound parameters.  Handles:
 *   - Integers / floats → bare decimal literals
 *   - Strings (SQL_C_CHAR) → single-quoted; internal ' doubled
 *   - NULL (SQL_NULL_DATA / null ind_ptr with SQL_NULL_DATA) → NULL
 *   - Skips ? inside single-quoted literals (does not break valid SQL)
 *
 * Returns a heap-allocated string (caller must free) or NULL on OOM/overflow.
 */
static char *expand_sql_params(const char *sql,
                                sdb2_param_binding_t *bindings, int n) {
    /* Worst-case: each ? expands to ~256 chars + surrounding quotes */
    size_t alloc = strlen(sql) + (size_t)n * 300 + 4;
    char *out = (char *)malloc(alloc);
    if (!out) return NULL;

    const char *p   = sql;
    size_t      pos = 0;
    int         pi  = 0;   /* current ? index (1-based param number = pi+1) */
    int         in_quote = 0;

    while (*p) {
        if (*p == '\'') {
            in_quote = !in_quote;
            out[pos++] = *p++;
        } else if (*p == '?' && !in_quote) {
            /* Substitute parameter pi */
            char lit[512] = "NULL";
            if (pi < n && bindings[pi].active) {
                sdb2_param_binding_t *pb = &bindings[pi];
                long ind = pb->ind_ptr ? *pb->ind_ptr : 0L;
                int is_null = (ind == SQL_NULL_DATA) || !pb->value_ptr;

                if (!is_null) {
                    switch (pb->value_type) {
                    case SQL_C_SHORT:
                        snprintf(lit, sizeof lit, "%d",
                                 (int)*(const int16_t *)pb->value_ptr);
                        break;
                    case SQL_C_LONG:
                        snprintf(lit, sizeof lit, "%d",
                                 (int)*(const int32_t *)pb->value_ptr);
                        break;
                    case SQL_C_SBIGINT:
                        snprintf(lit, sizeof lit, "%lld",
                                 (long long)*(const int64_t *)pb->value_ptr);
                        break;
                    case SQL_C_FLOAT:
                        snprintf(lit, sizeof lit, "%.7g",
                                 (double)*(const float *)pb->value_ptr);
                        break;
                    case SQL_C_DOUBLE:
                        snprintf(lit, sizeof lit, "%.15g",
                                 *(const double *)pb->value_ptr);
                        break;
                    case SQL_C_CHAR: {
                        const char *sv = (const char *)pb->value_ptr;
                        int slen = (ind == SQL_NTS || ind < 0)
                                   ? (int)strlen(sv) : (int)ind;
                        /* Build 'value' with '' escaping */
                        char *lp = lit;
                        const char *end = lit + sizeof lit - 2;
                        *lp++ = '\'';
                        for (int i = 0; i < slen && lp < end; i++) {
                            if (sv[i] == '\'') {
                                if (lp + 1 < end) { *lp++ = '\''; *lp++ = '\''; }
                            } else {
                                *lp++ = sv[i];
                            }
                        }
                        *lp++ = '\'';
                        *lp   = '\0';
                        break;
                    }
                    default:
                        /* Fallback: treat as string */
                        snprintf(lit, sizeof lit, "'?'");
                        break;
                    }
                }
            }
            /* Append literal */
            size_t llen = strlen(lit);
            if (pos + llen >= alloc - 1) {
                /* Reallocate */
                alloc = alloc * 2 + llen + 4;
                char *np = (char *)realloc(out, alloc);
                if (!np) { free(out); return NULL; }
                out = np;
            }
            memcpy(out + pos, lit, llen);
            pos += llen;
            pi++;
            p++;
        } else {
            out[pos++] = *p++;
        }
        if (pos >= alloc - 1) {
            alloc = alloc * 2 + 4;
            char *np = (char *)realloc(out, alloc);
            if (!np) { free(out); return NULL; }
            out = np;
        }
    }
    out[pos] = '\0';
    return out;
}

/* Reset per-execution statement state (cursor, row buffer, etc.). */
static void stmt_reset(sdb2_stmt_t *stmt) {
    drda_close_cursor(stmt);
    fdoca_result_free(&stmt->cols);
    fdoca_result_free(&stmt->param_desc);
    free(stmt->param_bindings);
    stmt->param_bindings   = NULL;
    stmt->n_param_bindings = 0;
    stmt->row_buf_used = 0;
    stmt->row_pos      = 0;
    stmt->eof          = 0;
    stmt->cur_row      = NULL;
    stmt->cur_row_len  = 0;
    stmt->row_count    = -1;
    stmt->is_prepared  = 0;
    stmt->is_query     = 0;
    free(stmt->prepared_sql);
    stmt->prepared_sql = NULL;
}

SQLRETURN SQLExecDirect(SQLHSTMT StatementHandle,
                        SQLCHAR *StatementText, SQLINTEGER TextLength) {
    sdb2_stmt_t *stmt = (sdb2_stmt_t *)StatementHandle;
    if (!stmt) return SQL_INVALID_HANDLE;
    if (!stmt->dbc || !stmt->dbc->conn) {
        snprintf(stmt->diag_msg, sizeof stmt->diag_msg,
                 "Not connected");
        return SQL_ERROR;
    }
    if (!StatementText) return SQL_ERROR;

    const char *sql = (const char *)StatementText;
    int sql_len = (TextLength == SQL_NTS || TextLength < 0)
                  ? (int)strlen(sql) : (int)TextLength;

    stmt_reset(stmt);
    stmt->diag_msg[0] = '\0';
    stmt->diag_sqlcode = 0;

    if (sql_is_query(sql, sql_len)) {
        /* Query: prepare + open cursor */
        /* Make a NUL-terminated copy for drda_prepare */
        char *sqls = sdb2_strndup(sql, (size_t)sql_len);
        if (!sqls) {
            snprintf(stmt->diag_msg, sizeof stmt->diag_msg, "Out of memory");
            return SQL_ERROR;
        }

        int rc = drda_prepare(stmt, sqls);
        free(sqls);
        if (rc != SDB2_OK) {
            sync_conn_diag_to_stmt(stmt);
            return SQL_ERROR;
        }

        rc = drda_open_query(stmt);
        if (rc == SDB2_ERR_NODATA) return SQL_NO_DATA;
        if (rc != SDB2_OK) {
            sync_conn_diag_to_stmt(stmt);
            return SQL_ERROR;
        }
        stmt->is_query = 1;
        return SQL_SUCCESS;

    } else {
        /* Non-query: exec immediate */
        char *sqls = sdb2_strndup(sql, (size_t)sql_len);
        if (!sqls) {
            snprintf(stmt->diag_msg, sizeof stmt->diag_msg, "Out of memory");
            return SQL_ERROR;
        }

        int32_t nrows = 0;
        int rc = drda_exec_immediate(stmt->dbc->conn, sqls, &nrows);
        free(sqls);
        stmt->row_count = nrows;

        if (rc != SDB2_OK) {
            sync_conn_diag_to_stmt(stmt);
            return SQL_ERROR;
        }
        return SQL_SUCCESS;
    }
}

SQLRETURN SQLPrepare(SQLHSTMT StatementHandle,
                     SQLCHAR *StatementText, SQLINTEGER TextLength) {
    sdb2_stmt_t *stmt = (sdb2_stmt_t *)StatementHandle;
    if (!stmt) return SQL_INVALID_HANDLE;
    if (!stmt->dbc || !stmt->dbc->conn) {
        snprintf(stmt->diag_msg, sizeof stmt->diag_msg, "Not connected");
        return SQL_ERROR;
    }
    if (!StatementText) return SQL_ERROR;

    const char *sql = (const char *)StatementText;
    int sql_len = (TextLength == SQL_NTS || TextLength < 0)
                  ? (int)strlen(sql) : (int)TextLength;

    stmt_reset(stmt);
    stmt->diag_msg[0]  = '\0';
    stmt->diag_sqlcode = 0;

    char *sqls = sdb2_strndup(sql, (size_t)sql_len);
    if (!sqls) {
        snprintf(stmt->diag_msg, sizeof stmt->diag_msg, "Out of memory");
        return SQL_ERROR;
    }

    int rc = drda_prepare(stmt, sqls);
    if (rc != SDB2_OK) {
        free(sqls);
        sync_conn_diag_to_stmt(stmt);
        return SQL_ERROR;
    }

    stmt->prepared_sql = sqls;
    stmt->is_prepared  = 1;
    stmt->is_query     = sql_is_query(sql, sql_len);
    return SQL_SUCCESS;
}

SQLRETURN SQLExecute(SQLHSTMT StatementHandle) {
    sdb2_stmt_t *stmt = (sdb2_stmt_t *)StatementHandle;
    if (!stmt) return SQL_INVALID_HANDLE;
    if (!stmt->is_prepared) {
        snprintf(stmt->diag_msg, sizeof stmt->diag_msg,
                 "Statement not prepared");
        return SQL_ERROR;
    }
    if (!stmt->dbc || !stmt->dbc->conn) {
        snprintf(stmt->diag_msg, sizeof stmt->diag_msg, "Not connected");
        return SQL_ERROR;
    }

    /* Reset cursor state (keep section + prepared_sql). */
    drda_close_cursor(stmt);
    fdoca_result_free(&stmt->cols);
    stmt->row_buf_used = 0;
    stmt->row_pos      = 0;
    stmt->eof          = 0;
    stmt->cur_row      = NULL;
    stmt->cur_row_len  = 0;
    stmt->row_count    = -1;
    stmt->diag_msg[0]  = '\0';
    stmt->diag_sqlcode = 0;

    if (stmt->is_query) {
        if (stmt->n_param_bindings > 0) {
            /* DB2 SYSSH200 does not accept SQLDTA in OPNQRY.  Use client-side
             * parameter expansion: inline the values into the SQL text, then
             * re-prepare the expanded statement before opening the cursor. */
            if (!stmt->prepared_sql) {
                snprintf(stmt->diag_msg, sizeof stmt->diag_msg,
                         "No SQL text for prepared query with parameters");
                return SQL_ERROR;
            }
            char *expanded = expand_sql_params(stmt->prepared_sql,
                                               stmt->param_bindings,
                                               stmt->n_param_bindings);
            if (!expanded) {
                snprintf(stmt->diag_msg, sizeof stmt->diag_msg,
                         "Out of memory expanding SQL parameters");
                return SQL_ERROR;
            }
            int rc = drda_prepare(stmt, expanded);
            free(expanded);
            if (rc != SDB2_OK) {
                sync_conn_diag_to_stmt(stmt);
                return SQL_ERROR;
            }
        }
        int rc = drda_open_query(stmt);
        if (rc == SDB2_ERR_NODATA) return SQL_NO_DATA;
        if (rc != SDB2_OK) {
            sync_conn_diag_to_stmt(stmt);
            return SQL_ERROR;
        }
        return SQL_SUCCESS;
    } else {
        /* Non-query prepared statement. */
        if (stmt->n_param_bindings > 0) {
            /* Client-side param substitution: expand ? markers into SQL literals,
             * then execute via EXCSQLIMM.  DB2's SYSSH200 package does not accept
             * EXCSQLSTT+SQLDTA, so this is the correct execution path. */
            if (!stmt->prepared_sql) {
                snprintf(stmt->diag_msg, sizeof stmt->diag_msg,
                         "No SQL text for prepared non-query");
                return SQL_ERROR;
            }
            char *expanded = expand_sql_params(stmt->prepared_sql,
                                               stmt->param_bindings,
                                               stmt->n_param_bindings);
            if (!expanded) {
                snprintf(stmt->diag_msg, sizeof stmt->diag_msg,
                         "Out of memory expanding SQL parameters");
                return SQL_ERROR;
            }
            int32_t nrows = 0;
            int rc = drda_exec_immediate(stmt->dbc->conn, expanded, &nrows);
            free(expanded);
            stmt->row_count = nrows;
            if (rc != SDB2_OK) {
                sync_conn_diag_to_stmt(stmt);
                return SQL_ERROR;
            }
        } else {
            /* No bound params: re-execute via EXCSQLIMM. */
            if (!stmt->prepared_sql) {
                snprintf(stmt->diag_msg, sizeof stmt->diag_msg,
                         "No SQL text for prepared non-query");
                return SQL_ERROR;
            }
            int32_t nrows = 0;
            int rc = drda_exec_immediate(stmt->dbc->conn, stmt->prepared_sql, &nrows);
            stmt->row_count = nrows;
            if (rc != SDB2_OK) {
                sync_conn_diag_to_stmt(stmt);
                return SQL_ERROR;
            }
        }
        return SQL_SUCCESS;
    }
}

/* ================================================================== */
/* Result set metadata                                                  */
/* ================================================================== */

SQLRETURN SQLNumResultCols(SQLHSTMT StatementHandle, SQLSMALLINT *ColumnCountPtr) {
    sdb2_stmt_t *stmt = (sdb2_stmt_t *)StatementHandle;
    if (!stmt) return SQL_INVALID_HANDLE;
    if (!ColumnCountPtr) return SQL_ERROR;
    *ColumnCountPtr = (SQLSMALLINT)stmt->cols.ncols;
    return SQL_SUCCESS;
}

SQLRETURN SQLDescribeCol(SQLHSTMT StatementHandle, SQLUSMALLINT ColumnNumber,
                         SQLCHAR *ColumnName, SQLSMALLINT BufferLength,
                         SQLSMALLINT *NameLengthPtr, SQLSMALLINT *DataTypePtr,
                         SQLULEN *ColumnSizePtr, SQLSMALLINT *DecimalDigitsPtr,
                         SQLSMALLINT *NullablePtr) {
    sdb2_stmt_t *stmt = (sdb2_stmt_t *)StatementHandle;
    if (!stmt) return SQL_INVALID_HANDLE;

    if (ColumnNumber == 0 || (int)ColumnNumber > stmt->cols.ncols)
        return SQL_ERROR;

    const fdoca_col_t *col = &stmt->cols.cols[ColumnNumber - 1];

    if (ColumnName && BufferLength > 0) {
        int nlen = (int)strlen(col->name);
        int copy = (nlen < BufferLength - 1) ? nlen : BufferLength - 1;
        memcpy(ColumnName, col->name, (size_t)copy);
        ColumnName[copy] = '\0';
    }
    if (NameLengthPtr)
        *NameLengthPtr = (SQLSMALLINT)strlen(col->name);
    if (DataTypePtr)
        *DataTypePtr = sdb2ctype_to_sql(col->sdb2_type);
    if (ColumnSizePtr)
        *ColumnSizePtr = (SQLULEN)col->length;
    if (DecimalDigitsPtr)
        *DecimalDigitsPtr = (SQLSMALLINT)col->scale;
    if (NullablePtr)
        *NullablePtr = col->nullable ? SQL_NULLABLE : SQL_NO_NULLS;

    return SQL_SUCCESS;
}

/* ================================================================== */
/* Column binding and fetch                                             */
/* ================================================================== */

/* Ensure stmt->bindings has at least n slots (1-based n → 0-based index). */
static SQLRETURN ensure_bindings(sdb2_stmt_t *stmt, int n) {
    if (n <= stmt->n_bindings) return SQL_SUCCESS;
    sdb2_col_binding_t *nb =
        (sdb2_col_binding_t *)realloc(stmt->bindings,
                                      (size_t)n * sizeof(sdb2_col_binding_t));
    if (!nb) return SQL_ERROR;
    /* Zero-init new slots. */
    memset(nb + stmt->n_bindings, 0,
           (size_t)(n - stmt->n_bindings) * sizeof(sdb2_col_binding_t));
    stmt->bindings  = nb;
    stmt->n_bindings = n;
    return SQL_SUCCESS;
}

SQLRETURN SQLBindCol(SQLHSTMT StatementHandle, SQLUSMALLINT ColumnNumber,
                     SQLSMALLINT TargetType, SQLPOINTER TargetValuePtr,
                     SQLLEN BufferLength, SQLLEN *StrLen_or_IndPtr) {
    sdb2_stmt_t *stmt = (sdb2_stmt_t *)StatementHandle;
    if (!stmt) return SQL_INVALID_HANDLE;
    if (ColumnNumber == 0) return SQL_ERROR;

    SQLRETURN rc = ensure_bindings(stmt, (int)ColumnNumber);
    if (rc != SQL_SUCCESS) return rc;

    sdb2_col_binding_t *b = &stmt->bindings[ColumnNumber - 1];
    if (!TargetValuePtr) {
        /* Unbind this column. */
        memset(b, 0, sizeof *b);
        return SQL_SUCCESS;
    }
    b->active  = 1;
    b->c_type  = (short)TargetType;
    b->buf     = TargetValuePtr;
    b->buf_len = (long)BufferLength;
    b->ind_ptr = (long *)StrLen_or_IndPtr;
    return SQL_SUCCESS;
}

SQLRETURN SQLFetch(SQLHSTMT StatementHandle) {
    sdb2_stmt_t *stmt = (sdb2_stmt_t *)StatementHandle;
    if (!stmt) return SQL_INVALID_HANDLE;

    const uint8_t *row;
    size_t         row_len;
    int rc = drda_fetch(stmt, &row, &row_len);
    if (rc == SDB2_ERR_NODATA) return SQL_NO_DATA;
    if (rc != SDB2_OK) {
        sync_conn_diag_to_stmt(stmt);
        return SQL_ERROR;
    }

    /* Write to all bound columns. */
    SQLRETURN ret = SQL_SUCCESS;
    for (int i = 0; i < stmt->n_bindings; i++) {
        sdb2_col_binding_t *b = &stmt->bindings[i];
        if (!b->active) continue;

        sdb2_value_t val;
        int grc = sdb2_stmt_get_col(stmt, i, &val);
        if (grc != SDB2_OK) {
            if (b->ind_ptr) *b->ind_ptr = SQL_NULL_DATA;
            continue;
        }

        SQLRETURN brc = copy_value_to_c_buf(&val, (SQLSMALLINT)b->c_type,
                                             b->buf, (SQLLEN)b->buf_len,
                                             (SQLLEN *)b->ind_ptr);
        if (brc == SQL_SUCCESS_WITH_INFO) ret = SQL_SUCCESS_WITH_INFO;
        else if (brc == SQL_ERROR) ret = SQL_ERROR;
    }
    return ret;
}

SQLRETURN SQLGetData(SQLHSTMT StatementHandle, SQLUSMALLINT ColumnNumber,
                     SQLSMALLINT TargetType, SQLPOINTER TargetValuePtr,
                     SQLLEN BufferLength, SQLLEN *StrLen_or_IndPtr) {
    sdb2_stmt_t *stmt = (sdb2_stmt_t *)StatementHandle;
    if (!stmt) return SQL_INVALID_HANDLE;
    if (ColumnNumber == 0 || (int)ColumnNumber > stmt->cols.ncols)
        return SQL_ERROR;

    sdb2_value_t val;
    int rc = sdb2_stmt_get_col(stmt, (int)ColumnNumber - 1, &val);
    if (rc == SDB2_ERR_NODATA) return SQL_NO_DATA;
    if (rc != SDB2_OK) return SQL_ERROR;

    return copy_value_to_c_buf(&val, TargetType,
                                TargetValuePtr, BufferLength,
                                StrLen_or_IndPtr);
}

SQLRETURN SQLRowCount(SQLHSTMT StatementHandle, SQLLEN *RowCountPtr) {
    sdb2_stmt_t *stmt = (sdb2_stmt_t *)StatementHandle;
    if (!stmt) return SQL_INVALID_HANDLE;
    if (!RowCountPtr) return SQL_ERROR;
    *RowCountPtr = (SQLLEN)stmt->row_count;
    return SQL_SUCCESS;
}

/* ================================================================== */
/* Parameter binding (Phase 12)                                         */
/* ================================================================== */

/* Ensure stmt->param_bindings has at least n slots (1-based n → 0-based). */
static SQLRETURN ensure_param_bindings(sdb2_stmt_t *stmt, int n) {
    if (n <= stmt->n_param_bindings) return SQL_SUCCESS;
    sdb2_param_binding_t *nb =
        (sdb2_param_binding_t *)realloc(stmt->param_bindings,
                                        (size_t)n * sizeof(sdb2_param_binding_t));
    if (!nb) return SQL_ERROR;
    memset(nb + stmt->n_param_bindings, 0,
           (size_t)(n - stmt->n_param_bindings) * sizeof(sdb2_param_binding_t));
    stmt->param_bindings   = nb;
    stmt->n_param_bindings = n;
    return SQL_SUCCESS;
}

SQLRETURN SQLBindParameter(SQLHSTMT StatementHandle,
                           SQLUSMALLINT ParameterNumber,
                           SQLSMALLINT  InputOutputType,
                           SQLSMALLINT  ValueType,
                           SQLSMALLINT  ParameterType,
                           SQLULEN      ColumnSize,
                           SQLSMALLINT  DecimalDigits,
                           SQLPOINTER   ParameterValuePtr,
                           SQLLEN       BufferLength,
                           SQLLEN      *StrLen_or_IndPtr) {
    sdb2_stmt_t *stmt = (sdb2_stmt_t *)StatementHandle;
    if (!stmt) return SQL_INVALID_HANDLE;
    if (ParameterNumber == 0) return SQL_ERROR;
    (void)InputOutputType;  /* Phase 12: input only */

    SQLRETURN rc = ensure_param_bindings(stmt, (int)ParameterNumber);
    if (rc != SQL_SUCCESS) return rc;

    sdb2_param_binding_t *pb = &stmt->param_bindings[ParameterNumber - 1];

    /* Passing NULL ParameterValuePtr with no SQL_NULL_DATA indicator unbinds. */
    if (!ParameterValuePtr &&
        (!StrLen_or_IndPtr || *StrLen_or_IndPtr != SQL_NULL_DATA)) {
        memset(pb, 0, sizeof *pb);
        return SQL_SUCCESS;
    }

    pb->active     = 1;
    pb->value_type = (short)ValueType;
    pb->sql_type   = (short)ParameterType;
    pb->col_size   = (unsigned int)ColumnSize;
    pb->dec_digits = (short)DecimalDigits;
    pb->value_ptr  = ParameterValuePtr;
    pb->buf_len    = (long)BufferLength;
    pb->ind_ptr    = (long *)StrLen_or_IndPtr;

    return SQL_SUCCESS;
}

SQLRETURN SQLNumParams(SQLHSTMT StatementHandle, SQLSMALLINT *ParameterCountPtr) {
    sdb2_stmt_t *stmt = (sdb2_stmt_t *)StatementHandle;
    if (!stmt) return SQL_INVALID_HANDLE;
    if (!ParameterCountPtr) return SQL_ERROR;

    if (!stmt->is_prepared) {
        *ParameterCountPtr = 0;
        return SQL_SUCCESS;
    }
    /* Prefer the server-side count from DSCSQLSTT; fall back to bound count. */
    *ParameterCountPtr = stmt->param_desc.ncols > 0
                         ? (SQLSMALLINT)stmt->param_desc.ncols
                         : (SQLSMALLINT)stmt->n_param_bindings;
    return SQL_SUCCESS;
}

/* ================================================================== */
/* Cursor / statement cleanup                                           */
/* ================================================================== */

SQLRETURN SQLFreeStmt(SQLHSTMT StatementHandle, SQLUSMALLINT Option) {
    sdb2_stmt_t *stmt = (sdb2_stmt_t *)StatementHandle;
    if (!stmt) return SQL_INVALID_HANDLE;

    switch (Option) {
    case SQL_CLOSE:
        drda_close_cursor(stmt);
        fdoca_result_free(&stmt->cols);
        stmt->row_buf_used = 0;
        stmt->row_pos      = 0;
        stmt->cur_row      = NULL;
        stmt->cur_row_len  = 0;
        stmt->eof          = 0;
        return SQL_SUCCESS;

    case SQL_DROP:
        sdb2_stmt_free(stmt);
        return SQL_SUCCESS;

    case SQL_UNBIND:
        if (stmt->bindings) {
            memset(stmt->bindings, 0,
                   (size_t)stmt->n_bindings * sizeof(sdb2_col_binding_t));
        }
        return SQL_SUCCESS;

    case SQL_RESET_PARAMS:
        free(stmt->param_bindings);
        stmt->param_bindings   = NULL;
        stmt->n_param_bindings = 0;
        fdoca_result_free(&stmt->param_desc);
        return SQL_SUCCESS;

    default:
        return SQL_ERROR;
    }
}

/* ================================================================== */
/* Transaction control                                                  */
/* ================================================================== */

SQLRETURN SQLEndTran(SQLSMALLINT HandleType, SQLHANDLE Handle,
                     SQLSMALLINT CompletionType) {
    if (!Handle) return SQL_INVALID_HANDLE;

    drda_conn_t *conn = NULL;

    switch (HandleType) {
    case SQL_HANDLE_DBC: {
        sdb2_dbc_t *dbc = (sdb2_dbc_t *)Handle;
        if (!dbc->conn) {
            snprintf(dbc->diag_msg, sizeof dbc->diag_msg, "Not connected");
            return SQL_ERROR;
        }
        conn = dbc->conn;
        break;
    }
    case SQL_HANDLE_ENV: {
        /* Per ODBC spec, SQLEndTran on ENV commits/rolls back all connections.
         * We only support single-connection here; loop would go here in a full impl. */
        return SQL_SUCCESS;  /* no connections → trivial */
    }
    default:
        return SQL_ERROR;
    }

    int r;
    if (CompletionType == SQL_COMMIT) {
        r = drda_commit(conn);
    } else if (CompletionType == SQL_ROLLBACK) {
        r = drda_rollback(conn);
    } else {
        return SQL_ERROR;
    }

    if (r != SDB2_OK) {
        /* Copy diagnostic from conn to dbc for SQLGetDiagRec on DBC handle */
        if (HandleType == SQL_HANDLE_DBC) {
            sdb2_dbc_t *dbc = (sdb2_dbc_t *)Handle;
            strncpy(dbc->diag_msg, conn->diag_msg, sizeof dbc->diag_msg - 1);
            dbc->diag_sqlcode = conn->diag_sqlcode;
        }
        return SQL_ERROR;
    }
    return SQL_SUCCESS;
}

/* ================================================================== */
/* Diagnostics                                                          */
/* ================================================================== */

/*
 * Retrieve diagnostic msg/sqlcode from the typed handle.
 * Returns 0 (has diag) or -1 (no diag / handle type unknown).
 */
static int get_diag(SQLSMALLINT HandleType, SQLHANDLE Handle,
                    const char **msg, int *sqlcode) {
    switch (HandleType) {
    case SQL_HANDLE_STMT: {
        sdb2_stmt_t *stmt = (sdb2_stmt_t *)Handle;
        if (!stmt) return -1;
        *msg     = stmt->diag_msg;
        *sqlcode = stmt->diag_sqlcode;
        return (*stmt->diag_msg || stmt->diag_sqlcode) ? 0 : -1;
    }
    case SQL_HANDLE_DBC: {
        sdb2_dbc_t *dbc = (sdb2_dbc_t *)Handle;
        if (!dbc) return -1;
        /* Prefer connection-level diag from the drda_conn if available. */
        if (dbc->conn && (dbc->conn->diag_msg[0] || dbc->conn->diag_sqlcode)) {
            *msg     = dbc->conn->diag_msg;
            *sqlcode = dbc->conn->diag_sqlcode;
            return 0;
        }
        *msg     = dbc->diag_msg;
        *sqlcode = dbc->diag_sqlcode;
        return (*dbc->diag_msg || dbc->diag_sqlcode) ? 0 : -1;
    }
    case SQL_HANDLE_ENV:
        *msg = ""; *sqlcode = 0;
        return -1;  /* env has no diagnostics */
    default:
        return -1;
    }
}

SQLRETURN SQLGetDiagRec(SQLSMALLINT HandleType, SQLHANDLE Handle,
                        SQLSMALLINT RecNumber,
                        SQLCHAR *SQLState, SQLINTEGER *NativeErrorPtr,
                        SQLCHAR *MessageText, SQLSMALLINT BufferLength,
                        SQLSMALLINT *TextLengthPtr) {
    if (!Handle) return SQL_INVALID_HANDLE;
    if (RecNumber <= 0) return SQL_ERROR;
    if (RecNumber > 1)  return SQL_NO_DATA;  /* at most one record */

    const char *msg;
    int sqlcode;
    if (get_diag(HandleType, Handle, &msg, &sqlcode) < 0)
        return SQL_NO_DATA;

    /* Derive SQLSTATE. */
    const char *state = (sqlcode != 0)
                        ? sqlcode_to_sqlstate(sqlcode)
                        : sdb2err_to_sqlstate(SDB2_ERR_SQL);
    if (!msg || !*msg) {
        /* No message on record — treat as no data. */
        return SQL_NO_DATA;
    }

    if (SQLState)
        memcpy(SQLState, state, 6);  /* 5 chars + NUL */

    if (NativeErrorPtr)
        *NativeErrorPtr = (SQLINTEGER)sqlcode;

    int mlen = (int)strlen(msg);
    if (MessageText && BufferLength > 0) {
        int copy = (mlen < BufferLength - 1) ? mlen : BufferLength - 1;
        memcpy(MessageText, msg, (size_t)copy);
        MessageText[copy] = '\0';
    }
    if (TextLengthPtr)
        *TextLengthPtr = (SQLSMALLINT)mlen;

    return SQL_SUCCESS;
}

SQLRETURN SQLGetDiagField(SQLSMALLINT HandleType, SQLHANDLE Handle,
                          SQLSMALLINT RecNumber, SQLSMALLINT DiagIdentifier,
                          SQLPOINTER DiagInfoPtr, SQLSMALLINT BufferLength,
                          SQLSMALLINT *StringLengthPtr) {
    if (!Handle) return SQL_INVALID_HANDLE;

    const char *msg;
    int sqlcode;
    int has_diag = (get_diag(HandleType, Handle, &msg, &sqlcode) == 0);
    (void)BufferLength;

    switch (DiagIdentifier) {
    case SQL_DIAG_NUMBER:
        if (DiagInfoPtr) *(SQLINTEGER *)DiagInfoPtr = has_diag ? 1 : 0;
        return SQL_SUCCESS;

    case SQL_DIAG_NATIVE:
        if (RecNumber != 1 || !has_diag) return SQL_NO_DATA;
        if (DiagInfoPtr) *(SQLINTEGER *)DiagInfoPtr = (SQLINTEGER)sqlcode;
        return SQL_SUCCESS;

    case SQL_DIAG_SQLSTATE: {
        if (RecNumber != 1 || !has_diag) return SQL_NO_DATA;
        const char *state = (sqlcode != 0)
                            ? sqlcode_to_sqlstate(sqlcode)
                            : "HY000";
        if (DiagInfoPtr && BufferLength >= 6)
            memcpy(DiagInfoPtr, state, 6);
        if (StringLengthPtr) *StringLengthPtr = 5;
        return SQL_SUCCESS;
    }

    case SQL_DIAG_MESSAGE_TEXT: {
        if (RecNumber != 1 || !has_diag) return SQL_NO_DATA;
        int mlen = msg ? (int)strlen(msg) : 0;
        if (DiagInfoPtr && BufferLength > 0 && msg) {
            int copy = (mlen < BufferLength - 1) ? mlen : BufferLength - 1;
            memcpy(DiagInfoPtr, msg, (size_t)copy);
            ((char *)DiagInfoPtr)[copy] = '\0';
        }
        if (StringLengthPtr) *StringLengthPtr = (SQLSMALLINT)mlen;
        return SQL_SUCCESS;
    }

    case SQL_DIAG_ROW_COUNT:
        if (HandleType == SQL_HANDLE_STMT) {
            sdb2_stmt_t *stmt = (sdb2_stmt_t *)Handle;
            if (DiagInfoPtr) *(SQLLEN *)DiagInfoPtr = (SQLLEN)stmt->row_count;
            return SQL_SUCCESS;
        }
        return SQL_ERROR;

    default:
        return SQL_ERROR;
    }
}

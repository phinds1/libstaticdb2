#ifndef SDB2_SDB2_H
#define SDB2_SDB2_H

/*
 * sdb2.h — internal error codes and opaque handle typedefs for libstaticdb2.
 *
 * All internal functions return int: 0 (SDB2_OK) on success, negative on error.
 * Never call exit() inside the library — accumulate diagnostics on the handle.
 */

typedef enum {
    SDB2_OK            =  0,  /* success */
    SDB2_ERR_MEM       = -1,  /* allocation failure */
    SDB2_ERR_NET       = -2,  /* socket / I/O error */
    SDB2_ERR_PROTO     = -3,  /* protocol violation */
    SDB2_ERR_TRUNC     = -4,  /* value too large for field */
    SDB2_ERR_NODATA    = -5,  /* no more rows / data */
    SDB2_ERR_BADARG    = -6,  /* invalid argument */
    SDB2_NEED_MORE     = -7,  /* buffer incomplete; supply more bytes */
    SDB2_ERR_UNSUPPORT = -8,  /* feature not yet implemented */
    SDB2_ERR_SQL       = -9,  /* SQL error — see conn->diag_sqlcode */
    SDB2_NEED_WRITE    = -10, /* socket not writable yet; retry after poll(POLLOUT) */
} sdb2_err_t;

/* Opaque handle typedefs — structs defined in later phases. */
typedef struct sdb2_env  sdb2_env_t;
typedef struct sdb2_dbc  sdb2_dbc_t;
typedef struct sdb2_stmt sdb2_stmt_t;

/*
 * Internal C-level type tags used by value decoding (Phase 10) and bind
 * (Phase 11). Derived from the DRDA type byte in QRYDSC/SQLDARD descriptors.
 */
typedef enum {
    SDB2_C_UNKNOWN   = 0,
    SDB2_C_BOOL      = 1,
    SDB2_C_INT16     = 2,
    SDB2_C_INT32     = 3,
    SDB2_C_INT64     = 4,
    SDB2_C_FLOAT32   = 5,
    SDB2_C_FLOAT64   = 6,
    SDB2_C_DECIMAL   = 7,
    SDB2_C_CHAR      = 8,
    SDB2_C_VARCHAR   = 9,
    SDB2_C_LONG_VARCHAR = 10,
    SDB2_C_BINARY    = 11,
    SDB2_C_VARBINARY = 12,
    SDB2_C_DATE      = 13,
    SDB2_C_TIME      = 14,
    SDB2_C_TIMESTAMP = 15,
    SDB2_C_BLOB      = 16,
    SDB2_C_CLOB      = 17,
} sdb2_ctype_t;

/* Maximum string/binary bytes in a decoded value (NUL-terminated for strings). */
#define SDB2_VALUE_STR_MAX 4096

/*
 * Tagged value returned by sdb2_stmt_get_col().
 *
 * Integer/float types are stored in the appropriate union field.
 * String-like types (CHAR, VARCHAR, DATE, TIME, TIMESTAMP, DECIMAL-as-text,
 * BINARY, VARBINARY) are stored in v.str.data (NUL-terminated for character
 * types; raw bytes for binary); v.str.len is the byte count without NUL.
 * DECIMAL is returned as a decimal string, e.g. "-123.45".
 */
typedef struct {
    sdb2_ctype_t type;
    int          is_null;
    union {
        int8_t   b;      /* SDB2_C_BOOL */
        int16_t  i16;    /* SDB2_C_INT16 */
        int32_t  i32;    /* SDB2_C_INT32 */
        int64_t  i64;    /* SDB2_C_INT64 */
        float    f32;    /* SDB2_C_FLOAT32 */
        double   f64;    /* SDB2_C_FLOAT64 */
        struct {
            char data[SDB2_VALUE_STR_MAX];
            int  len;
        } str;           /* CHAR, VARCHAR, DATE, TIME, TIMESTAMP, DECIMAL, BINARY, VARBINARY */
    } v;
} sdb2_value_t;

#endif /* SDB2_SDB2_H */

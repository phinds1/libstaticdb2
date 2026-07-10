#pragma once
#include <stdint.h>
#include <stddef.h>
#include "buf.h"
#include "../include/staticdb2/sdb2.h"

/* Maximum number of columns in a result set */
#define FDOCA_MAX_COLS 1024

/* Maximum column name length (from SQLDARD SQLNAME field) */
#define FDOCA_COLNAME_MAX 128

/*
 * FD:OCA column descriptor — one entry per SELECT column.
 * Populated by fdoca_parse_qrydsc() from the QRYDSC DDM payload.
 */
typedef struct {
    uint8_t      drda_type;          /* raw DRDA type byte from descriptor */
    uint16_t     length;             /* wire byte count (for DECIMAL: packed-BCD byte count) */
    uint16_t     ccsid;              /* character CCSID (0 if not character type) */
    uint8_t      nullable;           /* 1 = nullable (drda_type is odd) */
    uint8_t      precision;          /* DECIMAL precision (0 if not applicable) */
    uint8_t      scale;              /* DECIMAL scale    (0 if unknown/not applicable) */
    sdb2_ctype_t sdb2_type;          /* mapped internal C type tag */
    char         name[FDOCA_COLNAME_MAX]; /* column name; empty if not provided */
} fdoca_col_t;

/*
 * FD:OCA result descriptor — array of column entries.
 * Caller must call fdoca_result_free() when done.
 */
typedef struct {
    fdoca_col_t *cols;
    int          ncols;
} fdoca_result_t;

/*
 * Parse the FD:OCA triplet stream inside a QRYDSC DDM payload.
 * buf must be positioned at the first triplet byte; ddm_payload_len bytes
 * of payload must be available.
 *
 * Returns SDB2_OK and populates *out on success.
 * Caller frees with fdoca_result_free().
 */
int fdoca_parse_qrydsc(buf_t *b, size_t ddm_payload_len, fdoca_result_t *out);

/* Free memory allocated by fdoca_parse_qrydsc. */
void fdoca_result_free(fdoca_result_t *r);

/*
 * Map a raw DRDA type byte (including nullable bit) to an sdb2_ctype_t.
 */
sdb2_ctype_t fdoca_drda_to_sdb2(uint8_t drda_type);

/*
 * Return the wire byte count for one column's value (NOT including the
 * nullable null-indicator byte).  For variable-length types, peeks at
 * data[0..1] for the 2-byte length prefix.  Returns 0 when avail < 2.
 */
size_t fdoca_col_wire_size(const fdoca_col_t *col,
                           const uint8_t *data, size_t avail);

/*
 * Decode one column value from raw QRYDTA wire bytes.
 * If col->nullable, data[0] is the null-indicator byte; (data[0] & 0x80)
 * means NULL and out->is_null is set to 1 with no value bytes consumed beyond
 * the indicator.  Otherwise data[0] is the first value byte.
 *
 * Returns SDB2_OK, SDB2_NEED_MORE, or a negative sdb2_err_t.
 */
int fdoca_decode_col(const fdoca_col_t *col,
                     const uint8_t *data, size_t avail,
                     sdb2_value_t *out);

/*
 * Get a column value from the statement's current row (populated by
 * drda_fetch).  col_idx is 0-based.
 *
 * Returns SDB2_OK, SDB2_ERR_BADARG (bad index), SDB2_ERR_NODATA (no current
 * row), or a decode error.
 */
int sdb2_stmt_get_col(sdb2_stmt_t *stmt, int col_idx, sdb2_value_t *out);

/* ================================================================
 * Phase 12 — parameter encoding (input direction: C value → DRDA wire)
 * ================================================================ */

/*
 * Build a synthetic fdoca_col_t from an ODBC SQL_* type + precision/scale.
 * Always produces a nullable descriptor (null-indicator byte will be present).
 * Used as fallback when the server's SQLDARD is not available.
 */
fdoca_col_t fdoca_sql_type_to_col(short sql_type, unsigned int col_size, short dec_digits);

/*
 * Encode one parameter value into *b for SQLDTA (EXCSQLSTT input data).
 *
 * If col->nullable, writes a 1-byte null indicator first:
 *   0xFF = NULL,  0x00 = not null.
 * Then writes the wire-format value (if not null).
 *
 * c_type  : SQL_C_* type of *value_ptr
 * value_ptr : caller data buffer (may be NULL when ind == SQL_NULL_DATA)
 * buf_len : size of value_ptr buffer (used for SQL_C_BINARY)
 * ind     : SQL_NULL_DATA → null; SQL_NTS → strlen; >= 0 → actual byte length
 *
 * Returns SDB2_OK or a negative sdb2_err_t.
 */
int fdoca_encode_col(buf_t *b, const fdoca_col_t *col,
                     short c_type, const void *value_ptr,
                     long buf_len, long ind);

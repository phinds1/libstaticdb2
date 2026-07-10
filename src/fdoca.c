#include "fdoca.h"
#include "ccsid.h"
#include "handles.h"
#include <stdlib.h>
#include <string.h>

/*
 * DRDA type constants (from DRDAConstants.java).
 * Even = not nullable, odd = nullable.
 * Base type = drda_type & ~1u.
 */
#define DT_SMALL    0x04u  /* SMALLINT */
#define DT_INTEGER  0x02u  /* INTEGER */
#define DT_INTEGER8 0x16u  /* BIGINT */
#define DT_FLOAT4   0x0Cu  /* REAL / FLOAT(n<=24) */
#define DT_FLOAT8   0x0Au  /* DOUBLE / FLOAT(n>24) */
#define DT_DECIMAL  0x0Eu  /* DECIMAL(p,s) */
#define DT_ZDECIMAL 0x10u  /* ZONED DECIMAL */
#define DT_DATE     0x20u  /* DATE */
#define DT_TIME     0x22u  /* TIME */
#define DT_TIMESTAMP 0x24u /* TIMESTAMP */
#define DT_CHAR     0x30u  /* CHAR */
#define DT_VARCHAR  0x32u  /* VARCHAR */
#define DT_LONG     0x34u  /* LONG VARCHAR */
#define DT_VARBYTE  0x28u  /* VARCHAR FOR BIT DATA */
#define DT_FIXBYTE  0x26u  /* CHAR FOR BIT DATA */
#define DT_LONGVARBYTE 0x2Au /* LONG VARCHAR FOR BIT DATA */
#define DT_MIX      0x3Cu  /* MIXED (DBCS+SB) CHAR */
#define DT_VARMIX   0x3Eu  /* MIXED VARCHAR */
#define DT_LONGMIX  0x40u  /* MIXED LONG VARCHAR */
#define DT_LOBBYTES 0xC8u  /* BLOB */
#define DT_LOBCSBCS 0xCAu  /* CLOB (SB character set) */
#define DT_LOBCMIXED 0xCEu /* CLOB (mixed character set) */
#define DT_BOOLEAN  0xBEu  /* BOOLEAN */

/*
 * FD:OCA triplet type bytes.
 */
#define TRIP_MDD   0x78u  /* Mapping Data Descriptor */
#define TRIP_NGDA  0x76u  /* N-GDA (non-nullable group data array) */
#define TRIP_RLO   0x71u  /* Row Layout Object */
#define TRIP_SDA   0x70u  /* Simple Data Array (MDD override) */
#define TRIP_CPT   0x7Fu  /* Continuation Triplet */

/* LID values */
#define LID_SQLDTAGRP 0xD0u  /* column-data group */
#define LID_SQLCADTA  0xE0u  /* SQLCA data */
#define LID_SQLDTARD  0xF0u  /* SQLDTARD end RLO */
#define LID_NULL      0x00u

sdb2_ctype_t fdoca_drda_to_sdb2(uint8_t drda_type) {
    switch (drda_type & ~1u) {
    case DT_BOOLEAN:  return SDB2_C_BOOL;
    case DT_SMALL:    return SDB2_C_INT16;
    case DT_INTEGER:  return SDB2_C_INT32;
    case DT_INTEGER8: return SDB2_C_INT64;
    case DT_FLOAT4:   return SDB2_C_FLOAT32;
    case DT_FLOAT8:   return SDB2_C_FLOAT64;
    case DT_DECIMAL:
    case DT_ZDECIMAL: return SDB2_C_DECIMAL;
    case DT_DATE:     return SDB2_C_DATE;
    case DT_TIME:     return SDB2_C_TIME;
    case DT_TIMESTAMP: return SDB2_C_TIMESTAMP;
    case DT_CHAR:     return SDB2_C_CHAR;
    case DT_VARCHAR:  return SDB2_C_VARCHAR;
    case DT_LONG:     return SDB2_C_LONG_VARCHAR;
    case DT_MIX:      return SDB2_C_CHAR;
    case DT_VARMIX:   return SDB2_C_VARCHAR;
    case DT_LONGMIX:  return SDB2_C_LONG_VARCHAR;
    case DT_FIXBYTE:  return SDB2_C_BINARY;
    case DT_VARBYTE:  return SDB2_C_VARBINARY;
    case DT_LONGVARBYTE: return SDB2_C_VARBINARY;
    case DT_LOBBYTES: return SDB2_C_BLOB;
    case DT_LOBCSBCS:
    case DT_LOBCMIXED: return SDB2_C_CLOB;
    default:          return SDB2_C_UNKNOWN;
    }
}

/*
 * Parse column entries from an NGDA or CPT triplet payload.
 * triplet_payload_len = tripletLength - 3 (already consumed header bytes).
 */
static int parse_cols_from_triplet(buf_t *b, size_t payload_len,
                                   fdoca_col_t *cols, int *col_idx) {
    int n = (int)(payload_len / 3);
    for (int i = 0; i < n; i++) {
        if (*col_idx >= FDOCA_MAX_COLS) return SDB2_ERR_PROTO;
        uint8_t dtype; uint16_t len;
        if (buf_get_u8(b, &dtype))    return SDB2_NEED_MORE;
        if (buf_get_u16(b, &len))     return SDB2_NEED_MORE;

        fdoca_col_t *c = &cols[*col_idx];
        c->drda_type = dtype;
        c->nullable  = dtype & 1u;
        c->sdb2_type = fdoca_drda_to_sdb2(dtype);
        c->ccsid     = 0;
        c->name[0]   = '\0';

        /* For DECIMAL/ZDECIMAL the 2-byte length field carries
         * [precision(1)][scale(1)] per the DRDA/QRYDSC spec. */
        uint8_t base = dtype & ~1u;
        if (base == DT_DECIMAL || base == DT_ZDECIMAL) {
            c->precision = (uint8_t)((len >> 8) & 0xFF);
            c->scale     = (uint8_t)(len & 0xFF);
            /* Packed-BCD wire byte count = ceil((precision+1)/2) */
            c->length    = (uint16_t)((c->precision + 2u) / 2u);
        } else {
            c->precision = 0;
            c->scale     = 0;
            c->length    = len;
        }
        (*col_idx)++;
    }
    return SDB2_OK;
}

int fdoca_parse_qrydsc(buf_t *b, size_t ddm_payload_len, fdoca_result_t *out) {
    out->cols  = NULL;
    out->ncols = 0;

    /* Allocate worst-case buffer */
    fdoca_col_t *cols = (fdoca_col_t *)calloc(FDOCA_MAX_COLS, sizeof(fdoca_col_t));
    if (!cols) return SDB2_ERR_MEM;

    int col_idx  = 0;
    size_t consumed = 0;

    while (consumed < ddm_payload_len) {
        /* Read triplet header: [len(1)][type(1)][id(1)] */
        if (buf_remaining(b) < 3) { free(cols); return SDB2_NEED_MORE; }

        uint8_t trip_len, trip_type, trip_id;
        buf_get_u8(b, &trip_len);
        buf_get_u8(b, &trip_type);
        buf_get_u8(b, &trip_id);

        if (trip_len < 3) { free(cols); return SDB2_ERR_PROTO; }
        size_t payload_len = (size_t)(trip_len - 3);

        if (buf_remaining(b) < payload_len) { free(cols); return SDB2_NEED_MORE; }

        switch (trip_type) {
        case TRIP_MDD:
            /* Mapping Data Descriptor — skip payload (7 bytes total, 4 payload) */
            b->pos += payload_len;
            break;

        case TRIP_NGDA:
            /* N-GDA: id must be SQLDTAGRP_LID (0xD0) */
            if (trip_id != LID_SQLDTAGRP) { free(cols); return SDB2_ERR_PROTO; }
            {
                int r = parse_cols_from_triplet(b, payload_len, cols, &col_idx);
                if (r) { free(cols); return r; }
            }
            break;

        case TRIP_CPT:
            /* Continuation triplet — same layout as NGDA column entries */
            {
                int r = parse_cols_from_triplet(b, payload_len, cols, &col_idx);
                if (r) { free(cols); return r; }
            }
            break;

        case TRIP_SDA:
            /* Simple Data Array override — skip (handled by Typdef in Derby;
             * we store raw drda_type, so this override only matters for CCSID.
             * For ASCII agent, the default CCSID 367/1208 is fine. Skip. */
            b->pos += payload_len;
            break;

        case TRIP_RLO:
            /* Row Layout Object */
            b->pos += payload_len;
            if (trip_id == LID_SQLDTARD) {
                /* SQLDTARD end marker — stop parsing */
                consumed += trip_len;
                goto done;
            }
            break;

        default:
            /* Unknown triplet — skip */
            b->pos += payload_len;
            break;
        }

        consumed += trip_len;
    }

done:
    if (col_idx == 0) {
        free(cols);
        out->cols  = NULL;
        out->ncols = 0;
    } else {
        /* Shrink to exact size */
        fdoca_col_t *shrunk = (fdoca_col_t *)realloc(cols, (size_t)col_idx * sizeof(fdoca_col_t));
        out->cols  = shrunk ? shrunk : cols;
        out->ncols = col_idx;
    }
    return SDB2_OK;
}

void fdoca_result_free(fdoca_result_t *r) {
    free(r->cols);
    r->cols  = NULL;
    r->ncols = 0;
}

/* ================================================================
 * Phase 10 — column value decoding
 * ================================================================ */

size_t fdoca_col_wire_size(const fdoca_col_t *col,
                           const uint8_t *data, size_t avail) {
    switch (col->sdb2_type) {
    case SDB2_C_BOOL:         return 1;
    case SDB2_C_INT16:        return 2;
    case SDB2_C_INT32:        return 4;
    case SDB2_C_INT64:        return 8;
    case SDB2_C_FLOAT32:      return 4;
    case SDB2_C_FLOAT64:      return 8;
    case SDB2_C_DECIMAL:      return col->length;  /* packed-BCD byte count */
    case SDB2_C_DATE:         return 10;
    case SDB2_C_TIME:         return 8;
    case SDB2_C_TIMESTAMP:    return 26;
    case SDB2_C_CHAR:
    case SDB2_C_BINARY:       return col->length;
    case SDB2_C_VARCHAR:
    case SDB2_C_LONG_VARCHAR:
    case SDB2_C_VARBINARY: {
        if (avail < 2) return 0;
        /* VCS length prefix is big-endian (DRDA protocol level) */
        uint16_t vlen = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
        return 2u + vlen;
    }
    default:
        return col->length ? col->length : 0;
    }
}

/* Decode a big-endian 4-byte IEEE 754 float. */
/* decode_f32_be / decode_f64_be kept for potential future use with QTDSQLOLC/EBCDIC servers */
__attribute__((unused)) static float decode_f32_be(const uint8_t *p) {
    uint32_t bits = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                    ((uint32_t)p[2] <<  8) | p[3];
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

/* Decode a little-endian 4-byte IEEE 754 float (QTDSQLASC). */
static float decode_f32_le(const uint8_t *p) {
    uint32_t bits = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                    ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

/* Decode a big-endian 8-byte IEEE 754 double. */
__attribute__((unused)) static double decode_f64_be(const uint8_t *p) {
    uint64_t bits = ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) |
                    ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) |
                    ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) |
                    ((uint64_t)p[6] <<  8) | p[7];
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

/* Decode a little-endian 8-byte IEEE 754 double (QTDSQLASC). */
static double decode_f64_le(const uint8_t *p) {
    uint64_t bits = (uint64_t)p[0] | ((uint64_t)p[1] << 8) |
                    ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24) |
                    ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
                    ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

/*
 * Decode packed BCD.
 * nbytes : wire byte count (col->length).
 * scale  : decimal places (col->scale; 0 = integer / unknown).
 *
 * Format: each byte = 2 nibbles (high = first digit, low = second).
 * Last byte: high nibble = last digit, low nibble = sign
 *   0xC, 0xA, 0xE, 0xF = positive   0xD, 0xB = negative
 */
static int decode_packed_decimal(const uint8_t *data, uint8_t nbytes,
                                  uint8_t scale, sdb2_value_t *out) {
    if (nbytes == 0 || nbytes > 32) return SDB2_ERR_PROTO;

    int ndigits = 2 * (int)nbytes - 1;
    uint8_t digits[63];

    /* Extract all digits */
    for (int i = 0; i < (int)nbytes - 1; i++) {
        digits[i * 2]     = (data[i] >> 4) & 0x0F;
        digits[i * 2 + 1] = data[i] & 0x0F;
    }
    digits[ndigits - 1] = (data[nbytes - 1] >> 4) & 0x0F;
    uint8_t sign_nibble  = data[nbytes - 1] & 0x0F;

    /* Validate BCD digits */
    for (int i = 0; i < ndigits; i++) {
        if (digits[i] > 9) return SDB2_ERR_PROTO;
    }

    int negative = (sign_nibble == 0xD || sign_nibble == 0xB);
    char *p = out->v.str.data;
    int   pos = 0;

    /* Suppress negative zero */
    int all_zero = 1;
    for (int i = 0; i < ndigits && all_zero; i++)
        if (digits[i]) all_zero = 0;

    if (negative && !all_zero) p[pos++] = '-';

    if (scale == 0 || (int)scale >= ndigits) {
        /* Integer: trim leading zeros, keep at least one digit */
        int start = 0;
        while (start < ndigits - 1 && digits[start] == 0) start++;
        for (int i = start; i < ndigits; i++) p[pos++] = '0' + digits[i];
    } else {
        int int_digits = ndigits - (int)scale;
        /* Integer part: trim leading zeros, keep at least one */
        int start = 0;
        while (start < int_digits - 1 && digits[start] == 0) start++;
        for (int i = start; i < int_digits; i++) p[pos++] = '0' + digits[i];
        p[pos++] = '.';
        for (int i = int_digits; i < ndigits; i++) p[pos++] = '0' + digits[i];
    }

    p[pos] = '\0';
    out->v.str.len = pos;
    out->type      = SDB2_C_DECIMAL;
    return SDB2_OK;
}

int fdoca_decode_col(const fdoca_col_t *col,
                     const uint8_t *data, size_t avail,
                     sdb2_value_t *out) {
    out->type    = col->sdb2_type;
    out->is_null = 0;

    /* Consume null indicator for nullable columns. */
    if (col->nullable) {
        if (avail < 1) return SDB2_NEED_MORE;
        if (data[0] & 0x80) {
            out->is_null = 1;
            return SDB2_OK;
        }
        data++;
        avail--;
    }

    switch (col->sdb2_type) {

    case SDB2_C_BOOL:
        if (avail < 1) return SDB2_NEED_MORE;
        out->v.b = (int8_t)data[0];
        return SDB2_OK;

    case SDB2_C_INT16:
        if (avail < 2) return SDB2_NEED_MORE;
        out->v.i16 = (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
        return SDB2_OK;

    case SDB2_C_INT32:
        if (avail < 4) return SDB2_NEED_MORE;
        out->v.i32 = (int32_t)((uint32_t)data[0] | ((uint32_t)data[1] <<  8) |
                               ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24));
        return SDB2_OK;

    case SDB2_C_INT64:
        if (avail < 8) return SDB2_NEED_MORE;
        out->v.i64 = (int64_t)((uint64_t)data[0] | ((uint64_t)data[1] <<  8) |
                               ((uint64_t)data[2] << 16) | ((uint64_t)data[3] << 24) |
                               ((uint64_t)data[4] << 32) | ((uint64_t)data[5] << 40) |
                               ((uint64_t)data[6] << 48) | ((uint64_t)data[7] << 56));
        return SDB2_OK;

    case SDB2_C_FLOAT32:
        if (avail < 4) return SDB2_NEED_MORE;
        out->v.f32 = decode_f32_le(data);
        return SDB2_OK;

    case SDB2_C_FLOAT64:
        if (avail < 8) return SDB2_NEED_MORE;
        out->v.f64 = decode_f64_le(data);
        return SDB2_OK;

    case SDB2_C_DECIMAL:
        if (avail < col->length) return SDB2_NEED_MORE;
        return decode_packed_decimal(data, (uint8_t)col->length, col->scale, out);

    case SDB2_C_DATE:
        if (avail < 10) return SDB2_NEED_MORE;
        memcpy(out->v.str.data, data, 10);
        out->v.str.data[10] = '\0';
        out->v.str.len      = 10;
        return SDB2_OK;

    case SDB2_C_TIME:
        if (avail < 8) return SDB2_NEED_MORE;
        memcpy(out->v.str.data, data, 8);
        out->v.str.data[8] = '\0';
        out->v.str.len     = 8;
        return SDB2_OK;

    case SDB2_C_TIMESTAMP:
        if (avail < 26) return SDB2_NEED_MORE;
        memcpy(out->v.str.data, data, 26);
        out->v.str.data[26] = '\0';
        out->v.str.len      = 26;
        return SDB2_OK;

    case SDB2_C_CHAR: {
        if (avail < col->length) return SDB2_NEED_MORE;
        int n = ccsid_wire_to_utf8(col->ccsid ? col->ccsid : CCSID_UTF8,
                                   data, col->length,
                                   (uint8_t *)out->v.str.data, SDB2_VALUE_STR_MAX - 1);
        if (n < 0) return n;
        out->v.str.data[n] = '\0';
        out->v.str.len     = n;
        return SDB2_OK;
    }

    case SDB2_C_VARCHAR:
    case SDB2_C_LONG_VARCHAR: {
        if (avail < 2) return SDB2_NEED_MORE;
        /* VCS length prefix is big-endian (DRDA protocol level) */
        uint16_t vlen = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
        if (avail < 2u + vlen) return SDB2_NEED_MORE;
        size_t src_len = vlen < (SDB2_VALUE_STR_MAX - 1) ? vlen : (SDB2_VALUE_STR_MAX - 1);
        int n = ccsid_wire_to_utf8(col->ccsid ? col->ccsid : CCSID_UTF8,
                                   data + 2, src_len,
                                   (uint8_t *)out->v.str.data, SDB2_VALUE_STR_MAX - 1);
        if (n < 0) return n;
        out->v.str.data[n] = '\0';
        out->v.str.len     = n;
        return SDB2_OK;
    }

    case SDB2_C_BINARY: {
        if (avail < col->length) return SDB2_NEED_MORE;
        size_t n = col->length < SDB2_VALUE_STR_MAX ? col->length : SDB2_VALUE_STR_MAX;
        memcpy(out->v.str.data, data, n);
        out->v.str.len = (int)n;
        return SDB2_OK;
    }

    case SDB2_C_VARBINARY: {
        if (avail < 2) return SDB2_NEED_MORE;
        /* VCS length prefix is big-endian (DRDA protocol level) */
        uint16_t vlen = ((uint16_t)data[0] << 8) | (uint16_t)data[1];
        if (avail < 2u + vlen) return SDB2_NEED_MORE;
        size_t n = vlen < SDB2_VALUE_STR_MAX ? vlen : SDB2_VALUE_STR_MAX;
        memcpy(out->v.str.data, data + 2, n);
        out->v.str.len = (int)n;
        return SDB2_OK;
    }

    default:
        return SDB2_ERR_UNSUPPORT;
    }
}

int sdb2_stmt_get_col(sdb2_stmt_t *stmt, int col_idx, sdb2_value_t *out) {
    if (!stmt || !out) return SDB2_ERR_BADARG;
    if (!stmt->cur_row)  return SDB2_ERR_NODATA;
    if (col_idx < 0 || col_idx >= stmt->cols.ncols) return SDB2_ERR_BADARG;

    const uint8_t *p    = stmt->cur_row;
    size_t         avail = stmt->cur_row_len;

    /* Walk past preceding columns to reach col_idx. */
    for (int i = 0; i < col_idx; i++) {
        const fdoca_col_t *c = &stmt->cols.cols[i];
        int is_null = 0;

        if (c->nullable) {
            if (avail < 1) return SDB2_NEED_MORE;
            is_null = (p[0] & 0x80) ? 1 : 0;
            p++; avail--;
        }

        if (!is_null) {
            size_t sz = fdoca_col_wire_size(c, p, avail);
            if (sz == 0 && avail < 2) return SDB2_NEED_MORE;
            if (sz > avail) return SDB2_ERR_PROTO;
            p += sz; avail -= sz;
        }
    }

    return fdoca_decode_col(&stmt->cols.cols[col_idx], p, avail, out);
}

/* ================================================================
 * Phase 12 — parameter encoding (C value → DRDA wire bytes)
 * ================================================================ */

#include "staticdb2/sql.h"   /* SQL_C_* / SQL_* constants */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

fdoca_col_t fdoca_sql_type_to_col(short sql_type, unsigned int col_size, short dec_digits) {
    fdoca_col_t c;
    memset(&c, 0, sizeof c);
    c.nullable = 1; /* always nullable — includes indicator byte */

    switch (sql_type) {
    case SQL_SMALLINT:
        c.drda_type = DT_SMALL   + 1; c.length = 2;  c.sdb2_type = SDB2_C_INT16;   break;
    case SQL_INTEGER:
        c.drda_type = DT_INTEGER + 1; c.length = 4;  c.sdb2_type = SDB2_C_INT32;   break;
    case SQL_BIGINT:
        c.drda_type = DT_INTEGER8+ 1; c.length = 8;  c.sdb2_type = SDB2_C_INT64;   break;
    case SQL_REAL:
        c.drda_type = DT_FLOAT4  + 1; c.length = 4;  c.sdb2_type = SDB2_C_FLOAT32; break;
    case SQL_DOUBLE: case SQL_FLOAT:
        c.drda_type = DT_FLOAT8  + 1; c.length = 8;  c.sdb2_type = SDB2_C_FLOAT64; break;
    case SQL_CHAR:
        c.drda_type = DT_CHAR    + 1;
        c.length    = col_size ? (uint16_t)col_size : 1;
        c.sdb2_type = SDB2_C_CHAR; c.ccsid = 1208; break;
    case SQL_VARCHAR:
        c.drda_type = DT_VARCHAR + 1;
        c.length    = col_size ? (uint16_t)col_size : 255;
        c.sdb2_type = SDB2_C_VARCHAR; c.ccsid = 1208; break;
    case SQL_LONGVARCHAR:
        c.drda_type = DT_LONG    + 1;
        c.length    = col_size ? (uint16_t)col_size : 32700;
        c.sdb2_type = SDB2_C_LONG_VARCHAR; c.ccsid = 1208; break;
    case SQL_DECIMAL: case SQL_NUMERIC:
        c.precision = col_size ? (uint8_t)col_size : 18;
        c.scale     = (uint8_t)dec_digits;
        c.length    = (uint16_t)((c.precision + 2) / 2);
        c.drda_type = DT_DECIMAL + 1; c.sdb2_type = SDB2_C_DECIMAL; break;
    case SQL_DATE:
        c.drda_type = DT_DATE      + 1; c.length = 10; c.sdb2_type = SDB2_C_DATE;      break;
    case SQL_TIME:
        c.drda_type = DT_TIME      + 1; c.length = 8;  c.sdb2_type = SDB2_C_TIME;      break;
    case SQL_TIMESTAMP:
        c.drda_type = DT_TIMESTAMP + 1; c.length = 26; c.sdb2_type = SDB2_C_TIMESTAMP; break;
    case SQL_BINARY:
        c.drda_type = DT_FIXBYTE   + 1;
        c.length    = col_size ? (uint16_t)col_size : 1;
        c.sdb2_type = SDB2_C_BINARY; break;
    case SQL_VARBINARY:
        c.drda_type = DT_VARBYTE   + 1;
        c.length    = col_size ? (uint16_t)col_size : 255;
        c.sdb2_type = SDB2_C_VARBINARY; break;
    default: /* fallback to VARCHAR */
        c.drda_type = DT_VARCHAR   + 1;
        c.length    = col_size ? (uint16_t)col_size : 255;
        c.sdb2_type = SDB2_C_VARCHAR; c.ccsid = 1208; break;
    }
    return c;
}

/* Encode packed-BCD from a decimal string (e.g. "123.45") into *b.
 * nbytes = wire byte count = ceil((precision+1)/2).
 * scale  = number of fractional digits.
 */
static int encode_packed_decimal(buf_t *b, const char *str,
                                  uint8_t nbytes, uint8_t scale) {
    if (nbytes == 0 || nbytes > 32) return SDB2_ERR_BADARG;

    int ndigits = (int)nbytes * 2 - 1; /* = precision */

    /* Parse sign + digit string, strip decimal point */
    int negative = 0;
    const char *p = str;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '-')      { negative = 1; p++; }
    else if (*p == '+') { p++; }

    /* Separate integer and fractional parts */
    char ipart[64] = {0}; int idig = 0;
    char fpart[64] = {0}; int fdig = 0;
    int in_frac = 0;
    for (; *p; p++) {
        if (*p == '.') { in_frac = 1; continue; }
        if (*p < '0' || *p > '9') continue;
        if (!in_frac && idig < 63) ipart[idig++] = (char)(*p - '0');
        else if (in_frac && fdig < 63) fpart[fdig++] = (char)(*p - '0');
    }
    /* Pad fractional part to scale */
    while (fdig < (int)scale) fpart[fdig++] = 0;

    /* Build full `ndigits` digit array (left-zero-padded) */
    char digits[64] = {0};
    int total_src = idig + (int)scale;
    int pad = ndigits - total_src;
    if (pad < 0) { idig += pad; pad = 0; if (idig < 0) idig = 0; } /* overflow: truncate */
    for (int i = 0; i < pad; i++) digits[i] = 0;
    for (int i = 0; i < idig; i++) digits[pad + i] = ipart[i];
    for (int i = 0; i < (int)scale; i++) digits[pad + idig + i] = fpart[i];

    /* Detect all-zero → suppress negative */
    int all_zero = 1;
    for (int i = 0; i < ndigits && all_zero; i++) if (digits[i]) all_zero = 0;
    if (all_zero) negative = 0;

    /* Write packed bytes */
    int r;
    for (int i = 0; i < (int)nbytes - 1; i++) {
        uint8_t byte = (uint8_t)((uint8_t)(digits[i*2] << 4) | (uint8_t)digits[i*2+1]);
        r = buf_ensure(b, 1); if (r) return r;
        buf_put_u8(b, byte);
    }
    uint8_t last = (uint8_t)((uint8_t)(digits[ndigits-1] << 4) | (negative ? 0x0Du : 0x0Cu));
    r = buf_ensure(b, 1); if (r) return r;
    buf_put_u8(b, last);
    return SDB2_OK;
}

int fdoca_encode_col(buf_t *b, const fdoca_col_t *col,
                     short c_type, const void *value_ptr,
                     long buf_len, long ind) {
    /* Determine is_null */
    int is_null = (ind == SQL_NULL_DATA) || (!value_ptr);

    /* Write null indicator for nullable columns */
    if (col->nullable) {
        int r = buf_ensure(b, 1); if (r) return r;
        buf_put_u8(b, is_null ? (uint8_t)0xFF : (uint8_t)0x00);
    }
    if (is_null) return SDB2_OK;

    uint8_t base = col->drda_type & ~1u;
    char tmp[128];

    switch (base) {
    case DT_SMALL: {
        int16_t v;
        switch (c_type) {
        case SQL_C_SHORT:   v = *(const int16_t *)value_ptr; break;
        case SQL_C_LONG:    v = (int16_t)*(const int32_t *)value_ptr; break;
        case SQL_C_SBIGINT: v = (int16_t)*(const int64_t *)value_ptr; break;
        case SQL_C_FLOAT:   v = (int16_t)*(const float *)value_ptr; break;
        case SQL_C_DOUBLE:  v = (int16_t)*(const double *)value_ptr; break;
        case SQL_C_CHAR:    v = (int16_t)atoi((const char *)value_ptr); break;
        default:            v = 0; break;
        }
        return buf_put_u16_le(b, (uint16_t)v);
    }
    case DT_INTEGER: {
        int32_t v;
        switch (c_type) {
        case SQL_C_SHORT:   v = *(const int16_t *)value_ptr; break;
        case SQL_C_LONG:    v = *(const int32_t *)value_ptr; break;
        case SQL_C_SBIGINT: v = (int32_t)*(const int64_t *)value_ptr; break;
        case SQL_C_FLOAT:   v = (int32_t)*(const float *)value_ptr; break;
        case SQL_C_DOUBLE:  v = (int32_t)*(const double *)value_ptr; break;
        case SQL_C_CHAR:    v = atoi((const char *)value_ptr); break;
        default:            v = 0; break;
        }
        return buf_put_u32_le(b, (uint32_t)v);
    }
    case DT_INTEGER8: {
        int64_t v;
        switch (c_type) {
        case SQL_C_SHORT:   v = *(const int16_t *)value_ptr; break;
        case SQL_C_LONG:    v = *(const int32_t *)value_ptr; break;
        case SQL_C_SBIGINT: v = *(const int64_t *)value_ptr; break;
        case SQL_C_FLOAT:   v = (int64_t)*(const float *)value_ptr; break;
        case SQL_C_DOUBLE:  v = (int64_t)*(const double *)value_ptr; break;
        case SQL_C_CHAR:    v = (int64_t)atoll((const char *)value_ptr); break;
        default:            v = 0; break;
        }
        return buf_put_u64_le(b, (uint64_t)v);
    }
    case DT_FLOAT4: {
        float f;
        switch (c_type) {
        case SQL_C_SHORT:   f = (float)*(const int16_t *)value_ptr; break;
        case SQL_C_LONG:    f = (float)*(const int32_t *)value_ptr; break;
        case SQL_C_SBIGINT: f = (float)*(const int64_t *)value_ptr; break;
        case SQL_C_FLOAT:   f = *(const float *)value_ptr; break;
        case SQL_C_DOUBLE:  f = (float)*(const double *)value_ptr; break;
        case SQL_C_CHAR:    f = (float)atof((const char *)value_ptr); break;
        default:            f = 0.0f; break;
        }
        uint32_t bits; memcpy(&bits, &f, 4);
        return buf_put_u32_le(b, bits);
    }
    case DT_FLOAT8: {
        double d;
        switch (c_type) {
        case SQL_C_SHORT:   d = (double)*(const int16_t *)value_ptr; break;
        case SQL_C_LONG:    d = (double)*(const int32_t *)value_ptr; break;
        case SQL_C_SBIGINT: d = (double)*(const int64_t *)value_ptr; break;
        case SQL_C_FLOAT:   d = (double)*(const float *)value_ptr; break;
        case SQL_C_DOUBLE:  d = *(const double *)value_ptr; break;
        case SQL_C_CHAR:    d = atof((const char *)value_ptr); break;
        default:            d = 0.0; break;
        }
        uint64_t bits; memcpy(&bits, &d, 8);
        return buf_put_u64_le(b, bits);
    }
    case DT_CHAR: {
        /* Fixed-length: write `col->length` bytes, space-pad if short */
        const char *s;
        int slen;
        if (c_type == SQL_C_CHAR || c_type == SQL_C_BINARY) {
            s    = (const char *)value_ptr;
            slen = (ind == SQL_NTS || ind < 0) ? (int)strlen(s) : (int)ind;
        } else {
            switch (c_type) {
            case SQL_C_SHORT:   snprintf(tmp, sizeof tmp, "%d",   (int)*(const int16_t *)value_ptr); break;
            case SQL_C_LONG:    snprintf(tmp, sizeof tmp, "%d",   *(const int32_t *)value_ptr); break;
            case SQL_C_SBIGINT: snprintf(tmp, sizeof tmp, "%lld", (long long)*(const int64_t *)value_ptr); break;
            case SQL_C_FLOAT:   snprintf(tmp, sizeof tmp, "%.7g", (double)*(const float *)value_ptr); break;
            case SQL_C_DOUBLE:  snprintf(tmp, sizeof tmp, "%.15g",*(const double *)value_ptr); break;
            default:            tmp[0] = '\0'; break;
            }
            s = tmp; slen = (int)strlen(tmp);
        }
        int fixed = (int)col->length;
        int copy  = (slen < fixed) ? slen : fixed;
        int r = buf_ensure(b, (size_t)fixed); if (r) return r;
        if (copy > 0) buf_put_bytes(b, (const uint8_t *)s, (size_t)copy);
        for (int i = copy; i < fixed; i++) buf_put_u8(b, 0x20u); /* space pad */
        return SDB2_OK;
    }
    case DT_LONG:
    case DT_VARCHAR: {
        /* Variable: 2-byte length + data */
        const char *s;
        int slen;
        if (c_type == SQL_C_CHAR || c_type == SQL_C_BINARY) {
            s    = (const char *)value_ptr;
            slen = (ind == SQL_NTS || ind < 0) ? (int)strlen(s) : (int)ind;
        } else {
            switch (c_type) {
            case SQL_C_SHORT:   snprintf(tmp, sizeof tmp, "%d",   (int)*(const int16_t *)value_ptr); break;
            case SQL_C_LONG:    snprintf(tmp, sizeof tmp, "%d",   *(const int32_t *)value_ptr); break;
            case SQL_C_SBIGINT: snprintf(tmp, sizeof tmp, "%lld", (long long)*(const int64_t *)value_ptr); break;
            case SQL_C_FLOAT:   snprintf(tmp, sizeof tmp, "%.7g", (double)*(const float *)value_ptr); break;
            case SQL_C_DOUBLE:  snprintf(tmp, sizeof tmp, "%.15g",*(const double *)value_ptr); break;
            default:            tmp[0] = '\0'; break;
            }
            s = tmp; slen = (int)strlen(tmp);
        }
        int r = buf_put_u16(b, (uint16_t)slen); if (r) return r;
        return buf_put_bytes(b, (const uint8_t *)s, (size_t)slen);
    }
    case DT_FIXBYTE: {
        /* Fixed binary: write `col->length` bytes, zero-pad if short */
        int fixed  = (int)col->length;
        int avail2 = (int)(ind >= 0 ? ind : buf_len);
        int copy   = (avail2 < fixed) ? avail2 : fixed;
        int r = buf_ensure(b, (size_t)fixed); if (r) return r;
        if (copy > 0) buf_put_bytes(b, (const uint8_t *)value_ptr, (size_t)copy);
        for (int i = copy; i < fixed; i++) buf_put_u8(b, 0x00u);
        return SDB2_OK;
    }
    case DT_LONGVARBYTE:
    case DT_VARBYTE: {
        /* Variable binary: 2-byte length + data */
        int slen = (int)(ind >= 0 ? ind : buf_len);
        int r = buf_put_u16(b, (uint16_t)slen); if (r) return r;
        return buf_put_bytes(b, (const uint8_t *)value_ptr, (size_t)slen);
    }
    case DT_DATE: {
        /* 10 ASCII bytes "YYYY-MM-DD" */
        const char *s = (c_type == SQL_C_CHAR || c_type == SQL_C_DATE)
                        ? (const char *)value_ptr : "0001-01-01";
        int r = buf_ensure(b, 10); if (r) return r;
        buf_put_bytes(b, (const uint8_t *)s, 10);
        return SDB2_OK;
    }
    case DT_TIME: {
        /* 8 ASCII bytes "HH:MM:SS" */
        const char *s = (c_type == SQL_C_CHAR || c_type == SQL_C_TIME)
                        ? (const char *)value_ptr : "00:00:00";
        int r = buf_ensure(b, 8); if (r) return r;
        buf_put_bytes(b, (const uint8_t *)s, 8);
        return SDB2_OK;
    }
    case DT_TIMESTAMP: {
        /* 26 ASCII bytes "YYYY-MM-DD-HH.MM.SS.ffffff" */
        const char *s = (c_type == SQL_C_CHAR || c_type == SQL_C_TIMESTAMP)
                        ? (const char *)value_ptr : "0001-01-01-00.00.00.000000";
        int r = buf_ensure(b, 26); if (r) return r;
        buf_put_bytes(b, (const uint8_t *)s, 26);
        return SDB2_OK;
    }
    case DT_DECIMAL: case DT_ZDECIMAL: {
        /* Packed BCD, col->length bytes */
        const char *s;
        if (c_type == SQL_C_CHAR || c_type == SQL_C_NUMERIC) {
            s = (const char *)value_ptr;
        } else {
            switch (c_type) {
            case SQL_C_SHORT:   snprintf(tmp, sizeof tmp, "%d",   (int)*(const int16_t *)value_ptr); break;
            case SQL_C_LONG:    snprintf(tmp, sizeof tmp, "%d",   *(const int32_t *)value_ptr); break;
            case SQL_C_SBIGINT: snprintf(tmp, sizeof tmp, "%lld", (long long)*(const int64_t *)value_ptr); break;
            case SQL_C_FLOAT:   snprintf(tmp, sizeof tmp, "%.7g", (double)*(const float *)value_ptr); break;
            case SQL_C_DOUBLE:  snprintf(tmp, sizeof tmp, "%.15g",*(const double *)value_ptr); break;
            default:            snprintf(tmp, sizeof tmp, "0");   break;
            }
            s = tmp;
        }
        return encode_packed_decimal(b, s, (uint8_t)col->length, col->scale);
    }
    default:
        /* Unknown: zero-fill to col->length */
        for (uint16_t i = 0; i < col->length; i++) buf_put_u8(b, 0x00u);
        return SDB2_OK;
    }
}

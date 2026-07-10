#ifndef SDB2_DSS_H
#define SDB2_DSS_H

/*
 * dss.h — DDM Structured Stream (DSS) framing layer.
 *
 * Every DRDA packet is wrapped in one or more 6-byte DSS envelopes:
 *
 *   [ll ll] [D0] [ff] [cc cc]
 *    length  magic fmt  corr-id
 *
 *   ll ll  : total DSS length including the 6-byte header (big-endian).
 *            0xFFFF (continuation bit set) means 32767 bytes + more to follow.
 *   D0     : mandatory GDS magic byte.
 *   ff     : format byte — DSS type in low nibble, chaining bits in high nibble.
 *   cc cc  : request correlation ID (big-endian).
 *
 * The DSS is followed immediately by zero or more DDM objects.
 *
 * Usage for writing:
 *   dss_begin_rqsdss(buf, corrId, chained, same_id);
 *     ddm_put_scalar(...);
 *     ...
 *   dss_finalize(buf);   // backfills the length field
 *
 * Usage for reading:
 *   dss_read_header(buf, &hdr);   // consumes 6 bytes, fills dss_hdr_t
 */

#include <stdint.h>
#include "buf.h"

/* ---- Constants ---- */

#define DSS_HDR_LEN      6        /* DSS header is always 6 bytes */
#define DSS_MAX_LEN      32767    /* 0x7FFF — max DSS frame size including header */
#define DSS_MAGIC        0xD0     /* mandatory second byte of every DSS header */

/* Format byte — DSS type (bits 1-0) */
#define DSS_FMT_RQSDSS   0x01    /* request DSS */
#define DSS_FMT_RPYDSS   0x02    /* reply DSS */
#define DSS_FMT_OBJDSS   0x03    /* object DSS (chained data object) */

/* Format byte — chaining bits (bits 7-4) */
#define DSS_CHAIN        0x40    /* bit 6: this DSS is chained to the next */
#define DSS_CHAIN_SAMEID 0x10    /* bit 4: next DSS shares the same corr-id */

/* ---- Parsed DSS header ---- */
typedef struct {
    uint16_t payload_len; /* bytes remaining in DSS after the 6-byte header */
    uint8_t  fmt;         /* raw format byte */
    uint16_t corr_id;     /* correlation id */
    int      chained;     /* 1 → another DSS follows */
    int      same_id;     /* 1 → next DSS has the same corr_id */
    int      continued;   /* 1 → continuation segment (length was 0x7FFF) */
} dss_hdr_t;

/* ---- Writers ---- */

/*
 * Begin a Request DSS (RQSDSS) at the current buffer write position.
 * chained  : 1 if this DSS is immediately followed by another DSS.
 * same_id  : 1 if the following DSS shares the same correlation id
 *            (used for OBJDSS data appended to the same command).
 *
 * Internally uses buf_mark_len2 to reserve the 2-byte length field;
 * call dss_finalize() when all DDM content has been written.
 */
int dss_begin_rqsdss(buf_t *b, uint16_t corr_id, int chained, int same_id);

/*
 * Begin an Object DSS (OBJDSS) at the current buffer write position.
 * Used for chained data objects within the same DRDA command.
 */
int dss_begin_objdss(buf_t *b, uint16_t corr_id, int chained, int same_id);

/*
 * Finalise the current DSS: backfill its 2-byte length field with the total
 * number of bytes from the start of the DSS header to the current position.
 * Must be matched to a preceding dss_begin_* call.
 *
 * Note: this implementation supports DSS frames up to 32767 bytes.
 * DSS continuation (frames > 32767) is not yet implemented.
 */
int dss_finalize(buf_t *b);

/* ---- Readers ---- */

/*
 * Parse a 6-byte DSS header from the read buffer, advancing buf->pos by 6.
 * Populates *hdr.
 * Returns SDB2_NEED_MORE if < 6 bytes remain, SDB2_ERR_PROTO on bad magic.
 */
int dss_read_header(buf_t *b, dss_hdr_t *hdr);

#endif /* SDB2_DSS_H */

#ifndef SDB2_CCSID_H
#define SDB2_CCSID_H

/*
 * ccsid.h — character-set conversion between UTF-8 and DRDA wire encodings.
 *
 * Supported CCSIDs:
 *   1208  UTF-8  — identity passthrough (modern DB2 LUW with UTF-8 agents)
 *    367  ASCII  — identity for 0x00-0x7F; error on bytes > 0x7F
 *    500  EBCDIC — full 256-entry IBM Code Page 500 lookup tables
 *     37  EBCDIC — same tables (CCSID 037 is compatible for the ASCII subset)
 *
 * All DRDA DDM control-data fields (RDBNAM, EXTNAM, SRVNAM, …) are encoded
 * as EBCDIC even when the agent uses ASCII/UTF-8 for user data, until the
 * client negotiates TYPDEFNAM=QTDSQLASC in the ACCRDB command (Phase 5).
 */

#include <stdint.h>
#include <stddef.h>

#define CCSID_UTF8   1208
#define CCSID_ASCII   367
#define CCSID_EBCDIC  500
#define CCSID_EBCDIC_US 37   /* US EBCDIC; shares code-page-500 tables here */

/*
 * Encode src_len bytes of UTF-8 text to the wire encoding for ccsid.
 * out must hold at least out_cap bytes.
 * Returns the number of bytes written, or a negative sdb2_err_t on error.
 * SDB2_ERR_TRUNC is returned when out_cap is too small.
 * SDB2_ERR_UNSUPPORT for an unrecognised CCSID.
 */
int ccsid_utf8_to_wire(int ccsid, const uint8_t *src, size_t src_len,
                       uint8_t *out, size_t out_cap);

/*
 * Decode src_len bytes in the given wire encoding back to UTF-8.
 * out must hold at least out_cap bytes.
 * Returns bytes written, or a negative sdb2_err_t.
 */
int ccsid_wire_to_utf8(int ccsid, const uint8_t *src, size_t src_len,
                       uint8_t *out, size_t out_cap);

/*
 * Convenience: encode a NUL-terminated C string and write it directly into
 * a buf_t at the current write position (does NOT write a length prefix).
 * Returns bytes written or negative error.
 */
#include "buf.h"
int ccsid_put_string(buf_t *b, int ccsid, const char *str);

#endif /* SDB2_CCSID_H */

#include "sqlca.h"
#include <string.h>

/*
 * SQLCAGRP FD:OCA format (SQLAM >= 7, our target):
 *
 *   null_indicator  : 1 byte  (0xFF = NULLDATA → no SQLCA)
 * Integers (SQLCODE, SQLERRD) are little-endian per QTDSQLASC; VCS/VCM
 * length prefixes are big-endian (DDM protocol level).
 *   SQLSTATE        : FCS[5]  (5 bytes, ASCII)
 *   SQLERRPROC      : FCS[8]  (8 bytes)
 *   SQLCAXGRP       : N-GDA (nullable group):
 *     null_indicator: 1 byte  (0xFF → skip rest of group)
 *     SQLERRD1..6   : 6 × I4 = 24 bytes
 *     SQLWARN0..A   : 11 × FCS[1] = 11 bytes
 *     SQLRDBNAME    : VCS (2-byte len + chars) — skip
 *     SQLERRMC_m    : VCM (2-byte len + bytes) — keep if len > 0
 *     SQLERRMC_s    : VCS (2-byte len + bytes) — keep if VCM was empty
 *   SQLDIAGGRP      : N-GDA (nullable), 1-byte null indicator then skip
 */
int sqlca_parse(buf_t *b, sdb2_sqlca_t *out) {
    memset(out, 0, sizeof(*out));

    if (buf_remaining(b) < 1) return SDB2_NEED_MORE;

    uint8_t null_ind;
    if (buf_get_u8(b, &null_ind)) return SDB2_NEED_MORE;
    if (null_ind == 0xFF) {
        /* NULLDATA — no SQLCA, implied success */
        out->has_sqlca = 0;
        return SDB2_OK;
    }

    out->has_sqlca = 1;

    /* SQLCODE: I4 signed, little-endian (QTDSQLASC) */
    uint32_t raw_code;
    if (buf_get_u32_le(b, &raw_code)) return SDB2_NEED_MORE;
    out->sqlcode = (int32_t)raw_code;

    /* SQLSTATE: FCS[5] */
    if (buf_get_bytes(b, (uint8_t *)out->sqlstate, 5)) return SDB2_NEED_MORE;
    out->sqlstate[5] = '\0';

    /* SQLERRPROC: FCS[8] */
    if (buf_get_bytes(b, (uint8_t *)out->sqlerrproc, 8)) return SDB2_NEED_MORE;
    out->sqlerrproc[8] = '\0';

    /* SQLCAXGRP — nullable */
    uint8_t cax_ind;
    if (buf_get_u8(b, &cax_ind)) return SDB2_NEED_MORE;
    if (cax_ind == 0xFF) {
        out->has_caxgrp = 0;
    } else {
        out->has_caxgrp = 1;

        /* SQLERRD1..6: 6 × I4, little-endian (QTDSQLASC) */
        for (int i = 0; i < 6; i++) {
            uint32_t v;
            if (buf_get_u32_le(b, &v)) return SDB2_NEED_MORE;
            out->sqlerrd[i] = (int32_t)v;
        }

        /* SQLWARN0..A: 11 bytes */
        if (buf_get_bytes(b, (uint8_t *)out->sqlwarn, 11)) return SDB2_NEED_MORE;
        out->sqlwarn[11] = '\0';

        /* SQLRDBNAME: VCS — 2-byte BE len + chars, skip */
        uint16_t rdbname_len;
        if (buf_get_u16(b, &rdbname_len)) return SDB2_NEED_MORE;
        if (rdbname_len > 0) {
            if (buf_remaining(b) < rdbname_len) return SDB2_NEED_MORE;
            b->pos += rdbname_len;
        }

        /* SQLERRMC_m: VCM — 2-byte BE len + bytes */
        uint16_t vcm_len;
        if (buf_get_u16(b, &vcm_len)) return SDB2_NEED_MORE;
        if (vcm_len > 0) {
            uint16_t copy_len = vcm_len;
            if (copy_len > SQLCA_ERRMSG_MAX) copy_len = SQLCA_ERRMSG_MAX;
            if (buf_remaining(b) < vcm_len) return SDB2_NEED_MORE;
            if (buf_get_bytes(b, out->errmsg, copy_len)) return SDB2_NEED_MORE;
            if (vcm_len > copy_len) b->pos += (vcm_len - copy_len);
            out->errmsg_len = copy_len;
            /* skip VCS field (we used VCM) */
            uint16_t vcs_skip;
            if (buf_get_u16(b, &vcs_skip)) return SDB2_NEED_MORE;
            if (vcs_skip > 0) {
                if (buf_remaining(b) < vcs_skip) return SDB2_NEED_MORE;
                b->pos += vcs_skip;
            }
        } else {
            /* VCM was empty — read VCS */
            uint16_t vcs_len;
            if (buf_get_u16(b, &vcs_len)) return SDB2_NEED_MORE;
            if (vcs_len > 0) {
                uint16_t copy_len = vcs_len;
                if (copy_len > SQLCA_ERRMSG_MAX) copy_len = SQLCA_ERRMSG_MAX;
                if (buf_remaining(b) < vcs_len) return SDB2_NEED_MORE;
                if (buf_get_bytes(b, out->errmsg, copy_len)) return SDB2_NEED_MORE;
                if (vcs_len > copy_len) b->pos += (vcs_len - copy_len);
                out->errmsg_len = copy_len;
            }
        }
    }

    /* SQLDIAGGRP — nullable, skip entire group */
    if (buf_remaining(b) >= 1) {
        uint8_t diag_ind;
        if (buf_get_u8(b, &diag_ind)) return SDB2_NEED_MORE;
        if (diag_ind != 0xFF && buf_remaining(b) > 0) {
            /* Skip remainder — SQLDIAGGRP is complex; not needed for basic operation */
            b->pos = b->limit;
        }
    }

    return SDB2_OK;
}

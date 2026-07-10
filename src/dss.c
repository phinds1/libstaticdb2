#include "dss.h"
#include "staticdb2/sdb2.h"

/* ---- Internal helper ---- */

static int dss_begin(buf_t *b, uint8_t dss_type,
                     uint16_t corr_id, int chained, int same_id) {
    uint8_t fmt = dss_type;
    if (chained) {
        fmt |= DSS_CHAIN;
        if (same_id) fmt |= DSS_CHAIN_SAMEID;
    }

    /* Reserve 2 bytes for the DSS length field (backfilled by dss_finalize). */
    int r = buf_mark_len2(b); if (r) return r;

    /* Write magic, format byte, and correlation id. */
    r = buf_put_u8(b, DSS_MAGIC); if (r) return r;
    r = buf_put_u8(b, fmt);       if (r) return r;
    r = buf_put_u16(b, corr_id);  if (r) return r;

    return SDB2_OK;
}

/* ---- Writers ---- */

int dss_begin_rqsdss(buf_t *b, uint16_t corr_id, int chained, int same_id) {
    return dss_begin(b, DSS_FMT_RQSDSS, corr_id, chained, same_id);
}

int dss_begin_objdss(buf_t *b, uint16_t corr_id, int chained, int same_id) {
    return dss_begin(b, DSS_FMT_OBJDSS, corr_id, chained, same_id);
}

int dss_finalize(buf_t *b) {
    return buf_fill_len2(b);
}

/* ---- Readers ---- */

int dss_read_header(buf_t *b, dss_hdr_t *hdr) {
    uint16_t raw_len, corr_id;
    uint8_t  magic, fmt;
    int r;

    if (buf_remaining(b) < DSS_HDR_LEN) return SDB2_NEED_MORE;

    r = buf_get_u16(b, &raw_len); if (r) return r;
    r = buf_get_u8(b,  &magic);   if (r) return r;
    r = buf_get_u8(b,  &fmt);     if (r) return r;
    r = buf_get_u16(b, &corr_id); if (r) return r;

    if (magic != DSS_MAGIC) return SDB2_ERR_PROTO;

    hdr->fmt     = fmt;
    hdr->corr_id = corr_id;
    hdr->chained = (fmt & DSS_CHAIN)        ? 1 : 0;
    hdr->same_id = (fmt & DSS_CHAIN_SAMEID) ? 1 : 0;

    /* Continuation: MSB set means this segment is 32767 bytes and more follow. */
    if (raw_len & 0x8000) {
        hdr->payload_len = DSS_MAX_LEN - DSS_HDR_LEN;  /* 32761 */
        hdr->continued   = 1;
    } else {
        if (raw_len < DSS_HDR_LEN) return SDB2_ERR_PROTO;
        hdr->payload_len = raw_len - DSS_HDR_LEN;
        hdr->continued   = 0;
    }

    return SDB2_OK;
}

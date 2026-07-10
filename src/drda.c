#include "drda.h"
#include "dss.h"
#include "ddm.h"
#include "ccsid.h"
#include "net.h"
#include "buf.h"
#include "sqlca.h"
#include "staticdb2/sdb2.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>

/* ---- Driver identity strings ---- */
#define DRDA_PRDID    "SDB01000"       /* product id sent in ACCRDB */
#define DRDA_SRVRLSLV "SDB01000"       /* server release level in EXCSAT */
#define DRDA_EXTNAM   "libstaticdb2"   /* external name (client process) */
#define DRDA_SRVNAM   "libstaticdb2"   /* server name field in EXCSAT */

/* Manager levels we announce in EXCSAT MGRLVLLS */
#define MGRLVL_AGENT    7
#define MGRLVL_SQLAM    7
#define MGRLVL_RDB      7
#define MGRLVL_SECMGR   7
#define MGRLVL_CMNTCPIP 5

/* RDBNAM: minimum padded length (bytes), padded with EBCDIC space (0x40) */
#define RDBNAM_MIN_LEN  18

/* CCSID for single-byte user data (UTF-8) */
#define CCSID_SBC       1208

/* Extra codepoints used only in this file (not in ddm.h yet) */
#define CP_SRVCLSNM  0x1147u   /* server class name */
#define CP_RDBALWUPD 0x211Au   /* RDB allow updates */
#define CP_CRRTKN    0x2135u   /* correlation token */
#define CP_CCSIDSBC  0x119Cu   /* single-byte CCSID override */
#define CP_CCSIDDBC  0x119Du   /* double-byte CCSID override */
#define CP_CCSIDMBC  0x119Eu   /* mixed-byte CCSID override */
#define CP_RDBINTTKN 0x2103u   /* RDB interrupt token */
#define CP_RDB       0x240Fu   /* RDB manager */
#define CP_UNICODEMGR 0x1C08u  /* Unicode manager */

/* ---- I/O helpers ---- */

int drda_flush(drda_conn_t *conn) {
    buf_flip(&conn->send_buf);
    int r = conn->io_send(conn->io_ctx,
                          conn->send_buf.data,
                          buf_remaining(&conn->send_buf));
    buf_reset(&conn->send_buf);
    return r;
}

int drda_recv_n(drda_conn_t *conn, size_t n) {
    int r = buf_ensure(&conn->recv_buf, n);
    if (r) return r;
    while (n > 0) {
        size_t got = 0;
        r = conn->io_recv(conn->io_ctx,
                          conn->recv_buf.data + conn->recv_buf.pos,
                          n, &got);
        if (r) return r;
        conn->recv_buf.pos += got;
        n -= got;
    }
    return SDB2_OK;
}

int drda_recv_dss(drda_conn_t *conn) {
    buf_reset(&conn->recv_buf);

    /* Receive 6-byte DSS header */
    int r = drda_recv_n(conn, 6);
    if (r) return r;

    /* Peek at raw length bytes (big-endian uint16 at offset 0) */
    uint16_t raw_len = ((uint16_t)conn->recv_buf.data[0] << 8)
                     |  (uint16_t)conn->recv_buf.data[1];

    uint8_t magic = conn->recv_buf.data[2];
    if (magic != DSS_MAGIC) {
        snprintf(conn->diag_msg, sizeof(conn->diag_msg),
                 "DSS magic expected 0xD0, got 0x%02X", magic);
        return SDB2_ERR_PROTO;
    }

    size_t payload_len;
    if (raw_len & 0x8000) {
        payload_len = DSS_MAX_LEN - DSS_HDR_LEN;  /* continuation */
    } else {
        if (raw_len < DSS_HDR_LEN) return SDB2_ERR_PROTO;
        payload_len = raw_len - DSS_HDR_LEN;
    }

    /* Receive DSS payload */
    r = drda_recv_n(conn, payload_len);
    if (r) return r;

    /* Flip to read mode: pos=0, limit=total_bytes */
    buf_flip(&conn->recv_buf);
    return SDB2_OK;
}

/* ---- RDBNAM helper: write RDBNAM as EBCDIC, padded to RDBNAM_MIN_LEN ---- */
static int put_rdbnam(buf_t *b, const char *db) {
    size_t dblen = strlen(db);
    /* Encode db name to EBCDIC into a temp buffer */
    uint8_t tmp[1024];
    int enc = ccsid_utf8_to_wire(CCSID_EBCDIC,
                                  (const uint8_t *)db, dblen,
                                  tmp, sizeof(tmp));
    if (enc < 0) return enc;

    /* Pad to minimum length with EBCDIC space (0x40) */
    size_t wire_len = (size_t)enc;
    if (wire_len < RDBNAM_MIN_LEN) {
        memset(tmp + wire_len, 0x40, RDBNAM_MIN_LEN - wire_len);
        wire_len = RDBNAM_MIN_LEN;
    }

    return ddm_put_scalar_bytes(b, CP_RDBNAM, tmp, wire_len);
}

/* ---- Build EXCSAT command (chained; same corrId as ACCSEC) ---- */
static int build_excsat(drda_conn_t *conn) {
    buf_t *b = &conn->send_buf;
    uint16_t cid = ++conn->corr_id;

    /* EXCSAT is chained to ACCSEC with the SAME correlation ID (same_id=1). */
    int r = dss_begin_rqsdss(b, cid, 1, 1); if (r) return r;

    r = ddm_begin_collection(b, CP_EXCSAT); if (r) return r;

    /* EXTNAM: external (client process) name */
    r = ddm_put_scalar_string(b, CP_EXTNAM, CCSID_EBCDIC, DRDA_EXTNAM);
    if (r < 0) return r;

    /* SRVNAM: server name */
    r = ddm_put_scalar_string(b, CP_SRVNAM, CCSID_EBCDIC, DRDA_SRVNAM);
    if (r < 0) return r;

    /* SRVRLSLV: product release level */
    r = ddm_put_scalar_string(b, CP_SRVRLSLV, CCSID_EBCDIC, DRDA_SRVRLSLV);
    if (r < 0) return r;

    /* MGRLVLLS: manager level list (each entry is a 4-byte cp+level pair) */
    r = ddm_begin_collection(b, CP_MGRLVLLS); if (r) return r;
        r = ddm_put_mgrlvl(b, CP_AGENT,    MGRLVL_AGENT);    if (r) return r;
        r = ddm_put_mgrlvl(b, CP_SQLAM,    MGRLVL_SQLAM);    if (r) return r;
        r = ddm_put_mgrlvl(b, CP_RDB,      MGRLVL_RDB);      if (r) return r;
        r = ddm_put_mgrlvl(b, CP_SECMGR,   MGRLVL_SECMGR);   if (r) return r;
        r = ddm_put_mgrlvl(b, CP_CMNTCPIP, MGRLVL_CMNTCPIP); if (r) return r;
    r = ddm_end_collection(b); if (r) return r;

    /* SRVCLSNM: server class name (QDERBY/JVM equivalent for our driver) */
    r = ddm_put_scalar_string(b, CP_SRVCLSNM, CCSID_EBCDIC, "QSDB/C");
    if (r < 0) return r;

    r = ddm_end_collection(b); if (r) return r;  /* close EXCSAT */
    return dss_finalize(b);
}

/* ---- Build ACCSEC command (chained after EXCSAT, same corrId) ---- */
static int build_accsec(drda_conn_t *conn, const char *db) {
    buf_t *b = &conn->send_buf;

    /* ACCSEC uses the same corrId as EXCSAT (same_id=1 on EXCSAT's DSS),
     * so we do NOT increment corr_id here. */
    int r = dss_begin_rqsdss(b, conn->corr_id, 0, 0); if (r) return r;

    r = ddm_begin_collection(b, CP_ACCSEC); if (r) return r;
    r = ddm_put_scalar_u16(b, CP_SECMEC, SECMEC_USRIDPWD); if (r) return r;
    r = put_rdbnam(b, db); if (r) return r;
    r = ddm_end_collection(b); if (r) return r;

    return dss_finalize(b);
}

/* ---- Build SECCHK command ---- */
static int build_secchk(drda_conn_t *conn,
                        const char *db,
                        const char *user,
                        const char *pass) {
    buf_t *b = &conn->send_buf;
    uint16_t cid = ++conn->corr_id;

    int r = dss_begin_rqsdss(b, cid, 0, 0); if (r) return r;

    r = ddm_begin_collection(b, CP_SECCHK); if (r) return r;
    r = ddm_put_scalar_u16(b, CP_SECMEC, SECMEC_USRIDPWD); if (r) return r;
    r = put_rdbnam(b, db); if (r) return r;
    r = ddm_put_scalar_string(b, CP_USRID,   CCSID_EBCDIC, user); if (r < 0) return r;
    r = ddm_put_scalar_string(b, CP_PASSWORD, CCSID_EBCDIC, pass); if (r < 0) return r;
    r = ddm_end_collection(b); if (r) return r;

    return dss_finalize(b);
}

/* ---- Build ACCRDB command ---- */
static int build_accrdb(drda_conn_t *conn, const char *db) {
    buf_t *b = &conn->send_buf;
    uint16_t cid = ++conn->corr_id;

    int r = dss_begin_rqsdss(b, cid, 0, 0); if (r) return r;

    r = ddm_begin_collection(b, CP_ACCRDB); if (r) return r;
    r = put_rdbnam(b, db); if (r) return r;

    /* RDBACCCL = SQLAM (manager class) */
    r = ddm_put_scalar_u16(b, CP_RDBACCCL, CP_SQLAM); if (r) return r;

    /* PRDID: our product id */
    r = ddm_put_scalar_string(b, CP_PRDID, CCSID_EBCDIC, DRDA_PRDID);
    if (r < 0) return r;

    /* TYPDEFNAM: ASCII agent */
    r = ddm_put_scalar_string(b, CP_TYPDEFNAM, CCSID_EBCDIC, TYPDEFNAM_QTDSQLASC);
    if (r < 0) return r;

    /* TYPDEFOVR collection: SBC CCSID = 1208 (UTF-8) */
    r = ddm_begin_collection(b, CP_TYPDEFOVR); if (r) return r;
        r = ddm_put_scalar_u16(b, CP_CCSIDSBC, CCSID_SBC); if (r) return r;
    r = ddm_end_collection(b); if (r) return r;

    /* CRRTKN: 6-byte correlation token (required by DB2 LUW; client-generated unique token) */
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        uint8_t crrtkn[6];
        uint32_t ts = (uint32_t)tv.tv_sec;
        crrtkn[0] = (uint8_t)(ts >> 24);
        crrtkn[1] = (uint8_t)(ts >> 16);
        crrtkn[2] = (uint8_t)(ts >>  8);
        crrtkn[3] = (uint8_t)(ts);
        crrtkn[4] = (uint8_t)(conn->corr_id >> 8);
        crrtkn[5] = (uint8_t)(conn->corr_id);
        r = ddm_put_scalar_bytes(b, CP_CRRTKN, crrtkn, 6); if (r) return r;
    }

    r = ddm_end_collection(b); if (r) return r;  /* close ACCRDB */

    return dss_finalize(b);
}

/* ---- Reply parsers ---- */

/* Parse EXCSATRD (just verify codepoint, skip contents) */
static int parse_excsatrd(drda_conn_t *conn) {
    buf_t *b = &conn->recv_buf;
    dss_hdr_t dss;
    int r = dss_read_header(b, &dss); if (r) return r;

    if (buf_remaining(b) < 4) return SDB2_NEED_MORE;
    ddm_hdr_t hdr;
    r = ddm_read_hdr(b, &hdr); if (r) return r;
    if (hdr.cp != CP_EXCSATRD) {
        snprintf(conn->diag_msg, sizeof(conn->diag_msg),
                 "expected EXCSATRD (0x%04X), got %s (0x%04X)",
                 CP_EXCSATRD, ddm_cp_name(hdr.cp), hdr.cp);
        return SDB2_ERR_PROTO;
    }
    /* Skip EXCSATRD payload (we don't need the mgrlvl response for basic operation) */
    return ddm_skip_payload(b, &hdr);
}

/* Parse ACCSECRD: extract server SECMEC */
static int parse_accsecrd(drda_conn_t *conn) {
    buf_t *b = &conn->recv_buf;
    dss_hdr_t dss;
    int r = dss_read_header(b, &dss); if (r) return r;

    ddm_hdr_t hdr;
    r = ddm_read_hdr(b, &hdr); if (r) return r;
    if (hdr.cp != CP_ACCSECRD) {
        snprintf(conn->diag_msg, sizeof(conn->diag_msg),
                 "expected ACCSECRD (0x%04X), got %s",
                 CP_ACCSECRD, ddm_cp_name(hdr.cp));
        return SDB2_ERR_PROTO;
    }

    /* ACCSECRD contains: SVRCOD (required), SECMEC (required) */
    size_t end_pos = b->pos + (hdr.len - 4);
    while (b->pos + 4 <= end_pos) {
        ddm_hdr_t inner;
        r = ddm_read_hdr(b, &inner); if (r) return r;
        if (inner.cp == CP_SECMEC && inner.len == 6) {
            uint16_t secmec;
            r = buf_get_u16(b, &secmec); if (r) return r;
            conn->srv_secmec = (int)secmec;
        } else {
            r = ddm_skip_payload(b, &inner); if (r) return r;
        }
    }
    b->pos = end_pos;  /* align to end of ACCSECRD */
    return SDB2_OK;
}

/* Parse SECCHKRM: return SDB2_OK if secchkcd==0, else diagnostic error */
static int parse_secchkrm(drda_conn_t *conn) {
    buf_t *b = &conn->recv_buf;
    dss_hdr_t dss;
    int r = dss_read_header(b, &dss); if (r) return r;

    ddm_hdr_t hdr;
    r = ddm_read_hdr(b, &hdr); if (r) return r;
    if (hdr.cp != CP_SECCHKRM) {
        snprintf(conn->diag_msg, sizeof(conn->diag_msg),
                 "expected SECCHKRM (0x%04X), got %s",
                 CP_SECCHKRM, ddm_cp_name(hdr.cp));
        return SDB2_ERR_PROTO;
    }

    size_t end_pos = b->pos + (hdr.len - 4);
    int secchkcd = 0;

    while (b->pos + 4 <= end_pos) {
        ddm_hdr_t inner;
        r = ddm_read_hdr(b, &inner); if (r) return r;
        if (inner.cp == CP_SECCHKCD && inner.len == 5) {
            uint8_t cd;
            r = buf_get_u8(b, &cd); if (r) return r;
            secchkcd = (int)cd;
        } else {
            r = ddm_skip_payload(b, &inner); if (r) return r;
        }
    }
    b->pos = end_pos;

    if (secchkcd != 0) {
        snprintf(conn->diag_msg, sizeof(conn->diag_msg),
                 "SECCHKRM: security check failed, SECCHKCD=0x%02X", secchkcd);
        return SDB2_ERR_NET;  /* auth failure */
    }
    return SDB2_OK;
}

/* Parse ACCRDBRM: check SVRCOD, extract PRDID */
static int parse_accrdbrm(drda_conn_t *conn) {
    buf_t *b = &conn->recv_buf;
    dss_hdr_t dss;
    int r = dss_read_header(b, &dss); if (r) return r;

    ddm_hdr_t hdr;
    r = ddm_read_hdr(b, &hdr); if (r) return r;
    if (hdr.cp != CP_ACCRDBRM) {
        /* DB2 sometimes returns RDBNFNRM or RDBATHRM instead */
        if (hdr.cp == CP_RDBNFNRM) {
            snprintf(conn->diag_msg, sizeof(conn->diag_msg), "RDB not found");
        } else if (hdr.cp == CP_RDBATHRM) {
            snprintf(conn->diag_msg, sizeof(conn->diag_msg), "RDB access denied");
        } else if (hdr.cp == CP_SYNTAXRM) {
            /* Extract SYNERRCD for diagnostics */
            size_t ep = b->pos + (hdr.len - 4);
            uint8_t synerrcd = 0;
            while (b->pos + 4 <= ep) {
                ddm_hdr_t si; ddm_read_hdr(b, &si);
                if (si.cp == CP_SYNERRCD && si.len == 5) buf_get_u8(b, &synerrcd);
                else ddm_skip_payload(b, &si);
            }
            snprintf(conn->diag_msg, sizeof(conn->diag_msg),
                     "ACCRDB rejected: SYNTAXRM SYNERRCD=0x%02X", synerrcd);
        } else {
            snprintf(conn->diag_msg, sizeof(conn->diag_msg),
                     "expected ACCRDBRM, got %s", ddm_cp_name(hdr.cp));
        }
        return SDB2_ERR_PROTO;
    }

    size_t end_pos = b->pos + (hdr.len - 4);
    int svrcod = 0;

    while (b->pos + 4 <= end_pos) {
        ddm_hdr_t inner;
        r = ddm_read_hdr(b, &inner); if (r) return r;
        if (inner.cp == CP_SVRCOD && inner.len == 6) {
            uint16_t sv;
            r = buf_get_u16(b, &sv); if (r) return r;
            svrcod = (int)sv;
        } else if (inner.cp == CP_PRDID) {
            /* PRDID is a string: decode from EBCDIC */
            size_t plen = inner.len - 4;
            if (plen >= sizeof(conn->srv_prdid)) plen = sizeof(conn->srv_prdid) - 1;
            uint8_t ebcdic_prdid[16];
            if (plen <= sizeof(ebcdic_prdid)) {
                r = buf_get_bytes(b, ebcdic_prdid, plen); if (r) return r;
                int dec = ccsid_wire_to_utf8(CCSID_EBCDIC, ebcdic_prdid, plen,
                                              (uint8_t *)conn->srv_prdid,
                                              sizeof(conn->srv_prdid) - 1);
                if (dec > 0) conn->srv_prdid[dec] = '\0';
                /* skip any remaining bytes */
                size_t remaining_prdid = (inner.len - 4) - plen;
                if (remaining_prdid > 0) b->pos += remaining_prdid;
            } else {
                r = ddm_skip_payload(b, &inner); if (r) return r;
            }
        } else {
            r = ddm_skip_payload(b, &inner); if (r) return r;
        }
    }
    b->pos = end_pos;

    if (svrcod >= 8) {
        snprintf(conn->diag_msg, sizeof(conn->diag_msg),
                 "ACCRDBRM: SVRCOD=%d (error)", svrcod);
        return SDB2_ERR_PROTO;
    }
    return SDB2_OK;
}

/* ---- Public API ---- */

drda_conn_t *drda_connect(const char *host, int port,
                          const char *db,
                          const char *user, const char *pass,
                          char *errmsg, size_t errmsg_len) {
    drda_conn_t *conn = calloc(1, sizeof(*conn));
    if (!conn) return NULL;

    conn->fd = SDB2_ERR_NET;  /* sentinel: not connected */
    conn->corr_id  = 0;
    conn->autocommit = 1;     /* default: autocommit ON */
    conn->next_section = 2;   /* section 1 reserved for EXCSQLIMM; cursor sections start at 2 */
    conn->diag_msg[0] = '\0';

    if (buf_init(&conn->send_buf, 4096) != SDB2_OK) {
        free(conn);
        return NULL;
    }
    if (buf_init(&conn->recv_buf, 4096) != SDB2_OK) {
        buf_free(&conn->send_buf);
        free(conn);
        return NULL;
    }

    /* 1. TCP connect */
    conn->fd = net_connect(host, port);
    if (conn->fd < 0) {
        snprintf(conn->diag_msg, sizeof(conn->diag_msg),
                 "TCP connect to %s:%d failed", host, port);
        goto fail;
    }

    /* Install blocking I/O vtable (default).  Replace with sdb2_async_configure()
     * after drda_connect() returns to use non-blocking poll-retrying I/O. */
    conn->io_ctx  = (void *)(intptr_t)conn->fd;
    conn->io_send = net_blocking_send;
    conn->io_recv = net_blocking_recv;

    /* 2. Build and send EXCSAT (chained) + ACCSEC */
    int r = build_excsat(conn);
    if (r) { snprintf(conn->diag_msg, sizeof(conn->diag_msg), "build_excsat: %d", r); goto fail; }
    r = build_accsec(conn, db);
    if (r) { snprintf(conn->diag_msg, sizeof(conn->diag_msg), "build_accsec: %d", r); goto fail; }
    r = drda_flush(conn);
    if (r) { snprintf(conn->diag_msg, sizeof(conn->diag_msg), "send EXCSAT+ACCSEC: %d", r); goto fail; }

    /* 3. Receive EXCSATRD. DB2 may or may not chain ACCSECRD in the same response:
     *   - chain=1: server explicitly chains ACCSECRD as a second DSS (older behaviour)
     *   - chain=0: server accepted ACCSEC silently; ACCSECRD is omitted (DB2 11.5+)
     */
    r = drda_recv_dss(conn);
    if (r) { snprintf(conn->diag_msg, sizeof(conn->diag_msg), "recv EXCSATRD: %d", r); goto fail; }
    /* Peek chain bit from raw DSS header byte 3 before parse consumes it */
    int excsatrd_chained = (conn->recv_buf.limit >= 4) &&
                           (conn->recv_buf.data[3] & DSS_CHAIN) != 0;
    r = parse_excsatrd(conn);
    if (r) goto fail;

    if (excsatrd_chained) {
        /* Server is sending ACCSECRD as a chained DSS */
        r = drda_recv_dss(conn);
        if (r) { snprintf(conn->diag_msg, sizeof(conn->diag_msg), "recv ACCSECRD: %d", r); goto fail; }
        r = parse_accsecrd(conn);
        if (r) goto fail;
    }
    /* else: DB2 accepted ACCSEC without replying; proceed to SECCHK */

    /* 4. Build and send SECCHK */
    r = build_secchk(conn, db, user, pass);
    if (r) { snprintf(conn->diag_msg, sizeof(conn->diag_msg), "build_secchk: %d", r); goto fail; }
    r = drda_flush(conn);
    if (r) goto fail;

    /* 5. Receive SECCHKRM */
    r = drda_recv_dss(conn);
    if (r) { snprintf(conn->diag_msg, sizeof(conn->diag_msg), "recv SECCHKRM: %d", r); goto fail; }
    r = parse_secchkrm(conn);
    if (r) goto fail;

    /* 6. Build and send ACCRDB */
    r = build_accrdb(conn, db);
    if (r) { snprintf(conn->diag_msg, sizeof(conn->diag_msg), "build_accrdb: %d", r); goto fail; }
    r = drda_flush(conn);
    if (r) goto fail;

    /* 7. Receive ACCRDBRM */
    r = drda_recv_dss(conn);
    if (r) { snprintf(conn->diag_msg, sizeof(conn->diag_msg), "recv ACCRDBRM: %d", r); goto fail; }
    /* Peek chain bit before parse consumes the header */
    int accrdbrm_chained = (conn->recv_buf.limit >= 4) &&
                           (conn->recv_buf.data[3] & DSS_CHAIN) != 0;
    r = parse_accrdbrm(conn);
    if (r) goto fail;

    /* Drain any chained DSS frames DB2 appends after ACCRDBRM (e.g. SQLCARD) */
    while (accrdbrm_chained) {
        r = drda_recv_dss(conn);
        if (r) { snprintf(conn->diag_msg, sizeof(conn->diag_msg), "drain post-ACCRDBRM: %d", r); goto fail; }
        accrdbrm_chained = (conn->recv_buf.limit >= 4) &&
                           (conn->recv_buf.data[3] & DSS_CHAIN) != 0;
    }

    /* Store RDBNAM EBCDIC for later use in PKGNAMCSN */
    {
        size_t dblen = strlen(db);
        int enc = ccsid_utf8_to_wire(CCSID_EBCDIC,
                                     (const uint8_t *)db, dblen,
                                     conn->rdbnam_ebcdic, 18);
        if (enc < 0) { r = enc; goto fail; }
        size_t elen = (size_t)enc;
        if (elen < 18)
            memset(conn->rdbnam_ebcdic + elen, 0x40, 18 - elen);
    }

    return conn;  /* success */

fail:
    if (errmsg && errmsg_len > 0) {
        strncpy(errmsg, conn->diag_msg, errmsg_len - 1);
        errmsg[errmsg_len - 1] = '\0';
    }
    drda_disconnect(conn);
    return NULL;
}

void drda_disconnect(drda_conn_t *conn) {
    if (!conn) return;
    net_close(conn->fd);
    buf_free(&conn->send_buf);
    buf_free(&conn->recv_buf);
    free(conn);
}

/* ---- EXCSQLIMM: execute-immediate (INSERT/UPDATE/DELETE/DDL) ---- */

/*
 * PKGNAMCSN fixed format (names all <= 18 bytes):
 *   [RDBNAM:18][RDBCOLID:18][PKGID:18][PKGCNSTKN:8][PKGSN:2] = 64 bytes payload
 * Total DDM = 4 + 64 = 68 bytes
 *
 * We use:
 *   RDBCOLID = "NULLID  " padded to 18 with EBCDIC space (0x40)
 *   PKGID    = "SYSSH200" padded to 18 (IBM DB2 LUW standard CS package for dynamic exec)
 *   PKGCNSTKN = 8 zero bytes (server-specific; zeros work on some DB2 LUW configs)
 *   PKGSN    = 1 (first section in package)
 *   RDBNAM   = blank (18 × 0x40) meaning "current server"
 */

/* EBCDIC encodings of collection/package IDs (precomputed) */
static const uint8_t RDBCOLID_NULLID_EBCDIC[18] = {
    /* "NULLID" = 0xD5 0xE4 0xD3 0xD3 0xC9 0xC4, then 12 spaces (0x40) */
    0xD5, 0xE4, 0xD3, 0xD3, 0xC9, 0xC4,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40
};

static const uint8_t PKGID_SYSSH200_EBCDIC[18] = {
    /* "SYSSH200" = S=0xE2 Y=0xE8 S=0xE2 S=0xE2 H=0xC8 2=0xF2 0=0xF0 0=0xF0, then 10 spaces */
    0xE2, 0xE8, 0xE2, 0xE2, 0xC8, 0xF2, 0xF0, 0xF0,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40
};

/* Blank RDBNAM (18 EBCDIC spaces) — used in PKGNAMCSN to mean "current server" */
static const uint8_t RDBNAM_BLANK_EBCDIC[18] = {
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40
};

/* PKGCNSTKN for NULLID.SYSSH200 — "SYSLVL01" as stored in SYSCAT.PACKAGES.UNIQUE_ID */
static const uint8_t PKGCNSTKN_SYSSH200[8] = {
    0x53, 0x59, 0x53, 0x4C, 0x56, 0x4C, 0x30, 0x31  /* "SYSLVL01" */
};

static int build_pkgnamcsn(buf_t *b, const uint8_t rdbnam[18], uint16_t section) {
    (void)rdbnam; /* use blank RDBNAM (current server) */
    ddm_begin_collection(b, CP_PKGNAMCSN);
    buf_put_bytes(b, RDBNAM_BLANK_EBCDIC, 18);
    buf_put_bytes(b, RDBCOLID_NULLID_EBCDIC, 18);
    buf_put_bytes(b, PKGID_SYSSH200_EBCDIC, 18);
    buf_put_bytes(b, PKGCNSTKN_SYSSH200, 8);
    buf_put_u16(b, section);
    return ddm_end_collection(b);
}

/*
 * Overwrite sections 2..next_section-1 with a benign SELECT that references
 * no user tables.  This releases the session-level dependency that DB2 holds
 * on user tables through PRPSQLSTT-prepared statements in NULLID.SYSSH200,
 * which would otherwise cause DROP TABLE to fail with SQLCODE -950.
 *
 * Forward declarations for helpers defined later in this file.
 */
static int build_prpsqlstt(drda_conn_t *conn, uint16_t section);
static int build_sqlstt_objdss(drda_conn_t *conn, const char *sql);
static int recv_dss_chain(drda_conn_t *conn);
static int parse_reply_chain(drda_conn_t *conn, sdb2_stmt_t *stmt, int parse_cols);

static int drda_clear_prepared_sections(drda_conn_t *conn) {
    if (conn->next_section <= 2) return SDB2_OK;

    static const char DUMMY_SQL[] = "SELECT 1 FROM SYSIBM.SYSDUMMY1";

    for (uint16_t sec = 2; sec < conn->next_section; sec++) {
        int r = build_prpsqlstt(conn, sec);
        if (r) return r;
        r = build_sqlstt_objdss(conn, DUMMY_SQL);
        if (r) return r;
        r = drda_flush(conn); if (r) return r;

        r = recv_dss_chain(conn); if (r) return r;
        (void)parse_reply_chain(conn, NULL, /*parse_cols=*/0);
        /* Best-effort: ignore per-section errors; continue clearing. */
    }
    return SDB2_OK;
}

int drda_exec_immediate(drda_conn_t *conn, const char *sql, int32_t *row_count) {
    buf_t *b = &conn->send_buf;
    int r;

    /* For DDL statements (DROP, CREATE, ALTER), ensure any pending unit of
     * work is committed before executing, and clear any prepared sections
     * that reference user tables (to avoid SQLCODE -950 "object in use"). */
    {
        const char *p = sql;
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
        if (strncasecmp(p, "DROP",   4) == 0 ||
            strncasecmp(p, "CREATE", 6) == 0 ||
            strncasecmp(p, "ALTER",  5) == 0) {
            if (conn->autocommit) {
                /* Commit any pending DML first to preserve data. */
                int cr = drda_commit(conn);
                (void)cr;
                int rr = drda_rollback(conn);
                (void)rr;
            }
            /* Overwrite all prepared sections with a dummy SELECT so that
             * DB2 no longer holds a dependency on any user table. */
            int cr2 = drda_clear_prepared_sections(conn);
            (void)cr2; /* best-effort: proceed even if clearing fails */
        }
    }

    conn->corr_id++;
    uint16_t cid = conn->corr_id;

    /* ---- DSS 1: RQSDSS chained+same_id (EXCSQLIMM DDM) ---- */
    r = dss_begin_rqsdss(b, cid, /*chained=*/1, /*same_id=*/1);
    if (r) return r;
    ddm_begin_collection(b, CP_EXCSQLIMM);
    r = build_pkgnamcsn(b, conn->rdbnam_ebcdic, 1);
    if (r) return r;
    ddm_put_scalar_u8(b, CP_RDBCMTOK, conn->autocommit ? 0xF1u : 0xF0u);
    ddm_end_collection(b);
    r = dss_finalize(b);
    if (r) return r;

    /* ---- DSS 2: OBJDSS, not chained, same corrId (SQLSTT DDM) ---- */
    r = dss_begin_objdss(b, cid, /*chained=*/0, /*same_id=*/0);
    if (r) return r;
    ddm_begin_collection(b, CP_SQLSTT);
    /* SQLSTTGRP NOCS format (ASCII/SBC agent): [FF][00][4-byte-len][sql bytes] */
    buf_put_u8(b, 0xFF); /* NOCM null indicator (skip multibyte path) */
    buf_put_u8(b, 0x00); /* NOCS not-null indicator */
    size_t sql_len = strlen(sql);
    buf_put_u32(b, (uint32_t)sql_len);
    buf_put_bytes(b, (const uint8_t *)sql, sql_len);
    ddm_end_collection(b);
    r = dss_finalize(b);
    if (r) return r;

    /* ---- Flush ---- */
    r = drda_flush(conn);
    if (r) return r;

    /* ---- Read and parse reply DSS(es) — DB2 may chain multiple frames ---- */
    for (;;) {
        r = drda_recv_dss(conn);
        if (r) return r;

        buf_t *rb = &conn->recv_buf;
        int chained = (rb->limit >= 4) && (rb->data[3] & DSS_CHAIN) != 0;

        dss_hdr_t dss;
        r = dss_read_header(rb, &dss);
        if (r) return r;

        /* Walk DDM objects: skip RDBUPDRM/ENDUOWRM, parse SQLCARD */
        while (buf_remaining(rb) >= 4) {
            ddm_hdr_t hdr;
            r = ddm_read_hdr(rb, &hdr);
            if (r) return r;

            if (hdr.cp == CP_SQLCARD) {
                sdb2_sqlca_t ca;
                r = sqlca_parse(rb, &ca);
                if (r) return r;
                conn->diag_sqlcode = ca.sqlcode;
                if (row_count)
                    *row_count = ca.has_caxgrp ? ca.sqlerrd[2] : 0;
                if (ca.has_sqlca && ca.sqlcode != 0) {
                    snprintf(conn->diag_msg, sizeof conn->diag_msg,
                             "SQLCARD: SQLCODE=%d SQLSTATE=%s",
                             ca.sqlcode, ca.sqlstate);
                    return SDB2_ERR_SQL;
                }
                /* DB2 LUW does not commit DML automatically on RDBCMTOK=TRUE;
                 * send explicit RDBCMM to complete the unit of work. */
                if (conn->autocommit)
                    return drda_commit(conn);
                return SDB2_OK;
            } else if (hdr.cp == CP_SQLERRRM) {
                /* SQLERRRM contains a SQLCA — extract SQLCODE */
                sdb2_sqlca_t ca;
                r = sqlca_parse(rb, &ca);
                if (r) return r;
                conn->diag_sqlcode = ca.sqlcode;
                if (row_count) *row_count = 0;
                return SDB2_ERR_SQL;
            } else {
                /* RDBUPDRM, ENDUOWRM, or other — skip payload */
                r = ddm_skip_payload(rb, &hdr);
                if (r) return r;
            }
        }

        if (!chained) break;
    }

    return SDB2_ERR_PROTO; /* no SQLCARD found */
}

/* ================================================================
 * Phase 9 — PRPSQLSTT, OPNQRY, CNTQRY, CLSQRY, drda_fetch
 * ================================================================ */

#include "handles.h"
#include "fdoca.h"
#include <ctype.h>

/* QRYBLKSZ we announce on OPNQRY / CNTQRY (max DSS size = 32767 bytes).
 * Use a small value during testing to exercise the CNTQRY continuation path.
 * The value here is the default / production value; tests override via env. */
#define DEFAULT_QRYBLKSZ  32767u

/* ---- Build helpers ---- */

/* Build PKGNAMCSN for a given section number (re-uses conn->rdbnam_ebcdic). */
static int build_pkgnamcsn_section(buf_t *b, const uint8_t rdbnam[18], uint16_t section) {
    (void)rdbnam; /* use blank RDBNAM (current server) */
    ddm_begin_collection(b, CP_PKGNAMCSN);
    buf_put_bytes(b, RDBNAM_BLANK_EBCDIC, 18);
    buf_put_bytes(b, RDBCOLID_NULLID_EBCDIC, 18);
    buf_put_bytes(b, PKGID_SYSSH200_EBCDIC, 18);
    buf_put_bytes(b, PKGCNSTKN_SYSSH200, 8);
    buf_put_u16(b, section);
    return ddm_end_collection(b);
}

/* Build SQLSTT OBJDSS (chained data object carrying the SQL text). */
static int build_sqlstt_objdss(drda_conn_t *conn, const char *sql) {
    buf_t *b = &conn->send_buf;
    int r = dss_begin_objdss(b, conn->corr_id, /*chained=*/0, /*same_id=*/0);
    if (r) return r;

    ddm_begin_collection(b, CP_SQLSTT);
    /* SQLSTTGRP NOCS format on ASCII agent:
     *   [0xFF] NOCM null indicator (skip multi-byte path)
     *   [0x00] NOCS not-null indicator
     *   [4-byte BE length][sql bytes]  */
    buf_put_u8(b, 0xFF);
    buf_put_u8(b, 0x00);
    size_t sql_len = strlen(sql);
    buf_put_u32(b, (uint32_t)sql_len);
    buf_put_bytes(b, (const uint8_t *)sql, sql_len);
    ddm_end_collection(b);

    return dss_finalize(b);
}

/* Build PRPSQLSTT RQSDSS (chained; SQLSTT OBJDSS follows with same corrId). */
static int build_prpsqlstt(drda_conn_t *conn, uint16_t section) {
    buf_t *b = &conn->send_buf;
    conn->corr_id++;
    int r = dss_begin_rqsdss(b, conn->corr_id, /*chained=*/1, /*same_id=*/1);
    if (r) return r;

    ddm_begin_collection(b, CP_PRPSQLSTT);
    r = build_pkgnamcsn_section(b, conn->rdbnam_ebcdic, section);
    if (r) return r;
    ddm_end_collection(b);

    return dss_finalize(b);
}

/* Forward declaration — defined later in Phase 12 section. */
static int build_sqldta_params(buf_t *b, sdb2_param_binding_t *bindings, int n,
                               fdoca_result_t *param_desc);

/* Build OPNQRY RQSDSS.  When bindings/n_bindings are non-zero the parameter
 * values are embedded as a SQLDTA nested DDM collection inside OPNQRY. */
static int build_opnqry(drda_conn_t *conn, uint16_t section, uint32_t qryblksz,
                        sdb2_param_binding_t *bindings, int n_bindings,
                        fdoca_result_t *param_desc) {
    buf_t *b = &conn->send_buf;
    conn->corr_id++;
    int r = dss_begin_rqsdss(b, conn->corr_id, /*chained=*/0, /*same_id=*/0);
    if (r) return r;

    ddm_begin_collection(b, CP_OPNQRY);
    r = build_pkgnamcsn_section(b, conn->rdbnam_ebcdic, section);
    if (r) return r;
    /* QRYBLKSZ: 4-byte scalar */
    ddm_put_scalar_u32(b, CP_QRYBLKSZ, qryblksz);
    if (n_bindings > 0) {
        /* Embed SQLDTA (parameter values) as a nested DDM collection. */
        ddm_begin_collection(b, CP_SQLDTA);
        r = build_sqldta_params(b, bindings, n_bindings, param_desc);
        if (r) { ddm_end_collection(b); return r; }
        ddm_end_collection(b);
    }
    ddm_end_collection(b);

    return dss_finalize(b);
}

/* Build CNTQRY RQSDSS (not chained). */
static int build_cntqry(drda_conn_t *conn, uint16_t section,
                        uint64_t qryinsid, uint32_t qryblksz) {
    buf_t *b = &conn->send_buf;
    conn->corr_id++;
    int r = dss_begin_rqsdss(b, conn->corr_id, /*chained=*/0, /*same_id=*/0);
    if (r) return r;

    ddm_begin_collection(b, CP_CNTQRY);
    r = build_pkgnamcsn_section(b, conn->rdbnam_ebcdic, section);
    if (r) return r;
    ddm_put_scalar_u32(b, CP_QRYBLKSZ, qryblksz);
    /* QRYINSID: 8-byte big-endian */
    {
        int er = buf_ensure(b, 12); if (er) return er;
        buf_put_u16(b, (uint16_t)12);            /* length = 4 header + 8 data */
        buf_put_u16(b, CP_QRYINSID);
        buf_put_u64(b, qryinsid);
    }
    ddm_end_collection(b);

    return dss_finalize(b);
}

/* ---- DSS chain receive: read all chained DSS frames into recv_buf ---- */
/*
 * Receive a sequence of chained DSS frames from the server.
 * Each DSS header+payload is appended to recv_buf in order, so that the
 * parser can later call dss_read_header() at each DSS boundary.
 *
 * After return, recv_buf is in read mode:
 *   [DSS1 hdr(6)][DSS1 payload][DSS2 hdr(6)][DSS2 payload]...
 */
static int recv_dss_chain(drda_conn_t *conn) {
    buf_reset(&conn->recv_buf);

    for (;;) {
        size_t hdr_pos = conn->recv_buf.pos;

        /* Receive the 6-byte DSS header. */
        int r = drda_recv_n(conn, DSS_HDR_LEN);
        if (r) return r;

        /* Peek at the raw header bytes we just appended. */
        uint8_t *h = conn->recv_buf.data + hdr_pos;
        if (h[2] != DSS_MAGIC) {
            snprintf(conn->diag_msg, sizeof(conn->diag_msg),
                     "recv_dss_chain: DSS magic expected 0xD0, got 0x%02X", h[2]);
            return SDB2_ERR_PROTO;
        }
        uint16_t raw_len = ((uint16_t)h[0] << 8) | h[1];
        uint8_t  fmt     = h[3];

        size_t payload_len;
        if (raw_len & 0x8000) {
            payload_len = DSS_MAX_LEN - DSS_HDR_LEN;
        } else {
            if (raw_len < DSS_HDR_LEN) return SDB2_ERR_PROTO;
            payload_len = raw_len - DSS_HDR_LEN;
        }

        /* Receive the DSS payload. */
        r = drda_recv_n(conn, payload_len);
        if (r) return r;

        /* Stop if not chained. */
        if (!(fmt & DSS_CHAIN)) break;
    }

    buf_flip(&conn->recv_buf);
    return SDB2_OK;
}

/* ---- Parse OPNQRYRM inner content to extract QRYINSID ---- */
static int parse_opnqryrm(buf_t *b, const ddm_hdr_t *hdr,
                           uint64_t *qryinsid_out, int *svrcod_out) {
    size_t end = b->pos + (hdr->len - 4);
    *qryinsid_out = 0;
    *svrcod_out   = 0;

    while (b->pos + 4 <= end) {
        ddm_hdr_t inner;
        int r = ddm_read_hdr(b, &inner);
        if (r) return r;

        if (inner.cp == CP_SVRCOD && inner.len == 6) {
            uint16_t sv;
            r = buf_get_u16(b, &sv); if (r) return r;
            *svrcod_out = (int)sv;
        } else if (inner.cp == CP_QRYINSID && inner.len == 12) {
            r = buf_get_u64(b, qryinsid_out); if (r) return r;
        } else {
            r = ddm_skip_payload(b, &inner); if (r) return r;
        }
    }
    b->pos = end;
    return SDB2_OK;
}

/* ---- Append QRYDTA payload to stmt->row_buf ---- */
static int append_qrydta(sdb2_stmt_t *stmt, buf_t *b, size_t payload_len) {
    if (payload_len == 0) return SDB2_OK;

    /* Grow buffer if needed. */
    size_t need = stmt->row_buf_used + payload_len;
    if (need > stmt->row_buf_cap) {
        size_t new_cap = need + 4096;
        uint8_t *nb = (uint8_t *)realloc(stmt->row_buf, new_cap);
        if (!nb) return SDB2_ERR_MEM;
        stmt->row_buf     = nb;
        stmt->row_buf_cap = new_cap;
    }

    int r = buf_get_bytes(b, stmt->row_buf + stmt->row_buf_used, payload_len);
    if (r) return r;
    stmt->row_buf_used += payload_len;
    return SDB2_OK;
}

/* ---- Parse PRPSQLSTT / OPNQRY reply chains ---- */

/*
 * Walk a received DSS chain (already in recv_buf, read mode) and dispatch
 * each DDM object.  Used by both drda_prepare (simple SQLCARD check) and
 * drda_open_query (full OPNQRYRM + QRYDSC + QRYDTA handling).
 *
 * parse_cols: if true, handle QRYDSC / OPNQRYRM; else just check SQLCARD.
 */
static int parse_reply_chain(drda_conn_t *conn, sdb2_stmt_t *stmt,
                             int parse_cols) {
    buf_t *rb = &conn->recv_buf;
    while (buf_remaining(rb) > 0) {
        dss_hdr_t dss;
        int r = dss_read_header(rb, &dss);
        if (r) return r;

        size_t dss_end = rb->pos + dss.payload_len;

        while (rb->pos < dss_end) {
            if (buf_remaining(rb) < 4) return SDB2_ERR_PROTO;
            ddm_hdr_t hdr;
            r = ddm_read_hdr(rb, &hdr);
            if (r) return r;

            if (parse_cols && hdr.cp == CP_OPNQRYRM) {
                uint64_t qid = 0;
                int sv = 0;
                r = parse_opnqryrm(rb, &hdr, &qid, &sv);
                if (r) return r;
                stmt->qryinsid = qid;
                if (sv >= 8) {
                    snprintf(stmt->diag_msg, sizeof(stmt->diag_msg),
                             "OPNQRYRM: SVRCOD=%d", sv);
                    return SDB2_ERR_PROTO;
                }
            } else if (parse_cols && hdr.cp == CP_QRYDSC) {
                r = fdoca_parse_qrydsc(rb, hdr.len - 4, &stmt->cols);
                if (r) return r;
                /* DB2 LUW always sends a null indicator byte for every column
                 * in QRYDTA, even for columns declared NOT NULL.  Force all
                 * column descriptors to nullable so that the row-walk functions
                 * (compute_row_wire_size, fdoca_decode_col) consume those bytes
                 * correctly. */
                { int _i; for (_i = 0; _i < stmt->cols.ncols; _i++)
                    stmt->cols.cols[_i].nullable = 1; }
            } else if (parse_cols && hdr.cp == CP_SQLDARD && stmt) {
                /* SQLDARD contains column names (returned when TYPSQLDA sent
                 * in OPNQRY).  Format: null_indicator(1) + SQLDABC(4) +
                 * SQLN(2) + SQLD(2) + per-col[SQLTYPE(2)+SQLLEN(2)+SQLRES(8)
                 * +SQLNAME(2+N)]. */
                size_t sqldard_end = rb->pos + (hdr.len - 4);
                uint8_t ni = 0xFF;
                if (buf_remaining(rb) >= 1) buf_get_u8(rb, &ni);
                if (ni == 0x00 && buf_remaining(rb) >= 8) {
                    rb->pos += 4; /* skip SQLDABC */
                    uint16_t sqln = 0, sqld = 0;
                    buf_get_u16(rb, &sqln);
                    buf_get_u16(rb, &sqld);
                    int ncols = (int)sqld;
                    if (ncols > FDOCA_MAX_COLS) ncols = FDOCA_MAX_COLS;
                    for (int _ci = 0; _ci < ncols; _ci++) {
                        if (buf_remaining(rb) < 12) break;
                        rb->pos += 12; /* skip SQLTYPE+SQLLEN+SQLRES */
                        if (buf_remaining(rb) < 2) break;
                        uint16_t nlen = 0;
                        buf_get_u16(rb, &nlen);
                        if (nlen > 0 && buf_remaining(rb) >= nlen) {
                            if (_ci < stmt->cols.ncols) {
                                int cp = (int)nlen < (FDOCA_COLNAME_MAX - 1)
                                         ? (int)nlen : (FDOCA_COLNAME_MAX - 1);
                                memcpy(stmt->cols.cols[_ci].name,
                                       rb->data + rb->pos, (size_t)cp);
                                stmt->cols.cols[_ci].name[cp] = '\0';
                            }
                            rb->pos += nlen;
                        } else {
                            rb->pos += nlen;
                        }
                    }
                }
                rb->pos = sqldard_end;
            } else if (parse_cols && hdr.cp == CP_QRYDTA) {
                r = append_qrydta(stmt, rb, hdr.len - 4);
                if (r) return r;
            } else if (hdr.cp == CP_ENDQRYRM) {
                if (stmt) stmt->eof = true;
                r = ddm_skip_payload(rb, &hdr); if (r) return r;
            } else if (hdr.cp == CP_SQLCARD) {
                sdb2_sqlca_t ca;
                r = sqlca_parse(rb, &ca);
                if (r) return r;
                if (stmt) {
                    stmt->diag_sqlcode = ca.sqlcode;
                }
                conn->diag_sqlcode = ca.sqlcode;
                if (ca.has_sqlca && ca.sqlcode < 0) {
                    if (stmt) {
                        snprintf(stmt->diag_msg, sizeof(stmt->diag_msg),
                                 "SQLCARD: SQLCODE=%d SQLSTATE=%s",
                                 ca.sqlcode, ca.sqlstate);
                    }
                    snprintf(conn->diag_msg, sizeof(conn->diag_msg),
                             "SQLCARD: SQLCODE=%d SQLSTATE=%s",
                             ca.sqlcode, ca.sqlstate);
                    return SDB2_ERR_SQL;
                }
            } else if (hdr.cp == CP_SQLERRRM) {
                /* SQLERRRM is a collection DDM containing sub-DDMs (SVRCOD,
                 * RDBNAM, SRVDGN, etc.) — NOT a raw SQLCAGRP.  Skip the
                 * entire payload and signal a protocol-level SQL error. */
                snprintf(conn->diag_msg, sizeof(conn->diag_msg),
                         "SQLERRRM (cp=0x2213) from server");
                if (stmt) snprintf(stmt->diag_msg, sizeof(stmt->diag_msg),
                                   "SQLERRRM from server");
                r = ddm_skip_payload(rb, &hdr); if (r) return r;
                return SDB2_ERR_SQL;
            } else if (hdr.cp == CP_CMDCHKRM ||
                       hdr.cp == CP_SYNTAXRM || hdr.cp == CP_PRCCNVRM) {
                /* Protocol error reply messages */
                snprintf(conn->diag_msg, sizeof(conn->diag_msg),
                         "reply RM %s (0x%04X)", ddm_cp_name(hdr.cp), hdr.cp);
                r = ddm_skip_payload(rb, &hdr); if (r) return r;
                return SDB2_ERR_PROTO;
            } else {
                r = ddm_skip_payload(rb, &hdr);
                if (r) return r;
            }
        }
        rb->pos = dss_end;  /* align in case of parse slack */
    }

    return SDB2_OK;
}

/* ---- Row-advance helpers ---- */

/*
 * Compute the total wire size of one row's column data (not including the
 * SQLCA null indicator or the DA null indicator byte).
 *
 * Returns the byte count on success, or 0 if the buffer has insufficient
 * bytes to fully measure the row (caller should CNTQRY for more data).
 */
static size_t compute_row_wire_size(const fdoca_result_t *cols,
                                    const uint8_t *data, size_t avail) {
    size_t offset = 0;

    for (int i = 0; i < cols->ncols; i++) {
        const fdoca_col_t *col = &cols->cols[i];

        if (col->nullable) {
            if (offset >= avail) return 0;
            uint8_t null_ind = data[offset++];
            if (null_ind & 0x80) continue; /* null — no value bytes */
        }

        size_t csz = fdoca_col_wire_size(col, data + offset, avail - offset);
        if (csz == 0 && avail - offset < 2) return 0; /* need more */
        offset += csz;
        if (offset > avail) return 0;
    }

    return offset;
}

/* ---- CNTQRY flow ---- */

static int send_cntqry_and_recv(sdb2_stmt_t *stmt) {
    drda_conn_t *conn = stmt->dbc->conn;
    /* Environment variable QRYBLKSZ override for testing */
    const char *qbs_env = getenv("SDB2_QRYBLKSZ");
    uint32_t qbs = qbs_env ? (uint32_t)atoi(qbs_env) : DEFAULT_QRYBLKSZ;
    if (qbs < 512) qbs = 512;

    int r = build_cntqry(conn, stmt->section, stmt->qryinsid, qbs);
    if (r) return r;
    r = drda_flush(conn); if (r) return r;

    r = recv_dss_chain(conn); if (r) return r;

    /* Reset row buffer for new block. */
    stmt->row_buf_used = 0;
    stmt->row_pos      = 0;
    stmt->eof          = false;  /* might be set again by ENDQRYRM */

    r = parse_reply_chain(conn, stmt, /*parse_cols=*/0);
    if (r == SDB2_ERR_SQL) return r;
    if (r != SDB2_OK) return r;

    return SDB2_OK;
}

/* ================================================================
 * Public Phase 9 API
 * ================================================================ */

int drda_prepare(sdb2_stmt_t *stmt, const char *sql) {
    drda_conn_t *conn = stmt->dbc->conn;

    /* Allocate a section number. */
    uint16_t section = conn->next_section++;
    if (conn->next_section > 254) conn->next_section = 1;
    stmt->section = section;

    int r = build_prpsqlstt(conn, section);
    if (r) return r;
    r = build_sqlstt_objdss(conn, sql);
    if (r) return r;
    r = drda_flush(conn); if (r) return r;

    /* Receive the reply chain (SQLCARD or error RM). */
    r = recv_dss_chain(conn); if (r) return r;

    r = parse_reply_chain(conn, stmt, /*parse_cols=*/0);
    if (r) return r;

    /* Extract column names from SQL text for SELECT queries.
     * These are stored in pending_col_names and applied to stmt->cols
     * in drda_open_query after QRYDSC is parsed. */
    stmt->pending_ncols = 0;
    {
        const char *ps = sql;
        while (*ps == ' ' || *ps == '\t' || *ps == '\n' || *ps == '\r') ps++;
        if (strncasecmp(ps, "SELECT", 6) == 0) {
            ps += 6;
            /* Skip whitespace after SELECT */
            while (*ps == ' ' || *ps == '\t') ps++;
            /* Skip DISTINCT/ALL modifier */
            if (strncasecmp(ps, "DISTINCT", 8) == 0 && !isalnum((unsigned char)ps[8])) ps += 8;
            if (strncasecmp(ps, "ALL",      3) == 0 && !isalnum((unsigned char)ps[3])) ps += 3;
            while (*ps == ' ' || *ps == '\t') ps++;

            /* Find end of SELECT list (the FROM keyword at top level) */
            const char *from_pos = NULL;
            int depth = 0;
            for (const char *p = ps; *p; p++) {
                if (*p == '(') depth++;
                else if (*p == ')') depth--;
                else if (depth == 0 && strncasecmp(p, "FROM", 4) == 0 &&
                         (p == ps || !isalnum((unsigned char)*(p-1))) &&
                         !isalnum((unsigned char)p[4])) {
                    from_pos = p;
                    break;
                }
            }
            const char *sel_end = from_pos ? from_pos : ps + strlen(ps);

            /* Split by top-level commas and extract last identifier from each */
            int col_i = 0;
            const char *item = ps;
            depth = 0;
            for (const char *p = ps; p <= sel_end && col_i < FDOCA_MAX_COLS; p++) {
                int at_end = (p == sel_end);
                if (!at_end && *p == '(') { depth++; continue; }
                if (!at_end && *p == ')') { depth--; continue; }
                if ((!at_end && *p != ',') || depth > 0) continue;

                /* Extract the item [item, p) */
                const char *item_end = p;
                /* Skip trailing whitespace */
                while (item_end > item && (*(item_end-1) == ' ' || *(item_end-1) == '\t'
                                           || *(item_end-1) == '\n')) item_end--;

                /* Find the last identifier token (handles t.col, "col AS alias") */
                const char *tok_end = item_end;
                /* Skip trailing quoted identifiers */
                while (tok_end > item && tok_end[-1] == '"') {
                    tok_end--;
                    while (tok_end > item && tok_end[-1] != '"') tok_end--;
                    if (tok_end > item) tok_end--; /* skip opening quote */
                    break;
                }
                /* Walk backwards to find start of last word */
                const char *tok_start = tok_end;
                while (tok_start > item &&
                       (isalnum((unsigned char)tok_start[-1]) || tok_start[-1] == '_'))
                    tok_start--;

                int nlen = (int)(tok_end - tok_start);
                if (nlen > 0 && nlen < FDOCA_COLNAME_MAX) {
                    int cp = nlen < (FDOCA_COLNAME_MAX - 1) ? nlen : (FDOCA_COLNAME_MAX - 1);
                    memcpy(stmt->pending_col_names[col_i], tok_start, (size_t)cp);
                    stmt->pending_col_names[col_i][cp] = '\0';
                    /* Upper-case the name to match DB2 convention */
                    for (int _k = 0; _k < cp; _k++)
                        stmt->pending_col_names[col_i][_k] =
                            (char)toupper((unsigned char)stmt->pending_col_names[col_i][_k]);
                }
                col_i++;
                item = p + 1;
                while (item < sel_end && (*item == ' ' || *item == '\t')) item++;
            }
            stmt->pending_ncols = col_i;
        }
    }

    return SDB2_OK;
}

int drda_open_query(sdb2_stmt_t *stmt) {
    drda_conn_t *conn = stmt->dbc->conn;

    /* Use environment variable to allow small block sizes in tests. */
    const char *qbs_env = getenv("SDB2_QRYBLKSZ");
    uint32_t qbs = qbs_env ? (uint32_t)atoi(qbs_env) : DEFAULT_QRYBLKSZ;
    if (qbs < 512) qbs = 512;

    stmt->eof          = false;
    stmt->cursor_open  = false;
    stmt->row_buf_used = 0;
    stmt->row_pos      = 0;
    fdoca_result_free(&stmt->cols);

    /* Revert build_opnqry to simple (no params) — params handled via expansion */
    int r = build_opnqry(conn, stmt->section, qbs, NULL, 0, NULL);
    if (r) return r;
    r = drda_flush(conn); if (r) return r;

    r = recv_dss_chain(conn); if (r) return r;
    r = parse_reply_chain(conn, stmt, /*parse_cols=*/1);
    if (r) return r;

    stmt->cursor_open = true;

    /* Apply column names captured from DSCSQLSTT (called during drda_prepare). */
    for (int _ni = 0; _ni < stmt->pending_ncols && _ni < stmt->cols.ncols; _ni++) {
        if (stmt->pending_col_names[_ni][0] != '\0')
            memcpy(stmt->cols.cols[_ni].name, stmt->pending_col_names[_ni],
                   FDOCA_COLNAME_MAX);
    }

    if (stmt->eof && stmt->row_buf_used == 0) return SDB2_ERR_NODATA;
    return SDB2_OK;
}

int drda_fetch(sdb2_stmt_t *stmt, const uint8_t **out_row, size_t *out_row_len) {
    for (;;) {
        /* ---- Attempt to read next row from the current buffer ---- */
        if (stmt->row_pos < stmt->row_buf_used) {
            const uint8_t *p        = stmt->row_buf + stmt->row_pos;
            size_t         remaining = stmt->row_buf_used - stmt->row_pos;

            /* Each row starts with a SQLCAGRP null indicator. */
            uint8_t sqlca_ni = p[0];
            size_t  skip     = 1; /* bytes consumed so far (null indicator) */

            if (sqlca_ni != 0xFF) {
                /*
                 * Inline SQLCA present.
                 * Format at SQLAM 7:
                 *   SQLCODE(4) + SQLSTATE(5) + SQLERRPROC(8) = 17 bytes fixed
                 *   + SQLCAXGRP null_indicator(1) [+ extended, if not 0xFF]
                 *   + optional SQLDIAGGRP
                 * For Phase 9 we approximate: read past the 17-byte fixed part
                 * and the SQLCAXGRP indicator.  If SQLCAXGRP is not null
                 * we advance further by a fixed estimate; full handling in P10.
                 *
                 * Note: the SQLCA is embedded in the row ONLY as a per-row
                 * warning.  SQLCODE +100 (end of data) can appear here.
                 */
                if (remaining < skip + 17 + 1) goto need_more;
                /* SQLCODE in FDOCA application data is little-endian (QTDSQLASC) */
                int32_t sqlcode = (int32_t)(
                    ((uint32_t)p[skip+3] << 24) |
                    ((uint32_t)p[skip+2] << 16) |
                    ((uint32_t)p[skip+1] <<  8) |
                     (uint32_t)p[skip]);

                /* Check for +100 (end of data) immediately — no need to parse
                 * the rest of the inline SQLCA in this common case. */
                if (sqlcode == 100) {
                    stmt->eof = true;
                    return SDB2_ERR_NODATA;
                }

                skip += 17; /* code(4)+state(5)+proc(8) */

                /* SQLCAXGRP null indicator */
                uint8_t cax_ni = p[skip++];
                if (cax_ni != 0xFF) {
                    /* SQLCAXGRP at SQLAM 7: 4*6 + 11 = 35 bytes
                     * + VCM(2+up-to-70) + VCS(2+up-to-70).
                     * Read the two VCS/VCM lengths conservatively. */
                    if (remaining < skip + 35 + 2 + 2) goto need_more;
                    skip += 35;
                    uint16_t vm_len = ((uint16_t)p[skip] << 8) | p[skip+1];
                    skip += 2 + vm_len;
                    if (remaining < skip + 2) goto need_more;
                    uint16_t vs_len = ((uint16_t)p[skip] << 8) | p[skip+1];
                    skip += 2 + vs_len;
                    /* SQLDIAGGRP null indicator — skip if present */
                    if (remaining < skip + 1) goto need_more;
                    uint8_t diag_ni = p[skip++];
                    if (diag_ni != 0xFF) {
                        /* Skip entire SQLDIAGGRP: not parsed in this phase.
                         * We already handled sqlcode != 100 above, so this
                         * is a per-row warning; just skip the remaining
                         * inline SQLCA and continue to column data.
                         * Approximate skip: advance row_pos past this entry. */
                        stmt->row_pos += skip;
                        continue;
                    }
                }
            } /* end inline SQLCA handling */

            /* Column data follows directly after the SQLCAGRP indicator.
             * DB2 LUW QRYDTA rows have no separate "DA" indicator byte. */
            const uint8_t *col_start = p + skip;
            size_t         col_avail = remaining - skip;
            size_t         row_bytes = compute_row_wire_size(&stmt->cols,
                                                             col_start, col_avail);
            if (row_bytes == 0 && col_avail < 2) goto need_more;

            *out_row     = col_start;
            *out_row_len = row_bytes;
            stmt->cur_row     = col_start;
            stmt->cur_row_len = row_bytes;
            stmt->row_pos += skip + row_bytes;
            return SDB2_OK;
        }

        /* ---- Buffer exhausted ---- */
        if (stmt->eof || !stmt->cursor_open) return SDB2_ERR_NODATA;

    need_more:
        {
            int r = send_cntqry_and_recv(stmt);
            if (r) return r;
            if (stmt->row_buf_used == 0) return SDB2_ERR_NODATA;
        }
    }
}

void drda_close_cursor(sdb2_stmt_t *stmt) {
    if (!stmt || !stmt->cursor_open) return;

    if (stmt->dbc && stmt->dbc->conn) {
        drda_conn_t *conn = stmt->dbc->conn;

        /*
         * When the client hasn't fetched all rows to EOF (eof=false), the
         * server-side cursor may still be open.  Send CLSQRY to explicitly
         * close it.  Ignore SYNTAXRM errors — they mean the server already
         * closed the cursor (e.g., after sending inline SQLCODE=100).
         *
         * For cursors that have already reached EOF server-side (eof=true),
         * DB2 has already closed the cursor; no CLSQRY is needed.
         */
        if (!stmt->eof) {
            /* Build and send CLSQRY without QRYINSID.
             * DB2 LUW returns QRYINSID=0 in OPNQRYRM; sending CLSQRY with
             * QRYINSID=0 causes SYNTAXRM "cursor not found" because DB2
             * identifies open cursors by section, not by a zero QRYINSID.
             * Omitting QRYINSID causes DB2 to close any open cursor on
             * the given section, which is what we actually want. */
            buf_t *b = &conn->send_buf;
            conn->corr_id++;
            int r = dss_begin_rqsdss(b, conn->corr_id, /*chained=*/0, /*same_id=*/0);
            if (r == SDB2_OK) {
                ddm_begin_collection(b, CP_CLSQRY);
                r = build_pkgnamcsn_section(b, conn->rdbnam_ebcdic, stmt->section);
                if (r == SDB2_OK) ddm_end_collection(b);
                if (r == SDB2_OK) r = dss_finalize(b);
            }
            if (r == SDB2_OK) r = drda_flush(conn);
            if (r == SDB2_OK) {
                r = recv_dss_chain(conn);
                if (r == SDB2_OK) {
                    int clsqry_r = parse_reply_chain(conn, stmt, /*parse_cols=*/0);
                    (void)clsqry_r; /* ignore errors (cursor may already be closed) */
                }
            }
        }

        /* DB2 does not auto-commit a UOW that contains only SELECT operations.
         * Send an explicit RDBCMM to release any row or table locks held since
         * the cursor was opened; without this, DDL (e.g. DROP TABLE) on the
         * same table will fail with SQLCODE -950 (object in use). */
        if (conn->autocommit)
            (void)drda_commit(conn);
    }

    stmt->cursor_open  = false;
    stmt->eof          = true;
    stmt->row_pos      = 0;
    stmt->row_buf_used = 0;
    stmt->cur_row      = NULL;
    stmt->cur_row_len  = 0;
}

/* ================================================================
 * Phase 12 — DSCSQLSTT (describe params) + EXCSQLSTT (execute with params)
 * ================================================================ */

int drda_describe_params(sdb2_stmt_t *stmt) {
    drda_conn_t *conn = stmt->dbc->conn;
    buf_t *b = &conn->send_buf;
    int r;

    conn->corr_id++;
    r = dss_begin_rqsdss(b, conn->corr_id, /*chained=*/0, /*same_id=*/0);
    if (r) return r;

    ddm_begin_collection(b, CP_DSCSQLSTT);
    r = build_pkgnamcsn_section(b, conn->rdbnam_ebcdic, stmt->section);
    if (r) return r;
    ddm_put_scalar_u8(b, CP_RTNSQLDA, 0xF1u);  /* return SQLDA = true */
    ddm_put_scalar_u8(b, CP_TYPSQLDA, 0x01u);  /* 0x01 = input parameters */
    ddm_end_collection(b);
    r = dss_finalize(b); if (r) return r;

    r = drda_flush(conn); if (r) return r;

    r = recv_dss_chain(conn); if (r) return r;

    buf_t *rb = &conn->recv_buf;

    while (buf_remaining(rb) > 0) {
        dss_hdr_t dss;
        r = dss_read_header(rb, &dss); if (r) return r;

        size_t dss_end = rb->pos + dss.payload_len;

        while (rb->pos < dss_end && buf_remaining(rb) >= 4) {
            ddm_hdr_t hdr;
            r = ddm_read_hdr(rb, &hdr); if (r) return r;

            if (hdr.cp == CP_SQLDARD) {
                fdoca_result_free(&stmt->param_desc);
                r = fdoca_parse_qrydsc(rb, hdr.len - 4, &stmt->param_desc);
                if (r) return r;
            } else if (hdr.cp == CP_SQLCARD) {
                sdb2_sqlca_t ca;
                r = sqlca_parse(rb, &ca); if (r) return r;
                conn->diag_sqlcode = ca.sqlcode;
                if (ca.has_sqlca && ca.sqlcode < 0) {
                    snprintf(conn->diag_msg, sizeof conn->diag_msg,
                             "DSCSQLSTT: SQLCODE=%d", ca.sqlcode);
                    return SDB2_ERR_SQL;
                }
            } else {
                r = ddm_skip_payload(rb, &hdr); if (r) return r;
            }
        }
        rb->pos = dss_end;
    }
    return SDB2_OK;
}

/* Build the SQLDTA payload (raw param bytes) into *b. */
static int build_sqldta_params(buf_t *b,
                                sdb2_param_binding_t *bindings, int n,
                                fdoca_result_t *param_desc) {
    for (int i = 0; i < n; i++) {
        sdb2_param_binding_t *pb = &bindings[i];
        if (!pb->active) continue;

        fdoca_col_t col;
        if (param_desc && i < param_desc->ncols) {
            col = param_desc->cols[i];
        } else {
            col = fdoca_sql_type_to_col(pb->sql_type,
                                        pb->col_size, pb->dec_digits);
        }

        long ind = pb->ind_ptr ? *pb->ind_ptr : 0L;
        int r = fdoca_encode_col(b, &col, pb->value_type,
                                  pb->value_ptr, pb->buf_len, ind);
        if (r) return r;
    }
    return SDB2_OK;
}

int drda_execute_prepared(sdb2_stmt_t *stmt, int32_t *row_count) {
    drda_conn_t *conn = stmt->dbc->conn;
    buf_t *b = &conn->send_buf;
    int r;

    /* Lazily fetch input parameter descriptor on first execute */
    if (stmt->n_param_bindings > 0 && stmt->param_desc.ncols == 0) {
        /* Ignore failure — fall back to synthetic types if describe unavailable */
        (void)drda_describe_params(stmt);
    }

    int has_params = (stmt->n_param_bindings > 0);

    conn->corr_id++;
    /* EXCSQLSTT RQSDSS: chained if we have a SQLDTA to follow */
    r = dss_begin_rqsdss(b, conn->corr_id,
                         /*chained=*/has_params ? 1 : 0,
                         /*same_id=*/has_params ? 1 : 0);
    if (r) return r;

    ddm_begin_collection(b, CP_EXCSQLSTT);
    r = build_pkgnamcsn_section(b, conn->rdbnam_ebcdic, stmt->section);
    if (r) return r;
    ddm_put_scalar_u8(b, CP_RDBCMTOK, conn->autocommit ? 0xF1u : 0xF0u);
    ddm_end_collection(b);
    r = dss_finalize(b); if (r) return r;

    if (has_params) {
        /* SQLDTA OBJDSS (not chained, same corrId) */
        r = dss_begin_objdss(b, conn->corr_id, /*chained=*/0, /*same_id=*/0);
        if (r) return r;

        ddm_begin_collection(b, CP_SQLDTA);
        r = build_sqldta_params(b, stmt->param_bindings,
                                 stmt->n_param_bindings, &stmt->param_desc);
        ddm_end_collection(b);
        if (r) return r;

        r = dss_finalize(b); if (r) return r;
    }

    r = drda_flush(conn); if (r) return r;

    /* Receive and parse reply — DB2 may chain RDBUPDRM before SQLCARD */
    for (;;) {
        r = drda_recv_dss(conn); if (r) return r;

        buf_t *rb = &conn->recv_buf;
        int chained = (rb->limit >= 4) && (rb->data[3] & DSS_CHAIN) != 0;

        dss_hdr_t dss;
        r = dss_read_header(rb, &dss); if (r) return r;

        while (buf_remaining(rb) >= 4) {
            ddm_hdr_t hdr;
            r = ddm_read_hdr(rb, &hdr); if (r) return r;

            if (hdr.cp == CP_SQLCARD) {
                sdb2_sqlca_t ca;
                r = sqlca_parse(rb, &ca); if (r) return r;
                conn->diag_sqlcode = ca.sqlcode;
                if (row_count) *row_count = ca.has_caxgrp ? ca.sqlerrd[2] : 0;
                if (ca.has_sqlca && ca.sqlcode != 0) return SDB2_ERR_SQL;
                return SDB2_OK;
            } else if (hdr.cp == CP_SQLERRRM) {
                sdb2_sqlca_t ca;
                r = sqlca_parse(rb, &ca); if (r) return r;
                conn->diag_sqlcode = ca.sqlcode;
                if (row_count) *row_count = 0;
                return SDB2_ERR_SQL;
            } else {
                r = ddm_skip_payload(rb, &hdr); if (r) return r;
            }
        }

        if (!chained) break;
    }

    return SDB2_ERR_PROTO;
}

/* ================================================================
 * Phase 13 — RDBCMM (commit) + RDBRLLBCK (rollback)
 * ================================================================ */

/* Send a commit/rollback command and parse the ENDUOWRM reply. */
static int send_uow_command(drda_conn_t *conn, uint16_t cmd_cp) {
    buf_t *b = &conn->send_buf;
    int r;

    conn->corr_id++;
    r = dss_begin_rqsdss(b, conn->corr_id, /*chained=*/0, /*same_id=*/0);
    if (r) return r;
    ddm_begin_collection(b, cmd_cp);
    ddm_end_collection(b);
    r = dss_finalize(b); if (r) return r;

    r = drda_flush(conn); if (r) return r;

    /* Receive and parse reply — DB2 may chain multiple DSS frames */
    for (;;) {
        r = drda_recv_dss(conn); if (r) return r;

        buf_t *rb = &conn->recv_buf;
        int chained = (rb->limit >= 4) && (rb->data[3] & DSS_CHAIN) != 0;

        dss_hdr_t dss;
        r = dss_read_header(rb, &dss); if (r) return r;

        /* Parse ENDUOWRM: look for SVRCOD and UOWDSP (1-byte disposition). */
        while (buf_remaining(rb) >= 4) {
            ddm_hdr_t hdr;
            r = ddm_read_hdr(rb, &hdr); if (r) return r;

            if (hdr.cp == CP_SVRCOD && hdr.len == 6) {
                uint16_t sv; buf_get_u16(rb, &sv);
                if (sv >= 8) {
                    snprintf(conn->diag_msg, sizeof conn->diag_msg,
                             "%s: SVRCOD=%u",
                             cmd_cp == CP_RDBCMM ? "RDBCMM" : "RDBRLLBCK", sv);
                    return SDB2_ERR_PROTO;
                }
            } else if (hdr.cp == CP_SQLCARD) {
                sdb2_sqlca_t ca;
                r = sqlca_parse(rb, &ca); if (r) return r;
                conn->diag_sqlcode = ca.sqlcode;
                if (ca.has_sqlca && ca.sqlcode < 0) return SDB2_ERR_SQL;
                return SDB2_OK;
            } else {
                r = ddm_skip_payload(rb, &hdr); if (r) return r;
            }
        }

        if (!chained) break;
    }
    return SDB2_OK;
}

int drda_commit(drda_conn_t *conn) {
    return send_uow_command(conn, CP_RDBCMM);
}

int drda_rollback(drda_conn_t *conn) {
    return send_uow_command(conn, CP_RDBRLLBCK);
}

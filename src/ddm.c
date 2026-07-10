#include "ddm.h"
#include "ccsid.h"
#include "staticdb2/sdb2.h"
#include <stdio.h>
#include <string.h>

/* ---- Writers ---- */

int ddm_put_scalar_u8(buf_t *b, uint16_t cp, uint8_t v) {
    /* [00 05] [cp_hi cp_lo] [v] */
    int r;
    r = buf_put_u16(b, 0x0005u); if (r) return r;
    r = buf_put_u16(b, cp);      if (r) return r;
    r = buf_put_u8(b,  v);       if (r) return r;
    return SDB2_OK;
}

int ddm_put_scalar_u16(buf_t *b, uint16_t cp, uint16_t v) {
    /* [00 06] [cp_hi cp_lo] [v_hi v_lo] */
    int r;
    r = buf_put_u16(b, 0x0006u); if (r) return r;
    r = buf_put_u16(b, cp);      if (r) return r;
    r = buf_put_u16(b, v);       if (r) return r;
    return SDB2_OK;
}

int ddm_put_scalar_u32(buf_t *b, uint16_t cp, uint32_t v) {
    /* [00 08] [cp_hi cp_lo] [v…] */
    int r;
    r = buf_put_u16(b, 0x0008u); if (r) return r;
    r = buf_put_u16(b, cp);      if (r) return r;
    r = buf_put_u32(b, v);       if (r) return r;
    return SDB2_OK;
}

int ddm_put_scalar_u64(buf_t *b, uint16_t cp, uint64_t v) {
    /* [00 0C] [cp_hi cp_lo] [v…] */
    int r;
    r = buf_put_u16(b, 0x000Cu); if (r) return r;
    r = buf_put_u16(b, cp);      if (r) return r;
    r = buf_put_u64(b, v);       if (r) return r;
    return SDB2_OK;
}

int ddm_put_scalar_bytes(buf_t *b, uint16_t cp,
                         const uint8_t *data, size_t data_len) {
    uint16_t total_len = (uint16_t)(4 + data_len);
    int r;
    r = buf_put_u16(b, total_len);       if (r) return r;
    r = buf_put_u16(b, cp);             if (r) return r;
    r = buf_put_bytes(b, data, data_len); if (r) return r;
    return SDB2_OK;
}

int ddm_put_scalar_string(buf_t *b, uint16_t cp, int ccsid, const char *str) {
    size_t slen = strlen(str);
    /* Ensure capacity: worst case 3 bytes per UTF-8 char + 4 byte header */
    int r = buf_ensure(b, 4 + slen * 3);
    if (r) return r;

    /* Save position for length field */
    size_t len_pos = b->pos;
    b->pos += 2;                         /* reserve length field */
    r = buf_put_u16(b, cp); if (r) return r;

    /* Encode the string directly into the buffer */
    int enc = ccsid_utf8_to_wire(ccsid, (const uint8_t *)str, slen,
                                  b->data + b->pos, b->cap - b->pos);
    if (enc < 0) return enc;
    b->pos += (size_t)enc;

    /* Backfill length = 4 (header) + encoded bytes */
    uint16_t total = (uint16_t)(4 + (size_t)enc);
    b->data[len_pos]     = (uint8_t)(total >> 8);
    b->data[len_pos + 1] = (uint8_t)(total);
    return enc;
}

int ddm_put_mgrlvl(buf_t *b, uint16_t cp, uint16_t level) {
    /* MGRLVLLS entries are raw 4-byte pairs — no length prefix. */
    int r;
    r = buf_put_u16(b, cp);    if (r) return r;
    r = buf_put_u16(b, level); if (r) return r;
    return SDB2_OK;
}

int ddm_begin_collection(buf_t *b, uint16_t cp) {
    int r = buf_mark_len2(b); if (r) return r;
    return buf_put_u16(b, cp);
}

int ddm_end_collection(buf_t *b) {
    return buf_fill_len2(b);
}

/* ---- Readers ---- */

int ddm_read_hdr(buf_t *b, ddm_hdr_t *hdr) {
    int r;
    r = buf_get_u16(b, &hdr->len); if (r) return r;
    r = buf_get_u16(b, &hdr->cp);  if (r) return r;
    if (hdr->len < 4) return SDB2_ERR_PROTO;
    return SDB2_OK;
}

int ddm_skip_payload(buf_t *b, const ddm_hdr_t *hdr) {
    uint16_t payload = hdr->len - 4;
    if (buf_remaining(b) < payload) return SDB2_NEED_MORE;
    b->pos += payload;
    return SDB2_OK;
}

/* ---- Debug: codepoint name table ---- */

static const struct { uint16_t cp; const char *name; } cp_names[] = {
    { CP_EXCSAT,    "EXCSAT"    }, { CP_ACCSEC,    "ACCSEC"    },
    { CP_SECCHK,    "SECCHK"    }, { CP_ACCRDB,    "ACCRDB"    },
    { CP_CNTQRY,    "CNTQRY"    }, { CP_DSCSQLSTT, "DSCSQLSTT" },
    { CP_EXCSQLIMM, "EXCSQLIMM" }, { CP_EXCSQLSTT, "EXCSQLSTT" },
    { CP_OPNQRY,    "OPNQRY"    }, { CP_PRPSQLSTT, "PRPSQLSTT" },
    { CP_RDBCMM,    "RDBCMM"   }, { CP_RDBRLLBCK,  "RDBRLLBCK" },
    { CP_SQLSTT,    "SQLSTT"   }, { CP_QRYDSC,     "QRYDSC"    },
    { CP_QRYDTA,    "QRYDTA"   }, { CP_EXCSATRD,   "EXCSATRD"  },
    { CP_ACCSECRD,  "ACCSECRD" }, { CP_SECCHKRM,   "SECCHKRM"  },
    { CP_ACCRDBRM,  "ACCRDBRM" }, { CP_OPNQRYRM,   "OPNQRYRM"  },
    { CP_ENDQRYRM,  "ENDQRYRM" }, { CP_ENDUOWRM,   "ENDUOWRM"  },
    { CP_ABNUOWRM,  "ABNUOWRM" }, { CP_DTAMCHRM,   "DTAMCHRM"  },
    { CP_QRYPOPRM,  "QRYPOPRM" }, { CP_RDBNFNRM,   "RDBNFNRM"  },
    { CP_SQLERRRM,  "SQLERRRM" }, { CP_RDBUPDRM,   "RDBUPDRM"  },
    { CP_SQLCARD,   "SQLCARD"  }, { CP_SQLDARD,    "SQLDARD"   },
    { CP_CMDCHKRM,  "CMDCHKRM" }, { CP_SYNTAXRM,   "SYNTAXRM"  },
    { CP_PRCCNVRM,  "PRCCNVRM" }, { CP_MGRLVLRM,   "MGRLVLRM"  },
    { CP_RDBATHRM,  "RDBATHRM" }, { CP_AGENT,      "AGENT"     },
    { CP_SQLAM,     "SQLAM"    }, { CP_CMNTCPIP,   "CMNTCPIP"  },
    { CP_SECMGR,    "SECMGR"   }, { CP_MGRLVLLS,   "MGRLVLLS"  },
    { CP_EXTNAM,    "EXTNAM"   }, { CP_SRVNAM,     "SRVNAM"    },
    { CP_SRVRLSLV,  "SRVRLSLV" }, { CP_PRDID,      "PRDID"     },
    { CP_PRDDTA,    "PRDDTA"   }, { CP_RDBNAM,     "RDBNAM"    },
    { CP_RDBACCCL,  "RDBACCCL" }, { CP_TYPDEFNAM,  "TYPDEFNAM" },
    { CP_TYPDEFOVR, "TYPDEFOVR"}, { CP_RDBCMTOK,   "RDBCMTOK"  },
    { CP_SECMEC,    "SECMEC"   }, { CP_SECTKN,     "SECTKN"    },
    { CP_USRID,     "USRID"    }, { CP_PASSWORD,   "PASSWORD"  },
    { CP_SVRCOD,    "SVRCOD"   }, { CP_SYNERRCD,   "SYNERRCD"  },
    { CP_SECCHKCD,  "SECCHKCD" }, { CP_QRYBLKSZ,   "QRYBLKSZ"  },
    { CP_PKGNAMCSN, "PKGNAMCSN"},
    { 0, NULL }
};

const char *ddm_cp_name(uint16_t cp) {
    static char buf[12];
    for (int i = 0; cp_names[i].name != NULL; i++) {
        if (cp_names[i].cp == cp) return cp_names[i].name;
    }
    snprintf(buf, sizeof(buf), "0x%04X", cp);
    return buf;
}

#ifndef SDB2_DDM_H
#define SDB2_DDM_H

/*
 * ddm.h — DDM (Distributed Data Management) object layer.
 *
 * Every DRDA command payload is built from DDM objects, each consisting of:
 *
 *   [ll ll] [cp cp] [data...]
 *    length  codepoint  payload
 *
 *   ll ll : total object length in bytes, INCLUDING the 4-byte ll/cp header.
 *   cp cp : 2-byte codepoint (identifies the DDM parameter type).
 *   data  : payload bytes, whose interpretation depends on the codepoint.
 *
 * Collections (nested DDM objects) use the same format; their payload is
 * further DDM objects.  Use ddm_begin_collection / ddm_end_collection to
 * write them — the length is backfilled automatically.
 *
 * Usage example (writing an EXTNAM scalar inside an EXCSAT object):
 *
 *   ddm_begin_collection(&buf, CP_EXCSAT);        // open EXCSAT
 *     ddm_put_scalar_bytes(&buf, CP_EXTNAM, ...); // EXTNAM value
 *   ddm_end_collection(&buf);                     // close EXCSAT, patch length
 */

#include <stdint.h>
#include <stddef.h>
#include "buf.h"
#include "ccsid.h"

/* ---- DRDA codepoints ---- */

/* Commands (sent by client) */
#define CP_EXCSAT    0x1041u  /* Exchange Server Attributes */
#define CP_ACCSEC    0x106Du  /* Access Security */
#define CP_SECCHK    0x106Eu  /* Security Check */
#define CP_ACCRDB    0x2001u  /* Access RDB */
#define CP_CNTQRY    0x2006u  /* Continue Query */
#define CP_DSCSQLSTT 0x2008u  /* Describe SQL Statement */
#define CP_EXCSQLIMM 0x200Au  /* Execute Immediate SQL */
#define CP_EXCSQLSTT 0x200Bu  /* Execute SQL Statement */
#define CP_OPNQRY    0x200Cu  /* Open Query */
#define CP_PRPSQLSTT 0x200Du  /* Prepare SQL Statement */
#define CP_RDBCMM    0x200Eu  /* RDB Commit Unit of Work */
#define CP_RDBRLLBCK 0x200Fu  /* RDB Rollback Unit of Work */
#define CP_CLSQRY    0x2007u  /* Close Query */
#define CP_SQLSTT    0x2414u  /* SQL Statement (text payload) */
#define CP_SQLDTA    0x2412u  /* SQL Data (parameter values for EXCSQLSTT) */
#define CP_QRYDSC   0x241Au  /* Query Descriptor */
#define CP_QRYDTA   0x241Bu  /* Query Data */

/* Reply messages (sent by server) */
#define CP_EXCSATRD  0x1443u  /* EXCSAT reply */
#define CP_ACCSECRD  0x14ACu  /* ACCSEC reply */
#define CP_SECCHKRM  0x1219u  /* Security Check Reply Message */
#define CP_ACCRDBRM  0x2201u  /* ACCRDB Reply Message */
#define CP_OPNQRYRM  0x2205u  /* Open Query Reply Message */
#define CP_ENDQRYRM  0x220Bu  /* End Query Reply Message */
#define CP_ENDUOWRM  0x220Cu  /* End Unit of Work Reply Message */
#define CP_ABNUOWRM  0x220Du  /* Abnormal End UoW Reply Message */
#define CP_DTAMCHRM  0x220Eu  /* Data Mismatch Reply Message */
#define CP_QRYPOPRM  0x220Fu  /* Query Previously Opened Reply Message */
#define CP_RDBNFNRM  0x2211u  /* RDB Not Found Reply Message */
#define CP_SQLERRRM  0x2213u  /* SQL Error Reply Message */
#define CP_RDBUPDRM  0x2218u  /* RDB Update Reply Message */
#define CP_SQLCARD   0x2408u  /* SQL Communications Area Descriptor */
#define CP_SQLDARD   0x2411u  /* SQL Descriptor Area Descriptor */
#define CP_CMDCHKRM  0x1254u  /* Command Check Reply Message */
#define CP_SYNTAXRM  0x124Cu  /* Syntax Error Reply Message */
#define CP_PRCCNVRM  0x1245u  /* Protocol Conversation Reply Message */
#define CP_MGRLVLRM  0x1210u  /* Manager Level Reply Message */
#define CP_RDBATHRM  0x22CBu  /* RDB Authorization Reply Message */

/* Instance variable codepoints (parameters within commands/replies) */
#define CP_AGENT     0x1403u  /* Agent manager */
#define CP_SQLAM     0x2407u  /* SQL Application Manager */
#define CP_CMNTCPIP  0x1474u  /* Comm TCP/IP manager */
#define CP_SECMGR    0x1440u  /* Security Manager */
#define CP_MGRLVLLS  0x1404u  /* Manager Level List */
#define CP_EXTNAM    0x115Eu  /* External Name */
#define CP_SRVNAM    0x116Du  /* Server Name */
#define CP_SRVRLSLV  0x115Au  /* Server Product Release Level */
#define CP_PRDID     0x112Eu  /* Product Specific Identifier */
#define CP_PRDDTA    0x2104u  /* Product Specific Data */
#define CP_RDBNAM    0x2110u  /* Relational Database Name */
#define CP_RDBACCCL  0x210Fu  /* RDB Access Manager Class */
#define CP_TYPDEFNAM 0x002Fu  /* Data Type Definition Name */
#define CP_TYPDEFOVR 0x0035u  /* Type Definition Overrides */
#define CP_RDBCMTOK  0x2105u  /* RDB Commit Allowed */
#define CP_SECMEC    0x11A2u  /* Security Mechanism */
#define CP_SECTKN    0x11DCu  /* Security Token */
#define CP_USRID     0x11A0u  /* User ID */
#define CP_PASSWORD  0x11A1u  /* Password */
#define CP_SVRCOD    0x1149u  /* Severity Code */
#define CP_SYNERRCD  0x114Au  /* Syntax Error Code */
#define CP_SECCHKCD  0x11A4u  /* Security Check Code */
#define CP_QRYBLKSZ  0x2114u  /* Query Block Size */
#define CP_PKGNAMCSN 0x2113u  /* Package Name, Consistency Token */
#define CP_PKGID     0x112Eu  /* Package ID (alias for PRDID in this context) */

/* Query / prepare parameter codepoints */
#define CP_RTNSQLDA  0x2116u  /* Return SQLDA (1-byte boolean) */
#define CP_TYPSQLDA  0x2146u  /* Type of SQLDA to return (1-byte) */
#define CP_QRYINSID  0x215Bu  /* Query Instance Identifier (8 bytes) */
#define CP_MAXBLKEXT 0x2141u  /* Maximum Extra Query Blocks (int16) */
#define CP_QRYCLSIMP 0x215Du  /* Query Close Implicit (1-byte) */
#define CP_QRYPRCTYP 0x2102u  /* Query Protocol Type (int16) */
#define CP_QRYROWSET 0x2156u  /* Query Rowset Size (int32) */

/* Security mechanism values */
#define SECMEC_USRIDPWD  0x0003u  /* userid + clear-text password */
#define SECMEC_EUSRIDPWD 0x0009u  /* encrypted userid + password */

/* TYPDEFNAM values */
#define TYPDEFNAM_QTDSQLASC "QTDSQLASC"  /* ASCII/UTF-8 agent type */
#define TYPDEFNAM_QTDSQLX86 "QTDSQLX86"  /* x86 encoding */

/* ---- Accessor-pair read result ---- */
typedef struct {
    uint16_t cp;     /* codepoint */
    uint16_t len;    /* total object length (including 4-byte header) */
} ddm_hdr_t;

/* ---- Writers ---- */

/*
 * Fixed-length scalars.
 * Each writes a 4-byte [len][cp] header followed by the value.
 */
int ddm_put_scalar_u8 (buf_t *b, uint16_t cp, uint8_t  v);   /* 5 bytes total */
int ddm_put_scalar_u16(buf_t *b, uint16_t cp, uint16_t v);   /* 6 bytes total */
int ddm_put_scalar_u32(buf_t *b, uint16_t cp, uint32_t v);   /* 8 bytes total */
int ddm_put_scalar_u64(buf_t *b, uint16_t cp, uint64_t v);   /* 12 bytes total */

/*
 * Variable-length scalar: [len][cp][data…]
 * len = 4 + data_len.
 */
int ddm_put_scalar_bytes(buf_t *b, uint16_t cp,
                         const uint8_t *data, size_t data_len);

/*
 * String scalar encoded in the given CCSID.
 * len = 4 + strlen(str) for ASCII/UTF-8; same for EBCDIC (1:1 byte mapping).
 */
int ddm_put_scalar_string(buf_t *b, uint16_t cp, int ccsid, const char *str);

/*
 * Manager level pair for MGRLVLLS: writes a raw 4-byte [cp][level] entry.
 * MGRLVLLS entries have NO individual length prefix — just CP + 2-byte level.
 */
int ddm_put_mgrlvl(buf_t *b, uint16_t cp, uint16_t level);

/*
 * Collection object: begin reserves [00 00][cp] and pushes a mark;
 * end patches the 2-byte length with the total object size.
 */
int ddm_begin_collection(buf_t *b, uint16_t cp);
int ddm_end_collection(buf_t *b);

/* ---- Readers ---- */

/*
 * Read the 4-byte [ll][cp] header at the current position.
 * Does NOT advance past the payload — the caller must consume hdr.len-4 bytes.
 */
int ddm_read_hdr(buf_t *b, ddm_hdr_t *hdr);

/* Convenience: skip payload bytes (hdr.len - 4) without parsing. */
int ddm_skip_payload(buf_t *b, const ddm_hdr_t *hdr);

/* ---- Debug ---- */

/* Return a short human-readable name for a known codepoint, or "0xXXXX". */
const char *ddm_cp_name(uint16_t cp);

#endif /* SDB2_DDM_H */

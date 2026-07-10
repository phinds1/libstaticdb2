#pragma once
#include <stdint.h>
#include "buf.h"
#include "../include/staticdb2/sdb2.h"

/* Maximum length for error message tokens (VCM or VCS, up to 70 chars each) */
#define SQLCA_ERRMSG_MAX 140

/*
 * SQLCA — SQL Communications Area.
 * Populated from a SQLCARD FD:OCA body parsed off the wire.
 */
typedef struct {
    int32_t  sqlcode;
    char     sqlstate[6];    /* 5 chars + NUL */
    char     sqlerrproc[9];  /* 8 chars + NUL */
    int32_t  sqlerrd[6];
    char     sqlwarn[12];    /* 11 warn bytes + NUL */
    uint8_t  errmsg[SQLCA_ERRMSG_MAX];
    uint16_t errmsg_len;
    int      has_sqlca;      /* 0 = NULLDATA (no error), 1 = present */
    int      has_caxgrp;     /* 1 = sqlerrd/sqlwarn populated */
} sdb2_sqlca_t;

/*
 * Parse a SQLCAGRP FD:OCA body from buf (already positioned at the null-indicator
 * byte of the SQLCAGRP — i.e. the first byte inside the SQLCARD DDM payload).
 * On success returns SDB2_OK and populates *out.
 * Returns SDB2_NEED_MORE or SDB2_ERR_PROTO on malformed data.
 *
 * Targets SQLAM level 7 (the level negotiated in Phase 5).
 */
int sqlca_parse(buf_t *b, sdb2_sqlca_t *out);

/*
 * SVRCOD severity constants (from Derby CodePoint).
 */
#define SVRCOD_INFO   0
#define SVRCOD_WARN   4
#define SVRCOD_ERROR  8

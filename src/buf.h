#ifndef SDB2_BUF_H
#define SDB2_BUF_H

/*
 * buf.h — growable byte buffer with big-endian scalar I/O.
 *
 * Design mirrors Derby's Request/Reply buffer pair:
 *  - buf_mark_len2 / buf_fill_len2 implement the markLengthBytes /
 *    updateLengthBytes trick: reserve 2 bytes, write data, then backfill
 *    the 2-byte big-endian length covering the reserved slot + data.
 *  - The buffer has two modes:
 *      write mode: pos advances on put_*; limit == cap
 *      read  mode: limit set by buf_flip(); pos advances on get_*
 *
 * All functions return 0 (SDB2_OK) on success or a negative sdb2_err_t.
 */

#include <stdint.h>
#include <stddef.h>

/* Maximum nesting depth for the DDM length-marker stack. */
#define BUF_MARK_STACK_DEPTH 16

typedef struct {
    uint8_t *data;
    size_t   pos;    /* current read/write position */
    size_t   limit;  /* read boundary (write mode: limit == cap) */
    size_t   cap;    /* total allocated bytes */
    /* mark stack for backfilling lengths (buf_mark_len2 / buf_fill_len2) */
    size_t   marks[BUF_MARK_STACK_DEPTH];
    int      mark_top;
} buf_t;

/* ---- Lifecycle ---- */
int    buf_init(buf_t *b, size_t initial_cap);
void   buf_free(buf_t *b);
void   buf_reset(buf_t *b);   /* pos=0; keep existing allocation */
void   buf_flip(buf_t *b);    /* limit=pos, pos=0 — switch to read mode */

/* Ensure at least n more bytes are writable from current pos. */
int    buf_ensure(buf_t *b, size_t n);

/* ---- Writers (big-endian, advance pos) ---- */
int    buf_put_u8 (buf_t *b, uint8_t  v);
int    buf_put_u16(buf_t *b, uint16_t v);
int    buf_put_u24(buf_t *b, uint32_t v);  /* 3 bytes, MSB first */
int    buf_put_u32(buf_t *b, uint32_t v);
int    buf_put_u64(buf_t *b, uint64_t v);
int    buf_put_bytes(buf_t *b, const uint8_t *src, size_t n);

/* ---- Writers (little-endian — FDOCA/QTDSQLASC user data) ---- */
int    buf_put_u16_le(buf_t *b, uint16_t v);
int    buf_put_u32_le(buf_t *b, uint32_t v);
int    buf_put_u64_le(buf_t *b, uint64_t v);

/* ---- Readers (big-endian, advance pos up to limit) ---- */
int    buf_get_u8 (buf_t *b, uint8_t  *v);
int    buf_get_u16(buf_t *b, uint16_t *v);
int    buf_get_u24(buf_t *b, uint32_t *v);
int    buf_get_u32(buf_t *b, uint32_t *v);
int    buf_get_u64(buf_t *b, uint64_t *v);
int    buf_get_bytes(buf_t *b, uint8_t *dst, size_t n);

/* ---- Readers (little-endian — FDOCA/QTDSQLASC user data) ---- */
int    buf_get_u16_le(buf_t *b, uint16_t *v);
int    buf_get_u32_le(buf_t *b, uint32_t *v);
int    buf_get_u64_le(buf_t *b, uint64_t *v);

/* ---- DDM length-backfill helpers ---- */
/* Push current pos onto the mark stack, then advance pos by 2 (reserves the
 * 2-byte length field). buf_fill_len2 patches those bytes with the distance
 * from the pushed mark to the current pos (inclusive of the 2 bytes). */
int    buf_mark_len2(buf_t *b);
int    buf_fill_len2(buf_t *b);

/* Directly overwrite 2 bytes at a known offset (for DSS header patching). */
int    buf_poke_u16_at(buf_t *b, size_t offset, uint16_t v);

/* ---- Introspection ---- */
size_t buf_remaining(const buf_t *b);  /* limit - pos (read mode) */
size_t buf_writable(const buf_t *b);   /* cap   - pos (write mode) */

#endif /* SDB2_BUF_H */

#include "buf.h"
#include "staticdb2/sdb2.h"
#include <stdlib.h>
#include <string.h>

/* ---- Lifecycle ---- */

int buf_init(buf_t *b, size_t initial_cap) {
    if (initial_cap == 0) initial_cap = 256;
    b->data = malloc(initial_cap);
    if (!b->data) return SDB2_ERR_MEM;
    b->pos      = 0;
    b->limit    = initial_cap;  /* write mode: limit == cap */
    b->cap      = initial_cap;
    b->mark_top = 0;
    return SDB2_OK;
}

void buf_free(buf_t *b) {
    free(b->data);
    b->data     = NULL;
    b->pos      = 0;
    b->limit    = 0;
    b->cap      = 0;
    b->mark_top = 0;
}

void buf_reset(buf_t *b) {
    b->pos      = 0;
    b->limit    = b->cap;  /* back to write mode */
    b->mark_top = 0;
}

void buf_flip(buf_t *b) {
    b->limit = b->pos;
    b->pos   = 0;
}

/* Grow buffer so that at least n bytes are available beyond current pos.
 * New capacity is max(cap*2, pos+n) — same policy as Derby's ensureLength. */
int buf_ensure(buf_t *b, size_t n) {
    if (b->cap - b->pos >= n) return SDB2_OK;
    size_t need   = b->pos + n;
    size_t newcap = b->cap * 2;
    if (newcap < need) newcap = need;
    uint8_t *nd = realloc(b->data, newcap);
    if (!nd) return SDB2_ERR_MEM;
    b->data  = nd;
    b->cap   = newcap;
    b->limit = newcap;  /* stays in write mode */
    return SDB2_OK;
}

/* ---- Writers ---- */

int buf_put_u8(buf_t *b, uint8_t v) {
    int r = buf_ensure(b, 1); if (r) return r;
    b->data[b->pos++] = v;
    return SDB2_OK;
}

int buf_put_u16(buf_t *b, uint16_t v) {
    int r = buf_ensure(b, 2); if (r) return r;
    b->data[b->pos++] = (uint8_t)(v >> 8);
    b->data[b->pos++] = (uint8_t)(v);
    return SDB2_OK;
}

int buf_put_u24(buf_t *b, uint32_t v) {
    int r = buf_ensure(b, 3); if (r) return r;
    b->data[b->pos++] = (uint8_t)(v >> 16);
    b->data[b->pos++] = (uint8_t)(v >> 8);
    b->data[b->pos++] = (uint8_t)(v);
    return SDB2_OK;
}

int buf_put_u32(buf_t *b, uint32_t v) {
    int r = buf_ensure(b, 4); if (r) return r;
    b->data[b->pos++] = (uint8_t)(v >> 24);
    b->data[b->pos++] = (uint8_t)(v >> 16);
    b->data[b->pos++] = (uint8_t)(v >> 8);
    b->data[b->pos++] = (uint8_t)(v);
    return SDB2_OK;
}

int buf_put_u64(buf_t *b, uint64_t v) {
    int r = buf_ensure(b, 8); if (r) return r;
    b->data[b->pos++] = (uint8_t)(v >> 56);
    b->data[b->pos++] = (uint8_t)(v >> 48);
    b->data[b->pos++] = (uint8_t)(v >> 40);
    b->data[b->pos++] = (uint8_t)(v >> 32);
    b->data[b->pos++] = (uint8_t)(v >> 24);
    b->data[b->pos++] = (uint8_t)(v >> 16);
    b->data[b->pos++] = (uint8_t)(v >> 8);
    b->data[b->pos++] = (uint8_t)(v);
    return SDB2_OK;
}

int buf_put_bytes(buf_t *b, const uint8_t *src, size_t n) {
    int r = buf_ensure(b, n); if (r) return r;
    memcpy(b->data + b->pos, src, n);
    b->pos += n;
    return SDB2_OK;
}

/* ---- Little-endian writers (FDOCA/QTDSQLASC user data) ---- */

int buf_put_u16_le(buf_t *b, uint16_t v) {
    int r = buf_ensure(b, 2); if (r) return r;
    b->data[b->pos++] = (uint8_t)(v);
    b->data[b->pos++] = (uint8_t)(v >> 8);
    return SDB2_OK;
}

int buf_put_u32_le(buf_t *b, uint32_t v) {
    int r = buf_ensure(b, 4); if (r) return r;
    b->data[b->pos++] = (uint8_t)(v);
    b->data[b->pos++] = (uint8_t)(v >>  8);
    b->data[b->pos++] = (uint8_t)(v >> 16);
    b->data[b->pos++] = (uint8_t)(v >> 24);
    return SDB2_OK;
}

int buf_put_u64_le(buf_t *b, uint64_t v) {
    int r = buf_ensure(b, 8); if (r) return r;
    b->data[b->pos++] = (uint8_t)(v);
    b->data[b->pos++] = (uint8_t)(v >>  8);
    b->data[b->pos++] = (uint8_t)(v >> 16);
    b->data[b->pos++] = (uint8_t)(v >> 24);
    b->data[b->pos++] = (uint8_t)(v >> 32);
    b->data[b->pos++] = (uint8_t)(v >> 40);
    b->data[b->pos++] = (uint8_t)(v >> 48);
    b->data[b->pos++] = (uint8_t)(v >> 56);
    return SDB2_OK;
}


/* ---- Readers ---- */

int buf_get_u8(buf_t *b, uint8_t *v) {
    if (b->pos + 1 > b->limit) return SDB2_NEED_MORE;
    *v = b->data[b->pos++];
    return SDB2_OK;
}

int buf_get_u16(buf_t *b, uint16_t *v) {
    if (b->pos + 2 > b->limit) return SDB2_NEED_MORE;
    *v = ((uint16_t)b->data[b->pos] << 8) | (uint16_t)b->data[b->pos + 1];
    b->pos += 2;
    return SDB2_OK;
}

int buf_get_u24(buf_t *b, uint32_t *v) {
    if (b->pos + 3 > b->limit) return SDB2_NEED_MORE;
    *v = ((uint32_t)b->data[b->pos]     << 16)
       | ((uint32_t)b->data[b->pos + 1] <<  8)
       |  (uint32_t)b->data[b->pos + 2];
    b->pos += 3;
    return SDB2_OK;
}

int buf_get_u32(buf_t *b, uint32_t *v) {
    if (b->pos + 4 > b->limit) return SDB2_NEED_MORE;
    *v = ((uint32_t)b->data[b->pos]     << 24)
       | ((uint32_t)b->data[b->pos + 1] << 16)
       | ((uint32_t)b->data[b->pos + 2] <<  8)
       |  (uint32_t)b->data[b->pos + 3];
    b->pos += 4;
    return SDB2_OK;
}

int buf_get_u64(buf_t *b, uint64_t *v) {
    if (b->pos + 8 > b->limit) return SDB2_NEED_MORE;
    *v = ((uint64_t)b->data[b->pos]     << 56)
       | ((uint64_t)b->data[b->pos + 1] << 48)
       | ((uint64_t)b->data[b->pos + 2] << 40)
       | ((uint64_t)b->data[b->pos + 3] << 32)
       | ((uint64_t)b->data[b->pos + 4] << 24)
       | ((uint64_t)b->data[b->pos + 5] << 16)
       | ((uint64_t)b->data[b->pos + 6] <<  8)
       |  (uint64_t)b->data[b->pos + 7];
    b->pos += 8;
    return SDB2_OK;
}

int buf_get_bytes(buf_t *b, uint8_t *dst, size_t n) {
    if (b->pos + n > b->limit) return SDB2_NEED_MORE;
    memcpy(dst, b->data + b->pos, n);
    b->pos += n;
    return SDB2_OK;
}

/* ---- Little-endian readers (FDOCA/QTDSQLASC user data) ---- */

int buf_get_u16_le(buf_t *b, uint16_t *v) {
    if (b->pos + 2 > b->limit) return SDB2_NEED_MORE;
    *v = (uint16_t)b->data[b->pos]
       | ((uint16_t)b->data[b->pos + 1] << 8);
    b->pos += 2;
    return SDB2_OK;
}

int buf_get_u32_le(buf_t *b, uint32_t *v) {
    if (b->pos + 4 > b->limit) return SDB2_NEED_MORE;
    *v = (uint32_t)b->data[b->pos]
       | ((uint32_t)b->data[b->pos + 1] <<  8)
       | ((uint32_t)b->data[b->pos + 2] << 16)
       | ((uint32_t)b->data[b->pos + 3] << 24);
    b->pos += 4;
    return SDB2_OK;
}

int buf_get_u64_le(buf_t *b, uint64_t *v) {
    if (b->pos + 8 > b->limit) return SDB2_NEED_MORE;
    *v = (uint64_t)b->data[b->pos]
       | ((uint64_t)b->data[b->pos + 1] <<  8)
       | ((uint64_t)b->data[b->pos + 2] << 16)
       | ((uint64_t)b->data[b->pos + 3] << 24)
       | ((uint64_t)b->data[b->pos + 4] << 32)
       | ((uint64_t)b->data[b->pos + 5] << 40)
       | ((uint64_t)b->data[b->pos + 6] << 48)
       | ((uint64_t)b->data[b->pos + 7] << 56);
    b->pos += 8;
    return SDB2_OK;
}


/* ---- DDM length-backfill ---- */

/* Reserve 2 bytes for a length field: push current pos, then skip 2 bytes.
 * Call buf_fill_len2 when the enclosing object is complete to write the length. */
int buf_mark_len2(buf_t *b) {
    if (b->mark_top >= BUF_MARK_STACK_DEPTH) return SDB2_ERR_BADARG;
    int r = buf_ensure(b, 2); if (r) return r;
    b->marks[b->mark_top++] = b->pos;
    b->pos += 2;
    return SDB2_OK;
}

/* Pop the top mark; write big-endian uint16 = (current_pos - mark) into the
 * reserved slot.  The length covers itself (2 bytes) plus all data written
 * since the mark — identical to Derby's updateLengthBytes semantics for
 * simple (non-extended) objects. */
int buf_fill_len2(buf_t *b) {
    if (b->mark_top == 0) return SDB2_ERR_BADARG;
    size_t mark   = b->marks[--b->mark_top];
    size_t length = b->pos - mark;
    if (length > 0xFFFF) return SDB2_ERR_TRUNC;
    b->data[mark]     = (uint8_t)(length >> 8);
    b->data[mark + 1] = (uint8_t)(length);
    return SDB2_OK;
}

/* Overwrite 2 bytes at a known absolute offset (for DSS header length patching). */
int buf_poke_u16_at(buf_t *b, size_t offset, uint16_t v) {
    if (offset + 2 > b->cap) return SDB2_ERR_BADARG;
    b->data[offset]     = (uint8_t)(v >> 8);
    b->data[offset + 1] = (uint8_t)(v);
    return SDB2_OK;
}

/* ---- Introspection ---- */

size_t buf_remaining(const buf_t *b) {
    return (b->pos < b->limit) ? b->limit - b->pos : 0;
}

size_t buf_writable(const buf_t *b) {
    return (b->pos < b->cap) ? b->cap - b->pos : 0;
}

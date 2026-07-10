#ifndef SHA256_H
#define SHA256_H

#include <stdint.h>
#include <stddef.h>

/* One-shot SHA-256.  out32 receives 32 raw bytes. */
void sha256(const uint8_t *data, size_t len, uint8_t out32[32]);

/* One-shot SHA-256, hex-encoded.  out_hex receives 64 lowercase hex chars + NUL. */
void sha256_hex(const uint8_t *data, size_t len, char out_hex[65]);

#endif /* SHA256_H */

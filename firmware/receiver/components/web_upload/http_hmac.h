#pragma once
#include <stddef.h>

// Tính HMAC-SHA256(message, key) → output hex string (64 chars + null)
// out_hex buffer phải có ≥ 65 bytes
void http_hmac_sha256_hex(const char *message, size_t msg_len,
                           const char *key, size_t key_len,
                           char *out_hex);
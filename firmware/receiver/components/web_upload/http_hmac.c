#include "http_hmac.h"
#include "mbedtls/md.h"

void http_hmac_sha256_hex(const char *message, size_t msg_len,
                           const char *key, size_t key_len,
                           char *out_hex)
{
    uint8_t hmac[32];
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&ctx, (const uint8_t*)key, key_len);
    mbedtls_md_hmac_update(&ctx, (const uint8_t*)message, msg_len);
    mbedtls_md_hmac_finish(&ctx, hmac);
    mbedtls_md_free(&ctx);

    static const char *hex_chars = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        out_hex[i*2]     = hex_chars[hmac[i] >> 4];
        out_hex[i*2 + 1] = hex_chars[hmac[i] & 0x0F];
    }
    out_hex[64] = '\0';
}
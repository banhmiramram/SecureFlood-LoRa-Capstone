#include "lora_crypto.h"
#include "lora_crypto_keys.h"
#include "mbedtls/aes.h"
#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include "esp_random.h"
#include "esp_log.h"
#include <string.h>

#define TAG       "lora_crypto"
#define IV_SIZE   16
#define HMAC_SIZE 4

// PKCS7 padding
static size_t pkcs7_pad(uint8_t *buf, size_t len) {
    size_t pad = 16 - (len % 16);
    if (pad == 0) pad = 16;
    for (size_t i = 0; i < pad; i++) buf[len + i] = (uint8_t)pad;
    return len + pad;
}

static int pkcs7_unpad(uint8_t *buf, size_t len) {
    if (len == 0 || len % 16 != 0) return -1;
    uint8_t pad = buf[len - 1];
    if (pad == 0 || pad > 16) return -1;
    for (size_t i = 0; i < pad; i++) {
        if (buf[len - 1 - i] != pad) return -1;
    }
    return len - pad;
}

void lora_crypto_init(void) {
    ESP_LOGI(TAG, "Crypto ready (AES-128-CBC + HMAC-SHA256 truncated 8B)");
}

int lora_crypto_encrypt(const char *plaintext, size_t plain_len,
                        char *out_packet, size_t out_size)
{
    if (plain_len == 0 || plain_len > LORA_CRYPTO_MAX_PLAINTEXT) return -1;

    // 1. Pad plaintext
    uint8_t pt[LORA_CRYPTO_MAX_PLAINTEXT + 16];
    memcpy(pt, plaintext, plain_len);
    size_t pt_padded = pkcs7_pad(pt, plain_len);

    // 2. Random IV
    uint8_t iv[IV_SIZE];
    esp_fill_random(iv, IV_SIZE);

    // 3. AES-128-CBC encrypt
    uint8_t ct[LORA_CRYPTO_MAX_PLAINTEXT + 16];
    uint8_t iv_work[IV_SIZE];
    memcpy(iv_work, iv, IV_SIZE);
    
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_enc(&aes, LORA_AES_KEY, 128);
    int ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_ENCRYPT, pt_padded, iv_work, pt, ct);
    mbedtls_aes_free(&aes);
    if (ret != 0) return -1;

    // 4. Build IV || CT, compute HMAC over it
    uint8_t blob[IV_SIZE + LORA_CRYPTO_MAX_PLAINTEXT + 16 + HMAC_SIZE];
    memcpy(blob, iv, IV_SIZE);
    memcpy(blob + IV_SIZE, ct, pt_padded);

    uint8_t hmac[32];
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&md, LORA_HMAC_KEY, 32);
    mbedtls_md_hmac_update(&md, blob, IV_SIZE + pt_padded);
    mbedtls_md_hmac_finish(&md, hmac);
    mbedtls_md_free(&md);

    memcpy(blob + IV_SIZE + pt_padded, hmac, HMAC_SIZE);
    size_t blob_len = IV_SIZE + pt_padded + HMAC_SIZE;

    // 5. Base64 encode
    size_t b64_len = 0;
    ret = mbedtls_base64_encode((uint8_t*)out_packet, out_size, &b64_len, blob, blob_len);
    if (ret != 0) return -1;

    return (int)b64_len;
}

int lora_crypto_decrypt(const char *packet, size_t packet_len,
                        char *out_plain, size_t out_size)
{
    // 1. Base64 decode
    uint8_t blob[LORA_CRYPTO_MAX_PACKET];
    size_t blob_len = 0;
    int ret = mbedtls_base64_decode(blob, sizeof(blob), &blob_len,
                                     (const uint8_t*)packet, packet_len);
    if (ret != 0) { ESP_LOGW(TAG, "base64 decode fail"); return -1; }

    if (blob_len < IV_SIZE + 16 + HMAC_SIZE) return -1;
    size_t ct_len = blob_len - IV_SIZE - HMAC_SIZE;
    if (ct_len % 16 != 0) return -1;

    // 2. Verify HMAC
    uint8_t expected[32];
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&md, LORA_HMAC_KEY, 32);
    mbedtls_md_hmac_update(&md, blob, IV_SIZE + ct_len);
    mbedtls_md_hmac_finish(&md, expected);
    mbedtls_md_free(&md);

    if (memcmp(expected, blob + IV_SIZE + ct_len, HMAC_SIZE) != 0) {
        ESP_LOGW(TAG, "HMAC mismatch - tampered or wrong key");
        return -1;
    }

    // 3. AES decrypt
    uint8_t pt[LORA_CRYPTO_MAX_PACKET];
    uint8_t iv_work[IV_SIZE];
    memcpy(iv_work, blob, IV_SIZE);

    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, LORA_AES_KEY, 128);
    ret = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, ct_len, iv_work,
                                 blob + IV_SIZE, pt);
    mbedtls_aes_free(&aes);
    if (ret != 0) return -1;

    int plain_len = pkcs7_unpad(pt, ct_len);
    if (plain_len < 0 || (size_t)plain_len >= out_size) return -1;

    memcpy(out_plain, pt, plain_len);
    out_plain[plain_len] = '\0';
    return plain_len;
}
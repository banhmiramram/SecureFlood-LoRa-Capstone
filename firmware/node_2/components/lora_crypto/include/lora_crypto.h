#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define LORA_CRYPTO_MAX_PLAINTEXT  64
#define LORA_CRYPTO_MAX_PACKET     128

void lora_crypto_init(void);

// Trả về số bytes của packet đã encrypt + base64 (in vào out_packet)
// Trả -1 nếu lỗi
int lora_crypto_encrypt(const char *plaintext, size_t plain_len,
                        char *out_packet, size_t out_size);

// Decrypt packet về plaintext. Trả số bytes plaintext, -1 nếu lỗi (HMAC sai/data hỏng)
int lora_crypto_decrypt(const char *packet, size_t packet_len,
                        char *out_plain, size_t out_size);
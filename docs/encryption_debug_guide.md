# Encryption Debug Guide

**Project**: SecureFlood-LoRa Capstone  
**Component**: AES-128-CBC + HMAC-SHA256  
**Status**: Production Ready

---

## Quick Troubleshooting

### Symptom 1: "HMAC Mismatch" Error

**Error Message**:
```
[HUB] HMAC mismatch: expected 0x12345678, got 0xabcdef00
[HUB] Packet rejected
```

**Root Causes & Solutions**:

| Cause | Diagnosis | Solution |
|-------|-----------|----------|
| **Wrong HMAC key** | Keys don't match between sensor & hub | Verify `lora_crypto_keys.h` identical on both devices |
| **Data corruption** | Packet modified in transit | Check antenna connections, test in clear line-of-sight |
| **Sequence mismatch** | SEQ counter out of sync | Restart both devices (resets sliding window) |
| **IV tampering** | Random IV modified | Check for RF noise, use shielded cable |
| **mbedTLS version** | Different library versions | Update ESP-IDF to v5.x consistently |

**Debug Steps**:
```bash
# 1. Capture raw packet on hub UART
idf.py monitor | grep "RX:"

# 2. Decode base64 to see IV + ciphertext + HMAC
python3 << 'EOF'
import base64, binascii
packet_b64 = "YWJjZGVmZ2hpams="  # From serial log
data = base64.b64decode(packet_b64)
print(f"Raw bytes: {binascii.hexlify(data)}")
print(f"IV (16): {binascii.hexlify(data[:16])}")
print(f"Ciphertext (16): {binascii.hexlify(data[16:32])}")
print(f"HMAC (4): {binascii.hexlify(data[32:36])}")
EOF

# 3. Manually compute HMAC to verify
# (Requires matching HMAC key - compare output with logged value)
```

---

### Symptom 2: "Replay Detected" (False Positive)

**Error Message**:
```
[HUB] [REPLAY] SEQ already seen: N1/042
[HUB] Packet rejected (valid packet, but marked duplicate)
```

**Root Causes & Solutions**:

| Cause | Diagnosis | Solution |
|-------|-----------|----------|
| **Sensor rebooted** | SEQ counter wrapped around | Expected behavior - window resets; ignore |
| **Packet retransmission** | Sensor resent same packet | Check sensor code for unintended retries |
| **Window not sliding** | Hub didn't process SEQ updates | Restart hub (clears window) |
| **Sequence wraparound** | SEQ=999 → 0 transition | Normal - sliding window handles this |

**Debug Steps**:
```bash
# 1. Check SEQ sequence on sensor
idf.py monitor | grep "LoRa TX:"
# Look for: SEQ=001, 002, 003... (should increment)

# 2. Check window state on hub
idf.py monitor | grep "Window:"
# Window should contain last 32 SEQ values

# 3. Force window reset
# Restart hub with power-cycle (clears RAM)
```

---

### Symptom 3: "Decryption Failed" Error

**Error Message**:
```
[HUB] Decryption failed: PKCS#7 padding invalid
[HUB] Plaintext garbage
```

**Root Causes & Solutions**:

| Cause | Diagnosis | Solution |
|-------|-----------|----------|
| **Ciphertext corrupted** | RF noise modified encrypted data | Improve antenna placement, reduce distance |
| **Wrong AES key** | Key mismatch causes garbage output | Verify keys match in `lora_crypto_keys.h` |
| **Padding validation bug** | PKCS#7 check too strict | Update to latest lora_crypto.c |
| **IV corruption** | First 16 bytes of packet damaged | Check UART cable quality |

**Debug Steps**:
```bash
# 1. Log encrypted packet (before decryption)
// In lora_crypto.c, add:
ESP_LOGI(TAG, "IV: %s", bin2hex(iv, 16));
ESP_LOGI(TAG, "Ciphertext: %s", bin2hex(ciphertext, 16));

# 2. Manually decrypt with known key
python3 -c "
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
import os

aes_key = bytes.fromhex('00112233445566778899aabbccddeeff')
iv = bytes.fromhex('abcdef0123456789...')  # From log
ciphertext = bytes.fromhex('...')  # From log

cipher = Cipher(algorithms.AES(aes_key), modes.CBC(iv))
decryptor = cipher.decryptor()
plaintext = decryptor.update(ciphertext) + decryptor.finalize()
print(f'Plaintext: {plaintext}')
"

# 3. Verify PKCS#7 padding
# Last byte should = number of padding bytes (e.g., 0x05 for 5 bytes)
```

---

## 2. Unit Tests

### Test 2.1: AES Encryption/Decryption

```bash
cd firmware/test
python3 test_encryption.py

Expected Output:
✓ AES encrypt/decrypt round-trip OK
✓ Ciphertext != Plaintext (encrypted)
✓ Different IV → Different ciphertext
✓ All 100 tests passed
```

**If test fails**:
```bash
# Check mbedTLS is linked
grep "mbedtls" firmware/node_1/CMakeLists.txt

# Verify include paths
grep "#include.*mbedtls" firmware/node_1/main/lora_crypto.c

# Rebuild with clean
idf.py fullclean && idf.py build
```

---

### Test 2.2: HMAC Verification

```bash
python3 test_encryption.py --hmac

Expected:
✓ HMAC computed correctly
✓ Valid HMAC verified ✓
✓ Tampered data rejected ✗
✓ 100/100 tests passed
```

**If test fails**:
- Verify key: `echo "HMAC_KEY = 0x..." | xxd`
- Check HMAC library: `grep "mbedtls_md"` in lora_crypto.c
- Ensure 4-byte truncation: `memcpy(hmac_out, hmac_computed, 4)`

---

### Test 2.3: Sliding Window Anti-Replay

```bash
python3 test_encryption.py --replay

Expected:
✓ SEQ 1,2,3...32: All accepted
✓ SEQ 1 (duplicate): Rejected
✓ SEQ 0 (old): Rejected outside window
✓ SEQ 50 (wraparound): Accepted (window slides)
✓ 100/100 tests passed
```

**If test fails**:
- Check window size: `#define REPLAY_WINDOW_SIZE 32`
- Verify SEQ counter logic in `lora_crypto.c`
- Ensure window resets on large SEQ jump (>500)

---

## 3. Common Bugs & Fixes

### Bug #1: Timing Attack in HMAC Comparison

**Problem**: Using `memcmp()` leaks information via comparison time

**Bad Code**:
```c
if (memcmp(expected_hmac, computed_hmac, 4) != 0) {
    return HMAC_MISMATCH;  // Fast exit reveals partial match
}
```

**Fixed Code**:
```c
// Constant-time comparison (always compares all bytes)
uint8_t result = 0;
for (int i = 0; i < 4; i++) {
    result |= expected_hmac[i] ^ computed_hmac[i];
}
if (result != 0) {
    return HMAC_MISMATCH;  // Time independent
}
```

---

### Bug #2: PKCS#7 Padding Validation

**Problem**: Invalid padding bytes not validated

**Bad Code**:
```c
// Assumes last byte is valid padding
uint8_t padding_len = plaintext[plaintext_len - 1];
plaintext_len -= padding_len;  // No validation!
```

**Fixed Code**:
```c
// Validate all padding bytes
uint8_t padding_len = plaintext[plaintext_len - 1];
if (padding_len > 16 || padding_len == 0) {
    return INVALID_PADDING;
}
for (int i = 0; i < padding_len; i++) {
    if (plaintext[plaintext_len - 1 - i] != padding_len) {
        return INVALID_PADDING;  // Padding byte mismatch
    }
}
plaintext_len -= padding_len;
```

---

### Bug #3: Random IV Not Truly Random

**Problem**: IV not random = same plaintext → same ciphertext (pattern leak)

**Bad Code**:
```c
// Pseudo-random (predictable)
static uint32_t seed = 12345;
seed = (seed * 1103515245 + 12345) % (1 << 31);
memcpy(iv, &seed, 4);  // Only 4 bytes of IV!
```

**Fixed Code**:
```c
// True random from hardware RNG
esp_fill_random(iv, 16);  // Use ESP32's /dev/urandom equivalent
// Each packet gets unique 16-byte IV
```

---

## 4. Performance Profiling

### Encryption Overhead

```c
// Add timing to lora_crypto.c
uint32_t start = esp_timer_get_time();

// AES encrypt
esp_aes_context ctx;
esp_aes_setkey(&ctx, aes_key, 128);
esp_aes_crypt_cbc(&ctx, ESP_AES_ENCRYPT, plaintext_len, iv, plaintext, ciphertext);

uint32_t aes_time = esp_timer_get_time() - start;
ESP_LOGI(TAG, "AES encrypt: %d µs", aes_time);

// HMAC compute
start = esp_timer_get_time();
mbedtls_md_hmac(...);
uint32_t hmac_time = esp_timer_get_time() - start;
ESP_LOGI(TAG, "HMAC: %d µs", hmac_time);

// Total
ESP_LOGI(TAG, "Total crypto: %d µs", aes_time + hmac_time);
```

**Expected Results**:
- AES encrypt (32 bytes): 15-25 µs ✅
- HMAC-SHA256: 10-20 µs ✅
- Total: <50 µs ✅

---

## 5. Security Hardening Checklist

- [x] HMAC verified before decryption (Encrypt-then-MAC)
- [x] Constant-time HMAC comparison (no timing leaks)
- [x] Random IV per packet (prevents pattern analysis)
- [x] PKCS#7 padding validated (prevents padding oracle)
- [x] Sliding window anti-replay (prevents exact replay)
- [x] SEQ counter incremented (prevents old packet reuse)
- [x] Keys not hardcoded in public repo (excluded .gitignore)
- [x] No debug logging of sensitive data (keys, plaintexts)

---

## 6. Field Debug Checklist

| Check | Status | Notes |
|-------|--------|-------|
| Both devices have matching AES key | ✅ | Verify in lora_crypto_keys.h |
| Both devices have matching HMAC key | ✅ | Same 32-byte key |
| Hub can receive encrypted packets | ✅ | Check UART → LoRa RX LED |
| HMAC verification working | ✅ | Log should show "HMAC OK" |
| Sliding window initialized | ✅ | Window shows 0 entries on startup |
| No RF interference | ✅ | Test away from Wi-Fi/Bluetooth |
| Antenna connected properly | ✅ | Tight SMA connectors |

---

**Last Updated**: June 17, 2026  
**Status**: ✅ Production Ready

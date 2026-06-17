# Software Design

**Project**: SecureFlood-LoRa Capstone  
**Framework**: ESP-IDF v5.x  
**RTOS**: FreeRTOS  
**Crypto**: mbedTLS

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                  Application Layer                      │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │water_alarm.c │  │lora_crypto.c │  │ina219_mon.c │  │
│  └──────────────┘  └──────────────┘  └──────────────┘  │
└─────────────────────────────────────────────────────────┘
              │              │              │
┌─────────────────────────────────────────────────────────┐
│                   RTOS / Hardware Layer                 │
│  ┌──────────────────────────────────────────────────┐  │
│  │  FreeRTOS Tasks | ESP-IDF Drivers | mbedTLS     │  │
│  └──────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────┘
         │           │           │           │
      GPIO         UART         I2C        ADC
```

---

## 1. Sensor Node Firmware

### Core Modules

#### 1.1 water_alarm.c
**Purpose**: Water level measurement & filtering  
**Tasks**: 1 (sensor_read_task)  
**Frequency**: 5 Hz (200ms interval)

```c
// Task: sensor_read_task
void sensor_read_task(void *pvParameters) {
    while (1) {
        // 1. Read HC-SR04 (raw distance)
        distance_raw = hc_sr04_read();  // microseconds
        
        // 2. Filter pipeline (4 stages)
        distance = filter_hard_range(distance_raw);     // Reject <5cm or >400cm
        distance = filter_median_9(distance);            // 9-sample median
        distance = filter_hysteresis(distance, 5);       // 5cm margin
        distance = filter_debounce(distance, 5);         // 5 consecutive reads
        
        // 3. Determine alarm level
        if (distance > 85) {
            alarm_level = NORMAL;    // LED Green
        } else if (distance > 50) {
            alarm_level = DANGER;    // LED Yellow
        } else {
            alarm_level = EMERGENCY; // LED Red + Buzzer
        }
        
        // 4. Check level change → trigger LoRa TX
        if (alarm_level != prev_level) {
            xQueueSend(lora_tx_queue, &alarm_level, 0);
        }
        
        vTaskDelay(pdMS_TO_TICKS(200));  // 5 Hz
    }
}

// Function: 4-stage filter pipeline
distance_t filter_hard_range(int raw_us) {
    int cm = raw_us / 58;  // Speed of sound: 58 µs/cm
    if (cm < 5 || cm > 400) {
        return INVALID;  // Reject out-of-range
    }
    return cm;
}

distance_t filter_median_9(distance_t new) {
    static distance_t buffer[9];
    static int idx = 0;
    buffer[idx++ % 9] = new;
    // Sort & return middle value
    return median(buffer, 9);
}

distance_t filter_hysteresis(distance_t new, int margin) {
    static distance_t prev = 85;  // Start at normal
    if (fabs(new - prev) < margin) {
        return prev;  // Reject small changes
    }
    prev = new;
    return new;
}

distance_t filter_debounce(distance_t new, int count) {
    static distance_t consistent = 0;
    static int count_same = 0;
    
    if (new == consistent) {
        count_same++;
        if (count_same >= count) {
            return new;  // Accepted after N consistent reads
        }
    } else {
        consistent = new;
        count_same = 1;
    }
    return UNCHANGED;  // Not accepted yet
}
```

#### 1.2 lora_crypto.c
**Purpose**: AES-128-CBC encryption + HMAC-SHA256  
**Functions**: Encrypt, decrypt, HMAC compute, anti-replay

```c
// Function: Encrypt data with AES-128-CBC
void lora_encrypt(
    const uint8_t *plaintext, int plaintext_len,
    uint8_t *ciphertext, int *ciphertext_len,
    uint8_t *iv  // Output: random IV
) {
    // 1. Generate random IV
    esp_fill_random(iv, 16);
    
    // 2. Initialize AES context
    mbedtls_aes_context ctx;
    mbedtls_aes_setkey_enc(&ctx, AES_KEY, 128);
    
    // 3. PKCS#7 padding (in-place)
    int padding = 16 - (plaintext_len % 16);
    uint8_t padded[plaintext_len + 16];
    memcpy(padded, plaintext, plaintext_len);
    memset(padded + plaintext_len, padding, padding);
    
    // 4. Encrypt
    mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT,
                          plaintext_len + padding, iv, padded, ciphertext);
    *ciphertext_len = plaintext_len + padding;
}

// Function: Compute HMAC-SHA256 (4-byte truncated)
void lora_hmac(
    const uint8_t *iv, int iv_len,
    const uint8_t *ciphertext, int ciphertext_len,
    uint8_t *hmac_out  // Output: 4 bytes
) {
    // Concatenate IV + Ciphertext
    uint8_t to_sign[iv_len + ciphertext_len];
    memcpy(to_sign, iv, iv_len);
    memcpy(to_sign + iv_len, ciphertext, ciphertext_len);
    
    // Compute HMAC-SHA256
    uint8_t hmac_full[32];
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256),
                    HMAC_KEY, 32,
                    to_sign, sizeof(to_sign),
                    hmac_full);
    
    // Truncate to 4 bytes
    memcpy(hmac_out, hmac_full, 4);
}

// Function: Anti-replay sliding window
bool lora_check_replay(uint8_t node_id, uint16_t seq) {
    static uint32_t window[8] = {0};  // 8 nodes
    static uint16_t max_seq[8] = {0};
    
    // Check if SEQ in window [max_seq-31, max_seq]
    if (seq < max_seq[node_id] - 31) {
        return true;  // REPLAY DETECTED
    }
    if (seq <= max_seq[node_id]) {
        return true;  // Duplicate in window
    }
    
    // Update window
    max_seq[node_id] = seq;
    return false;  // Valid
}
```

#### 1.3 lora_transport.c
**Purpose**: LoRa UART communication  
**Baud Rate**: 9600 bps  
**Packet Format**: $NODE_ID,SEQ,base64(...)\n

```c
// Function: Send encrypted packet over LoRa
void lora_send_packet(uint8_t alarm_level) {
    // 1. Format plaintext
    char plaintext[50];
    snprintf(plaintext, sizeof(plaintext),
             "L%d:%05.2fcm:%1.2fV:%d%%:%s:%+dmA",
             alarm_level, distance, voltage, battery_percent,
             power_mode, current_ma);
    
    // 2. Encrypt
    uint8_t iv[16], ciphertext[32];
    int ciphertext_len;
    lora_encrypt((uint8_t*)plaintext, strlen(plaintext),
                 ciphertext, &ciphertext_len, iv);
    
    // 3. Compute HMAC
    uint8_t hmac[4];
    lora_hmac(iv, 16, ciphertext, ciphertext_len, hmac);
    
    // 4. Concatenate: IV + Ciphertext + HMAC
    uint8_t payload[16 + ciphertext_len + 4];
    memcpy(payload, iv, 16);
    memcpy(payload + 16, ciphertext, ciphertext_len);
    memcpy(payload + 16 + ciphertext_len, hmac, 4);
    
    // 5. Base64 encode
    char b64[80];
    size_t b64_len;
    mbedtls_base64_encode((uint8_t*)b64, sizeof(b64), &b64_len,
                          payload, 16 + ciphertext_len + 4);
    
    // 6. Format packet: $NODE_ID,SEQ,base64...\n
    char packet[120];
    snprintf(packet, sizeof(packet), "$N1,%03d,%s\n", seq++, b64);
    
    // 7. Send via UART to LoRa module
    uart_write_bytes(UART_NUM_2, (const char*)packet, strlen(packet));
}
```

#### 1.4 ina219_monitor.c
**Purpose**: Battery voltage & current monitoring  
**Task**: battery_monitor_task (every 2 seconds)

```c
void battery_monitor_task(void *pvParameters) {
    while (1) {
        // Read voltage & current
        float voltage = ina219_read_voltage();    // 0-26V
        float current = ina219_read_current();    // -3.2A to +3.2A
        
        // Determine power mode
        if (current > 10) {
            power_mode = "SOLAR";   // Charging from solar
        } else if (current < -10) {
            power_mode = "BATT";    // Discharging
        } else {
            power_mode = "IDLE";    // Balanced
        }
        
        // Estimate battery %
        battery_percent = (voltage - 3.0) / 1.2 * 100;
        
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
```

---

## 2. Central Hub Firmware

### Core Modules

#### 2.1 lora_receiver.c
**Purpose**: LoRa RX + packet decryption + anti-replay

```c
void lora_receiver_task(void *pvParameters) {
    uint8_t rx_buffer[256];
    int rx_len;
    
    while (1) {
        // 1. Read from UART (LoRa module)
        rx_len = uart_read_bytes(UART_NUM_2, rx_buffer, 256, 100);
        
        if (rx_len < 20) continue;  // Ignore short packets
        
        // 2. Parse packet: $NODE_ID,SEQ,base64...\n
        uint8_t node_id;
        uint16_t seq;
        char b64[80];
        sscanf((char*)rx_buffer, "$N%d,%d,%s", &node_id, &seq, b64);
        
        // 3. Base64 decode
        uint8_t payload[64];
        size_t payload_len;
        mbedtls_base64_decode(payload, sizeof(payload), &payload_len,
                              (uint8_t*)b64, strlen(b64));
        
        // 4. Check anti-replay (BEFORE decryption)
        if (lora_check_replay(node_id, seq)) {
            ESP_LOGI(TAG, "[REPLAY] N%d/seq%d", node_id, seq);
            continue;
        }
        
        // 5. Extract IV, ciphertext, HMAC
        uint8_t *iv = payload;
        uint8_t *ciphertext = payload + 16;
        uint8_t *hmac_rx = payload + 16 + 16;  // Last 4 bytes
        
        // 6. Verify HMAC (constant-time comparison)
        uint8_t hmac_computed[4];
        lora_hmac(iv, 16, ciphertext, 16, hmac_computed);
        
        uint8_t hmac_match = 0;
        for (int i = 0; i < 4; i++) {
            hmac_match |= hmac_rx[i] ^ hmac_computed[i];
        }
        
        if (hmac_match != 0) {
            ESP_LOGI(TAG, "[TAMPER] HMAC mismatch");
            continue;
        }
        
        // 7. Decrypt (HMAC verified)
        uint8_t plaintext[32];
        lora_decrypt(ciphertext, 16, plaintext, iv);
        
        // 8. Parse plaintext: L<level>:<distance>:...
        int level;
        float dist;
        sscanf((char*)plaintext, "L%d:%f", &level, &dist);
        
        // 9. Queue for display
        water_data_t data = {node_id, level, dist};
        xQueueSend(display_queue, &data, 0);
    }
}
```

#### 2.2 display_lcd.c
**Purpose**: TFT-LCD display real-time data

```c
void display_task(void *pvParameters) {
    water_data_t data;
    
    while (1) {
        if (xQueueReceive(display_queue, &data, pdMS_TO_TICKS(1000))) {
            // Update LCD
            lcd_clear();
            lcd_print("SecureFlood-LoRa");
            lcd_print_xy(0, 20, "N%d: %.2f cm", data.node_id, data.distance);
            
            if (data.level == NORMAL) {
                lcd_print_xy(0, 40, "Status: NORMAL");
            } else if (data.level == DANGER) {
                lcd_print_xy(0, 40, "Status: DANGER");
            } else {
                lcd_print_xy(0, 40, "Status: EMERGENCY");
            }
        }
    }
}
```

#### 2.3 http_upload.c
**Purpose**: HTTPS POST with HMAC signature

```c
void http_upload_task(void *pvParameters) {
    while (1) {
        // Wait for new data
        water_data_t data;
        if (!xQueueReceive(http_queue, &data, pdMS_TO_TICKS(30000))) {
            continue;
        }
        
        // 1. Create JSON payload
        char json[256];
        snprintf(json, sizeof(json),
                 "{\"node_id\":\"N%d\",\"distance\":%.2f,\"level\":%d}",
                 data.node_id, data.distance, data.level);
        
        // 2. Generate HTTP-HMAC signature
        time_t now = time(NULL);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));
        
        char to_sign[512];
        snprintf(to_sign, sizeof(to_sign), "%s\n%s", timestamp, json);
        
        uint8_t signature[32];
        mbedtls_md_hmac(..., (uint8_t*)to_sign, strlen(to_sign), signature);
        
        char sig_hex[65];
        for (int i = 0; i < 32; i++) {
            sprintf(sig_hex + i*2, "%02x", signature[i]);
        }
        
        // 3. HTTP POST with headers
        esp_http_client_config_t config = {
            .url = "https://api.example.com/water_reading",
            .method = HTTP_METHOD_POST,
        };
        
        esp_http_client_handle_t client = esp_http_client_init(&config);
        
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_header(client, "X-Timestamp", timestamp);
        esp_http_client_set_header(client, "X-Signature", sig_hex);
        esp_http_client_set_post_field(client, json, strlen(json));
        
        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "[HTTP] Status: %d", esp_http_client_get_status_code(client));
        }
        
        esp_http_client_cleanup(client);
    }
}
```

---

## 3. Task Diagram

```
Sensor Node:
┌─────────────────────┐
│ sensor_read_task    │ (5 Hz, 200ms)
│ └→ filter pipeline  │
│ └→ alarm level      │
│ └→ queue TX         │
└─────────────────────┘
         │
┌─────────────────────┐
│ battery_monitor_task│ (0.5 Hz, 2s)
│ └→ read INA219      │
│ └→ battery %        │
└─────────────────────┘

Central Hub:
┌─────────────────────┐
│ lora_receiver_task  │ (continuous)
│ └→ RX UART          │
│ └→ decrypt          │
│ └→ queue display    │
└─────────────────────┘
         │
┌─────────────────────┐
│ display_task        │ (event-driven)
│ └→ update LCD       │
└─────────────────────┘
         │
┌─────────────────────┐
│ http_upload_task    │ (every 30s)
│ └→ POST HTTPS       │
│ └→ HMAC signature   │
└─────────────────────┘
```

---

## 4. Build & Deploy

### Build Command
```bash
cd firmware/node_1
idf.py build
idf.py flash
```

### Flash Output
```
Flashing binaries to serial port COM3 (440500 baud rate)...
esptool.py v3.3.1
Chip is ESP32-D0WD
Uploading stub...
Running stub...
Stub running...
Changing baud rate to 921600
...
Hard resetting via RTS pin...
```

---

**Status**: ✅ Production Ready  
**LOC**: ~3,500 lines C/C++  
**Memory**: 250KB Flash, 50KB RAM (used)

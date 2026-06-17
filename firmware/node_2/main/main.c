#include <stdio.h>
#include <string.h>         

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_random.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "hc_sr04.h"
#include "lora_as32_send.h"
#include "data_filter.h"
#include "water_alarm.h"
#include "lora_crypto.h"
#include "ina219.h"

// ─── Pin (ESP32-C3) ─────────────────────────────────────────
#define TRIG             GPIO_NUM_2
#define ECHO             GPIO_NUM_3
#define ULTRASONIC_OUT   0xFF  // không dùng chân OUT, đọc bằng filter

#define LED_NORMAL       GPIO_NUM_10
#define LED_DANGER       GPIO_NUM_18
#define LED_EMERGENCY    GPIO_NUM_19

// ─── INA219 I2C (ESP32 WROOM-32, chân I2C mặc định) ─────────
#define INA_SDA          GPIO_NUM_1
#define INA_SCL          GPIO_NUM_0
#define INA_ADDR         0x40

// ─── Cấu hình bộ lọc ────────────────────────────────────────
#define FILTER_THRESHOLD_CM     5.0f
#define FILTER_HEARTBEAT_MS     30000
#define FILTER_MIN_INTERVAL_MS  5000
#define SAMPLE_INTERVAL_MS      230

// ─── Đo pin ──────────────────────────────────────────────────
#define BAT_SAMPLE_PERIOD_MS    2000   // đo pin mỗi 2s
#define BAT_EMA_ALPHA           0.3f   // hệ số làm trơn

// ─── State pin (global, cập nhật bởi update_battery, đọc khi gửi) ──
static float       s_bat_voltage = -1.0f;  // V
static float       s_bat_current = 0.0f;   // mA, >0 = nạp, <0 = xả
static int         s_bat_percent = -1;     // %
static pwr_state_t s_pwr_state   = PWR_STATE_UNKNOWN;
static bool        s_bat_ok      = false;

static void update_battery(void)
{
    float v, ma;
    int   pct;
    pwr_state_t st;
    if (!ina219_read_all(&v, &ma, &pct, &st)) {
        s_bat_ok = false;
        return;
    }

    // EMA smoothing — giảm nhiễu khi LoRa TX gây sụt áp đột biến
    if (s_bat_voltage < 0.0f) {
        s_bat_voltage = v;       // lần đầu lấy trực tiếp
        s_bat_current = ma;
    } else {
        s_bat_voltage = BAT_EMA_ALPHA * v  + (1.0f - BAT_EMA_ALPHA) * s_bat_voltage;
        s_bat_current = BAT_EMA_ALPHA * ma + (1.0f - BAT_EMA_ALPHA) * s_bat_current;
    }
    s_bat_percent = ina219_voltage_to_percent(s_bat_voltage);
    s_pwr_state   = ina219_classify_power(s_bat_current, s_bat_voltage);
    s_bat_ok      = true;
}

void app_main(void)
{
    // 1. Khởi tạo LoRa và đặt Node ID
    lora_init();
    lora_crypto_init();
    lora_config();
    lora_read_config(); 
    lora_set_node_id("N2");                          

    // 2. Cảm biến siêu âm (đầy đủ 5 chân)
    hc_sr04_init_full(TRIG, ECHO, -1);

    // 3. LED + filter
    water_alarm_init(LED_NORMAL, LED_DANGER, LED_EMERGENCY);

    // ═══ INA219 ═══
    esp_err_t ina_err = ina219_init(INA_SDA, INA_SCL, INA_ADDR);
    if (ina_err != ESP_OK) {
        ESP_LOGW("MAIN", "INA219 init failed (%s) — battery monitoring disabled",
                 esp_err_to_name(ina_err));
    } else {
        update_battery();
        if (s_bat_ok) {
            ESP_LOGI("MAIN", "Battery initial: %.2fV / %d%% / %+.1fmA / %s",
                     s_bat_voltage, s_bat_percent, s_bat_current,
                     ina219_pwr_state_str(s_pwr_state));
        }
    }

    data_filter_init(FILTER_THRESHOLD_CM,
                 FILTER_HEARTBEAT_MS,
                 FILTER_MIN_INTERVAL_MS);

    printf("[N2] Hệ thống sẵn sàng.\n");

    int64_t last_bat_ms = esp_timer_get_time() / 1000;

    while (1) {
        // ═══ Cập nhật pin định kỳ (không cần đo mỗi vòng) ═══
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_bat_ms >= BAT_SAMPLE_PERIOD_MS) {
            update_battery();
            last_bat_ms = now_ms;
        }

        float d = hc_sr04_get_distance();
        // bool  hw_alarm  = hc_sr04_alarm_active();    // chân OUT vật lý

        if (d < 0) {
            water_alarm_clear();
            water_alarm_reset_filter();
            lora_send_packet("ERR:TIMEOUT");
            data_filter_confirm_sent(d);
            printf("[N2] Cảm biến timeout.\n");

        } else {
            // Pipeline chống cảnh báo giả
            float smoothed;
            water_level_t lv = water_alarm_process(d, &smoothed);

            water_alarm_update_leds(lv);

            // Dùng giá trị ĐÃ LỌC cho LoRa filter (không phải d thô)
            if (data_filter_should_send(smoothed, lv)) {
                uint32_t jitter = esp_random() % 400;   // (Node 1) hoặc 600 + (Node 2)
                vTaskDelay(pdMS_TO_TICKS(jitter));

                // Build plaintext 
                char value[128];
                if (s_bat_ok) {
                    snprintf(value, sizeof(value),
                             "L%d:%.2fcm:%.2fV:%d%%:%s:%+.0fmA",
                             lv, smoothed,
                             s_bat_voltage, s_bat_percent,
                             ina219_pwr_state_str(s_pwr_state),
                             s_bat_current);
                } else {
                    snprintf(value, sizeof(value),
                             "L%d:%.2fcm:?V:?%%:UNK:?mA",
                             lv, smoothed);
                }

                // ═══ MỚI: Encrypt trước khi gửi ═══
                char encrypted[200];
                int enc_len = lora_crypto_encrypt(value, strlen(value), encrypted, sizeof(encrypted));
                if (enc_len < 0) {
                    ESP_LOGE("MAIN", "Encrypt failed!");
                } else {
                    encrypted[enc_len] = '\0';
                    lora_send_packet(encrypted);    // gửi chuỗi base64 đã encrypt
                    data_filter_confirm_sent(smoothed);
                    data_filter_set_last_level(lv);
                    printf("[Main] Sent encrypted (%d bytes) | %s | %s\n",
                           enc_len, water_alarm_level_str(lv), value);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_INTERVAL_MS));
    }
}
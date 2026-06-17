#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_sntp.h"

#include "web_uploader.h"
#include "lora_receive.h"
#include "parser_lora.h"
#include "lcd_display.h"
#include "lora_crypto.h"
#include "lora_replay.h"

// === Cấu hình WiFi & Web ===
#define WIFI_SSID       "Cali House"
#define WIFI_PASSWORD   "anbinh88"
#define WEB_ENDPOINT    "https://thaiiihieppp.io.vn/api/ingest.php"
#define WEB_API_KEY     "6a8b3e5d9c1f047a2d6b3e5d9c1f047a2d6b3e5d9c1f047a2d6b3e5d9c1f047a"

#define LORA_BUF_SIZE       256
#define LORA_QUEUE_DEPTH    8
#define LORA_RX_CHUNK       128
#define LORA_ACCUM_SIZE     512

#define MAX_NODES           4
#define DISPLAY_REFRESH_MS  1000

typedef struct {
    uint8_t data[LORA_BUF_SIZE];
    int     len;
} lora_packet_t;

typedef struct {
    char    id[8];
    bool    used;
    int     level;
    float   distance;
    int     seq;
    bool    is_error;
    int64_t last_seen_us;
    bool    has_battery;
    float   voltage;
    int     battery_pct;
    char    power_state[6];
    float   current_ma;
} node_state_t;

static node_state_t  s_nodes[MAX_NODES];
static QueueHandle_t s_lora_queue = NULL;

static node_state_t *node_find_or_create(const char *id)
{
    for (int i = 0; i < MAX_NODES; i++) {
        if (s_nodes[i].used && strcmp(s_nodes[i].id, id) == 0)
            return &s_nodes[i];
    }
    for (int i = 0; i < MAX_NODES; i++) {
        if (!s_nodes[i].used) {
            strncpy(s_nodes[i].id, id, sizeof(s_nodes[i].id) - 1);
            s_nodes[i].id[sizeof(s_nodes[i].id) - 1] = '\0';
            s_nodes[i].used = true;
            return &s_nodes[i];
        }
    }
    return NULL;
}

static bool parse_value_field(const char *value,
                              int *level, float *dist,
                              bool *out_has_bat,
                              float *out_voltage, int *out_battery_pct,
                              char *out_state, size_t state_sz,
                              float *out_current_ma)
{
    *out_has_bat = false;

    if (strncmp(value, "ERR:", 4) == 0) return false;

    int   lv;
    float d;
    float v;
    int   pct;
    char  st[8] = {0};
    int   ma;
    int   cv;

    // ═══ Format B (đầy đủ, có suffix "cm", "V", "%", "mA") ═══
    // L0:9.84cm:3.44V:7%:BATT:-236mA
    int n = sscanf(value, "L%d:%fcm:%fV:%d%%:%7[^:]:%dmA",
                   &lv, &d, &v, &pct, st, &ma);
    if (n == 6) {
        *level           = lv;
        *dist            = d;
        *out_voltage     = v;
        *out_battery_pct = pct;
        *out_current_ma  = (float)ma;
        strncpy(out_state, st, state_sz - 1);
        out_state[state_sz - 1] = '\0';
        *out_has_bat = true;
        return true;
    }

    // ═══ Format A (rút gọn, centi-volt) ═══
    // L0:5.2:344:7:B-:-236
    n = sscanf(value, "L%d:%f:%d:%d:%7[^:]:%d",
               &lv, &d, &cv, &pct, st, &ma);
    if (n == 6) {
        *level           = lv;
        *dist            = d;
        *out_voltage     = cv / 100.0f;
        *out_battery_pct = pct;
        *out_current_ma  = (float)ma;
        strncpy(out_state, st, state_sz - 1);
        out_state[state_sz - 1] = '\0';
        *out_has_bat = true;
        return true;
    }

    // ═══ Format C (cũ — chỉ level + dist) ═══
    if (sscanf(value, "L%d:%fcm", level, dist) == 2) {
        return true;
    }
    return false;
}

static void lora_rx_task(void *arg)
{
    static char accum[LORA_ACCUM_SIZE];
    static int  accum_len = 0;
    uint8_t buf[LORA_RX_CHUNK];

    for (;;) {
        int len = lora_receive_data(buf, sizeof(buf));
        if (len <= 0) continue;

        for (int i = 0; i < len; i++) {
            char c = (char)buf[i];
            if (c == '\r') continue;

            if (c == '\n') {
                if (accum_len > 0) {
                    lora_packet_t pkt;
                    int copy_len = (accum_len < LORA_BUF_SIZE - 1)
                                       ? accum_len : LORA_BUF_SIZE - 1;
                    memcpy(pkt.data, accum, copy_len);
                    pkt.data[copy_len] = '\0';
                    pkt.len = copy_len;
                    xQueueSend(s_lora_queue, &pkt, pdMS_TO_TICKS(100));
                    accum_len = 0;
                }
            } else {
                if (accum_len < (int)sizeof(accum) - 1)
                    accum[accum_len++] = c;
                else
                    accum_len = 0;
            }
        }
    }
}

static void lora_parse_task(void *arg)
{
    lora_packet_t pkt;
    for (;;) {
        if (xQueueReceive(s_lora_queue, &pkt, portMAX_DELAY) != pdTRUE)
            continue;

        lora_msg_t m;
        if (!parser_extract((char *)pkt.data, pkt.len, &m)) {
            printf("[RX] Parse fail (len=%d): %.*s\n", pkt.len, pkt.len, pkt.data);
            continue;
        }

        char plaintext[128];
        int pt_len = lora_crypto_decrypt(m.value, strlen(m.value),
                                          plaintext, sizeof(plaintext));
        if (pt_len < 0) {
            printf("[RX] %s SEQ=%03d - DECRYPT FAILED\n", m.node_id, m.seq);
            continue;
        }

        if (!lora_replay_check(m.node_id, (uint32_t)m.seq)) {
            printf("[RX] %s SEQ=%03d - REPLAY REJECTED\n", m.node_id, m.seq);
            continue;
        }

        node_state_t *n = node_find_or_create(m.node_id);
        if (n == NULL) {
            printf("[RX] Hết slot node, bỏ %s\n", m.node_id);
            continue;
        }
        n->seq          = m.seq;
        n->last_seen_us = esp_timer_get_time();

        int   lv;
        float d;
        bool  has_bat = false;
        float voltage = 0.0f;
        int   battery_pct = 0;
        char  power_state[6] = {0};
        float current_ma = 0.0f;

        if (parse_value_field(plaintext, &lv, &d, &has_bat,
                              &voltage, &battery_pct, power_state,
                              sizeof(power_state), &current_ma)) {
            n->level    = lv;
            n->distance = d;
            n->is_error = false;
            n->has_battery = has_bat;
            if (has_bat) {
                n->voltage     = voltage;
                n->battery_pct = battery_pct;
                strncpy(n->power_state, power_state, sizeof(n->power_state) - 1);
                n->current_ma  = current_ma;
            }
            lcd_history_push_for(m.node_id, d);

            web_uploader_post(m.node_id, d, lv, m.seq, false,
                              has_bat ? voltage     : -1.0f,
                              has_bat ? battery_pct : -1,
                              has_bat ? power_state : NULL,
                              has_bat ? current_ma  : 0.0f);

            if (has_bat) {
                printf("[RX] %s SEQ=%03d | L%d d=%.1f V=%.2f %d%% %s %+dmA ✓\n",
                       m.node_id, m.seq, lv, d,
                       voltage, battery_pct, power_state, (int)current_ma);
            } else {
                printf("[RX] %s SEQ=%03d | %s ✓ (no battery data)\n",
                       m.node_id, m.seq, plaintext);
            }
        } else {
            n->is_error = true;
            printf("[RX] %s SEQ=%03d | Parse fail: %s\n", m.node_id, m.seq, plaintext);
        }
    }
}

static void display_task(void *arg)
{
    for (;;) {
        lcd_node_info_t infos[MAX_NODES];
        int count = 0;
        int64_t now = esp_timer_get_time();

        for (int i = 0; i < MAX_NODES; i++) {
            if (!s_nodes[i].used) continue;
            infos[count].id          = s_nodes[i].id;
            infos[count].status      = (s_nodes[i].level == 2) ? LCD_STATUS_EMERGENCY
                                     : (s_nodes[i].level == 1) ? LCD_STATUS_DANGER
                                                                : LCD_STATUS_NORMAL;
            infos[count].distance_cm = s_nodes[i].distance;
            infos[count].seq         = s_nodes[i].seq;
            infos[count].is_error    = s_nodes[i].is_error;
            infos[count].age_seconds = (int)((now - s_nodes[i].last_seen_us) / 1000000);
            count++;
        }

        if (count == 0)
            lcd_show_boot_screen();
        else
            lcd_show_multi_node(infos, count);

        vTaskDelay(pdMS_TO_TICKS(DISPLAY_REFRESH_MS));
    }
}

void app_main(void)
{
    lcd_init();
    lcd_show_boot_screen();

    lora_crypto_init();
    lora_replay_init();

    lora_receive_init();
    lora_receive_config();
    lora_receive_read_config();

    esp_err_t err = web_uploader_init(WIFI_SSID, WIFI_PASSWORD, WEB_ENDPOINT, WEB_API_KEY);
    if (err == ESP_OK) {
        ESP_LOGI("APP", "Web uploader started");
    } else {
        ESP_LOGE("APP", "Web uploader init failed: %s", esp_err_to_name(err));
    }

    ESP_LOGI("APP", "Syncing time with NTP...");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_setservername(1, "time.google.com");
    sntp_init();

    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    while (timeinfo.tm_year < (2024 - 1900) && retry++ < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        time(&now);
        localtime_r(&now, &timeinfo);
    }

    if (retry >= 20) {
        ESP_LOGW("APP", "NTP sync timeout, HMAC may fail");
    } else {
        setenv("TZ", "ICT-7", 1);
        tzset();
        ESP_LOGI("APP", "Time synced: %s", asctime(&timeinfo));
    }

    s_lora_queue = xQueueCreate(LORA_QUEUE_DEPTH, sizeof(lora_packet_t));
    if (s_lora_queue == NULL) {
        lcd_show_error("QUEUE FAIL");
        return;
    }

    xTaskCreate(lora_rx_task,    "lora_rx",    4096, NULL, 5, NULL);
    xTaskCreate(lora_parse_task, "lora_parse", 4096, NULL, 4, NULL);
    xTaskCreate(display_task,    "display",    4096, NULL, 3, NULL);
}
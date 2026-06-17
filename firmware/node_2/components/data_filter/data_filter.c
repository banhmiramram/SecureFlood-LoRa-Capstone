#include "data_filter.h"

#include <math.h>

#include "esp_timer.h"
#include "esp_log.h"

#define TAG "data_filter"

// Hard floor cho min_interval ngay cả khi alarm escalate
// (tránh spam khi sensor nhiễu gây alarm flicker)
#define EMERGENCY_MIN_INTERVAL_MS  2000

// ─── State nội bộ ────────────────────────────────────────────
static float    s_last_sent       = -9999.0f;
static int64_t  s_last_time_ms    = 0;
static int      s_last_level      = -1;

static float    s_threshold_cm    = 5.0f;
static uint32_t s_heartbeat_ms    = 30000;
static uint32_t s_min_interval_ms = 5000;
static bool     s_initialized     = false;

// ─── Stats ───────────────────────────────────────────────────
static uint32_t s_sent_count       = 0;
static uint32_t s_dropped_rate_lim = 0;

// ─── API ─────────────────────────────────────────────────────
void data_filter_init(float    threshold_cm,
                      uint32_t heartbeat_ms,
                      uint32_t min_interval_ms)
{
    s_threshold_cm    = threshold_cm;
    s_heartbeat_ms    = heartbeat_ms;
    s_min_interval_ms = min_interval_ms;
    s_last_sent       = -9999.0f;
    s_last_time_ms    = 0;
    s_last_level      = -1;
    s_sent_count       = 0;
    s_dropped_rate_lim = 0;
    s_initialized     = true;

    ESP_LOGI(TAG, "Init: threshold=%.1fcm heartbeat=%lums min_interval=%lums",
             threshold_cm, (unsigned long)heartbeat_ms, (unsigned long)min_interval_ms);
}

bool data_filter_should_send(float new_value, int alarm_level)
{
    if (!s_initialized) return true;

    int64_t now_ms  = esp_timer_get_time() / 1000;
    int64_t elapsed = now_ms - s_last_time_ms;

    // Lần đầu chưa gửi gì → gửi luôn
    if (s_last_sent == -9999.0f) return true;

    // ═══ RATE LIMIT — LUÔN ÁP DỤNG (không bypass) ════════════
    // Trường hợp escalate lên EMERGENCY: dùng min_interval ngắn hơn (2s)
    // → vẫn phản hồi nhanh khi thực sự nguy cấp, nhưng không spam
    uint32_t effective_min = s_min_interval_ms;
    bool emerg_escalate = (alarm_level == 2 && s_last_level != 2);
    if (emerg_escalate && EMERGENCY_MIN_INTERVAL_MS < s_min_interval_ms) {
        effective_min = EMERGENCY_MIN_INTERVAL_MS;
    }

    if (elapsed < (int64_t)effective_min) {
        s_dropped_rate_lim++;
        return false;  // CHẶN — bất kể có alarm change hay không
    }

    // ─── Sau khi qua rate limit, mới xét các điều kiện gửi ──

    // Điều kiện 1: mức cảnh báo thay đổi
    if (alarm_level != s_last_level) {
        ESP_LOGI(TAG, "Send: level changed %d→%d", s_last_level, alarm_level);
        return true;
    }

    // Điều kiện 2: heartbeat đến hạn
    if (elapsed >= (int64_t)s_heartbeat_ms) return true;

    // Điều kiện 3: thay đổi đủ lớn
    if (fabsf(new_value - s_last_sent) >= s_threshold_cm) return true;

    return false;
}

void data_filter_confirm_sent(float sent_value)
{
    s_last_sent    = sent_value;
    s_last_time_ms = esp_timer_get_time() / 1000;
    s_sent_count++;

    // Log stats mỗi 50 packet
    if ((s_sent_count % 50) == 0) {
        ESP_LOGI(TAG, "Stats: sent=%lu dropped_rate=%lu",
                 (unsigned long)s_sent_count, (unsigned long)s_dropped_rate_lim);
    }
}

void data_filter_set_last_level(int level)
{
    s_last_level = level;
}
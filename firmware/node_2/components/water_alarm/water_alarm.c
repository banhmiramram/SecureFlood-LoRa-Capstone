#include "water_alarm.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

#define TAG "water_alarm"

// ─── Ngưỡng KHOẢNG CÁCH (cm) ─────────────────────────────────
#define DIST_DANGER_CM       100.0f  // 1m = ngưỡng nguy hiểm
#define DIST_EMERGENCY_CM    85.0f   // 85cm = ngưỡng khẩn cấp (còn tùy bể cao hay thấp, có thể điều chỉnh)

// ─── Tham số filter (BALANCE: phản hồi nhanh + chống nhiễu) ─
#define MEDIAN_WINDOW         9      // 9 mẫu × 200ms = 1.8s lịch sử
#define HYST_MARGIN_CM        5.0f   // biên hysteresis 5cm
#define CONFIRM_COUNT         5      // 5 mẫu liên tiếp = 1s xác nhận

// ─── Hard range — CHỈ loại giá trị hardware-impossible ──────
#define MIN_VALID_DIST_CM     5.0f   // HC-SR04 datasheet min ~2cm
#define MAX_VALID_DIST_CM     400.0f // HC-SR04 max range

// (ĐÃ BỎ MAX_DELTA_CM — nó từng chặn cả thay đổi thật, gây lag emergency)

// ─── State LED ───────────────────────────────────────────────
static int s_led_normal;
static int s_led_danger;
static int s_led_emergency;

// ─── State median filter ─────────────────────────────────────
static float s_buf[MEDIAN_WINDOW];
static int   s_buf_idx    = 0;
static int   s_buf_filled = 0;

// ─── State debounce ──────────────────────────────────────────
static water_level_t s_stable_level   = WATER_LEVEL_NORMAL;
static water_level_t s_candidate      = WATER_LEVEL_NORMAL;
static int           s_candidate_cnt  = 0;

// ─── Stats nhiễu ─────────────────────────────────────────────
static uint32_t s_rejected_range = 0;
static uint32_t s_accepted       = 0;

// ─── Helpers ─────────────────────────────────────────────────
static int cmp_float(const void *a, const void *b)
{
    float fa = *(const float *)a;
    float fb = *(const float *)b;
    return (fa > fb) - (fa < fb);
}

static float median_push(float v)
{
    s_buf[s_buf_idx] = v;
    s_buf_idx = (s_buf_idx + 1) % MEDIAN_WINDOW;
    if (s_buf_filled < MEDIAN_WINDOW) s_buf_filled++;

    float sorted[MEDIAN_WINDOW];
    memcpy(sorted, s_buf, s_buf_filled * sizeof(float));
    qsort(sorted, s_buf_filled, sizeof(float), cmp_float);
    return sorted[s_buf_filled / 2];
}

static water_level_t classify_hyst(float d, water_level_t current)
{
    switch (current) {
        case WATER_LEVEL_NORMAL:
            if (d <= DIST_EMERGENCY_CM) return WATER_LEVEL_EMERGENCY;
            if (d <= DIST_DANGER_CM)    return WATER_LEVEL_DANGER;
            return WATER_LEVEL_NORMAL;

        case WATER_LEVEL_DANGER:
            if (d <= DIST_EMERGENCY_CM)               return WATER_LEVEL_EMERGENCY;
            if (d >  DIST_DANGER_CM + HYST_MARGIN_CM) return WATER_LEVEL_NORMAL;
            return WATER_LEVEL_DANGER;

        case WATER_LEVEL_EMERGENCY:
            if (d > DIST_EMERGENCY_CM + HYST_MARGIN_CM) {
                if (d > DIST_DANGER_CM + HYST_MARGIN_CM) return WATER_LEVEL_NORMAL;
                return WATER_LEVEL_DANGER;
            }
            return WATER_LEVEL_EMERGENCY;
    }
    return current;
}

// ─── API gốc ────────────────────────────────────────────────
void water_alarm_init(int led_normal_pin, int led_danger_pin, int led_emergency_pin)
{
    s_led_normal    = led_normal_pin;
    s_led_danger    = led_danger_pin;
    s_led_emergency = led_emergency_pin;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << led_normal_pin)  |
                        (1ULL << led_danger_pin)  |
                        (1ULL << led_emergency_pin),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);

    water_alarm_clear();
    water_alarm_reset_filter();
}

water_level_t water_alarm_classify(float distance_cm)
{
    if (distance_cm <= DIST_EMERGENCY_CM) return WATER_LEVEL_EMERGENCY;
    if (distance_cm <= DIST_DANGER_CM)    return WATER_LEVEL_DANGER;
    return WATER_LEVEL_NORMAL;
}

void water_alarm_update_leds(water_level_t level)
{
    gpio_set_level(s_led_normal,    level == WATER_LEVEL_NORMAL    ? 1 : 0);
    gpio_set_level(s_led_danger,    level == WATER_LEVEL_DANGER    ? 1 : 0);
    gpio_set_level(s_led_emergency, level == WATER_LEVEL_EMERGENCY ? 1 : 0);
}

void water_alarm_clear(void)
{
    gpio_set_level(s_led_normal,    0);
    gpio_set_level(s_led_danger,    0);
    gpio_set_level(s_led_emergency, 0);
}

const char *water_alarm_level_str(water_level_t level)
{
    switch (level) {
        case WATER_LEVEL_NORMAL:    return "BÌNH THƯỜNG";
        case WATER_LEVEL_DANGER:    return "NGUY HIỂM";
        case WATER_LEVEL_EMERGENCY: return "KHẨN CẤP";
        default:                    return "?";
    }
}

void water_alarm_reset_filter(void)
{
    s_buf_idx        = 0;
    s_buf_filled     = 0;
    s_candidate      = s_stable_level;
    s_candidate_cnt  = 0;
    s_rejected_range = 0;
    s_accepted       = 0;
}

water_level_t water_alarm_process(float raw_distance_cm, float *out_smoothed)
{
    // ═══ LỚP 0: HARD RANGE CHECK ═════════════════════════════
    // CHỈ loại giá trị hardware-impossible (sensor lỗi), KHÔNG
    // chặn thay đổi lớn (emergency thật cũng là thay đổi lớn!)
    if (raw_distance_cm < MIN_VALID_DIST_CM || raw_distance_cm > MAX_VALID_DIST_CM) {
        s_rejected_range++;
        if ((s_rejected_range % 10) == 1) {
            ESP_LOGW(TAG, "Sensor garbage: %.1f cm (rejected #%lu)",
                     raw_distance_cm, (unsigned long)s_rejected_range);
        }
        if (out_smoothed) {
            if (s_buf_filled > 0) {
                float sorted[MEDIAN_WINDOW];
                memcpy(sorted, s_buf, s_buf_filled * sizeof(float));
                qsort(sorted, s_buf_filled, sizeof(float), cmp_float);
                *out_smoothed = sorted[s_buf_filled / 2];
            } else {
                *out_smoothed = raw_distance_cm;
            }
        }
        return s_stable_level;
    }

    s_accepted++;

    // ═══ LỚP 1: MEDIAN FILTER ════════════════════════════════
    float smoothed = median_push(raw_distance_cm);
    if (out_smoothed) *out_smoothed = smoothed;

    // ═══ LỚP 2: HYSTERESIS ═══════════════════════════════════
    water_level_t candidate = classify_hyst(smoothed, s_stable_level);

    // ═══ LỚP 3: DEBOUNCE ═════════════════════════════════════
    if (candidate == s_stable_level) {
        s_candidate_cnt = 0;
    } else if (candidate == s_candidate) {
        if (++s_candidate_cnt >= CONFIRM_COUNT) {
            ESP_LOGI(TAG, "Level changed: %s → %s",
                     water_alarm_level_str(s_stable_level),
                     water_alarm_level_str(candidate));
            s_stable_level  = candidate;
            s_candidate_cnt = 0;
        }
    } else {
        s_candidate     = candidate;
        s_candidate_cnt = 1;
    }

    return s_stable_level;
}
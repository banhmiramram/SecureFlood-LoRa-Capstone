#pragma once

#include <stdint.h>

typedef enum {
    WATER_LEVEL_NORMAL    = 0,
    WATER_LEVEL_DANGER    = 1,
    WATER_LEVEL_EMERGENCY = 2,
} water_level_t;

void water_alarm_init(int led_normal_pin,
                      int led_danger_pin,
                      int led_emergency_pin);

// Phân loại trực tiếp (không qua filter) — vẫn giữ cho debug
water_level_t water_alarm_classify(float distance_cm);

void water_alarm_update_leds(water_level_t level);
void water_alarm_clear(void);
const char *water_alarm_level_str(water_level_t level);

// ── MỚI: pipeline đầy đủ chống cảnh báo giả ──────────────────
//
// Đẩy 1 mẫu khoảng cách thô (cm) vào pipeline.
// Pipeline: median filter → hysteresis → debounce
// Trả về mức cảnh báo đã ổn định.
// Nếu out_smoothed != NULL, ghi giá trị đã lọc median vào đó.
water_level_t water_alarm_process(float raw_distance_cm,
                                   float *out_smoothed_cm);

// Reset toàn bộ state filter (gọi khi cảm biến lỗi liên tục)
void water_alarm_reset_filter(void);
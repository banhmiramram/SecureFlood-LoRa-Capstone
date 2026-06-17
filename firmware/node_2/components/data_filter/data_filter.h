#pragma once

#include <stdbool.h>
#include <stdint.h>

// Khởi tạo bộ lọc gửi
void data_filter_init(float    threshold_cm,
                      uint32_t heartbeat_ms,
                      uint32_t min_interval_ms);

// Quyết định có gửi gói LoRa hay không.
// alarm_level: mức cảnh báo hiện tại (0/1/2). 
// Nếu mức đổi → gửi ngay (bypass rate limit).
bool data_filter_should_send(float new_value, int alarm_level);

// Gọi sau khi đã gửi thành công
void data_filter_confirm_sent(float sent_value);
void data_filter_set_last_level(int level);
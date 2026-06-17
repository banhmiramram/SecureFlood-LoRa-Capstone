#pragma once

#include <stdbool.h>

// API cũ — chỉ TRIG và ECHO (dùng cho cảm biến 4 chân)
void hc_sr04_init(int trig_pin, int echo_pin);

// API mới — TRIG + ECHO + OUT (cho cảm biến 5 chân)
void hc_sr04_init_full(int trig_pin, int echo_pin, int out_pin);

// Đo khoảng cách (cm). Trả về -1 nếu timeout.
float hc_sr04_get_distance(void);

// Đọc trạng thái chân OUT (0/1). Trả về -1 nếu OUT chưa cấu hình.
int hc_sr04_read_out(void);

// Wrapper: chân OUT đang active hay không (giả định active HIGH)
bool hc_sr04_alarm_active(void);
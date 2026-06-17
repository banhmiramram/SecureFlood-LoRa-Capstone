#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Khởi tạo WiFi STA + queue uploader.
 * Phải gọi 1 lần ở đầu app_main, sau khi nvs_flash_init.
 */
esp_err_t web_uploader_init(const char *ssid,
                             const char *password,
                             const char *endpoint_url,
                             const char *api_key);

/**
 * Đẩy 1 reading lên web (non-blocking, đẩy vào queue).
 * An toàn để gọi từ task khác (vd lora_parse_task).
 *
 * 4 tham số cuối là thông tin pin/nguồn từ INA219:
 *   voltage      : V, hoặc -1 nếu không có data
 *   battery_pct  : 0–100, hoặc -1 nếu không có data
 *   power_state  : "S+" | "S=" | "B-" | "BF" | "??" hoặc NULL
 *   current_ma   : có dấu (+ = nạp, - = xả), 0 cũng hợp lệ
 *                  truyền 0 khi không có data sẽ vẫn được gửi đi;
 *                  để bỏ qua hãy truyền voltage<0 (server check voltage)
 */
esp_err_t web_uploader_post(const char *node_id,
                             float       distance_cm,
                             int         alarm_level,
                             int         seq,
                             bool        is_error,
                             float       voltage,
                             int         battery_pct,
                             const char *power_state,
                             float       current_ma);

/** Trả về true nếu WiFi đang kết nối */
bool web_uploader_is_connected(void);

#ifdef __cplusplus
}
#endif
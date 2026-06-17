#ifndef INA219_H
#define INA219_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define INA219_DEFAULT_ADDR    0x40
#define INA219_SHUNT_OHMS      0.1f

// ─── Loại pin (chọn 1) ───────────────────────────────────────
// LI_ION:  18650 / 21700 cells, 4.2V đầy → 3.0V cạn
// LIFEPO4: pin sắt 3.65V đầy → 2.5V cạn
#define BAT_TYPE_LI_ION    1
#define BAT_TYPE_LIFEPO4   2

#ifndef BAT_TYPE
#define BAT_TYPE   BAT_TYPE_LI_ION    // <<< đổi tại đây nếu dùng LiFePO4
#endif

// ─── Hướng dây INA219 ────────────────────────────────────────
// Hệ load-sharing: INA giữa BAT+ và +pin.
// Tùy hướng nối Vin+/Vin-, dòng dương có thể là sạc HOẶC xả.
//
// Setup hiện tại: Vin+ ← +pin, Vin- ← BAT+
//   → INA219 đọc DƯƠNG khi dòng từ pin chảy về BAT+ (= xả)
//   → INA219 đọc ÂM khi dòng từ BAT+ chảy vào pin (= sạc)
//
// Code dùng "current_to_pin" với quy ước:
//   DƯƠNG = nạp pin (charging)
//   ÂM    = xả pin (discharging)
//
// Nếu bạn đảo dây Vin+ ↔ Vin-, đổi macro này thành 0:
#define INA219_INVERT_CURRENT   1

// ─── Trạng thái nguồn (load-sharing system) ──────────────────
typedef enum {
    PWR_STATE_UNKNOWN = 0,
    PWR_STATE_SOLAR_CHARGE,    // solar đang cấp ESP32 + dư để sạc pin
    PWR_STATE_SOLAR_BALANCE,   // solar cấp đủ ESP32, không dư
    PWR_STATE_BATTERY,         // pin đang cấp ngược (không/yếu solar)
    PWR_STATE_BATTERY_FULL,    // pin đầy, sạc tự ngắt (vẫn có thể có solar)
} pwr_state_t;

// ─── API ─────────────────────────────────────────────────────

esp_err_t ina219_init(int sda_pin, int scl_pin, uint8_t addr);

float ina219_read_voltage(void);

/**
 * Đọc dòng đã quy về quy ước "to_pin":
 *   > 0  = đang sạc pin
 *   < 0  = đang xả pin (cấp ngược cho ESP32)
 *   ~ 0  = cân bằng
 */
float ina219_read_current_to_pin_ma(void);

int ina219_voltage_to_percent(float voltage);

/**
 * Phân loại trạng thái nguồn (chỉ cần dòng + áp, không cần slope).
 *  - I > +threshold   → SOLAR_CHARGE
 *  - |I| ≤ threshold & V ≥ V_FULL  → BATTERY_FULL
 *  - |I| ≤ threshold  → SOLAR_BALANCE
 *  - I < -threshold   → BATTERY
 */
pwr_state_t ina219_classify_power(float current_to_pin_ma, float voltage);

const char *ina219_pwr_state_str(pwr_state_t s);

bool ina219_read_all(float *out_voltage,
                     float *out_current_to_pin,
                     int   *out_percent,
                     pwr_state_t *out_state);

#ifdef __cplusplus
}
#endif

#endif // INA219_H
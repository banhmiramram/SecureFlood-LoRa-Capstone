#include "ina219.h"

#include <string.h>
#include <math.h>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "ina219"

#define INA219_REG_CONFIG       0x00
#define INA219_REG_BUS_V        0x02
#define INA219_REG_CURRENT      0x04
#define INA219_REG_CALIB        0x05

#define INA219_CONFIG_16V_2A    0x199F
#define INA219_CAL_VAL          4096
#define CURRENT_LSB_MA          0.1f

// ─── Ngưỡng phân loại nguồn ──────────────────────────────────
#define I_THRESHOLD_MA         10.0f   // |I| ≤ 10mA coi như cân bằng

#if BAT_TYPE == BAT_TYPE_LI_ION
  #define V_FULL_THRESHOLD     4.15f
#elif BAT_TYPE == BAT_TYPE_LIFEPO4
  #define V_FULL_THRESHOLD     3.55f
#endif

// ─── State I2C ───────────────────────────────────────────────
static i2c_master_bus_handle_t s_bus    = NULL;
static i2c_master_dev_handle_t s_dev    = NULL;
static bool                    s_inited = false;

static esp_err_t reg_write(uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = { reg, (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 100);
}

static esp_err_t reg_read(uint8_t reg, uint16_t *out)
{
    uint8_t rx[2] = {0};
    esp_err_t err = i2c_master_transmit_receive(s_dev, &reg, 1, rx, 2, 100);
    if (err != ESP_OK) return err;
    *out = ((uint16_t)rx[0] << 8) | rx[1];
    return ESP_OK;
}

esp_err_t ina219_init(int sda_pin, int scl_pin, uint8_t addr)
{
    if (s_inited) return ESP_OK;

    i2c_master_bus_config_t bus_cfg = {
        .clk_source                   = I2C_CLK_SRC_DEFAULT,
        .i2c_port                     = I2C_NUM_0,
        .scl_io_num                   = scl_pin,
        .sda_io_num                   = sda_pin,
        .glitch_ignore_cnt            = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_new_master_bus fail: %s", esp_err_to_name(err));
        return err;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = addr,
        .scl_speed_hz    = 100000,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "add_device fail: %s", esp_err_to_name(err));
        return err;
    }

    err = reg_write(INA219_REG_CONFIG, INA219_CONFIG_16V_2A);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Write config fail: %s", esp_err_to_name(err));
        return err;
    }
    err = reg_write(INA219_REG_CALIB, INA219_CAL_VAL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Write calib fail: %s", esp_err_to_name(err));
        return err;
    }

    uint16_t cfg_read = 0;
    if (reg_read(INA219_REG_CONFIG, &cfg_read) == ESP_OK) {
        ESP_LOGI(TAG, "INA219 init OK @0x%02X (config=0x%04X) [BAT_TYPE=%d, INVERT=%d]",
                 addr, cfg_read, BAT_TYPE, INA219_INVERT_CURRENT);
    }

    s_inited = true;
    vTaskDelay(pdMS_TO_TICKS(50));
    return ESP_OK;
}

float ina219_read_voltage(void)
{
    if (!s_inited) return -1.0f;
    uint16_t raw = 0;
    if (reg_read(INA219_REG_BUS_V, &raw) != ESP_OK) return -1.0f;
    return (raw >> 3) * 0.004f;
}

float ina219_read_current_to_pin_ma(void)
{
    if (!s_inited) return NAN;
    uint16_t raw = 0;
    if (reg_read(INA219_REG_CURRENT, &raw) != ESP_OK) return NAN;
    float raw_ma = (int16_t)raw * CURRENT_LSB_MA;
#if INA219_INVERT_CURRENT
    return -raw_ma;
#else
    return raw_ma;
#endif
}

// ─── Bảng V → % cho 2 loại pin ───────────────────────────────
typedef struct { float v; int p; } bat_point_t;

#if BAT_TYPE == BAT_TYPE_LI_ION
static const bat_point_t BAT_CURVE[] = {
    { 4.20f, 100 }, { 4.10f, 95 }, { 4.00f, 85 }, { 3.90f, 75 },
    { 3.80f, 60 },  { 3.75f, 50 }, { 3.70f, 40 }, { 3.65f, 30 },
    { 3.60f, 20 },  { 3.50f, 10 }, { 3.40f, 5 },  { 3.30f, 2 },
    { 3.00f, 0 },
};
#elif BAT_TYPE == BAT_TYPE_LIFEPO4
// LiFePO4 đường cong RẤT phẳng — plateau ở 3.20-3.30V chiếm ~80% dung lượng
static const bat_point_t BAT_CURVE[] = {
    { 3.65f, 100 }, { 3.40f, 95 }, { 3.35f, 90 }, { 3.32f, 80 },
    { 3.30f, 70 },  { 3.28f, 60 }, { 3.26f, 50 }, { 3.24f, 40 },
    { 3.22f, 30 },  { 3.20f, 20 }, { 3.15f, 10 }, { 3.00f, 5 },
    { 2.80f, 2 },   { 2.50f, 0 },
};
#endif

#define BAT_CURVE_LEN (sizeof(BAT_CURVE) / sizeof(BAT_CURVE[0]))

int ina219_voltage_to_percent(float v)
{
    if (v >= BAT_CURVE[0].v)                 return 100;
    if (v <= BAT_CURVE[BAT_CURVE_LEN-1].v)   return 0;
    for (size_t i = 0; i < BAT_CURVE_LEN - 1; i++) {
        if (v <= BAT_CURVE[i].v && v >= BAT_CURVE[i+1].v) {
            float r = (v - BAT_CURVE[i+1].v) / (BAT_CURVE[i].v - BAT_CURVE[i+1].v);
            int p = BAT_CURVE[i+1].p + (int)roundf(r * (BAT_CURVE[i].p - BAT_CURVE[i+1].p));
            if (p < 0)   p = 0;
            if (p > 100) p = 100;
            return p;
        }
    }
    return 0;
}

// ─── Phân loại nguồn (đơn giản, dựa dấu của dòng) ───────────
pwr_state_t ina219_classify_power(float i_to_pin, float voltage)
{
    if (isnan(i_to_pin)) return PWR_STATE_UNKNOWN;

    if (i_to_pin >= I_THRESHOLD_MA) {
        return PWR_STATE_SOLAR_CHARGE;          // solar dư, đang nạp pin
    }
    if (i_to_pin <= -I_THRESHOLD_MA) {
        return PWR_STATE_BATTERY;               // pin đang xả ngược cấp ESP32
    }
    // |I| ≤ threshold = cân bằng
    if (voltage >= V_FULL_THRESHOLD) {
        return PWR_STATE_BATTERY_FULL;          // pin đầy, sạc ngắt
    }
    return PWR_STATE_SOLAR_BALANCE;             // solar đủ bù tải
}

const char *ina219_pwr_state_str(pwr_state_t s)
{
    switch (s) {
        case PWR_STATE_SOLAR_CHARGE:   return "SOLAR+";
        case PWR_STATE_SOLAR_BALANCE:  return "SOLAR=";
        case PWR_STATE_BATTERY:        return "BATT";
        case PWR_STATE_BATTERY_FULL:   return "FULL";
        default:                       return "UNK";
    }
}

bool ina219_read_all(float *out_v, float *out_ma, int *out_pct, pwr_state_t *out_st)
{
    float v  = ina219_read_voltage();
    float ma = ina219_read_current_to_pin_ma();

    if (v < 0.0f || isnan(ma)) return false;

    if (out_v)   *out_v   = v;
    if (out_ma)  *out_ma  = ma;
    if (out_pct) *out_pct = ina219_voltage_to_percent(v);
    if (out_st)  *out_st  = ina219_classify_power(ma, v);
    return true;
}
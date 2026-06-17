#pragma once

/**
 * @brief Khởi tạo UART + GPIO cho module LoRa AS32.
 */
void lora_init(void);

/**
 * @brief Gửi lệnh cấu hình channel/address/speed cho module.
 *        Gọi sau lora_init().
 */
void lora_config(void);

/**
 * @brief Đặt chân M0/M1 để chuyển mode hoạt động.
 */
void lora_set_mode(int m0, int m1);

void lora_set_node_id(const char *id);

/**
 * @brief Đóng khung và gửi 1 gói tin qua LoRa.
 *
 * Định dạng gói: $<SEQ>,<value>,<CRC>\n
 * Ví dụ:         $042,99.62 cm,B7\n
 *
 * @param value  Chuỗi dữ liệu cần gửi (vd: "99.62 cm", "ERR:TIMEOUT")
 */
void lora_send_packet(const char *value);

void lora_read_config(void);
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "lora_as32_send.h"

#define LORA_ADDR_HIGH  0xBE   // ADDR = 0xBEEF (16-bit)
#define LORA_ADDR_LOW   0xEF
#define LORA_CHAN       0x18
 
// ─── Cấu hình cho ESP32-C3 ──────────────────────────────────
#define UART_PORT   UART_NUM_1
#define TXD         GPIO_NUM_5
#define RXD         GPIO_NUM_4
#define M0          GPIO_NUM_6
#define M1          GPIO_NUM_7

static uint16_t s_seq = 0;
static char     s_node_id[8] = "N0";

// ─── Tính CRC ───────────────────────────────────────────────
static uint8_t calc_crc(const char *str)
{
    uint8_t crc = 0;
    while (*str) crc ^= (uint8_t)(*str++);
    return crc;
}

// ─── Set Node ID ────────────────────────────────────────────
void lora_set_node_id(const char *id)
{
    if (id == NULL) return;
    strncpy(s_node_id, id, sizeof(s_node_id) - 1);
    s_node_id[sizeof(s_node_id) - 1] = '\0';
}

// ─── Mode ────────────────────────────────────────────
void lora_set_mode(int m0, int m1)
{
    gpio_set_level(M0, m0);
    gpio_set_level(M1, m1);
    vTaskDelay(pdMS_TO_TICKS(100));
}

// ─── Init UART + GPIO ───────────────────────────────────────
void lora_init(void)
{
    uart_config_t cfg = {
        .baud_rate  = 9600,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(UART_PORT, 1024, 1024, 0, NULL, 0);
    uart_param_config(UART_PORT, &cfg);
    uart_set_pin(UART_PORT, TXD, RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << M0) | (1ULL << M1),
        .mode         = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);
}

// ─── Cấu hình module LoRa ───────────────────────────────────
void lora_config(void)
{
    lora_set_mode(1, 1);

    uint8_t cfg[6] = {
        0xC0,            // Set parameters command
        LORA_ADDR_HIGH,  // ADDR H
        LORA_ADDR_LOW,   // ADDR L
        0x1A,            // SPED (default speed)
        LORA_CHAN,       // CHAN
        0x44             // OPTION (default)
    };
    uart_write_bytes(UART_PORT, cfg, 6);

    printf("[LoRa] Config gửi xong.\n");
    vTaskDelay(pdMS_TO_TICKS(200));

    lora_set_mode(0, 0);
    printf("[LoRa] Normal mode.\n");
}

// ─── Gửi gói: $<NODE_ID>,<SEQ>,<VALUE>,<CRC>\n ──────────────
void lora_send_packet(const char *value)
{
    char packet[128];
    snprintf(packet, sizeof(packet), "$%s,%03u,%s\n", s_node_id, s_seq, value);
    uart_write_bytes(UART_PORT, packet, strlen(packet));
    s_seq = (s_seq + 1) % 1000;
}

void lora_read_config(void)
{
    lora_set_mode(1, 1);
    uart_flush(UART_PORT);

    uint8_t cmd[] = { 0xC1, 0xC1, 0xC1 };
    uart_write_bytes(UART_PORT, (const char *)cmd, sizeof(cmd));

    vTaskDelay(pdMS_TO_TICKS(100));

    uint8_t resp[6] = {0};
    int n = uart_read_bytes(UART_PORT, resp, 6, pdMS_TO_TICKS(500));

    printf("[N2 LoRa] Read config (n=%d): ", n);
    for (int i = 0; i < n; i++) printf("%02X ", resp[i]);
    printf("\n");

    vTaskDelay(pdMS_TO_TICKS(100));
    lora_set_mode(0, 0);
}
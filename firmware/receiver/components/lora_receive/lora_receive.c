#include "lora_receive.h"

#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "driver/gpio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LORA_ADDR_HIGH  0xBE   // ADDR = 0xBEEF (16-bit)
#define LORA_ADDR_LOW   0xEF
#define LORA_CHAN       0x18  

#define UART_PORT UART_NUM_2

#define TXD GPIO_NUM_5
#define RXD GPIO_NUM_4 

#define M0 GPIO_NUM_18
#define M1 GPIO_NUM_19

void lora_set_mode(int m0, int m1)
{
    gpio_set_level(M0, m0);
    gpio_set_level(M1, m1);

    vTaskDelay(pdMS_TO_TICKS(100));
}

void lora_receive_init(void)
{
    // UART init
    uart_config_t config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_driver_install(
        UART_PORT,
        1024,
        1024,
        0,
        NULL,
        0
    );

    uart_param_config(
        UART_PORT,
        &config
    );

    uart_set_pin(
        UART_PORT,
        TXD,
        RXD,
        UART_PIN_NO_CHANGE,
        UART_PIN_NO_CHANGE
    );

    // GPIO M0 M1
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << M0) | (1ULL << M1),
        .mode = GPIO_MODE_OUTPUT
    };

    gpio_config(&io_conf);
}

void lora_receive_config(void)
{
    // 👉 vào mode config
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

    printf("Receiver config done\n");

    vTaskDelay(pdMS_TO_TICKS(200));

    // 👉 về normal mode
    lora_set_mode(0, 0);

    printf("Receiver normal mode\n");
}

int lora_receive_data(
    uint8_t *data,
    size_t max_len
)
{
    return uart_read_bytes(
        UART_PORT,
        data,
        max_len,
        pdMS_TO_TICKS(1000)
    );
}

void lora_receive_read_config(void)
{
    lora_set_mode(1, 1);
    uart_flush(UART_PORT);

    uint8_t cmd[] = { 0xC1, 0xC1, 0xC1 };
    uart_write_bytes(UART_PORT, (const char *)cmd, sizeof(cmd));

    vTaskDelay(pdMS_TO_TICKS(100));

    uint8_t resp[6] = {0};
    int n = uart_read_bytes(UART_PORT, resp, 6, pdMS_TO_TICKS(500));

    printf("[RX LoRa] Read config (n=%d): ", n);
    for (int i = 0; i < n; i++) printf("%02X ", resp[i]);
    printf("\n");

    vTaskDelay(pdMS_TO_TICKS(100));
    lora_set_mode(0, 0);
}
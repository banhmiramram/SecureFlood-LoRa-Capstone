#ifndef LORA_RECEIVE_H
#define LORA_RECEIVE_H

#include <stdint.h>
#include <stddef.h>

void lora_receive_init(void);

void lora_receive_config(void);

void lora_set_mode(int m0, int m1);

void lora_receive_read_config(void);

int lora_receive_data(
    uint8_t *data,
    size_t max_len
);

#endif
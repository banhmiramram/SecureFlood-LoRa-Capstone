#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MSG_NODE_ID_MAX 8
#define MSG_VALUE_MAX   200

typedef struct {
    char  node_id[MSG_NODE_ID_MAX];
    int   seq;
    char  value[MSG_VALUE_MAX];
    bool  crc_ok;
} lora_msg_t;

// Phân tích 1 gói thô (đã tách \n) → cấu trúc.
// Trả về true nếu CRC hợp lệ.
bool parser_extract(const char *data, int len, lora_msg_t *out);

// API cũ giữ lại cho việc log
void parse_packet(char *data, int len);
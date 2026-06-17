#include "parser_lora.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

bool parser_extract(const char *data, int len, lora_msg_t *out)
{
    if (data == NULL || len <= 4 || data[0] != '$' || out == NULL)
        return false;

    const char *body = data + 1;
    const char *c1   = strchr(body, ',');     if (!c1) return false;
    const char *c2   = strchr(c1 + 1, ',');   if (!c2) return false;

    // Tách NODE_ID
    int node_len = c1 - body;
    if (node_len <= 0 || node_len >= MSG_NODE_ID_MAX) return false;
    memcpy(out->node_id, body, node_len);
    out->node_id[node_len] = '\0';

    // Tách SEQ
    out->seq = atoi(c1 + 1);

    // Tách VALUE — toàn bộ phần còn lại sau c2 (chuỗi base64 encrypted)
    int value_len = (data + len) - (c2 + 1);
    if (value_len <= 0 || value_len >= MSG_VALUE_MAX) return false;
    memcpy(out->value, c2 + 1, value_len);
    out->value[value_len] = '\0';

    return true;
}

// API log (giữ nguyên)
void parse_packet(char *data, int len)
{
    lora_msg_t m;
    if (!parser_extract(data, len, &m)) {
        printf("[Parser] Gói lỗi: %.*s\n", len, data);
        return;
    }
    printf("[Parser] %s SEQ=%03d | %s\n", m.node_id, m.seq, m.value);
}
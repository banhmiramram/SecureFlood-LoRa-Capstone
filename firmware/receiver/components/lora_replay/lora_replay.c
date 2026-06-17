#include "lora_replay.h"
#include "esp_log.h"
#include <string.h>

#define TAG          "lora_replay"
#define MAX_NODES    8
#define WINDOW_SIZE  32

typedef struct {
    char     node_id[16];
    uint32_t window[WINDOW_SIZE];
    int      pos;
    uint32_t last_seq;
    bool     active;
} replay_state_t;

static replay_state_t s_states[MAX_NODES];

void lora_replay_init(void) {
    memset(s_states, 0, sizeof(s_states));
    ESP_LOGI(TAG, "Replay protection initialized (%d nodes × %d window)",
             MAX_NODES, WINDOW_SIZE);
}

static replay_state_t *find_or_create(const char *node_id) {
    // Tìm node đã có
    for (int i = 0; i < MAX_NODES; i++) {
        if (s_states[i].active && strcmp(s_states[i].node_id, node_id) == 0) {
            return &s_states[i];
        }
    }
    // Cấp slot mới
    for (int i = 0; i < MAX_NODES; i++) {
        if (!s_states[i].active) {
            strncpy(s_states[i].node_id, node_id, sizeof(s_states[i].node_id) - 1);
            s_states[i].active = true;
            s_states[i].pos = 0;
            s_states[i].last_seq = 0;
            memset(s_states[i].window, 0xFF, sizeof(s_states[i].window));
            ESP_LOGI(TAG, "New node registered: %s", node_id);
            return &s_states[i];
        }
    }
    return NULL;
}

bool lora_replay_check(const char *node_id, uint32_t seq) {
    replay_state_t *st = find_or_create(node_id);
    if (!st) return false;

    // Check duplicate trong window
    for (int i = 0; i < WINDOW_SIZE; i++) {
        if (st->window[i] == seq) {
            ESP_LOGW(TAG, "[%s] REPLAY DETECTED: seq=%lu (đã thấy)", 
                     node_id, (unsigned long)seq);
            return false;
        }
    }

    // Phát hiện reboot: seq quá nhỏ so với last_seq
    if (st->last_seq > 500 && seq < 100) {
        ESP_LOGI(TAG, "[%s] Node reboot detected (seq %lu → %lu), resetting window",
                 node_id, (unsigned long)st->last_seq, (unsigned long)seq);
        memset(st->window, 0xFF, sizeof(st->window));
        st->pos = 0;
        st->last_seq = 0;
    }

    // Chấp nhận, thêm vào window
    st->window[st->pos] = seq;
    st->pos = (st->pos + 1) % WINDOW_SIZE;
    if (seq > st->last_seq) st->last_seq = seq;
    return true;
}

void lora_replay_reset(const char *node_id) {
    for (int i = 0; i < MAX_NODES; i++) {
        if (s_states[i].active && strcmp(s_states[i].node_id, node_id) == 0) {
            memset(s_states[i].window, 0xFF, sizeof(s_states[i].window));
            s_states[i].pos = 0;
            s_states[i].last_seq = 0;
        }
    }
}
#pragma once
#include <stdint.h>
#include <stdbool.h>

void lora_replay_init(void);

// Kiểm tra seq từ node có hợp lệ không (không phải replay)
// Trả true: chấp nhận, đã thêm vào window
// Trả false: replay attack, từ chối
bool lora_replay_check(const char *node_id, uint32_t seq);

// Reset state (vd khi node reboot)
void lora_replay_reset(const char *node_id);
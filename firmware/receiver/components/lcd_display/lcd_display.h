#pragma once

#include <stdint.h>
#include <stdbool.h>

// ─── Màu RGB565 ──────────────────────────────────────────────
#define LCD_BLACK    0x0000
#define LCD_WHITE    0xFFFF
#define LCD_RED      0xF800
#define LCD_GREEN    0x07E0
#define LCD_BLUE     0x001F
#define LCD_YELLOW   0xFFE0
#define LCD_ORANGE   0xFC00
#define LCD_GRAY     0x8410
#define LCD_DARKGRAY 0x4208

#define LCD_W 240
#define LCD_H 320

// ─── Cấu hình lịch sử ────────────────────────────────────────
#define LCD_HISTORY_SIZE   30      // số mẫu hiển thị trên chart
#define LCD_GAUGE_MAX_CM   100.0f  // dải đo tối đa của gauge

// ─── API mức thấp ────────────────────────────────────────────
void lcd_init(void);
void lcd_clear(uint16_t color);
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color);
void lcd_draw_hline(int x, int y, int w, uint16_t color);
void lcd_draw_vline(int x, int y, int h, uint16_t color);
void lcd_draw_rect(int x, int y, int w, int h, uint16_t color);
void lcd_draw_string(int x, int y, const char *s,
                     uint16_t fg, uint16_t bg, uint8_t scale);

// ─── Lịch sử ─────────────────────────────────────────────────
void lcd_history_push(float distance_cm);
void lcd_history_reset(void);

// ─── API mức cao ─────────────────────────────────────────────
typedef enum {
    LCD_STATUS_NORMAL    = 0,
    LCD_STATUS_DANGER    = 1,
    LCD_STATUS_EMERGENCY = 2,
} lcd_status_t;

typedef struct {
    const char  *id;
    lcd_status_t status;
    float        distance_cm;
    int          seq;
    bool         is_error;
    int          age_seconds;     // thời gian từ lần update cuối
} lcd_node_info_t;

void lcd_show_multi_node(const lcd_node_info_t *nodes, int count);

// History theo node
void lcd_history_push_for(const char *node_id, float distance_cm);

void lcd_show_dashboard(lcd_status_t status, float distance_cm, int seq);
void lcd_show_error(const char *msg);
void lcd_show_boot_screen(void);
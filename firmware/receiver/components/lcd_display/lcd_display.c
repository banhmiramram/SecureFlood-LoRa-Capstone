#include "lcd_display.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ─── Pin & SPI ───────────────────────────────────────────────
#define LCD_HOST       SPI2_HOST
#define PIN_CS         15
#define PIN_DC         27
#define PIN_RST        26
#define PIN_MOSI       13
#define PIN_SCK        14
#define PIN_BL         21
#define LCD_SPI_HZ     (26 * 1000 * 1000)

static spi_device_handle_t s_spi;

#define TX_BUF_PIX 256
static uint16_t s_tx[TX_BUF_PIX];

static void IRAM_ATTR pre_cb(spi_transaction_t *t)
{
    gpio_set_level(PIN_DC, (int)(uintptr_t)t->user);
}

static void send_cmd(uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8, .tx_buffer = &cmd, .user = (void *)0,
    };
    spi_device_polling_transmit(s_spi, &t);
}

static void send_data(const uint8_t *data, int len)
{
    if (len <= 0) return;
    spi_transaction_t t = {
        .length = len * 8, .tx_buffer = data, .user = (void *)1,
    };
    spi_device_polling_transmit(s_spi, &t);
}

// ─── ILI9341 init ────────────────────────────────────────────
typedef struct { uint8_t cmd; uint8_t data[16]; uint8_t n; } init_cmd_t;

static const init_cmd_t INIT_SEQ[] = {
    {0xCF, {0x00, 0x83, 0x30}, 3},
    {0xED, {0x64, 0x03, 0x12, 0x81}, 4},
    {0xE8, {0x85, 0x01, 0x79}, 3},
    {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},
    {0xF7, {0x20}, 1},
    {0xEA, {0x00, 0x00}, 2},
    {0xC0, {0x26}, 1},
    {0xC1, {0x11}, 1},
    {0xC5, {0x35, 0x3E}, 2},
    {0xC7, {0xBE}, 1},
    {0x36, {0xA0}, 1},          // MADCTL: portrait, RGB, MY flip
    {0x3A, {0x55}, 1},
    {0xB1, {0x00, 0x1B}, 2},
    {0xF2, {0x08}, 1},
    {0x26, {0x01}, 1},
    {0xB7, {0x07}, 1},
    {0xB6, {0x0A, 0x82, 0x27, 0x00}, 4},
    {0x11, {0}, 0x80},
    {0x29, {0}, 0x80},
    {0x00, {0}, 0xFF},
};

static void set_window(int x0, int y0, int x1, int y1)
{
    uint8_t b[4];
    send_cmd(0x2A);
    b[0] = x0 >> 8; b[1] = x0 & 0xFF;
    b[2] = x1 >> 8; b[3] = x1 & 0xFF;
    send_data(b, 4);

    send_cmd(0x2B);
    b[0] = y0 >> 8; b[1] = y0 & 0xFF;
    b[2] = y1 >> 8; b[3] = y1 & 0xFF;
    send_data(b, 4);

    send_cmd(0x2C);
}

// ─── Init ────────────────────────────────────────────────────
void lcd_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RST) | (1ULL << PIN_BL),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&io);

    spi_bus_config_t bus = {
        .mosi_io_num = PIN_MOSI, .miso_io_num = -1, .sclk_io_num = PIN_SCK,
        .quadwp_io_num = -1, .quadhd_io_num = -1,
        .max_transfer_sz = LCD_W * LCD_H * 2 + 8,
    };
    spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t dev = {
        .clock_speed_hz = LCD_SPI_HZ, .mode = 0, .spics_io_num = PIN_CS,
        .queue_size = 7, .pre_cb = pre_cb,
    };
    spi_bus_add_device(LCD_HOST, &dev, &s_spi);

    gpio_set_level(PIN_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 1); vTaskDelay(pdMS_TO_TICKS(120));

    for (int i = 0; INIT_SEQ[i].n != 0xFF; i++) {
        send_cmd(INIT_SEQ[i].cmd);
        if (INIT_SEQ[i].n & 0x1F)
            send_data(INIT_SEQ[i].data, INIT_SEQ[i].n & 0x1F);
        if (INIT_SEQ[i].n & 0x80)
            vTaskDelay(pdMS_TO_TICKS(120));
    }

    gpio_set_level(PIN_BL, 1);
    lcd_clear(LCD_BLACK);
}

// ─── Drawing primitives ──────────────────────────────────────
void lcd_fill_rect(int x, int y, int w, int h, uint16_t color)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    if (w <= 0 || h <= 0) return;

    uint16_t cbe = (color >> 8) | (color << 8);
    for (int i = 0; i < TX_BUF_PIX; i++) s_tx[i] = cbe;

    set_window(x, y, x + w - 1, y + h - 1);
    int total = w * h;
    while (total > 0) {
        int chunk = total > TX_BUF_PIX ? TX_BUF_PIX : total;
        send_data((uint8_t *)s_tx, chunk * 2);
        total -= chunk;
    }
}

void lcd_clear(uint16_t color)        { lcd_fill_rect(0, 0, LCD_W, LCD_H, color); }
void lcd_draw_hline(int x, int y, int w, uint16_t c) { lcd_fill_rect(x, y, w, 1, c); }
void lcd_draw_vline(int x, int y, int h, uint16_t c) { lcd_fill_rect(x, y, 1, h, c); }

void lcd_draw_rect(int x, int y, int w, int h, uint16_t c)
{
    lcd_draw_hline(x, y,         w, c);
    lcd_draw_hline(x, y + h - 1, w, c);
    lcd_draw_vline(x,         y, h, c);
    lcd_draw_vline(x + w - 1, y, h, c);
}

// ─── Font 8x8 ────────────────────────────────────────────────
typedef struct { char c; uint8_t bm[8]; } glyph_t;
static const glyph_t GLYPHS[] = {
    {' ', {0,0,0,0,0,0,0,0}},
    {'.', {0,0,0,0,0,0,0x18,0x18}},
    {':', {0,0x18,0x18,0,0x18,0x18,0,0}},
    {'-', {0,0,0,0x7E,0,0,0,0}},
    {'/', {0x02,0x06,0x0C,0x18,0x30,0x60,0xC0,0x80}},
    {'!', {0x18,0x18,0x18,0x18,0,0,0x18,0}},
    {'0', {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0}},
    {'1', {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0}},
    {'2', {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0}},
    {'3', {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0}},
    {'4', {0x0C,0x1C,0x2C,0x4C,0x7E,0x0C,0x0C,0}},
    {'5', {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0}},
    {'6', {0x3C,0x66,0x60,0x7C,0x66,0x66,0x3C,0}},
    {'7', {0x7E,0x06,0x0C,0x18,0x30,0x30,0x30,0}},
    {'8', {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0}},
    {'9', {0x3C,0x66,0x66,0x3E,0x06,0x66,0x3C,0}},
    {'A', {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0}},
    {'B', {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0}},
    {'C', {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0}},
    {'D', {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0}},
    {'E', {0x7E,0x60,0x60,0x7C,0x60,0x60,0x7E,0}},
    {'F', {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0}},
    {'G', {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3C,0}},
    {'H', {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0}},
    {'I', {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0}},
    {'J', {0x06,0x06,0x06,0x06,0x06,0x66,0x3C,0}},
    {'K', {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0}},
    {'L', {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0}},
    {'M', {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0}},
    {'N', {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0}},
    {'O', {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0}},
    {'P', {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0}},
    {'Q', {0x3C,0x66,0x66,0x66,0x66,0x3C,0x0E,0}},
    {'R', {0x7C,0x66,0x66,0x7C,0x78,0x6C,0x66,0}},
    {'S', {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0}},
    {'T', {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0}},
    {'U', {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0}},
    {'V', {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0}},
    {'W', {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0}},
    {'X', {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0}},
    {'Y', {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0}},
    {'Z', {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0}},
};
#define N_GLYPHS (sizeof(GLYPHS) / sizeof(GLYPHS[0]))

static const uint8_t *find_glyph(char c)
{
    if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
    for (int i = 0; i < (int)N_GLYPHS; i++)
        if (GLYPHS[i].c == c) return GLYPHS[i].bm;
    return GLYPHS[0].bm;
}

static void draw_char(int x, int y, char c, uint16_t fg, uint16_t bg, uint8_t scale)
{
    const uint8_t *g = find_glyph(c);
    int w = 8 * scale;
    uint16_t fg_be = (fg >> 8) | (fg << 8);
    uint16_t bg_be = (bg >> 8) | (bg << 8);

    set_window(x, y, x + w - 1, y + 8 * scale - 1);

    for (int row = 0; row < 8; row++) {
        uint8_t bits = g[row];
        uint16_t *p = s_tx;
        for (int col = 0; col < 8; col++) {
            uint16_t color = (bits & (0x80 >> col)) ? fg_be : bg_be;
            for (int sx = 0; sx < scale; sx++) *p++ = color;
        }
        for (int sy = 0; sy < scale; sy++)
            send_data((uint8_t *)s_tx, w * 2);
    }
}

void lcd_draw_string(int x, int y, const char *s,
                     uint16_t fg, uint16_t bg, uint8_t scale)
{
    while (*s) {
        draw_char(x, y, *s, fg, bg, scale);
        x += 8 * scale;
        s++;
    }
}

// ─── Lịch sử (circular buffer) ───────────────────────────────
static float s_hist[LCD_HISTORY_SIZE];
static int   s_hist_count = 0;
static int   s_hist_head  = 0;

void lcd_history_push(float v)
{
    s_hist[s_hist_head] = v;
    s_hist_head = (s_hist_head + 1) % LCD_HISTORY_SIZE;
    if (s_hist_count < LCD_HISTORY_SIZE) s_hist_count++;
}

void lcd_history_reset(void)
{
    s_hist_count = 0;
    s_hist_head  = 0;
}

// Lấy mẫu thứ i (0 = cũ nhất, count-1 = mới nhất)
static float hist_at(int i)
{
    int start = (s_hist_count < LCD_HISTORY_SIZE) ? 0 : s_hist_head;
    return s_hist[(start + i) % LCD_HISTORY_SIZE];
}

// Tính xu hướng: so sánh mẫu mới nhất với mẫu cách 3 vị trí
typedef enum { TREND_FLAT, TREND_UP, TREND_DOWN } trend_t;

static trend_t calc_trend(void)
{
    if (s_hist_count < 4) return TREND_FLAT;
    float now  = hist_at(s_hist_count - 1);
    float past = hist_at(s_hist_count - 4);
    float diff = past - now;          // distance giảm = nước dâng
    if (diff >  1.5f) return TREND_UP;
    if (diff < -1.5f) return TREND_DOWN;
    return TREND_FLAT;
}

// ─── Mũi tên xu hướng (16x16 bitmap) ─────────────────────────
static const uint8_t ARROW_UP[32] = {
    0x01,0x80, 0x03,0xC0, 0x07,0xE0, 0x0F,0xF0,
    0x1F,0xF8, 0x3F,0xFC, 0x7F,0xFE, 0xFF,0xFF,
    0x07,0xE0, 0x07,0xE0, 0x07,0xE0, 0x07,0xE0,
    0x07,0xE0, 0x07,0xE0, 0x07,0xE0, 0x07,0xE0,
};
static const uint8_t ARROW_DOWN[32] = {
    0x07,0xE0, 0x07,0xE0, 0x07,0xE0, 0x07,0xE0,
    0x07,0xE0, 0x07,0xE0, 0x07,0xE0, 0x07,0xE0,
    0xFF,0xFF, 0x7F,0xFE, 0x3F,0xFC, 0x1F,0xF8,
    0x0F,0xF0, 0x07,0xE0, 0x03,0xC0, 0x01,0x80,
};
static const uint8_t ARROW_FLAT[32] = {
    0,0, 0,0, 0,0, 0x00,0x60, 0x00,0x70, 0x00,0x78,
    0x00,0x7C, 0xFF,0xFE, 0xFF,0xFE, 0x00,0x7C,
    0x00,0x78, 0x00,0x70, 0x00,0x60, 0,0, 0,0, 0,0,
};

static void draw_bitmap(int x, int y, int w, int h,
                        const uint8_t *bm, uint16_t fg, uint16_t bg)
{
    int bytes_per_row = (w + 7) / 8;
    uint16_t fg_be = (fg >> 8) | (fg << 8);
    uint16_t bg_be = (bg >> 8) | (bg << 8);

    set_window(x, y, x + w - 1, y + h - 1);

    for (int row = 0; row < h; row++) {
        uint16_t *p = s_tx;
        for (int col = 0; col < w; col++) {
            int byte_idx = row * bytes_per_row + col / 8;
            int bit_idx  = 7 - (col % 8);
            *p++ = (bm[byte_idx] & (1 << bit_idx)) ? fg_be : bg_be;
        }
        send_data((uint8_t *)s_tx, w * 2);
    }
}

// ─── Gauge dọc ───────────────────────────────────────────────
static void draw_gauge(int x, int y, int w, int h, float dist_cm, uint16_t fill_color)
{
    float ratio = 1.0f - (dist_cm / LCD_GAUGE_MAX_CM);
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;

    int inner_h = h - 2;
    int fill_h  = (int)(inner_h * ratio);
    int empty_h = inner_h - fill_h;

    if (empty_h > 0) lcd_fill_rect(x + 1, y + 1,             w - 2, empty_h, LCD_DARKGRAY);
    if (fill_h  > 0) lcd_fill_rect(x + 1, y + 1 + empty_h,   w - 2, fill_h,  fill_color);

    lcd_draw_rect(x, y, w, h, LCD_WHITE);

    // Vạch ngưỡng (nhỏ ở bên trái)
    int mark_50 = y + (int)(inner_h * 50.0f / LCD_GAUGE_MAX_CM);
    int mark_85 = y + (int)(inner_h * 85.0f / LCD_GAUGE_MAX_CM);
    lcd_fill_rect(x - 6, mark_50, 5, 2, LCD_RED);     // ngưỡng EMERGENCY
    lcd_fill_rect(x - 6, mark_85, 5, 2, LCD_YELLOW);  // ngưỡng cảnh báo
}

// ─── Mini chart lịch sử ──────────────────────────────────────
static void draw_chart(int x, int y, int w, int h)
{
    lcd_fill_rect(x, y, w, h, LCD_BLACK);
    lcd_draw_rect(x, y, w, h, LCD_DARKGRAY);

    if (s_hist_count < 1) return;

    int chart_w = w - 4;
    int chart_h = h - 4;
    int origin_x = x + 2;
    int origin_y = y + 2;

    int bar_w = chart_w / LCD_HISTORY_SIZE;
    if (bar_w < 1) bar_w = 1;

    for (int i = 0; i < s_hist_count; i++) {
        float v = hist_at(i);
        if (v <= 0 || v > LCD_GAUGE_MAX_CM * 2) continue;

        float water = 1.0f - (v / LCD_GAUGE_MAX_CM);
        if (water < 0) water = 0;
        if (water > 1) water = 1;

        int bar_h = (int)(chart_h * water);
        if (bar_h < 1) bar_h = 1;

        int bx = origin_x + i * bar_w;
        int by = origin_y + chart_h - bar_h;

        uint16_t color = (v <= 50.0f) ? LCD_RED
                       : (v <= 85.0f) ? LCD_ORANGE
                                      : LCD_GREEN;
        lcd_fill_rect(bx, by, bar_w - 1 > 0 ? bar_w - 1 : 1, bar_h, color);
    }
}

// ─── Lịch sử theo từng node ──────────────────────────────────
typedef struct {
    char   id[8];
    bool   used;
    float  data[LCD_HISTORY_SIZE];
    int    count;
    int    head;
} node_hist_t;

#define LCD_MAX_HIST_NODES 4
static node_hist_t s_node_hist[LCD_MAX_HIST_NODES];

static node_hist_t *find_or_create_hist(const char *id)
{
    for (int i = 0; i < LCD_MAX_HIST_NODES; i++)
        if (s_node_hist[i].used && strcmp(s_node_hist[i].id, id) == 0)
            return &s_node_hist[i];
    for (int i = 0; i < LCD_MAX_HIST_NODES; i++)
        if (!s_node_hist[i].used) {
            strncpy(s_node_hist[i].id, id, sizeof(s_node_hist[i].id) - 1);
            s_node_hist[i].used = true;
            return &s_node_hist[i];
        }
    return NULL;
}

void lcd_history_push_for(const char *node_id, float v)
{
    node_hist_t *h = find_or_create_hist(node_id);
    if (!h) return;
    h->data[h->head] = v;
    h->head = (h->head + 1) % LCD_HISTORY_SIZE;
    if (h->count < LCD_HISTORY_SIZE) h->count++;
}

static float hist_get_for(node_hist_t *h, int i)
{
    int start = (h->count < LCD_HISTORY_SIZE) ? 0 : h->head;
    return h->data[(start + i) % LCD_HISTORY_SIZE];
}

// ─── Cache state cho selective redraw (chống flicker) ─────────
typedef struct {
    bool          used;
    char          id[8];
    lcd_status_t  status;
    float         distance_cm;
    int           seq;
    bool          is_error;
    int           age_seconds;
    int           panel_y;
    int           panel_h;
    int           hist_count_drawn;
    int           gauge_fill_h;    // pixels đã fill cho gauge
} panel_cache_t;

static panel_cache_t s_panel_cache[LCD_MAX_HIST_NODES];
static bool          s_panels_inited = false;
static int           s_last_node_count = 0;

// Đệm string ra cùng độ rộng để text mới luôn đè hết text cũ
static void pad_to_width(char *buf, int width)
{
    int len = strlen(buf);
    while (len < width && len < 30) buf[len++] = ' ';
    buf[len] = '\0';
}

// ─── Vẽ FULL panel cho 1 node (lần đầu hoặc layout đổi) ──────
static void draw_node_panel_full(int y, int height, const lcd_node_info_t *n,
                                   panel_cache_t *cache)
{
    uint16_t color;
    const char *label;
    switch (n->status) {
        case LCD_STATUS_DANGER:    color = LCD_ORANGE; label = "DANGER";    break;
        case LCD_STATUS_EMERGENCY: color = LCD_RED;    label = "EMERGENCY"; break;
        default:                   color = LCD_GREEN;  label = "NORMAL";    break;
    }

    // Clear toàn bộ panel (CHỈ lần đầu)
    lcd_fill_rect(0, y, LCD_W, height, LCD_BLACK);

    // ─ Sub-header (20px): ID + SEQ + AGE ─
    const int SH_H = 20;
    lcd_fill_rect(0, y, LCD_W, SH_H, LCD_DARKGRAY);

    char buf[32];
    lcd_draw_string(8, y + 6, n->id, LCD_WHITE, LCD_DARKGRAY, 1);

    snprintf(buf, sizeof(buf), "SEQ:%03d", n->seq);
    lcd_draw_string(60, y + 6, buf, LCD_GRAY, LCD_DARKGRAY, 1);

    // AGE: pad đến 9 ký tự để width cố định
    snprintf(buf, sizeof(buf), "%4dS AGO", n->age_seconds);
    lcd_draw_string(LCD_W - 9*8 - 4, y + 6, buf, LCD_GRAY, LCD_DARKGRAY, 1);

    // ─ Body region ─
    const int CHART_H = (height >= 120) ? 28 : (height >= 80) ? 18 : 0;
    const int GAP = 4;
    int body_y = y + SH_H + GAP;
    int body_h = height - SH_H - CHART_H - GAP * 2;
    if (body_h < 30) body_h = 30;

    // ─ Gauge ─
    const int GAUGE_X = 12;
    const int GAUGE_W = 32;
    float ratio = 1.0f - (n->distance_cm / LCD_GAUGE_MAX_CM);
    if (ratio < 0) ratio = 0;
    if (ratio > 1) ratio = 1;
    int inner_h = body_h - 2;
    int fill_h  = (int)(inner_h * ratio);
    int empty_h = inner_h - fill_h;
    if (empty_h > 0)
        lcd_fill_rect(GAUGE_X + 1, body_y + 1, GAUGE_W - 2, empty_h, LCD_DARKGRAY);
    if (fill_h > 0)
        lcd_fill_rect(GAUGE_X + 1, body_y + 1 + empty_h, GAUGE_W - 2, fill_h, color);
    lcd_draw_rect(GAUGE_X, body_y, GAUGE_W, body_h, LCD_WHITE);
    cache->gauge_fill_h = fill_h;

    // ─ Status + value: pad fixed width ─
    int text_x = GAUGE_X + GAUGE_W + 14;
    if (n->is_error) {
        lcd_draw_string(text_x, body_y + (body_h - 16) / 2, "TIMEOUT  ",
                        LCD_RED, LCD_BLACK, 2);
    } else {
        // Status pad 9 chars (max "EMERGENCY")
        char status_buf[16];
        snprintf(status_buf, sizeof(status_buf), "%s", label);
        pad_to_width(status_buf, 9);
        lcd_draw_string(text_x, body_y + 8, status_buf, color, LCD_BLACK, 2);

        // Distance pad 8 chars (max "999.9 CM")
        snprintf(buf, sizeof(buf), "%5.1f CM", n->distance_cm);
        lcd_draw_string(text_x, body_y + body_h - 24, buf, LCD_WHITE, LCD_BLACK, 2);
    }

    // ─ Chart frame + tất cả bar hiện có ─
    if (CHART_H > 0) {
        int chart_y = y + height - CHART_H;
        int chart_x = 12;
        int chart_w = LCD_W - 24;

        lcd_fill_rect(chart_x, chart_y, chart_w, CHART_H, LCD_BLACK);
        lcd_draw_rect(chart_x, chart_y, chart_w, CHART_H, LCD_DARKGRAY);

        node_hist_t *h = find_or_create_hist(n->id);
        if (h && h->count >= 1) {
            int bar_w = (chart_w - 2) / LCD_HISTORY_SIZE;
            if (bar_w < 1) bar_w = 1;
            for (int i = 0; i < h->count; i++) {
                float v = hist_get_for(h, i);
                if (v <= 0 || v > LCD_GAUGE_MAX_CM * 2) continue;
                float water = 1.0f - (v / LCD_GAUGE_MAX_CM);
                if (water < 0) water = 0;
                if (water > 1) water = 1;
                int bh_bar = (int)((CHART_H - 2) * water);
                if (bh_bar < 1) bh_bar = 1;
                int bx = chart_x + 1 + i * bar_w;
                int by_bar = chart_y + CHART_H - 1 - bh_bar;
                uint16_t bc = (v <= 50) ? LCD_RED
                            : (v <= 85) ? LCD_ORANGE
                                        : LCD_GREEN;
                lcd_fill_rect(bx, by_bar, bar_w, bh_bar, bc);
            }
            cache->hist_count_drawn = h->count;
        } else {
            cache->hist_count_drawn = 0;
        }
    }
}

// ─── Update chỉ những gì THAY ĐỔI (không flicker) ─────────────
static void draw_node_panel_update(int y, int height, const lcd_node_info_t *n,
                                     panel_cache_t *cache)
{
    uint16_t color;
    const char *label;
    switch (n->status) {
        case LCD_STATUS_DANGER:    color = LCD_ORANGE; label = "DANGER";    break;
        case LCD_STATUS_EMERGENCY: color = LCD_RED;    label = "EMERGENCY"; break;
        default:                   color = LCD_GREEN;  label = "NORMAL";    break;
    }

    char buf[32];
    const int SH_H = 20;

    // SEQ chỉ update khi đổi
    if (cache->seq != n->seq) {
        snprintf(buf, sizeof(buf), "SEQ:%03d", n->seq);
        lcd_draw_string(60, y + 6, buf, LCD_GRAY, LCD_DARKGRAY, 1);
    }

    // AGE chỉ update khi đổi (thường update mỗi giây)
    if (cache->age_seconds != n->age_seconds) {
        snprintf(buf, sizeof(buf), "%4dS AGO", n->age_seconds);
        lcd_draw_string(LCD_W - 9*8 - 4, y + 6, buf, LCD_GRAY, LCD_DARKGRAY, 1);
    }

    // ─ Body region ─
    const int CHART_H = (height >= 120) ? 28 : (height >= 80) ? 18 : 0;
    const int GAP = 4;
    int body_y = y + SH_H + GAP;
    int body_h = height - SH_H - CHART_H - GAP * 2;
    if (body_h < 30) body_h = 30;

    // Gauge fill — chỉ update khi distance/status đổi
    bool gauge_dirty = (cache->distance_cm != n->distance_cm) ||
                        (cache->status != n->status);
    if (gauge_dirty) {
        const int GAUGE_X = 12;
        const int GAUGE_W = 32;
        float ratio = 1.0f - (n->distance_cm / LCD_GAUGE_MAX_CM);
        if (ratio < 0) ratio = 0;
        if (ratio > 1) ratio = 1;
        int inner_h = body_h - 2;
        int fill_h  = (int)(inner_h * ratio);
        int empty_h = inner_h - fill_h;
        if (empty_h > 0)
            lcd_fill_rect(GAUGE_X + 1, body_y + 1, GAUGE_W - 2, empty_h, LCD_DARKGRAY);
        if (fill_h > 0)
            lcd_fill_rect(GAUGE_X + 1, body_y + 1 + empty_h, GAUGE_W - 2, fill_h, color);
        cache->gauge_fill_h = fill_h;
    }

    // Status text — update khi status hoặc error đổi
    int text_x = 12 + 32 + 14;  // GAUGE_X + GAUGE_W + 14
    bool status_dirty = (cache->status != n->status) || 
                         (cache->is_error != n->is_error);
    if (status_dirty) {
        if (n->is_error) {
            lcd_draw_string(text_x, body_y + 8, "TIMEOUT  ",
                            LCD_RED, LCD_BLACK, 2);
        } else {
            char status_buf[16];
            snprintf(status_buf, sizeof(status_buf), "%s", label);
            pad_to_width(status_buf, 9);
            lcd_draw_string(text_x, body_y + 8, status_buf, color, LCD_BLACK, 2);
        }
    }

    // Distance value — update khi distance hoặc error đổi
    bool dist_dirty = (cache->distance_cm != n->distance_cm) || 
                       (cache->is_error != n->is_error);
    if (dist_dirty && !n->is_error) {
        snprintf(buf, sizeof(buf), "%5.1f CM", n->distance_cm);
        lcd_draw_string(text_x, body_y + body_h - 24, buf, LCD_WHITE, LCD_BLACK, 2);
    }

    // Chart — chỉ thêm bar mới khi history tăng
    if (CHART_H > 0) {
        node_hist_t *h = find_or_create_hist(n->id);
        if (h && h->count != cache->hist_count_drawn) {
            int chart_y = y + height - CHART_H;
            int chart_x = 12;
            int chart_w = LCD_W - 24;
            int bar_w = (chart_w - 2) / LCD_HISTORY_SIZE;
            if (bar_w < 1) bar_w = 1;

            if (h->count >= LCD_HISTORY_SIZE) {
                // Circular buffer wrap → full redraw chart interior
                lcd_fill_rect(chart_x + 1, chart_y + 1, chart_w - 2, CHART_H - 2, LCD_BLACK);
                for (int j = 0; j < h->count; j++) {
                    float v = hist_get_for(h, j);
                    if (v <= 0 || v > LCD_GAUGE_MAX_CM * 2) continue;
                    float water = 1.0f - (v / LCD_GAUGE_MAX_CM);
                    if (water < 0) water = 0;
                    if (water > 1) water = 1;
                    int bh_bar = (int)((CHART_H - 2) * water);
                    if (bh_bar < 1) bh_bar = 1;
                    int bx = chart_x + 1 + j * bar_w;
                    int by_bar = chart_y + CHART_H - 1 - bh_bar;
                    uint16_t bc = (v <= 50) ? LCD_RED
                                : (v <= 85) ? LCD_ORANGE
                                            : LCD_GREEN;
                    lcd_fill_rect(bx, by_bar, bar_w, bh_bar, bc);
                }
            } else {
                // Chỉ vẽ bar mới
                for (int j = cache->hist_count_drawn; j < h->count; j++) {
                    float v = hist_get_for(h, j);
                    if (v <= 0 || v > LCD_GAUGE_MAX_CM * 2) continue;
                    float water = 1.0f - (v / LCD_GAUGE_MAX_CM);
                    if (water < 0) water = 0;
                    if (water > 1) water = 1;
                    int bh_bar = (int)((CHART_H - 2) * water);
                    if (bh_bar < 1) bh_bar = 1;
                    int bx = chart_x + 1 + j * bar_w;
                    int by_bar = chart_y + CHART_H - 1 - bh_bar;
                    // Clear column trước rồi vẽ bar
                    lcd_fill_rect(bx, chart_y + 1, bar_w, CHART_H - 2, LCD_BLACK);
                    uint16_t bc = (v <= 50) ? LCD_RED
                                : (v <= 85) ? LCD_ORANGE
                                            : LCD_GREEN;
                    lcd_fill_rect(bx, by_bar, bar_w, bh_bar, bc);
                }
            }
            cache->hist_count_drawn = h->count;
        }
    }
}

// Invalidate cache (gọi khi đổi màn hình, error, boot...)
void lcd_invalidate(void)
{
    s_panels_inited = false;
    s_last_node_count = 0;
    for (int i = 0; i < LCD_MAX_HIST_NODES; i++) {
        s_panel_cache[i].used = false;
    }
}

// ─── Hiển thị nhiều node (với selective redraw) ───────────────
void lcd_show_multi_node(const lcd_node_info_t *nodes, int count)
{
    if (count <= 0) return;

    // Phát hiện layout change → cần full redraw
    bool full_redraw = !s_panels_inited || (count != s_last_node_count);

    if (full_redraw) {
        // Header chính (vẽ 1 lần)
        lcd_fill_rect(0, 0, LCD_W, 32, LCD_DARKGRAY);
        lcd_draw_string(16, 8, "WATER MONITOR", LCD_WHITE, LCD_DARKGRAY, 2);
        // Reset cache
        for (int i = 0; i < LCD_MAX_HIST_NODES; i++) {
            s_panel_cache[i].used = false;
        }
        s_panels_inited = true;
        s_last_node_count = count;
    }

    int avail_h = LCD_H - 32;
    int panel_h = avail_h / count;

    for (int i = 0; i < count; i++) {
        int panel_y = 32 + i * panel_h;
        panel_cache_t *cache = &s_panel_cache[i];

        // Slot chưa init, ID đổi, hoặc panel size khác → full draw panel
        bool need_full = !cache->used ||
                         strcmp(cache->id, nodes[i].id) != 0 ||
                         cache->panel_y != panel_y ||
                         cache->panel_h != panel_h;

        if (need_full) {
            draw_node_panel_full(panel_y, panel_h, &nodes[i], cache);
            strncpy(cache->id, nodes[i].id, sizeof(cache->id) - 1);
            cache->id[sizeof(cache->id) - 1] = '\0';
            cache->panel_y = panel_y;
            cache->panel_h = panel_h;
            cache->used = true;

            // Đường phân cách (vẽ 1 lần khi full draw)
            if (i < count - 1)
                lcd_draw_hline(0, panel_y + panel_h - 1, LCD_W, LCD_GRAY);
        } else {
            // Chỉ update field đã đổi
            draw_node_panel_update(panel_y, panel_h, &nodes[i], cache);
        }

        // Cache giá trị mới
        cache->status = nodes[i].status;
        cache->distance_cm = nodes[i].distance_cm;
        cache->seq = nodes[i].seq;
        cache->is_error = nodes[i].is_error;
        cache->age_seconds = nodes[i].age_seconds;
    }
}

// ─── Dashboard ───────────────────────────────────────────────
void lcd_show_dashboard(lcd_status_t status, float distance_cm, int seq)
{
    lcd_history_push(distance_cm);

    uint16_t  status_color;
    const char *label;
    switch (status) {
        case LCD_STATUS_DANGER:    status_color = LCD_ORANGE; label = "DANGER";    break;
        case LCD_STATUS_EMERGENCY: status_color = LCD_RED;    label = "EMERGENCY"; break;
        default:                   status_color = LCD_GREEN;  label = "NORMAL";    break;
    }

    // ─ Header ─
    lcd_fill_rect(0, 0, LCD_W, 32, LCD_DARKGRAY);
    lcd_draw_string(8, 8, "WATER MONITOR", LCD_WHITE, LCD_DARKGRAY, 2);
    lcd_draw_hline(0, 32, LCD_W, LCD_GRAY);

    // ─ Body background ─
    lcd_fill_rect(0, 33, LCD_W, 175, LCD_BLACK);

    // ─ Gauge dọc ─
    draw_gauge(20, 50, 50, 150, distance_cm, status_color);

    // ─ Status + value + trend ─
    lcd_draw_string(90, 55, label, status_color, LCD_BLACK, 2);

    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f", distance_cm);
    lcd_draw_string(90, 95, buf, LCD_WHITE, LCD_BLACK, 3);
    lcd_draw_string(90, 140, "CM", LCD_GRAY, LCD_BLACK, 2);

    trend_t tr = calc_trend();
    const uint8_t *arrow_bm = (tr == TREND_UP)   ? ARROW_UP
                             : (tr == TREND_DOWN) ? ARROW_DOWN
                                                  : ARROW_FLAT;
    uint16_t arrow_color = (tr == TREND_UP)   ? LCD_RED
                          : (tr == TREND_DOWN) ? LCD_GREEN
                                               : LCD_GRAY;
    draw_bitmap(200, 100, 16, 16, arrow_bm, arrow_color, LCD_BLACK);

    // ─ History chart ─
    lcd_draw_string(15, 215, "HISTORY", LCD_WHITE, LCD_BLACK, 1);
    draw_chart(15, 230, LCD_W - 30, 60);

    // ─ Footer ─
    lcd_fill_rect(0, 295, LCD_W, 25, LCD_DARKGRAY);
    snprintf(buf, sizeof(buf), "SEQ: %03d", seq);
    lcd_draw_string(8, 302, buf, LCD_WHITE, LCD_DARKGRAY, 2);
}

void lcd_show_error(const char *msg)
{
    lcd_clear(LCD_BLACK);
    lcd_fill_rect(0, 0, LCD_W, 40, LCD_RED);
    lcd_draw_string(40, 12, "SENSOR ERROR", LCD_WHITE, LCD_RED, 2);
    lcd_draw_string(20, 100, msg, LCD_RED, LCD_BLACK, 2);
    lcd_invalidate();  // ← reset cache để lần sau multi_node full-redraw
}

void lcd_show_boot_screen(void)
{
    lcd_clear(LCD_BLACK);
    lcd_draw_string(40, 120, "WATER MONITOR", LCD_WHITE, LCD_BLACK, 2);
    lcd_draw_string(60, 160, "STARTING...",   LCD_GRAY,  LCD_BLACK, 2);
    lcd_invalidate();  // ← reset cache để lần sau multi_node full-redraw
}
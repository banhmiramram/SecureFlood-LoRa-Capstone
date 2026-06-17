#include "hc_sr04.h"

#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

static int s_trig = -1;
static int s_echo = -1;
static int s_out  = -1;

void hc_sr04_init(int trig_pin, int echo_pin)
{
    hc_sr04_init_full(trig_pin, echo_pin, -1);
}

void hc_sr04_init_full(int trig_pin, int echo_pin, int out_pin)
{
    s_trig = trig_pin;
    s_echo = echo_pin;
    s_out  = out_pin;

    gpio_set_direction(s_trig, GPIO_MODE_OUTPUT);
    gpio_set_direction(s_echo, GPIO_MODE_INPUT);

    if (s_out >= 0 && s_out <= 47) {
        gpio_set_direction(s_out, GPIO_MODE_INPUT);
        gpio_set_pull_mode(s_out, GPIO_FLOATING);
    }
    else {
        s_out = -1;    // không dùng chân OUT
    }
}

float hc_sr04_get_distance(void)
{
    gpio_set_level(s_trig, 0);
    esp_rom_delay_us(2);
    gpio_set_level(s_trig, 1);
    esp_rom_delay_us(10);
    gpio_set_level(s_trig, 0);

    int64_t start = esp_timer_get_time();
    while (gpio_get_level(s_echo) == 0) {
        if (esp_timer_get_time() - start > 30000) return -1;
    }

    int64_t echo_start = esp_timer_get_time();
    while (gpio_get_level(s_echo) == 1) {
        if (esp_timer_get_time() - echo_start > 30000) return -1;
    }

    int64_t duration = esp_timer_get_time() - echo_start;
    return duration * 0.0343 / 2;
}

int hc_sr04_read_out(void)
{
    if (s_out < 0) return -1;
    return gpio_get_level(s_out);
}

bool hc_sr04_alarm_active(void)
{
    int v = hc_sr04_read_out();
    return v == 1;
}
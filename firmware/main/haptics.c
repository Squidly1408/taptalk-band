#include "driver/gpio.h"
#include "esp_timer.h"
#include "haptics.h"
#include "pins.h"

static esp_timer_handle_t s_timer;

static void stop_cb(void *arg)
{
    gpio_set_level(PIN_HAPTIC, 0);
}

void haptics_init(void)
{
    gpio_config_t cfg = { .pin_bit_mask = 1ULL << PIN_HAPTIC, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(PIN_HAPTIC, 0);
    const esp_timer_create_args_t args = { .callback = stop_cb, .name = "haptic" };
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_timer));
}

void haptics_pulse(uint32_t ms)
{
    esp_timer_stop(s_timer);  // fine if not running
    gpio_set_level(PIN_HAPTIC, 1);
    esp_timer_start_once(s_timer, (uint64_t)ms * 1000);
}

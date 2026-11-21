// Button scanner: debounced polling while any key is down, interrupt-woken otherwise.
//
// Symbol buttons post EVT_BUTTON_PRESS the moment they are pressed (fast feedback).
// The page key is time-based:
//   released after 1-3 s  -> EVT_PAGE_NEXT
//   held 3 s              -> EVT_BLE_WINDOW
//   held 5 s              -> EVT_PAIRING_MODE
//   held 15 s             -> EVT_RESTART
//   page key + button 1 held 10 s -> EVT_FACTORY_RESET
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "buttons.h"
#include "events.h"
#include "pins.h"

#define SCAN_MS        10
#define DEBOUNCE_COUNT 3

static const char *TAG = "buttons";
static const gpio_num_t s_pins[BUTTON_COUNT + 1] = {
    PIN_BTN_1, PIN_BTN_2, PIN_BTN_3, PIN_BTN_4, PIN_PAGE_KEY,
};
static TaskHandle_t s_task;

static void IRAM_ATTR gpio_isr(void *arg)
{
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_task, &woken);
    portYIELD_FROM_ISR(woken);
}

static void buttons_task(void *arg)
{
    int wake_button = (int)(intptr_t)arg;
    bool pressed[BUTTON_COUNT + 1] = { 0 };
    uint8_t change_count[BUTTON_COUNT + 1] = { 0 };
    uint32_t page_held_ms = 0, combo_held_ms = 0;
    bool fired_ble = false, fired_pair = false, fired_restart = false, fired_reset = false;

    // Treat keys already down at start-up as pressed, so a wake press is not doubled.
    for (int i = 0; i <= BUTTON_COUNT; i++) {
        pressed[i] = gpio_get_level(s_pins[i]) == 0;
    }
    if (wake_button >= 0 && wake_button < BUTTON_COUNT) {
        events_post(EVT_BUTTON_PRESS, wake_button);
    }

    for (;;) {
        bool any_down = false;
        for (int i = 0; i <= BUTTON_COUNT; i++) {
            any_down |= pressed[i];
        }
        if (!any_down) {
            // Sleep until an edge interrupt arrives.
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
        vTaskDelay(pdMS_TO_TICKS(SCAN_MS));

        for (int i = 0; i <= BUTTON_COUNT; i++) {
            bool level_down = gpio_get_level(s_pins[i]) == 0;
            if (level_down == pressed[i]) {
                change_count[i] = 0;
                continue;
            }
            if (++change_count[i] < DEBOUNCE_COUNT) {
                continue;
            }
            change_count[i] = 0;
            pressed[i] = level_down;

            if (i < BUTTON_COUNT && level_down) {
                events_post(EVT_BUTTON_PRESS, i);
            }
            if (i == KEY_PAGE && !level_down) {
                if (page_held_ms >= 1000 && page_held_ms < 3000) {
                    events_post(EVT_PAGE_NEXT, 0);
                }
                page_held_ms = 0;
                fired_ble = fired_pair = fired_restart = false;
            }
        }

        if (pressed[KEY_PAGE]) {
            page_held_ms += SCAN_MS;
            if (!fired_ble && page_held_ms >= 3000) {
                fired_ble = true;
                events_post(EVT_BLE_WINDOW, 0);
            }
            if (!fired_pair && page_held_ms >= 5000) {
                fired_pair = true;
                events_post(EVT_PAIRING_MODE, 0);
            }
            if (!fired_restart && page_held_ms >= 15000) {
                fired_restart = true;
                events_post(EVT_RESTART, 0);
            }
        }

        if (pressed[KEY_PAGE] && pressed[0]) {
            combo_held_ms += SCAN_MS;
            if (!fired_reset && combo_held_ms >= 10000) {
                fired_reset = true;
                fired_restart = true;  // don't also restart at 15 s
                events_post(EVT_FACTORY_RESET, 0);
            }
        } else {
            combo_held_ms = 0;
            fired_reset = false;
        }
    }
}

void buttons_init(int wake_button)
{
    gpio_config_t cfg = {
        .pin_bit_mask = buttons_wake_mask() & ~(1ULL << PIN_CHARGER_PG),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    xTaskCreate(buttons_task, "buttons", 3072, (void *)(intptr_t)wake_button, 10, &s_task);

    // The ISR service may already be installed by power.c; ignore that case.
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_ERROR_CHECK(err);
    }
    for (int i = 0; i <= BUTTON_COUNT; i++) {
        gpio_isr_handler_add(s_pins[i], gpio_isr, NULL);
    }
    ESP_LOGI(TAG, "ready");
}

uint64_t buttons_wake_mask(void)
{
    uint64_t mask = 1ULL << PIN_CHARGER_PG;
    for (int i = 0; i <= BUTTON_COUNT; i++) {
        mask |= 1ULL << s_pins[i];
    }
    return mask;
}

int buttons_index_from_wake_mask(uint64_t mask)
{
    for (int i = 0; i < BUTTON_COUNT; i++) {
        if (mask & (1ULL << s_pins[i])) {
            return i;
        }
    }
    return -1;
}

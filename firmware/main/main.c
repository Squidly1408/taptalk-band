// TapTalk Band firmware: main event loop.
//
// Everything that happens (button presses, charger changes, Bluetooth requests)
// arrives as an event on g_events and is handled here, one at a time.
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "audio.h"
#include "ble_service.h"
#include "board_store.h"
#include "buttons.h"
#include "display.h"
#include "events.h"
#include "haptics.h"
#include "pins.h"
#include "power.h"
#include "ui.h"

#define IDLE_SLEEP_MS     5000
#define PRESS_FLASH_MS    500
#define BLE_WINDOW_S      120
#define LOW_BATTERY_PCT   3

static const char *TAG = "main";

QueueHandle_t g_events;

// Survives deep sleep, so a wake-up press doesn't redraw the whole screen.
RTC_DATA_ATTR static int s_page;
RTC_DATA_ATTR static bool s_screen_valid;

static board_t s_board;
static bool s_board_ok;
static bool s_inverted[BOARD_MAX_BUTTONS];
static int64_t s_last_press_us[BOARD_MAX_BUTTONS];
static esp_timer_handle_t s_flash_timer[BOARD_MAX_BUTTONS];
static uint8_t s_tile[2500];

// ---------- drawing ----------

static void render_page(bool refresh)
{
    if (!s_board_ok) {
        ui_draw_set_me_up();
        s_screen_valid = false;
        return;
    }
    if (s_page >= s_board.n_pages) {
        s_page = 0;
    }
    const board_page_t *page = &s_board.pages[s_page];
    size_t bytes = board_tile_bytes(s_board.layout);
    display_clear(true);
    for (int i = 0; i < page->n_buttons; i++) {
        int x, y, w, h;
        board_tile_rect(s_board.layout, i, &x, &y, &w, &h);
        if (board_read_tile(&page->buttons[i], s_tile, bytes) == (int)bytes) {
            display_blit(x, y, w, h, s_tile);
        }
        s_inverted[i] = false;
    }
    if (refresh) {
        display_update_full();
    } else {
        display_sync_ram();  // picture already on the glass; just restore the base image
    }
    s_screen_valid = true;
}

static void load_board(void)
{
    char why[64];
    s_board_ok = board_load(board_store_live_dir(), &s_board, why, sizeof why) == ESP_OK;
    if (!s_board_ok) {
        ESP_LOGW(TAG, "no usable board: %s", why);
        return;
    }
    audio_set_volume(s_board.settings.volume);
    if (s_board.settings.page_lock >= 0) {
        s_page = s_board.settings.page_lock;
    }
}

// ---------- press feedback ----------

static void flash_done(void *arg)
{
    events_post(EVT_TILE_RESTORE, (int32_t)(intptr_t)arg);
}

static void toggle_tile(int index)
{
    int x, y, w, h;
    board_tile_rect(s_board.layout, index, &x, &y, &w, &h);
    display_invert_rect(x, y, w, h);
    display_update_partial();
    s_inverted[index] = !s_inverted[index];
}

static void on_button(int index)
{
    if (!s_board_ok) {
        audio_beep(440, 120);
        return;
    }
    const board_page_t *page = &s_board.pages[s_page];
    if (index >= page->n_buttons) {
        return;  // big2 layout: buttons 3 and 4 share tiles with 1 and 2
    }
    int64_t now = esp_timer_get_time();
    if (now - s_last_press_us[index] < (int64_t)s_board.settings.repeat_guard_ms * 1000) {
        return;
    }
    s_last_press_us[index] = now;

    haptics_pulse(s_board.settings.vibrate_only ? 150 : 60);
    if (!s_board.settings.vibrate_only) {
        char clip[128];
        board_clip_path(&page->buttons[index], clip, sizeof clip);
        audio_play_clip(clip);
    }
    if (!s_inverted[index]) {
        toggle_tile(index);
    }
    esp_timer_stop(s_flash_timer[index]);
    esp_timer_start_once(s_flash_timer[index], PRESS_FLASH_MS * 1000);
}

// ---------- sleep ----------

static bool can_sleep(void)
{
    for (int i = 0; i < BOARD_MAX_BUTTONS; i++) {
        if (s_inverted[i]) {
            return false;
        }
    }
    return !power_on_charger() && !ble_service_active() && !board_stage_active();
}

static void deep_sleep(uint64_t wake_mask)
{
    ESP_LOGI(TAG, "deep sleep");
    display_sleep();
    for (int pin = 0; pin < 22; pin++) {
        if (wake_mask & (1ULL << pin)) {
            rtc_gpio_pullup_en(pin);
            rtc_gpio_pulldown_dis(pin);
        }
    }
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ext1_wakeup(wake_mask, ESP_EXT1_WAKEUP_ANY_LOW);
    esp_deep_sleep_start();
}

static void check_battery(void)
{
    if (power_on_charger()) {
        return;
    }
    int pct = power_battery_percent();
    if (pct >= 0 && pct <= LOW_BATTERY_PCT) {
        ui_draw_charge_me();
        s_screen_valid = false;
        deep_sleep(1ULL << PIN_CHARGER_PG);  // only a charger wakes it now
    }
}

// ---------- event handling ----------

static void notify_op(const char *op, const char *err)
{
    char buf[96];
    if (err) {
        snprintf(buf, sizeof buf, "{\"ok\":false,\"op\":\"%s\",\"err\":\"%s\"}", op, err);
    } else {
        snprintf(buf, sizeof buf, "{\"ok\":true,\"op\":\"%s\"}", op);
    }
    ble_service_notify_file_result(buf);
}

static void handle(const app_event_t *e)
{
    switch (e->type) {
    case EVT_BUTTON_PRESS:
        on_button(e->arg);
        break;

    case EVT_TILE_RESTORE:
        if (s_inverted[e->arg]) {
            toggle_tile(e->arg);
        }
        break;

    case EVT_PAGE_NEXT:
        if (s_board_ok && s_board.settings.page_lock < 0 && s_board.n_pages > 1) {
            s_page = (s_page + 1) % s_board.n_pages;
            haptics_pulse(40);
            render_page(true);
        }
        break;

    case EVT_BLE_WINDOW:
        haptics_pulse(40);
        ble_service_open_window(power_on_charger() ? 0 : BLE_WINDOW_S);
        break;

    case EVT_PAIRING_MODE:
        if (power_on_charger()) {
            ble_service_allow_pairing(true);
            ble_service_open_window(0);
            audio_beep(988, 200);
        } else {
            audio_beep(330, 300);  // pairing only works on the charger
        }
        break;

    case EVT_BLE_PASSKEY:
        ui_draw_passkey((uint32_t)e->arg);
        s_screen_valid = false;
        break;

    case EVT_BLE_PAIRING_DONE:
        ble_service_allow_pairing(false);
        audio_beep(e->arg ? 1319 : 330, 200);
        render_page(true);
        break;

    case EVT_CHARGER_CHANGED:
        if (power_on_charger()) {
            ble_service_open_window(0);
        } else {
            ble_service_allow_pairing(false);
            ble_service_close_window();
        }
        break;

    case EVT_BLE_SESSION:
        notify_op("session", board_stage_begin(e->arg));
        break;

    case EVT_BLE_COMMIT: {
        ui_draw_busy();
        const char *err = board_stage_commit();
        notify_op("commit", err);
        if (!err) {
            s_page = 0;
            load_board();
        }
        render_page(true);
        break;
    }

    case EVT_BLE_SETTINGS: {
        int old_page = s_page;
        load_board();
        if (s_page != old_page) {
            render_page(true);
        }
        break;
    }

    case EVT_BLE_FIND_ME: {
        int n = e->arg < 1 ? 1 : e->arg > 10 ? 10 : e->arg;
        for (int i = 0; i < n; i++) {
            audio_beep(1568, 250);
        }
        haptics_pulse(300);
        display_invert_rect(0, 0, DISPLAY_W, DISPLAY_H);
        display_update_partial();
        vTaskDelay(pdMS_TO_TICKS(600));
        display_invert_rect(0, 0, DISPLAY_W, DISPLAY_H);
        display_update_partial();
        break;
    }

    case EVT_FACTORY_RESET:
        if (power_on_charger() && board_restore_factory() == ESP_OK) {
            ble_service_clear_bonds();
            s_page = 0;
            load_board();
            render_page(true);
            audio_beep(523, 150);
            audio_beep(1047, 300);
        }
        break;

    case EVT_RESTART:
        vTaskDelay(pdMS_TO_TICKS(300));  // let the last notification go out
        esp_restart();
        break;

    case EVT_BATTERY_LOW:
        check_battery();
        break;

    case EVT_BLE_IDLE:
    default:
        break;
    }
}

// ---------- start-up ----------

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    g_events = xQueueCreate(16, sizeof(app_event_t));

    int wake_button = -1;
    if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1) {
        wake_button = buttons_index_from_wake_mask(esp_sleep_get_ext1_wakeup_status());
    } else {
        s_screen_valid = false;  // cold boot: the glass may show anything
        s_page = 0;
    }

    power_init();
    haptics_init();
    audio_init();
    display_init();
    for (int i = 0; i < BOARD_MAX_BUTTONS; i++) {
        const esp_timer_create_args_t args = {
            .callback = flash_done, .arg = (void *)(intptr_t)i, .name = "flash",
        };
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_flash_timer[i]));
    }

    if (board_store_init() == ESP_OK) {
        load_board();
    }
    check_battery();
    render_page(!s_screen_valid);

    buttons_init(wake_button);
    ble_service_init();
    if (power_on_charger()) {
        ble_service_open_window(0);
    }

    // Started cleanly: keep this firmware (otherwise the bootloader rolls back).
    esp_ota_mark_app_valid_cancel_rollback();

    app_event_t e;
    for (;;) {
        if (xQueueReceive(g_events, &e, pdMS_TO_TICKS(IDLE_SLEEP_MS)) == pdTRUE) {
            handle(&e);
        } else if (can_sleep()) {
            check_battery();
            deep_sleep(buttons_wake_mask());
        }
    }
}

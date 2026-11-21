// Application events. Every module posts here; main.c handles them in one place.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum {
    EVT_BUTTON_PRESS,      // arg = button index 0-3
    EVT_PAGE_NEXT,         // page key held 1-3 s
    EVT_BLE_WINDOW,        // page key held 3 s: open Bluetooth for 2 minutes
    EVT_PAIRING_MODE,      // page key held 5 s
    EVT_RESTART,           // page key held 15 s
    EVT_FACTORY_RESET,     // page key + button 1 held 10 s
    EVT_CHARGER_CHANGED,   // arg = 1 on charger, 0 off
    EVT_BATTERY_LOW,
    EVT_TILE_RESTORE,      // arg = button index whose press flash should end
    EVT_BLE_PASSKEY,       // arg = 6-digit passkey to show
    EVT_BLE_PAIRING_DONE,  // arg = 1 success, 0 failure
    EVT_BLE_SESSION,       // arg = 1 patch, 0 replace
    EVT_BLE_COMMIT,
    EVT_BLE_FIND_ME,       // arg = seconds
    EVT_BLE_SETTINGS,      // settings were written; reload the board
    EVT_BLE_IDLE,          // Bluetooth window closed and no connection
} event_type_t;

typedef struct {
    event_type_t type;
    int32_t arg;
} app_event_t;

extern QueueHandle_t g_events;

static inline bool events_post(event_type_t type, int32_t arg)
{
    app_event_t e = { .type = type, .arg = arg };
    return xQueueSend(g_events, &e, 0) == pdTRUE;
}

static inline void events_post_from_isr(event_type_t type, int32_t arg)
{
    app_event_t e = { .type = type, .arg = arg };
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(g_events, &e, &woken);
    portYIELD_FROM_ISR(woken);
}

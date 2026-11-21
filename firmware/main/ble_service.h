// Bluetooth LE peripheral: the TapTalk GATT service (see docs/ble-protocol.md).
#pragma once

#include <stdbool.h>
#include <stdint.h>

void ble_service_init(void);

// Start advertising. seconds = 0 keeps advertising until ble_service_close_window().
void ble_service_open_window(uint32_t seconds);
void ble_service_close_window(void);
bool ble_service_active(void);  // advertising or connected

// New pairings are only accepted while this is true (band on charger + staff gesture).
void ble_service_allow_pairing(bool allow);
void ble_service_clear_bonds(void);

// Notify a JSON result on the File control characteristic.
void ble_service_notify_file_result(const char *json);

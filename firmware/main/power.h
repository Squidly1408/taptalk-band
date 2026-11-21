#pragma once

#include <stdbool.h>

void power_init(void);
bool power_on_charger(void);
int power_battery_percent(void);  // 0-100, estimated from voltage
// Put the charger chip into ship mode (battery disconnected until next charge).
void power_enter_ship_mode(void);

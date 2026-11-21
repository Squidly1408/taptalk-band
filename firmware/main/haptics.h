#pragma once

#include <stdint.h>

void haptics_init(void);
void haptics_pulse(uint32_t ms);  // non-blocking

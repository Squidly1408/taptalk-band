#pragma once

#include <stdint.h>

#define BUTTON_COUNT   4   // symbol buttons
#define KEY_PAGE       4   // index of the page key in the internal table

// Start the button scanner. If the chip woke from deep sleep because of a
// button, pass that button's index (0-3) as wake_button so the press is not lost,
// otherwise pass -1.
void buttons_init(int wake_button);

// Bit mask of every GPIO that should wake the chip from deep sleep.
uint64_t buttons_wake_mask(void);

// Map a GPIO wake mask back to a symbol button index, or -1.
int buttons_index_from_wake_mask(uint64_t mask);

// 1.54 in 200 x 200 black/white e-paper with an SSD1681-class controller.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#define DISPLAY_W 200
#define DISPLAY_H 200

void display_init(void);

// Frame buffer helpers (nothing reaches the panel until an update call).
void display_clear(bool white);
void display_fill_rect(int x, int y, int w, int h, bool white);
void display_invert_rect(int x, int y, int w, int h);
// Draw a 1-bit tile: bits packed continuously across rows (no row padding),
// MSB first, 1 = white, 0 = black. See docs/board-format.md.
void display_blit(int x, int y, int w, int h, const uint8_t *bits);

void display_update_full(void);     // ~2 s, clears ghosting
void display_update_partial(void);  // fast, for press feedback
// Load the frame buffer into the panel RAM without changing the picture.
// Needed after deep sleep so the next partial update has a correct base image.
void display_sync_ram(void);
void display_sleep(void);

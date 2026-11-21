#include <stdbool.h>
#include "display.h"
#include "ui.h"

#define BLACK false
#define WHITE true

// 7-segment layout: bits a b c d e f g = 0..6
static const uint8_t SEGMENTS[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
};

static void draw_digit(int x, int y, int w, int h, int t, int d)
{
    uint8_t s = SEGMENTS[d % 10];
    int mid = y + h / 2 - t / 2;
    if (s & 0x01) display_fill_rect(x, y, w, t, BLACK);                         // a
    if (s & 0x02) display_fill_rect(x + w - t, y, t, h / 2, BLACK);             // b
    if (s & 0x04) display_fill_rect(x + w - t, y + h / 2, t, h / 2, BLACK);     // c
    if (s & 0x08) display_fill_rect(x, y + h - t, w, t, BLACK);                 // d
    if (s & 0x10) display_fill_rect(x, y + h / 2, t, h / 2, BLACK);             // e
    if (s & 0x20) display_fill_rect(x, y, t, h / 2, BLACK);                     // f
    if (s & 0x40) display_fill_rect(x, mid, w, t, BLACK);                       // g
}

static void frame(int x, int y, int w, int h, int t)
{
    display_fill_rect(x, y, w, t, BLACK);
    display_fill_rect(x, y + h - t, w, t, BLACK);
    display_fill_rect(x, y, t, h, BLACK);
    display_fill_rect(x + w - t, y, t, h, BLACK);
}

void ui_draw_passkey(uint32_t passkey)
{
    display_clear(WHITE);
    frame(4, 60, 192, 80, 4);
    uint32_t div = 100000;
    for (int i = 0; i < 6; i++) {
        int d = (passkey / div) % 10;
        div /= 10;
        draw_digit(14 + i * 30, 76, 24, 48, 5, d);
    }
    display_update_full();
}

void ui_draw_charge_me(void)
{
    display_clear(WHITE);
    frame(30, 65, 128, 70, 8);                 // battery body
    display_fill_rect(158, 85, 12, 30, BLACK); // terminal
    display_fill_rect(42, 77, 10, 46, BLACK);  // one thin bar left: empty
    display_update_full();
}

void ui_draw_set_me_up(void)
{
    // Simplified logo mark: three button tiles and one filled "speech" tile.
    display_clear(WHITE);
    frame(30, 30, 140, 140, 8);
    frame(48, 48, 46, 46, 5);
    frame(48, 106, 46, 46, 5);
    frame(106, 106, 46, 46, 5);
    display_fill_rect(106, 48, 46, 38, BLACK);
    display_fill_rect(112, 86, 10, 10, BLACK);
    display_update_full();
}

void ui_draw_busy(void)
{
    // Three dots in the middle of the current picture; partial update keeps it quick.
    for (int i = 0; i < 3; i++) {
        display_fill_rect(76 + i * 20, 94, 12, 12, BLACK);
    }
    display_update_partial();
}

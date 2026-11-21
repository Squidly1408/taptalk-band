// Built-in screens drawn without fonts (the band's tiles carry their own label art).
#pragma once

#include <stdint.h>

void ui_draw_passkey(uint32_t passkey);  // six large 7-segment digits
void ui_draw_charge_me(void);            // empty battery picture
void ui_draw_set_me_up(void);            // logo mark: no board loaded yet
void ui_draw_busy(void);                 // receiving a board

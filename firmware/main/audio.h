#pragma once

#include <stdint.h>

void audio_init(void);
void audio_set_volume(int percent);  // 0-100
// Queue a button's clip (absolute path). Falls back to a beep if it can't be played.
void audio_play_clip(const char *path);
void audio_beep(uint16_t freq_hz, uint16_t ms);

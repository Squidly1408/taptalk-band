// Board storage on the "storage" FAT partition, mounted at /board.
//
//   /board/live/     the board the band is showing
//   /board/stage/    a board being received over Bluetooth
//   /board/old/      the previous board, only during a commit
//   /board/factory/  the default board, for factory reset
//
// A commit only swaps stage -> live after the staged board validates, so a
// broken transfer can never leave the band without a working board.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define BOARD_MAX_PAGES    8
#define BOARD_MAX_BUTTONS  4
#define BOARD_PATH_MAX     48

typedef enum { BOARD_LAYOUT_GRID4 = 0, BOARD_LAYOUT_BIG2 = 1 } board_layout_t;

typedef struct {
    char id[17];
    char label[49];               // up to 12 characters, UTF-8
    char image[BOARD_PATH_MAX];
    char sound[BOARD_PATH_MAX];   // empty if none
} board_button_t;

typedef struct {
    char id[17];
    char title[49];
    uint8_t n_buttons;
    board_button_t buttons[BOARD_MAX_BUTTONS];
} board_page_t;

typedef struct {
    int volume;           // 0-100
    bool vibrate_only;
    int page_lock;        // page index, or -1
    int repeat_guard_ms;
    char idle_text[49];
} board_settings_t;

typedef struct {
    char name[49];
    board_layout_t layout;
    uint8_t n_pages;
    board_page_t pages[BOARD_MAX_PAGES];
    board_settings_t settings;
} board_t;

esp_err_t board_store_init(void);
const char *board_store_live_dir(void);
bool board_path_ok(const char *path);

// Parse and fully validate the board in dir (including that every file exists).
// On failure returns an error and writes a reason into err.
esp_err_t board_load(const char *dir, board_t *out, char *err, size_t errlen);

// Tile geometry for a layout and button index.
void board_tile_rect(board_layout_t layout, int index, int *x, int *y, int *w, int *h);
size_t board_tile_bytes(board_layout_t layout);

// Read a button's tile image from the live board. Returns bytes read or -1.
int board_read_tile(const board_button_t *b, uint8_t *buf, size_t len);
// Absolute path of a button's clip in the live board, or "" if none.
void board_clip_path(const board_button_t *b, char *out, size_t len);

// Staging (Bluetooth transfer). Each returns NULL on success or a protocol error
// code string (see docs/ble-protocol.md).
const char *board_stage_begin(bool patch);
const char *board_stage_file_begin(const char *path, uint32_t size, uint32_t crc);
const char *board_stage_file_data(uint16_t seq, const uint8_t *data, size_t len);
const char *board_stage_file_end(void);
const char *board_stage_delete(const char *path);
const char *board_stage_commit(void);
void board_stage_abort(void);
bool board_stage_active(void);

// JSON list of live files with sizes and CRC-32s. Caller frees.
char *board_manifest_json(void);
// Replace the "settings" object of the live board.json.
esp_err_t board_update_settings_json(const char *json, size_t len);
// Copy /board/factory over /board/live.
esp_err_t board_restore_factory(void);

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
#include "board_store.h"
#include "crc32.h"

#define ROOT        "/board"
#define LIVE_DIR    ROOT "/live"
#define STAGE_DIR   ROOT "/stage"
#define OLD_DIR     ROOT "/old"
#define FACTORY_DIR ROOT "/factory"
#define JSON_MAX    (16 * 1024)

static const char *TAG = "board";
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static SemaphoreHandle_t s_lock;

// Current incoming file
static bool s_stage_active;
static FILE *s_file;
static char s_file_path[BOARD_PATH_MAX];
static uint32_t s_file_size, s_file_crc, s_written, s_crc;
static uint16_t s_seq;

// ---------- small filesystem helpers ----------

static bool exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static bool is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static int rm_rf(const char *path)
{
    if (!exists(path)) {
        return 0;
    }
    if (!is_dir(path)) {
        return unlink(path);
    }
    DIR *d = opendir(path);
    if (!d) {
        return -1;
    }
    struct dirent *e;
    char child[160];
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        snprintf(child, sizeof child, "%s/%s", path, e->d_name);
        rm_rf(child);
    }
    closedir(d);
    return rmdir(path);
}

static int mkdirs_for(const char *filepath)
{
    char tmp[160];
    snprintf(tmp, sizeof tmp, "%s", filepath);
    for (char *p = tmp + strlen(ROOT) + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (!is_dir(tmp) && mkdir(tmp, 0775) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    return 0;
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in = fopen(src, "rb");
    if (!in) {
        return -1;
    }
    if (mkdirs_for(dst) != 0) {
        fclose(in);
        return -1;
    }
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }
    uint8_t buf[512];
    size_t n;
    int rc = 0;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            rc = -1;
            break;
        }
    }
    fclose(in);
    fclose(out);
    return rc;
}

static int copy_tree(const char *src, const char *dst)
{
    if (!is_dir(src)) {
        return copy_file(src, dst);
    }
    if (!is_dir(dst) && mkdir(dst, 0775) != 0) {
        return -1;
    }
    DIR *d = opendir(src);
    if (!d) {
        return -1;
    }
    struct dirent *e;
    char s[160], t[160];
    int rc = 0;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        snprintf(s, sizeof s, "%s/%s", src, e->d_name);
        snprintf(t, sizeof t, "%s/%s", dst, e->d_name);
        if (copy_tree(s, t) != 0) {
            rc = -1;
        }
    }
    closedir(d);
    return rc;
}

static char *read_text(const char *path, size_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return NULL;
    }
    char *buf = malloc(max + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, max, f);
    fclose(f);
    buf[n] = '\0';
    return buf;
}

static long file_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

// ---------- validation ----------

bool board_path_ok(const char *p)
{
    size_t n = p ? strlen(p) : 0;
    if (n == 0 || n >= BOARD_PATH_MAX || p[0] == '/' || strstr(p, "..")) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = p[i];
        if (!(islower((unsigned char)c) || isdigit((unsigned char)c) || c == '_' || c == '-' || c == '.' || c == '/')) {
            return false;
        }
    }
    return true;
}

static bool id_ok(const char *s)
{
    size_t n = s ? strlen(s) : 0;
    if (n == 0 || n > 16) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        if (!(islower((unsigned char)s[i]) || isdigit((unsigned char)s[i]) || s[i] == '_')) {
            return false;
        }
    }
    return true;
}

static size_t utf8_chars(const char *s)
{
    size_t n = 0;
    for (; *s; s++) {
        if (((unsigned char)*s & 0xC0) != 0x80) {
            n++;
        }
    }
    return n;
}

#define FAIL(...) do { snprintf(err, errlen, __VA_ARGS__); goto fail; } while (0)

esp_err_t board_load(const char *dir, board_t *out, char *err, size_t errlen)
{
    char path[160];
    snprintf(path, sizeof path, "%s/board.json", dir);
    char *text = read_text(path, JSON_MAX);
    cJSON *root = NULL;
    if (!text) {
        snprintf(err, errlen, "missing board.json");
        return ESP_ERR_NOT_FOUND;
    }
    root = cJSON_Parse(text);
    free(text);
    memset(out, 0, sizeof *out);
    if (!root) FAIL("bad json");

    cJSON *fmt = cJSON_GetObjectItem(root, "format");
    if (!cJSON_IsNumber(fmt) || fmt->valueint != 1) FAIL("format must be 1");

    cJSON *name = cJSON_GetObjectItem(root, "name");
    snprintf(out->name, sizeof out->name, "%s", cJSON_IsString(name) ? name->valuestring : "");

    cJSON *layout = cJSON_GetObjectItem(root, "layout");
    if (!cJSON_IsString(layout)) FAIL("layout missing");
    if (!strcmp(layout->valuestring, "grid4")) out->layout = BOARD_LAYOUT_GRID4;
    else if (!strcmp(layout->valuestring, "big2")) out->layout = BOARD_LAYOUT_BIG2;
    else FAIL("unknown layout");
    int per_page = out->layout == BOARD_LAYOUT_GRID4 ? 4 : 2;
    long tile_bytes = (long)board_tile_bytes(out->layout);

    cJSON *pages = cJSON_GetObjectItem(root, "pages");
    int n_pages = cJSON_GetArraySize(pages);
    if (!cJSON_IsArray(pages) || n_pages < 1 || n_pages > BOARD_MAX_PAGES) FAIL("need 1-8 pages");
    out->n_pages = n_pages;

    for (int p = 0; p < n_pages; p++) {
        cJSON *pg = cJSON_GetArrayItem(pages, p);
        board_page_t *page = &out->pages[p];
        cJSON *pid = cJSON_GetObjectItem(pg, "id");
        if (!cJSON_IsString(pid) || !id_ok(pid->valuestring)) FAIL("page %d: bad id", p);
        snprintf(page->id, sizeof page->id, "%s", pid->valuestring);
        cJSON *title = cJSON_GetObjectItem(pg, "title");
        snprintf(page->title, sizeof page->title, "%s", cJSON_IsString(title) ? title->valuestring : "");

        cJSON *buttons = cJSON_GetObjectItem(pg, "buttons");
        if (!cJSON_IsArray(buttons) || cJSON_GetArraySize(buttons) != per_page) FAIL("page %s: need %d buttons", page->id, per_page);
        page->n_buttons = per_page;
        for (int b = 0; b < per_page; b++) {
            cJSON *bt = cJSON_GetArrayItem(buttons, b);
            board_button_t *btn = &page->buttons[b];
            cJSON *bid = cJSON_GetObjectItem(bt, "id");
            cJSON *label = cJSON_GetObjectItem(bt, "label");
            cJSON *image = cJSON_GetObjectItem(bt, "image");
            cJSON *sound = cJSON_GetObjectItem(bt, "sound");
            if (!cJSON_IsString(bid) || !id_ok(bid->valuestring)) FAIL("page %s button %d: bad id", page->id, b);
            if (!cJSON_IsString(label) || utf8_chars(label->valuestring) < 1 || utf8_chars(label->valuestring) > 12 ||
                strlen(label->valuestring) >= sizeof btn->label) FAIL("button %s: label must be 1-12 characters", bid->valuestring);
            if (!cJSON_IsString(image) || !board_path_ok(image->valuestring)) FAIL("button %s: bad image path", bid->valuestring);
            snprintf(btn->id, sizeof btn->id, "%s", bid->valuestring);
            snprintf(btn->label, sizeof btn->label, "%s", label->valuestring);
            snprintf(btn->image, sizeof btn->image, "%s", image->valuestring);
            snprintf(path, sizeof path, "%s/%s", dir, btn->image);
            if (file_size(path) != tile_bytes) FAIL("button %s: image must be %ld bytes", btn->id, tile_bytes);
            if (sound && !cJSON_IsNull(sound)) {
                if (!cJSON_IsString(sound) || !board_path_ok(sound->valuestring)) FAIL("button %s: bad sound path", btn->id);
                snprintf(btn->sound, sizeof btn->sound, "%s", sound->valuestring);
                snprintf(path, sizeof path, "%s/%s", dir, btn->sound);
                if (!exists(path)) FAIL("button %s: sound file missing", btn->id);
            }
        }
    }

    board_settings_t *s = &out->settings;
    s->volume = 70;
    s->page_lock = -1;
    s->repeat_guard_ms = 600;
    cJSON *set = cJSON_GetObjectItem(root, "settings");
    if (cJSON_IsObject(set)) {
        cJSON *v;
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(set, "volume"))) {
            s->volume = v->valueint < 0 ? 0 : v->valueint > 100 ? 100 : v->valueint;
        }
        s->vibrate_only = cJSON_IsTrue(cJSON_GetObjectItem(set, "vibrate_only"));
        if (cJSON_IsNumber(v = cJSON_GetObjectItem(set, "repeat_guard_ms"))) {
            s->repeat_guard_ms = v->valueint < 0 ? 0 : v->valueint > 5000 ? 5000 : v->valueint;
        }
        if (cJSON_IsString(v = cJSON_GetObjectItem(set, "idle_text"))) {
            snprintf(s->idle_text, sizeof s->idle_text, "%s", v->valuestring);
        }
        if (cJSON_IsString(v = cJSON_GetObjectItem(set, "page_lock"))) {
            for (int p = 0; p < out->n_pages; p++) {
                if (!strcmp(out->pages[p].id, v->valuestring)) {
                    s->page_lock = p;
                }
            }
        }
    }

    cJSON_Delete(root);
    return ESP_OK;

fail:
    cJSON_Delete(root);
    return ESP_ERR_INVALID_ARG;
}

// ---------- tiles and clips ----------

void board_tile_rect(board_layout_t layout, int index, int *x, int *y, int *w, int *h)
{
    if (layout == BOARD_LAYOUT_BIG2) {
        *x = (index % 2) * 100;
        *y = 0;
        *w = 100;
        *h = 200;
    } else {
        *x = (index % 2) * 100;
        *y = (index / 2) * 100;
        *w = 100;
        *h = 100;
    }
}

size_t board_tile_bytes(board_layout_t layout)
{
    return layout == BOARD_LAYOUT_BIG2 ? 2500 : 1250;
}

int board_read_tile(const board_button_t *b, uint8_t *buf, size_t len)
{
    char path[160];
    snprintf(path, sizeof path, "%s/%s", LIVE_DIR, b->image);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    size_t n = fread(buf, 1, len, f);
    fclose(f);
    return (int)n;
}

void board_clip_path(const board_button_t *b, char *out, size_t len)
{
    if (b->sound[0]) {
        snprintf(out, len, "%s/%s", LIVE_DIR, b->sound);
    } else {
        out[0] = '\0';
    }
}

// ---------- init ----------

esp_err_t board_store_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    const esp_vfs_fat_mount_config_t cfg = {
        .format_if_mount_failed = true,
        .max_files = 4,
        .allocation_unit_size = 4096,
    };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(ROOT, "storage", &cfg, &s_wl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
        return err;
    }

    // Recover from a commit interrupted by a crash or flat battery.
    if (!is_dir(LIVE_DIR) && is_dir(OLD_DIR)) {
        rename(OLD_DIR, LIVE_DIR);
    }
    rm_rf(OLD_DIR);
    rm_rf(STAGE_DIR);
    if (!is_dir(LIVE_DIR) && is_dir(FACTORY_DIR)) {
        copy_tree(FACTORY_DIR, LIVE_DIR);
    }
    return ESP_OK;
}

const char *board_store_live_dir(void)
{
    return LIVE_DIR;
}

// ---------- staging ----------

static void close_file(bool remove_it)
{
    if (s_file) {
        fclose(s_file);
        s_file = NULL;
        if (remove_it) {
            char path[160];
            snprintf(path, sizeof path, "%s/%s", STAGE_DIR, s_file_path);
            unlink(path);
        }
    }
}

const char *board_stage_begin(bool patch)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    close_file(true);
    rm_rf(STAGE_DIR);
    int rc = patch && is_dir(LIVE_DIR) ? copy_tree(LIVE_DIR, STAGE_DIR) : mkdir(STAGE_DIR, 0775);
    s_stage_active = rc == 0;
    xSemaphoreGive(s_lock);
    return s_stage_active ? NULL : "io";
}

bool board_stage_active(void)
{
    return s_stage_active;
}

const char *board_stage_file_begin(const char *path, uint32_t size, uint32_t crc)
{
    if (!s_stage_active) return "no_session";
    if (!board_path_ok(path)) return "bad_path";
    if (size > 256 * 1024) return "size";
    xSemaphoreTake(s_lock, portMAX_DELAY);
    close_file(true);
    char full[160];
    snprintf(full, sizeof full, "%s/%s", STAGE_DIR, path);
    const char *res = NULL;
    if (mkdirs_for(full) != 0 || (s_file = fopen(full, "wb")) == NULL) {
        res = "io";
    } else {
        snprintf(s_file_path, sizeof s_file_path, "%s", path);
        s_file_size = size;
        s_file_crc = crc;
        s_written = 0;
        s_crc = 0;
        s_seq = 0;
    }
    xSemaphoreGive(s_lock);
    return res;
}

const char *board_stage_file_data(uint16_t seq, const uint8_t *data, size_t len)
{
    if (!s_file) return "no_file";
    if (seq != s_seq) return "seq";
    if (s_written + len > s_file_size) return "size";
    if (fwrite(data, 1, len, s_file) != len) return "io";
    s_crc = crc32_update(s_crc, data, len);
    s_written += len;
    s_seq++;
    return NULL;
}

const char *board_stage_file_end(void)
{
    if (!s_file) return "no_file";
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const char *res = NULL;
    if (s_written != s_file_size) res = "size";
    else if (s_crc != s_file_crc) res = "crc";
    close_file(res != NULL);
    xSemaphoreGive(s_lock);
    return res;
}

const char *board_stage_delete(const char *path)
{
    if (!s_stage_active) return "no_session";
    if (!board_path_ok(path)) return "bad_path";
    char full[160];
    snprintf(full, sizeof full, "%s/%s", STAGE_DIR, path);
    return unlink(full) == 0 || errno == ENOENT ? NULL : "io";
}

const char *board_stage_commit(void)
{
    if (!s_stage_active) return "no_session";
    xSemaphoreTake(s_lock, portMAX_DELAY);
    close_file(true);
    board_t *tmp = malloc(sizeof(board_t));
    char why[64];
    const char *res = NULL;
    if (!tmp) {
        res = "io";
    } else if (board_load(STAGE_DIR, tmp, why, sizeof why) != ESP_OK) {
        ESP_LOGW(TAG, "staged board rejected: %s", why);
        res = "invalid_board";
    } else {
        rm_rf(OLD_DIR);
        if (is_dir(LIVE_DIR) && rename(LIVE_DIR, OLD_DIR) != 0) {
            res = "io";
        } else if (rename(STAGE_DIR, LIVE_DIR) != 0) {
            rename(OLD_DIR, LIVE_DIR);
            res = "io";
        } else {
            rm_rf(OLD_DIR);
            s_stage_active = false;
        }
    }
    free(tmp);
    xSemaphoreGive(s_lock);
    return res;
}

void board_stage_abort(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    close_file(true);
    rm_rf(STAGE_DIR);
    s_stage_active = false;
    xSemaphoreGive(s_lock);
}

// ---------- manifest, settings, factory reset ----------

static void add_files(cJSON *arr, const char *base, const char *rel)
{
    char dirpath[160];
    snprintf(dirpath, sizeof dirpath, "%s%s%s", base, rel[0] ? "/" : "", rel);
    DIR *d = opendir(dirpath);
    if (!d) {
        return;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) {
            continue;
        }
        char child_rel[BOARD_PATH_MAX + 32], child_abs[200];
        snprintf(child_rel, sizeof child_rel, "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
        snprintf(child_abs, sizeof child_abs, "%s/%s", base, child_rel);
        if (is_dir(child_abs)) {
            add_files(arr, base, child_rel);
            continue;
        }
        FILE *f = fopen(child_abs, "rb");
        if (!f) {
            continue;
        }
        uint8_t buf[512];
        size_t n, total = 0;
        uint32_t crc = 0;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
            crc = crc32_update(crc, buf, n);
            total += n;
        }
        fclose(f);
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "path", child_rel);
        cJSON_AddNumberToObject(item, "size", (double)total);
        cJSON_AddNumberToObject(item, "crc32", (double)crc);
        cJSON_AddItemToArray(arr, item);
    }
    closedir(d);
}

char *board_manifest_json(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON *root = cJSON_CreateObject();
    cJSON *files = cJSON_AddArrayToObject(root, "files");
    add_files(files, LIVE_DIR, "");
    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    xSemaphoreGive(s_lock);
    return out;
}

esp_err_t board_update_settings_json(const char *json, size_t len)
{
    cJSON *incoming = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsObject(incoming)) {
        cJSON_Delete(incoming);
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = ESP_FAIL;
    char *text = read_text(LIVE_DIR "/board.json", JSON_MAX);
    cJSON *root = text ? cJSON_Parse(text) : NULL;
    free(text);
    if (root) {
        cJSON_ReplaceItemInObject(root, "settings", incoming);
        incoming = NULL;  // now owned by root
        char *out = cJSON_Print(root);
        FILE *f = out ? fopen(LIVE_DIR "/board.tmp", "wb") : NULL;
        if (f) {
            size_t n = strlen(out);
            bool ok = fwrite(out, 1, n, f) == n;
            fclose(f);
            if (ok) {
                unlink(LIVE_DIR "/board.json");
                err = rename(LIVE_DIR "/board.tmp", LIVE_DIR "/board.json") == 0 ? ESP_OK : ESP_FAIL;
            }
        }
        free(out);
        cJSON_Delete(root);
    }
    cJSON_Delete(incoming);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t board_restore_factory(void)
{
    if (!is_dir(FACTORY_DIR)) {
        return ESP_ERR_NOT_FOUND;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    rm_rf(LIVE_DIR);
    int rc = copy_tree(FACTORY_DIR, LIVE_DIR);
    xSemaphoreGive(s_lock);
    return rc == 0 ? ESP_OK : ESP_FAIL;
}

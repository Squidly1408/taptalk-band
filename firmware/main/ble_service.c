#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_ota_ops.h"
#include "esp_random.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "ble_service.h"
#include "board_store.h"
#include "events.h"
#include "power.h"

#define FW_VERSION "0.1.0"

static const char *TAG = "ble";

// Base UUID 9e1d0000-5b2c-4f7a-8a3e-6c7d2e1f0b0a, stored little-endian.
#define TT_UUID(n) BLE_UUID128_INIT(0x0a, 0x0b, 0x1f, 0x2e, 0x7d, 0x6c, 0x3e, 0x8a, \
                                    0x7a, 0x4f, 0x2c, 0x5b, (n) & 0xff, ((n) >> 8) & 0xff, 0x1d, 0x9e)

static const ble_uuid128_t UUID_SVC       = TT_UUID(0x0001);
static const ble_uuid128_t UUID_INFO      = TT_UUID(0x0002);
static const ble_uuid128_t UUID_MANIFEST  = TT_UUID(0x0003);
static const ble_uuid128_t UUID_FILE_CTRL = TT_UUID(0x0004);
static const ble_uuid128_t UUID_FILE_DATA = TT_UUID(0x0005);
static const ble_uuid128_t UUID_SETTINGS  = TT_UUID(0x0006);
static const ble_uuid128_t UUID_FIND_ME   = TT_UUID(0x0007);
static const ble_uuid128_t UUID_OTA_CTRL  = TT_UUID(0x0008);
static const ble_uuid128_t UUID_OTA_DATA  = TT_UUID(0x0009);

static uint16_t s_file_ctrl_handle, s_ota_ctrl_handle;
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint8_t s_own_addr_type;
static bool s_advertising, s_allow_pairing, s_synced;
static uint32_t s_pending_window_s = UINT32_MAX;  // window requested before host sync
static char s_name[20];

// OTA state
static esp_ota_handle_t s_ota;
static const esp_partition_t *s_ota_part;
static uint32_t s_ota_size, s_ota_written;
static uint16_t s_ota_seq;
static bool s_ota_active;

static int gap_event(struct ble_gap_event *event, void *arg);

// ---------- helpers ----------

static void notify_json(uint16_t handle, const char *json)
{
    if (s_conn == BLE_HS_CONN_HANDLE_NONE || !json) {
        return;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(json, strlen(json));
    if (om) {
        ble_gatts_notify_custom(s_conn, handle, om);
    }
}

static void notify_result(uint16_t handle, const char *op, const char *path, const char *err)
{
    char buf[160];
    if (err) {
        snprintf(buf, sizeof buf, "{\"ok\":false,\"op\":\"%s\",\"err\":\"%s\"}", op, err);
    } else if (path) {
        snprintf(buf, sizeof buf, "{\"ok\":true,\"op\":\"%s\",\"path\":\"%s\"}", op, path);
    } else {
        snprintf(buf, sizeof buf, "{\"ok\":true,\"op\":\"%s\"}", op);
    }
    notify_json(handle, buf);
}

void ble_service_notify_file_result(const char *json)
{
    notify_json(s_file_ctrl_handle, json);
}

static int append_and_free(struct ble_gatt_access_ctxt *ctxt, char *s)
{
    if (!s) {
        return BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    int rc = os_mbuf_append(ctxt->om, s, strlen(s));
    cJSON_free(s);
    return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

// ---------- characteristic handlers ----------

static int handle_file_ctrl(const char *json, size_t len)
{
    cJSON *msg = cJSON_ParseWithLength(json, len);
    cJSON *op = cJSON_GetObjectItem(msg, "op");
    if (!cJSON_IsString(op)) {
        notify_result(s_file_ctrl_handle, "?", NULL, "bad_json");
        cJSON_Delete(msg);
        return 0;
    }
    const char *o = op->valuestring;
    if (!strcmp(o, "session")) {
        cJSON *mode = cJSON_GetObjectItem(msg, "mode");
        bool patch = !cJSON_IsString(mode) || strcmp(mode->valuestring, "replace") != 0;
        events_post(EVT_BLE_SESSION, patch);  // copying files is slow: done in the main task
    } else if (!strcmp(o, "begin")) {
        cJSON *path = cJSON_GetObjectItem(msg, "path");
        cJSON *size = cJSON_GetObjectItem(msg, "size");
        cJSON *crc = cJSON_GetObjectItem(msg, "crc32");
        if (!cJSON_IsString(path) || !cJSON_IsNumber(size) || !cJSON_IsNumber(crc)) {
            notify_result(s_file_ctrl_handle, o, NULL, "bad_json");
        } else {
            const char *err = board_stage_file_begin(path->valuestring, (uint32_t)size->valuedouble,
                                                     (uint32_t)crc->valuedouble);
            notify_result(s_file_ctrl_handle, o, err ? NULL : path->valuestring, err);
        }
    } else if (!strcmp(o, "end")) {
        notify_result(s_file_ctrl_handle, o, NULL, board_stage_file_end());
    } else if (!strcmp(o, "delete")) {
        cJSON *path = cJSON_GetObjectItem(msg, "path");
        const char *err = cJSON_IsString(path) ? board_stage_delete(path->valuestring) : "bad_json";
        notify_result(s_file_ctrl_handle, o, NULL, err);
    } else if (!strcmp(o, "commit")) {
        events_post(EVT_BLE_COMMIT, 0);  // validated and swapped in the main task
    } else if (!strcmp(o, "abort")) {
        board_stage_abort();
        notify_result(s_file_ctrl_handle, o, NULL, NULL);
    } else {
        notify_result(s_file_ctrl_handle, o, NULL, "bad_json");
    }
    cJSON_Delete(msg);
    return 0;
}

static void handle_ota_ctrl(const char *json, size_t len)
{
    cJSON *msg = cJSON_ParseWithLength(json, len);
    cJSON *op = cJSON_GetObjectItem(msg, "op");
    const char *o = cJSON_IsString(op) ? op->valuestring : "?";
    if (!strcmp(o, "begin")) {
        cJSON *size = cJSON_GetObjectItem(msg, "size");
        s_ota_part = esp_ota_get_next_update_partition(NULL);
        if (!cJSON_IsNumber(size) || !s_ota_part || size->valuedouble > s_ota_part->size) {
            notify_result(s_ota_ctrl_handle, o, NULL, "size");
        } else if (esp_ota_begin(s_ota_part, OTA_WITH_SEQUENTIAL_WRITES, &s_ota) != ESP_OK) {
            notify_result(s_ota_ctrl_handle, o, NULL, "io");
        } else {
            s_ota_size = (uint32_t)size->valuedouble;
            s_ota_written = 0;
            s_ota_seq = 0;
            s_ota_active = true;
            notify_result(s_ota_ctrl_handle, o, NULL, NULL);
        }
    } else if (!strcmp(o, "end")) {
        const char *err = NULL;
        if (!s_ota_active) err = "no_session";
        else if (s_ota_written != s_ota_size) err = "size";
        else if (esp_ota_end(s_ota) != ESP_OK) err = "crc";
        else if (esp_ota_set_boot_partition(s_ota_part) != ESP_OK) err = "io";
        if (err && s_ota_active && s_ota_written != s_ota_size) {
            esp_ota_abort(s_ota);
        }
        s_ota_active = false;
        notify_result(s_ota_ctrl_handle, o, NULL, err);
        if (!err) {
            events_post(EVT_RESTART, 0);
        }
    } else if (!strcmp(o, "abort")) {
        if (s_ota_active) {
            esp_ota_abort(s_ota);
        }
        s_ota_active = false;
        notify_result(s_ota_ctrl_handle, o, NULL, NULL);
    } else {
        notify_result(s_ota_ctrl_handle, o, NULL, "bad_json");
    }
    cJSON_Delete(msg);
}

static int chr_access(uint16_t conn, uint16_t attr, struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    const ble_uuid_t *uuid = ctxt->chr->uuid;
    static uint8_t buf[520];
    uint16_t len = 0;

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (ble_uuid_cmp(uuid, &UUID_INFO.u) == 0) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddStringToObject(o, "name", s_name);
            cJSON_AddStringToObject(o, "fw", FW_VERSION);
            cJSON_AddNumberToObject(o, "battery", power_battery_percent());
            cJSON_AddBoolToObject(o, "charging", power_on_charger());
            cJSON_AddNumberToObject(o, "format", 1);
            char *s = cJSON_PrintUnformatted(o);
            cJSON_Delete(o);
            return append_and_free(ctxt, s);
        }
        if (ble_uuid_cmp(uuid, &UUID_MANIFEST.u) == 0) {
            return append_and_free(ctxt, board_manifest_json());
        }
        if (ble_uuid_cmp(uuid, &UUID_SETTINGS.u) == 0) {
            board_t *b = malloc(sizeof(board_t));
            char why[48];
            char *s = NULL;
            if (b && board_load(board_store_live_dir(), b, why, sizeof why) == ESP_OK) {
                cJSON *o = cJSON_CreateObject();
                cJSON_AddNumberToObject(o, "volume", b->settings.volume);
                cJSON_AddBoolToObject(o, "vibrate_only", b->settings.vibrate_only);
                if (b->settings.page_lock >= 0) {
                    cJSON_AddStringToObject(o, "page_lock", b->pages[b->settings.page_lock].id);
                } else {
                    cJSON_AddNullToObject(o, "page_lock");
                }
                cJSON_AddNumberToObject(o, "repeat_guard_ms", b->settings.repeat_guard_ms);
                cJSON_AddStringToObject(o, "idle_text", b->settings.idle_text);
                s = cJSON_PrintUnformatted(o);
                cJSON_Delete(o);
            }
            free(b);
            if (!s) {
                return os_mbuf_append(ctxt->om, "{}", 2) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return append_and_free(ctxt, s);
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
        return BLE_ATT_ERR_UNLIKELY;
    }
    if (ble_hs_mbuf_to_flat(ctxt->om, buf, sizeof buf - 1, &len) != 0) {
        return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
    }
    buf[len] = '\0';

    if (ble_uuid_cmp(uuid, &UUID_FILE_CTRL.u) == 0) {
        return handle_file_ctrl((const char *)buf, len);
    }
    if (ble_uuid_cmp(uuid, &UUID_FILE_DATA.u) == 0) {
        if (len < 3) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        uint16_t seq = buf[0] | (buf[1] << 8);
        const char *err = board_stage_file_data(seq, buf + 2, len - 2);
        if (err) {
            notify_result(s_file_ctrl_handle, "data", NULL, err);
        }
        return 0;
    }
    if (ble_uuid_cmp(uuid, &UUID_SETTINGS.u) == 0) {
        if (board_update_settings_json((const char *)buf, len) != ESP_OK) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        events_post(EVT_BLE_SETTINGS, 0);
        return 0;
    }
    if (ble_uuid_cmp(uuid, &UUID_FIND_ME.u) == 0) {
        events_post(EVT_BLE_FIND_ME, len ? buf[0] : 3);
        return 0;
    }
    if (ble_uuid_cmp(uuid, &UUID_OTA_CTRL.u) == 0) {
        handle_ota_ctrl((const char *)buf, len);
        return 0;
    }
    if (ble_uuid_cmp(uuid, &UUID_OTA_DATA.u) == 0) {
        if (!s_ota_active || len < 3) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        uint16_t seq = buf[0] | (buf[1] << 8);
        if (seq != s_ota_seq || esp_ota_write(s_ota, buf + 2, len - 2) != ESP_OK) {
            notify_result(s_ota_ctrl_handle, "data", NULL, seq != s_ota_seq ? "seq" : "io");
            esp_ota_abort(s_ota);
            s_ota_active = false;
            return 0;
        }
        s_ota_seq++;
        s_ota_written += len - 2;
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// Reads need an encrypted link; writes need an authenticated (passkey) bond.
#define R  (BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC)
#define W  (BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN)
#define WN (BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN)

static const struct ble_gatt_svc_def s_services[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &UUID_SVC.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            { .uuid = &UUID_INFO.u,      .access_cb = chr_access, .flags = R },
            { .uuid = &UUID_MANIFEST.u,  .access_cb = chr_access, .flags = R },
            { .uuid = &UUID_FILE_CTRL.u, .access_cb = chr_access, .flags = W | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &s_file_ctrl_handle },
            { .uuid = &UUID_FILE_DATA.u, .access_cb = chr_access, .flags = WN },
            { .uuid = &UUID_SETTINGS.u,  .access_cb = chr_access, .flags = R | W },
            { .uuid = &UUID_FIND_ME.u,   .access_cb = chr_access, .flags = W },
            { .uuid = &UUID_OTA_CTRL.u,  .access_cb = chr_access, .flags = W | BLE_GATT_CHR_F_NOTIFY,
              .val_handle = &s_ota_ctrl_handle },
            { .uuid = &UUID_OTA_DATA.u,  .access_cb = chr_access, .flags = WN },
            { 0 },
        },
    },
    { 0 },
};

// ---------- advertising ----------

static void advertise(int32_t duration_ms)
{
    struct ble_hs_adv_fields adv = { 0 };
    adv.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv.uuids128 = &UUID_SVC;
    adv.num_uuids128 = 1;
    adv.uuids128_is_complete = 1;
    ble_gap_adv_set_fields(&adv);

    struct ble_hs_adv_fields rsp = { 0 };
    rsp.name = (const uint8_t *)s_name;
    rsp.name_len = strlen(s_name);
    rsp.name_is_complete = 1;
    ble_gap_adv_rsp_set_fields(&rsp);

    struct ble_gap_adv_params params = {
        .conn_mode = BLE_GAP_CONN_MODE_UND,
        .disc_mode = BLE_GAP_DISC_MODE_GEN,
    };
    if (ble_gap_adv_start(s_own_addr_type, NULL, duration_ms, &params, gap_event, NULL) == 0) {
        s_advertising = true;
    }
}

void ble_service_open_window(uint32_t seconds)
{
    if (!s_synced) {
        s_pending_window_s = seconds;
        return;
    }
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
        return;  // already connected
    }
    if (s_advertising) {
        ble_gap_adv_stop();
    }
    advertise(seconds ? (int32_t)seconds * 1000 : BLE_HS_FOREVER);
}

void ble_service_close_window(void)
{
    s_pending_window_s = UINT32_MAX;
    if (s_advertising) {
        ble_gap_adv_stop();
        s_advertising = false;
    }
}

bool ble_service_active(void)
{
    return s_advertising || s_conn != BLE_HS_CONN_HANDLE_NONE || s_pending_window_s != UINT32_MAX;
}

void ble_service_allow_pairing(bool allow)
{
    s_allow_pairing = allow;
}

void ble_service_clear_bonds(void)
{
    ble_store_clear();
}

// ---------- GAP events ----------

static int gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        s_advertising = false;
        if (event->connect.status == 0) {
            s_conn = event->connect.conn_handle;
            ESP_LOGI(TAG, "connected");
        } else {
            events_post(EVT_BLE_IDLE, 0);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_conn = BLE_HS_CONN_HANDLE_NONE;
        if (board_stage_active()) {
            board_stage_abort();
        }
        if (s_ota_active) {
            esp_ota_abort(s_ota);
            s_ota_active = false;
        }
        if (power_on_charger()) {
            advertise(BLE_HS_FOREVER);
        } else {
            events_post(EVT_BLE_IDLE, 0);
        }
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        s_advertising = false;
        if (s_conn == BLE_HS_CONN_HANDLE_NONE) {
            events_post(EVT_BLE_IDLE, 0);
        }
        return 0;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        if (event->passkey.params.action == BLE_SM_IOACT_DISP) {
            if (!s_allow_pairing || !power_on_charger()) {
                ESP_LOGW(TAG, "pairing refused: not in pairing mode on the charger");
                ble_gap_terminate(event->passkey.conn_handle, BLE_ERR_AUTH_FAIL);
                return 0;
            }
            struct ble_sm_io io = { .action = BLE_SM_IOACT_DISP, .passkey = esp_random() % 1000000 };
            events_post(EVT_BLE_PASSKEY, (int32_t)io.passkey);
            ble_sm_inject_io(event->passkey.conn_handle, &io);
        }
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (s_allow_pairing) {
            events_post(EVT_BLE_PAIRING_DONE, event->enc_change.status == 0);
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING: {
        // A known device lost its bond: forget the old one and let it pair again
        // (still subject to pairing mode).
        struct ble_gap_conn_desc desc;
        if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
            ble_store_util_delete_peer(&desc.peer_id_addr);
        }
        return BLE_GAP_REPEAT_PAIRING_RETRY;
    }

    default:
        return 0;
    }
}

// ---------- host start-up ----------

static void on_sync(void)
{
    ble_hs_util_ensure_addr(0);
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    s_synced = true;
    if (s_pending_window_s != UINT32_MAX) {
        uint32_t s = s_pending_window_s;
        s_pending_window_s = UINT32_MAX;
        ble_service_open_window(s);
    }
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset, reason %d", reason);
    s_synced = false;
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_store_config_init(void);

void ble_service_init(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_BT);
    snprintf(s_name, sizeof s_name, "TapTalk-%02X%02X", mac[4], mac[5]);

    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;  // drop the oldest bond when full
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_DISP_ONLY;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_svc_gap_init();
    ble_svc_gatt_init();
    ESP_ERROR_CHECK(ble_gatts_count_cfg(s_services));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(s_services));
    ble_svc_gap_device_name_set(s_name);
    ble_store_config_init();

    nimble_port_freertos_init(host_task);
}

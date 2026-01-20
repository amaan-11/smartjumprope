#include "jr_ble.h"

#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "JR_BLE";

// UUIDs must match your webapp ble.js
static const ble_uuid128_t JR_SVC_UUID  = BLE_UUID128_INIT(
    0x6e,0x40,0x00,0x01,0xb5,0xa3,0xf3,0x93,0xe0,0xa9,0xe5,0x0e,0x24,0xdc,0xca,0x9e
);
static const ble_uuid128_t JR_CTRL_UUID = BLE_UUID128_INIT(
    0x6e,0x40,0x00,0x02,0xb5,0xa3,0xf3,0x93,0xe0,0xa9,0xe5,0x0e,0x24,0xdc,0xca,0x9e
);
static const ble_uuid128_t JR_DATA_UUID = BLE_UUID128_INIT(
    0x6e,0x40,0x00,0x03,0xb5,0xa3,0xf3,0x93,0xe0,0xa9,0xe5,0x0e,0x24,0xdc,0xca,0x9e
);

static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_data_val_handle = 0;

static volatile bool     g_streaming  = false;
static volatile uint32_t g_jump_count = 0;
static volatile uint8_t  g_hr_bpm     = 0;
static volatile uint16_t g_accel_mag  = 0;

static void start_advertising(void);

// 12-byte packet LE:
// u32 ts_ms, u32 jumps, u8 hr, u16 accel, u8 flags
static void build_packet(uint8_t out[12], uint32_t ts, uint32_t jumps, uint8_t hr, uint16_t accel, uint8_t flags) {
    out[0] = (uint8_t)(ts);
    out[1] = (uint8_t)(ts >> 8);
    out[2] = (uint8_t)(ts >> 16);
    out[3] = (uint8_t)(ts >> 24);

    out[4] = (uint8_t)(jumps);
    out[5] = (uint8_t)(jumps >> 8);
    out[6] = (uint8_t)(jumps >> 16);
    out[7] = (uint8_t)(jumps >> 24);

    out[8] = hr;

    out[9]  = (uint8_t)(accel);
    out[10] = (uint8_t)(accel >> 8);

    out[11] = flags;
}

// Control write: 0x01 start, 0x00 stop
static int ctrl_access_cb(uint16_t, uint16_t, ble_gatt_access_ctxt *ctxt, void *) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) return BLE_ATT_ERR_UNLIKELY;

    uint8_t cmd = 0;
    if (OS_MBUF_PKTLEN(ctxt->om) < 1) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

    if (ble_hs_mbuf_to_flat(ctxt->om, &cmd, sizeof(cmd), nullptr) != 0) return BLE_ATT_ERR_UNLIKELY;

    if (cmd == 0x01) { g_streaming = true;  ESP_LOGI(TAG, "Streaming START"); }
    else if (cmd == 0x00) { g_streaming = false; ESP_LOGI(TAG, "Streaming STOP"); }
    else { ESP_LOGW(TAG, "Unknown cmd 0x%02X", cmd); }

    return 0;
}

// Data characteristic: allow read (optional), notify happens in task
static int data_access_cb(uint16_t, uint16_t, ble_gatt_access_ctxt *ctxt, void *) {
    if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) return BLE_ATT_ERR_UNLIKELY;

    uint8_t pkt[12];
    uint32_t ts = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    build_packet(pkt, ts, (uint32_t)g_jump_count, (uint8_t)g_hr_bpm, (uint16_t)g_accel_mag, 0x01);

    return (os_mbuf_append(ctxt->om, pkt, sizeof(pkt)) == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static ble_gatt_chr_def gatt_chars[] = {
    {
        /* .uuid */      (ble_uuid_t *)&JR_CTRL_UUID,
        /* .access_cb */ ctrl_access_cb,
        /* .arg */       nullptr,
        /* .descriptors */ nullptr,
        /* .flags */     BLE_GATT_CHR_F_WRITE,
        /* .min_key_size */ 0,
        /* .val_handle */ nullptr,
        /* .cpfd */      nullptr
    },
    {
        /* .uuid */      (ble_uuid_t *)&JR_DATA_UUID,
        /* .access_cb */ data_access_cb,
        /* .arg */       nullptr,
        /* .descriptors */ nullptr,
        /* .flags */     (uint16_t)(BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ),
        /* .min_key_size */ 0,
        /* .val_handle */ &g_data_val_handle,
        /* .cpfd */      nullptr
    },
    { 0 } // terminator
};

static const ble_gatt_svc_def gatt_svcs[] = {
    {
        /* .type */ BLE_GATT_SVC_TYPE_PRIMARY,
        /* .uuid */ (ble_uuid_t *)&JR_SVC_UUID,
        /* .includes */ nullptr,
        /* .characteristics */ gatt_chars
    },
    { 0 } // terminator
};


static int gap_event_cb(ble_gap_event *event, void *) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                g_conn_handle = event->connect.conn_handle;
                ESP_LOGI(TAG, "Connected (handle=%d)", g_conn_handle);
            } else {
                start_advertising();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "Disconnected");
            g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            g_streaming = false;
            start_advertising();
            return 0;

        case BLE_GAP_EVENT_ADV_COMPLETE:
            start_advertising();
            return 0;

        default:
            return 0;
    }
}

static void start_advertising(void) {
    ble_gap_adv_params adv_params{};
    ble_hs_adv_fields fields{};
    std::memset(&fields, 0, sizeof(fields));

    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    const char *name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)name;
    fields.name_len = (uint8_t)std::strlen(name);
    fields.name_is_complete = 1;

    // CRITICAL: include service UUID in advertising (so Chrome filter finds it)
    fields.uuids128 = &JR_SVC_UUID;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    if (ble_gap_adv_set_fields(&fields) != 0) {
        ESP_LOGE(TAG, "adv_set_fields failed");
        return;
    }

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    int rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, nullptr, BLE_HS_FOREVER, &adv_params, gap_event_cb, nullptr);
    if (rc == 0) ESP_LOGI(TAG, "Advertising as '%s'", name);
    else ESP_LOGE(TAG, "adv_start rc=%d", rc);
}

static void ble_on_sync(void) {
    ble_svc_gap_device_name_set("JRope-C6");
    start_advertising();
}

static void notify_task(void *) {
    while (true) {
        if (g_streaming && g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
            uint8_t pkt[12];
            uint32_t ts = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);

            build_packet(pkt, ts, (uint32_t)g_jump_count, (uint8_t)g_hr_bpm, (uint16_t)g_accel_mag, 0x01);

            os_mbuf *om = ble_hs_mbuf_from_flat(pkt, sizeof(pkt));
            if (om) {
                int rc = ble_gatts_notify_custom(g_conn_handle, g_data_val_handle, om);
                if (rc != 0) ESP_LOGW(TAG, "notify rc=%d", rc);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100)); // 10 Hz
    }
}

static void host_task(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void jr_ble_init(void) {
    // NVS is required for BLE
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    nimble_port_init();
    ble_hs_cfg.sync_cb = ble_on_sync;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    ESP_ERROR_CHECK(ble_gatts_count_cfg(gatt_svcs));
    ESP_ERROR_CHECK(ble_gatts_add_svcs(gatt_svcs));

    xTaskCreate(notify_task, "jr_notify", 4096, nullptr, 5, nullptr);
    nimble_port_freertos_init(host_task);
}

void jr_ble_set_values(uint32_t jump_count, uint8_t heart_rate_bpm, uint16_t accel_mag) {
    g_jump_count = jump_count;
    g_hr_bpm = heart_rate_bpm;
    g_accel_mag = accel_mag;
}

bool jr_ble_is_streaming(void) {
    return g_streaming;
}

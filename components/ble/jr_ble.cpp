#include "jr_ble.h"

#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "JR_BLE";

/*
 Web UUID:
 6e400001-b5a3-f393-e0a9-e50e24dcca9e
 (stored little-endian)
*/
static const ble_uuid128_t JR_SVC_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3,
                     0xa3, 0xb5, 0x01, 0x00, 0x40, 0x6e);

static const ble_uuid128_t JR_CTRL_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3,
                     0xa3, 0xb5, 0x02, 0x00, 0x40, 0x6e);

static const ble_uuid128_t JR_DATA_UUID =
    BLE_UUID128_INIT(0x9e, 0xca, 0xdc, 0x24, 0x0e, 0xe5, 0xa9, 0xe0, 0x93, 0xf3,
                     0xa3, 0xb5, 0x03, 0x00, 0x40, 0x6e);

/* =========================
   STATE (TRANSPORT ONLY)
   ========================= */

static uint8_t own_addr_type = 0;
static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_data_val_handle = 0;

static bool g_streaming = false;
static uint8_t g_hr_bpm = 0;
static uint8_t g_spo2 = 0;

/* =========================
   PACKET
   =========================
   3 bytes:
   [0] version = 1
   [1] heart rate (BPM)
   [2] SpO2 (%)
*/
static void build_packet(uint8_t out[3]) {
  out[0] = 1;
  out[1] = g_hr_bpm;
  out[2] = g_spo2;
}

/* =========================
   GATT CALLBACKS
   ========================= */

static int ctrl_access_cb(uint16_t, uint16_t, ble_gatt_access_ctxt *ctxt,
                          void *) {
  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR)
    return BLE_ATT_ERR_UNLIKELY;

  uint8_t cmd = 0;
  if (OS_MBUF_PKTLEN(ctxt->om) < 1)
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

  if (ble_hs_mbuf_to_flat(ctxt->om, &cmd, sizeof(cmd), nullptr) != 0)
    return BLE_ATT_ERR_UNLIKELY;

  g_streaming = (cmd == 0x01);
  ESP_LOGI(TAG, "Streaming %s", g_streaming ? "START" : "STOP");

  return 0;
}

static int data_access_cb(uint16_t, uint16_t, ble_gatt_access_ctxt *ctxt,
                          void *) {
  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR)
    return BLE_ATT_ERR_UNLIKELY;

  uint8_t pkt[3];
  build_packet(pkt);

  return os_mbuf_append(ctxt->om, pkt, sizeof(pkt)) == 0
             ? 0
             : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* =========================
   GATT TABLE
   ========================= */

static ble_gatt_chr_def gatt_chars[] = {
    {
        (ble_uuid_t *)&JR_CTRL_UUID,
        ctrl_access_cb,
        nullptr,
        nullptr,
        BLE_GATT_CHR_F_WRITE,
        0,
        nullptr,
        nullptr,
    },
    {
        (ble_uuid_t *)&JR_DATA_UUID,
        data_access_cb,
        nullptr,
        nullptr,
        (uint16_t)(BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ),
        0,
        &g_data_val_handle,
        nullptr,
    },
    {nullptr, nullptr, nullptr, nullptr, 0, 0, nullptr, nullptr},
};

static const ble_gatt_svc_def gatt_svcs[] = {
    {
        BLE_GATT_SVC_TYPE_PRIMARY,
        (ble_uuid_t *)&JR_SVC_UUID,
        nullptr,
        gatt_chars,
    },
    {0, nullptr, nullptr, nullptr},
};

/* =========================
   GAP
   ========================= */

static void start_advertising(void);

static int gap_event_cb(ble_gap_event *event, void *) {
  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      g_conn_handle = event->connect.conn_handle;
      ESP_LOGI(TAG, "Connected");
    } else {
      start_advertising();
    }
    break;

  case BLE_GAP_EVENT_DISCONNECT:
    g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    g_streaming = false;
    start_advertising();
    break;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    start_advertising();
    break;

  default:
    break;
  }
  return 0;
}

static void start_advertising(void) {
  ble_gap_adv_params adv{};
  ble_hs_adv_fields fields{};
  std::memset(&fields, 0, sizeof(fields));

  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  const char *name = ble_svc_gap_device_name();
  fields.name = (uint8_t *)name;
  fields.name_len = strlen(name);
  fields.name_is_complete = 1;

  fields.uuids128 = &JR_SVC_UUID;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;

  ble_gap_adv_set_fields(&fields);

  adv.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv.disc_mode = BLE_GAP_DISC_MODE_GEN;

  ble_gap_adv_start(own_addr_type, nullptr, BLE_HS_FOREVER, &adv, gap_event_cb,
                    nullptr);
}

static void ble_on_sync(void) {
  ble_hs_id_infer_auto(0, &own_addr_type);
  ble_svc_gap_device_name_set("JRope-C6");
  start_advertising();
}

/* =========================
   TASKS
   ========================= */

static void notify_task(void *) {
  while (true) {
    if (g_streaming && g_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
      uint8_t pkt[3];
      build_packet(pkt);

      os_mbuf *om = ble_hs_mbuf_from_flat(pkt, sizeof(pkt));
      if (om) {
        ble_gatts_notify_custom(g_conn_handle, g_data_val_handle, om);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(200)); // 5 Hz
  }
}

static void host_task(void *) {
  nimble_port_run();
  nimble_port_freertos_deinit();
}

/* =========================
   PUBLIC API
   ========================= */

void jr_ble_init(void) {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  nimble_port_init();
  ble_hs_cfg.sync_cb = ble_on_sync;

  ble_svc_gap_init();
  ble_svc_gatt_init();

  ble_gatts_count_cfg(gatt_svcs);
  ble_gatts_add_svcs(gatt_svcs);

  xTaskCreate(notify_task, "jr_notify", 4096, nullptr, 5, nullptr);
  nimble_port_freertos_init(host_task);
}

void jr_ble_set_values(uint8_t heart_rate_bpm, uint8_t spo2_percent) {
  g_hr_bpm = heart_rate_bpm;
  g_spo2 = spo2_percent;
}

bool jr_ble_is_streaming(void) { return g_streaming; }

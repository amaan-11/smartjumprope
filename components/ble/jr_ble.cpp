#include "jr_ble.h"

/*
 * components/ble/jr_ble.cpp
 *
 * NimBLE GATT server for ESP32-C6.
 *
 * Goal: stream workout data to the browser via Web Bluetooth notifications.
 *
 * Characteristics:
 * - Control (UUID ...0002): write 0x01 => Start streaming; write 0x00 => Stop
 * streaming
 * - Data    (UUID ...0003): notify/read => jr_packet_v1_t (12 bytes)
 *
 * Notes:
 * - Packet is little-endian (frontend uses DataView.getUint32(..., true)).
 * - "Reset on Start" is implemented using a baseline: transmitted jump_count
 * becomes (jump_total - baseline_at_start), while internal counter can stay
 * cumulative.
 */

#include <cstring>
#include <inttypes.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
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
 UUIDs (match frontend; Nordic UART style / NUS-like)
 Service: 6e400001-b5a3-f393-e0a9-e50e24dcca9e
 Control: 6e400002-b5a3-f393-e0a9-e50e24dcca9e
 Data:    6e400003-b5a3-f393-e0a9-e50e24dcca9e

 Important: BLE_UUID128_INIT uses the little-endian byte order of the UUID.
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
   CONFIG (demo-friendly)
   ========================= */

// Default notify frequency (Hz). Override with JR_BLE_NOTIFY_HZ at build time.
#ifndef JR_BLE_NOTIFY_HZ
#define JR_BLE_NOTIFY_HZ 10
#endif

#if (JR_BLE_NOTIFY_HZ <= 0)
#error "JR_BLE_NOTIFY_HZ must be > 0"
#endif

static inline TickType_t notify_period_ticks(void) {
  const uint32_t period_ms = (uint32_t)(1000 / JR_BLE_NOTIFY_HZ);
  const uint32_t safe_ms = (period_ms == 0) ? 1 : period_ms;
  return pdMS_TO_TICKS(safe_ms);
}

/* =========================
   STATE (transport-only)
   ========================= */

static uint8_t g_own_addr_type = 0;
static uint16_t g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t g_data_val_handle = 0;

static bool g_streaming = false;

// "Reset-on-start" implemented via baseline
static bool g_reset_on_start = true;
static uint32_t g_jump_baseline = 0;

// Sensor snapshot (updated by the rest of firmware)
static uint32_t g_jump_total = 0;
static uint8_t g_hr_bpm = 0;
static uint16_t g_accel_mag = 0;
static uint8_t g_flags = 0;

// Simple critical section to make snapshot reads/writes consistent
static portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;

/* =========================
   PACKET BUILDER
   ========================= */

static inline uint32_t uptime_ms(void) {
  // esp_timer_get_time() returns microseconds since boot
  return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void build_packet(jr_packet_v1_t *out) {
  uint32_t jump_total;
  uint8_t hr;
  uint16_t accel;
  uint8_t flags;
  uint32_t baseline;
  bool reset_on_start;

  portENTER_CRITICAL(&g_lock);
  jump_total = g_jump_total;
  hr = g_hr_bpm;
  accel = g_accel_mag;
  flags = g_flags;
  baseline = g_jump_baseline;
  reset_on_start = g_reset_on_start;
  portEXIT_CRITICAL(&g_lock);

  uint32_t jump_to_send = jump_total;

  if (reset_on_start) {
    // Avoid underflow if total counter changes unexpectedly
    jump_to_send =
        (jump_total >= baseline) ? (jump_total - baseline) : jump_total;
  }

  out->timestamp_ms = uptime_ms();
  out->jump_count = jump_to_send;
  out->heart_rate_bpm = hr;
  out->accel_mag = accel;
  out->flags = flags;
}

/* =========================
   GATT CALLBACKS
   ========================= */

static int ctrl_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  if (ctxt->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  uint8_t cmd = 0;
  if (OS_MBUF_PKTLEN(ctxt->om) < 1) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  if (ble_hs_mbuf_to_flat(ctxt->om, &cmd, sizeof(cmd), NULL) != 0) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  if (cmd == 0x01) {
    g_streaming = true;

    // Capture baseline at START (transmitted jumps will begin at 0)
    portENTER_CRITICAL(&g_lock);
    g_jump_baseline = g_jump_total;
    portEXIT_CRITICAL(&g_lock);

    ESP_LOGI(TAG, "Streaming START (reset_on_start=%d, baseline=%" PRIu32 ")",
             (int)g_reset_on_start, (uint32_t)g_jump_baseline);
  } else if (cmd == 0x00) {
    g_streaming = false;
    ESP_LOGI(TAG, "Streaming STOP");
  } else {
    ESP_LOGW(TAG, "Unknown control cmd: 0x%02X", cmd);
  }

  return 0;
}

static int data_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg) {
  (void)conn_handle;
  (void)attr_handle;
  (void)arg;

  if (ctxt->op != BLE_GATT_ACCESS_OP_READ_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }

  jr_packet_v1_t pkt;
  build_packet(&pkt);

  return (os_mbuf_append(ctxt->om, &pkt, sizeof(pkt)) == 0)
             ? 0
             : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/* =========================
   GATT TABLE
   ========================= */

static struct ble_gatt_chr_def gatt_chars[] = {
    {
        (ble_uuid_t *)&JR_CTRL_UUID,
        ctrl_access_cb,
        NULL,
        NULL,
        (uint16_t)(BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP),
        0,
        NULL,
        NULL,
    },
    {
        (ble_uuid_t *)&JR_DATA_UUID,
        data_access_cb,
        NULL,
        NULL,
        (uint16_t)(BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ),
        0,
        &g_data_val_handle,
        NULL,
    },
    {0} // terminator
};

static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        BLE_GATT_SVC_TYPE_PRIMARY,
        (ble_uuid_t *)&JR_SVC_UUID,
        NULL,
        gatt_chars,
    },
    {0} // terminator
};

/* =========================
   GAP / Advertising
   ========================= */

static void start_advertising(void);

static int gap_event_cb(struct ble_gap_event *event, void *arg) {
  (void)arg;

  switch (event->type) {
  case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
      g_conn_handle = event->connect.conn_handle;
      ESP_LOGI(TAG, "Connected (handle=%d)", (int)g_conn_handle);
    } else {
      ESP_LOGW(TAG, "Connect failed; status=%d", event->connect.status);
      start_advertising();
    }
    return 0;

  case BLE_GAP_EVENT_DISCONNECT:
    ESP_LOGI(TAG, "Disconnected; reason=%d", event->disconnect.reason);
    g_conn_handle = BLE_HS_CONN_HANDLE_NONE;
    g_streaming = false;
    start_advertising();
    return 0;

  case BLE_GAP_EVENT_ADV_COMPLETE:
    // Advertising ended; restart (demo).
    start_advertising();
    return 0;

  case BLE_GAP_EVENT_SUBSCRIBE:
    ESP_LOGI(
        TAG, "Subscribe: conn=%d attr=%d notify=%d indicate=%d",
        (int)event->subscribe.conn_handle, (int)event->subscribe.attr_handle,
        (int)event->subscribe.cur_notify, (int)event->subscribe.cur_indicate);
    return 0;

  default:
    return 0;
  }
}

static void start_advertising(void) {
  struct ble_gap_adv_params adv;
  struct ble_hs_adv_fields fields;

  memset(&adv, 0, sizeof(adv));
  memset(&fields, 0, sizeof(fields));

  fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

  // Device name in advertising
  const char *name = ble_svc_gap_device_name();
  fields.name = (uint8_t *)name;
  fields.name_len = (uint8_t)strlen(name);
  fields.name_is_complete = 1;

  // IMPORTANT for Web Bluetooth service filter:
  // requestDevice({filters:[{services:[JR_SERVICE_UUID]}]}) needs the service
  // UUID in advertising.
  fields.uuids128 = (ble_uuid128_t *)&JR_SVC_UUID;
  fields.num_uuids128 = 1;
  fields.uuids128_is_complete = 1;

  int rc = ble_gap_adv_set_fields(&fields);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_gap_adv_set_fields failed: rc=%d", rc);
  }

  adv.conn_mode = BLE_GAP_CONN_MODE_UND;
  adv.disc_mode = BLE_GAP_DISC_MODE_GEN;

  rc = ble_gap_adv_start(g_own_addr_type, NULL, BLE_HS_FOREVER, &adv,
                         gap_event_cb, NULL);
  if (rc != 0) {
    ESP_LOGE(TAG, "ble_gap_adv_start failed: rc=%d", rc);
  } else {
    ESP_LOGI(TAG, "Advertising...");
  }
}

static void ble_on_sync(void) {
  ble_hs_id_infer_auto(0, &g_own_addr_type);

  // This is what shows as device.name in the browser chooser (can be changed).
  ble_svc_gap_device_name_set("JRope-C6");

  start_advertising();
}

static void ble_on_reset(int reason) {
  ESP_LOGE(TAG, "BLE reset; reason=%d", reason);
}

/* =========================
   TASKS
   ========================= */

static void notify_task(void *param) {
  (void)param;

  const TickType_t period = notify_period_ticks();
  jr_packet_v1_t pkt;

  while (true) {
    if (g_streaming && g_conn_handle != BLE_HS_CONN_HANDLE_NONE &&
        g_data_val_handle != 0) {
      build_packet(&pkt);

      // Send 12-byte binary payload
      struct os_mbuf *om = ble_hs_mbuf_from_flat(&pkt, sizeof(pkt));
      if (om) {
        int rc = ble_gatts_notify_custom(g_conn_handle, g_data_val_handle, om);
        if (rc != 0) {
          ESP_LOGD(TAG, "notify rc=%d", rc);
        }
      }
    }
    vTaskDelay(period);
  }
}

static void host_task(void *param) {
  (void)param;
  nimble_port_run();
  nimble_port_freertos_deinit();
}

/* =========================
   PUBLIC API
   ========================= */

void jr_ble_init(void) {
  // BLE uses NVS internally
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ESP_ERROR_CHECK(nvs_flash_init());
  } else {
    ESP_ERROR_CHECK(ret);
  }

  nimble_port_init();

  ble_svc_gap_init();
  ble_svc_gatt_init();

  ble_gatts_count_cfg(gatt_svcs);
  ble_gatts_add_svcs(gatt_svcs);

  ble_hs_cfg.sync_cb = ble_on_sync;
  ble_hs_cfg.reset_cb = ble_on_reset;

  xTaskCreate(notify_task, "jr_notify", 4096, NULL, 5, NULL);
  nimble_port_freertos_init(host_task);

  ESP_LOGI(TAG, "jr_ble_init ok (notify_hz=%d)", (int)JR_BLE_NOTIFY_HZ);
}

void jr_ble_set_sensor_snapshot(uint32_t jump_count_total,
                                uint8_t heart_rate_bpm, uint16_t accel_mag,
                                uint8_t flags) {
  portENTER_CRITICAL(&g_lock);
  g_jump_total = jump_count_total;
  g_hr_bpm = heart_rate_bpm;
  g_accel_mag = accel_mag;
  g_flags = flags;
  portEXIT_CRITICAL(&g_lock);
}

void jr_ble_set_reset_on_start(bool enable) {
  portENTER_CRITICAL(&g_lock);
  g_reset_on_start = enable;
  portEXIT_CRITICAL(&g_lock);
}

bool jr_ble_is_streaming(void) { return g_streaming; }

bool jr_ble_is_connected(void) {
  return g_conn_handle != BLE_HS_CONN_HANDLE_NONE;
}
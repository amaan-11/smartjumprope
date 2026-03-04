#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2cInit.h"
#include "jump.h"
#include "jr_ble.h"
#include "mutex.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

/* =========================
   CONFIGURATION
   ========================= */

#define JUMP_THRESHOLD_FACTOR 1.3f
#define MIN_JUMP_INTERVAL_MS 300

#define JUMP_UPDATE_HZ 100
#define DISPLAY_UPDATE_HZ 4
#define BLE_SNAPSHOT_HZ 20

/* =========================
   GLOBALS
   ========================= */

static const char *TAG = "MAIN";

static OledDisplay *display = nullptr;
static JumpDetector *jumpDetector = nullptr;

static SemaphoreHandle_t dataMutex = nullptr;

// Cumulative values (since boot)
static uint32_t g_jump_count_total = 0;
static uint32_t g_last_jump_ts_ms = 0;

// Optional sensor detail for UI/BLE
static uint16_t g_last_accel_mag = 0;

/* =========================
   HELPERS
   ========================= */

// Reads raw MPU data and computes a simple acceleration magnitude in raw units.
// This is demo-friendly and avoids changing the gyro component API.
static bool read_accel_magnitude(uint16_t &out_mag) {
  SensorReading &sr = SensorReading::getInstance();

  int16_t ax = 0, ay = 0, az = 0;
  int16_t gx = 0, gy = 0, gz = 0;

  if (sr.readRaw(ax, ay, az, gx, gy, gz) != ESP_OK) {
    return false;
  }

  const int32_t ax32 = ax;
  const int32_t ay32 = ay;
  const int32_t az32 = az;

  const uint32_t sumsq =
      (uint32_t)(ax32 * ax32) + (uint32_t)(ay32 * ay32) + (uint32_t)(az32 * az32);

  // sqrtf on float is fine for demo use.
  const uint32_t mag = (uint32_t)std::sqrt((double)sumsq);

  out_mag = (mag > 0xFFFFu) ? 0xFFFFu : (uint16_t)mag;
  return true;
}

/* =========================
   TASK: JUMP DETECTION
   ========================= */

void jumpTask(void *param) {
  (void)param;
  ESP_LOGI(TAG, "Jump task started (%d Hz)", JUMP_UPDATE_HZ);

  while (true) {
    if (jumpDetector) {
      // Update detector (may enqueue jump timestamps internally)
      jumpDetector->update();

      // Drain queued jump events and convert them into a cumulative counter
      uint32_t ts = 0;
      while (jumpDetector->getJump(ts, 0)) {
        MutexGuard lock(dataMutex);
        g_jump_count_total++;
        g_last_jump_ts_ms = ts;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1000 / JUMP_UPDATE_HZ));
  }
}

/* =========================
   TASK: BLE SNAPSHOT FEED
   ========================= */

void bleSnapshotTask(void *param) {
  (void)param;
  ESP_LOGI(TAG, "BLE snapshot task started (%d Hz)", BLE_SNAPSHOT_HZ);

  while (true) {
    uint32_t jumps = 0;
    uint16_t accel_mag = 0;

    // Read accel magnitude (optional)
    if (!read_accel_magnitude(accel_mag)) {
      accel_mag = 0;
    }

    {
      MutexGuard lock(dataMutex);
      jumps = g_jump_count_total;
      g_last_accel_mag = accel_mag;
    }

    // Heart rate is not wired into this firmware path yet => 0 means invalid/no reading
    const uint8_t heart_rate_bpm = 0;

    // Flags are free to define; keep 0 for demo simplicity
    const uint8_t flags = 0;

    // BLE layer will baseline-reset the transmitted jump_count on START if enabled,
    // but it expects this "jumps" value to be cumulative since boot.
    jr_ble_set_sensor_snapshot(jumps, heart_rate_bpm, accel_mag, flags);

    vTaskDelay(pdMS_TO_TICKS(1000 / BLE_SNAPSHOT_HZ));
  }
}

/* =========================
   TASK: DISPLAY
   ========================= */

void displayTask(void *param) {
  (void)param;
  ESP_LOGI(TAG, "Display task started");

  char line0[32];
  char line1[32];
  char line2[32];
  char line3[32];

  while (true) {
    uint32_t jumps = 0;
    uint32_t lastTs = 0;
    uint16_t accel = 0;

    {
      MutexGuard lock(dataMutex);
      jumps = g_jump_count_total;
      lastTs = g_last_jump_ts_ms;
      accel = g_last_accel_mag;
    }

    const bool connected = jr_ble_is_connected();
    const bool streaming = jr_ble_is_streaming();

    snprintf(line0, sizeof(line0), "Jumps: %lu", (unsigned long)jumps);
    snprintf(line1, sizeof(line1), "AccelMag: %u", (unsigned)accel);
    snprintf(line2, sizeof(line2), "LastJump: %lu ms", (unsigned long)lastTs);

    if (streaming) snprintf(line3, sizeof(line3), "BLE: streaming");
    else if (connected) snprintf(line3, sizeof(line3), "BLE: connected");
    else snprintf(line3, sizeof(line3), "BLE: idle");

    if (display) {
      display->clear();
      display->drawString(0, 0, line0);
      display->drawString(0, 16, line1);
      display->drawString(0, 32, line2);
      display->drawString(0, 48, line3);
      display->commit();
    }

    vTaskDelay(pdMS_TO_TICKS(1000 / DISPLAY_UPDATE_HZ));
  }
}

/* =========================
   INIT TASK
   ========================= */

void initTask(void *param) {
  (void)param;
  ESP_LOGI(TAG, "Initialization start");

  dataMutex = xSemaphoreCreateMutex();
  configASSERT(dataMutex != nullptr);

  // IMPORTANT: I2C must be initialized before SensorReading::getInstance()
  I2CManager &i2c = I2CManager::getInstance();
  i2c.init();
  configASSERT(i2c.isInitialized());
  vTaskDelay(pdMS_TO_TICKS(100));

  // Create/initialize the singleton after I2C is ready
  SensorReading &sr = SensorReading::getInstance();
  configASSERT(sr.isInitialized());

  // Display
  display = new OledDisplay();
  vTaskDelay(pdMS_TO_TICKS(150));

  // BLE
  jr_ble_init();
  jr_ble_set_reset_on_start(true);

  // Jump detector (queue-based API)
  jumpDetector = new JumpDetector(&sr, JUMP_THRESHOLD_FACTOR, MIN_JUMP_INTERVAL_MS);

  xTaskCreate(jumpTask, "jump_task", 4096, nullptr, 5, nullptr);
  xTaskCreate(bleSnapshotTask, "ble_snapshot_task", 3072, nullptr, 4, nullptr);
  xTaskCreate(displayTask, "display_task", 3072, nullptr, 3, nullptr);

  ESP_LOGI(TAG, "Initialization complete");
  vTaskDelete(nullptr);
}

/* =========================
   APP MAIN
   ========================= */

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "app_main");
  xTaskCreate(initTask, "init_task", 4096, nullptr, 6, nullptr);
}

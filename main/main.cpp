#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "algorithm_by_RF.h"
#include "display.h"
#include "i2cInit.h"
#include "jump.h"
#include "jr_ble.h"
#include "max30102.h"
#include "mutex.h"

#include <cstdio>
#include <cstdint>

/* =========================
   CONFIGURATION
   ========================= */

#define JUMP_THRESHOLD_FACTOR 1.3f
#define MIN_JUMP_INTERVAL_MS 300
#define JUMP_UPDATE_HZ 100

#define DISPLAY_UPDATE_HZ 4

#define HR_BUFFER_SIZE 100
#define HR_SAMPLE_PERIOD_MS 40  // ~25 Hz sampling

// How often we push a snapshot into BLE (does not need to match notify rate)
#define BLE_SNAPSHOT_HZ 20

/* =========================
   GLOBALS
   ========================= */

static const char *TAG = "MAIN";

static OledDisplay *display = nullptr;
static SensorReading *sensor = nullptr;
static JumpDetector *gyroDetector = nullptr;
static JumpDetector *accelDetector = nullptr;

// Shared data (protected by mutex)
static SemaphoreHandle_t dataMutex = nullptr;

static uint8_t latest_hr = 0;
static bool hr_valid = false;

static uint8_t latest_spo2 = 0;
static bool spo2_valid = false;

/* =========================
   HELPERS
   ========================= */

static inline uint32_t u32max(uint32_t a, uint32_t b) { return (a > b) ? a : b; }

// Compute a single jump total for BLE/UI.
// For demo simplicity: take the max across accel axes and gyro axes, then max of those.
static uint32_t compute_jump_total_unlocked() {
  uint32_t ax = 0, ay = 0, az = 0;
  uint32_t gx = 0, gy = 0, gz = 0;

  if (accelDetector) accelDetector->getTotalJumps(ax, ay, az);
  if (gyroDetector) gyroDetector->getTotalJumps(gx, gy, gz);

  const uint32_t accelMax = u32max(ax, u32max(ay, az));
  const uint32_t gyroMax = u32max(gx, u32max(gy, gz));
  return u32max(accelMax, gyroMax);
}

/* =========================
   TASK: JUMP DETECTION
   ========================= */

void jumpDetectionTask(void *param) {
  (void)param;
  ESP_LOGI(TAG, "Jump detection task started");

  while (true) {
    if (gyroDetector) gyroDetector->update();
    if (accelDetector) accelDetector->update();
    vTaskDelay(pdMS_TO_TICKS(1000 / JUMP_UPDATE_HZ));
  }
}

/* =========================
   TASK: HEART RATE (MAX30102)
   ========================= */

void heartRateTask(void *param) {
  (void)param;
  ESP_LOGI(TAG, "Heart rate task started");

  uint32_t irBuf[HR_BUFFER_SIZE];
  uint32_t redBuf[HR_BUFFER_SIZE];
  int idx = 0;

  int32_t heartRate = 0;
  float spo2 = 0.0f;
  int8_t hrValid = 0, spo2Valid = 0;
  float ratio = 0.0f, correl = 0.0f;

  maxim_max30102_init();

  while (true) {
    uint32_t ir = 0, red = 0;

    if (maxim_max30102_read_fifo(&red, &ir) == ESP_OK) {
      irBuf[idx] = ir;
      redBuf[idx] = red;
      idx++;

      if (idx >= HR_BUFFER_SIZE) {
        rf_heart_rate_and_oxygen_saturation(
            irBuf,
            HR_BUFFER_SIZE,
            redBuf,
            &spo2,
            &spo2Valid,
            &heartRate,
            &hrValid,
            &ratio,
            &correl
        );

        {
          MutexGuard lock(dataMutex);

          if (hrValid && heartRate > 0 && heartRate < 255) {
            latest_hr = static_cast<uint8_t>(heartRate);
            hr_valid = true;
          } else {
            hr_valid = false;
          }

          if (spo2Valid && spo2 >= 0.0f && spo2 <= 100.0f) {
            latest_spo2 = static_cast<uint8_t>(spo2);
            spo2_valid = true;
          } else {
            spo2_valid = false;
          }
        }

        idx = 0;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(HR_SAMPLE_PERIOD_MS));
  }
}

/* =========================
   TASK: BLE SNAPSHOT FEED
   ========================= */

void bleSnapshotTask(void *param) {
  (void)param;
  ESP_LOGI(TAG, "BLE snapshot task started (%d Hz)", BLE_SNAPSHOT_HZ);

  while (true) {
    uint32_t jump_total = 0;
    uint8_t hr_bpm = 0;
    bool hrOk = false;

    // accel_mag is optional for now; keep 0 until you expose acceleration magnitude in firmware.
    const uint16_t accel_mag = 0;

    uint8_t flags = 0;

    {
      MutexGuard lock(dataMutex);
      jump_total = compute_jump_total_unlocked();
      hr_bpm = latest_hr;
      hrOk = hr_valid;
    }

    const uint8_t hr_to_send = hrOk ? hr_bpm : 0;
    if (hrOk) flags |= 0x01; // bit0: HR valid

    jr_ble_set_sensor_snapshot(jump_total, hr_to_send, accel_mag, flags);

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
    uint32_t jump_total = 0;
    uint8_t hr = 0, spo2 = 0;
    bool hrOk = false, spo2Ok = false;

    {
      MutexGuard lock(dataMutex);
      jump_total = compute_jump_total_unlocked();
      hr = latest_hr;
      spo2 = latest_spo2;
      hrOk = hr_valid;
      spo2Ok = spo2_valid;
    }

    const bool bleConnected = jr_ble_is_connected();
    const bool bleStreaming = jr_ble_is_streaming();

    snprintf(line0, sizeof(line0), "Jumps: %lu", static_cast<unsigned long>(jump_total));

    if (hrOk) snprintf(line1, sizeof(line1), "HR: %u bpm", hr);
    else      snprintf(line1, sizeof(line1), "HR: --");

    if (spo2Ok) snprintf(line2, sizeof(line2), "SpO2: %u%%", spo2);
    else        snprintf(line2, sizeof(line2), "SpO2: --");

    if (bleStreaming) snprintf(line3, sizeof(line3), "BLE: streaming");
    else if (bleConnected) snprintf(line3, sizeof(line3), "BLE: connected");
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

  // Start BLE early so advertising is available immediately
  jr_ble_init();
  jr_ble_set_reset_on_start(true);
  ESP_LOGI(TAG, "BLE initialized (advertising started)");

  // I2C init (used by MPU6050 and MAX30102)
  I2CManager &i2c = I2CManager::getInstance();
  i2c.init();
  configASSERT(i2c.isInitialized());
  vTaskDelay(pdMS_TO_TICKS(100));

  // Display
  display = new OledDisplay();
  vTaskDelay(pdMS_TO_TICKS(150));

  // Jump detection
  sensor = new SensorReading();
  vTaskDelay(pdMS_TO_TICKS(100));

  gyroDetector = new JumpDetector(sensor, SENSOR_GYRO, JUMP_THRESHOLD_FACTOR, MIN_JUMP_INTERVAL_MS);
  accelDetector = new JumpDetector(sensor, SENSOR_ACCEL, JUMP_THRESHOLD_FACTOR, MIN_JUMP_INTERVAL_MS);

  ESP_LOGI(TAG, "Creating tasks...");
  xTaskCreate(jumpDetectionTask, "jump_task", 4096, nullptr, 5, nullptr);
  xTaskCreate(heartRateTask, "hr_task", 4096, nullptr, 5, nullptr);
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

#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2cInit.h"
#include "jump.h"
#include "mutex.h"
#include <cstdio>

// BLE layer (NimBLE)
#include "jr_ble.h"

/* =========================
   CONFIGURATION
   ========================= */
#define JUMP_THRESHOLD_FACTOR 1.3f
#define MIN_JUMP_INTERVAL_MS 300
#define JUMP_UPDATE_HZ 100
#define DISPLAY_UPDATE_HZ 4
#define CALIBRATION_TIME_MS 3000

// How often we push a snapshot into BLE (does not need to match notify rate)
#define BLE_SNAPSHOT_HZ 20

/* =========================
   GLOBALS
   ========================= */
static const char *TAG = "JUMP_TEST";
static OledDisplay *display = nullptr;
static JumpDetector *gyroDetector = nullptr;
static JumpDetector *accelDetector = nullptr;
static SensorReading *sensor = nullptr;
static SemaphoreHandle_t dataMutex = nullptr;

// Calibration tracking
static uint32_t calibrationStartTime = 0;
static bool calibrationPhase = true;

// Store counts for display (3 axes * 4 configs each)
static uint32_t gyroCountsX[NUM_TIMING_CONFIGS];
static uint32_t gyroCountsY[NUM_TIMING_CONFIGS];
static uint32_t gyroCountsZ[NUM_TIMING_CONFIGS];
static uint32_t accelCountsX[NUM_TIMING_CONFIGS];
static uint32_t accelCountsY[NUM_TIMING_CONFIGS];
static uint32_t accelCountsZ[NUM_TIMING_CONFIGS];

/* =========================
   HELPERS
   ========================= */

// Simple max helper (avoid <algorithm> include)
static inline uint32_t u32max(uint32_t a, uint32_t b) { return (a > b) ? a : b; }

// Decide a single "best" jump_total value for BLE.
// Your project currently runs multiple detectors (gyro/accel, X/Y/Z).
// For a single workout counter, we must choose ONE stream.
// In a demo-friendly way, we take the maximum of accel totals across axes.
// (If you later decide one axis is best, replace this with totalZ or similar.)
static uint32_t getJumpTotalForBle() {
  uint32_t ax = 0, ay = 0, az = 0;
  uint32_t gx = 0, gy = 0, gz = 0;

  // Protect against reading detector state while update() is running
  MutexGuard lock(dataMutex);

  if (accelDetector) accelDetector->getTotalJumps(ax, ay, az);
  if (gyroDetector) gyroDetector->getTotalJumps(gx, gy, gz);

  // Prefer accel totals (usually more direct for jump rope movement),
  // but still take the maximum across all to avoid under-counting.
  uint32_t accelMax = u32max(ax, u32max(ay, az));
  uint32_t gyroMax = u32max(gx, u32max(gy, gz));
  return u32max(accelMax, gyroMax);
}

/* =========================
   JUMP DETECTION TASK
   ========================= */
void jumpDetectionTask(void *param) {
  ESP_LOGI(TAG, "Jump detection task started");
  ESP_LOGI(TAG, "Running: GYRO (X,Y,Z) + ACCEL (X,Y,Z)");
  ESP_LOGI(TAG, "Total: 6 axes * 4 configs = 24 detectors");

  while (true) {
    // If JumpDetector internally reads SensorReading, and SensorReading reads I2C,
    // these updates are the core of the sensor processing chain.
    gyroDetector->update();
    accelDetector->update();
    vTaskDelay(pdMS_TO_TICKS(1000 / JUMP_UPDATE_HZ));
  }
}

/* =========================
   BLE SNAPSHOT TASK
   ========================= */
// This task feeds the BLE layer with the most recent sensor "summary".
// The BLE service will stream packets at its own rate when START is received from the web app.
// We keep the snapshot update rate modest; BLE notifies will re-send the latest snapshot.
void bleSnapshotTask(void *param) {
  ESP_LOGI(TAG, "BLE snapshot task started (%d Hz)", BLE_SNAPSHOT_HZ);

  // For now:
  // - heart_rate_bpm is not available in this firmware -> send 0
  // - accel_mag is not directly exposed by your current SensorReading API -> send 0
  //
  // When you later add heart rate sensor integration (e.g., MAX3010x),
  // and expose accel magnitude, you can fill those fields here.
  while (true) {
    const uint32_t jump_total = getJumpTotalForBle();

    const uint8_t heart_rate_bpm = 0; // not implemented in this main.cpp
    const uint16_t accel_mag = 0;     // not implemented in this main.cpp
    const uint8_t flags = 0;          // define bits later if you want (e.g., HR valid)

    jr_ble_set_sensor_snapshot(jump_total, heart_rate_bpm, accel_mag, flags);

    vTaskDelay(pdMS_TO_TICKS(1000 / BLE_SNAPSHOT_HZ));
  }
}

/* =========================
   DISPLAY TASK
   ========================= */
void displayTask(void *param) {
  ESP_LOGI(TAG, "Display task started");

  // Display pages: 0-2=gyro (X,Y,Z), 3-5=accel (X,Y,Z), 6=gyro summary, 7=accel summary
  int displayPage = 0;
  uint32_t lastPageChange = 0;
  uint32_t now = 0;

  while (true) {
    now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // Handle calibration phase
    if (calibrationPhase) {
      uint32_t elapsed = now - calibrationStartTime;

      if (elapsed < CALIBRATION_TIME_MS) {
        display->clear();
        display->drawString(10, 10, "CALIBRATING...");

        uint32_t remaining = (CALIBRATION_TIME_MS - elapsed) / 1000;
        char countdownStr[32];
        snprintf(countdownStr, sizeof(countdownStr), "Start jumping!");
        display->drawString(5, 25, countdownStr);

        snprintf(countdownStr, sizeof(countdownStr), "%lu seconds", remaining + 1);
        display->drawString(30, 40, countdownStr);

        display->commit();
        vTaskDelay(pdMS_to_TICKS(1000 / DISPLAY_UPDATE_HZ));
        continue;
      } else {
        calibrationPhase = false;
        ESP_LOGI(TAG, "Calibration complete");
      }
    }

    // Get current counts
    {
      MutexGuard lock(dataMutex);
      gyroDetector->getCounts(gyroCountsX, gyroCountsY, gyroCountsZ);
      accelDetector->getCounts(accelCountsX, accelCountsY, accelCountsZ);
    }

    // Cycle display pages every 3 seconds
    if (now - lastPageChange > 3000) {
      displayPage = (displayPage + 1) % 8;
      lastPageChange = now;
    }

    display->clear();
    char line[32];

    if (displayPage == 0) {
      display->drawString(0, 0, "GYRO X-AXIS:");
      for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
        uint32_t rise, fall;
        gyroDetector->getTimingConfig(i, rise, fall);
        snprintf(line, sizeof(line), "%lums: %lu", rise, gyroCountsX[i]);
        display->drawString(0, 12 + i * 12, line);
      }

    } else if (displayPage == 1) {
      display->drawString(0, 0, "GYRO Y-AXIS:");
      for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
        uint32_t rise, fall;
        gyroDetector->getTimingConfig(i, rise, fall);
        snprintf(line, sizeof(line), "%lums: %lu", rise, gyroCountsY[i]);
        display->drawString(0, 12 + i * 12, line);
      }

    } else if (displayPage == 2) {
      display->drawString(0, 0, "GYRO Z-AXIS:");
      for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
        uint32_t rise, fall;
        gyroDetector->getTimingConfig(i, rise, fall);
        snprintf(line, sizeof(line), "%lums: %lu", rise, gyroCountsZ[i]);
        display->drawString(0, 12 + i * 12, line);
      }

    } else if (displayPage == 3) {
      display->drawString(0, 0, "ACCEL X-AXIS:");
      for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
        uint32_t rise, fall;
        accelDetector->getTimingConfig(i, rise, fall);
        snprintf(line, sizeof(line), "%lums: %lu", rise, accelCountsX[i]);
        display->drawString(0, 12 + i * 12, line);
      }

    } else if (displayPage == 4) {
      display->drawString(0, 0, "ACCEL Y-AXIS:");
      for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
        uint32_t rise, fall;
        accelDetector->getTimingConfig(i, rise, fall);
        snprintf(line, sizeof(line), "%lums: %lu", rise, accelCountsY[i]);
        display->drawString(0, 12 + i * 12, line);
      }

    } else if (displayPage == 5) {
      display->drawString(0, 0, "ACCEL Z-AXIS:");
      for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
        uint32_t rise, fall;
        accelDetector->getTimingConfig(i, rise, fall);
        snprintf(line, sizeof(line), "%lums: %lu", rise, accelCountsZ[i]);
        display->drawString(0, 12 + i * 12, line);
      }

    } else if (displayPage == 6) {
      display->drawString(15, 0, "GYRO TOTAL");

      uint32_t totalX, totalY, totalZ;
      float rateX, rateY, rateZ;
      gyroDetector->getTotalJumps(totalX, totalY, totalZ);
      gyroDetector->getAverageRates(rateX, rateY, rateZ);

      snprintf(line, sizeof(line), "X: %lu", totalX);
      display->drawString(0, 15, line);
      snprintf(line, sizeof(line), "%.0f/min", rateX);
      display->drawString(60, 15, line);

      snprintf(line, sizeof(line), "Y: %lu", totalY);
      display->drawString(0, 30, line);
      snprintf(line, sizeof(line), "%.0f/min", rateY);
      display->drawString(60, 30, line);

      snprintf(line, sizeof(line), "Z: %lu", totalZ);
      display->drawString(0, 45, line);
      snprintf(line, sizeof(line), "%.0f/min", rateZ);
      display->drawString(60, 45, line);

    } else {
      display->drawString(15, 0, "ACCEL TOTAL");

      uint32_t totalX, totalY, totalZ;
      float rateX, rateY, rateZ;
      accelDetector->getTotalJumps(totalX, totalY, totalZ);
      accelDetector->getAverageRates(rateX, rateY, rateZ);

      snprintf(line, sizeof(line), "X: %lu", totalX);
      display->drawString(0, 15, line);
      snprintf(line, sizeof(line), "%.0f/min", rateX);
      display->drawString(60, 15, line);

      snprintf(line, sizeof(line), "Y: %lu", totalY);
      display->drawString(0, 30, line);
      snprintf(line, sizeof(line), "%.0f/min", rateY);
      display->drawString(60, 30, line);

      snprintf(line, sizeof(line), "Z: %lu", totalZ);
      display->drawString(0, 45, line);
      snprintf(line, sizeof(line), "%.0f/min", rateZ);
      display->drawString(60, 45, line);
    }

    display->commit();
    vTaskDelay(pdMS_TO_TICKS(1000 / DISPLAY_UPDATE_HZ));
  }
}

/* =========================
   INITIALIZATION TASK
   ========================= */
void initTask(void *param) {
  ESP_LOGI(TAG, "=== 3-Axis Gyro vs Accel Jump Detector ===");

  dataMutex = xSemaphoreCreateMutex();
  configASSERT(dataMutex != nullptr);

  // Initialize BLE early (so advertising starts)
  jr_ble_init();
  jr_ble_set_reset_on_start(true);
  ESP_LOGI(TAG, "BLE initialized (service advertising started)");

  I2CManager &i2c = I2CManager::getInstance();
  i2c.init();
  configASSERT(i2c.isInitialized());
  vTaskDelay(pdMS_TO_TICKS(100));

  display = new OledDisplay();
  ESP_LOGI(TAG, "Display initialized");

  display->clear();
  display->drawString(15, 20, "Initializing");
  display->drawString(30, 35, "Sensors...");
  display->commit();
  vTaskDelay(pdMS_TO_TICKS(1500));

  sensor = new SensorReading();
  ESP_LOGI(TAG, "Sensor initialized");
  vTaskDelay(pdMS_TO_TICKS(100));

  gyroDetector = new JumpDetector(sensor, SENSOR_GYRO, JUMP_THRESHOLD_FACTOR,
                                  MIN_JUMP_INTERVAL_MS);
  ESP_LOGI(TAG, "Gyro 3-axis detector initialized");

  accelDetector = new JumpDetector(sensor, SENSOR_ACCEL, JUMP_THRESHOLD_FACTOR,
                                   MIN_JUMP_INTERVAL_MS);
  ESP_LOGI(TAG, "Accel 3-axis detector initialized");

  calibrationStartTime = xTaskGetTickCount() * portTICK_PERIOD_MS;
  calibrationPhase = true;

  ESP_LOGI(TAG, "Starting %d second calibration", CALIBRATION_TIME_MS / 1000);

  // Create tasks
  xTaskCreate(jumpDetectionTask, "jump_task", 4096, nullptr, 5, nullptr);
  xTaskCreate(displayTask, "display_task", 3072, nullptr, 4, nullptr);

  // Feed BLE snapshots continuously (BLE will stream when the web app sends START)
  xTaskCreate(bleSnapshotTask, "ble_snapshot_task", 3072, nullptr, 4, nullptr);

  ESP_LOGI(TAG, "=== All tasks started ===");
  ESP_LOGI(TAG, "Display pages (3s each):");
  ESP_LOGI(TAG, "  0: GYRO X-AXIS");
  ESP_LOGI(TAG, "  1: GYRO Y-AXIS");
  ESP_LOGI(TAG, "  2: GYRO Z-AXIS");
  ESP_LOGI(TAG, "  3: ACCEL X-AXIS");
  ESP_LOGI(TAG, "  4: ACCEL Y-AXIS");
  ESP_LOGI(TAG, "  5: ACCEL Z-AXIS");
  ESP_LOGI(TAG, "  6: GYRO SUMMARY (totals + rates)");
  ESP_LOGI(TAG, "  7: ACCEL SUMMARY (totals + rates)");

  vTaskDelete(nullptr);
}

/* =========================
   APP MAIN
   ========================= */
extern "C" void app_main(void) {
  ESP_LOGI(TAG, "3-Axis Jump Rope Test Starting...");
  ESP_LOGI(TAG, "Testing: Gyro(X,Y,Z) + Accel(X,Y,Z) = 6 axes");
  ESP_LOGI(TAG, "Each axis tests 4 timing configs = 24 total detectors");
  xTaskCreate(initTask, "init_task", 4096, nullptr, 6, nullptr);
}
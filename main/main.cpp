#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "display.h"
#include "gpio_pin.h"
#include "i2cInit.h"

#include "algorithm_by_RF.h"
#include "max30102.h"

#include "jr_ble.h"
#include "mutex.h"

#include <cstdio>

// BLE layer (NimBLE)
#include "jr_ble.h"

/* =========================
   CONFIG
   ========================= */

#define BUTTON_PIN GPIO_NUM_0
#define HR_BUFFER_SIZE 100

// How often we push a snapshot into BLE (does not need to match notify rate)
#define BLE_SNAPSHOT_HZ 20

/* =========================
   GLOBALS
   ========================= */

static const char *TAG = "MAIN";

static OledDisplay *display = nullptr;
static GPIOPin *modeButton = nullptr;

/* Shared data (protected) */
static SemaphoreHandle_t dataMutex = nullptr;
static uint8_t latest_hr = 0;
static uint8_t latest_spo2 = 0;
static bool hr_valid = false;
static bool spo2_valid = false;

/* =========================
<<<<<<< Updated upstream
   HEART RATE TASK
=======
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
>>>>>>> Stashed changes
   ========================= */

void heartRateTask(void *param) {
  uint32_t irBuf[HR_BUFFER_SIZE];
  uint32_t redBuf[HR_BUFFER_SIZE];
  int idx = 0;

  int32_t heartRate;
  float spo2;
  int8_t hrValid, spo2Valid;
  float ratio, correl;

  maxim_max30102_init();

  while (true) {
<<<<<<< Updated upstream
    uint32_t ir, red;

    if (maxim_max30102_read_fifo(&red, &ir) == ESP_OK) {
      irBuf[idx] = ir;
      redBuf[idx] = red;
      idx++;

      if (idx >= HR_BUFFER_SIZE) {
        rf_heart_rate_and_oxygen_saturation(irBuf, HR_BUFFER_SIZE, redBuf,
                                            &spo2, &spo2Valid, &heartRate,
                                            &hrValid, &ratio, &correl);

        {
          MutexGuard lock(dataMutex);

          if (hrValid && heartRate > 0 && heartRate < 255) {
            latest_hr = (uint8_t)heartRate;
            hr_valid = true;
          }

          if (spo2Valid && spo2 >= 0 && spo2 <= 100) {
            latest_spo2 = (uint8_t)spo2;
            spo2_valid = true;
          }

          /* Feed BLE with latest known values */
          jr_ble_set_values(latest_hr, latest_spo2);
        }

        idx = 0;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(40)); // ~25 Hz sampling
=======
    // If JumpDetector internally reads SensorReading, and SensorReading reads I2C,
    // these updates are the core of the sensor processing chain.
    gyroDetector->update();
    accelDetector->update();
    vTaskDelay(pdMS_TO_TICKS(1000 / JUMP_UPDATE_HZ));
>>>>>>> Stashed changes
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

<<<<<<< Updated upstream
void displayTask(void *param) {
  char line1[32];
  char line2[32];
=======
  // Display pages: 0-2=gyro (X,Y,Z), 3-5=accel (X,Y,Z), 6=gyro summary, 7=accel summary
  int displayPage = 0;
  uint32_t lastPageChange = 0;
  uint32_t now = 0;
>>>>>>> Stashed changes

  while (true) {
    uint8_t hr, spo2;
    bool hrOk, spo2Ok;

<<<<<<< Updated upstream
=======
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
        vTaskDelay(pdMS_TO_TICKS(1000 / DISPLAY_UPDATE_HZ));
        continue;
      } else {
        calibrationPhase = false;
        ESP_LOGI(TAG, "Calibration complete");
      }
    }

    // Get current counts
>>>>>>> Stashed changes
    {
      MutexGuard lock(dataMutex);
      hr = latest_hr;
      spo2 = latest_spo2;
      hrOk = hr_valid;
      spo2Ok = spo2_valid;
    }

    display->clear();

<<<<<<< Updated upstream
    if (hrOk)
      snprintf(line1, sizeof(line1), "HR: %u BPM", hr);
    else
      snprintf(line1, sizeof(line1), "HR: --");

    if (spo2Ok)
      snprintf(line2, sizeof(line2), "SpO2: %u %%", spo2);
    else
      snprintf(line2, sizeof(line2), "SpO2: --");
=======
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
>>>>>>> Stashed changes

    display->drawString(0, 0, line1);
    display->drawString(0, 16, line2);
    display->commit();

    vTaskDelay(pdMS_TO_TICKS(500)); // 2 Hz UI refresh
  }
}

/* =========================
   INITIALIZATION TASK
   ========================= */

void initTask(void *param) {
  ESP_LOGI(TAG, "Initialization start");

  /* Mutex */
  dataMutex = xSemaphoreCreateMutex();
  configASSERT(dataMutex != nullptr);

<<<<<<< Updated upstream
  /* I2C */
=======
  // Initialize BLE early (so advertising starts)
  jr_ble_init();
  jr_ble_set_reset_on_start(true);
  ESP_LOGI(TAG, "BLE initialized (service advertising started)");

>>>>>>> Stashed changes
  I2CManager &i2c = I2CManager::getInstance();
  i2c.init();
  configASSERT(i2c.isInitialized());

  vTaskDelay(pdMS_TO_TICKS(100));

  /* Display */
  display = new OledDisplay();
  vTaskDelay(pdMS_TO_TICKS(150));

  /* BLE */
  jr_ble_init();

<<<<<<< Updated upstream
  /* Tasks */
  xTaskCreate(heartRateTask, "hr_task", 4096, nullptr, 5, nullptr);
  xTaskCreate(displayTask, "display_task", 3072, nullptr, 4, nullptr);

  ESP_LOGI(TAG, "Initialization complete");
=======
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

>>>>>>> Stashed changes
  vTaskDelete(nullptr);
}

/* =========================
   APP MAIN
   ========================= */

extern "C" void app_main(void) {
  ESP_LOGI(TAG, "app_main");
  xTaskCreate(initTask, "init_task", 4096, nullptr, 6, nullptr);
}

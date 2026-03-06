#include "algorithm_by_RF.h"
#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2cInit.h"
#include "jr_ble.h"
#include "jump.h"
#include "max30102.h"
#include "mutex.h"
#include <cstdio>
#include <inttypes.h>

/* =========================
   CONFIGURATION
   ========================= */
#define JUMP_THRESHOLD_FACTOR 1.3f
#define MIN_JUMP_INTERVAL_MS 300
#define JUMP_UPDATE_HZ 100
#define DISPLAY_UPDATE_HZ 4
#define CALIBRATION_TIME_MS 3000

// MAX30102 buffer: algorithm needs at least 100 samples at 100 Hz = 1 second
#define SPO2_BUFFER_SIZE 100
#define SPO2_SAMPLE_HZ 100

/* =========================
   GLOBALS
   ========================= */
static const char *TAG = "JUMP_TEST";
static OledDisplay *display = nullptr;
static JumpDetector *accelDetector = nullptr;
static SensorReading *sensor = nullptr;
static SemaphoreHandle_t dataMutex = nullptr;

// Calibration
static uint32_t calibrationStartTime = 0;
static bool calibrationPhase = true;

// Count cache for display (Z-axis only)
static uint32_t accelCountsZ[NUM_TIMING_CONFIGS];

// Heart rate / SpO2 — written by heartRateTask, read by bleUpdateTask +
// displayTask
static SemaphoreHandle_t hrMutex = nullptr;
static int32_t g_heart_rate = 0;
static int8_t g_hr_valid = 0;
static float g_spo2 = 0.0f;
static int8_t g_spo2_valid = 0;

/* =========================
   JUMP DETECTION TASK
   ========================= */
void jumpDetectionTask(void *param) {
  ESP_LOGI(TAG, "Jump detection task started");
  while (true) {
    {
      MutexGuard lock(dataMutex);
      accelDetector->update();
    }
    vTaskDelay(pdMS_TO_TICKS(1000 / JUMP_UPDATE_HZ));
  }
}

/* =========================
   DISPLAY TASK
   ========================= */
void displayTask(void *param) {
  ESP_LOGI(TAG, "Display task started");

  int displayPage = 0; // 0 = jump detail, 1 = jump summary, 2 = HR/SpO2
  uint32_t lastPageChange = 0;
  uint32_t now = 0;

  while (true) {
    now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // --- Calibration phase ---
    if (calibrationPhase) {
      uint32_t elapsed = now - calibrationStartTime;
      if (elapsed < CALIBRATION_TIME_MS) {
        uint32_t remaining = (CALIBRATION_TIME_MS - elapsed) / 1000;
        char countdownStr[32];

        display->clear();
        display->drawString(10, 10, "CALIBRATING...");
        display->drawString(5, 25, "Start jumping!");
        snprintf(countdownStr, sizeof(countdownStr), "%lu seconds",
                 remaining + 1);
        display->drawString(30, 40, countdownStr);
        display->commit();

        vTaskDelay(pdMS_TO_TICKS(1000 / DISPLAY_UPDATE_HZ));
        continue;
      } else {
        calibrationPhase = false;
        ESP_LOGI(TAG, "Calibration complete");
      }
    }

    // --- Fetch Z counts under mutex ---
    {
      MutexGuard lock(dataMutex);
      accelDetector->getCounts(nullptr, nullptr, accelCountsZ);
    }

    // --- Cycle pages every 3 seconds ---
    if (now - lastPageChange > 3000) {
      displayPage = (displayPage + 1) % 3;
      lastPageChange = now;
    }

    display->clear();
    char line[32];

    if (displayPage == 0) {
      // ===== ACCEL Z per timing config =====
      display->drawString(0, 0, "ACCEL Z-AXIS:");
      for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
        uint32_t rise, fall;
        accelDetector->getTimingConfig(i, rise, fall);
        snprintf(line, sizeof(line), "%lums: %lu", rise, accelCountsZ[i]);
        display->drawString(0, 12 + i * 12, line);
      }

    } else if (displayPage == 1) {
      // ===== ACCEL Z summary =====
      display->drawString(20, 0, "JUMP TOTAL");

      uint32_t totalX, totalY, totalZ;
      float rateX, rateY, rateZ;
      {
        MutexGuard lock(dataMutex);
        accelDetector->getTotalJumps(totalX, totalY, totalZ);
        accelDetector->getAverageRates(rateX, rateY, rateZ);
      }
      snprintf(line, sizeof(line), "Jumps: %lu", totalZ);
      display->drawString(15, 25, line);
      snprintf(line, sizeof(line), "Rate:  %.0f/min", rateZ);
      display->drawString(15, 42, line);

    } else {
      // ===== HR / SpO2 =====
      display->drawString(20, 0, "VITALS");

      int32_t hr;
      int8_t hrValid;
      float spo2;
      int8_t spo2Valid;
      {
        MutexGuard lock(hrMutex);
        hr = g_heart_rate;
        hrValid = g_hr_valid;
        spo2 = g_spo2;
        spo2Valid = g_spo2_valid;
      }

      if (hrValid) {
        snprintf(line, sizeof(line), "HR:  %" PRId32 " bpm", hr);
        display->drawString(10, 20, line);
      } else {
        display->drawString(10, 20, "HR:  --");
      }

      if (spo2Valid) {
        snprintf(line, sizeof(line), "SpO2: %.0f%%", spo2);
        display->drawString(10, 38, line);
      } else {
        display->drawString(10, 38, "SpO2: --");
      }
    }

    display->commit();
    vTaskDelay(pdMS_TO_TICKS(1000 / DISPLAY_UPDATE_HZ));
  }
}

/* =========================
   BLE UPDATE TASK
   ========================= */
void bleUpdateTask(void *param) {
  while (true) {
    uint32_t totalX, totalY, totalZ;
    float rateX, rateY, rateZ;
    {
      MutexGuard lock(dataMutex);
      accelDetector->getTotalJumps(totalX, totalY, totalZ);
      accelDetector->getAverageRates(rateX, rateY, rateZ);
    }

    int32_t hr;
    int8_t hrValid;
    float spo2;
    int8_t spo2Valid;
    {
      MutexGuard lock(hrMutex);
      hr = g_heart_rate;
      hrValid = g_hr_valid;
      spo2 = g_spo2;
      spo2Valid = g_spo2_valid;
    }

    // Only send real SpO2 when the algorithm says it's valid; otherwise 0
    uint8_t hr_to_send = hrValid ? (uint8_t)hr : 0;
    uint8_t spo2_to_send = spo2Valid ? (uint8_t)spo2 : 0;

    jr_ble_set_sensor_snapshot(totalZ,       // jump count (Z-axis only)
                               hr_to_send,   // heart rate bpm, 0 if invalid
                               spo2_to_send, // SpO2 %, 0 if invalid
                               0             // flags — reserved for now
    );

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

/* =========================
   HEART RATE TASK
   ========================= */
void heartRateTask(void *param) {
  (void)param;

  if (!maxim_max30102_init()) {
    ESP_LOGE(TAG, "MAX30102 failed to initialize!");
    vTaskDelete(nullptr);
    return;
  }
  ESP_LOGI(TAG, "MAX30102 initialized");

  // Circular sample buffers
  static uint32_t irBuffer[SPO2_BUFFER_SIZE];
  static uint32_t redBuffer[SPO2_BUFFER_SIZE];
  int sampleIndex = 0;

  float ratio = 0.0f, correl = 0.0f;
  float spo2 = 0.0f;
  int8_t spo2Valid = 0;
  int32_t heartRate = 0;
  int8_t hrValid = 0;

  while (true) {
    uint32_t red, ir;
    esp_err_t ret = maxim_max30102_read_fifo(&red, &ir);

    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Failed to read MAX30102 FIFO");
      vTaskDelay(pdMS_TO_TICKS(1000 / SPO2_SAMPLE_HZ));
      continue;
    }

    redBuffer[sampleIndex] = red;
    irBuffer[sampleIndex] = ir;
    sampleIndex++;

    // Once the buffer is full, run the algorithm and reset
    if (sampleIndex >= SPO2_BUFFER_SIZE) {
      sampleIndex = 0;

      rf_heart_rate_and_oxygen_saturation(irBuffer, SPO2_BUFFER_SIZE, redBuffer,
                                          &spo2, &spo2Valid, &heartRate,
                                          &hrValid, &ratio, &correl);

      ESP_LOGI(TAG, "HR: %" PRId32 " (valid=%d)  SpO2: %.1f (valid=%d)",
               heartRate, hrValid, spo2, spo2Valid);

      // Publish results — send 0 for SpO2 when invalid
      {
        MutexGuard lock(hrMutex);
        g_heart_rate = hrValid ? heartRate : 0;
        g_hr_valid = hrValid;
        g_spo2 = spo2Valid ? spo2 : 0.0f;
        g_spo2_valid = spo2Valid;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(1000 / SPO2_SAMPLE_HZ));
  }
}

/* =========================
   INIT TASK
   ========================= */
void initTask(void *param) {
  ESP_LOGI(TAG, "=== Accel Z-Axis Jump Detector ===");

  dataMutex = xSemaphoreCreateMutex();
  configASSERT(dataMutex != nullptr);

  hrMutex = xSemaphoreCreateMutex();
  configASSERT(hrMutex != nullptr);

  I2CManager &i2c = I2CManager::getInstance();
  i2c.init();
  configASSERT(i2c.isInitialized());
  vTaskDelay(pdMS_TO_TICKS(100));

  jr_ble_init();

  display = new OledDisplay();
  display->clear();
  display->drawString(15, 20, "Initializing");
  display->drawString(30, 35, "Sensors...");
  display->commit();
  vTaskDelay(pdMS_TO_TICKS(1500));

  sensor = new SensorReading();
  sensor->startTask();
  vTaskDelay(pdMS_TO_TICKS(100));

  accelDetector =
      new JumpDetector(sensor, JUMP_THRESHOLD_FACTOR, MIN_JUMP_INTERVAL_MS);

  calibrationStartTime = xTaskGetTickCount() * portTICK_PERIOD_MS;
  calibrationPhase = true;

  xTaskCreate(jumpDetectionTask, "jump_task", 4096, nullptr, 5, nullptr);
  xTaskCreate(displayTask, "display_task", 3072, nullptr, 4, nullptr);
  xTaskCreate(bleUpdateTask, "ble_task", 3072, nullptr, 3, nullptr);
  xTaskCreate(heartRateTask, "hr_task", 3072, nullptr, 3, nullptr);

  ESP_LOGI(TAG, "All tasks started");
  vTaskDelete(nullptr);
}

/* =========================
   APP MAIN
   ========================= */
extern "C" void app_main(void) {
  ESP_LOGI(TAG, "Jump Rope Monitor Starting...");
  xTaskCreate(initTask, "init_task", 4096, nullptr, 6, nullptr);
}
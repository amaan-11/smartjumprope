#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2cInit.h"
#include "jump.h"
#include "mutex.h"
//#include "sensor_reading.h"
#include <cstdio>

/* =========================
   CONFIGURATION
   ========================= */
#define JUMP_THRESHOLD_FACTOR 1.5f
#define MIN_JUMP_INTERVAL_MS 350
#define JUMP_UPDATE_HZ 100
#define DISPLAY_UPDATE_HZ 2 // Slower update for readability

/* =========================
   GLOBALS
   ========================= */
static const char *TAG = "JUMP_TEST";
static OledDisplay *display = nullptr;
static JumpDetector *jumpDetector = nullptr;
static SensorReading *gyro = nullptr;
static SemaphoreHandle_t dataMutex = nullptr;

// Store counts for display
static uint32_t countsX[NUM_TIMING_CONFIGS];
static uint32_t countsY[NUM_TIMING_CONFIGS];
static uint32_t countsZ[NUM_TIMING_CONFIGS];

/* =========================
   JUMP DETECTION TASK
   ========================= */
void jumpDetectionTask(void *param) {
  ESP_LOGI(TAG, "Jump detection task started");
  ESP_LOGI(TAG, "Testing 3 axes * 5 timing configs = 15 detectors");

  while (true) {
    jumpDetector->update();
    vTaskDelay(pdMS_TO_TICKS(1000 / JUMP_UPDATE_HZ));
  }
}

/* =========================
   DISPLAY TASK
   ========================= */
void displayTask(void *param) {
  ESP_LOGI(TAG, "Display task started");

  int displayMode = 0; // Cycle through different views
  uint32_t lastUpdate = 0;

  while (true) {
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

    // Get current counts
    {
      MutexGuard lock(dataMutex);
      jumpDetector->getCounts(countsX, countsY, countsZ);
    }

    // Cycle display mode every 5 seconds
    if (now - lastUpdate > 5000) {
      displayMode = (displayMode + 1) % 3;
      lastUpdate = now;
    }

    display->clear();

    if (displayMode == 0) {
      // Show X axis results
      display->drawString(0, 0, "X-AXIS JUMPS:");
      char line[32];
      for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
        uint32_t rise, fall;
        jumpDetector->getTimingConfig(i, rise, fall);
        snprintf(line, sizeof(line), "%lums: %lu", rise, countsX[i]);
        display->drawString(0, 12 + i * 10, line);
      }
    } else if (displayMode == 1) {
      // Show Y axis results
      display->drawString(0, 0, "Y-AXIS JUMPS:");
      char line[32];
      for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
        uint32_t rise, fall;
        jumpDetector->getTimingConfig(i, rise, fall);
        snprintf(line, sizeof(line), "%lums: %lu", rise, countsY[i]);
        display->drawString(0, 12 + i * 10, line);
      }
    } else {
      // Show Z axis results
      display->drawString(0, 0, "Z-AXIS JUMPS:");
      char line[32];
      for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
        uint32_t rise, fall;
        jumpDetector->getTimingConfig(i, rise, fall);
        snprintf(line, sizeof(line), "%lums: %lu", rise, countsZ[i]);
        display->drawString(0, 12 + i * 10, line);
      }
    }

    display->commit();

    vTaskDelay(pdMS_TO_TICKS(1000 / DISPLAY_UPDATE_HZ));
  }
}

/* =========================
   LOGGING TASK
   ========================= */
void loggingTask(void *param) {
  ESP_LOGI(TAG, "Logging task started");

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10000)); // Log every 10 seconds

    uint32_t xCounts[NUM_TIMING_CONFIGS];
    uint32_t yCounts[NUM_TIMING_CONFIGS];
    uint32_t zCounts[NUM_TIMING_CONFIGS];

    {
      MutexGuard lock(dataMutex);
      jumpDetector->getCounts(xCounts, yCounts, zCounts);
    }

    ESP_LOGI(TAG, "=== JUMP COUNTS (10sec update) ===");

    // Log X axis
    ESP_LOGI(TAG, "X-axis:");
    for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
      uint32_t rise, fall;
      jumpDetector->getTimingConfig(i, rise, fall);
      ESP_LOGI(TAG, "  Config %d (%lums/%lums): %lu jumps", i, rise, fall,
               xCounts[i]);
    }

    // Log Y axis
    ESP_LOGI(TAG, "Y-axis:");
    for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
      uint32_t rise, fall;
      jumpDetector->getTimingConfig(i, rise, fall);
      ESP_LOGI(TAG, "  Config %d (%lums/%lums): %lu jumps", i, rise, fall,
               yCounts[i]);
    }

    // Log Z axis
    ESP_LOGI(TAG, "Z-axis:");
    for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
      uint32_t rise, fall;
      jumpDetector->getTimingConfig(i, rise, fall);
      ESP_LOGI(TAG, "  Config %d (%lums/%lums): %lu jumps", i, rise, fall,
               zCounts[i]);
    }

    ESP_LOGI(TAG, "================================");
  }
}

/* =========================
   INITIALIZATION TASK
   ========================= */
void initTask(void *param) {
  ESP_LOGI(TAG, "=== Multi-Axis Jump Detector Test ===");

  dataMutex = xSemaphoreCreateMutex();
  configASSERT(dataMutex != nullptr);

  I2CManager &i2c = I2CManager::getInstance();
  i2c.init();
  configASSERT(i2c.isInitialized());
  vTaskDelay(pdMS_TO_TICKS(100));

  display = new OledDisplay();
  ESP_LOGI(TAG, "Display initialized");

  display->clear();
  display->commit();
  vTaskDelay(pdMS_TO_TICKS(2000));

  gyro = new SensorReading();
  ESP_LOGI(TAG, "Gyro initialized");
  vTaskDelay(pdMS_TO_TICKS(100));

  jumpDetector =
      new JumpDetector(gyro, JUMP_THRESHOLD_FACTOR, MIN_JUMP_INTERVAL_MS);
  ESP_LOGI(TAG, "Jump detector initialized");

  display->clear();
  display->drawString(20, 20, "Ready!");
  display->drawString(5, 35, "Start jumping");
  display->commit();
  vTaskDelay(pdMS_TO_TICKS(2000));

  xTaskCreate(jumpDetectionTask, "jump_task", 4096, nullptr, 5, nullptr);
  xTaskCreate(displayTask, "display_task", 3072, nullptr, 4, nullptr);
  xTaskCreate(loggingTask, "log_task", 3072, nullptr, 3, nullptr);

  ESP_LOGI(TAG, "=== All tasks started ===");
  ESP_LOGI(TAG, "Display will cycle through X/Y/Z axes every 5 seconds");
  ESP_LOGI(TAG, "Serial logs every 10 seconds");

  vTaskDelete(nullptr);
}

/* =========================
   APP MAIN
   ========================= */
extern "C" void app_main(void) {
  ESP_LOGI(TAG, "Jump Rope Multi-Axis Test Starting...");
  xTaskCreate(initTask, "init_task", 4096, nullptr, 6, nullptr);
}
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2cInit.h"

#include "display.h"
#include "gpio_pin.h"
#include "gyro.h"

#include "algorithm_by_RF.h"
#include "max30102.h"

#include <cstdio>

/* =========================
   CONFIG
   ========================= */

#define BUTTON_PIN GPIO_NUM_0
#define HR_BUFFER_SIZE 100

/* =========================
   GLOBALS
   ========================= */

static const char *TAG = "MAIN";

static OledDisplay *display = nullptr;
static GPIOPin *modeButton = nullptr;

static TaskHandle_t gyroTaskHandle = nullptr;
static TaskHandle_t hrTaskHandle = nullptr;

enum class DisplayMode { GYRO, HEART };

static DisplayMode currentMode = DisplayMode::GYRO;

/* =========================
   GYRO TASK
   ========================= */

void gyroDisplayTask(void *param) {
  SensorReading &gyro = SensorReading::getInstance();
  QueueHandle_t q = gyro.getQueue();

  mpu_data_t data;
  char l1[32], l2[32], l3[32];

  gyro.startTask();

  while (true) {
    if (xQueueReceive(q, &data, pdMS_TO_TICKS(200))) {
      snprintf(l1, sizeof(l1), "AX: %5.2f g", data.ax_g);
      snprintf(l2, sizeof(l2), "AY: %5.2f g", data.ay_g);
      snprintf(l3, sizeof(l3), "AZ: %5.2f g", data.az_g);

      display->clear();
      display->drawString(0, 0, l1);
      display->drawString(0, 16, l2);
      display->drawString(0, 32, l3);
      display->commit();
    }
  }
}

/* =========================
   HEART RATE TASK
   ========================= */

void heartRateTask(void *param) {
  uint32_t irBuf[HR_BUFFER_SIZE];
  uint32_t redBuf[HR_BUFFER_SIZE];
  int idx = 0;

  int32_t heartRate;
  float spo2;
  int8_t hrValid, spo2Valid;
  float ratio, correl;

  char line1[32], line2[32];

  maxim_max30102_init();

  while (true) {
    uint32_t ir, red;

    if (maxim_max30102_read_fifo(&red, &ir) == ESP_OK) {
      irBuf[idx] = ir;
      redBuf[idx] = red;
      idx++;

      if (idx >= HR_BUFFER_SIZE) {
        rf_heart_rate_and_oxygen_saturation(irBuf, HR_BUFFER_SIZE, redBuf,
                                            &spo2, &spo2Valid, &heartRate,
                                            &hrValid, &ratio, &correl);

        display->clear();

        if (hrValid) {
          snprintf(line1, sizeof(line1), "HR: %ld BPM", heartRate);
        } else {
          snprintf(line1, sizeof(line1), "HR: --");
        }

        if (spo2Valid) {
          snprintf(line2, sizeof(line2), "SpO2: %.1f%%", spo2);
        } else {
          snprintf(line2, sizeof(line2), "SpO2: --");
        }

        display->drawString(0, 0, line1);
        display->drawString(0, 16, line2);
        display->commit();

        idx = 0;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(40)); // ~25Hz
  }
}

/* =========================
   BUTTON TASK
   ========================= */

void buttonTask(void *param) {
  while (true) {
    modeButton->update();

    if (modeButton->pressed()) {
      if (currentMode == DisplayMode::GYRO) {
        ESP_LOGI(TAG, "Switching to HEART mode");

        if (gyroTaskHandle) {
          vTaskDelete(gyroTaskHandle);
          gyroTaskHandle = nullptr;
        }

        maxim_max30102_init();
        xTaskCreate(heartRateTask, "heart_rate_task", 4096, nullptr, 5,
                    &hrTaskHandle);

        currentMode = DisplayMode::HEART;

      } else {
        ESP_LOGI(TAG, "Switching to GYRO mode");

        if (hrTaskHandle) {
          vTaskDelete(hrTaskHandle);
          hrTaskHandle = nullptr;
        }

        maxim_max30102_init();
        xTaskCreate(gyroDisplayTask, "gyro_task", 4096, nullptr, 5,
                    &gyroTaskHandle);

        currentMode = DisplayMode::GYRO;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

/* =========================
   INITIALIZATION TASK
   ========================= */

void initTask(void *param) {
  ESP_LOGI(TAG, "Starting initialization task");

  // Step 1: Initialize I2C Manager
  I2CManager &i2c = I2CManager::getInstance();
  i2c.init();

  if (!i2c.isInitialized()) {
    ESP_LOGE(TAG, "I2C init failed");
    vTaskDelete(nullptr);
    return;
  }

  ESP_LOGI(TAG, "I2C initialized");

  // Step 2: Wait for I2C bus to stabilize
  vTaskDelay(pdMS_TO_TICKS(100));

  // Step 3: Create display (constructor calls init() internally)
  ESP_LOGI(TAG, "Creating display...");
  display = new OledDisplay();

  // Step 4: CRITICAL - Wait for display init to complete
  vTaskDelay(pdMS_TO_TICKS(150));

  // Step 5: Initialize gyro singleton (constructor calls init() internally)
  ESP_LOGI(TAG, "Initializing gyro...");
  SensorReading &gyro = SensorReading::getInstance();

  // Step 6: CRITICAL - Wait for gyro init to complete
  vTaskDelay(pdMS_TO_TICKS(150));

  // Step 7: Create button (no I2C, safe anytime)
  ESP_LOGI(TAG, "Creating button...");
  modeButton =
      new GPIOPin(BUTTON_PIN, GPIOMode::INPUT, GPIOPull::PULLUP, false, 30);

  ESP_LOGI(TAG, "Initialization complete");

  // Step 8: Start application tasks
  xTaskCreate(gyroDisplayTask, "gyro_task", 4096, nullptr, 5, &gyroTaskHandle);
  xTaskCreate(buttonTask, "button_task", 2048, nullptr, 6, nullptr);

  // Step 9: Delete init task
  vTaskDelete(nullptr);
}

/* =========================
   APP MAIN
   ========================= */

extern "C" void app_main() {
  ESP_LOGI(TAG, "Starting app_main");

  // Create initialization task and let it handle everything
  // This ensures FreeRTOS scheduler is running before creating mutex-using
  // objects
  xTaskCreate(initTask, "init_task", 4096, nullptr, 5, nullptr);
}
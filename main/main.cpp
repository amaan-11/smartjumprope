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

/* Shared data (protected) */
static SemaphoreHandle_t dataMutex = nullptr;
static uint8_t latest_hr = 0;
static uint8_t latest_spo2 = 0;
static bool hr_valid = false;
static bool spo2_valid = false;

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
  }
}

/* =========================
   DISPLAY TASK
   ========================= */

void displayTask(void *param) {
  char line1[32];
  char line2[32];

  while (true) {
    uint8_t hr, spo2;
    bool hrOk, spo2Ok;

    {
      MutexGuard lock(dataMutex);
      hr = latest_hr;
      spo2 = latest_spo2;
      hrOk = hr_valid;
      spo2Ok = spo2_valid;
    }

    display->clear();

    if (hrOk)
      snprintf(line1, sizeof(line1), "HR: %u BPM", hr);
    else
      snprintf(line1, sizeof(line1), "HR: --");

    if (spo2Ok)
      snprintf(line2, sizeof(line2), "SpO2: %u %%", spo2);
    else
      snprintf(line2, sizeof(line2), "SpO2: --");

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

  /* I2C */
  I2CManager &i2c = I2CManager::getInstance();
  i2c.init();
  configASSERT(i2c.isInitialized());

  vTaskDelay(pdMS_TO_TICKS(100));

  /* Display */
  display = new OledDisplay();
  vTaskDelay(pdMS_TO_TICKS(150));

  /* BLE */
  jr_ble_init();

  /* Tasks */
  xTaskCreate(heartRateTask, "hr_task", 4096, nullptr, 5, nullptr);
  xTaskCreate(displayTask, "display_task", 3072, nullptr, 4, nullptr);

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

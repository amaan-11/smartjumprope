#include "display.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gyro.h"
#include "i2cInit.h"
#include "max30102.h"
#include "jr_ble.h"          // <<< BLE module
#include <cstdio>

static const char *TAG = "MAIN";

// --------------------------------------------------
// Display task for gyro readings
// --------------------------------------------------
void displayTask(void *param) {
  auto *display = static_cast<OledDisplay *>(param);
  SensorReading &gyro = SensorReading::getInstance();
  QueueHandle_t q = gyro.getQueue();

  mpu_data_t data;
  char line1[32];
  char line2[32];
  char line3[32];

  while (true) {
    if (xQueueReceive(q, &data, pdMS_TO_TICKS(500))) {
      snprintf(line1, sizeof(line1), "GX: %6.1f", data.gx_dps);
      snprintf(line2, sizeof(line2), "GY: %6.1f", data.gy_dps);
      snprintf(line3, sizeof(line3), "GZ: %6.1f", data.gz_dps);

      display->clear();
      display->drawString(0, 0, line1);
      display->drawString(0, 16, line2);
      display->drawString(0, 32, line3);
      display->commit();
    }
  }
}

// --------------------------------------------------
// Heart rate task (reads MAX30102 FIFO)
// --------------------------------------------------
void heartRateTask(void *param) {
  (void)param;

  if (!maxim_max30102_init()) {
    ESP_LOGE(TAG, "MAX30102 failed to initialize!");
    vTaskDelete(nullptr);
    return;
  }

  ESP_LOGI(TAG, "MAX30102 initialized");

  while (true) {
    uint32_t red, ir;
    esp_err_t ret = maxim_max30102_read_fifo(&red, &ir);
    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "PPG Red: %lu, IR: %lu", red, ir);
    } else {
      ESP_LOGW(TAG, "Failed to read MAX30102 FIFO");
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// --------------------------------------------------
// BLE demo feed task (temporary test data)
// --------------------------------------------------
static volatile uint32_t g_jump_count = 0;
static volatile uint8_t  g_hr_bpm     = 0;
static volatile uint16_t g_accel_mag  = 0;

void bleFeedTask(void *param) {
  (void)param;

  while (true) {
    // Demo values so web app shows activity
    g_jump_count++;          // increments every second
    g_hr_bpm    = 0;         // unknown for now
    g_accel_mag = 123;       // placeholder magnitude

    jr_ble_set_values(g_jump_count, g_hr_bpm, g_accel_mag);

    ESP_LOGI(TAG, "BLE update - jumps=%lu", (unsigned long)g_jump_count);

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// --------------------------------------------------
// Main entry
// --------------------------------------------------
extern "C" void app_main() {
  I2CManager &i2c = I2CManager::getInstance();
  i2c.init();

  if (!i2c.isInitialized()) {
    ESP_LOGE(TAG, "I2C Manager failed to initialize!");
    return;
  }

  static OledDisplay display;
  display.clear();

  // Start gyro task
  SensorReading &gyro = SensorReading::getInstance();
  gyro.startTask();
  xTaskCreate(displayTask, "display_task", 4096, &display, 4, nullptr);

  // Start heart rate task
  xTaskCreate(heartRateTask, "heart_rate_task", 4096, nullptr, 5, nullptr);

  // --------------------------------------------------
  // Start BLE
  // --------------------------------------------------
  jr_ble_init();
  xTaskCreate(bleFeedTask, "ble_feed_task", 4096, nullptr, 5, nullptr);

  while (true) {
    vTaskDelay(portMAX_DELAY);
  }
}

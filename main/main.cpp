#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include "display.h"
#include "i2cInit.h"
#include"gyro.h"

static const char *TAG = "MAIN";

  void displayTask(void *param) {
    auto *display = static_cast<OledDisplay *>(param);

    SensorReading &gyro = SensorReading::getInstance(); // or pass pointer
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
  extern "C" void app_main() {
    I2CManager &i2c = I2CManager::getInstance();
    i2c.init();

    if (!i2c.isInitialized()) {
      ESP_LOGE(TAG, "I2C Manager failed to initialize!");
      return;
    }

    static OledDisplay display; // OK
    display.clear();

    SensorReading &gyro = SensorReading::getInstance();
    gyro.startTask();

    xTaskCreate(displayTask, "display_task", 4096, &display, 4, nullptr);
    while (true) {
      vTaskDelay(portMAX_DELAY);
    }
  }

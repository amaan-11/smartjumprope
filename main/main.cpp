#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include "display.h"
#include "i2cInit.h"

static const char *TAG = "MAIN";

// Global instance

extern "C" void app_main() {

  I2CManager &i2c = I2CManager::getInstance();
  i2c.init();

  if (!i2c.isInitialized()) {
    ESP_LOGE(TAG, "I2C Manager failed to initialize!");
    return;
  }

  OledDisplay display; // constructed AFTER I2C init

  vTaskDelay(pdMS_TO_TICKS(500));

  ESP_LOGI(TAG, "Display initialized: %s",
           display.isInitialized() ? "YES" : "NO");

  if (!display.isInitialized()) {
    ESP_LOGE(TAG, "Display failed to initialize - stopping");
    return;
  }

  // Startup screen
  display.clear();
  display.drawStringFlipped(10, 20, "World, Hello!");
  display.commit();
  vTaskDelay(pdMS_TO_TICKS(5000));
  display.clear();
  display.drawStringFlipped(10, 20, "!Hello, world!");
  display.commit();

  vTaskDelay(pdMS_TO_TICKS(2000));

  // Task 1: Display update task
  xTaskCreate(
      [](void *param) {
        auto *display = static_cast<OledDisplay *>(param);
        int counter = 0;

        while (true) {
          display->clear();
          display->drawStringFlipped(5, 5, "OLED I2C Test");
          display->commit();

          counter++;
          vTaskDelay(pdMS_TO_TICKS(5000));
        }
      },
      "DisplayTask", 4096,
      &display, // 👈 PASS POINTER HERE
      4, nullptr);
}

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include "display.h"
#include "i2cInit.h"

static const char *TAG = "MAIN";

extern "C" void app_main() {
  I2CManager &i2c = I2CManager::getInstance();
  i2c.init();

  if (!i2c.isInitialized()) {
    ESP_LOGE(TAG, "I2C Manager failed to initialize!");
    return;
  }

  static OledDisplay display;  // ✅ STATIC, NOT STACK
  // Startup screen
  display.clear();
  display.drawStringFlipped(10, 20, "World, Hello!");
  display.commit();
  vTaskDelay(pdMS_TO_TICKS(5000));
  display.clear();
  display.drawStringFlipped(10, 20, "!Hello, world!");
  display.commit();

  xTaskCreate(
      [](void *param) {
        auto *display = static_cast<OledDisplay *>(param);
        while (true) {
          display->clear();
          display->drawStringFlipped(5, 5, "OLED I2C Test");
          display->commit();
          vTaskDelay(pdMS_TO_TICKS(5000));
        }
      },
      "DisplayTask",
      4096,
      &display,
      4,
      nullptr);

  // KEEP app_main alive
  while (true) {
    vTaskDelay(portMAX_DELAY);
  }
}

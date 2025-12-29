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
  i2c.init(); // ✅ scheduler is alive now

  printf("\n========================================\n");
  printf("OLED Display I2C Test (NO GYRO)\n");
  printf("========================================\n\n");


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
  display.drawString(10, 20, "OLED I2C TEST");
  display.drawString(10, 35, "Display only");
  display.commit();
  vTaskDelay(pdMS_TO_TICKS(2000));

  printf("\n========================================\n");
  printf("Starting DISPLAY-ONLY I2C test...\n");
  printf("========================================\n\n");

  // Task 1: Display update task
  xTaskCreate(
      [](void *param) {
        auto *display = static_cast<OledDisplay *>(param);
        int counter = 0;

        while (true) {
          printf("[DISPLAY] Update #%d\n", counter);

          display->clear();
          display->drawString(5, 5, "OLED I2C Test");

          char buf[32];
          snprintf(buf, sizeof(buf), "Count: %d", counter);
          display->drawString(5, 25, buf);

          display->commit();

          counter++;
          vTaskDelay(pdMS_TO_TICKS(500));
        }
      },
      "DisplayTask", 4096,
      &display, // 👈 PASS POINTER HERE
      4, nullptr);
}

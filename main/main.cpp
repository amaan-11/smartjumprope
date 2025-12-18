#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>

#include "display.h"
#include "gyro.h"
#include "i2cInit.h"

static const char *TAG = "MAIN";

// Global instances - auto-initialize on creation
SensorReading gyro;
OledDisplay display;

extern "C" void app_main() {
  printf("\n========================================\n");
  printf("Smart Jump Rope - I2C Mutex Test\n");
  printf("========================================\n\n");

  // I2C Manager auto-initializes on first getInstance() call
  I2CManager &i2c = I2CManager::getInstance();

  if (!i2c.isInitialized()) {
    ESP_LOGE(TAG, "I2C Manager failed to initialize!");
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(500));

  // Check initialization status
  ESP_LOGI(TAG, "Gyro initialized: %s", gyro.isInitialized() ? "YES" : "NO");
  ESP_LOGI(TAG, "Display initialized: %s",
           display.isInitialized() ? "YES" : "NO");

  if (!gyro.isInitialized()) {
    ESP_LOGE(TAG, "Gyro failed to initialize - stopping");
    return;
  }

  if (display.isInitialized()) {
    // Show startup screen
    display.clear();
    display.drawString(20, 20, "I2C TEST");
    display.drawString(15, 35, "Starting...");
    display.commit();
    vTaskDelay(pdMS_TO_TICKS(2000));
  }

  printf("\n========================================\n");
  printf("Starting I2C bus sharing test...\n");
  printf("Watch for lock acquire/release messages\n");
  printf("========================================\n\n");

  // Task 1: Rapidly read gyro data (high frequency)
  xTaskCreate(
      [](void *) {
        int counter = 0;
        while (true) {
          int16_t ax, ay, az, gx, gy, gz;

          printf("[TASK1] Attempting gyro read #%d...\n", counter);

          if (gyro.readRaw(ax, ay, az, gx, gy, gz) == ESP_OK) {
            printf("[TASK1] ✓ Read successful: GX=%d GY=%d GZ=%d\n", gx, gy,
                   gz);
          } else {
            printf("[TASK1] ✗ Read failed\n");
          }

          counter++;
          vTaskDelay(pdMS_TO_TICKS(200)); // Read every 200ms
        }
      },
      "GyroReadTask", 4096, nullptr, 5, nullptr);

  // Task 2: Update display (lower frequency)
  xTaskCreate(
      [](void *) {
        if (!display.isInitialized()) {
          ESP_LOGW(TAG, "Display not initialized, skipping display task");
          vTaskDelete(nullptr);
          return;
        }

        int counter = 0;
        while (true) {
          printf("[TASK2] Attempting display update #%d...\n", counter);

          display.clear();
          display.drawString(10, 10, "I2C Mutex Test");

          char buf[32];
          snprintf(buf, sizeof(buf), "Updates: %d", counter);
          display.drawString(10, 30, buf);

          display.commit();

          printf("[TASK2] ✓ Display updated\n");

          counter++;
          vTaskDelay(pdMS_TO_TICKS(500)); // Update every 500ms
        }
      },
      "DisplayTask", 4096, nullptr, 4, nullptr);

  // Task 3: Interleaved burst reads (stress test)
  xTaskCreate(
      [](void *) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Start after 1 second

        while (true) {
          printf("\n[TASK3] ========== BURST TEST START ==========\n");

          // Rapid burst of gyro reads
          for (int i = 0; i < 5; i++) {
            int16_t ax, ay, az, gx, gy, gz;
            printf("[TASK3] Burst read %d/5...\n", i + 1);

            if (gyro.readRaw(ax, ay, az, gx, gy, gz) == ESP_OK) {
              printf("[TASK3] ✓ Burst read OK\n");
            }

            vTaskDelay(pdMS_TO_TICKS(50)); // Very fast reads
          }

          printf("[TASK3] ========== BURST TEST END ==========\n\n");

          vTaskDelay(pdMS_TO_TICKS(2000)); // Wait 2 seconds before next burst
        }
      },
      "BurstTask", 4096, nullptr, 3, nullptr);

  // Task 4: Monitor task - prints status
  xTaskCreate(
      [](void *) {
        int sec = 0;
        while (true) {
          vTaskDelay(pdMS_TO_TICKS(5000));
          sec += 5;
          printf("\n");
          printf("========================================\n");
          printf("Status: %d seconds elapsed\n", sec);
          printf("All tasks running, mutex working!\n");
          printf("========================================\n\n");
        }
      },
      "MonitorTask", 2048, nullptr, 1, nullptr);
}
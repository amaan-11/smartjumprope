#include "algorithm_by_RF.h"
#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2cInit.h"
#include "jr_ble.h"
#include "jump.h"
#include "mutex.h"2
#include "max30102.h" 
#include <cstdio>
#include <inttypes.h>

#define MIN_THRESHOLD 40.0f
#define MAX_THRESHOLD 800.0f
#define INITIAL_THRESHOLD 100.0f
#define JUMP_THRESHOLD_FACTOR 1.3f
#define MIN_JUMP_INTERVAL_MS 300
#define JUMP_UPDATE_HZ 100
#define DISPLAY_UPDATE_HZ 4
#define CALIBRATION_TIME_MS 3000

    static const char *TAG = "JUMP_TEST";
static OledDisplay *display = nullptr;
static JumpDetector *gyroDetector = nullptr;
static JumpDetector *accelDetector = nullptr;
static SensorReading *sensor = nullptr;
static SemaphoreHandle_t dataMutex = nullptr;

void jumpDetectionTask(void *param) {
  // Assume sensor was created before
  SensorReading *sensor = static_cast<SensorReading *>(param);

  JumpDetector detector(sensor, JUMP_THRESHOLD_FACTOR, MIN_JUMP_INTERVAL_MS);

  while (true) {
    detector.update();
    vTaskDelay(pdMS_TO_TICKS(1000 / JUMP_UPDATE_HZ));
  }
}

void displayTask(void *param) {
  auto *detector = static_cast<JumpDetector *>(param);

  static uint32_t lastTotalJumps = 0;

  while (true) {
    uint32_t totalX, totalY, totalZ;
    detector->getTotalJumps(totalX, totalY, totalZ);

    uint32_t currentTotal = totalX + totalY + totalZ;

    if (currentTotal != lastTotalJumps) {
      lastTotalJumps = currentTotal;

      display->clear();
      char buf[16];
      snprintf(buf, sizeof(buf), "Jumps: %" PRIu32, currentTotal);
      display->drawString(0, 0, buf);
      //in the buf place we needed to add the actual value.
      display->commit();
    }

    vTaskDelay(pdMS_TO_TICKS(1000 / DISPLAY_UPDATE_HZ));
  }
}

void bleUpdateTask(void *param) {
  auto *detector = static_cast<JumpDetector *>(param);

  uint32_t lastZ = 0;
  int8_t lastSpo2 = -2; // force first update

  while (true) {
    //uint32_t z = detector->getTotalJumps();
      jr_ble_set_values(lastZ, lastSpo2);
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }

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

      // For now just print raw LED values; later can implement BPM algorithm

      ESP_LOGI(TAG, "PPG Red: %lu, IR: %lu", red, ir);

    } else {

      ESP_LOGW(TAG, "Failed to read MAX30102 FIFO");
    }

    vTaskDelay(pdMS_TO_TICKS(100)); // Read ~10 times per second
  }
}

extern "C" void app_main() {
  // 1. Initialize I2C manager
  I2CManager &i2c = I2CManager::getInstance();
  i2c.init();
  vTaskDelay(1000);
  jr_ble_init();

  // 2. Sensor
  static SensorReading sensor;
  sensor.startTask();

  // 3. Jump detector
  static JumpDetector jumpDetector(&sensor, JUMP_THRESHOLD_FACTOR,
                                   MIN_JUMP_INTERVAL_MS);

  // 4. Display
  static OledDisplay oled;
  display = &oled;

  // 5. Start tasks
  xTaskCreate(
      [](void *param) {
        auto *detector = static_cast<JumpDetector *>(param);
        while (true) {
          detector->update();
          vTaskDelay(pdMS_TO_TICKS(1000 / JUMP_UPDATE_HZ));
        }
      },
      "jump_task", 4096, &jumpDetector, 5, nullptr);

  xTaskCreate(displayTask, "display_task", 4096, &jumpDetector, 5, nullptr);
  xTaskCreate(bleUpdateTask, "ble_task", 4096, &jumpDetector, 5, nullptr);
}
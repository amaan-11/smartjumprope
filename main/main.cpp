#include "display.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "i2cInit.h"
#include "jump.h"
#include "mutex.h"
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
      display->drawString(0, 0, buf);
      //in the buf place we needed to add the actual value.
      display->commit();
    }

    vTaskDelay(pdMS_TO_TICKS(1000 / DISPLAY_UPDATE_HZ));
  }
}
extern "C" void app_main() {
  // 1. Initialize I2C manager
  I2CManager &i2c = I2CManager::getInstance();
  i2c.init();

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
  //while (1){}
}
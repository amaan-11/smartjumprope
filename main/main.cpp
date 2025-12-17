#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_pin.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdint.h>
#include "mutex.h"
#include "gyro.h"
#include "jump.h"
#include "display.h"

SensorReading gyro(I2C_NUM_0, 21, 22);
JumpDetector jumpDetector(&gyro);
OledDisplay display(I2C_NUM_1, 30, 31, 0x55);

extern "C" void app_main() {
  gyro.begin();
  // Jump detection task
  xTaskCreate(
      [](void *) {
        while (true) {
          jumpDetector.update();
          vTaskDelay(pdMS_TO_TICKS(10)); // check every 10ms
        }
      },
      "JumpDetectTask", 2048, nullptr, 5, nullptr);
  xTaskCreate(
      [](void *) {
        uint32_t timestamp;
        while (true) {
          if (jumpDetector.getJump(timestamp, portMAX_DELAY)) {
            printf("Jump detected at %lu ms\n", timestamp);
          }
          display.drawMainMenu();
        }
      },
      "JumpHandlerTask", 2048, nullptr, 5, nullptr);
}

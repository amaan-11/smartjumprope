#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_pin.h"
#include "menu.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdint.h>
#include "mutex.h"
#include "components/gyro/gyro.h"
#include "jump.h"

SensorReading gyro(0, 21, 22); // i2c_port, sda_pin, scl_pin
JumpDetector jumpDetector(&gyro);

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

  // Task that handles jumps (display, logging, cloud)
  xTaskCreate(
      [](void *) {
        uint32_t timestamp;
        while (true) {
          if (jumpDetector.getJump(timestamp, portMAX_DELAY)) {
            printf("Jump detected at %u ms\n", timestamp);
            // TODO: update display, send to cloud, etc.
          }
        }
      },
      "JumpHandlerTask", 2048, nullptr, 5, nullptr);
}

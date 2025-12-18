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


SensorReading gyro(I2C_NUM_0, 19, 20);
JumpDetector jumpDetector(&gyro);

//OledDisplay display(I2C_NUM_0, 31, 31, 0x55);
extern "C" void app_main() {
  printf("Hello, world!\n");
  gyro.begin();
  gyro.startTask(); // Start background reading

  // Task to consume and display queue data
  xTaskCreate(
      [](void *param) {
        QueueHandle_t queue = ((SensorReading *)param)->getQueue();
        mpu_data_t data;

        while (true) {
          if (xQueueReceive(queue, &data, portMAX_DELAY) == pdTRUE) {
            printf("Accel: X=%.2f Y=%.2f Z=%.2f g | Gyro: X=%.2f Y=%.2f Z=%.2f "
                   "dps\n",
                   data.ax_g, data.ay_g, data.az_g, data.gx_dps, data.gy_dps,
                   data.gz_dps);
          }
          vTaskDelay(400);
        }
      },
      "DisplayData", 2048, &gyro, 5, nullptr);

  // Jump detection task
  xTaskCreate(
      [](void *) {
        while (true) {
          jumpDetector.update();
          vTaskDelay(pdMS_TO_TICKS(10));
        }
      },
      "JumpDetectTask", 2048, nullptr, 5, nullptr);
}

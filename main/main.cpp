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
#include "components/display/ssd1366.h"
extern "C" void app_main()
{
  static Menu menu;
  static SensorReading GyroSensor;

  while (true)
  {
    menu.update();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
#include "menu.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <cmath>
#include <cstdio>
#include <memory>
#include "esp_log.h"
#include "driver/gpio.h"

extern "C" void app_main()
{
  static Menu menu;

  while (true)
  {
    menu.update();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

extern "C" void app_main(void)
{
  printf("Hello ESP32-C6!\n");
  while (1)
  {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}

#include "display.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gpio_pin.h"
#include "gyro.h"
#include "jump.h"
#include "mutex.h"
#include <cmath>
#include <cstdio>
#include <memory>
#include <stdint.h>

#include "display.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>

static const char *TAG = "DISPLAY_TEST";

// I2C scanner function
void i2c_scanner(i2c_port_t port) {
  ESP_LOGI(TAG, "Scanning I2C bus...");

  for (uint8_t addr = 1; addr < 127; addr++) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret == ESP_OK) {
      ESP_LOGI(TAG, ">>> Found device at address: 0x%02X <<<", addr);
    }
  }
  ESP_LOGI(TAG, "Scan complete!");
}

extern "C" void app_main() {
  printf("OLED Display Test\n");

  // First, run I2C scanner to find all devices
  ESP_LOGI(TAG, "Step 1: Running I2C scanner to find devices...");

  i2c_config_t conf = {};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = 19; // Same as gyro
  conf.scl_io_num = 20; // Same as gyro
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = 400000;

  i2c_param_config(I2C_NUM_0, &conf);
  i2c_driver_install(I2C_NUM_0, conf.mode, 0, 0, 0);

  i2c_scanner(I2C_NUM_0);
  vTaskDelay(pdMS_TO_TICKS(1000));

  // Now try to initialize display with common addresses
  ESP_LOGI(TAG, "\nStep 2: Attempting to initialize OLED display...");

  // Extended list of possible OLED addresses
  uint8_t addresses[] = {0x3C, 0x3D, 0x78, 0x7A};
  bool display_found = false;
  OledDisplay *display = nullptr;

  for (int i = 0; i < sizeof(addresses); i++) {
    ESP_LOGI(TAG, "Trying OLED at address 0x%02X...", addresses[i]);

    // Delete previous I2C driver to avoid conflicts
    i2c_driver_delete(I2C_NUM_0);
    vTaskDelay(pdMS_TO_TICKS(100));

    display = new OledDisplay(I2C_NUM_0, 19, 20, addresses[i]);

    esp_err_t ret = display->init();

    if (ret == ESP_OK) {
      ESP_LOGI(TAG, "✓✓✓ Display initialized successfully at 0x%02X! ✓✓✓",
               addresses[i]);
      display_found = true;
      break;
    } else {
      ESP_LOGW(TAG, "✗ Failed at 0x%02X: %s", addresses[i],
               esp_err_to_name(ret));
      delete display;
      display = nullptr;
    }
  }

  if (!display_found) {
    ESP_LOGE(TAG, "\n*** No display found at any address! ***");
    ESP_LOGE(TAG, "Check your wiring:");
    ESP_LOGE(TAG, "  - SDA connected to GPIO 6?");
    ESP_LOGE(TAG, "  - SCL connected to GPIO 7?");
    ESP_LOGE(TAG, "  - VCC and GND connected?");
    ESP_LOGE(TAG, "  - Display powered on?");
    return;
  }

  // Display test sequence
  ESP_LOGI(TAG, "\nStarting display test sequence...");

  // Test 1: Clear screen
  ESP_LOGI(TAG, "Test 1: Clear screen");
  display->clear();
  display->commit();
  vTaskDelay(pdMS_TO_TICKS(1000));

  // Test 2: Draw single character
  ESP_LOGI(TAG, "Test 2: Single character");
  display->clear();
  display->drawChar(10, 10, 'A');
  display->commit();
  vTaskDelay(pdMS_TO_TICKS(2000));

  // Test 3: Draw string
  ESP_LOGI(TAG, "Test 3: String test");
  display->clear();
  display->drawString(0, 0, "Hello!");
  display->drawString(0, 10, "OLED Works");
  display->commit();
  vTaskDelay(pdMS_TO_TICKS(2000));

  // Test 4: Draw main menu
  ESP_LOGI(TAG, "Test 4: Main menu");
  display->drawMainMenu();
  vTaskDelay(pdMS_TO_TICKS(3000));

  // Test 5: Animated counter
  ESP_LOGI(TAG, "Test 5: Animated counter");
  for (int i = 0; i < 10; i++) {
    display->clear();
    display->drawString(20, 20, "Count:");

    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%d", i);
    display->drawString(60, 20, buffer);

    display->commit();
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  // Test 6: Continuous update loop
  ESP_LOGI(TAG, "Test 6: Continuous update");
  int counter = 0;
  while (true) {
    display->clear();

    display->drawString(10, 0, "Running...");

    char buf[32];
    snprintf(buf, sizeof(buf), "Count: %d", counter++);
    display->drawString(10, 20, buf);

    snprintf(buf, sizeof(buf), "Time: %llu", esp_timer_get_time() / 1000000);
    display->drawString(10, 40, buf);

    display->commit();
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
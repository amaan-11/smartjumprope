#pragma once

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_panel_vendor.h"
#include <cstdint>

constexpr uint8_t DISPLAY_ADDR = 0x3C;

class OledDisplay {
public:
  OledDisplay(); // Auto-initializes on construction

  void clear();
  void commit();

  void drawChar(int x, int y, char c);
  void drawString(int x, int y, const char *str);
  void drawMainMenu();
  void drawJumps(uint64_t jumps);
  void drawTimer(uint64_t cur_time);
  void drawCalories(uint16_t cals);

  bool isInitialized() const { return _initialized; }

private:
  void sendCommand(uint8_t cmd);
  void initSSD1306();
  void init();
  void cleanup();
  void drawCharInternal(int x, int y, char c);

  bool _initialized;
  const uint8_t _i2c_addr;

  esp_lcd_panel_io_handle_t _io;
  esp_lcd_panel_handle_t _panel;

  static constexpr int WIDTH = 128;
  static constexpr int HEIGHT = 64;

  uint8_t _framebuffer[WIDTH * HEIGHT / 8];
};
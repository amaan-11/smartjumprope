#include "display.h"
#include "esp_log.h"
#include "font5x7.h"
#include <cstring>

static const char *TAG = "OLED_DISPLAY";

const uint8_t c = 32;
const uint8_t *glyph = font5x7[c - FONT_FIRST_CHAR];

OledDisplay::OledDisplay(i2c_port_t i2c_port, int sda_pin, int scl_pin,
                         uint8_t i2c_addr)
    : _i2c_port(i2c_port), _sda_pin(sda_pin), _scl_pin(scl_pin),
      _i2c_addr(i2c_addr), _io(nullptr), _panel(nullptr) {
  clear();
}

esp_err_t OledDisplay::init() {
  esp_err_t ret;

  // Initialize I2C
  ret = initI2C();
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C init failed: %s", esp_err_to_name(ret));
    return ret;
  }

  // Configure panel I/O
  esp_lcd_panel_io_i2c_config_t io_cfg = {};
  io_cfg.dev_addr = _i2c_addr;
  io_cfg.control_phase_bytes = 1;
  io_cfg.lcd_cmd_bits = 8;
  io_cfg.lcd_param_bits = 8;

  ret = esp_lcd_new_panel_io_i2c(_i2c_port, &io_cfg, &_io);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
    return ret;
  }

  // Configure panel
  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.bits_per_pixel = 1;
  panel_cfg.reset_gpio_num = -1;

  ret = esp_lcd_new_panel_ssd1306(_io, &panel_cfg, &_panel);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to create SSD1306 panel: %s", esp_err_to_name(ret));
    if (_io) {
      esp_lcd_panel_io_del(_io);
      _io = nullptr;
    }
    return ret;
  }

  // Reset panel
  ret = esp_lcd_panel_reset(_panel);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Panel reset failed: %s", esp_err_to_name(ret));
    cleanup();
    return ret;
  }

  // Initialize panel
  ret = esp_lcd_panel_init(_panel);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Panel init failed: %s", esp_err_to_name(ret));
    cleanup();
    return ret;
  }

  // Turn on display
  ret = esp_lcd_panel_disp_on_off(_panel, true);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "Failed to turn on display: %s", esp_err_to_name(ret));
    cleanup();
    return ret;
  }

  clear();
  commit();

  ESP_LOGI(TAG, "Display initialized successfully at 0x%02X", _i2c_addr);
  return ESP_OK;
}

esp_err_t OledDisplay::initI2C() {
  i2c_config_t conf = {};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = _sda_pin;
  conf.scl_io_num = _scl_pin;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = 400000;

  esp_err_t ret = i2c_param_config(_i2c_port, &conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
    return ret;
  }

  ret = i2c_driver_install(_i2c_port, conf.mode, 0, 0, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
    return ret;
  }

  return ESP_OK;
}

void OledDisplay::cleanup() {
  if (_panel) {
    esp_lcd_panel_del(_panel);
    _panel = nullptr;
  }
  if (_io) {
    esp_lcd_panel_io_del(_io);
    _io = nullptr;
  }
}

void OledDisplay::clear() { memset(_framebuffer, 0x00, sizeof(_framebuffer)); }

void OledDisplay::commit() {
  if (_panel) {
    esp_lcd_panel_draw_bitmap(_panel, 0, 0, WIDTH, HEIGHT, _framebuffer);
  }
}

void OledDisplay::drawChar(int x, int y, char c) { drawCharInternal(x, y, c); }

void OledDisplay::drawString(int x, int y, const char *str) {
  int cursor = x;
  while (*str) {
    drawCharInternal(cursor, y, *str++);
    cursor += 6; // 5px font + 1px space
  }
}

void OledDisplay::drawCharInternal(int x, int y, char c) {
  if (c < 32 || c > 127)
    return;
  const uint8_t *glyph = font5x7[c - 32];

  for (int col = 0; col < 5; col++) {
    uint8_t line = glyph[col];
    for (int row = 0; row < 7; row++) {
      if (line & (1 << row)) {
        int px = x + col;
        int py = y + row;
        if (px < 0 || px >= WIDTH || py < 0 || py >= HEIGHT)
          continue;

        int index = px + (py / 8) * WIDTH;
        _framebuffer[index] |= (1 << (py % 8));
      }
    }
  }
}

void OledDisplay::drawMainMenu() {
  clear();
  drawString(0, 0, "jump");
  drawString(20, 20, "time");
  drawString(35, 40, "calories");
  commit();
}

void OledDisplay::drawJumps(const uint64_t jumps) {
  clear();
  drawString(10, 10, "jump");
  commit();
}

void OledDisplay::drawTimer(const uint64_t cur_time) {
  clear();
  drawString(30, 30, "time");
  commit();
}

void OledDisplay::drawCalories(const uint16_t cals) {
  clear();
  drawString(50, 50, "calories");
  commit();
}
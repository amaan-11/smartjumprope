#include "display.h"
#include "esp_log.h"
#include "font5x7.h"
#include "i2cInit.h"
#include "mutex.h"
#include <cstring>

static const char *TAG = "OLED_DISPLAY";

OledDisplay::OledDisplay()
    : _initialized(false), _i2c_addr(DISPLAY_ADDR), _io(nullptr),
      _panel(nullptr) {
  clear();
  init();
}

void OledDisplay::init() {
  I2CManager &i2c = I2CManager::getInstance();

  if (!i2c.isInitialized()) {
    ESP_LOGE(TAG, "I2C Manager not initialized!");
    return;
  }

  ESP_LOGI(TAG, "Initializing OLED display at 0x%02X...", _i2c_addr);

  {
    MutexGuard lock(i2c.getMutex());
    ESP_LOGI(TAG, "[DISPLAY] Acquired I2C lock for initialization");

    esp_err_t ret;

    // Configure panel I/O
    esp_lcd_panel_io_i2c_config_t io_cfg = {};
    io_cfg.dev_addr = _i2c_addr;
    io_cfg.control_phase_bytes = 1;
    io_cfg.lcd_cmd_bits = 8;
    io_cfg.lcd_param_bits = 8;

    ret = esp_lcd_new_panel_io_i2c(i2c.getPort(), &io_cfg, &_io);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to create panel IO: %s", esp_err_to_name(ret));
      ESP_LOGI(TAG, "[DISPLAY] Released I2C lock (failed at IO creation)");
      return;
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
      ESP_LOGI(TAG, "[DISPLAY] Released I2C lock (failed at panel creation)");
      return;
    }

    // Reset panel
    ret = esp_lcd_panel_reset(_panel);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Panel reset failed: %s", esp_err_to_name(ret));
      cleanup();
      ESP_LOGI(TAG, "[DISPLAY] Released I2C lock (failed at reset)");
      return;
    }

    // Initialize panel
    ret = esp_lcd_panel_init(_panel);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Panel init failed: %s", esp_err_to_name(ret));
      cleanup();
      ESP_LOGI(TAG, "[DISPLAY] Released I2C lock (failed at init)");
      return;
    }

    // Turn on display
    ret = esp_lcd_panel_disp_on_off(_panel, true);
    if (ret != ESP_OK) {
      ESP_LOGE(TAG, "Failed to turn on display: %s", esp_err_to_name(ret));
      cleanup();
      ESP_LOGI(TAG, "[DISPLAY] Released I2C lock (failed at turn on)");
      return;
    }

    clear();
    commit();

    ESP_LOGI(TAG, "[DISPLAY] Released I2C lock (success)");
  }

  _initialized = true;
  ESP_LOGI(TAG, "Display initialized successfully at 0x%02X", _i2c_addr);
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
  if (!_initialized || !_panel) {
    return;
  }

  I2CManager &i2c = I2CManager::getInstance();

  {
    MutexGuard lock(i2c.getMutex());
    // ESP_LOGD(TAG, "[DISPLAY] Acquired I2C lock for commit");

    esp_lcd_panel_draw_bitmap(_panel, 0, 0, WIDTH, HEIGHT, _framebuffer);

    // ESP_LOGD(TAG, "[DISPLAY] Released I2C lock");
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
  if (c < 0 || c > 127) return;

  const uint8_t* glyph = (const uint8_t*)font8x8_basic[(unsigned char)c];

  // Loop over 8 rows (like console)
  for (int row = 0; row < 8; row++) {
      uint8_t byte = glyph[row]; // row data

      // Loop over 8 columns (left to right = bit 0 to bit 7)
      for (int col = 0; col < 8; col++) {
          if ((byte >> col) & 1) { // LSB = leftmost → col 0
              int px = x + col;    // horizontal position
              int py = y + row;    // vertical position

              // Bounds check
              if (px < 0 || px >= WIDTH || py < 0 || py >= HEIGHT)
                  continue;

              // Write to vertical-mode framebuffer
              int byteIndex = px + (py / 8) * WIDTH;
              int bitIndex  = py % 8;
              _framebuffer[byteIndex] |= (1 << bitIndex);
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
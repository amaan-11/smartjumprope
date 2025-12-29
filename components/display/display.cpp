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
  initSSD1306(); // NEW

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
  if (!_initialized)
    return;

  uint8_t col_cmd[] = {0x21, 0, uint8_t(WIDTH - 1)};
  uint8_t page_cmd[] = {0x22, 0, uint8_t(HEIGHT / 8 - 1)};
  sendCommand(col_cmd[0]);
  sendCommand(col_cmd[1]);
  sendCommand(col_cmd[2]);
  sendCommand(page_cmd[0]);
  sendCommand(page_cmd[1]);
  sendCommand(page_cmd[2]);

  // Prepend 0x40 control byte for data
  uint8_t buf[WIDTH * HEIGHT / 8 + 1];
  buf[0] = 0x40;
  memcpy(buf + 1, _framebuffer, WIDTH * HEIGHT / 8);

  I2CManager &i2c = I2CManager::getInstance();
  MutexGuard lock(i2c.getMutex());
  esp_err_t ret = i2c_master_write_to_device(i2c.getPort(), _i2c_addr, buf,
                                             sizeof(buf), pdMS_TO_TICKS(1000));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
  }
}
void OledDisplay::drawChar(int x, int y, char c) { drawCharInternal(x, y, c); }

void OledDisplay::drawString(int x, int y, const char *str) {
  int cursor = x;
  while (*str) {
    drawCharInternal(cursor, y, *str++);
    cursor += 8; // 5px font + 1px space
  }
}

void OledDisplay::drawCharInternal(int x, int y, char c) {
  if (c < 0 || c > 127)
    return;

  const uint8_t *glyph =
      reinterpret_cast<const uint8_t *>(font8x8_basic[(uint8_t)c]);

  for (int row = 0; row < 8; row++) {
    uint8_t rowBits = glyph[row];

    for (int col = 0; col < 8; col++) {
      if (rowBits & (1 << (7 - col))) { // FIXED bit order
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
  drawString(0, 0, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
  drawString(0, 16, "abcdefghijklmnopqrstuvwxyz");
  drawString(0, 32, "0123456789");
  drawString(0, 48, "!@#$%^&*()");
  commit();

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

void OledDisplay::sendCommand(uint8_t cmd) {
  uint8_t buf[2] = {0x80, cmd}; // Control byte = command

  I2CManager &i2c = I2CManager::getInstance();
  MutexGuard lock(i2c.getMutex());

  esp_err_t ret = i2c_master_write_to_device(i2c.getPort(), _i2c_addr, buf,
                                             sizeof(buf), pdMS_TO_TICKS(1000));
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C write failed: %s", esp_err_to_name(ret));
  }
}

void OledDisplay::initSSD1306() {
  // Basic SSD1306 initialization sequence
  sendCommand(0xAE); // Display off
  sendCommand(0x20);
  sendCommand(0x00); // Memory mode = horizontal
  sendCommand(0x40); // Start line = 0
  sendCommand(0xA1); // Segment remap
  sendCommand(0xC0); // COM scan direction remapped
  sendCommand(0xA8);
  sendCommand(HEIGHT - 1); // Multiplex ratio
  sendCommand(0xD3);
  sendCommand(0x00); // Display offset
  sendCommand(0xDA);
  sendCommand(0x12); // COM pins
  sendCommand(0x81);
  sendCommand(0xFF); // Contrast
  sendCommand(0xD9);
  sendCommand(0xF1); // Pre-charge
  sendCommand(0xDB);
  sendCommand(0x30); // VCOMH deselect
  sendCommand(0xA4); // Entire display on follow RAM
  sendCommand(0xA6); // Normal display
  sendCommand(0x8D);
  sendCommand(0x14); // Charge pump
  sendCommand(0xAF); // Display on
}

void OledDisplay::testTextOrientations() {
  const char *testStr = "b d p q";

  // Possible hardware configs
  struct Config {
    uint8_t segRemap;
    uint8_t comScan;
    const char *name;
  } configs[] = {
      {0xA0, 0xC0, "Normal"},
      {0xA0, 0xC8, "Vertical flip"},
      {0xA1, 0xC0, "Horizontal flip"},
      {0xA1, 0xC8, "Both flips"},
  };

  // Optional: also test vertical bit-flip in framebuffer
  bool flipBitsOptions[] = {false, true};

  for (auto &cfg : configs) {
    for (bool flipBits : flipBitsOptions) {
      clear();

      // Apply hardware config
      sendCommand(cfg.segRemap);
      sendCommand(cfg.comScan);

      // Draw string with optional bit flip
      int cursor = 0;
      while (*testStr) {
        if (flipBits) {
          drawCharFlipped(cursor, 0, *testStr++);
        } else {
          drawCharInternal(cursor, 0, *testStr++);
        }
        cursor += 8;
      }

      commit();

      ESP_LOGI("OLED_TEST", "Displayed with %s, flipBits=%d", cfg.name,
               flipBits);

      vTaskDelay(pdMS_TO_TICKS(2000)); // pause 2s to see
    }
  }
}
void OledDisplay::drawCharFlipped(int x, int y, char c) {
  if (c < 0 || c > 127)
    return;

  // Correctly cast from const char* to const uint8_t*
  const uint8_t *glyph =
      reinterpret_cast<const uint8_t *>(font8x8_basic[(uint8_t)c]);

  for (int row = 0; row < 8; row++) {
    uint8_t rowBits = glyph[row];

    // Bit-reverse the row to flip horizontally
    rowBits = ((rowBits & 0xF0) >> 4) | ((rowBits & 0x0F) << 4);
    rowBits = ((rowBits & 0xCC) >> 2) | ((rowBits & 0x33) << 2);
    rowBits = ((rowBits & 0xAA) >> 1) | ((rowBits & 0x55) << 1);

    for (int col = 0; col < 8; col++) {
      if (rowBits & (1 << (7 - col))) {
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

// Draw a full string with flipped characters
void OledDisplay::drawStringFlipped(int x, int y, const char *str) {
  int cursor = x;
  while (*str) {
    drawCharFlipped(cursor, y, *str); // pass single char
    cursor += 8;                      // advance cursor by font width + spacing
    str++;
  }
}

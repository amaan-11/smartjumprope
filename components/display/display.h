#pragma once

#include "driver/i2c.h"
#include "esp_err.h"
#include "esp_lcd_io_i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_panel_vendor.h"
#include <cstdint>

class OledDisplay {
public:
    OledDisplay(
        i2c_port_t i2c_port,
        int sda_pin,
        int scl_pin,
        uint8_t i2c_addr = 0x3C
    );

    esp_err_t init();

    void clear();
    void commit();

    void drawChar(int x, int y, char c);
    void drawString(int x, int y, const char *str);

private:
    void initI2C();
    void drawCharInternal(int x, int y, char c);

    i2c_port_t _i2c_port;
    int _sda_pin;
    int _scl_pin;
    uint8_t _i2c_addr;

    esp_lcd_panel_io_handle_t _io;
    esp_lcd_panel_handle_t _panel;

    static constexpr int WIDTH = 128;
    static constexpr int HEIGHT = 64;

    uint8_t _framebuffer[WIDTH * HEIGHT / 8];
};

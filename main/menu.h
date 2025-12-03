#pragma once

#include "driver/i2c.h"
#include "components/display/ssd1366.h"

class Menu
{
public:
  Menu();        // Constructor initializes everything
  void update(); // Draw menu items and react to input

private:
  void initI2C();
  void initDisplay();
  void initMenuState();

private:
  ssd1306_handle_t display;
  uint_8t currentMenu = 0;
};

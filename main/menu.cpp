#include "menu.h"
#include "esp_log.h"

static const char *TAG = "Menu";

Menu::Menu()
{
  initI2C();
  initDisplay();
  initMenuState();
}

void Menu::initI2C()
{
  i2c_config_t config = {};
  config.mode = I2C_MODE_MASTER;
  config.sda_io_num = GPIO_NUM_14;
  config.scl_io_num = GPIO_NUM_15;
  config.sda_pullup_en = GPIO_PULLUP_ENABLE;
  config.scl_pullup_en = GPIO_PULLUP_ENABLE;
  config.master.clk_speed = 400000;

  i2c_param_config(I2C_NUM_0, &config);
  i2c_driver_install(I2C_NUM_0, config.mode, 0, 0, 0);
}

void Menu::initDisplay()
{
  display = ssd1306_create(I2C_NUM_0, 0x3C);

  ssd1306_clear_screen(display, 0x00);
  ssd1306_refresh(display, true);
}

void Menu::initMenuState()
{
  currentMenu = 0;

  ssd1306_draw_string(display, 0, 0, (uint8_t *)"Menu Loaded", 12, 1);
  ssd1306_refresh(display, true);
}

void Menu::update()
{
}

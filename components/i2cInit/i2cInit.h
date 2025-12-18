#pragma once

#include "driver/i2c.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class I2CManager {
public:
  static I2CManager &getInstance();

  // Get the I2C port number
  i2c_port_t getPort() const { return _i2c_port; }

  // Get mutex for RAII guard
  SemaphoreHandle_t getMutex() const { return _mutex; }

  // Check if initialized
  bool isInitialized() const { return _initialized; }

  // Delete copy constructor and assignment operator
  I2CManager(const I2CManager &) = delete;
  I2CManager &operator=(const I2CManager &) = delete;

private:
  I2CManager();
  ~I2CManager();

  i2c_port_t _i2c_port;
  int _sda_pin;
  int _scl_pin;
  uint32_t _clk_speed;
  bool _initialized;

  SemaphoreHandle_t _mutex;

  void init();
};
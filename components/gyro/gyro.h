#pragma once

#include "driver/i2c.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <cstdint>

constexpr uint8_t MPU_ADDR = 0x68;

typedef struct {
  float ax_g, ay_g, az_g;
  float gx_dps, gy_dps, gz_dps;
} mpu_data_t;

class SensorReading {
public:
  // Singleton access
  static SensorReading &getInstance() {
    static SensorReading instance;
    return instance;
  }

  // Public API
  void startTask();
  QueueHandle_t getQueue() const { return data_queue; }

  bool isInitialized() const { return _initialized; }

  esp_err_t readRaw(int16_t &ax, int16_t &ay, int16_t &az, int16_t &gx,
                    int16_t &gy, int16_t &gz);

private:
  // Prevent accidental copies
  SensorReading();
  SensorReading(const SensorReading &) = delete;
  SensorReading &operator=(const SensorReading &) = delete;

  // Internal state
  bool _initialized;
  float accel_sensitivity;
  float gyro_sensitivity;

  QueueHandle_t data_queue;

  // Internal methods
  void init();
  void readSensitivity();
  void taskLoop();

  static void taskEntry(void *param);
};

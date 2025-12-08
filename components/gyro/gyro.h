#pragma once

#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct {
  float ax_g, ay_g, az_g;
  float gx_dps, gy_dps, gz_dps;
} mpu_data_t;

class SensorReading {
public:
  SensorReading(int i2c_port, int sda_pin, int scl_pin);

  esp_err_t begin();
  void startTask(); // start background reader task
  QueueHandle_t getQueue() const { return data_queue; }

private:
  int _i2c_port, _sda_pin, _scl_pin;

  const float accel_sensitivity = 16384.0f; // default ±2g
  const float gyro_sensitivity = 131.0f;    // default ±250 deg/s

  QueueHandle_t data_queue;

  esp_err_t readRaw(int16_t &ax, int16_t &ay, int16_t &az, int16_t &gx,
                    int16_t &gy, int16_t &gz);

  void readSensitivity(); // reads ACCEL_CONFIG, GYRO_CONFIG
  void taskLoop();        // background task

  static void taskEntry(void *param);
};

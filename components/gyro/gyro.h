#pragma once

#include "driver/i2c_master.h"
#include "driver/i2c_slave.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

constexpr uint8_t MPU_ADDR = 0x68;

typedef struct {
  float ax_g, ay_g, az_g;
  float gx_dps, gy_dps, gz_dps;
} mpu_data_t;

class SensorReading {
public:
  SensorReading(); // Auto-initializes on construction

  void startTask(); // start background reader task
  QueueHandle_t getQueue() const { return data_queue; }
  esp_err_t readRaw(int16_t &ax, int16_t &ay, int16_t &az, int16_t &gx,
                    int16_t &gy, int16_t &gz);

  bool isInitialized() const { return _initialized; }

private:
  bool _initialized;
  float accel_sensitivity = 16384.0f; // default ±2g
  float gyro_sensitivity = 131.0f;    // default ±250 deg/s

  QueueHandle_t data_queue;

  void init();
  void readSensitivity(); // reads ACCEL_CONFIG, GYRO_CONFIG
  void taskLoop();        // background task

  static void taskEntry(void *param);
};
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gyro.h"
#include <cstdint>

class JumpDetector {
public:
  JumpDetector(SensorReading *gyro, float thresholdFactor = 0.8,
               uint32_t minIntervalMs = 300);

  // Call this periodically from a task
  void update();

  // Queue access
  bool getJump(uint32_t &timestampMs, TickType_t waitTicks = 0);

private:
  SensorReading *_gyro;

  float _avgJump;          // running average of peak-to-valley amplitude
  float _thresholdFactor;  // fraction of average to consider a jump
  uint32_t _minIntervalMs; // minimum time between jumps

  float _lastValue;
  float _peak;
  float _valley;
  bool _isRising;

  uint32_t _lastJumpTime; // timestamp of last detected jump

  QueueHandle_t _jumpQueue;

  uint32_t getMillis(); // helper for current time in ms
};


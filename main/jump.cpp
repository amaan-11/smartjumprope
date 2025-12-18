#include "jump.h"
#include "esp_timer.h"
#include <algorithm>

//I (922) MPU: Accel sensitivity: 16384.000000
//I (922) MPU: Gyro sensitivity: 131.000000

JumpDetector::JumpDetector(SensorReading *gyro, float thresholdFactor,
                           uint32_t minIntervalMs)
    : _gyro(gyro), _avgJump(20.0f), _thresholdFactor(thresholdFactor),
      _minIntervalMs(minIntervalMs), _lastValue(0.0f), _peak(-1e6f),
      _valley(1e6f), _isRising(false), _lastJumpTime(0) {
  _jumpQueue =
      xQueueCreate(10, sizeof(uint32_t)); // queue of 10 jump timestamps
}

uint32_t JumpDetector::getMillis() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void JumpDetector::update() {
  int16_t ax, ay, az, gx, gy, gz;
  if (_gyro->readRaw(ax, ay, az, gx, gy, gz) != ESP_OK) {
    return; // failed read
  }

  float gyroVal = static_cast<float>(gy); // example axis

  if (gyroVal > _lastValue) {
    _isRising = true;
  } else if (gyroVal < _lastValue && _isRising) {
    _peak = _lastValue;
    _isRising = false;
  }

  if (!_isRising && gyroVal < _lastValue) {
    _valley = _lastValue;
    float diff = _peak - _valley;

    uint32_t now = getMillis();
    if (diff > _thresholdFactor * _avgJump &&
        now - _lastJumpTime > _minIntervalMs) {
      _lastJumpTime = now;
      _avgJump = 0.9f * _avgJump + 0.1f * diff;

      // push to queue, non-blocking
      xQueueSendToBack(_jumpQueue, &now, 0);
    }
  }

  _lastValue = gyroVal;
}

bool JumpDetector::getJump(uint32_t &timestampMs, TickType_t waitTicks) {
  return xQueueReceive(_jumpQueue, &timestampMs, waitTicks) == pdTRUE;
}

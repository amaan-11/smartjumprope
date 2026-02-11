#ifndef JUMP_H
#define JUMP_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gyro.h"
#include <cstdint>

// Number of different timing configurations to test
#define NUM_TIMING_CONFIGS 5

// Structure to track jumps for one configuration
struct JumpConfig {
  uint32_t minRiseDuration;
  uint32_t minFallDuration;
  uint32_t jumpCount;
  uint32_t lastJumpTime;

  // State tracking
  float peak;
  float valley;
  bool isRising;
  bool peakConfirmed;
  uint32_t risingStartTime;
  uint32_t fallingStartTime;
  float lastValue;
};

// Structure to track jumps for one axis
struct AxisDetector {
  char name[8]; // "X", "Y", or "Z"
  JumpConfig configs[NUM_TIMING_CONFIGS];
};

class JumpDetector {
public:
  JumpDetector(SensorReading *gyro, float thresholdFactor = 1.5f,
               uint32_t minIntervalMs = 200);

  void update();

  // Get jump counts for display
  void getCounts(uint32_t countsX[NUM_TIMING_CONFIGS],
                 uint32_t countsY[NUM_TIMING_CONFIGS],
                 uint32_t countsZ[NUM_TIMING_CONFIGS]);

  // Get configuration info
  void getTimingConfig(int configIndex, uint32_t &riseDuration,
                       uint32_t &fallDuration);

private:
  uint32_t getMillis();
  void updateAxis(AxisDetector &axis, float gyroValue, uint32_t now);
  void updateConfig(JumpConfig &config, float gyroValue, uint32_t now);

  SensorReading *_gyro;
  float _thresholdFactor;
  uint32_t _minIntervalMs;
  float _avgJump;

  // Three axis detectors
  AxisDetector _axisX;
  AxisDetector _axisY;
  AxisDetector _axisZ;
};

#endif // JUMP_H
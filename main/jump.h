#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "gyro.h"
#include <cstdint>

// The "dae9b47-style" algorithm tests multiple timing configs.
// Keep this here so main.cpp can also use it if needed.
#define NUM_TIMING_CONFIGS 4

class JumpDetector {
public:
  // Keep your current constructor signature so your current main.cpp still builds.
  JumpDetector(SensorReading *gyro, float thresholdFactor = 1.3f,
               uint32_t minIntervalMs = 300);

  // Call this periodically from a task
  void update();

  // Queue access (event-based): each event = one detected jump
  bool getJump(uint32_t &timestampMs, TickType_t waitTicks = 0);

  // Optional: lets you show calibration status later (doesn't break old code)
  bool isCalibrated() const { return _calibrationComplete; }

  // Optional debug helpers (safe even if you don't use them)
  void getCounts(uint32_t countsX[NUM_TIMING_CONFIGS],
                 uint32_t countsY[NUM_TIMING_CONFIGS],
                 uint32_t countsZ[NUM_TIMING_CONFIGS]) const;

private:
  enum PhaseState : uint8_t {
    STATE_IDLE = 0,
    STATE_RISING,
    STATE_FALLING
  };

  struct JumpConfig {
    uint32_t minRiseDuration;
    uint32_t minFallDuration;

    uint32_t jumpCount;
    uint32_t lastJumpTime;

    PhaseState state;

    float peak;
    float valley;

    uint32_t risingStartTime;
    uint32_t fallingStartTime;

    float lastValue;
    float filteredFast;
    float filteredSlow;
  };

  struct AxisDetector {
    JumpConfig configs[NUM_TIMING_CONFIGS];
  };

  SensorReading *_gyro;

  float _thresholdFactor;
  uint32_t _minIntervalMs;

  // Adaptive threshold state (shared across axes/configs like dae9b47 code)
  float _avgJump;
  bool _calibrationComplete;
  uint32_t _calibrationJumps;

  // Per-axis detectors (we use GYRO X/Y/Z)
  AxisDetector _axisX;
  AxisDetector _axisY;
  AxisDetector _axisZ;

  // Event queue
  QueueHandle_t _jumpQueue;

  // Track “best total so far” so we can convert counts -> jump events
  uint32_t _lastBestTotal;

private:
  uint32_t getMillis() const;

  void initAxis(AxisDetector &axis);

  void updateAxis(AxisDetector &axis, float value, uint32_t now);
  void updateConfig(JumpConfig &config, float value, uint32_t now);

  static uint32_t maxConfig(const uint32_t counts[NUM_TIMING_CONFIGS]);
  static uint32_t maxConfigFromAxis(const AxisDetector &axis);

  uint32_t computeBestTotal() const;
};
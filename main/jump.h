#ifndef JUMP_H
#define JUMP_H

#include "gyro.h"
#include <cstdint>

// Number of timing configurations to test
#define NUM_TIMING_CONFIGS 4

// Sensor type enumeration
enum SensorType { SENSOR_GYRO, SENSOR_ACCEL };

// Detector state machine
enum DetectorState { STATE_IDLE, STATE_RISING, STATE_FALLING };

// Configuration for a single timing pattern
struct JumpConfig {
  uint32_t minRiseDuration; // Expected rise time (ms)
  uint32_t minFallDuration; // Expected fall time (ms)
  uint32_t jumpCount;       // Jumps detected with this config
  uint32_t lastJumpTime;    // Last jump timestamp (ms)

  DetectorState state;       // Current state machine state
  float peak;                // Current peak value
  float valley;              // Current valley value
  uint32_t risingStartTime;  // When rise phase started
  uint32_t fallingStartTime; // When fall phase started

  float lastValue;    // Previous filtered value
  float filteredFast; // Fast filter for peak detection
  float filteredSlow; // Slow filter for baseline
};

// Single axis detector
struct AxisDetector {
  char name[8];                           // "X", "Y", "Z"
  JumpConfig configs[NUM_TIMING_CONFIGS]; // All timing configurations
};

class JumpDetector {
public:
  JumpDetector(SensorReading *sensor,
               float thresholdFactor = 1.3f, uint32_t minIntervalMs = 300);

  void update();
  void jumpDetectionTask();
  esp_err_t readAccelRaw(int16_t &ax, int16_t &ay, int16_t &az);

  void getCounts(
                 uint32_t countsZ[NUM_TIMING_CONFIGS]);

  void getTimingConfig(int configIndex, uint32_t &riseDuration,
                       uint32_t &fallDuration);

  void getTotalJumps(uint32_t &totalZ) const;

  void getAverageRates(float &rateZ) const;

  bool isCalibrated() const;

  const char *getName() const;

private:
  SensorReading *_sensor;
  float _thresholdFactor;
  uint32_t _minIntervalMs;

  AxisDetector _axisZ;

  float _avgJump;             // Adaptive threshold baseline
  bool _calibrationComplete;  // Calibration status
  uint32_t _calibrationJumps; // Jumps during calibration

  uint32_t getMillis();

  void updateAxis(AxisDetector &axis, float value, uint32_t now);

  void updateConfig(JumpConfig &config, float value, uint32_t now);

  int getConfigIndex(JumpConfig *config);

  uint32_t getAxisTotal(const AxisDetector &axis) const;

  float getAxisRate(const AxisDetector &axis) const;
};

#endif // JUMP_H
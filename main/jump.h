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
  /**
   * Constructor
   * @param sensor Pointer to sensor reading object
   * @param type Sensor type (SENSOR_GYRO or SENSOR_ACCEL)
   * @param thresholdFactor Multiplier for adaptive threshold (e.g., 1.3)
   * @param minIntervalMs Minimum time between jumps (ms)
   */
  JumpDetector(SensorReading *sensor, SensorType type,
               float thresholdFactor = 1.3f, uint32_t minIntervalMs = 300);

  /**
   * Update detector - call this frequently (100Hz recommended)
   */
  void update();

  /**
   * Get jump counts for all axes and timing configurations
   * @param countsX Output array for X axis (size NUM_TIMING_CONFIGS)
   * @param countsY Output array for Y axis (size NUM_TIMING_CONFIGS)
   * @param countsZ Output array for Z axis (size NUM_TIMING_CONFIGS)
   */
  void getCounts(uint32_t countsX[NUM_TIMING_CONFIGS],
                 uint32_t countsY[NUM_TIMING_CONFIGS],
                 uint32_t countsZ[NUM_TIMING_CONFIGS]);

  /**
   * Get timing configuration parameters
   * @param configIndex Configuration index (0 to NUM_TIMING_CONFIGS-1)
   * @param riseDuration Output rise duration (ms)
   * @param fallDuration Output fall duration (ms)
   */
  void getTimingConfig(int configIndex, uint32_t &riseDuration,
                       uint32_t &fallDuration);

  /**
   * Get total jumps for each axis
   * @param totalX Output total for X axis
   * @param totalY Output total for Y axis
   * @param totalZ Output total for Z axis
   */
  void getTotalJumps(uint32_t &totalX, uint32_t &totalY,
                     uint32_t &totalZ) const;

  /**
   * Get average jump rate for each axis
   * @param rateX Output rate for X axis (jumps/min)
   * @param rateY Output rate for Y axis (jumps/min)
   * @param rateZ Output rate for Z axis (jumps/min)
   */
  void getAverageRates(float &rateX, float &rateY, float &rateZ) const;

  /**
   * Check if calibration is complete
   * @return true if calibrated
   */
  bool isCalibrated() const;

  /**
   * Get detector name
   * @return "GYRO" or "ACCEL"
   */
  const char *getName() const;

private:
  SensorReading *_sensor;
  SensorType _sensorType;
  char _sensorName[8]; // "GYRO" or "ACCEL"
  float _thresholdFactor;
  uint32_t _minIntervalMs;

  AxisDetector _axisX;
  AxisDetector _axisY;
  AxisDetector _axisZ;

  float _avgJump;             // Adaptive threshold baseline
  bool _calibrationComplete;  // Calibration status
  uint32_t _calibrationJumps; // Jumps during calibration

  /**
   * Get current time in milliseconds
   */
  uint32_t getMillis();

  /**
   * Update a single axis
   */
  void updateAxis(AxisDetector &axis, float value, uint32_t now);

  /**
   * Update a single configuration
   */
  void updateConfig(JumpConfig &config, float value, uint32_t now);

  /**
   * Get config index from pointer
   */
  int getConfigIndex(JumpConfig *config);

  /**
   * Calculate total jumps for an axis
   */
  uint32_t getAxisTotal(const AxisDetector &axis) const;

  /**
   * Calculate average rate for an axis
   */
  float getAxisRate(const AxisDetector &axis) const;
};

#endif // JUMP_H
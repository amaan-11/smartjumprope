#include "jump.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cmath>
#include <cstring>
#include <initializer_list>

static const char *TAG = "JUMP";

// ===== Timing + Filtering Controls =====
#define TIMING_TOLERANCE_MS 40
#define MAX_PHASE_DURATION_MS 800
#define FILTER_ALPHA_FAST 0.4f
#define FILTER_ALPHA_SLOW 0.15f
#define CALIBRATION_JUMPS 5

// Peak detection hysteresis
#define PEAK_DROP_THRESHOLD 0.85f // 15% drop confirms peak
#define MIN_PEAK_VALUE 30.0f      // Minimum value to consider

// Adaptive threshold bounds
#define MIN_THRESHOLD 40.0f
#define MAX_THRESHOLD 800.0f
#define INITIAL_THRESHOLD 100.0f

// Timing configurations: {rise, fall} - 4 configs
static const struct {
  uint32_t rise;
  uint32_t fall;
} TIMING_CONFIGS[NUM_TIMING_CONFIGS] = {
    {140, 140}, // Fast
    {160, 160}, // Medium
    {180, 180}, // Slow
    {200, 200}  // Very slow
};

JumpDetector::JumpDetector(SensorReading *sensor, SensorType type,
                           float thresholdFactor, uint32_t minIntervalMs)
    : _sensor(sensor), _sensorType(type), _thresholdFactor(thresholdFactor),
      _minIntervalMs(minIntervalMs), _avgJump(INITIAL_THRESHOLD),
      _calibrationComplete(false), _calibrationJumps(0) {

  // Set sensor name
  if (_sensorType == SENSOR_GYRO) {
    strcpy(_sensorName, "GYRO");
  } else {
    strcpy(_sensorName, "ACCEL");
  }

  // Initialize axis names
  strcpy(_axisX.name, "X");
  strcpy(_axisY.name, "Y");
  strcpy(_axisZ.name, "Z");

  // Initialize all timing configs for all axes
  for (AxisDetector *axis : {&_axisX, &_axisY, &_axisZ}) {
    for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
      axis->configs[i].minRiseDuration = TIMING_CONFIGS[i].rise;
      axis->configs[i].minFallDuration = TIMING_CONFIGS[i].fall;
      axis->configs[i].jumpCount = 0;
      axis->configs[i].lastJumpTime = 0;
      axis->configs[i].state = STATE_IDLE;
      axis->configs[i].peak = 0.0f;
      axis->configs[i].valley = 0.0f;
      axis->configs[i].risingStartTime = 0;
      axis->configs[i].fallingStartTime = 0;
      axis->configs[i].lastValue = 0.0f;
      axis->configs[i].filteredFast = 0.0f;
      axis->configs[i].filteredSlow = 0.0f;
    }
  }

  ESP_LOGI(TAG, "=== %s 3-Axis Detector Initialized ===", _sensorName);
  ESP_LOGI(TAG,
           "Testing %d timing configurations per axis:", NUM_TIMING_CONFIGS);
  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    ESP_LOGI(TAG, "  Config %d: Rise=%lums, Fall=%lums", i,
             TIMING_CONFIGS[i].rise, TIMING_CONFIGS[i].fall);
  }
  ESP_LOGI(TAG, "Total detectors: 3 axes * %d configs = %d", NUM_TIMING_CONFIGS,
           3 * NUM_TIMING_CONFIGS);
  ESP_LOGI(TAG, "Threshold factor: %.2f", thresholdFactor);
  ESP_LOGI(TAG, "Min interval: %lu ms", minIntervalMs);
  ESP_LOGI(TAG, "Calibration: %d jumps needed", CALIBRATION_JUMPS);
}

uint32_t JumpDetector::getMillis() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void JumpDetector::update() {
  int16_t ax, ay, az, gx, gy, gz;
  if (_sensor->readRaw(ax, ay, az, gx, gy, gz) != ESP_OK)
    return;

  uint32_t now = getMillis();

  // Update each axis based on sensor type
  if (_sensorType == SENSOR_GYRO) {
    updateAxis(_axisX, static_cast<float>(gx), now);
    updateAxis(_axisY, static_cast<float>(gy), now);
    updateAxis(_axisZ, static_cast<float>(gz), now);
  } else {
    updateAxis(_axisX, static_cast<float>(ax), now);
    updateAxis(_axisY, static_cast<float>(ay), now);
    updateAxis(_axisZ, static_cast<float>(az), now);
  }
}

void JumpDetector::updateAxis(AxisDetector &axis, float value, uint32_t now) {
  // Update all timing configurations for this axis
  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    updateConfig(axis.configs[i], value, now);
  }
}

void JumpDetector::updateConfig(JumpConfig &config, float value, uint32_t now) {
  // Two-stage filtering
  config.filteredFast = FILTER_ALPHA_FAST * value +
                        (1.0f - FILTER_ALPHA_FAST) * config.filteredFast;
  config.filteredSlow = FILTER_ALPHA_SLOW * value +
                        (1.0f - FILTER_ALPHA_SLOW) * config.filteredSlow;

  float filteredValue = config.filteredFast;

  // State machine for jump detection
  switch (config.state) {
  case STATE_IDLE:
    // Look for start of rise
    if (filteredValue > config.lastValue &&
        fabs(filteredValue) > MIN_PEAK_VALUE) {
      config.state = STATE_RISING;
      config.risingStartTime = now;
      config.peak = filteredValue;
    }
    break;

  case STATE_RISING:
    // Update peak
    if (filteredValue > config.peak) {
      config.peak = filteredValue;
    }

    // Check for peak confirmation (significant drop)
    if (filteredValue < config.peak * PEAK_DROP_THRESHOLD) {
      uint32_t riseDuration = now - config.risingStartTime;

      // Validate rise timing
      if (riseDuration >= config.minRiseDuration - TIMING_TOLERANCE_MS &&
          riseDuration <= config.minRiseDuration + TIMING_TOLERANCE_MS) {
        config.state = STATE_FALLING;
        config.fallingStartTime = now;
        config.valley = filteredValue;
      } else {
        // Invalid timing, reset
        config.state = STATE_IDLE;
      }
    }

    // Timeout guard
    if (now - config.risingStartTime > MAX_PHASE_DURATION_MS) {
      config.state = STATE_IDLE;
    }
    break;

  case STATE_FALLING:
    // Update valley
    if (filteredValue < config.valley) {
      config.valley = filteredValue;
    }

    uint32_t fallDuration = now - config.fallingStartTime;

    // Check fall timing and complete jump
    if (fallDuration >= config.minFallDuration - TIMING_TOLERANCE_MS &&
        fallDuration <= config.minFallDuration + TIMING_TOLERANCE_MS) {

      float diff = fabs(config.peak - config.valley);

      // Use calibration mode threshold or normal threshold
      float currentThreshold = _avgJump * _thresholdFactor;
      if (!_calibrationComplete) {
        currentThreshold *= 0.7f; // More lenient during calibration
      }

      // Validate jump and enforce minimum interval
      if (diff > currentThreshold &&
          now - config.lastJumpTime > _minIntervalMs) {

        // Register jump
        config.jumpCount++;
        config.lastJumpTime = now;

        // Update adaptive threshold with bounds
        _avgJump = 0.92f * _avgJump + 0.08f * diff;
        _avgJump = fmaxf(MIN_THRESHOLD, fminf(MAX_THRESHOLD, _avgJump));

        // Track calibration
        if (!_calibrationComplete) {
          _calibrationJumps++;
          if (_calibrationJumps >= CALIBRATION_JUMPS) {
            _calibrationComplete = true;
            ESP_LOGI(TAG, "%s calibration complete! Threshold: %.2f",
                     _sensorName, _avgJump);
          }
        }
      }

      config.state = STATE_IDLE;
    }

    // Timeout guard
    if (fallDuration > MAX_PHASE_DURATION_MS) {
      config.state = STATE_IDLE;
    }
    break;
  }

  config.lastValue = filteredValue;
}

int JumpDetector::getConfigIndex(JumpConfig *config) {
  for (AxisDetector *axis : {&_axisX, &_axisY, &_axisZ}) {
    for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
      if (&axis->configs[i] == config) {
        return i;
      }
    }
  }
  return -1;
}

void JumpDetector::getCounts(uint32_t countsX[NUM_TIMING_CONFIGS],
                             uint32_t countsY[NUM_TIMING_CONFIGS],
                             uint32_t countsZ[NUM_TIMING_CONFIGS]) {
  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    countsX[i] = _axisX.configs[i].jumpCount;
    countsY[i] = _axisY.configs[i].jumpCount;
    countsZ[i] = _axisZ.configs[i].jumpCount;
  }
}

void JumpDetector::getTimingConfig(int configIndex, uint32_t &riseDuration,
                                   uint32_t &fallDuration) {
  if (configIndex >= 0 && configIndex < NUM_TIMING_CONFIGS) {
    riseDuration = TIMING_CONFIGS[configIndex].rise;
    fallDuration = TIMING_CONFIGS[configIndex].fall;
  }
}

uint32_t JumpDetector::getAxisTotal(const AxisDetector &axis) const {
  uint32_t total = 0;
  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    total += axis.configs[i].jumpCount;
  }
  return total;
}

float JumpDetector::getAxisRate(const AxisDetector &axis) const {
  uint32_t maxJumps = 0;
  uint32_t lastJumpTime = 0;

  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    if (axis.configs[i].jumpCount > maxJumps) {
      maxJumps = axis.configs[i].jumpCount;
      lastJumpTime = axis.configs[i].lastJumpTime;
    }
  }

  if (maxJumps < 2 || lastJumpTime == 0)
    return 0.0f;

  // Return jumps per minute
  float timeMinutes = lastJumpTime / 60000.0f;
  return maxJumps / timeMinutes;
}

void JumpDetector::getTotalJumps(uint32_t &totalX, uint32_t &totalY,
                                 uint32_t &totalZ) const {
  totalX = getAxisTotal(_axisX);
  totalY = getAxisTotal(_axisY);
  totalZ = getAxisTotal(_axisZ);
}

void JumpDetector::getAverageRates(float &rateX, float &rateY,
                                   float &rateZ) const {
  rateX = getAxisRate(_axisX);
  rateY = getAxisRate(_axisY);
  rateZ = getAxisRate(_axisZ);
}

bool JumpDetector::isCalibrated() const { return _calibrationComplete; }

const char *JumpDetector::getName() const { return _sensorName; }
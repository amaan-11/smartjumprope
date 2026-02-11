#include "jump.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <initializer_list>

static const char *TAG = "JUMP";

// Define the timing configurations to test
// Format: {minRiseDuration, minFallDuration}
static const struct {
  uint32_t rise;
  uint32_t fall;
} TIMING_CONFIGS[NUM_TIMING_CONFIGS] = {
    {80, 110},   // Config 0: Very fast (50ms)
    {110, 130},   // Config 1: Fast (75ms)
    {135, 165}, // Config 2: Medium (100ms)
    {170, 200}, // Config 3: Slow (150ms)
    {205, 250}  // Config 4: Very slow (200ms)
};

JumpDetector::JumpDetector(SensorReading *gyro, float thresholdFactor,
                           uint32_t minIntervalMs)
    : _gyro(gyro), _thresholdFactor(thresholdFactor),
      _minIntervalMs(minIntervalMs), _avgJump(20.0f) {

  // Initialize axis names
  strcpy(_axisX.name, "X");
  strcpy(_axisY.name, "Y");
  strcpy(_axisZ.name, "Z");

  // Initialize all configs for all axes
  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    for (AxisDetector *axis : {&_axisX, &_axisY, &_axisZ}) {
      axis->configs[i].minRiseDuration = TIMING_CONFIGS[i].rise;
      axis->configs[i].minFallDuration = TIMING_CONFIGS[i].fall;
      axis->configs[i].jumpCount = 0;
      axis->configs[i].lastJumpTime = 0;
      axis->configs[i].peak = -1e6f;
      axis->configs[i].valley = 1e6f;
      axis->configs[i].isRising = false;
      axis->configs[i].peakConfirmed = false;
      axis->configs[i].risingStartTime = 0;
      axis->configs[i].fallingStartTime = 0;
      axis->configs[i].lastValue = 0.0f;
    }
  }

  ESP_LOGI(TAG, "Initialized multi-axis detector");
  ESP_LOGI(TAG, "Testing %d timing configurations:", NUM_TIMING_CONFIGS);
  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    ESP_LOGI(TAG, "  Config %d: Rise=%lums, Fall=%lums", i,
             TIMING_CONFIGS[i].rise, TIMING_CONFIGS[i].fall);
  }
  ESP_LOGI(TAG, "Threshold factor: %.2f", thresholdFactor);
  ESP_LOGI(TAG, "Min interval: %lu ms", minIntervalMs);
}

uint32_t JumpDetector::getMillis() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void JumpDetector::update() {
  int16_t ax, ay, az, gx, gy, gz;
  if (_gyro->readRaw(ax, ay, az, gx, gy, gz) != ESP_OK) {
    return;
  }

  uint32_t now = getMillis();

  // Update all three axes
  updateAxis(_axisX, static_cast<float>(gx), now);
  updateAxis(_axisY, static_cast<float>(gy), now);
  updateAxis(_axisZ, static_cast<float>(gz), now);
}

void JumpDetector::updateAxis(AxisDetector &axis, float gyroValue,
                              uint32_t now) {
  // Test all timing configurations for this axis
  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    updateConfig(axis.configs[i], gyroValue, now);
  }
}

void JumpDetector::updateConfig(JumpConfig &config, float gyroValue,
                                uint32_t now) {
  // Detect rising edge START
  if (gyroValue > config.lastValue && !config.isRising) {
    config.isRising = true;
    config.risingStartTime = now;
    config.peakConfirmed = false;
  }

  // If rising, update peak
  if (config.isRising && gyroValue > config.lastValue) {
    if (gyroValue > config.peak || !config.peakConfirmed) {
      config.peak = gyroValue;
    }
  }

  // Detect peak (transition from rising to falling)
  if (gyroValue < config.lastValue && config.isRising) {
    uint32_t riseDuration = now - config.risingStartTime;
    if (riseDuration >= config.minRiseDuration) {
      config.peak = config.lastValue;
      config.isRising = false;
      config.fallingStartTime = now;
      config.peakConfirmed = true;
    } else {
      // Haven't risen long enough - ignore this peak
      config.isRising = false;
      config.peakConfirmed = false;
    }
  }

  // If falling, check for jump
  if (!config.isRising && gyroValue < config.lastValue &&
      config.peakConfirmed) {
    if (gyroValue < config.valley) {
      config.valley = gyroValue;
    }

    uint32_t fallDuration = now - config.fallingStartTime;
    if (fallDuration >= config.minFallDuration) {
      float diff = config.peak - config.valley;

      if (diff > _thresholdFactor * _avgJump &&
          now - config.lastJumpTime > _minIntervalMs) {

        config.lastJumpTime = now;
        config.jumpCount++;

        // Update average (shared across all configs)
        _avgJump = 0.9f * _avgJump + 0.1f * diff;

        // Reset for next jump
        config.peak = -1e6f;
        config.valley = 1e6f;
        config.peakConfirmed = false;
      }
    }
  }

  config.lastValue = gyroValue;
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
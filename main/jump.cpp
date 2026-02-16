#include "jump.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <cstring>
#include <initializer_list>

static const char *TAG = "JUMP";

// ===== Timing + Filtering Controls =====
#define TIMING_TOLERANCE_MS 35
#define MAX_PHASE_DURATION_MS 600
#define FILTER_ALPHA 0.25f

// Timing configurations: {rise, fall}
static const struct {
  uint32_t rise;
  uint32_t fall;
} TIMING_CONFIGS[NUM_TIMING_CONFIGS] = {
    {140, 140},  // Very fast
    {150, 150}, // Fast
    {160, 160}, // Medium
    {170, 170}, // Slow
    {180, 180}  // Very slow
};

JumpDetector::JumpDetector(SensorReading *gyro, float thresholdFactor,
                           uint32_t minIntervalMs)
    : _gyro(gyro), _thresholdFactor(thresholdFactor),
      _minIntervalMs(minIntervalMs), _avgJump(20.0f) {

  // Initialize axis names
  strcpy(_axisX.name, "X");
  strcpy(_axisY.name, "Y");
  strcpy(_axisZ.name, "Z");

  // Initialize configs
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
      axis->configs[i].filteredValue = 0.0f; // initialize filter
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
  if (_gyro->readRaw(ax, ay, az, gx, gy, gz) != ESP_OK)
    return;

  uint32_t now = getMillis();
  updateAxis(_axisX, static_cast<float>(gx), now);
  updateAxis(_axisY, static_cast<float>(gy), now);
  updateAxis(_axisZ, static_cast<float>(gz), now);
}

void JumpDetector::updateAxis(AxisDetector &axis, float gyroValue,
                              uint32_t now) {
  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    updateConfig(axis.configs[i], gyroValue, now);
  }
}

void JumpDetector::updateConfig(JumpConfig &config, float gyroValue,
                                uint32_t now) {
  // Low-pass filter
  config.filteredValue =
      FILTER_ALPHA * gyroValue + (1.0f - FILTER_ALPHA) * config.filteredValue;
  float value = config.filteredValue;

  // Start rising
  if (value > config.lastValue && !config.isRising) {
    config.isRising = true;
    config.risingStartTime = now;
    config.peakConfirmed = false;
    config.peak = value;
  }

  // Update peak
  if (config.isRising && value > config.peak)
    config.peak = value;

  // End rising → start fall
  if (config.isRising && value < config.lastValue) {
    uint32_t riseDuration = now - config.risingStartTime;
    if (riseDuration >= config.minRiseDuration - TIMING_TOLERANCE_MS &&
        riseDuration <= config.minRiseDuration + TIMING_TOLERANCE_MS) {
      config.isRising = false;
      config.fallingStartTime = now;
      config.peakConfirmed = true;
      config.valley = value;
    } else if (riseDuration > MAX_PHASE_DURATION_MS) {
      config.isRising = false;
      config.peakConfirmed = false;
    }
  }

  // Falling phase
  if (!config.isRising && config.peakConfirmed) {
    if (value < config.valley)
      config.valley = value;

    uint32_t fallDuration = now - config.fallingStartTime;
    if (fallDuration >= config.minFallDuration - TIMING_TOLERANCE_MS &&
        fallDuration <= config.minFallDuration + TIMING_TOLERANCE_MS) {

      float diff = config.peak - config.valley;
      if (diff > _thresholdFactor * _avgJump &&
          now - config.lastJumpTime > _minIntervalMs) {

        config.jumpCount++;
        config.lastJumpTime = now;
        _avgJump = 0.95f * _avgJump + 0.05f * diff;
        ESP_LOGI(TAG, "Jump detected! Peak=%.2f, Valley=%.2f, Diff=%.2f",
                 config.peak, config.valley, diff);
      }

      // Reset after evaluation
      config.peakConfirmed = false;
      config.isRising = false;
    } else if (fallDuration > MAX_PHASE_DURATION_MS) {
      config.peakConfirmed = false;
      config.isRising = false;
    }
  }

  config.lastValue = value;
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

// Optional: log peaks/valleys for debugging
void JumpDetector::logAxisStats() {
  for (AxisDetector *axis : {&_axisX, &_axisY, &_axisZ}) {
    for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
      ESP_LOGI(TAG, "%s Config %d: Peak=%.2f, Valley=%.2f, Count=%lu",
               axis->name, i, axis->configs[i].peak, axis->configs[i].valley,
               axis->configs[i].jumpCount);
    }
  }
}

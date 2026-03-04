#include "jump.h"

#include "esp_log.h"
#include "esp_timer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

static const char *TAG = "JUMP";

// ===== Timing + Filtering Controls (from dae9b47) =====
#define TIMING_TOLERANCE_MS 40
#define MAX_PHASE_DURATION_MS 800
#define FILTER_ALPHA_FAST 0.4f
#define FILTER_ALPHA_SLOW 0.15f
#define CALIBRATION_JUMPS 5

// Peak detection hysteresis
#define PEAK_DROP_THRESHOLD 0.85f // 15% drop confirms peak
#define MIN_PEAK_VALUE 30.0f      // Minimum magnitude to consider

// Adaptive threshold bounds
#define MIN_THRESHOLD 40.0f
#define MAX_THRESHOLD 800.0f
#define INITIAL_THRESHOLD 100.0f

// Timing configurations: {rise, fall}
static const struct {
  uint32_t rise;
  uint32_t fall;
} TIMING_CONFIGS[NUM_TIMING_CONFIGS] = {
    {140, 140}, // Fast
    {160, 160}, // Medium
    {180, 180}, // Slow
    {200, 200}  // Very slow
};

JumpDetector::JumpDetector(SensorReading *gyro, float thresholdFactor,
                           uint32_t minIntervalMs)
    : _gyro(gyro),
      _thresholdFactor(thresholdFactor),
      _minIntervalMs(minIntervalMs),
      _avgJump(INITIAL_THRESHOLD),
      _calibrationComplete(false),
      _calibrationJumps(0),
      _jumpQueue(nullptr),
      _lastBestTotal(0) {

  _jumpQueue = xQueueCreate(32, sizeof(uint32_t)); // room for bursts
  configASSERT(_jumpQueue != nullptr);

  initAxis(_axisX);
  initAxis(_axisY);
  initAxis(_axisZ);

  ESP_LOGI(TAG, "JumpDetector init (GYRO X/Y/Z, %d configs)", NUM_TIMING_CONFIGS);
  ESP_LOGI(TAG, "Threshold factor: %.2f, Min interval: %lu ms",
           (double)_thresholdFactor, (unsigned long)_minIntervalMs);

  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    ESP_LOGI(TAG, "Config %d: Rise=%lums Fall=%lums",
             i,
             (unsigned long)TIMING_CONFIGS[i].rise,
             (unsigned long)TIMING_CONFIGS[i].fall);
  }
  ESP_LOGI(TAG, "Calibration jumps needed: %d", CALIBRATION_JUMPS);
}

uint32_t JumpDetector::getMillis() const {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

void JumpDetector::initAxis(AxisDetector &axis) {
  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    JumpConfig &c = axis.configs[i];
    c.minRiseDuration = TIMING_CONFIGS[i].rise;
    c.minFallDuration = TIMING_CONFIGS[i].fall;

    c.jumpCount = 0;
    c.lastJumpTime = 0;

    c.state = STATE_IDLE;

    c.peak = 0.0f;
    c.valley = 0.0f;

    c.risingStartTime = 0;
    c.fallingStartTime = 0;

    c.lastValue = 0.0f;
    c.filteredFast = 0.0f;
    c.filteredSlow = 0.0f;
  }
}

void JumpDetector::update() {
  if (!_gyro) return;

  int16_t ax, ay, az, gx, gy, gz;
  if (_gyro->readRaw(ax, ay, az, gx, gy, gz) != ESP_OK) {
    return;
  }

  const uint32_t now = getMillis();

  // Use gyro axes X/Y/Z
  updateAxis(_axisX, static_cast<float>(gx), now);
  updateAxis(_axisY, static_cast<float>(gy), now);
  updateAxis(_axisZ, static_cast<float>(gz), now);

  // Convert "best total count" into event stream:
  // Emit one queue event per newly detected jump.
  const uint32_t bestNow = computeBestTotal();
  if (bestNow > _lastBestTotal) {
    const uint32_t delta = bestNow - _lastBestTotal;
    for (uint32_t i = 0; i < delta; i++) {
      uint32_t ts = now;
      xQueueSendToBack(_jumpQueue, &ts, 0);
    }
    _lastBestTotal = bestNow;
  }
}

void JumpDetector::updateAxis(AxisDetector &axis, float value, uint32_t now) {
  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    updateConfig(axis.configs[i], value, now);
  }
}

void JumpDetector::updateConfig(JumpConfig &config, float value, uint32_t now) {
  // Two-stage filtering (dae9b47 style)
  config.filteredFast = FILTER_ALPHA_FAST * value +
                        (1.0f - FILTER_ALPHA_FAST) * config.filteredFast;
  config.filteredSlow = FILTER_ALPHA_SLOW * value +
                        (1.0f - FILTER_ALPHA_SLOW) * config.filteredSlow;

  const float filteredValue = config.filteredFast;

  switch (config.state) {
  case STATE_IDLE:
    // Look for start of rise
    if (filteredValue > config.lastValue &&
        std::fabs(filteredValue) > MIN_PEAK_VALUE) {
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

    // Confirm peak when value drops enough
    if (filteredValue < config.peak * PEAK_DROP_THRESHOLD) {
      const uint32_t riseDuration = now - config.risingStartTime;

      if (riseDuration >= (config.minRiseDuration - TIMING_TOLERANCE_MS) &&
          riseDuration <= (config.minRiseDuration + TIMING_TOLERANCE_MS)) {
        config.state = STATE_FALLING;
        config.fallingStartTime = now;
        config.valley = filteredValue;
      } else {
        config.state = STATE_IDLE;
      }
    }

    // Timeout guard
    if ((now - config.risingStartTime) > MAX_PHASE_DURATION_MS) {
      config.state = STATE_IDLE;
    }
    break;

  case STATE_FALLING: {
    // Update valley
    if (filteredValue < config.valley) {
      config.valley = filteredValue;
    }

    const uint32_t fallDuration = now - config.fallingStartTime;

    // Complete jump when fall timing matches
    if (fallDuration >= (config.minFallDuration - TIMING_TOLERANCE_MS) &&
        fallDuration <= (config.minFallDuration + TIMING_TOLERANCE_MS)) {

      const float diff = std::fabs(config.peak - config.valley);

      float currentThreshold = _avgJump * _thresholdFactor;
      if (!_calibrationComplete) {
        currentThreshold *= 0.7f; // lenient during calibration
      }

      if (diff > currentThreshold &&
          (now - config.lastJumpTime) > _minIntervalMs) {

        config.jumpCount++;
        config.lastJumpTime = now;

        // Update adaptive threshold with bounds
        _avgJump = 0.92f * _avgJump + 0.08f * diff;
        _avgJump = std::max((float)MIN_THRESHOLD, std::min((float)MAX_THRESHOLD, _avgJump));

        // Calibration tracking
        if (!_calibrationComplete) {
          _calibrationJumps++;
          if (_calibrationJumps >= CALIBRATION_JUMPS) {
            _calibrationComplete = true;
            ESP_LOGI(TAG, "Calibration complete! avgJump=%.2f", (double)_avgJump);
          }
        }
      }

      config.state = STATE_IDLE;
    }

    // Timeout guard
    if (fallDuration > MAX_PHASE_DURATION_MS) {
      config.state = STATE_IDLE;
    }
  } break;
  }

  config.lastValue = filteredValue;
}

uint32_t JumpDetector::maxConfig(const uint32_t counts[NUM_TIMING_CONFIGS]) {
  uint32_t m = 0;
  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    m = (counts[i] > m) ? counts[i] : m;
  }
  return m;
}

uint32_t JumpDetector::maxConfigFromAxis(const AxisDetector &axis) {
  uint32_t m = 0;
  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    const uint32_t c = axis.configs[i].jumpCount;
    m = (c > m) ? c : m;
  }
  return m;
}

// Best total = max across axes, where each axis = max across configs.
// This avoids overcounting the same physical jump across multiple timing configs.
uint32_t JumpDetector::computeBestTotal() const {
  const uint32_t bx = maxConfigFromAxis(_axisX);
  const uint32_t by = maxConfigFromAxis(_axisY);
  const uint32_t bz = maxConfigFromAxis(_axisZ);
  return std::max(bx, std::max(by, bz));
}

bool JumpDetector::getJump(uint32_t &timestampMs, TickType_t waitTicks) {
  return xQueueReceive(_jumpQueue, &timestampMs, waitTicks) == pdTRUE;
}

void JumpDetector::getCounts(uint32_t countsX[NUM_TIMING_CONFIGS],
                             uint32_t countsY[NUM_TIMING_CONFIGS],
                             uint32_t countsZ[NUM_TIMING_CONFIGS]) const {
  for (int i = 0; i < NUM_TIMING_CONFIGS; i++) {
    countsX[i] = _axisX.configs[i].jumpCount;
    countsY[i] = _axisY.configs[i].jumpCount;
    countsZ[i] = _axisZ.configs[i].jumpCount;
  }
}
#pragma once

#include "driver/gpio.h"
#include "esp_timer.h"

// GPIO type: input or output
enum class GPIOMode
{
  INPUT,
  OUTPUT
};

// Pull configuration
enum class GPIOPull
{
  NONE,
  PULLUP,
  PULLDOWN
};

class GPIOPin
{
public:
  explicit GPIOPin(gpio_num_t pin,
                   GPIOMode mode = GPIOMode::INPUT,
                   GPIOPull pull = GPIOPull::PULLUP,
                   bool invert = false,
                   uint32_t debounce_ms = 50);

  // Basic I/O
  bool read() const;
  void write(bool value) const;
  int getPin() const;

  // Button logic
  void update();  // Call regularly (every 1–5 ms)
  bool pressed(); // Fires once per press
  bool held();    // Fires once per hold
  void setHoldTime(uint32_t ms);
  void setDebounceTime(uint32_t ms);

private:
  gpio_num_t pin_number;
  GPIOMode mode;
  GPIOPull pull;
  bool is_inverted;

  // Button state
  bool lastReading;
  bool stableState;
  bool pressEvent;
  bool holdEvent;

  int64_t lastChangeTime; // µs
  int64_t pressStartTime; // µs
  uint32_t debounce_ms;
  uint32_t hold_ms;
};

#include "gpio_pin.h"
#include <cstdio>

GPIOPin::GPIOPin(gpio_num_t pin, GPIOMode mode, GPIOPull pull,
                 bool invert, uint32_t debounce_ms)
    : pin_number(pin),
      mode(mode),
      pull(pull),
      is_inverted(invert),
      lastReading(false),
      stableState(false),
      pressEvent(false),
      holdEvent(false),
      debounce_ms(debounce_ms),
      hold_ms(1000) // default 1 second hold
{
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = (1ULL << pin);
  cfg.intr_type = GPIO_INTR_DISABLE;

  if (mode == GPIOMode::OUTPUT)
  {
    cfg.mode = GPIO_MODE_OUTPUT;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
  }
  else
  { // INPUT
    cfg.mode = GPIO_MODE_INPUT;

    cfg.pull_up_en =
        (pull == GPIOPull::PULLUP ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE);

    cfg.pull_down_en =
        (pull == GPIOPull::PULLDOWN ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE);
  }

  gpio_config(&cfg);

  lastChangeTime = esp_timer_get_time();
  pressStartTime = esp_timer_get_time();
}


bool GPIOPin::read() const
{
  if (mode != GPIOMode::INPUT)
  {
    printf("[GPIO WARNING] Tried to read OUTPUT pin %d\n", pin_number);
    return false;
  }

  bool val = gpio_get_level(pin_number);
  return is_inverted ? !val : val;
}

void GPIOPin::write(bool value) const
{
  if (mode != GPIOMode::OUTPUT)
  {
    printf("[GPIO WARNING] Tried to write INPUT pin %d\n", pin_number);
    return;
  }

  gpio_set_level(pin_number, is_inverted ? !value : value);
}

int GPIOPin::getPin() const
{
  return pin_number;
}

void GPIOPin::update()
{
  if (mode != GPIOMode::INPUT)
    return;

  // Assume active-low button like your Pico code
  bool reading = !gpio_get_level(pin_number);

  int64_t now = esp_timer_get_time();

  // Debounce
  if (reading != lastReading)
    lastChangeTime = now;

  if ((now - lastChangeTime) > (int64_t)debounce_ms * 1000)
  {
    if (reading != stableState)
    {
      stableState = reading;

      if (stableState)
      {
        // button pressed
        pressEvent = true;
        pressStartTime = now;
        holdEvent = false;
      }
      else
      {
        // button released
        holdEvent = false;
      }
    }
  }

  // Hold detection
  if (stableState && !holdEvent &&
      (now - pressStartTime) > (int64_t)hold_ms * 1000)
  {
    holdEvent = true;
  }

  lastReading = reading;
}

bool GPIOPin::pressed()
{
  if (pressEvent)
  {
    pressEvent = false;
    return true;
  }
  return false;
}

bool GPIOPin::held()
{
  if (holdEvent)
  {
    holdEvent = false;
    return true;
  }
  return false;
}

void GPIOPin::setHoldTime(uint32_t ms)
{
  hold_ms = ms;
}

void GPIOPin::setDebounceTime(uint32_t ms)
{
  debounce_ms = ms;
}

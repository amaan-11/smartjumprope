#pragma once

#include "esp_assert.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

class MutexGuard {
public:
  explicit MutexGuard(SemaphoreHandle_t m) : mutex(m), locked(false) {
    configASSERT(mutex != nullptr && "Mutex handle is null");

    BaseType_t taken = xSemaphoreTake(mutex, portMAX_DELAY);
    configASSERT(taken == pdTRUE && "Failed to acquire mutex");

    locked = true;
  }

  ~MutexGuard() {
    if (locked) {
      xSemaphoreGive(mutex);
    }
  }

  MutexGuard(const MutexGuard &) = delete;
  MutexGuard &operator=(const MutexGuard &) = delete;

  MutexGuard(MutexGuard &&other) noexcept
      : mutex(other.mutex), locked(other.locked) {
    other.locked = false;
  }

  MutexGuard &operator=(MutexGuard &&other) noexcept {
    if (this != &other) {
      if (locked) {
        xSemaphoreGive(mutex);
      }
      mutex = other.mutex;
      locked = other.locked;
      other.locked = false;
    }
    return *this;
  }

private:
  SemaphoreHandle_t mutex;
  bool locked;
};

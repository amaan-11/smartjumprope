#pragma once
/*
 * components/ble/jr_ble.h
 */
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

  // ===== Packet v1 (12 bytes) =====
  typedef struct __attribute__((packed))
  {
    uint32_t timestamp_ms;
    uint32_t jump_count;
    uint8_t heart_rate_bpm;
    uint16_t accel_mag;
    uint8_t flags;
  } jr_packet_v1_t;

#ifdef __cplusplus
  static_assert(sizeof(jr_packet_v1_t) == 12, "jr_packet_v1_t must be 12 bytes");
#else
_Static_assert(sizeof(jr_packet_v1_t) == 12, "jr_packet_v1_t must be 12 bytes");
#endif

  // ===== HR task control callback =====
  // Register a function matching this signature via jr_ble_set_hr_cmd_callback().
  // It will be called from the BLE control characteristic write handler with:
  //   cmd = 0x02 => start HR task
  //   cmd = 0x03 => stop HR task
  typedef void (*jr_hr_cmd_cb_t)(uint8_t cmd);

  // ===== Public API =====
  void jr_ble_init(void);

  void jr_ble_set_sensor_snapshot(uint32_t jump_count_total,
                                  uint8_t heart_rate_bpm,
                                  uint16_t accel_mag,
                                  uint8_t flags);

  void jr_ble_set_reset_on_start(bool enable);

  // Register the callback that main.cpp uses to start/stop the HR task.
  // Call this once during init, before jr_ble_init().
  void jr_ble_set_hr_cmd_callback(jr_hr_cmd_cb_t cb);

  bool jr_ble_is_streaming(void);
  bool jr_ble_is_connected(void);

#ifdef __cplusplus
}
#endif
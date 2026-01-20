#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

    void jr_ble_init(void);

    // Update values that get sent in notifications (optional for first test)
    void jr_ble_set_values(uint32_t jump_count, uint8_t heart_rate_bpm, uint16_t accel_mag);

    // True after browser writes 0x01 to control characteristic
    bool jr_ble_is_streaming(void);

#ifdef __cplusplus
}
#endif

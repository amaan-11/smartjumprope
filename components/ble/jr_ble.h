#pragma once
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

void jr_ble_init(void);

// Update the values that will be sent in notifications
void jr_ble_set_values(uint8_t heart_rate_bpm, uint8_t spo2_percent);

// True when browser enabled streaming
bool jr_ble_is_streaming(void);

#ifdef __cplusplus
}
#endif

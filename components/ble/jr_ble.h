#pragma once
/*
 * components/ble/jr_ble.h
 *
 * Smart Jump Rope BLE (NimBLE) layer.
 * - Exposes a Nordic UART style service with two characteristics:
 *   - Control (write): 0x01 starts streaming, 0x00 stops streaming
 *   - Data (notify/read): sends 12-byte binary packets (jr_packet_v1_t)
 *
 * IMPORTANT (contract with frontend):
 * Packet layout (12 bytes, little-endian):
 *   u32 timestamp_ms  (offset 0)
 *   u32 jump_count    (offset 4)
 *   u8  heart_rate    (offset 8)
 *   u16 accel_mag     (offset 9)
 *   u8  flags         (offset 11)
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ===== Packet v1 (12 bytes) =====
typedef struct __attribute__((packed)) {
    uint32_t timestamp_ms;     // ms since boot (or another firmware-defined base)
    uint32_t jump_count;       // count that is transmitted (can be relative to START)
    uint8_t  heart_rate_bpm;   // 0 => invalid / no reading
    uint16_t accel_mag;        // magnitude (firmware-defined units)
    uint8_t  flags;            // status/validity bits (firmware-defined)
} jr_packet_v1_t;

#ifdef __cplusplus
static_assert(sizeof(jr_packet_v1_t) == 12, "jr_packet_v1_t must be 12 bytes");
#else
_Static_assert(sizeof(jr_packet_v1_t) == 12, "jr_packet_v1_t must be 12 bytes");
#endif

// ===== Public API =====

// Initializes NimBLE, registers services/characteristics, and starts advertising.
void jr_ble_init(void);

// Updates the latest sensor snapshot (thread-safe via a simple critical section).
// - jump_count_total: total count since boot (recommended) OR already relative to workout.
// - heart_rate_bpm: 0 when invalid
// - accel_mag: pre-reduced to uint16_t
// - flags: free bits; recommended: bit0=HR valid, bit1=accel valid
void jr_ble_set_sensor_snapshot(uint32_t jump_count_total,
                                uint8_t heart_rate_bpm,
                                uint16_t accel_mag,
                                uint8_t flags);

// Enables/disables "reset-on-start" for the TRANSMITTED jump counter.
// Note: this does NOT reset sensors; it only resets what is transmitted (via baseline).
void jr_ble_set_reset_on_start(bool enable);

// True when the browser enabled streaming (control=0x01)
bool jr_ble_is_streaming(void);

// True when a BLE client is connected (useful for debug).
bool jr_ble_is_connected(void);

#ifdef __cplusplus
}
#endif
/** \file max30102_settings.h
 * Description: This module allows easy configuration of MAX30102 settings.
 * All the settings are based on the MAX30102 datasheet:
 * https://www.analog.com/media/en/technical-documentation/data-sheets/max30102.pdf
 */

#ifndef MAX30102_SETTINGS_H
#define MAX30102_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>

// ===================== Control Enumerations =====================

constexpr uint8_t bitToMask(uint8_t bitNumber, int value = 1) {
    return static_cast<uint8_t>(value << bitNumber);
}

enum SampleAveraging : uint8_t {
    NO_AVERAGING = bitToMask(7,0) | bitToMask(6,0) | bitToMask(5,0),
    AVG_2        = bitToMask(7,0) | bitToMask(6,0) | bitToMask(5,1),
    AVG_4        = bitToMask(7,0) | bitToMask(6,1) | bitToMask(5,0),
    AVG_8        = bitToMask(7,0) | bitToMask(6,1) | bitToMask(5,1),
    AVG_16       = bitToMask(7,1) | bitToMask(6,0) | bitToMask(5,0),
    AVG_32       = bitToMask(7,1) | bitToMask(6,0) | bitToMask(5,1)
};

enum ModeControl : uint8_t {
    HEART_RATE = bitToMask(2,0) | bitToMask(1,1) | bitToMask(0,0),
    SPO2       = bitToMask(2,0) | bitToMask(1,1) | bitToMask(0,1),
    MULTI_LED  = bitToMask(2,1) | bitToMask(1,1) | bitToMask(0,1),
};

enum SPO2_ADC_Range : uint8_t {
    ADC_RANGE_2048  = bitToMask(6,0) | bitToMask(5,0),
    ADC_RANGE_4096  = bitToMask(6,0) | bitToMask(5,1),
    ADC_RANGE_8192  = bitToMask(6,1) | bitToMask(5,0),
    ADC_RANGE_16384 = bitToMask(6,1) | bitToMask(5,1)
};

enum SPO2_SampleRate : uint8_t {
    SPO2_RATE_50   = bitToMask(4,0) | bitToMask(3,0) | bitToMask(2,0),
    SPO2_RATE_100  = bitToMask(4,0) | bitToMask(3,0) | bitToMask(2,1),
    SPO2_RATE_200  = bitToMask(4,0) | bitToMask(3,1) | bitToMask(2,0),
    SPO2_RATE_400  = bitToMask(4,0) | bitToMask(3,1) | bitToMask(2,1),
    SPO2_RATE_800  = bitToMask(4,1) | bitToMask(3,0) | bitToMask(2,0),
    SPO2_RATE_1000 = bitToMask(4,1) | bitToMask(3,0) | bitToMask(2,1),
    SPO2_RATE_1600 = bitToMask(4,1) | bitToMask(3,1) | bitToMask(2,0),
    SPO2_RATE_3200 = bitToMask(4,1) | bitToMask(3,1) | bitToMask(2,1)
};

enum SPO2_PulseWidth : uint8_t {
    PW_69  = bitToMask(1,0) | bitToMask(0,0),
    PW_118 = bitToMask(1,0) | bitToMask(0,1),
    PW_215 = bitToMask(1,1) | bitToMask(0,0),
    PW_411 = bitToMask(1,1) | bitToMask(0,1)
};

// ===================== Interrupts Status (0x00-0x01) =====================

bool interruptAFull(bool enable);
bool interruptPPGReady(bool enable);
bool interruptALCOverflow(bool enable);
bool interruptDIETempReady(bool enable);

// ===================== FIFO (0x04-0x07) =====================

bool setFifoWritePointer(uint8_t pointer);
bool setFifoOverflowCounter(uint8_t counter);
bool setFifoReadPointer(uint8_t pointer);
bool setFifoDataRegister(uint8_t data);

// ===================== FIFO Configuration (0x08) =====================

bool setSampleAveraging(SampleAveraging sampleAveraging);
bool setFifoRollOverOnFull(bool enable);
bool setFifoAlmostFullThreshold(uint8_t threshold);

// ===================== Mode Configuration (0x09) =====================

bool setShutdownCtrl(bool enable);
bool setResetCtrl(bool enable);
bool setModeControl(ModeControl modeControl);

// ===================== SPO2 Configuration (0x0A) =====================

bool setSPO2ADCRange(SPO2_ADC_Range adcRange);
bool setSPO2SampleRate(SPO2_SampleRate sampleRate);
bool setSPO2PulseWidth(SPO2_PulseWidth pulseWidth);

// ===================== LED Pulse Amplitude (0x0C-0x0D) =====================

bool setLED1PulseAmplitude(uint8_t amplitude);
bool setLED2PulseAmplitude(uint8_t amplitude);

// ===================== Temperature Data (0x1F-0x21) =====================

bool setTemperatureEnabled(bool enable);

#endif // MAX30102_SETTINGS_H

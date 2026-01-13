#include "max30102.h"
#include "max30102_settings.h"
#include <stdio.h>

namespace
{
    template<typename Func, typename ParamType>
    bool RUN_TEST(const char* testName, Func functionToCall, ParamType functionParamValue,
                  uint8_t regToRead, uint8_t expectedResult, bool expectedSuccess = true)
    {
        uint8_t regValue = 0;
        esp_err_t res = functionToCall(functionParamValue);

        if ((res != ESP_OK && expectedSuccess) || (res == ESP_OK && !expectedSuccess)) {
            printf("[FAIL] %s: Function call %s\n", testName, (res == ESP_OK) ? "succeeded unexpectedly" : "failed");
            return false;
        }

        if (maxim_max30102_read_reg(regToRead, &regValue) != ESP_OK) {
            printf("[FAIL] %s: Failed to read register 0x%02X\n", testName, regToRead);
            return false;
        }

        if (regValue != expectedResult) {
            printf("[FAIL] %s: Expected 0x%02X, got 0x%02X\n", testName, expectedResult, regValue);
            return false;
        }

        printf("[PASS] %s\n", testName);
        return true;
    }

    #define RUN_TEST_UPDATE(...) (RUN_TEST(__VA_ARGS__) ? passedTests++ : failedTests++)
}

bool testerSetter()
{
    int passedTests = 0;
    int failedTests = 0;

    // FIFO Write Pointer
    maxim_max30102_write_reg(REG_FIFO_WR_PTR, 0x00);
    RUN_TEST_UPDATE("FIFO Write Pointer 0x00", setFifoWritePointer, 0x00, REG_FIFO_WR_PTR, 0x00);
    RUN_TEST_UPDATE("FIFO Write Pointer 0x1F", setFifoWritePointer, 0x1F, REG_FIFO_WR_PTR, 0x1F);
    RUN_TEST_UPDATE("FIFO Write Pointer 0x20 (saturates to 0x1F, fail)", setFifoWritePointer, 0x20, REG_FIFO_WR_PTR, 0x1F, false);
    RUN_TEST_UPDATE("FIFO Write Pointer 0x00 again", setFifoWritePointer, 0x00, REG_FIFO_WR_PTR, 0x00);

    // FIFO Overflow Counter
    maxim_max30102_write_reg(REG_OVF_COUNTER, 0x00);
    RUN_TEST_UPDATE("FIFO Overflow Counter 0x00", setFifoOverflowCounter, 0x00, REG_OVF_COUNTER, 0x00);

    // FIFO Read Pointer
    maxim_max30102_write_reg(REG_FIFO_RD_PTR, 0x00);
    RUN_TEST_UPDATE("FIFO Read Pointer 0x00", setFifoReadPointer, 0x00, REG_FIFO_RD_PTR, 0x00);
    RUN_TEST_UPDATE("FIFO Read Pointer 0x1F", setFifoReadPointer, 0x1F, REG_FIFO_RD_PTR, 0x1F);
    RUN_TEST_UPDATE("FIFO Read Pointer 0x20 (saturates to 0x1F, fail)", setFifoReadPointer, 0x20, REG_FIFO_RD_PTR, 0x1F, false);

    // FIFO Configuration
    maxim_max30102_write_reg(REG_FIFO_CONFIG, 0x00);
    RUN_TEST_UPDATE("Sample Averaging NO_AVERAGING", setSampleAveraging, SampleAveraging::NO_AVERAGING, REG_FIFO_CONFIG, 0x00);
    RUN_TEST_UPDATE("Sample Averaging AVG_2", setSampleAveraging, SampleAveraging::AVG_2, REG_FIFO_CONFIG, 0x20);
    RUN_TEST_UPDATE("Sample Averaging AVG_4", setSampleAveraging, SampleAveraging::AVG_4, REG_FIFO_CONFIG, 0x40);
    RUN_TEST_UPDATE("FIFO RollOver Enabled", setFifoRollOverOnFull, true, REG_FIFO_CONFIG, 0x50);
    RUN_TEST_UPDATE("FIFO RollOver Disabled", setFifoRollOverOnFull, false, REG_FIFO_CONFIG, 0x40);
    RUN_TEST_UPDATE("FIFO Almost Full Threshold 0x00", setFifoAlmostFullThreshold, 0x00, REG_FIFO_CONFIG, 0x40);
    RUN_TEST_UPDATE("FIFO Almost Full Threshold 0x0F", setFifoAlmostFullThreshold, 0x0F, REG_FIFO_CONFIG, 0x4F);

    // SpO2 Configuration
    maxim_max30102_write_reg(REG_SPO2_CONFIG, 0x00);
    RUN_TEST_UPDATE("SPO2 ADC_RANGE_2048", setSPO2ADCRange, SPO2_ADC_Range::ADC_RANGE_2048, REG_SPO2_CONFIG, 0x00);
    RUN_TEST_UPDATE("SPO2 ADC_RANGE_4096", setSPO2ADCRange, SPO2_ADC_Range::ADC_RANGE_4096, REG_SPO2_CONFIG, 0x20);
    RUN_TEST_UPDATE("SPO2 ADC_RANGE_8192", setSPO2ADCRange, SPO2_ADC_Range::ADC_RANGE_8192, REG_SPO2_CONFIG, 0x40);
    RUN_TEST_UPDATE("SPO2 ADC_RANGE_16384", setSPO2ADCRange, SPO2_ADC_Range::ADC_RANGE_16384, REG_SPO2_CONFIG, 0x60);

    maxim_max30102_write_reg(REG_SPO2_CONFIG, 0x00);
    RUN_TEST_UPDATE("SPO2 SampleRate 50Hz", setSPO2SampleRate, SPO2_SampleRate::SPO2_RATE_50, REG_SPO2_CONFIG, 0x00);
    RUN_TEST_UPDATE("SPO2 SampleRate 100Hz", setSPO2SampleRate, SPO2_SampleRate::SPO2_RATE_100, REG_SPO2_CONFIG, 0x04);
    RUN_TEST_UPDATE("SPO2 SampleRate 200Hz", setSPO2SampleRate, SPO2_SampleRate::SPO2_RATE_200, REG_SPO2_CONFIG, 0x08);
    RUN_TEST_UPDATE("SPO2 SampleRate 400Hz", setSPO2SampleRate, SPO2_SampleRate::SPO2_RATE_400, REG_SPO2_CONFIG, 0x0C);
    RUN_TEST_UPDATE("SPO2 SampleRate 800Hz", setSPO2SampleRate, SPO2_SampleRate::SPO2_RATE_800, REG_SPO2_CONFIG, 0x10);
    RUN_TEST_UPDATE("SPO2 SampleRate 1000Hz", setSPO2SampleRate, SPO2_SampleRate::SPO2_RATE_1000, REG_SPO2_CONFIG, 0x14);
    RUN_TEST_UPDATE("SPO2 SampleRate 1600Hz", setSPO2SampleRate, SPO2_SampleRate::SPO2_RATE_1600, REG_SPO2_CONFIG, 0x18);
    RUN_TEST_UPDATE("SPO2 SampleRate 3200Hz", setSPO2SampleRate, SPO2_SampleRate::SPO2_RATE_3200, REG_SPO2_CONFIG, 0x1C);

    maxim_max30102_write_reg(REG_SPO2_CONFIG, 0x00);
    RUN_TEST_UPDATE("SPO2 PulseWidth 69us", setSPO2PulseWidth, SPO2_PulseWidth::PW_69, REG_SPO2_CONFIG, 0x00);
    RUN_TEST_UPDATE("SPO2 PulseWidth 118us", setSPO2PulseWidth, SPO2_PulseWidth::PW_118, REG_SPO2_CONFIG, 0x01);
    RUN_TEST_UPDATE("SPO2 PulseWidth 215us", setSPO2PulseWidth, SPO2_PulseWidth::PW_215, REG_SPO2_CONFIG, 0x02);
    RUN_TEST_UPDATE("SPO2 PulseWidth 411us", setSPO2PulseWidth, SPO2_PulseWidth::PW_411, REG_SPO2_CONFIG, 0x03);

    // LED Configuration
    RUN_TEST_UPDATE("LED1 Pulse Amplitude 0x00", setLED1PulseAmplitude, 0x00, REG_LED1_PA, 0x00);
    RUN_TEST_UPDATE("LED1 Pulse Amplitude 0xFF", setLED1PulseAmplitude, 0xFF, REG_LED1_PA, 0xFF);
    RUN_TEST_UPDATE("LED2 Pulse Amplitude 0x00", setLED2PulseAmplitude, 0x00, REG_LED2_PA, 0x00);
    RUN_TEST_UPDATE("LED2 Pulse Amplitude 0xFF", setLED2PulseAmplitude, 0xFF, REG_LED2_PA, 0xFF);

    printf("\nTotal tests: %d\nPassed: %d\nFailed: %d\n", passedTests + failedTests, passedTests, failedTests);

    return failedTests == 0;
}

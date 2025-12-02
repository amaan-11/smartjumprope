#ifndef SENSOR_READING_H
#define SENSOR_READING_H

#include "driver/i2c_master.h"
#include <stdint.h>

class SensorReading {
public:
    SensorReading(int i2c_port, int sda_pin, int scl_pin);

    esp_err_t begin();
    esp_err_t readRaw(int16_t &ax, int16_t &ay, int16_t &az,
                      int16_t &gx, int16_t &gy, int16_t &gz);

private:
    int _i2c_port;
    int _sda_pin;
    int _scl_pin;
    static constexpr uint8_t MPU_ADDR = 0x68;

    esp_err_t writeReg(uint8_t reg, uint8_t value);
    esp_err_t readRegs(uint8_t reg, uint8_t *data, size_t len);
};

#endif

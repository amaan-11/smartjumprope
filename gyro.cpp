#include "gyro.h"
#include "driver/i2c_master.h"
#include "esp_log.h"

static const char *TAG = "MPU";

SensorReading::SensorReading(int i2c_port, int sda_pin, int scl_pin)
    : _i2c_port(i2c_port), _sda_pin(sda_pin), _scl_pin(scl_pin) {}

esp_err_t SensorReading::begin() {
    i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = _sda_pin,
        .scl_io_num = _scl_pin,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master = { .clk_speed = 400000 }
    };

    ESP_ERROR_CHECK(i2c_param_config(_i2c_port, &cfg));
    ESP_ERROR_CHECK(i2c_driver_install(_i2c_port, cfg.mode, 0, 0, 0));

    // Wake MPU6050 (PWR_MGMT_1 = 0x00)
    return writeReg(0x6B, 0x00);
}

esp_err_t SensorReading::writeReg(uint8_t reg, uint8_t value) {
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(_i2c_port, MPU_ADDR, data, 2, 100);
}

esp_err_t SensorReading::readRegs(uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(
        _i2c_port, MPU_ADDR, &reg, 1, data, len, 100);
}

esp_err_t SensorReading::readRaw(int16_t &ax, int16_t &ay, int16_t &az,
                                 int16_t &gx, int16_t &gy, int16_t &gz) {

    uint8_t raw[14];
    esp_err_t ret = readRegs(0x3B, raw, 14);
    if (ret != ESP_OK) return ret;

    ax = (raw[0] << 8) | raw[1];
    ay = (raw[2] << 8) | raw[3];
    az = (raw[4] << 8) | raw[5];

    gx = (raw[8] << 8) | raw[9];
    gy = (raw[10] << 8) | raw[11];
    gz = (raw[12] << 8) | raw[13];

    return ESP_OK;
}

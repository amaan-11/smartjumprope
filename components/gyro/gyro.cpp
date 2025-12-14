#include "gyro.h"
#include "esp_log.h"

#define MPU_ADDR 0x68
static const char *TAG = "MPU";

SensorReading::SensorReading(int i2c_port, int sda_pin, int scl_pin)
    : _i2c_port(i2c_port), _sda_pin(sda_pin), _scl_pin(scl_pin) {}

esp_err_t SensorReading::begin() {
  i2c_config_t cfg = {.mode = I2C_MODE_MASTER,
                      .sda_io_num = _sda_pin,
                      .scl_io_num = _scl_pin,
                      .sda_pullup_en = GPIO_PULLUP_ENABLE,
                      .scl_pullup_en = GPIO_PULLUP_ENABLE,
                      .master = {.clk_speed = 400000}};

  ESP_ERROR_CHECK(i2c_param_config(_i2c_port, &cfg));
  ESP_ERROR_CHECK(i2c_driver_install(_i2c_port, cfg.mode, 0, 0, 0));

  // Wake the chip
  uint8_t wake[2] = {0x6B, 0x00};
  ESP_ERROR_CHECK(
      i2c_master_write_to_device(_i2c_port, MPU_ADDR, wake, 2, 100));

  readSensitivity();

  data_queue = xQueueCreate(10, sizeof(mpu_data_t));

  return ESP_OK;
}

void SensorReading::readSensitivity() {
  uint8_t a_cfg, g_cfg;

  i2c_master_write_read_device(_i2c_port, MPU_ADDR, (uint8_t *)"\x1C", 1,
                               &a_cfg, 1, 100);
  i2c_master_write_read_device(_i2c_port, MPU_ADDR, (uint8_t *)"\x1B", 1,
                               &g_cfg, 1, 100);

  uint8_t a_range = (a_cfg >> 3) & 0x03;
  uint8_t g_range = (g_cfg >> 3) & 0x03;

  switch (a_range) {
  case 0:
    accel_sensitivity = 16384.0f;
    break;
  case 1:
    accel_sensitivity = 8192.0f;
    break;
  case 2:
    accel_sensitivity = 4096.0f;
    break;
  case 3:
    accel_sensitivity = 2048.0f;
    break;
  }

  switch (g_range) {
  case 0:
    gyro_sensitivity = 131.0f;
    break;
  case 1:
    gyro_sensitivity = 65.5f;
    break;
  case 2:
    gyro_sensitivity = 32.8f;
    break;
  case 3:
    gyro_sensitivity = 16.4f;
    break;
  }

  ESP_LOGI(TAG, "Accel sensitivity: %f", accel_sensitivity);
  ESP_LOGI(TAG, "Gyro sensitivity: %f", gyro_sensitivity);
}

esp_err_t SensorReading::readRaw(int16_t &ax, int16_t &ay, int16_t &az,
                                 int16_t &gx, int16_t &gy, int16_t &gz) {
  uint8_t reg = 0x3B;
  uint8_t raw[14];

  esp_err_t ret =
      i2c_master_write_read_device(_i2c_port, MPU_ADDR, &reg, 1, raw, 14, 100);

  if (ret != ESP_OK)
    return ret;

  ax = (raw[0] << 8) | raw[1];
  ay = (raw[2] << 8) | raw[3];
  az = (raw[4] << 8) | raw[5];

  gx = (raw[8] << 8) | raw[9];
  gy = (raw[10] << 8) | raw[11];
  gz = (raw[12] << 8) | raw[13];

  return ESP_OK;
}

void SensorReading::taskEntry(void *param) {
  static_cast<SensorReading *>(param)->taskLoop();
}

void SensorReading::startTask() {
  xTaskCreate(taskEntry, "mpu_reader", 4096, this, 5, nullptr);
}

void SensorReading::taskLoop() {
  while (true) {
    int16_t ax, ay, az, gx, gy, gz;

    if (readRaw(ax, ay, az, gx, gy, gz) == ESP_OK) {
      mpu_data_t data;

      data.ax_g = ax / accel_sensitivity;
      data.ay_g = ay / accel_sensitivity;
      data.az_g = az / accel_sensitivity;

      data.gx_dps = gx / gyro_sensitivity;
      data.gy_dps = gy / gyro_sensitivity;
      data.gz_dps = gz / gyro_sensitivity;

      xQueueSend(data_queue, &data, 0);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

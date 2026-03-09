#include "gyro.h"
#include "esp_log.h"
#include "i2cInit.h"
#include "mutex.h"

static const char *TAG = "MPU";

SensorReading::SensorReading() : _initialized(false) {
  data_queue = xQueueCreate(10, sizeof(mpu_data_t));
  init();
}

void SensorReading::init() {
  I2CManager &i2c = I2CManager::getInstance();

  if (!i2c.isInitialized()) {
    ESP_LOGE(TAG, "I2C Manager not initialized!");
    return;
  }

  ESP_LOGI(TAG, "Initializing MPU6050...");

  {
    MutexGuard lock(i2c.getMutex());
    ESP_LOGI(TAG, "[GYRO] Acquired I2C lock for initialization");

    // Wake the chip
    uint8_t wake[2] = {0x6B, 0x00};
    esp_err_t err = i2c_master_write_to_device(i2c.getPort(), MPU_ADDR, wake, 2,
                                               pdMS_TO_TICKS(100));

    if (err != ESP_OK) {
      ESP_LOGE(TAG, "Failed to wake MPU: %s", esp_err_to_name(err));
      ESP_LOGI(TAG, "[GYRO] Released I2C lock (failed)");
      return;
    }

    readSensitivity();
    ESP_LOGI(TAG, "[GYRO] Released I2C lock (success)");
  }

  _initialized = true;
  ESP_LOGI(TAG, "MPU6050 initialized successfully");
}

void SensorReading::readSensitivity() {
  I2CManager &i2c = I2CManager::getInstance();

  uint8_t a_cfg, g_cfg;

  i2c_master_write_read_device(i2c.getPort(), MPU_ADDR, (uint8_t *)"\x1C", 1,
                               &a_cfg, 1, pdMS_TO_TICKS(100));
  i2c_master_write_read_device(i2c.getPort(), MPU_ADDR, (uint8_t *)"\x1B", 1,
                               &g_cfg, 1, pdMS_TO_TICKS(100));

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

// Read accelerometer only
esp_err_t SensorReading::readRawAccel(int16_t &az) {
  if (!_initialized)
    return ESP_FAIL;

  I2CManager &i2c = I2CManager::getInstance();
  MutexGuard lock(i2c.getMutex());

  uint8_t reg = 0x3B; // start register for accelerometer
  uint8_t raw[6];     // only need 6 bytes for accel
  esp_err_t ret = i2c_master_write_read_device(i2c.getPort(), MPU_ADDR, &reg, 1,
                                               raw, 6, pdMS_TO_TICKS(100));
  if (ret != ESP_OK)
    return ret;
  az = (raw[4] << 8) | raw[5];

  return ESP_OK;
}

// Read gyroscope only
esp_err_t SensorReading::readRawGyro(int16_t &gx, int16_t &gy, int16_t &gz) {
  if (!_initialized)
    return ESP_FAIL;

  I2CManager &i2c = I2CManager::getInstance();
  MutexGuard lock(i2c.getMutex());

  uint8_t reg = 0x43; // start register for gyro
  uint8_t raw[6];     // 6 bytes for gyro
  esp_err_t ret = i2c_master_write_read_device(i2c.getPort(), MPU_ADDR, &reg, 1,
                                               raw, 6, pdMS_TO_TICKS(100));
  if (ret != ESP_OK)
    return ret;

  gx = (raw[0] << 8) | raw[1];
  gy = (raw[2] << 8) | raw[3];
  gz = (raw[4] << 8) | raw[5];

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
    int16_t az, gx, gy, gz;

    if (readRawAccel(az) == ESP_OK && readRawGyro(gx, gy, gz)) {
      mpu_data_t data;
      
      data.az_g = az / accel_sensitivity;

      data.gx_dps = gx / gyro_sensitivity;
      data.gy_dps = gy / gyro_sensitivity;
      data.gz_dps = gz / gyro_sensitivity;

      xQueueSend(data_queue, &data, 0);
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
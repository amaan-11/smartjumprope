#include "esp_log.h"
#include "i2cInit.h"

static const char *TAG = "I2C_MANAGER";

// Configuration constants
constexpr i2c_port_t I2C_PORT = I2C_NUM_0;
constexpr int I2C_SDA_PIN = 19;
constexpr int I2C_SCL_PIN = 20;
constexpr uint32_t I2C_FREQ_HZ = 400000;

I2CManager::I2CManager()
    : _i2c_port(I2C_PORT), _sda_pin(I2C_SDA_PIN), _scl_pin(I2C_SCL_PIN),
      _clk_speed(I2C_FREQ_HZ), _initialized(false), _mutex(nullptr) {
  }

I2CManager::~I2CManager() = default;

I2CManager &I2CManager::getInstance() {
  static I2CManager instance;
  return instance;
}
void I2CManager::init() {
  if (_initialized) {
    ESP_LOGW(TAG, "I2C already initialized");
    return;
  }

  _mutex = xSemaphoreCreateMutex();
  assert(_mutex != nullptr);

  i2c_config_t conf = {};
  conf.mode = I2C_MODE_MASTER;
  conf.sda_io_num = _sda_pin;
  conf.scl_io_num = _scl_pin;
  conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
  conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
  conf.master.clk_speed = _clk_speed;

  esp_err_t ret = i2c_param_config(_i2c_port, &conf);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
    return;
  }

  ret = i2c_driver_install(_i2c_port, conf.mode, 0, 0, 0);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
    return;
  }

  _initialized = true;
  ESP_LOGI(TAG, "I2C initialized on port %d (SDA=%d, SCL=%d, Speed=%lu Hz)",
           _i2c_port, _sda_pin, _scl_pin, _clk_speed);
}

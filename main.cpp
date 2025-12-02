#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "gyro.h"
#include <stdio.h>

#define I2C_PORT I2C_NUM_0
#define SDA_PIN 8
#define SCL_PIN 9

printf("Hello, World");

static const char *TAG = "MAIN";

extern "C" void app_main(void)
{
    SensorReading imu(I2C_PORT, SDA_PIN, SCL_PIN);

    if (imu.begin() != ESP_OK) {
        ESP_LOGE(TAG, "MPU6050 init failed");
        return;
    }

    ESP_LOGI(TAG, "MPU6050 initialized");

    while (true) {
        int16_t ax, ay, az, gx, gy, gz;

        if (imu.readRaw(ax, ay, az, gx, gy, gz) == ESP_OK) {
            ESP_LOGI(TAG,
                     "ACC: %d %d %d | GYRO: %d %d %d",
                     ax, ay, az, gx, gy, gz);
        } else {
            ESP_LOGE(TAG, "MPU read failed");
        }

        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

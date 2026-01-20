cmake_minimum_required(VERSION 3.16)

idf_component_register(
        SRCS
            "main.cpp"
            "gpio_pin.cpp"
            "jump.cpp"
            "jr_ble.cpp"
        INCLUDE_DIRS "."
        REQUIRES nvs_flash bt gyro esp_lcd display driver esp_timer i2cInit common heartbeatSensor
)

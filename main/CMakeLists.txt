cmake_minimum_required(VERSION 3.16)
idf_component_register(
    SRCS
        "main.cpp"
        "gpio_pin.cpp"
        "jump.cpp"
       
    INCLUDE_DIRS "."
    REQUIRES gyro esp_lcd display driver esp_timer i2cInit common heartbeatSensor nvs_flash ble
)

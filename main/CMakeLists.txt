cmake_minimum_required(VERSION 3.16)
#cmake_minimum_required(VERSION 4.2)
idf_component_register(
    SRCS
        "main.cpp"
        "gpio_pin.cpp"
       
    INCLUDE_DIRS "."
    REQUIRES gyro esp_lcd display driver
)

cmake_minimum_required(VERSION 3.16)
#cmake_minimum_required(VERSION 4.2)
idf_component_register(
    SRCS
        "main.cpp"
        "gpio_pin.cpp"
        "menu.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        display
        gyro
)

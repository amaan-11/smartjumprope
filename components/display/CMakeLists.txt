cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        "display.cpp"
        "font5x7.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        i2cInit driver esp_lcd common
)

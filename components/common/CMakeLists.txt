cmake_minimum_required(VERSION 3.16)

idf_component_register(
    INCLUDE_DIRS "."

REQUIRES
        i2cInit driver esp_lcd
)
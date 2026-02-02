cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS "jr_ble.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver i2cInit common nvs_flash ble bt
)

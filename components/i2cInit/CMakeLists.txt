cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS "i2cInit.cpp"
    INCLUDE_DIRS "."
    REQUIRES driver common
)

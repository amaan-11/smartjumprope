#cmake_minimum_required(VERSION 4.2)
cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS "display.c"
    INCLUDE_DIRS "."
)

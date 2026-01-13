idf_component_register(
    SRCS "max30102_settings_TESTER.cpp" "algorithm.cpp" "max30102.cpp"
    INCLUDE_DIRS "."  # The headers are in the same folder
    REQUIRES driver   # Requires ESP-IDF I2C driver
)

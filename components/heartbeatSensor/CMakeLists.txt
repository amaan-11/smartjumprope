idf_component_register(
    SRCS "max30102_settings.cpp" "max30102_settings_TESTER.cpp"  "max30102.cpp"
    "algorithm_by_RF.cpp"
    INCLUDE_DIRS "."  # The headers are in the same folder
    REQUIRES driver   # Requires ESP-IDF I2C driver
)

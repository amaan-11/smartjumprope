#!/bin/bash

echo "Starting ESP32-C6 build"

IDF_PATH=~/esp/esp-idf

# Source ESP-IDF environment
. $IDF_PATH/export.sh

# Clean previous builds (optional)
idf.py fullclean

# Set target to ESP32-C6
idf.py set-target esp32c6

# Build the project
idf.py build

# Flash to the device
idf.py flash

# Open serial monitor
idf.py monitor

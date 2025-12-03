#!/bin/bash

cd ~/smartjumprope
. ~/esp/esp-idf/export.sh
rm -rf build sdkconfig sdkconfig.old
idf.py set-target esp32c6
idf.py build
idf.py -p /dev/ttyACM0 flash monitor

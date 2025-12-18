#!/bin/bash

cd 
. ~/esp/esp-idf/export.sh
cd ~/smartjumprope
idf.py set-target esp32c6    
idf.py fullclean
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
#use ctrl+] to exit monitoring
#!/bin/bash
cd 
. ~/esp/esp-idf/export.sh
cd ~/smartjumprope
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
#use ctrl+] to exit monitoring
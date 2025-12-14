#!/bin/bash

cd 
. ~/esp/esp-idf/export.sh
cd ~/smartjumprope
idf.py set-target esp32c6    # switch target
idf.py fullclean              # ensures build cache is fully cleared
idf.py build                  # rebuild for new target

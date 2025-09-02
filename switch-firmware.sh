#!/bin/bash

# Switch between main firmware and KY-038 test firmware

cd /home/rick/Arduino/esp32-wroom-32-AP-Provision/src

if [ "$1" = "test" ]; then
    echo "Switching to KY-038 TEST mode..."
    cp esp32-wroom-32-AP-Provision.cpp main-firmware.cpp
    cp KY-038-test.cpp esp32-wroom-32-AP-Provision.cpp
    echo "✅ Now running KY-038 hardware test"
    echo "Run: pio run --target upload"

elif [ "$1" = "main" ]; then
    echo "Switching to MAIN firmware..."
    cp esp32-wroom-32-AP-Provision.cpp KY-038-test.cpp
    cp main-firmware.cpp esp32-wroom-32-AP-Provision.cpp
    echo "✅ Now running main dog bark detector"
    echo "Run: pio run --target upload"

else
    echo "Usage: $0 [test|main]"
    echo "  test  - Switch to KY-038 hardware test"
    echo "  main  - Switch to main dog bark detector"
    echo ""
    echo "Current files:"
    ls -la *.cpp
fi

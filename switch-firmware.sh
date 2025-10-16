#!/bin/bash

# Switch between different firmware variants

cd /home/rick/Arduino/esp32-wroom-32-AP-Provision/src

if [ "$1" = "ky038-test" ]; then
    echo "Switching to KY-038 TEST mode..."
    cp esp32-wroom-32-AP-Provision.cpp main-firmware.cpp 2>/dev/null || true
    cp KY-038-test.cpp esp32-wroom-32-AP-Provision.cpp
    echo "✅ Now running KY-038 hardware test"
    echo "Run: pio run --target upload"

elif [ "$1" = "inmp441-test" ]; then
    echo "Switching to INMP441 TEST mode..."
    cp esp32-wroom-32-AP-Provision.cpp main-firmware.cpp 2>/dev/null || true
    cp INMP441-test.cpp esp32-wroom-32-AP-Provision.cpp
    echo "✅ Now running INMP441 I2S microphone test"
    echo "Run: pio run --target upload"

elif [ "$1" = "webrtc" ]; then
    echo "Switching to WEBRTC mode..."
    cp esp32-wroom-32-AP-Provision.cpp inmp441-firmware.cpp 2>/dev/null || true
    cp webrtc-firmware.cpp esp32-wroom-32-AP-Provision.cpp
    echo "✅ Now running WebRTC streaming firmware"
    echo "Run: pio run --target upload"

elif [ "$1" = "main" ] || [ "$1" = "legacy" ]; then
    echo "Switching to LEGACY KY-038 firmware..."
    cp esp32-wroom-32-AP-Provision.cpp backup-firmware.cpp 2>/dev/null || true
    cp main-firmware.cpp esp32-wroom-32-AP-Provision.cpp
    echo "✅ Now running legacy KY-038 firmware"
    echo "Run: pio run --target upload"

else
    echo "Usage: $0 [ky038-test|inmp441-test|webrtc|main|legacy]"
    echo ""
    echo "Firmware variants:"
    echo "  ky038-test   - KY-038 analog microphone hardware test"
    echo "  inmp441-test - INMP441 I2S digital microphone test" 
    echo "  webrtc       - WebRTC streaming with INMP441 (MAIN TARGET)"
    echo "  main/legacy  - Legacy KY-038 dog bark detector"
    echo ""
    echo "Current files:"
    ls -la *.cpp 2>/dev/null | head -10
    echo ""
    echo "Recommended: Use 'inmp441-test' first to verify hardware,"
    echo "             then 'webrtc' for full WebRTC functionality"
fi

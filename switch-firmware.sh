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

elif [ "$1" = "secure" ] || [ "$1" = "secure-webrtc" ]; then
    echo "Switching to SECURE WEBRTC mode..."
    cp esp32-wroom-32-AP-Provision.cpp webrtc-firmware.cpp 2>/dev/null || true
    cp secure-webrtc-firmware.cpp esp32-wroom-32-AP-Provision.cpp
    echo "✅ Now running SECURE WebRTC streaming firmware"
    echo "⚠️  HTTPS + Authentication + Rate Limiting + SRTP"
    echo "Run: pio run --target upload"

elif [ "$1" = "main" ] || [ "$1" = "legacy" ]; then
    echo "Switching to LEGACY KY-038 firmware..."
    cp esp32-wroom-32-AP-Provision.cpp backup-firmware.cpp 2>/dev/null || true
    cp main-firmware.cpp esp32-wroom-32-AP-Provision.cpp
    echo "✅ Now running legacy KY-038 firmware"
    echo "Run: pio run --target upload"

else
    echo "Usage: $0 [ky038-test|inmp441-test|webrtc|secure|main|legacy]"
    echo ""
    echo "Firmware variants:"
    echo "  ky038-test   - KY-038 analog microphone hardware test"
    echo "  inmp441-test - INMP441 I2S digital microphone test" 
    echo "  webrtc       - WebRTC streaming with INMP441"
    echo "  secure       - SECURE WebRTC with HTTPS/Auth/SRTP (PRODUCTION)"
    echo "  main/legacy  - Legacy KY-038 dog bark detector"
    echo ""
    echo "Current files:"
    ls -la *.cpp 2>/dev/null | head -10
    echo ""
    echo "Development flow:"
    echo "  1. 'inmp441-test' - Verify INMP441 hardware setup"
    echo "  2. 'webrtc'       - Test basic WebRTC functionality"  
    echo "  3. 'secure'       - Production-ready with full security"
fi

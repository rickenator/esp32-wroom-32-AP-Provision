# ESP32 WebRTC Audio Streaming Device

ESP32-WROOM-32 project implementing **real-time audio streaming** with **INMP441 digital microphone** and WebRTC capabilities. Features WiFi provisioning, I2S audio capture, G.711 encoding, and RTP streaming.

## 🎯 Current Status: **WebRTC Implementation Complete**

**✅ IMPLEMENTED:**
- ✅ INMP441 I2S digital microphone integration
- ✅ Real-time audio capture (16kHz, 16-bit mono)
- ✅ G.711 A-law encoding for WebRTC compatibility
- ✅ Ring buffer audio processing with FreeRTOS tasks
- ✅ WiFi provisioning with captive portal
- ✅ Web-based audio monitoring and RTP configuration
- ✅ Multi-firmware switching system (test/production modes)

## Hardware Requirements

- **Target Board**: ESP32-WROOM-32 (DevKitC or compatible)
- **Microphone Module**: INMP441 digital MEMS microphone (I2S interface)
  - High-quality 24-bit digital audio output via I2S
  - Built-in ADC eliminates analog noise
  - Optimized for voice and audio applications
  - Direct I2S connection to ESP32 for superior audio quality

## Development Environment Setup

### Prerequisites

1. **PlatformIO** - Cross-platform build system for embedded development
   - Download and install from: https://platformio.org/platformio-ide
   - Works with VS Code, Atom, CLion, Vim, and command line

2. **Git** - Version control system
   - Download from: https://git-scm.com/downloads

### Setup Steps

1. **Clone the repository:**
   ```bash
   git clone https://github.com/rickenator/esp32-wroom-32-AP-Provision.git
   cd esp32-wroom-32-AP-Provision
   ```

2. **Open in your preferred editor:**
   - **VS Code**: Install PlatformIO extension, then open the project folder
   - **Other editors**: Use PlatformIO Core CLI commands

3. **Install dependencies:**
   ```bash
   # Using PlatformIO CLI
   pio pkg install

   # Or build the project (this will install dependencies automatically)
   pio run
   ```

4. **Connect your ESP32 board** and **upload firmware:**
   ```bash
   pio run --target upload
   ```

5. **Monitor serial output:**
   ```bash
   pio device monitor
   ```

### Alternative: Arduino IDE Setup

If you prefer Arduino IDE:

1. Install Arduino IDE 2.x from: https://www.arduino.cc/en/software
2. Install ESP32 board support via Board Manager (search for "esp32")
3. Copy the contents of `src/esp32-wroom-32-AP-Provision.cpp` to a new Arduino sketch
4. Install required libraries via Library Manager:
   - WiFi
   - WebServer
   - DNSServer
   - Preferences
   - HTTPClient (optional, for webhook support)

## Usage

### Provisioning

1. **Power on the ESP32** - it will start in AP mode if no WiFi credentials are stored
2. **Connect to the captive portal** - look for SSID starting with "Aniviza-"
3. **Open browser** and navigate to the provisioning page
4. **Enter your WiFi credentials** and submit
5. **Device will connect** to your network and switch to STA mode

### Serial Console Commands

Connect to the ESP32 via serial monitor and use these commands:

- `help` - Show all available commands
- `status` - Print Wi-Fi/network status
- `clear-net` - Clear only saved SSID/password (Preferences 'net')
- `flush-nvs` - Erase entire NVS partition (all namespaces)
- `reprov` - Clear-net and start provisioning AP now
- `reboot` - Restart MCU
- `sound` - Show sound sensor status and detection count
- `record <ms>` - Start ADC recording for specified milliseconds

### Button Controls

- **Short press** (< 500ms): Start provisioning AP (keep existing NVS)
- **Long press** (500ms - 3s): Clear network settings + start provisioning AP
- **Very long press** (3s - 6s): Flush entire NVS + reboot

## Project Structure

```
esp32-wroom-32-AP-Provision/
├── src/
│   └── esp32-wroom-32-AP-Provision.cpp  # Main firmware
├── platformio.ini                      # PlatformIO configuration
├── .gitignore                         # Git ignore rules
└── README.md                          # This file
```

## Configuration

### GPIO Pin Assignments

- **INMP441 SCK (I2S Clock)**: GPIO 26
- **INMP441 WS (Word Select)**: GPIO 25  
- **INMP441 SD (Serial Data)**: GPIO 33
- **INMP441 L/R**: GND (left channel)
- **BOOT Button**: GPIO 0 (active-low, pull-up)
- **Heartbeat LED**: GPIO 2 (optional, set to -1 to disable)

### NVS Namespaces

- `net` - WiFi credentials (SSID, password)
- `sound` - Sound sensor configuration (MQTT settings, webhook URL)

## Current Features (feature-webrtc-INMP441 branch)

- **I2S Audio Capture**: High-quality digital audio from INMP441 microphone
- **Real-time Audio Processing**: Continuous audio capture with ring buffer
- **WebRTC Integration**: Audio streaming with G.711 encoding and RTP transport
- **Audio Monitoring**: Live audio level meters and quality metrics
- **WiFi Provisioning**: Captive portal for easy network setup

## Notes

- Stored Wi-Fi credentials are kept in NVS namespace `net`
- `flush-nvs` erases the entire NVS partition - use with caution
- WebRTC on ESP32 is experimental and may require careful resource tuning
- Sound sensor polarity can be configured via `SOUND_DO_ACTIVE_HIGH` define

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

This project is open source. Please check individual file headers for license information.

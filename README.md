# ESP32 Home Monitoring Device - Dog Bark Detector
Feature branch: feature-webrtc

This repository demonstrates a captive-portal provisioning flow for ESP32 devices and a planned extension on the `feature-webrtc` branch to capture microphone audio and stream it via WebRTC.

## Hardware Requirements

- **Target Board**: ESP32-WROOM-32 (DevKitC or compatible)
- **Microphone Module**: KY-038 module (LM393 comparator + analog output)
  - Audio input can be read from the module's A0 (analog) or D0 (digital comparator) pin
  - D0 pin provides hardware-level threshold detection
  - A0 pin provides raw analog audio signal for ADC sampling

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

- **Sound Sensor D0**: GPIO 15 (digital threshold detection)
- **Sound Sensor A0**: GPIO 34 (analog audio input)
- **BOOT Button**: GPIO 0 (active-low, pull-up)
- **Heartbeat LED**: GPIO 2 (optional, set to -1 to disable)

### NVS Namespaces

- `net` - WiFi credentials (SSID, password)
- `sound` - Sound sensor configuration (MQTT settings, webhook URL)

## Planned Features (feature-webrtc branch)

- **Digital Detection**: Hardware-level sound detection with MQTT/webhook notifications
- **ADC Recording**: Circular buffer for audio capture with configurable duration
- **WebRTC Integration**: Real-time audio streaming with Opus encoding
- **Calibration UI**: Web interface for sensor sensitivity tuning

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

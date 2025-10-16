# ESP32 WiFi Provisioning - Bare Bones Implementation

This is the **bare bones** WiFi provisioning code for ESP32, providing a minimal, reliable foundation for WiFi connectivity in IoT projects. This branch contains only the essential provisioning functionality without additional sensors or advanced features.

## 🎯 **What This Provides**

This branch offers **two implementation options**:

### 📋 **Option 1: Simple Polling Version** (`AP-Provision.ino`)
- **WiFi Provisioning** via captive portal (connects automatically when you join the AP)
- **Web-based configuration** with a clean, mobile-friendly interface
- **Persistent credential storage** using ESP32 NVS (Non-Volatile Storage)
- **Simple polling-based architecture** - easy to understand and modify
- **Serial console commands** for debugging and configuration
- **Hardware button controls** for reprovisioning and factory reset
- **Automatic reconnection** with configurable retry logic

### ⚡ **Option 2: Enhanced FreeRTOS Version** (`barebones-freertos.cpp`)
- **All features from Option 1** plus advanced capabilities:
- **Interrupt-driven button handling** - never miss button presses
- **Non-blocking serial processing** with command timeout
- **Task-based architecture** - better separation of concerns and real-time performance
- **FreeRTOS task monitoring** via web interface (`/tasks` endpoint)
- **Mutex-protected WiFi operations** for thread safety
- **Foundation for sensor integration** - easy to add dedicated processing tasks

## 🚀 **Quick Start Guide**

### Step 1: Hardware Requirements
- **ESP32-WROOM-32** (DevKitC or compatible board)
- **USB cable** for programming and power
- **Computer** with Arduino IDE or PlatformIO

### Step 2: Development Environment Setup

#### Option A: Arduino IDE (Recommended for beginners)
1. **Install Arduino IDE 2.x** from https://www.arduino.cc/en/software
2. **Add ESP32 board support:**
   - Open Arduino IDE → File → Preferences
   - Add to "Additional Board Manager URLs": 
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_dev_index.json
     ```
   - Go to Tools → Board → Board Manager
   - Search for "esp32" and install "ESP32 by Espressif Systems"

3. **Select your board:**
   - Tools → Board → ESP32 Arduino → "ESP32 Dev Module"
   - Tools → Port → (select your COM port)

#### Option B: PlatformIO (Advanced users)
1. **Install VS Code** from https://code.visualstudio.com/
2. **Install PlatformIO extension** from the VS Code marketplace
3. **Clone and open this project:**
   ```bash
   git clone https://github.com/rickenator/esp32-wroom-32-AP-Provision.git
   cd esp32-wroom-32-AP-Provision
   git checkout feature-barebones
   # Open folder in VS Code with PlatformIO
   ```

### Step 3: Choose Your Implementation

#### Simple Version (Recommended for beginners)
**Arduino IDE Method:**
1. **Open the sketch:** File → Open → `AP-Provision.ino`
2. **Verify compilation:** Click the checkmark (✓) button
3. **Upload to ESP32:** Click the arrow (→) button
4. **Open Serial Monitor:** Tools → Serial Monitor (set to 115200 baud)

#### Enhanced FreeRTOS Version (Advanced users)
**Arduino IDE Method:**
1. **Copy the enhanced code:** Copy contents of `barebones-freertos.cpp`
2. **Create new sketch:** File → New, paste the code
3. **Save as:** `Enhanced-Provisioning.ino`
4. **Compile and upload** as above

**Note:** The enhanced version requires more memory (~80KB vs ~50KB) but provides better real-time performance and easier expansion for sensor integration.

#### PlatformIO Method:
```bash
# Build the project
pio run

# Upload to ESP32
pio run --target upload

# Monitor serial output
pio device monitor
```

### Step 4: Provision Your Device

1. **Connect to the AP:**
   - Look for a WiFi network named `Aniviza-XXXX` (where XXXX is a random hex code)
   - Connect to this network (no password required)

2. **Access the provisioning page:**
   - Your browser should automatically open the provisioning page
   - If not, navigate to `http://192.168.4.1` manually

3. **Enter your WiFi credentials:**
   - Select your WiFi network or enter the SSID manually
   - Enter your WiFi password
   - Click "Save & Connect"

4. **Verify connection:**
   - Watch the serial monitor for connection status
   - The ESP32 will switch to station mode and connect to your WiFi
   - The provisioning AP will automatically shut down

## 🔧 **Features & Controls**

### Hardware Button Controls (GPIO 0 - BOOT button)
- **Short press** (0.5-3 seconds): Start provisioning AP (keeps other settings)
- **Long press** (3-6 seconds): Clear WiFi + start provisioning  
- **Very long press** (6+ seconds): Factory reset + reboot

### Status LED (GPIO 2)
- **Blinking** - Device is in provisioning mode or attempting connection
- **Solid** - Successfully connected to WiFi network

## 📊 **Configuration Options**

### Timing Settings (in code)
```cpp
#define CONNECT_TIMEOUT_MS 15000   // WiFi connection timeout
#define RETRY_CONNECT_MS    5000   // Retry interval for failed connections
#define BTN_SHORT_MS        500    // Short button press threshold  
#define BTN_LONG_MS         3000   // Long button press threshold
#define BTN_VLONG_MS        6000   // Very long press threshold
```

### GPIO Pin Assignments
```cpp
#define HEARTBEAT_GPIO 2    // Status LED (set to -1 to disable)
#define BOOT_BTN_GPIO  0    // Hardware button (active-low)
```

### Network Configuration
```cpp
IPAddress apIP(192,168,4,1), netMsk(255,255,255,0);  // AP network settings
#define DNS_PORT 53  // Captive portal DNS port
```

## 🔍 **Troubleshooting**

### Device Won't Connect to WiFi
1. **Check credentials:** Use serial command `status` to see connection attempts
2. **Signal strength:** Ensure ESP32 is close to your router during setup
3. **Hidden networks:** Type SSID exactly (case-sensitive) if network is hidden
4. **Reset and retry:** Use button long-press or serial `reprov` command

### Can't Access Provisioning Page
1. **Check AP name:** Look for `Aniviza-XXXX` in WiFi list
2. **Manual navigation:** Go to `http://192.168.4.1` in browser
3. **Clear browser cache:** Try incognito/private browsing mode
4. **Restart device:** Power cycle the ESP32

### Serial Monitor Issues
1. **Baud rate:** Ensure serial monitor is set to 115200 baud
2. **Port selection:** Check that correct COM port is selected
3. **Driver issues:** Install ESP32 USB drivers if needed

### Memory or Performance Issues
1. **Monitor heap:** Use `/diag` page or `status` command to check free memory
2. **Reduce log level:** Change `LOG_LEVEL` from `DEBUG` to `INFO` in code
3. **Factory reset:** Use very long button press or `flush-nvs` command

## 🔧 **Choosing the Right Version**

### When to Use Simple Version (`AP-Provision.ino`)
✅ **Best for:**
- Learning ESP32 WiFi provisioning concepts
- Simple IoT projects with basic sensor integration
- Battery-powered devices (lower memory usage)
- Projects where code simplicity is paramount
- Quick prototyping and educational purposes

❌ **Limitations:**
- Button presses can be missed during WiFi operations
- Serial commands may timeout during blocking operations
- Less suitable for real-time applications
- Single-threaded execution

### When to Use Enhanced Version (`barebones-freertos.cpp`)
✅ **Best for:**
- Production IoT devices requiring reliability
- Projects with multiple sensors or actuators
- Real-time applications with timing requirements
- Systems needing concurrent operations
- Foundation for complex feature additions

❌ **Trade-offs:**
- Higher memory usage (~30KB more RAM)
- More complex code structure
- Requires understanding of FreeRTOS concepts
- Slightly longer boot time

### Technical Comparison
| Feature | Simple Version | Enhanced Version |
|---------|---------------|------------------|
| **Memory (RAM)** | ~50KB | ~80KB |
| **Boot Time** | ~2 seconds | ~3 seconds |
| **Button Response** | Can miss presses | Never misses |
| **Serial Timeout** | No | Yes (5 seconds) |
| **Task Monitoring** | No | Yes (/tasks page) |
| **Thread Safety** | Single thread | Mutex protected |
| **Expansion Ready** | Manual integration | Task-based |
| **Code Complexity** | Simple | Moderate |

### Serial Console Commands (115200 baud)

#### Simple Version Commands:
```
help       - Show all available commands
status     - Print current WiFi/network status  
clear-net  - Clear saved WiFi credentials only
flush-nvs  - Erase entire NVS partition (factory reset)
reprov     - Clear credentials and restart provisioning
reboot     - Restart the ESP32
```

#### Enhanced Version Additional Commands:
```
tasks      - Show FreeRTOS task information and queue status
(all commands above plus enhanced timeout handling)
```

### Web Interface Pages

#### Both Versions:
- **`/`** - Main provisioning page with WiFi setup form
- **`/scan`** - Scan and display nearby WiFi networks
- **`/diag`** - System diagnostics (uptime, memory, WiFi status)
- **`/status`** - Connection status after submitting credentials

#### Enhanced Version Only:
- **`/tasks`** - FreeRTOS task status, stack usage, and queue monitoring

### Customization Points
This bare bones implementation is designed to be easily customized:

1. **Add your sensors/actuators** to the main loop
2. **Extend the web interface** by adding new routes in `bindRoutes()`
3. **Add MQTT or HTTP client** functionality after WiFi connection
4. **Implement OTA updates** using ESP32 built-in capabilities
5. **Add configuration parameters** using the Preferences library

### Memory Usage
- **Flash:** ~300KB (plenty of room for additional features)
- **RAM:** ~50KB used (200KB+ available for your application)
- **NVS:** Minimal usage (only WiFi credentials stored)

## 📁 **Project Structure**
```
esp32-wroom-32-AP-Provision/
├── AP-Provision.ino          # Simple polling version (RECOMMENDED START)
├── barebones-freertos.cpp    # Enhanced FreeRTOS version (PRODUCTION READY)
├── README.md                 # This comprehensive documentation
└── Review.md                 # Technical code review and analysis
```

## 🔮 **What's Next?**

After successfully provisioning your device, you can:

1. **Add your application logic** to the main loop
2. **Implement IoT connectivity** (MQTT, HTTP APIs, etc.)
3. **Add sensors or actuators** for your specific use case
4. **Create a custom web interface** for device control
5. **Implement data logging** or cloud integration

This bare bones foundation provides reliable WiFi connectivity that you can build upon for virtually any ESP32 IoT project.

## 📚 **Additional Resources**

- **ESP32 Arduino Core:** https://github.com/espressif/arduino-esp32
- **ESP32 Documentation:** https://docs.espressif.com/projects/esp32/
- **Preferences Library:** Store configuration data persistently
- **WebServer Library:** Create custom web interfaces
- **WiFi Library:** Advanced WiFi functionality

---

**License:** Open source - see individual file headers for details  
**Hardware Tested:** ESP32-WROOM-32 DevKitC  
**Arduino Core:** ESP32 v2.0.0+  
**Status:** Production ready for WiFi provisioning foundation
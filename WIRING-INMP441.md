# ESP32-WROOM-32 + INMP441 Wiring Guide

This document shows the pin-to-pin connections for the ESP32 WebRTC audio streaming device with INMP441 digital microphone.

## Hardware Components

- **ESP32-WROOM-32 DevKitC** (or compatible ESP32 development board)
- **INMP441 Digital MEMS Microphone** (I2S interface)
- **Optional: LED** (for heartbeat indicator)
- **Power Supply**: 3.3V for both ESP32 and INMP441

## Pin Assignments

| Component | ESP32 Pin | GPIO | INMP441 Pin | Purpose |
|-----------|-----------|------|-------------|---------|
| I2S SCK | GPIO 26 | 26 | SCK | I2S Bit Clock |
| I2S WS | GPIO 25 | 25 | WS | I2S Word Select (L/R Clock) |
| I2S SD | GPIO 33 | 33 | SD | I2S Serial Data |
| Power | 3.3V | - | VDD | Power supply |
| Ground | GND | - | GND | Ground |
| Channel Select | GND | - | L/R | Left channel (GND), Right channel (VDD) |
| BOOT Button | GPIO 0 | 0 | - | Built-in button for device management |
| Heartbeat LED | GPIO 2 | 2 | - | Optional status LED |

## Wiring Diagram

### ESP32 DevKitC Top View
```
ESP32-WROOM-32 DevKitC
+---------------------+
|                     |
|   EN  BOOT          |
|   [ ] [ ]           |
|                     |
|   GPIO 0  [BOOT] <--|-- BOOT Button (built-in)
|   GPIO 2  [LED]  <--|-- Heartbeat LED (optional)
|   GPIO 25 [WS]   <--|-- INMP441 WS
|   GPIO 26 [SCK]  <--|-- INMP441 SCK  
|   GPIO 33 [SD]   <--|-- INMP441 SD
|                     |
|   3.3V [PWR]     <--|-- INMP441 VDD
|   GND  [GND]     <--|-- INMP441 GND & L/R
|                     |
+---------------------+
```

### INMP441 Microphone Module
```
INMP441 Module (Top View)
+---------------------+
|                     |
|      [MIC HOLE]     |  <- Acoustic port (top side)
|                     |
|  VDD  [o] [o] L/R   |
|  SCK  [o] [o] WS    |
|  SD   [o] [o] GND   |
|                     |
+---------------------+
```

## Detailed Connections

### Power Connections
```
ESP32 DevKitC          INMP441 Module
-------------          --------------
3.3V (red)    ------>  VDD (red)
GND  (black)  ------>  GND (black)
GND  (black)  ------>  L/R (blue)    // Channel select: GND = Left
```

### I2S Signal Connections
```
ESP32 DevKitC          INMP441 Module
-------------          --------------
GPIO 26 (yellow) ----> SCK (yellow)   // I2S Bit Clock
GPIO 25 (green)  ----> WS  (green)    // I2S Word Select 
GPIO 33 (white)  ----> SD  (white)    // I2S Serial Data
```

### Optional Connections
```
ESP32 DevKitC          LED (optional)
-------------          --------------
GPIO 2 (orange)  ---->  Anode (+)     // Heartbeat indicator
GND             ---->  Cathode (-)   // via 220Ω resistor
```

## Connection Summary Table

| ESP32 Pin | Wire Color | INMP441 Pin | Purpose |
|-----------|------------|-------------|---------|
| 3.3V | Red | VDD | Power supply (3.3V) |
| GND | Black | GND | Ground |
| GND | Blue | L/R | Channel select (Left=GND) |
| GPIO 26 | Yellow | SCK | I2S Bit Clock |
| GPIO 25 | Green | WS | I2S Word Select |
| GPIO 33 | White | SD | I2S Serial Data |
| GPIO 2 | Orange | - | Optional heartbeat LED |
| GPIO 0 | - | - | Built-in BOOT button |

## Setup Steps

1. **Power off** both the ESP32 and INMP441 module
2. **Connect power wires** first:
   - ESP32 3.3V → INMP441 VDD
   - ESP32 GND → INMP441 GND
   - ESP32 GND → INMP441 L/R (selects left channel)
3. **Connect I2S signal wires**:
   - ESP32 GPIO 26 → INMP441 SCK
   - ESP32 GPIO 25 → INMP441 WS
   - ESP32 GPIO 33 → INMP441 SD
4. **Optional: Connect LED**:
   - ESP32 GPIO 2 → LED anode (with 220Ω resistor)
   - ESP32 GND → LED cathode
5. **Power on** the ESP32

## Important Notes

### INMP441 Specific
- **Orientation**: The microphone hole should face the sound source
- **Channel Selection**: L/R pin to GND = Left channel, L/R pin to VDD = Right channel
- **Power**: 3.3V only - do not use 5V (will damage the microphone)
- **I2S Protocol**: Uses standard I2S protocol with 24-bit data in 32-bit frames

### ESP32 I2S Notes
- **DMA Buffers**: I2S uses DMA for efficient data transfer
- **Sample Rates**: Supports 8kHz to 48kHz sample rates
- **Bit Depth**: Typically 16-bit or 24-bit samples
- **Core Assignment**: Audio processing should run on Core 1

### Signal Quality
- **Keep I2S wires short** (< 15cm) to minimize noise
- **Avoid running I2S wires parallel** to power wires
- **Add 0.1µF decoupling capacitor** near INMP441 VDD if experiencing noise
- **Ground plane**: Use solid ground connections for best audio quality

## Testing Connections

After wiring:

1. **Upload the INMP441 test firmware** to your ESP32:
   ```bash
   ./switch-firmware.sh inmp441-test
   pio run --target upload
   ```

2. **Open serial monitor** at 115200 baud:
   ```bash
   pio device monitor
   ```

3. **Check for successful I2S initialization**:
   - Look for "✅ I2S driver initialized successfully"
   - Watch for audio level readings when making sound

4. **Verify audio capture**:
   - Make noise near the microphone
   - Observe RMS and peak level changes
   - Check for "🔊 Good audio level" messages

## Troubleshooting

### No Audio Data
- **Check power connections**: Verify 3.3V to VDD and GND to GND
- **Verify I2S wiring**: Double-check SCK, WS, and SD connections  
- **Check channel select**: Ensure L/R is connected to GND for left channel

### Poor Audio Quality
- **Shorten I2S wires**: Keep connections under 15cm
- **Add decoupling capacitor**: 0.1µF from VDD to GND near INMP441
- **Check sample rate**: Ensure firmware uses appropriate sample rate (16kHz recommended)

### I2S Initialization Errors
- **GPIO conflicts**: Ensure chosen GPIOs are not used by other peripherals
- **Power issues**: Verify stable 3.3V supply under load
- **Driver conflicts**: Check that I2S driver is properly cleaned up on restart

### Audio Clipping
- **Reduce input volume**: Move microphone away from loud sources
- **Check bit depth**: Ensure proper 16-bit or 24-bit configuration
- **Verify L/R connection**: Incorrect channel selection can cause issues

## Alternative Pin Configurations

If you need to use different pins, modify these defines in the code:

```cpp
#define I2S_SCK_GPIO   26    // I2S Bit Clock
#define I2S_WS_GPIO    25    // I2S Word Select  
#define I2S_SD_GPIO    33    // I2S Serial Data
#define HEARTBEAT_GPIO 2     // Status LED (set to -1 to disable)
#define BOOT_BTN_GPIO  0     // Built-in button
```

**Recommended ESP32 I2S pins:**
- SCK: GPIO 26, 25, 27, 14
- WS: GPIO 25, 26, 27, 12  
- SD: GPIO 33, 32, 35, 34

Avoid using GPIOs 6-11 (connected to flash), GPIO 0 (boot), GPIO 2 (boot), and strapping pins during startup.

## Performance Considerations

### Sample Rate Selection
- **8kHz**: Voice/telephony applications (G.711 encoding)
- **16kHz**: Wideband voice, good quality/bandwidth balance 
- **44.1kHz**: CD quality, high bandwidth requirements

### Buffer Sizing
- **Smaller buffers**: Lower latency, higher CPU usage
- **Larger buffers**: Higher latency, more stable audio stream
- **Recommended**: 4 buffers × 512-1024 samples for real-time streaming

### CPU Core Usage
- **Core 0**: WiFi, network stack, web server
- **Core 1**: Audio capture, processing, encoding (recommended)
- Use `xTaskCreatePinnedToCore()` for deterministic audio tasks
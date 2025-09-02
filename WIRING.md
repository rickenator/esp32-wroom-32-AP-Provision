# ESP32-WROOM-32 Wiring Guide

This document shows the pin-to-pin connections for the ESP32 home monitoring device with KY-038 sound sensor module.

## Hardware Components

- **ESP32-WROOM-32 DevKitC** (or compatible ESP32 development board)
- **KY-038 Sound Sensor Module** (LM393 comparator + microphone)
- **Optional: LED** (for heartbeat indicator)
- **Power Supply**: 3.3V (ESP32) and appropriate power for KY-038

## Pin Assignments

| Component | ESP32 Pin | GPIO | Purpose |
|-----------|-----------|------|---------|
| KY-038 D0 | GPIO 27 | 27 | Digital sound detection (threshold) |
| KY-038 A0 | GPIO 34 | 34 | Analog audio input (ADC) |
| BOOT Button | GPIO 0 | 0 | Built-in button for device management |
| Heartbeat LED | GPIO 2 | 2 | Optional status LED (set to -1 to disable) |

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
|   GPIO 27 [D0]   <--|-- KY-038 D0 (Digital)
|   GPIO 34 [A0]   <--|-- KY-038 A0 (Analog)
|                     |
|   3.3V [PWR]     <--|-- KY-038 VCC
|   GND  [GND]     <--|-- KY-038 GND
|                     |
+---------------------+
```

### KY-038 Sound Sensor Module
```
KY-038 Module
+---------------------+
|                     |
|   Microphone        |
|   [MIC]             |
|                     |
|   Potentiometer     |
|   [POT]  (Adjust    |
|          sensitivity)|
|                     |
|   D0 [OUT] ------>  |-- ESP32 GPIO 27
|   A0 [OUT] ------>  |-- ESP32 GPIO 34
|   GND [GND] ------> |-- ESP32 GND
|   VCC [VCC] ------> |-- ESP32 3.3V
|   +   -   (Power LED)|
|                     |
+---------------------+
```

## Detailed Connections

### Power Connections
```
ESP32 DevKitC          KY-038 Module
-------------          -------------
3.3V (red)    ------>  VCC (red)
GND  (black)  ------>  GND (black)
```

### Signal Connections
```
ESP32 DevKitC          KY-038 Module
-------------          -------------
GPIO 27 (yellow) ----> D0 (yellow)    // Digital threshold detection
GPIO 34 (blue)   ----> A0 (blue)      // Analog audio input
```

### Optional Connections
```
ESP32 DevKitC          LED (optional)
-------------          -------------
GPIO 2 (green)  ---->  Anode (+)      // Heartbeat indicator
GND            ---->  Cathode (-)    // via 220Ω resistor
```

## Connection Summary Table

| ESP32 Pin | Wire Color | KY-038 Pin | Purpose |
|-----------|------------|------------|---------|
| 3.3V | Red | VCC | Power supply (3.3V) |
| GND | Black | GND | Ground |
| GPIO 27 | Yellow | D0 | Digital sound detection |
| GPIO 34 | Blue | A0 | Analog audio input |
| GPIO 2 | Green | - | Optional heartbeat LED |
| GPIO 0 | - | - | Built-in BOOT button |

## Setup Steps

1. **Power off** both the ESP32 and KY-038 module
2. **Connect power wires** first:
   - ESP32 3.3V → KY-038 VCC
   - ESP32 GND → KY-038 GND
3. **Connect signal wires**:
   - ESP32 GPIO 27 → KY-038 D0
   - ESP32 GPIO 34 → KY-038 A0
4. **Optional: Connect LED**:
   - ESP32 GPIO 2 → LED anode (with 220Ω resistor)
   - ESP32 GND → LED cathode
5. **Power on** the ESP32
6. **Adjust KY-038 sensitivity**:
   - Use the potentiometer on the KY-038 module
   - Turn clockwise to increase sensitivity
   - Turn counterclockwise to decrease sensitivity

## Important Notes

- **Power**: Ensure you're using 3.3V, not 5V (ESP32 is not 5V tolerant on GPIO pins)
- **KY-038 Module**: The module has an onboard LM393 comparator and microphone
- **Sensitivity Adjustment**: The potentiometer controls the digital threshold for D0 output
- **LED Resistor**: Use a 220Ω resistor with the heartbeat LED to prevent damage
- **BOOT Button**: GPIO 0 is connected to the built-in BOOT button on most ESP32 DevKitC boards
- **Pin Changes**: If you need to use different pins, update the `#define` statements in the code

## Testing Connections

After wiring:

1. **Upload the firmware** to your ESP32
2. **Open serial monitor** at 115200 baud
3. **Type `help`** to see available commands
4. **Type `sound`** to check sound sensor status
5. **Make noise** near the microphone to test detection

## Troubleshooting

- **No sound detection**: Check KY-038 potentiometer adjustment
- **Serial not working**: Verify USB connection and correct COM port
- **WiFi issues**: Check antenna connection and credentials
- **LED not working**: Verify GPIO 2 connection and resistor value

## Alternative Pin Configurations

If you need to use different pins, modify these `#define` statements in the code:

```cpp
#define SOUND_DO_GPIO   15    // Digital sound detection
#define SOUND_A0_GPIO   34    // Analog audio input
#define HEARTBEAT_GPIO  2     // Status LED (set to -1 to disable)
#define BOOT_BTN_GPIO   0     // Built-in button
```

Make sure the pins you choose are available and compatible with the ESP32's capabilities (ADC for analog input, etc.).

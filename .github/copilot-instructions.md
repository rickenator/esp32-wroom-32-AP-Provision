# ESP32 Home Monitoring Device - AI Coding Instructions

## Project Overview

This ESP32-WROOM-32 project implements a **dog bark detection device** with WiFi provisioning, MQTT/webhook notifications, and planned WebRTC audio streaming. The core architecture combines:

- **Captive portal provisioning** with web UI for WiFi setup
- **KY-038 sound sensor** (digital threshold + analog ADC sampling)  
- **Real-time sound detection** with configurable debouncing
- **MQTT/webhook notifications** with structured JSON payloads
- **ADC audio recording** for future WebRTC streaming

## Critical Architecture Knowledge

### Dual-Mode Sensor Design
The KY-038 module provides **two detection paths**:
- **D0 (GPIO 27)**: Hardware comparator with potentiometer threshold - used for real-time bark detection
- **A0 (GPIO 34)**: Raw analog audio signal - used for ADC recording and RMS/peak analysis

### System Stability Foundation
Performance-critical settings applied in `setup()`:
```cpp
WiFi.setSleep(false);                 // Disable power save for consistent timing
esp_wifi_set_ps(WIFI_PS_NONE);       // Modem sleep off
setCpuFrequencyMhz(240);              // Lock CPU to prevent timing jitter
analogSetPinAttenuation(SOUND_A0_GPIO, ADC_11db);  // 0-3.3V range
```

### NVS Configuration Schema
```
net: { ssid, pass, mqtt_host, mqtt_port, mqtt_user, mqtt_pass, topic_base, webhook_url, webhook_secret }
sound: { mqttEn, mqttSrv, mqttTopic, wbEn, wbUrl, tMinMs, tQuietMs, levelThresh }
```

### Detection Event Pipeline
1. **Hardware debouncing**: 50ms settle time on D0 state changes
2. **Duration filtering**: Events must exceed `T_min_ms` (100ms default)
3. **Queue-based notifications**: FreeRTOS queue + dedicated task for MQTT/webhook
4. **Structured JSON payloads**: ISO timestamps, sequence numbers, duration/RMS/peak metrics

## Development Workflows

### PlatformIO Build System
```bash
# Install dependencies and build
pio pkg install && pio run

# Upload firmware 
pio run --target upload

# Monitor serial output (115200 baud)
pio device monitor
```

### Firmware Switching System
Use `switch-firmware.sh` to toggle between main and test firmware:
```bash
./switch-firmware.sh test   # Switch to KY-038 hardware test
./switch-firmware.sh main   # Switch to full dog detection firmware
```

### Serial Console Commands
Connect at 115200 baud for runtime control:
- `help` - List all commands
- `status` - Network diagnostics  
- `sound` - Sound sensor status and count
- `record <ms>` - Start ADC recording burst
- `reprov` - Force provisioning mode
- `flush-nvs` - Factory reset (dangerous)

### Button Controls (GPIO 0)
- **Short press** (< 500ms): Start provisioning AP
- **Long press** (500ms-3s): Clear network + start provisioning  
- **Very long press** (3s-6s): Full NVS reset + reboot

## Project-Specific Patterns

### Enhanced Sound Detection Logic
Current implementation uses **stateful event tracking**:
```cpp
// State machine: idle -> potential event -> validated event -> quiet period
if (detected && !soundEventActive) {
    soundEventStart = now;
    soundEventActive = true;
} else if (!detected && soundEventActive) {
    uint32_t eventDuration = now - soundEventStart;
    if (eventDuration >= soundTMinMs) {
        // Valid bark detected - queue notification
    }
}
```

### WiFi Connection Resilience
Smart reconnection with user interaction preservation:
```cpp
// Allow serial/button input during connection attempts
while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    delay(100);
    pollSerial();    // Process commands during connection
    handleButton();  // Handle provisioning requests
}
```

### Notification Architecture
**Producer-consumer pattern** with FreeRTOS:
- Main loop detects events → pushes to queue
- Dedicated notification task → processes MQTT/webhook
- Prevents blocking on network I/O during detection

## Current Implementation Phase

**Phase 1**: Enhanced digital detection + notifications (75% complete)
- ✅ Basic D0 detection with debouncing
- ✅ Configurable timing parameters  
- ✅ MQTT/webhook integration
- ✅ NTP timestamp synchronization
- 🔄 Calibration UI (partially implemented)

**Phase 2**: ADC optimization for WebRTC (planned)
- Target: I2S-ADC DMA at 8kHz, 20ms frames
- Ring buffer ≥120ms capacity on core 1
- Test endpoints: `/pcm?ms=200`, `/levels`

## Key Integration Points

### Captive Portal Routes
- `/` - Main provisioning page
- `/calibrate` - Sound sensor configuration with live monitoring
- `/sound` - JSON API for real-time detection status  
- `/samples` - Base64 PCM data for audio validation
- `/diag` - System diagnostics and sensor status

### JSON Notification Schema
```json
{
  "ts": "2025-10-16T18:12:45Z",    // NTP-synced ISO timestamp
  "seq": 1234,                     // Sequence number for deduplication
  "duration_ms": 420,              // Event duration
  "rms": 812,                      // ADC RMS level during event
  "peak": 2014,                    // ADC peak level
  "do_edges": 1,                   // D0 transitions count
  "fw": "0.2.0",                   // Firmware version
  "id": "AA:BB:CC:DD:EE:FF"       // Device MAC address
}
```

### Hardware Configuration Notes
- **GPIO 15 bootstrap concern**: D0 currently on GPIO 27 to avoid boot issues
- **ADC anti-aliasing**: Plan for 1kΩ + 39nF RC filter at A0 pin
- **Power stability**: 0.1µF decoupling capacitor on KY-038 VCC recommended
- **LED heartbeat**: Solid when WiFi connected, blinks when disconnected

## Performance Targets & Constraints

### Phase 1 Success Criteria
- Event detection latency ≤150ms p95 (D0 edge → network notification)
- False positives <1/hour in quiet room
- Cold boot success 20/20 cycles with sensor attached

### Memory & Timing Constraints  
- ADC recorder: 2000ms max @ 8kHz = 16KB sample buffer
- Notification queue: 8 events max to prevent memory overflow
- Timing critical: CPU locked to 240MHz for consistent D0 sampling

## Common Pitfalls & Solutions

### WiFi Power Save Interference
**Problem**: Default power save causes timing jitter during sound detection
**Solution**: Explicitly disable in setup: `WiFi.setSleep(false); esp_wifi_set_ps(WIFI_PS_NONE);`

### GPIO Bootstrap Issues
**Problem**: KY-038 D0 could interfere with ESP32 boot if wired to GPIO 15
**Solution**: Use GPIO 27 for D0, validate boot safety with sensor connected

### NVS Namespace Confusion
**Pattern**: Use `net` for network credentials, `sound` for sensor configuration
**Critical**: `flush-nvs` erases ALL namespaces - document as dangerous operation

When modifying this codebase, always consider the real-time detection requirements and maintain the dual-mode sensor architecture that enables both immediate notifications and future audio streaming capabilities.
# Quick Start Guide - Secure WebRTC Dog Bark Detector

Get your ESP32-S3 dog bark detector running in 10 minutes!

## 📦 What You Need

- ESP32-S3-DevKitC-1 board (with N16R8 module)
- INMP441 digital microphone
- 5 jumper wires
- Micro-USB cable
- Computer with PlatformIO installed

## 🔌 Step 1: Wiring (5 minutes)

Connect INMP441 to ESP32-S3:

```
INMP441          ESP32-S3
-------          --------
VDD       →      3.3V
GND       →      GND
SD        →      GPIO 40
WS        →      GPIO 41
SCK       →      GPIO 42
L/R       →      GND
```

**⚠️ Important:** Connect VDD to 3.3V, NOT 5V!

## 💾 Step 2: Build & Upload (3 minutes)

```bash
# Navigate to project
cd esp32-wroom-32-AP-Provision

# Install dependencies (first time only)
pio pkg install

# Build and upload
pio run -e esp32s3-secure-webrtc-bark --target upload

# Open serial monitor
pio device monitor -e esp32s3-secure-webrtc-bark
```

## 🔐 Step 3: Get Admin Credentials (30 seconds)

Watch the serial monitor for initial credentials:

```
=== INITIAL ADMIN CREDENTIALS ===
Username: admin
Password: abcdefghijklmnop
=== CHANGE PASSWORD IMMEDIATELY ===
```

**💾 SAVE THESE CREDENTIALS!**

## 📡 Step 4: Configure WiFi (1 minute)

### Option A: Hardcode (Quick)

Before uploading, edit these lines in the source:

```cpp
// In setup() function, replace:
String ssid = prefs.getString("ssid", "");
String password = prefs.getString("password", "");

// With your WiFi credentials:
String ssid = "YourWiFiName";
String password = "YourWiFiPassword";
```

### Option B: Provisioning (More flexible)

Use serial console commands (future feature) or NVS Flash tool.

## 🎉 Step 5: Test It! (30 seconds)

### Get Device IP

From serial monitor:
```
Connected! IP: 192.168.1.100
```

### Test Public Endpoint

```bash
curl -k https://192.168.1.100/api/status
```

Expected response:
```json
{
  "device_id": "AA:BB:CC:DD:EE:FF",
  "firmware": "2.0.0-unified",
  "uptime_ms": 12345,
  "bark_count": 0,
  "mqtt_connected": false
}
```

### Login

```bash
curl -k -X POST https://192.168.1.100/api/login \
  -d "username=admin" \
  -d "password=abcdefghijklmnop"
```

Save the JWT token from response!

### Start Streaming

```bash
TOKEN="your_jwt_token_here"

curl -k -X POST https://192.168.1.100/api/start-stream \
  -H "Authorization: Bearer $TOKEN" \
  -d "bark_alerts_only=false"
```

### Check Bark Status

```bash
curl -k https://192.168.1.100/api/bark-status \
  -H "Authorization: Bearer $TOKEN"
```

## 🐕 Detecting Your First Bark

1. Make sure microphone is connected properly
2. Check serial monitor shows audio levels
3. Play dog bark sound or let your dog bark
4. Watch serial monitor for detection:

```
🐕 BARK DETECTED #1!
   Confidence: 92.00%
   Duration: 450ms
   RMS Level: 0.65
   Peak Level: 0.89
```

## 🔍 Troubleshooting

### No WiFi Connection
- Check SSID and password
- Ensure 2.4GHz WiFi (ESP32-S3 doesn't support 5GHz)
- Move closer to router

### No Bark Detection
- Verify INMP441 wiring (especially VDD to 3.3V!)
- Check microphone orientation (hole facing sound source)
- Increase volume or move closer
- Check serial monitor for audio levels

### Cannot Access API
- Verify device IP from serial monitor
- Check firewall allows HTTPS (port 443)
- Use `-k` flag with curl for self-signed certificate

## 📱 Next Steps

### Set Up MQTT Alerts

See [MQTT Integration Guide](SECURE_WEBRTC_DOGBARK_README.md#mqtt-integration)

### Configure Android App

See [Android Integration Example](SECURE_WEBRTC_DOGBARK_README.md#android-integration-example)

### Adjust Detection Sensitivity

```bash
curl -k -X POST https://192.168.1.100/api/bark-config \
  -H "Authorization: Bearer $TOKEN" \
  -d "bark_threshold=0.75" \
  -d "min_duration_ms=250"
```

### Enable Bark-Triggered Streaming

Only stream audio for 5 seconds after bark detection:

```bash
curl -k -X POST https://192.168.1.100/api/start-stream \
  -H "Authorization: Bearer $TOKEN" \
  -d "bark_alerts_only=true"
```

## 📚 Full Documentation

For complete details, see [SECURE_WEBRTC_DOGBARK_README.md](SECURE_WEBRTC_DOGBARK_README.md)

## 🆘 Need Help?

- Check [Troubleshooting Guide](SECURE_WEBRTC_DOGBARK_README.md#troubleshooting)
- Review serial monitor logs
- Report issues on [GitHub](https://github.com/rickenator/esp32-wroom-32-AP-Provision/issues)

---

**Happy bark detecting! 🐕🎤**

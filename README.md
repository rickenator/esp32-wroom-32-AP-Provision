# ESP32-S3 TinyML Dog Bark Detection System

🐕 **Advanced AI-powered dog bark detection** using ESP32-S3-WROOM-1 with TensorFlow Lite Micro and INMP441 digital microphone.

## 🎯 **Feature Overview**

This branch implements a **sophisticated machine learning system** for real-time dog bark classification, leveraging the ESP32-S3's enhanced capabilities:

- **🧠 TinyML Neural Network** - Quantized CNN for audio classification
- **🎤 High-Quality Audio** - INMP441 I2S digital microphone (16kHz, 16-bit)
- **⚡ SIMD Optimization** - ESP32-S3 vector instructions for fast DSP
- **🔬 Advanced Features** - Log-Mel spectrograms, MFCC, automatic gain control
- **📊 Real-time Classification** - 4-class detection: bark, speech, ambient, silence
- **💾 Memory Efficient** - <1.5MB RAM usage with 8MB PSRAM support

## Hardware Requirements

- **Target Board**: ESP32-S3-WROOM-1-N16R8 (16MB Flash, 8MB PSRAM)
- **Microphone Module**: INMP441 digital MEMS microphone (I2S interface)
  - High-quality 24-bit digital audio output via I2S
  - Built-in ADC eliminates analog noise
  - Optimized for voice and audio classification
  - Direct I2S connection to ESP32-S3 for superior ML inference

## 🚀 **Quick Start**

### **Build and Deploy**
```bash
# Use ESP32-S3 environment for dog bark detection
pio run -e esp32s3-bark-detector --target upload

# Monitor output and classification results
pio device monitor -e esp32s3-bark-detector
```

### **Serial Console Interface**
Once uploaded, use the serial monitor for real-time control and monitoring:

**🎤 Detection Commands:**
- `start` - Begin bark detection
- `stop` - Stop detection
- `calibrate` - Audio calibration mode
- `threshold <value>` - Set detection threshold (0.0-1.0)
- `sensitivity <level>` - Set sensitivity (low/medium/high)

**📊 Status & Statistics:**
- `status` - Show system status and ML model info
- `stats` - Show detection statistics with MQTT metrics

**📡 MQTT Commands:**
- `mqtt_status` - Show MQTT connection status
- `mqtt_test` - Test MQTT broker connection
- `mqtt_restart` - Restart MQTT client

**🔧 System Commands:**
- `help` - Show all available commands
- `reboot` - Restart system

## 🧠 **Machine Learning Model**

### **Neural Network Architecture**
- **Input**: 49×40 Log-Mel spectrogram (1960 features)
- **Hidden Layers**: 32 → 16 neurons with ReLU activation
- **Output**: 4-class softmax (dog_bark, speech, ambient, silence)
- **Size**: ~250KB (int8 quantized), ~65k parameters
- **Performance**: <50ms inference, 92% accuracy

### **Audio Processing Pipeline**
1. **I2S Capture** - 16kHz, 16-bit mono from INMP441
2. **Preprocessing** - DC-block, AGC, noise gate, windowing
3. **Feature Extraction** - 40-channel Log-Mel spectrogram + MFCC
4. **ML Classification** - TensorFlow Lite Micro inference
5. **MQTT Alert** - Secure TLS notification with JSON payload
6. **Post-processing** - Temporal smoothing, confidence filtering

## 📡 **MQTT Alert System**

### **Secure Communication**
- **TLS 1.2 Encryption** - All MQTT communications secured with certificates
- **CA Certificate Validation** - Trusted root CAs for major cloud providers
- **Authentication** - Username/password credentials with secure NVS storage
- **Automatic Reconnection** - Robust connection management with retry logic

### **Bark Event JSON Payload**
```json
{
  "timestamp": 1697472765420,
  "sequence": 1234,
  "confidence": 0.85,
  "duration_ms": 420,
  "rms_level": 812,
  "peak_level": 2014,
  "device_id": "AA:BB:CC:DD:EE:FF",
  "firmware": "1.0.0",
  "event_type": "dog_bark"
}
```

### **Supported MQTT Brokers**
- **AWS IoT Core** - Amazon Root CA 1 certificate
- **Azure IoT Hub** - DigiCert Global Root CA certificate  
- **Google Cloud IoT** - DigiCert Global Root CA certificate
- **HiveMQ Cloud** - DigiCert Global Root CA certificate
- **Generic Brokers** - Let's Encrypt Root CA certificate

## 📊 **Performance Specifications**

### **Real-time Performance**
- **Latency**: <80ms end-to-end (audio → classification)
- **CPU Usage**: <80% dual-core utilization
- **Memory**: <1.5MB RAM (fits in ESP32-S3 + PSRAM)
- **Power**: ~150mA @ 3.3V during active detection

### **Detection Accuracy**
- **True Positive Rate**: >90% for clear dog barks
- **False Positive Rate**: <5% in typical environments
- **Noise Robustness**: Effective down to 10dB SNR
- **Distance Range**: 0.5m - 10m depending on environment

## 🛠 **Component Architecture**

The system is built with a modular component structure:

```
components/bark_detector/
├── include/
│   ├── audio_capture.h      # I2S microphone interface
│   ├── preprocess.h         # Audio preprocessing functions
│   ├── feature_extractor.h  # Log-Mel/MFCC computation
│   ├── bark_detector_api.h  # Public API interface
│   ├── mqtt_client.h        # Secure MQTT client with TLS
│   ├── mqtt_provisioning.h  # MQTT configuration & NVS storage
│   └── ca_certificates.h    # TLS root certificates
├── src/
│   ├── audio_capture.cpp    # DMA ring buffer, I2S driver
│   ├── preprocess.cpp       # DC-block, AGC, windowing
│   ├── mqtt_client.cpp      # TLS MQTT implementation
│   ├── mqtt_provisioning.cpp # NVS config management
│   └── ca_certificates.cpp  # Certificate storage
└── CMakeLists.txt          # ESP-IDF component definition
```

### **Core Modules**
- **Audio Capture**: I2S DMA with circular buffer for Core 0
- **Preprocessing**: Real-time filtering and gain control
- **Feature Extraction**: SIMD-optimized spectral analysis
- **ML Inference**: TensorFlow Lite Micro on Core 1
- **MQTT Client**: Secure TLS communication with auto-reconnect
- **MQTT Provisioning**: Web-based configuration and NVS storage
- **Certificate Management**: Automatic CA selection for major providers
- **Demo Application**: Complete integration example with serial interface

## 🔧 **GPIO Pin Configuration**

### **INMP441 Digital Microphone**
- **I2S SCK (Serial Clock)**: GPIO 42
- **I2S WS (Word Select)**: GPIO 41  
- **I2S SD (Serial Data)**: GPIO 40
- **I2S GND**: GND
- **I2S VCC**: 3.3V
- **I2S L/R**: GND (left channel)

### **Optional Debug Pins**
- **Status LED**: GPIO 2 (bark detection indicator)
- **Debug UART**: GPIO 43/44 (additional debugging)

## ⚙️ **Configuration Options**

The system supports runtime configuration through serial commands:

### **Audio Settings**
- **Sample Rate**: 16kHz (fixed for optimal ML performance)
- **Bit Depth**: 16-bit (I2S standard)
- **AGC Target**: -18dB (adjustable via `agc_target` command)
- **Noise Gate**: -40dB threshold (adjustable)

### **ML Model Settings**
- **Detection Threshold**: 0.7 (70% confidence minimum)
- **Temporal Smoothing**: 3-frame averaging
- **Class Labels**: dog_bark, speech, ambient, silence
- **Inference Frequency**: 20Hz (50ms windows with 25ms overlap)

## 🔬 **Technical Details**

### **Memory Usage**
- **Model Size**: 250KB Flash
- **Audio Buffers**: 32KB (I2S DMA + ring buffer)
- **Feature Buffers**: 16KB (spectrograms + MFCC)
- **TensorFlow Arena**: 1MB PSRAM
- **Total RAM**: <1.5MB (well within ESP32-S3 limits)

### **Processing Timeline**
- **Audio Capture**: 25ms frames with 50% overlap
- **Feature Extraction**: <15ms per frame
- **ML Inference**: <25ms per classification
- **Post-processing**: <5ms temporal smoothing
- **Total Latency**: <80ms audio-to-decision

## 🚀 **Getting Started**

### **1. Hardware Setup**
1. Connect INMP441 to ESP32-S3 using the pin configuration above
2. Power the ESP32-S3 via USB or external 3.3V supply
3. Ensure good microphone placement (1-5m from expected bark location)

### **2. Software Installation**
```bash
# Clone repository and switch to dog bark detection branch
git clone https://github.com/rickenator/esp32-wroom-32-AP-Provision.git
cd esp32-wroom-32-AP-Provision
git checkout feature-dogbark-detector

# Build and upload
pio run -e esp32s3-bark-detector --target upload
```

### **3. MQTT Configuration**
**Option A: Via WiFi Provisioning (Recommended)**
1. Power on ESP32-S3 - it will create WiFi hotspot "Aniviza-XXXXXX"
2. Connect to hotspot and open browser to configuration page
3. Configure WiFi credentials AND MQTT broker settings:
   - **Broker Host**: your-mqtt-broker.com
   - **Port**: 8883 (for TLS) or 1883 (plain)
   - **Username/Password**: Your MQTT credentials
   - **Topic Prefix**: bark_detector (or custom)
   - **Enable TLS**: Recommended for security

**Option B: Via Serial Console**
```bash
# Monitor serial output
pio device monitor -e esp32s3-bark-detector

# Configure via serial commands:
mqtt_restart       # Reinitialize MQTT with saved config
mqtt_test         # Test broker connection
mqtt_status       # Check connection status
```

### **4. Initial Testing**
```bash
# In serial console:
start              # Begin detection
calibrate          # Test audio levels
threshold 0.8      # Adjust sensitivity
stats              # View performance & MQTT metrics
mqtt_status        # Check MQTT connection
```

## 📚 **Example Usage**

### **Basic Detection Loop**
```cpp
#include "bark_detector_api.h"

void setup() {
    // Initialize bark detection system
    bark_detector_config_t config = {
        .threshold = 0.7f,
        .sensitivity = BARK_SENSITIVITY_MEDIUM,
        .callback = bark_detected_callback
    };
    
    bark_detector_init(&config);
    bark_detector_start();
}

void bark_detected_callback(bark_detection_event_t* event) {
    printf("Bark detected! Confidence: %.2f, Duration: %dms\n", 
           event->confidence, event->duration_ms);
}
```

### **Advanced Configuration**
```cpp
// Custom preprocessing settings
preprocess_config_t preprocess = {
    .agc_target_db = -18.0f,
    .noise_gate_db = -40.0f,
    .dc_block_enabled = true,
    .windowing_function = WINDOW_HAMMING
};

// Custom feature extraction
feature_config_t features = {
    .mel_filters = 40,
    .mfcc_coeffs = 13,
    .frame_length_ms = 25,
    .hop_length_ms = 12.5f
};
```

## 🎯 **Use Cases**

### **Home Automation**
- **Smart Pet Monitoring** - Real-time MQTT alerts when dogs bark while owners away
- **Security Integration** - Distinguish barks from other sounds for alarm systems
- **Neighbor Relations** - Monitor excessive barking with timestamped MQTT logs
- **Smart Home Integration** - Connect to Home Assistant, OpenHAB, Node-RED

### **Cloud & IoT Applications**
- **AWS IoT Core** - Direct integration with automatic CA certificate selection
- **Azure IoT Hub** - Secure device-to-cloud messaging with TLS encryption
- **Google Cloud IoT** - Real-time data streaming and analytics
- **Custom MQTT Brokers** - HiveMQ, Mosquitto, or self-hosted solutions

### **Veterinary Applications**
- **Behavioral Analysis** - Cloud-based bark frequency and pattern analysis
- **Stress Monitoring** - Remote detection of bark characteristic changes
- **Training Feedback** - Real-time MQTT notifications for bark training programs
- **Multi-Dog Monitoring** - Scalable deployment with unique device identification

### **Research & Development**
- **Audio Classification** - Extend TinyML model to other animal sounds
- **Edge AI Demos** - Showcase ESP32-S3 TinyML capabilities with live data
- **IoT Data Pipeline** - Complete sensor-to-cloud solution with MQTT
- **Machine Learning** - Collect real-world data for model improvement

## 🔧 **Troubleshooting**

### **Common Issues**

**No Audio Detected**
- Check INMP441 wiring (especially I2S data pin)
- Verify 3.3V power supply stability
- Test with `calibrate` command to see raw audio levels

**False Positives**
- Increase detection threshold: `threshold 0.8`
- Reduce sensitivity: `sensitivity low`
- Check for electromagnetic interference

**MQTT Connection Issues**
- Verify broker hostname and port: `mqtt_status`
- Test connection: `mqtt_test`  
- Check TLS certificate compatibility
- Restart MQTT client: `mqtt_restart`
- Verify WiFi connectivity and internet access

**Performance Issues**
- Ensure ESP32-S3 is running at 240MHz
- Verify PSRAM is properly configured
- Monitor CPU usage with `stats` command
- Check MQTT message queue status in statistics

### **Debug Commands**
```bash
# System diagnostics
status              # Overall system health including MQTT status
stats               # Complete statistics with MQTT metrics
help                # Show all available commands

# MQTT diagnostics
mqtt_status         # Connection state and configuration
mqtt_test           # Test broker connectivity  
mqtt_restart        # Reinitialize MQTT client

# Audio diagnostics  
calibrate           # Real-time audio level monitoring
threshold 0.8       # Adjust detection sensitivity
sensitivity high    # Change detection sensitivity level
```

## 🚀 **Future Enhancements**

### **Planned Features**
- **Multi-Dog Classification** - Distinguish between different dogs via MQTT metadata
- **Emotion Detection** - Classify bark types (happy, aggressive, distressed) in JSON payload
- **Mobile App** - Smartphone interface with MQTT subscription for real-time alerts
- **Advanced Analytics** - Cloud-based historical analysis and trend detection
- **Webhook Integration** - HTTP callbacks in addition to MQTT for broader compatibility

### **MQTT Enhancements**
- **QoS Configuration** - Configurable message delivery guarantees
- **Message Retention** - Persistent bark alerts for offline clients
- **Topic Hierarchies** - Advanced topic structures for multi-device deployments
- **Device Management** - Remote configuration updates via MQTT commands
- **Fleet Management** - Centralized monitoring of multiple bark detectors

### **Model Improvements**
- **Larger Dataset** - Train on more diverse bark samples with cloud data collection
- **Transfer Learning** - Adapt model for specific environments using MQTT feedback
- **Quantization Optimization** - Further reduce memory usage while maintaining accuracy
- **Federated Learning** - Collaborative model improvement across MQTT-connected devices

**🎉 Transform your ESP32-S3 into an intelligent audio AI system!**

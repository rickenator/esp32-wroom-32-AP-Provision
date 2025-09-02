# ESP32 Home Monitoring Device - TODO List

## 🎯 Project Overview
This project implements a home monitoring device with ESP32, KY-038 sound sensor, WiFi provisioning, and WebRTC audio streaming capabilities.

## 📋 Current Status
- ✅ **WiFi Provisioning**: Captive portal with web UI working
- ✅ **Basic Sound Detection**: KY-038 D0 digital detection implemented
- ✅ **Serial Console**: Command processing with timeout mechanism
- ✅ **PlatformIO Migration**: Code migrated from Arduino IDE
- ✅ **Documentation**: README.md, WIRING.md, and .gitignore created
- 🔄 **Audio ADC**: Basic recording implemented, needs optimization

## 🚀 Implementation Roadmap

### Phase 1: Enhanced Digital Detection & Notifications (HIGH PRIORITY)
**Status**: Partially implemented - basic detection working, notifications needed

#### 1.1 Improve Sound Detection
- [ ] **Implement debounced event counting** in firmware
- [ ] **Add configurable detection thresholds** (currently fixed)
- [ ] **Test D0 polarity variations** with `SOUND_DO_ACTIVE_HIGH` flag
- [ ] **Add detection event logging** with timestamps

#### 1.2 MQTT Integration
- [ ] **Add MQTT client support** (PubSubClient library)
- [ ] **Implement MQTT publish on detection** to configurable topic
- [ ] **Add MQTT server/topic configuration** in provisioning UI
- [ ] **Test MQTT message format** and payload schema
- [ ] **Handle MQTT connection failures** gracefully

#### 1.3 HTTP Webhook Support
- [ ] **Implement HTTP POST webhook** on sound detection
- [ ] **Add webhook URL configuration** in provisioning UI
- [ ] **Define JSON payload schema** for webhook notifications
- [ ] **Add retry logic** for failed webhook deliveries

#### 1.4 Provisioning UI Enhancements
- [ ] **Add sound calibration page** to captive portal
- [ ] **Implement live D0 state monitoring** with JavaScript polling
- [ ] **Add MQTT/webhook configuration fields** to web UI
- [ ] **Store configurations** in NVS `sound` namespace

### Phase 2: ADC Audio Recording Optimization (MEDIUM PRIORITY)
**Status**: Basic implementation exists, needs Review.md improvements

#### 2.1 Apply Review.md Recommendations
- [ ] **Make ADC settings explicit**:
  ```cpp
  analogReadResolution(12);
  analogSetPinAttenuation(SOUND_A0_GPIO, ADC_11db);
  ```
- [ ] **Disable WiFi power save**:
  ```cpp
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  ```
- [ ] **Add DC blocker + soft gain** (HPF implementation)
- [ ] **Consider moving DO from GPIO15** to GPIO27 (optional)

#### 2.2 Performance Optimizations
- [ ] **Lock CPU frequency** to prevent timing jitter:
  ```cpp
  esp_pm_config_t pm = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 240,
    .light_sleep_enable = false
  };
  esp_pm_configure(&pm);
  ```
- [ ] **Pin ADC task to core 1** to avoid WiFi contention
- [ ] **Implement deterministic sampling** with I2S-ADC DMA
- [ ] **Add hardware decoupling** (0.1µF capacitor on KY-038 VCC)

#### 2.3 Audio Processing Improvements
- [ ] **Replace busy-wait with timer-based sampling**
- [ ] **Implement circular buffer** for continuous capture
- [ ] **Add audio level monitoring** and statistics
- [ ] **Implement 20ms frame production** for WebRTC compatibility

### Phase 3: WebRTC Integration (LOW PRIORITY)
**Status**: Planned but not started

#### 3.1 G.711 Audio Encoding
- [ ] **Implement G.711 u-law/A-law encoder** for ESP32
- [ ] **Add RTP packetization** for encoded audio
- [ ] **Test encoder performance** and CPU usage
- [ ] **Optimize for real-time operation**

#### 3.2 WebRTC Transport
- [ ] **Implement WebRTC peer connection** or lightweight alternative
- [ ] **Add SDP/ICE negotiation** support
- [ ] **Create signaling mechanism** (WebSocket-based)
- [ ] **Handle network transport** with appropriate QoS

#### 3.3 Audio Streaming
- [ ] **Integrate with ADC recorder** for continuous streaming
- [ ] **Add audio buffering** for network jitter compensation
- [ ] **Implement stream controls** (start/stop/pause)
- [ ] **Add audio quality monitoring**

## 🔧 Immediate Action Items (Next Sprint)

### High Priority (This Week)
1. **Apply Review.md optimizations**:
   - ADC settings and WiFi power save
   - DC blocker implementation
   - CPU frequency locking

2. **Complete MQTT integration**:
   - Add PubSubClient dependency
   - Implement publish on detection
   - Add configuration UI

3. **Enhance provisioning UI**:
   - Add calibration page
   - Live monitoring interface
   - Configuration persistence

### Medium Priority (Next 2 Weeks)
1. **I2S-ADC DMA implementation** for stable sampling
2. **Circular buffer system** for continuous capture
3. **HTTP webhook support** with retry logic
4. **Comprehensive testing** of all features

### Low Priority (Future)
1. **WebRTC integration** planning and prototyping
2. **G.711 encoder** implementation
3. **Advanced audio processing** features

## 🧪 Testing & Validation

### Unit Tests Needed
- [ ] **Sound detection accuracy** testing
- [ ] **MQTT message delivery** verification
- [ ] **Webhook reliability** testing
- [ ] **ADC recording quality** validation
- [ ] **WiFi reconnection** robustness

### Integration Tests
- [ ] **End-to-end notification flow**
- [ ] **Provisioning UI functionality**
- [ ] **Audio streaming performance**
- [ ] **Power consumption** optimization

## 📚 Documentation Updates Needed

- [ ] **API documentation** for MQTT/webhook payloads
- [ ] **Configuration guide** for all settings
- [ ] **Troubleshooting guide** for common issues
- [ ] **Performance benchmarks** and limitations

## 🔍 Known Issues & Edge Cases

- **KY-038 D0 polarity variations** - need UI toggle
- **WiFi power save interference** - addressed in Review.md
- **ADC timing jitter** - needs deterministic sampling
- **Memory constraints** for WebRTC features
- **Network reliability** for notifications

## 🎯 Success Criteria

### Phase 1 Success
- [ ] Reliable sound detection with configurable sensitivity
- [ ] Working MQTT/webhook notifications
- [ ] Functional calibration UI
- [ ] Stable device operation

### Phase 2 Success
- [ ] Optimized ADC recording with <5% timing jitter
- [ ] Continuous audio capture capability
- [ ] Real-time audio processing
- [ ] Hardware-optimized performance

### Phase 3 Success
- [ ] WebRTC audio streaming working
- [ ] Low-latency audio transmission
- [ ] Browser-compatible audio reception
- [ ] Production-ready streaming solution

## 📅 Timeline Estimates

- **Phase 1**: 1-2 weeks (Digital detection + notifications)
- **Phase 2**: 2-3 weeks (ADC optimization + continuous capture)
- **Phase 3**: 3-4 weeks (WebRTC integration)
- **Testing & Polish**: 1 week
- **Documentation**: Ongoing

---

*Last updated: September 2, 2025*
*Next review: After Phase 1 completion*

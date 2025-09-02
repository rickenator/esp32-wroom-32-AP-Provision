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

#### 1.1 System Stability & Performance (MOVED FROM PHASE 2)
- [ ] **Disable WiFi power save** for consistent timing:
  ```cpp
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  ```
- [ ] **Lock CPU frequency** to prevent ISR jitter:
  ```cpp
  esp_pm_config_t pm = {
    .max_freq_mhz = 240,
    .min_freq_mhz = 240,
    .light_sleep_enable = false
  };
  esp_pm_configure(&pm);
  ```
- [ ] **Add NTP time sync** for accurate timestamps
- [ ] **Implement RC anti-alias filter** (1kΩ + 39nF) at ADC pin
- [ ] **Add 0.1µF decoupling capacitor** on KY-038 VCC
- [ ] **Test boot safety** with sensor attached (GPIO15 strap concerns)

#### 1.2 Enhanced Sound Detection
- [ ] **Implement advanced debouncing** with configurable parameters:
  - `T_min_ms`: Minimum event duration (100ms default)
  - `T_quiet_ms`: Quiet period to end event (300ms default)
  - `level_threshold`: ADC RMS threshold
- [ ] **Add event counting and statistics** (events/minute, RMS/peak tracking)
- [ ] **Test D0 polarity variations** with `SOUND_DO_ACTIVE_HIGH` flag
- [ ] **Add detection event logging** with NTP timestamps

#### 1.3 MQTT Integration
- [ ] **Add MQTT client support** (PubSubClient library with TLS)
- [ ] **Implement MQTT publish on detection** with QoS 1
- [ ] **Add LWT (Last Will & Testament)** for offline detection
- [ ] **Create topic structure**:
  - `home/esp32/<id>/status` (retained)
  - `home/esp32/<id>/sound/event`
  - `home/esp32/<id>/sound/level` (periodic)
- [ ] **Define JSON payload schema**:
  ```json
  {
    "ts":"2025-09-02T18:12:45Z",
    "seq": 1234,
    "duration_ms": 420,
    "rms": 812,
    "peak": 2014,
    "do_edges": 3,
    "fw":"0.2.0",
    "id":"esp32-xxxx"
  }
  ```

#### 1.4 HTTP Webhook Support
- [ ] **Implement HTTP POST webhook** with same JSON schema
- [ ] **Add HMAC signature** with webhook_secret
- [ ] **Implement retry/backoff logic** (3 attempts: 0s/2s/10s)
- [ ] **Add event queuing** for offline scenarios

#### 1.5 Provisioning UI Enhancements
- [ ] **Add sound calibration page** with live monitoring:
  - Real-time DO state display
  - ADC meters (RMS/peak over 200ms windows)
  - Sliders for threshold, T_min, T_quiet parameters
- [ ] **Add connectivity test buttons** ("Test MQTT", "Test Webhook")
- [ ] **Add time zone & NTP configuration**
- [ ] **Implement unique default passwords** for security
- [ ] **Store configurations** in NVS namespaces:
  ```
  sound: { do_active_high, t_min_ms, t_quiet_ms, level_threshold }
  net: { mqtt_host, mqtt_port, mqtt_user, mqtt_pass, topic_base, webhook_url, webhook_secret }
  sys: { timezone, ntp_server, device_name }
  ```

#### 1.6 Security & Operations
- [ ] **Disable captive portal** after WiFi provisioning
- [ ] **Add factory reset** via GPIO hold on boot
- [ ] **Implement provisioning security** measures

### Phase 2: ADC Audio Recording Optimization (MEDIUM PRIORITY)
**Status**: Basic implementation exists, needs Review.md improvements

### Phase 2: ADC Audio Recording Optimization (MEDIUM PRIORITY)
**Status**: Basic implementation exists, needs Review.md improvements

#### 2.1 I2S-ADC DMA Implementation (PRIMARY PATH)
- [ ] **Implement I2S-ADC DMA** for deterministic 8kHz sampling:
  ```cpp
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_ADC_BUILT_IN),
    .sample_rate = 8000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT,
    .communication_format = I2S_COMM_FORMAT_STAND_MSB,
    .dma_buf_count = 4,
    .dma_buf_len = 160,   // 20 ms @ 8 kHz
    .use_apll = false,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1
  };
  ```
- [ ] **Pin ADC capture task to core 1** (high priority):
  ```cpp
  xTaskCreatePinnedToCore(captureTask, "cap", 4096, nullptr,
                          configMAX_PRIORITIES-2, nullptr, 1);
  ```
- [ ] **Implement producer-consumer ring buffer** (≥120ms capacity)
- [ ] **Replace busy-wait with DMA** for stable 20ms frames

#### 2.2 Audio Processing Pipeline
- [ ] **Add DC blocker + soft gain** (HPF implementation)
- [ ] **Implement 20ms frame production** for WebRTC compatibility
- [ ] **Add audio level monitoring** (RMS/peak statistics)
- [ ] **Create test endpoints**:
  - `/pcm?ms=200` → base64 PCM burst
  - `/levels` → JSON RMS/peak sliding window

#### 2.3 Performance Validation
- [ ] **Validate ADC jitter <5%** p95
- [ ] **Test buffer handling** under WiFi jitter (±20%)
- [ ] **Measure CPU usage** (<25% on core 1)
- [ ] **Verify 20ms frame timing** accuracy

### Phase 3: WebRTC Integration (LOW PRIORITY)
**Status**: Planned but not started

### Phase 3: WebRTC Integration (LOW PRIORITY)
**Status**: Planned but not started

#### 3.1 G.711 Audio Encoding
- [ ] **Implement G.711 A-law encoder** for 8kHz PCM
- [ ] **Add RTP packetization** for 20ms frames
- [ ] **Test encoder performance** and CPU usage (<30%)
- [ ] **Optimize for real-time operation** with zero underflows

#### 3.2 WebRTC Transport
- [ ] **Implement WebRTC peer connection** or lightweight alternative
- [ ] **Add SDP/ICE negotiation** support
- [ ] **Create signaling mechanism** (WebSocket-based)
- [ ] **Handle network transport** with DSCP/EF QoS:
  ```cpp
  int tos = 0xB8;  // EF (46)
  setsockopt(sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
  ```

#### 3.3 Audio Streaming Pipeline
- [ ] **Integrate with ADC recorder** for continuous 20ms frames
- [ ] **Add audio buffering** for network jitter compensation
- [ ] **Implement stream controls** (start/stop/pause)
- [ ] **Add audio quality monitoring** and statistics
- [ ] **Validate end-to-end latency** ≤250ms p95 on LAN

## 🔧 Immediate Action Items (Next Sprint)

### This Week (High Priority - Phase 1 Foundation)
1. **Apply system stability fixes**:
   - Disable WiFi power save (`WiFi.setSleep(false); esp_wifi_set_ps(WIFI_PS_NONE);`)
   - Lock CPU frequency (`esp_pm_configure(...)`)
   - Add NTP time sync
   - Install RC anti-alias filter (1kΩ + 39nF) + 0.1µF decoupling

2. **Test boot safety**:
   - Verify cold boot succeeds 20/20 with KY-038 connected
   - Consider moving DO from GPIO15 if issues persist

3. **Implement advanced debouncing**:
   - Add configurable T_min_ms, T_quiet_ms parameters
   - Implement event counting with RMS/peak tracking

4. **Complete MQTT integration**:
   - Add PubSubClient with TLS support
   - Implement QoS 1 publishing with LWT
   - Define topic structure and JSON schema

5. **Build calibration UI**:
   - Add live DO state + ADC meters
   - Implement parameter sliders
   - Add connectivity test buttons

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
- [ ] **Event detection latency ≤150ms p95** (DO edge → MQTT/webhook receive on LAN)
- [ ] **False positives <1/hour** in quiet room at default thresholds
- [ ] **Cold boot with sensor attached** passes 20/20 cycles
- [ ] **WiFi power save disabled** and CPU locked to 240MHz
- [ ] **NTP time sync** working with accurate timestamps
- [ ] **RC anti-alias filter** (1kΩ + 39nF) installed
- [ ] **MQTT/webhook notifications** working with defined JSON schema
- [ ] **Calibration UI** functional with live monitoring

### Phase 2 Success
- [ ] **ADC jitter <5% p95** (frame timestamps vs. schedule)
- [ ] **No buffer overrun** at ±20% WiFi jitter for ≥10 min runs
- [ ] **CPU usage <25%** on core 1 at 240MHz for capture+HPF
- [ ] **I2S-ADC DMA** producing stable 20ms frames
- [ ] **Ring buffer ≥120ms** capacity with producer on core 1
- [ ] **Test endpoints** working (/pcm, /levels)

### Phase 3 Success
- [ ] **Continuous 20ms frames** with G.711 A-law encoding
- [ ] **End-to-end audio latency ≤250ms p95** on LAN
- [ ] **Encoder CPU <30%** with zero underflows over 15 min
- [ ] **RTP transport** with DSCP/EF QoS settings
- [ ] **WebRTC signaling** functional

## 📅 Timeline Estimates

- **Phase 1**: 1-2 weeks (Digital detection + notifications)
- **Phase 2**: 2-3 weeks (ADC optimization + continuous capture)
- **Phase 3**: 3-4 weeks (WebRTC integration)
- **Testing & Polish**: 1 week
- **Documentation**: Ongoing

---

*Last updated: September 2, 2025*
*Next review: After Phase 1 completion*

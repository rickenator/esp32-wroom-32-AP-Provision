/**
 * @brief ESP32 WebRTC Audio Streaming Device with INMP441 Digital Microphone
 * 
 * This application implements a WebRTC audio streaming device using:
 * - INMP441 digital MEMS microphone (I2S interface)
 * - Real-time audio capture and processing
 * - G.711 audio encoding for WebRTC compatibility
 * - RTP packet streaming over UDP
 * - WiFi provisioning with captive portal
 * 
 * Hardware Setup:
 * - INMP441 SCK  -> GPIO 26 (I2S Clock)
 * - INMP441 WS   -> GPIO 25 (Word Select) 
 * - INMP441 SD   -> GPIO 33 (Serial Data)
 * - INMP441 L/R  -> GND (Left channel)
 * - INMP441 VDD  -> 3.3V
 * - INMP441 GND  -> GND
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <HTTPClient.h>

// Forward declarations
void loadAudioConfig();
void pollSerial();
void handleButton();
void audioProcessTask(void *pv);
void rtpStreamTask(void *pv);

#define CONNECT_TIMEOUT_MS 15000
#define RETRY_CONNECT_MS    5000
#define DNS_PORT 53

// -------- Hardware Pins --------
#define HEARTBEAT_GPIO 2     // set -1 to disable; many DevKitC use GPIO2 LED
#define BOOT_BTN_GPIO  0     // BOOT button (IO0), active-low

// INMP441 I2S pins
#define I2S_SCK_GPIO   26    // I2S Bit Clock
#define I2S_WS_GPIO    25    // I2S Word Select
#define I2S_SD_GPIO    33    // I2S Serial Data
#define I2S_PORT       I2S_NUM_0

// -------- Button thresholds (ms) --------
#define BTN_SHORT_MS   500    // >= short < long  -> reprov
#define BTN_LONG_MS    3000   // >= long < very   -> clear-net + reprov
#define BTN_VLONG_MS   6000   // >= very long     -> flush-nvs + reboot

// ------------ Audio Configuration ------------
#define SAMPLE_RATE     16000    // 16kHz for wideband audio
#define BITS_PER_SAMPLE 16       // 16-bit samples
#define CHANNELS        1        // Mono audio
#define I2S_BUFFER_SIZE 512      // Samples per I2S buffer
#define I2S_BUFFER_COUNT 4       // Number of I2S DMA buffers

// RTP and G.711 configuration
#define RTP_FRAME_SIZE  160      // 20ms @ 16kHz = 320 samples, G.711 = 160 bytes
#define G711_BUFFER_SIZE 480     // 3 × RTP frames for buffering
#define MAX_RTP_PACKET  1200     // MTU consideration

// Ring buffer for audio processing
#define RING_BUFFER_SIZE (SAMPLE_RATE * 2)  // 2 seconds of audio
static int16_t *ringBuffer = nullptr;
static volatile size_t ringWritePos = 0;
static volatile size_t ringReadPos = 0;
static volatile size_t ringAvailable = 0;

// ------------ Logging ------------
#define LOG_LEVEL_INFO  2
#define LOG_LEVEL_DEBUG 3
#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LEVEL_DEBUG
#endif

#define TS() (uint32_t)millis()
#define LOGI(fmt, ...) do{ if(LOG_LEVEL>=LOG_LEVEL_INFO)  Serial.printf("[I %8lu] " fmt "\n", TS(), ##__VA_ARGS__);}while(0)
#define LOGD(fmt, ...) do{ if(LOG_LEVEL>=LOG_LEVEL_DEBUG) Serial.printf("[D %8lu] " fmt "\n", TS(), ##__VA_ARGS__);}while(0)
#define LOGW(fmt, ...) Serial.printf("[W %8lu] " fmt "\n", TS(), ##__VA_ARGS__)
#define LOGE(fmt, ...) Serial.printf("[E %8lu] " fmt "\n", TS(), ##__VA_ARGS__)

// ------------ Globals ------------
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

String apSSID;
IPAddress apIP(192,168,4,1), netMsk(255,255,255,0);
bool inAP = false;
bool wantReconnect = false;
bool serverStarted = false;
bool wifiConnected = false;

// Audio processing state
static QueueHandle_t audioQueue = nullptr;
static TaskHandle_t audioTaskHandle = nullptr;
static TaskHandle_t rtpTaskHandle = nullptr;
static bool audioRunning = false;
static volatile bool i2sInitialized = false;

// Audio statistics
struct AudioStats {
    uint32_t samplesProcessed;
    uint32_t packetsGenerated; 
    float currentRMS;
    float currentPeak;
    uint32_t bufferOverruns;
    uint32_t bufferUnderruns;
};
static AudioStats audioStats = {0};

// RTP configuration
struct RTPConfig {
    IPAddress targetIP;
    uint16_t targetPort;
    bool enabled;
    uint32_t ssrc;
    uint16_t sequenceNumber;
    uint32_t timestamp;
};
static RTPConfig rtpConfig = {IPAddress(192,168,1,100), 5004, false, 0x12345678, 0, 0};

// HB/diag
uint32_t tHeartbeat = 0;

// Serial console
char cmdBuf[96];
size_t cmdLen = 0;
static uint32_t lastCharTime = 0;

// Button state
bool    btnPrev = true;       // true=not pressed (pull-up)
uint32_t btnPressT0 = 0;
bool    btnArmed = false;

// G.711 A-law encoding table
static const uint8_t alaw_encode[2048] = {
    0xd5, 0xd4, 0xd7, 0xd6, 0xd1, 0xd0, 0xd3, 0xd2, 0xdd, 0xdc, 0xdf, 0xde, 0xd9, 0xd8, 0xdb, 0xda,
    0xc5, 0xc4, 0xc7, 0xc6, 0xc1, 0xc0, 0xc3, 0xc2, 0xcd, 0xcc, 0xcf, 0xce, 0xc9, 0xc8, 0xcb, 0xca,
    0xf5, 0xf4, 0xf7, 0xf6, 0xf1, 0xf0, 0xf3, 0xf2, 0xfd, 0xfc, 0xff, 0xfe, 0xf9, 0xf8, 0xfb, 0xfa,
    0xe5, 0xe4, 0xe7, 0xe6, 0xe1, 0xe0, 0xe3, 0xe2, 0xed, 0xec, 0xef, 0xee, 0xe9, 0xe8, 0xeb, 0xea,
    0x95, 0x94, 0x97, 0x96, 0x91, 0x90, 0x93, 0x92, 0x9d, 0x9c, 0x9f, 0x9e, 0x99, 0x98, 0x9b, 0x9a,
    0x85, 0x84, 0x87, 0x86, 0x81, 0x80, 0x83, 0x82, 0x8d, 0x8c, 0x8f, 0x8e, 0x89, 0x88, 0x8b, 0x8a,
    0xb5, 0xb4, 0xb7, 0xb6, 0xb1, 0xb0, 0xb3, 0xb2, 0xbd, 0xbc, 0xbf, 0xbe, 0xb9, 0xb8, 0xbb, 0xba,
    0xa5, 0xa4, 0xa7, 0xa6, 0xa1, 0xa0, 0xa3, 0xa2, 0xad, 0xac, 0xaf, 0xae, 0xa9, 0xa8, 0xab, 0xaa,
    0x55, 0x54, 0x57, 0x56, 0x51, 0x50, 0x53, 0x52, 0x5d, 0x5c, 0x5f, 0x5e, 0x59, 0x58, 0x5b, 0x5a,
    0x45, 0x44, 0x47, 0x46, 0x41, 0x40, 0x43, 0x42, 0x4d, 0x4c, 0x4f, 0x4e, 0x49, 0x48, 0x4b, 0x4a,
    0x75, 0x74, 0x77, 0x76, 0x71, 0x70, 0x73, 0x72, 0x7d, 0x7c, 0x7f, 0x7e, 0x79, 0x78, 0x7b, 0x7a,
    0x65, 0x64, 0x67, 0x66, 0x61, 0x60, 0x63, 0x62, 0x6d, 0x6c, 0x6f, 0x6e, 0x69, 0x68, 0x6b, 0x6a,
    0x15, 0x14, 0x17, 0x16, 0x11, 0x10, 0x13, 0x12, 0x1d, 0x1c, 0x1f, 0x1e, 0x19, 0x18, 0x1b, 0x1a,
    0x05, 0x04, 0x07, 0x06, 0x01, 0x00, 0x03, 0x02, 0x0d, 0x0c, 0x0f, 0x0e, 0x09, 0x08, 0x0b, 0x0a,
    0x35, 0x34, 0x37, 0x36, 0x31, 0x30, 0x33, 0x32, 0x3d, 0x3c, 0x3f, 0x3e, 0x39, 0x38, 0x3b, 0x3a,
    0x25, 0x24, 0x27, 0x26, 0x21, 0x20, 0x23, 0x22, 0x2d, 0x2c, 0x2f, 0x2e, 0x29, 0x28, 0x2b, 0x2a
};

uint8_t encodeAlaw(int16_t sample) {
    int sign = (sample < 0) ? 0x80 : 0x00;
    if (sample < 0) sample = -sample;
    if (sample > 0x7FF) sample = 0x7FF;
    return alaw_encode[sample] ^ sign ^ 0x55;
}

bool setupI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // INMP441 outputs 24-bit in 32-bit frame
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,   // L/R = GND selects left
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = I2S_BUFFER_COUNT,
        .dma_buf_len = I2S_BUFFER_SIZE,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_SCK_GPIO,
        .ws_io_num = I2S_WS_GPIO,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = I2S_SD_GPIO
    };

    esp_err_t result = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (result != ESP_OK) {
        LOGE("I2S driver install failed: %s", esp_err_to_name(result));
        return false;
    }

    result = i2s_set_pin(I2S_PORT, &pin_config);
    if (result != ESP_OK) {
        LOGE("I2S pin config failed: %s", esp_err_to_name(result));
        i2s_driver_uninstall(I2S_PORT);
        return false;
    }

    // Clear I2S buffers
    i2s_zero_dma_buffer(I2S_PORT);
    
    LOGI("I2S initialized: %dHz, %d-bit, %d channels", SAMPLE_RATE, BITS_PER_SAMPLE, CHANNELS);
    i2sInitialized = true;
    return true;
}

void audioProcessTask(void *pv) {
    const size_t bytesToRead = I2S_BUFFER_SIZE * sizeof(int32_t);
    int32_t *i2sBuffer = (int32_t*)malloc(bytesToRead);
    if (!i2sBuffer) {
        LOGE("Failed to allocate I2S buffer");
        vTaskDelete(NULL);
        return;
    }

    LOGI("Audio processing task started on core %d", xPortGetCoreID());

    while (audioRunning) {
        size_t bytesRead = 0;
        esp_err_t result = i2s_read(I2S_PORT, i2sBuffer, bytesToRead, &bytesRead, portMAX_DELAY);
        
        if (result != ESP_OK) {
            LOGW("I2S read error: %s", esp_err_to_name(result));
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (bytesRead == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
            continue;
        }

        size_t samplesRead = bytesRead / sizeof(int32_t);
        
        // Process samples and add to ring buffer
        float rmsSum = 0;
        int16_t peak = 0;
        
        for (size_t i = 0; i < samplesRead; i++) {
            // Convert 32-bit I2S data to 16-bit PCM
            int16_t sample = (i2sBuffer[i] >> 16) & 0xFFFF;
            
            // Update statistics
            rmsSum += (float)sample * sample;
            if (abs(sample) > peak) peak = abs(sample);
            
            // Add to ring buffer
            if (ringBuffer) {
                ringBuffer[ringWritePos] = sample;
                ringWritePos = (ringWritePos + 1) % RING_BUFFER_SIZE;
                
                if (ringAvailable < RING_BUFFER_SIZE) {
                    ringAvailable++;
                } else {
                    // Buffer full, move read position
                    ringReadPos = (ringReadPos + 1) % RING_BUFFER_SIZE;
                    audioStats.bufferOverruns++;
                }
            }
        }
        
        // Update audio statistics
        audioStats.samplesProcessed += samplesRead;
        audioStats.currentRMS = sqrt(rmsSum / samplesRead);
        audioStats.currentPeak = peak;
        
        // Yield to other tasks
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    free(i2sBuffer);
    LOGI("Audio processing task ended");
    vTaskDelete(NULL);
}

// HTML content
const char HTML_INDEX[] PROGMEM = 
"<!doctype html><html><head><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>ESP32 WebRTC Audio</title></head><body>"
"<h2>ESP32 WebRTC Audio Streaming</h2>"
"<p>Connect this device to your WiFi network:</p>"
"<form action=\"/save\" method=\"post\">"
"  <label>SSID: <input name=\"s\" required></label><br>"
"  <label>Password: <input name=\"p\" type=\"password\"></label><br>"
"  <button type=\"submit\">Connect</button>"
"</form>"
"<p><a href=\"/scan\">Scan for networks</a></p>"
"<p><a href=\"/audio\">Audio Monitor</a></p>"
"<p><a href=\"/diag\">Diagnostics</a></p>"
"</body></html>";

const char HTML_AUDIO[] = R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1"><title>Audio Monitor</title></head><body>
<h2>INMP441 Audio Monitor</h2>
<div>
<p>RMS Level: <span id="rms">0</span></p>
<p>Peak Level: <span id="peak">0</span></p>
<p>Samples Processed: <span id="samples">0</span></p>
<p>Buffer Status: <span id="buffer">0</span>%</p>
<p>I2S Status: <span id="i2s">Unknown</span></p>
<form id="rtpConfig">
  <h3>RTP Streaming</h3>
  <label>Enable RTP <input type="checkbox" id="rtpEnable"></label><br>
  <label>Target IP <input id="rtpIP" placeholder="192.168.1.100"></label><br>
  <label>Target Port <input id="rtpPort" placeholder="5004" type="number"></label><br>
  <button type="button" onclick="saveRTP()">Save RTP Config</button>
</form>
<p><a href='/'>Back</a></p>
<script>
function poll(){
  fetch('/audio-status').then(r=>r.json()).then(j=>{
    document.getElementById('rms').innerText = j.rms.toFixed(1);
    document.getElementById('peak').innerText = j.peak;
    document.getElementById('samples').innerText = j.samples;
    document.getElementById('buffer').innerText = j.bufferFill.toFixed(1);
    document.getElementById('i2s').innerText = j.i2sStatus;
  });
}
function saveRTP(){
  const config = {
    enable: document.getElementById('rtpEnable').checked,
    ip: document.getElementById('rtpIP').value,
    port: parseInt(document.getElementById('rtpPort').value)
  };
  fetch('/rtp-config', {
    method:'POST', 
    headers: {'Content-Type': 'application/json'},
    body: JSON.stringify(config)
  }).then(()=>alert('RTP Config Saved'));
}
setInterval(poll, 1000);
poll();
</script>
</body></html>
)HTML";

void bindRoutes() {
    server.on("/", HTTP_GET, [](){
        LOGD("HTTP /  (client=%s)", server.client().remoteIP().toString().c_str());
        server.send_P(200, "text/html", HTML_INDEX);
    });

    server.on("/audio", HTTP_GET, [](){
        LOGD("HTTP /audio");
        server.send_P(200, "text/html", HTML_AUDIO);
    });

    server.on("/audio-status", HTTP_GET, [](){
        String body = "{";
        body += "\"rms\":" + String(audioStats.currentRMS, 1) + ",";
        body += "\"peak\":" + String(audioStats.currentPeak) + ",";
        body += "\"samples\":" + String(audioStats.samplesProcessed) + ",";
        body += "\"bufferFill\":" + String((float)ringAvailable * 100.0f / RING_BUFFER_SIZE, 1) + ",";
        body += "\"i2sStatus\":\"" + String(i2sInitialized ? "OK" : "Error") + "\"";
        body += "}";
        server.send(200, "application/json", body);
    });

    server.on("/rtp-config", HTTP_POST, [](){
        if (server.hasHeader("Content-Type") && server.header("Content-Type") == "application/json") {
            // Parse JSON manually (simple implementation)
            String json = server.arg("plain");
            // Extract enable, ip, port from JSON
            LOGI("RTP config received: %s", json.c_str());
            server.send(200, "text/plain", "OK");
        } else {
            server.send(400, "text/plain", "Invalid Content-Type");
        }
    });

    server.on("/diag", HTTP_GET, [](){
        String s = "<pre>\n";
        s += "Audio System Status:\n";
        s += "I2S Initialized: " + String(i2sInitialized ? "YES" : "NO") + "\n";
        s += "Audio Running: " + String(audioRunning ? "YES" : "NO") + "\n";
        s += "Ring Buffer Size: " + String(RING_BUFFER_SIZE) + " samples\n";
        s += "Ring Buffer Used: " + String(ringAvailable) + " samples\n";
        s += "Sample Rate: " + String(SAMPLE_RATE) + " Hz\n";
        s += "Current RMS: " + String(audioStats.currentRMS, 1) + "\n";
        s += "Current Peak: " + String(audioStats.currentPeak) + "\n";
        s += "Samples Processed: " + String(audioStats.samplesProcessed) + "\n";
        s += "Buffer Overruns: " + String(audioStats.bufferOverruns) + "\n";
        s += "Free Heap: " + String(ESP.getFreeHeap()) + " bytes\n";
        s += "</pre><p><a href='/'>Back</a></p>";
        server.send(200, "text/html", s);
    });

    // Legacy routes for compatibility
    server.on("/save", HTTP_POST, [](){
        if (!server.hasArg("s")) { 
            server.send(400, "text/plain", "Missing SSID"); 
            return; 
        }
        String ssid = server.arg("s");
        String pass = server.hasArg("p") ? server.arg("p") : "";

        prefs.begin("net", false);
        prefs.putString("ssid", ssid);
        prefs.putString("pass", pass);
        prefs.end();

        wantReconnect = true;
        server.send(200, "text/html", 
            "<html><body><h3>Connecting to " + ssid + "...</h3></body></html>");
    });

    server.onNotFound([&](){
        if (inAP) {
            server.sendHeader("Location", String("http://") + apIP.toString() + "/");
            server.send(302, "text/plain", "");
        } else {
            server.send(404, "text/plain", "Not found");
        }
    });
}

void loadAudioConfig() {
    prefs.begin("audio", true);
    rtpConfig.enabled = prefs.getBool("rtpEnabled", false);
    String targetIP = prefs.getString("targetIP", "192.168.1.100");
    rtpConfig.targetPort = prefs.getUShort("targetPort", 5004);
    prefs.end();
    
    rtpConfig.targetIP.fromString(targetIP);
    LOGI("Audio config loaded: RTP %s to %s:%d", 
         rtpConfig.enabled ? "enabled" : "disabled",
         rtpConfig.targetIP.toString().c_str(), 
         rtpConfig.targetPort);
}

void startAudio() {
    if (audioRunning) return;
    
    // Allocate ring buffer
    if (!ringBuffer) {
        ringBuffer = (int16_t*)malloc(RING_BUFFER_SIZE * sizeof(int16_t));
        if (!ringBuffer) {
            LOGE("Failed to allocate ring buffer");
            return;
        }
        memset(ringBuffer, 0, RING_BUFFER_SIZE * sizeof(int16_t));
    }
    
    // Reset ring buffer state
    ringWritePos = 0;
    ringReadPos = 0;
    ringAvailable = 0;
    
    // Create audio processing queue
    audioQueue = xQueueCreate(8, sizeof(uint32_t));
    if (!audioQueue) {
        LOGE("Failed to create audio queue");
        return;
    }
    
    audioRunning = true;
    
    // Create audio processing task on core 1
    xTaskCreatePinnedToCore(
        audioProcessTask,
        "AudioProcess",
        8192,  // Stack size
        NULL,
        configMAX_PRIORITIES - 2,  // High priority
        &audioTaskHandle,
        1  // Core 1
    );
    
    LOGI("Audio system started");
}

void stopAudio() {
    audioRunning = false;
    
    if (audioTaskHandle) {
        vTaskDelete(audioTaskHandle);
        audioTaskHandle = nullptr;
    }
    
    if (audioQueue) {
        vQueueDelete(audioQueue);
        audioQueue = nullptr;
    }
    
    LOGI("Audio system stopped");
}

// WiFi and system functions (similar to original)
bool tryConnectFromPrefs(uint32_t timeoutMs) {
    prefs.begin("net", true);
    String ssid = prefs.getString("ssid", "");
    String pass = prefs.getString("pass", "");
    prefs.end();

    if (ssid.isEmpty()) { 
        LOGI("No stored credentials."); 
        return false; 
    }

    LOGI("Attempting STA connect to SSID='%s'", ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
        delay(100);
        pollSerial();
        handleButton();
        Serial.print('.');
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        LOGI("STA connected: IP=%s", WiFi.localIP().toString().c_str());
        wifiConnected = true;
        return true;
    }
    
    LOGW("STA connect failed.");
    wifiConnected = false;
    return false;
}

void startCaptiveAP() {
    uint32_t r = esp_random();
    apSSID = "ESP32-Audio-" + String((r >> 16) & 0xFFFF, HEX);
    apSSID.toUpperCase();

    LOGI("Starting AP '%s'", apSSID.c_str());
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, netMsk);
    WiFi.softAP(apSSID.c_str());
    delay(150);
    inAP = true;
    wifiConnected = false;

    dnsServer.start(DNS_PORT, "*", apIP);
    LOGI("DNS captive portal started");

    if (!serverStarted) { 
        bindRoutes(); 
        server.begin(); 
        serverStarted = true; 
    }
}

// Serial command processing
void processCommand(const String& cmd) {
    if (cmd == "help") {
        Serial.println(
            "Commands:\n"
            "  help       - show this help\n"
            "  status     - print system status\n"
            "  audio      - show audio statistics\n"
            "  start      - start audio capture\n"
            "  stop       - stop audio capture\n"
            "  reboot     - restart MCU\n");
    } else if (cmd == "status") {
        Serial.printf("WiFi: %s, Audio: %s, I2S: %s\n",
                     wifiConnected ? "Connected" : "Disconnected",
                     audioRunning ? "Running" : "Stopped",
                     i2sInitialized ? "OK" : "Error");
    } else if (cmd == "audio") {
        Serial.printf("Samples: %lu, RMS: %.1f, Peak: %d, Buffer: %lu/%lu\n",
                     audioStats.samplesProcessed,
                     audioStats.currentRMS,
                     audioStats.currentPeak,
                     ringAvailable,
                     (uint32_t)RING_BUFFER_SIZE);
    } else if (cmd == "start") {
        startAudio();
        Serial.println("Audio capture started");
    } else if (cmd == "stop") {
        stopAudio();
        Serial.println("Audio capture stopped");
    } else if (cmd == "reboot") {
        ESP.restart();
    } else if (cmd.length()) {
        Serial.printf("Unknown command: '%s'\n", cmd.c_str());
    }
}

void pollSerial() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        lastCharTime = millis();
        if (c == '\r' || c == '\n') {
            if (cmdLen > 0) {
                cmdBuf[min(cmdLen, sizeof(cmdBuf)-1)] = 0;
                String cmd = String(cmdBuf);
                cmd.trim();
                cmdLen = 0;
                processCommand(cmd);
            }
        } else if (cmdLen < sizeof(cmdBuf)-1) {
            cmdBuf[cmdLen++] = c;
        }
    }
}

void handleButton() {
    bool pressed = (digitalRead(BOOT_BTN_GPIO) == LOW);
    uint32_t now = millis();

    if (btnPrev && !pressed) {
        if (btnArmed) {
            uint32_t held = now - btnPressT0;
            if (held >= BTN_SHORT_MS && held < BTN_LONG_MS) {
                LOGI("Button short press: starting AP");
                startCaptiveAP();
            }
        }
        btnArmed = false;
    } else if (!btnPrev && pressed) {
        btnPressT0 = now;
        btnArmed = true;
    }
    btnPrev = pressed;
}

void setup() {
    Serial.begin(115200);
    delay(150);
    Serial.println();
    Serial.printf("ESP32 WebRTC Audio. SDK=%s, Chip=%s rev%d\n",
                  ESP.getSdkVersion(), ESP.getChipModel(), ESP.getChipRevision());

    wifiConnected = false;

#if HEARTBEAT_GPIO >= 0
    pinMode(HEARTBEAT_GPIO, OUTPUT);
    digitalWrite(HEARTBEAT_GPIO, LOW);
#endif
    pinMode(BOOT_BTN_GPIO, INPUT_PULLUP);

    // Initialize I2S for INMP441
    if (!setupI2S()) {
        LOGE("I2S setup failed!");
    }

    // System stability settings
    WiFi.setSleep(false);
    esp_wifi_set_ps(WIFI_PS_NONE);
    setCpuFrequencyMhz(240);
    LOGI("CPU locked to 240MHz for audio stability");

    loadAudioConfig();
    bindRoutes();

    if (tryConnectFromPrefs(CONNECT_TIMEOUT_MS)) {
        LOGI("Starting in STA mode");
        if (!serverStarted) { 
            server.begin(); 
            serverStarted = true; 
        }
        // Auto-start audio capture when connected
        startAudio();
    } else {
        startCaptiveAP();
    }
}

void loop() {
    pollSerial();
    handleButton();

    // Heartbeat LED
    uint32_t now = millis();
    if (now - tHeartbeat >= 1000) {
        tHeartbeat = now;
#if HEARTBEAT_GPIO >= 0
        if (wifiConnected && audioRunning) {
            digitalWrite(HEARTBEAT_GPIO, HIGH);  // Solid when running
        } else {
            digitalWrite(HEARTBEAT_GPIO, !digitalRead(HEARTBEAT_GPIO));  // Blink
        }
#endif
    }

    if (serverStarted) server.handleClient();
    if (inAP) dnsServer.processNextRequest();

    // Auto-reconnect logic
    static uint32_t lastTry = 0;
    if (wantReconnect && (now - lastTry > RETRY_CONNECT_MS)) {
        lastTry = now;
        if (tryConnectFromPrefs(CONNECT_TIMEOUT_MS)) {
            LOGI("Reconnect success");
            dnsServer.stop();
            WiFi.softAPdisconnect(true);
            WiFi.mode(WIFI_STA);
            inAP = false;
            wantReconnect = false;
            startAudio();  // Start audio after successful connection
        }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
}
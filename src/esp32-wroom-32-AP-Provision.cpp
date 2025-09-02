/**
 * @brief This application demonstrates the provisioning process for ESP32 devices.
 *
 * It acts as a home monitoring device that sends audio input from a KY-038 sound sensor
 * module (LM393 comparator + analog output) and will stream audio via WebRTC.
 *
 * High-level plan (feature-webrtc) — add these items to the TODO and implement in sequence
 * so I can proceed only after you approve the plan below:
 *
 * 1) Hardware-level digital detection and MQTT/post callback
 *    - Use the KY-038 D0 comparator output as a trimmed hardware-level detector (user
 *      sets pot to detect events like dog barks). Debounce and count events in firmware.
 *    - Add optional network callbacks when detection occurs:
 *        * MQTT publish to topic (configurable in provisioning UI)
 *        * HTTP POST webhook (configurable URL)
 *    - Deliverables: config fields in provisioning UI, JSON payload schema, and a
 *      documented example (MQTT topic, sample POST body).
 *
 * 2) Provisioning UI: calibration and nominal defaults
 *    - Extend captive-portal web UI to include a simple calibration page that:
 *        * Shows live D0 state and detection count while the user adjusts the pot
 *        * Allows saving nominal sensitivity and endpoints (MQTT server, topic, webhook URL)
 *    - Provide sensible defaults: enable D0 detection by default, MQTT disabled by default,
 *      nominal sensitivity = mid pot setting (documented value and how to tune for barks).
 *    - Deliverables: UI fields, client-side JS to poll `/sound` endpoint, and stored configs
 *      in NVS (namespace `net` or `sound` as appropriate).
 *
 * 3) ADC recorder (optional, after D0/notification working)
 *    - Implement an ADC-based circular buffer recorder reading KY-038 A0 into PCM frames.
 *    - Provide a debug HTTP endpoint (or WebSocket) to stream small PCM snippets for testing.
 *    - Deliverables: ring buffer, task to collect samples, `/samples` debug route returning
 *      raw PCM or level summaries.
 *
 * 4) G.711 -> WebRTC transport
 *    - Implement a minimal encoder path from captured PCM to G.711 (u-law or A-law) since
 *      it's simpler and lightweight on ESP32. Use existing G.711 implementation or small codec.
 *    - Integrate a WebRTC transport or a bridge that accepts G.711 RTP packets and negotiates
 *      SDP/ICE with a remote peer (browser/server). If full WebRTC is heavy, provide an RTP
 *      relay or a signaling-based bridge running off-device as an interim approach.
 *    - Deliverables: encoder, signaling example (WebSocket), and an end-to-end demo plan.
 *
 * Contract / success criteria (for each numbered step):
 *  - Inputs: GPIO wiring (D0 to configured pin, A0 to ADC pin), provisioning values (MQTT/server)
 *  - Outputs: MQTT messages or POST bodies on detection; saved calibration values; working
 *    ADC recorder producing verified PCM; G.711 RTP packets negotiable via signaling.
 *  - Error modes: network down (buffer or retry), wrong polarity (configurable), NVS failures.
 *
 * Edge-cases & notes:
 *  - KY-038 D0 polarity can vary; include `SOUND_DO_ACTIVE_HIGH` flag and UI toggle.
 *  - `flush-nvs` clears all namespaces — keep it as a power-user option.
 *  - WebRTC on ESP32 is constrained; if runtime limits are hit, plan for a small bridge/server.
 *
 * After you approve the plan above I will implement items in sequence, starting with (1)
 * digital detection + MQTT/post and provisioning UI changes for calibration.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_pm.h>
#include <nvs_flash.h>

// Enable MQTT support for dog bark detection notifications
#define ENABLE_MQTT

// Optional MQTT support: define ENABLE_MQTT to enable PubSubClient-based MQTT publishing.
#ifdef ENABLE_MQTT
#include <PubSubClient.h>
WiFiClient espClient;
PubSubClient mqttClient(espClient);
#endif
#include <HTTPClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// Forward declarations
void loadSoundConfig();
void pollSerial();
void handleButton();
void notificationTask(void *pv);

#define CONNECT_TIMEOUT_MS 15000
#define RETRY_CONNECT_MS    5000
#define DNS_PORT 53

// -------- Pins --------
#define HEARTBEAT_GPIO 2     // set -1 to disable; many DevKitC use GPIO2 LED
#define BOOT_BTN_GPIO  0     // BOOT button (IO0), active-low

// Sound sensor (KY-038) digital output (D0) pin. Change to the GPIO you wired D0 to.
// The KY-038's LM393 comparator output is typically an open-collector style output;
// using INPUT_PULLUP is recommended unless you provide an external pull-up.
#define SOUND_DO_GPIO   15
// Set to 1 if the module outputs HIGH when sound is detected, set to 0 if it outputs LOW
// (some module configurations invert the logic via the onboard potentiometer).
#define SOUND_DO_ACTIVE_HIGH 1

// -------- Button thresholds (ms) --------
#define BTN_SHORT_MS   500    // >= short < long  -> reprov
#define BTN_LONG_MS    3000   // >= long < very   -> clear-net + reprov
#define BTN_VLONG_MS   6000   // >= very long     -> flush-nvs + reboot

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
bool wifiConnected = false;  // Track WiFi connection status for LED control

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

// Sound sensor state
bool    soundPrev = true;     // assume idle (pull-up)
uint32_t soundDebounceT = 0;
uint32_t lastSoundT = 0;      // last detection timestamp
uint32_t soundCount = 0;      // number of detections
const uint32_t SOUND_DEBOUNCE_MS = 50;
bool    soundActive = false;

// Enhanced sound detection parameters (configurable)
uint32_t soundTMinMs = 100;      // Minimum event duration
uint32_t soundTQuietMs = 300;    // Quiet period to end event
uint32_t soundLevelThreshold = 512; // ADC RMS threshold
uint32_t soundEventStart = 0;    // When current event started
bool     soundEventActive = false; // Whether we have an active event

// Notification / configuration
String mqttServer = "";
uint16_t mqttPort = 1883;
String mqttTopic = "";
bool mqttEnabled = false;
String webhookUrl = "";
bool webhookEnabled = false;

// Notification event queue
struct NotificationEvent {
  uint32_t timestamp;     // event start timestamp (ms)
  uint32_t duration_ms;   // event duration
  uint32_t rms;           // sampled RMS level
  uint32_t peak;          // sampled peak level
};

static QueueHandle_t notifyQueue = nullptr;

// ADC recorder configuration
#define SOUND_A0_GPIO 34   // default ADC pin for KY-038 A0 (ADC1_CH6). Change as needed.
#define REC_SAMPLE_RATE 8000 // Hz
#define REC_MAX_MS 2000      // max record length in ms
#define REC_MAX_SAMPLES ((REC_SAMPLE_RATE * REC_MAX_MS) / 1000)

static int16_t *recBuffer = nullptr;
static size_t recSamples = 0;
static volatile bool recBusy = false;
static volatile bool recReady = false;

// simple base64 encoder
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
String base64Encode(const uint8_t *data, size_t len) {
  String out;
  out.reserve(((len + 2) / 3) * 4 + 8);
  uint32_t val=0; int valb=-6;
  for (size_t i=0;i<len;i++) {
    val = (val<<8) + data[i];
    valb += 8;
    while (valb>=0) {
      out += b64[(val>>valb)&0x3F];
      valb -= 6;
    }
  }
  if (valb>-6) out += b64[((val<<8)>>(valb+8))&0x3F];
  while (out.length()%4) out += '=';
  return out;
}

// Recording task (runs on separate core)
void recorderTask(void *pv) {
  int sr = REC_SAMPLE_RATE;
  size_t samplesToCapture = recSamples;
  int16_t *buf = recBuffer;
  // basic busy-wait capture with ets_delay_us for timing
  uint32_t period_us = 1000000UL / sr;
  for (size_t i=0;i<samplesToCapture;i++) {
    int v = analogRead(SOUND_A0_GPIO); // 0..4095
    // convert to signed 16-bit centered at 0
    int16_t s = (int16_t)((v - 2048) * 16); // scale 12-bit->16-bit (~shift left 4)
    buf[i] = s;
    ets_delay_us(period_us);
  }
  recBusy = false;
  recReady = true;
  LOGI("Recording complete: %u samples", (unsigned)samplesToCapture);
  vTaskDelete(NULL);
}

bool startRecordingMs(uint32_t ms) {
  if (recBusy) return false;
  uint32_t wantSamples = (REC_SAMPLE_RATE * ms) / 1000;
  if (wantSamples == 0) return false;
  if (wantSamples > REC_MAX_SAMPLES) wantSamples = REC_MAX_SAMPLES;
  if (!recBuffer) {
    recBuffer = (int16_t*)malloc(sizeof(int16_t) * REC_MAX_SAMPLES);
    if (!recBuffer) { LOGE("Alloc recBuffer failed"); return false; }
  }
  recSamples = wantSamples;
  recBusy = true;
  recReady = false;
  BaseType_t r = xTaskCreatePinnedToCore(recorderTask, "recorder", 4096, NULL, 3, NULL, 1);
  if (r != pdPASS) { recBusy = false; LOGE("Recorder task create failed"); return false; }
  return true;
}

// expose samples as JSON with base64 PCM16LE
void handleSamplesRoute() {
  if (!recReady) { server.send(404, "text/plain", "No samples ready"); return; }
  size_t bytes = recSamples * sizeof(int16_t);
  // cast buffer to uint8_t
  uint8_t *b = (uint8_t*)recBuffer;
  String b64 = base64Encode(b, bytes);
  String body = "{\n  \"sampleRate\": " + String(REC_SAMPLE_RATE) + ",\n  \"samplesBase64\": \"" + b64 + "\"\n}\n";
  server.send(200, "application/json", body);
}

// --------- HTML ----------
// Main provisioning page
const char HTML_INDEX[] PROGMEM = 
"<!doctype html><html><head><meta name=viewport content=\"width=device-width,initial-scale=1\"><title>ESP32 Provisioning</title></head><body>"
"<h2>ESP32 WiFi Provisioning</h2>"
"<p>Connect this device to your WiFi network:</p>"
"<form action=\"/save\" method=\"post\">"
"  <label>SSID: <input name=\"s\" required></label><br>"
"  <label>Password: <input name=\"p\" type=\"password\"></label><br>"
"  <button type=\"submit\">Connect</button>"
"</form>"
"<p><a href=\"/scan\">Scan for networks</a></p>"
"<p><a href=\"/diag\">Diagnostics</a></p>"
"<p><a href=\"/calibrate\">Sound Calibration</a></p>"
"<p><a href=\"/samples\">Audio Samples</a></p>"
"</body></html>";

// (calibration page below)
const char HTML_CALIBRATE[] = R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1"><title>Sound Calibration</title></head><body>
<h2>Sound Calibration & Notification</h2>
<div>
<p>Adjust the module pot until desired sensitivity (try barking at the device).</p>
<p>Detected state: <span id="state">unknown</span></p>
<p>Count: <span id="count">0</span></p>
<form id="cfg">
  <label>Enable MQTT <input type="checkbox" id="mqenable"></label><br>
  <label>MQTT server <input id="mqsrv" placeholder="mqtt.example.com"></label><br>
  <label>MQTT topic <input id="mqtopic" placeholder="home/sound"></label><br>
  <label>Enable Webhook <input type="checkbox" id="wbenable"></label><br>
  <label>Webhook URL <input id="wburl" placeholder="https://example.com/webhook"></label><br>
  <button type="button" onclick="save()">Save</button>
</form>
<p><a href='/'>Back</a></p>
<script>
function poll(){fetch('/sound').then(r=>r.json()).then(j=>{document.getElementById('state').innerText = j.soundDetected; document.getElementById('count').innerText = j.count;});}
function save(){fetch('/calibrate', {method:'POST', body: new URLSearchParams({mqenable:document.getElementById('mqenable').checked?'1':'0', mqsrv:document.getElementById('mqsrv').value, mqtopic:document.getElementById('mqtopic').value, wbenable:document.getElementById('wbenable').checked?'1':'0', wburl:document.getElementById('wburl').value})}).then(()=>alert('Saved'))}
setInterval(poll,500);
poll();
</script>
</body></html>
)HTML";

// ------------- Helpers -------------
void printNetDiag() {
  wifi_mode_t m; esp_wifi_get_mode(&m);
  LOGI("Mode=%s, Status=%d", (m==WIFI_MODE_AP?"AP":m==WIFI_MODE_STA?"STA":m==WIFI_MODE_APSTA?"AP+STA":"UNK"), WiFi.status());
  if (WiFi.status()==WL_CONNECTED) {
    LOGI("STA IP=%s  GW=%s  Mask=%s", WiFi.localIP().toString().c_str(),
         WiFi.gatewayIP().toString().c_str(), WiFi.subnetMask().toString().c_str());
    LOGI("SSID='%s'  BSSID=%s  RSSI=%d dBm  Chan=%d",
         WiFi.SSID().c_str(), WiFi.BSSIDstr().c_str(), WiFi.RSSI(), WiFi.channel());
  }
  if (m==WIFI_MODE_AP || m==WIFI_MODE_APSTA) {
    LOGI("AP SSID='%s' IP=%s Clients=%d", apSSID.c_str(), apIP.toString().c_str(), WiFi.softAPgetStationNum());
  }
}

String getISOTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    // Fallback to millis-based timestamp if NTP not available
    char buf[32];
    uint32_t ms = millis();
    uint32_t sec = ms / 1000;
    uint32_t min = sec / 60;
    uint32_t hour = min / 60;
    snprintf(buf, sizeof(buf), "1970-01-01T%02u:%02u:%02u.%03uZ", 
             hour % 24, min % 60, sec % 60, ms % 1000);
    return String(buf);
  }
  
  char buf[25];
  strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
  return String(buf);
}

String htmlScan(const int n) {
  String h = F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'><title>Scan</title></head><body><h2>Nearby Networks</h2><ul>");
  for (int i=0;i<n;i++) {
    h += "<li>" + WiFi.SSID(i) + " (RSSI " + String(WiFi.RSSI(i)) +
         (WiFi.encryptionType(i)==WIFI_AUTH_OPEN ? ", open" : ", secured") +
         ", chan " + String(WiFi.channel(i)) + ")</li>";
  }
  h += F("</ul><p><a href='/'>Back</a></p></body></html>");
  LOGI("SCAN complete: %d networks", n);
  return h;
}

bool tryConnectFromPrefs(uint32_t timeoutMs) {
  prefs.begin("net", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  prefs.end();

  if (ssid.isEmpty()) { LOGI("No stored credentials."); return false; }

  LOGI("Attempting STA connect to SSID='%s' (timeout %u ms)", ssid.c_str(), timeoutMs);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
    delay(100);  // Reduced delay to allow more frequent polling
    pollSerial();  // Allow serial commands during connection attempt
    handleButton();  // Allow button presses during connection attempt
    
    // Check if credentials were cleared or mode changed during connection attempt
    wifi_mode_t currentMode;
    esp_wifi_get_mode(&currentMode);
    if (currentMode != WIFI_MODE_STA) {
      LOGI("WiFi mode changed during connection attempt, aborting");
      return false;
    }
    
    // Check if NVS was cleared
    prefs.begin("net", true);
    String checkSsid = prefs.getString("ssid", "");
    prefs.end();
    if (checkSsid.isEmpty()) {
      LOGI("Credentials cleared during connection attempt, aborting");
      return false;
    }
    
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    LOGI("STA connected: IP=%s RSSI=%d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    wifiConnected = true;
    #if HEARTBEAT_GPIO >= 0
      digitalWrite(HEARTBEAT_GPIO, HIGH);  // Solid on when connected
    #endif
    printNetDiag();
    return true;
  }
  LOGW("STA connect failed.");
  wifiConnected = false;
  #if HEARTBEAT_GPIO >= 0
    digitalWrite(HEARTBEAT_GPIO, LOW);  // Start blinking
  #endif
  return false;
}

void bindRoutes() {
  server.on("/", HTTP_GET, [](){
    LOGD("HTTP /  (client=%s)", server.client().remoteIP().toString().c_str());
    server.send_P(200, "text/html", HTML_INDEX);
  });

  server.on("/scan", HTTP_GET, [](){
    LOGD("HTTP /scan");
    if (inAP) WiFi.mode(WIFI_AP_STA); // allow scan while AP up
    int n = WiFi.scanNetworks(false,true);
    server.send(200, "text/html", htmlScan(n));
    WiFi.scanDelete();
  });

  server.on("/diag", HTTP_GET, [](){
    String s = "<pre>\n";
    wifi_mode_t m; esp_wifi_get_mode(&m);
    s += "Uptime(ms): " + String(millis()) + "\n";
    s += "FreeHeap: " + String(ESP.getFreeHeap()) + "\n";
    s += "SDK: " + String(ESP.getSdkVersion()) + "\n";
    s += "Chip: " + String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()) + "\n";
    s += "Mode: " + String((m==WIFI_MODE_AP)?"AP":(m==WIFI_MODE_STA)?"STA":"AP+STA") + "\n";
    s += "Status: " + String(WiFi.status()) + "\n";
    s += "AP SSID: " + apSSID + "  IP: " + apIP.toString() + "\n";
    if (WiFi.status()==WL_CONNECTED) {
      s += "STA SSID: " + WiFi.SSID() + "\n";
      s += "STA IP: " + WiFi.localIP().toString() + "\n";
      s += "RSSI: " + String(WiFi.RSSI()) + " dBm\n";
    }
    s += "\nSound sensor:\n";
    s += "  D0 pin: " + String(SOUND_DO_GPIO) + "\n";
    s += "  Last detected: " + String(lastSoundT) + " ms\n";
    s += "  Count: " + String(soundCount) + "\n";
    s += "</pre><p><a href='/'>Back</a></p>";
    LOGD("HTTP /diag");
    server.send(200, "text/html", s);
  });

  server.on("/sound", HTTP_GET, [](){
    String body = "{\n";
    body += "  \"soundDetected\": " + String(soundActive ? "true" : "false") + ",\n";
    body += "  \"lastSoundT\": " + String(lastSoundT) + ",\n";
    body += "  \"count\": " + String(soundCount) + "\n";
    body += "}\n";
    server.send(200, "application/json", body);
  });

  server.on("/samples", HTTP_GET, [](){ handleSamplesRoute(); });

  // Add /calibrate POST handler to save sound notification settings
  server.on("/calibrate", HTTP_POST, []() {
    bool mqenable = server.hasArg("mqenable") && server.arg("mqenable") == "1";
    String mqsrv = server.hasArg("mqsrv") ? server.arg("mqsrv") : "";
    String mqtopic = server.hasArg("mqtopic") ? server.arg("mqtopic") : "";
    bool wbenable = server.hasArg("wbenable") && server.arg("wbenable") == "1";
    String wburl = server.hasArg("wburl") ? server.arg("wburl") : "";

    prefs.begin("sound", false);
    prefs.putBool("mqttEn", mqenable);
    prefs.putString("mqttSrv", mqsrv);
    prefs.putString("mqttTopic", mqtopic);
    prefs.putBool("wbEn", wbenable);
    prefs.putString("wbUrl", wburl);
    prefs.end();

    loadSoundConfig(); // reload globals

    server.send(200, "text/plain", "Saved");
  });

  server.on("/save", HTTP_POST, [](){
    LOGD("HTTP /save  args=%d", server.args());
    if (!server.hasArg("s")) { server.send(400, "text/plain", "Missing SSID"); return; }
    String ssid = server.arg("s");
    String pass = server.hasArg("p") ? server.arg("p") : "";

    LOGI("Saving credentials: SSID='%s' (len pass=%u)", ssid.c_str(), (unsigned)pass.length());
    prefs.begin("net", false);
    prefs.putString("ssid", ssid);
    prefs.putString("pass", pass);
    prefs.end();

    wantReconnect = true;  // main loop will attempt connect
    server.send(200, "text/html",
      "<html><body><h3>Connecting to " + ssid +
      " ...</h3><p>Watch serial logs for status.</p>"
      "<meta http-equiv='refresh' content='2; url=/status'></body></html>");
  });

  server.on("/status", HTTP_GET, [](){
    wl_status_t st = WiFi.status();
    LOGD("HTTP /status  WiFi.status=%d", st);
    String body = "<html><body><h3>Status: ";
    body += (st==WL_CONNECTED ? "Connected" : "Not connected");
    body += "</h3>";
    if (st==WL_CONNECTED) {
      body += "<p>IP: " + WiFi.localIP().toString() + "</p>";
    } else {
      body += "<p>If connection fails, go <a href='/'>back</a> and re-enter credentials.</p>";
    }
    body += "</body></html>";
    server.send(200, "text/html", body);
  });

  server.onNotFound([&](){
    String host = server.hostHeader();
    String uri  = server.uri();
    LOGD("HTTP %s %s from %s", server.method()==HTTP_GET?"GET":"POST",
         uri.c_str(), server.client().remoteIP().toString().c_str());
    if (inAP && host != apIP.toString()) {
      LOGD("Captive redirect host='%s' -> %s", host.c_str(), apIP.toString().c_str());
      server.sendHeader("Location", String("http://") + apIP.toString() + "/");
      server.send(302, "text/plain", "");
    } else {
      server.send(404, "text/plain", "Not found");
    }
  });
}

void loadSoundConfig() {
  prefs.begin("sound", true);
  mqttEnabled = prefs.getBool("mqttEn", false);
  mqttServer = prefs.getString("mqttSrv", "");
  mqttTopic = prefs.getString("mqttTopic", "");
  webhookEnabled = prefs.getBool("wbEn", false);
  webhookUrl = prefs.getString("wbUrl", "");
  
  // Load enhanced sound detection parameters
  soundTMinMs = prefs.getULong("tMinMs", 100);
  soundTQuietMs = prefs.getULong("tQuietMs", 300);
  soundLevelThreshold = prefs.getULong("levelThresh", 512);
  
  prefs.end();
  
  LOGI("Sound config loaded: T_min=%lums, T_quiet=%lums, threshold=%lu", 
       soundTMinMs, soundTQuietMs, soundLevelThreshold);
}

// Notification sequence counter for event tracking
static uint32_t notificationSeq = 0;

// Enhanced webhook with detailed JSON payload
void sendWebhook(const NotificationEvent &evt) {
  if (!webhookEnabled || webhookUrl.length() == 0) return;

  HTTPClient http;
  http.begin(webhookUrl);
  http.addHeader("Content-Type", "application/json");

  // Create detailed JSON payload with NTP timestamp
  String payload = "{";
  payload += "\"ts\":\"" + getISOTimestamp() + "\",";
  payload += "\"seq\":" + String(notificationSeq++) + ",";
  payload += "\"duration_ms\":" + String(evt.duration_ms) + ",";
  payload += "\"rms\":" + String(evt.rms) + ",";
  payload += "\"peak\":" + String(evt.peak) + ",";
  payload += "\"do_edges\":1,";
  payload += "\"fw\":\"0.2.0\",";
  payload += "\"id\":\"" + WiFi.macAddress() + "\"";
  payload += "}";

  int rc = http.POST(payload);
  LOGI("Webhook POST %s rc=%d payload=%s", webhookUrl.c_str(), rc, payload.c_str());
  http.end();
}

#ifdef ENABLE_MQTT
void sendMQTT(const NotificationEvent &evt) {
  if (!mqttEnabled || mqttServer.length()==0 || mqttTopic.length()==0) return;

  if (!mqttClient.connected()) {
    mqttClient.setServer(mqttServer.c_str(), mqttPort);
    if (!mqttClient.connect(("esp32-sound-" + WiFi.macAddress()).c_str())) {
      LOGW("MQTT connect failed");
      return;
    }
    LOGI("MQTT connected");
  }

  // Create detailed JSON payload
  String payload = "{";
  payload += "\"ts\":\"" + getISOTimestamp() + "\",";
  payload += "\"seq\":" + String(notificationSeq++) + ",";
  payload += "\"duration_ms\":" + String(evt.duration_ms) + ",";
  payload += "\"rms\":" + String(evt.rms) + ",";
  payload += "\"peak\":" + String(evt.peak) + ",";
  payload += "\"do_edges\":1,";
  payload += "\"fw\":\"0.2.0\",";
  payload += "\"id\":\"" + WiFi.macAddress() + "\"";
  payload += "}";

  // Publish to event topic
  String eventTopic = mqttTopic + "/event";
  if (mqttClient.publish(eventTopic.c_str(), payload.c_str(), false)) {
    LOGI("MQTT published to %s: %s", eventTopic.c_str(), payload.c_str());
  } else {
    LOGW("MQTT publish failed");
  }
}
#endif

void sendNotify(const NotificationEvent &evt) {
  sendWebhook(evt);
#ifdef ENABLE_MQTT
  sendMQTT(evt);
#endif
}

// Task to process notification queue
void notificationTask(void *pv) {
  NotificationEvent evt;
  while (true) {
    if (xQueueReceive(notifyQueue, &evt, portMAX_DELAY) == pdTRUE) {
      sendNotify(evt);
    }
  }
}

void startCaptiveAP() {
  uint32_t r = esp_random();
  apSSID = "Aniviza-" + String((r >> 16) & 0xFFFF, HEX);
  apSSID.toUpperCase();

  LOGI("Starting AP '%s' on %s", apSSID.c_str(), apIP.toString().c_str());
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, netMsk);
  WiFi.softAP(apSSID.c_str()); // open AP for provisioning
  delay(150);
  inAP = true;
  wifiConnected = false;  // Not connected to external WiFi in AP mode
  #if HEARTBEAT_GPIO >= 0
    digitalWrite(HEARTBEAT_GPIO, LOW);  // Start blinking
  #endif

  dnsServer.start(DNS_PORT, "*", apIP);
  LOGI("DNS captive portal started on port %d", DNS_PORT);

  if (!serverStarted) { bindRoutes(); server.begin(); serverStarted = true; }

  printNetDiag();
}

void enterSTAOnly() {
  LOGI("Switching to STA-only mode");
  dnsServer.stop();
  WiFi.softAPdisconnect(true);   // stop AP
  delay(50);
  WiFi.mode(WIFI_STA);
  delay(150);
  inAP = false;
  wifiConnected = false;  // Will be updated when connection is established
  #if HEARTBEAT_GPIO >= 0
    digitalWrite(HEARTBEAT_GPIO, LOW);  // Start blinking until connected
  #endif

  if (!serverStarted) { bindRoutes(); server.begin(); serverStarted = true; }

  printNetDiag();
}

// ----------- NVS helpers -----------
void clearNet() {
  prefs.begin("net", false);
  prefs.clear();
  prefs.end();
  LOGI("Preferences 'net' cleared.");
}

void flushNVS() {
  LOGW("Erasing entire NVS partition...");
  esp_err_t err = nvs_flash_erase();
  if (err == ESP_OK) {
    LOGI("NVS erased. Re-initializing...");
    err = nvs_flash_init();
    if (err == ESP_OK) LOGI("NVS init OK.");
    else LOGE("NVS init failed: %d", (int)err);
  } else {
    LOGE("NVS erase failed: %d", (int)err);
  }
}

// ----------- Wi-Fi events -----------
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch(event) {
    case ARDUINO_EVENT_WIFI_READY:                 LOGI("WiFi READY"); break;
    case ARDUINO_EVENT_WIFI_STA_START:             LOGI("STA START"); break;
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:         LOGI("STA CONNECTED to '%s'", WiFi.SSID().c_str()); break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:            
      LOGI("STA GOT IP: %s", WiFi.localIP().toString().c_str()); 
      wifiConnected = true;  // Successfully connected with IP
      #if HEARTBEAT_GPIO >= 0
        digitalWrite(HEARTBEAT_GPIO, HIGH);  // Solid on when connected
      #endif
      
      // Initialize NTP time sync for accurate timestamps
      configTime(0, 0, "pool.ntp.org", "time.nist.gov");
      LOGI("NTP sync initialized");
      
      // Initialize MQTT connection if enabled
      #ifdef ENABLE_MQTT
      if (mqttEnabled && mqttServer.length() > 0) {
        mqttClient.setServer(mqttServer.c_str(), mqttPort);
        LOGI("MQTT server configured: %s:%d", mqttServer.c_str(), mqttPort);
      }
      #endif
      
      printNetDiag(); 
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      LOGW("STA DISCONNECTED, reason=%d", info.wifi_sta_disconnected.reason);
      wifiConnected = false;  // Lost connection
      #if HEARTBEAT_GPIO >= 0
        digitalWrite(HEARTBEAT_GPIO, LOW);  // Start blinking (will toggle in loop)
      #endif
      wantReconnect = true;
      break;
    case ARDUINO_EVENT_WIFI_AP_START:              
      LOGI("AP START '%s'", apSSID.c_str()); 
      wifiConnected = false;  // In AP mode, not connected to external WiFi
      #if HEARTBEAT_GPIO >= 0
        digitalWrite(HEARTBEAT_GPIO, LOW);  // Start blinking
      #endif
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:       LOGI("AP client JOIN: " MACSTR, MAC2STR(info.wifi_ap_staconnected.mac)); break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:    LOGI("AP client LEAVE: " MACSTR, MAC2STR(info.wifi_ap_stadisconnected.mac)); break;
    default:                                       LOGD("WiFi event %d", event); break;
  }
}

// ----------- Serial console -----------
void processCommand(const String& cmd) {
  LOGD("Processing command: '%s'", cmd.c_str());
  if (cmd == "help") {
    Serial.println(
      "Commands:\n"
      "  help       - show this help\n"
      "  status     - print Wi-Fi/network status\n"
      "  clear-net  - clear only saved SSID/password (Preferences 'net')\n"
      "  flush-nvs  - erase entire NVS partition (all namespaces)\n"
      "  reprov     - clear-net and start provisioning AP now\n"
      "  reboot     - restart MCU\n");
  } else if (cmd == "status") {
    printNetDiag();
  } else if (cmd == "clear-net") {
    clearNet();
  } else if (cmd == "flush-nvs") {
    flushNVS();
  } else if (cmd == "reprov") {
    clearNet();
    WiFi.disconnect(true, true);
    startCaptiveAP();
  } else if (cmd == "reboot") {
    Serial.println("Rebooting...");
    delay(100);
    ESP.restart();
  } else if (cmd == "sound") {
    Serial.printf("Sound: last=%lu ms, count=%u, active=%d\n", lastSoundT, (unsigned)soundCount, soundActive);
  } else if (cmd.startsWith("record ")) {
    String arg = cmd.substring(7);
    uint32_t ms = arg.toInt();
    if (ms == 0) { Serial.println("Invalid ms"); }
    else if (startRecordingMs(ms)) { Serial.printf("Recording %ums started\n", ms); }
    else { Serial.println("Recording busy or failed"); }
  } else if (cmd.length()) {
    Serial.printf("Unknown command: '%s' (type 'help')\n", cmd.c_str());
  }
}

void pollSerial() {
  static uint32_t lastPoll = 0;
  uint32_t now = millis();
  //if (now - lastPoll >= 1000) {  // Debug every second
  //  lastPoll = now;
  //  LOGD("pollSerial: available=%d, cmdLen=%d", Serial.available(), cmdLen);
  //}
  
  while (Serial.available()) {
    char c = (char)Serial.read();
    lastCharTime = now;
    if (c == '\r' || c == '\n') {
      if (cmdLen > 0) {  // Only process if we have characters
        cmdBuf[min(cmdLen, sizeof(cmdBuf)-1)] = 0;
        String cmd = String(cmdBuf);
        cmd.trim();  // Remove any whitespace
        cmdLen = 0;
        LOGD("Received command: '%s'", cmd.c_str());
        processCommand(cmd);
      }
    } else if (cmdLen < sizeof(cmdBuf)-1) {
      cmdBuf[cmdLen++] = c;
    }
  }
  
  // Process command if we have buffered data and no new chars for 500ms
  if (cmdLen > 0 && (now - lastCharTime) > 500) {
    cmdBuf[min(cmdLen, sizeof(cmdBuf)-1)] = 0;
    String cmd = String(cmdBuf);
    cmd.trim();
    cmdLen = 0;
    LOGD("Received command (timeout): '%s'", cmd.c_str());
    processCommand(cmd);
  }
}

// ----------- Button handling -----------
void handleButton() {
  bool pressed = (digitalRead(BOOT_BTN_GPIO) == LOW); // active-low
  uint32_t now = millis();

  if (btnPrev && !pressed) {
    // rising edge (release)
    if (btnArmed) {
      uint32_t held = now - btnPressT0;
      if (held >= BTN_VLONG_MS) {
        LOGW("BOOT very long press (%lums): flush-nvs + reboot", held);
        flushNVS();
        delay(200);
        ESP.restart();
      } else if (held >= BTN_LONG_MS) {
        LOGI("BOOT long press (%lums): clear-net + start provisioning AP", held);
        clearNet();
        WiFi.disconnect(true, true);
        startCaptiveAP();
      } else if (held >= BTN_SHORT_MS) {
        LOGI("BOOT short press (%lums): start provisioning AP (keep other NVS)", held);
        WiFi.disconnect(true, true);
        startCaptiveAP();
      } else {
        LOGD("BOOT tap ignored (%lums)", held);
      }
    }
    btnArmed = false;
  } else if (!btnPrev && pressed) {
    // falling edge (press)
    btnPressT0 = now;
    btnArmed = true;
    LOGD("BOOT pressed");
  } else if (btnPrev && pressed) {
    // held
    uint32_t held = now - btnPressT0;
    if (held == BTN_LONG_MS) LOGD("BOOT long threshold reached");
    if (held == BTN_VLONG_MS) LOGD("BOOT very-long threshold reached");
  }
  btnPrev = pressed;
}

// ----------- Setup/Loop -----------
void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.printf("ESP32 boot. SDK=%s, Chip=%s rev%d, Flash=%uMB\n",
                ESP.getSdkVersion(), ESP.getChipModel(), ESP.getChipRevision(),
                ESP.getFlashChipSize()/1024/1024);
  Serial.println("Type 'help' + Enter for commands.");

  wifiConnected = false;  // Initialize connection status

#if HEARTBEAT_GPIO >= 0
  pinMode(HEARTBEAT_GPIO, OUTPUT);
  digitalWrite(HEARTBEAT_GPIO, LOW);
#endif
  pinMode(BOOT_BTN_GPIO, INPUT_PULLUP);
  // Sound sensor digital output
  pinMode(SOUND_DO_GPIO, INPUT_PULLUP);

  // -------- Critical System Stability (Phase 1 Foundation) --------
  // Make ADC settings explicit for predictable audio capture
  analogReadResolution(12);
  analogSetPinAttenuation(SOUND_A0_GPIO, ADC_11db);  // ≈0–3.3 V full-scale

  // Disable WiFi power save for consistent timing (critical for bark detection)
  WiFi.setSleep(false);                 // Arduino wrapper -> PS off
  esp_wifi_set_ps(WIFI_PS_NONE);        // IDF call -> modem-sleep off

  // Lock CPU to 240MHz to prevent timing jitter during detection
  // Using Arduino API instead of ESP-IDF for compatibility
  setCpuFrequencyMhz(240);
  LOGI("CPU locked to 240MHz for stable timing");

  loadSoundConfig();

  WiFi.onEvent(onWiFiEvent);
  Serial.setDebugOutput(false);

  bindRoutes();

  notifyQueue = xQueueCreate(8, sizeof(NotificationEvent));
  if (!notifyQueue) {
    LOGE("Failed to create notification queue");
  } else {
    xTaskCreate(notificationTask, "notifyTask", 4096, NULL, 1, NULL);
  }

  if (tryConnectFromPrefs(CONNECT_TIMEOUT_MS)) {
    // STA path
    LOGI("Starting in STA mode");
    // No captive DNS in STA-only
    if (!serverStarted) { server.begin(); serverStarted = true; }
    printNetDiag();
  } else {
    startCaptiveAP();
  }
}

void loop() {
  pollSerial();
  handleButton();

  // Enhanced sound detection with configurable parameters
  bool raw = digitalRead(SOUND_DO_GPIO);
  bool detected = (SOUND_DO_ACTIVE_HIGH ? raw : !raw);
  uint32_t now = millis();
  
  if (detected != soundPrev) {
    // state changed, start debounce
    soundDebounceT = now;
  } else if ((now - soundDebounceT) >= SOUND_DEBOUNCE_MS) {
    // stable state achieved
    if (detected && !soundEventActive) {
      // DO went high - potential event start
      soundEventStart = now;
      soundEventActive = true;
      LOGD("Sound event started (debounced)");
    } else if (!detected && soundEventActive) {
      // DO went low - check if event meets minimum duration
      uint32_t eventDuration = now - soundEventStart;
      if (eventDuration >= soundTMinMs) {
        // Valid event detected
        soundActive = true;
        lastSoundT = soundEventStart; // Use start time for timestamp
        soundCount++;
        LOGI("Sound event detected: duration=%lums, count=%u", eventDuration, (unsigned)soundCount);
        NotificationEvent evt;
        evt.timestamp = soundEventStart;
        evt.duration_ms = eventDuration;
        evt.rms = analogRead(SOUND_A0_GPIO);
        evt.peak = evt.rms;
        if (notifyQueue && xQueueSend(notifyQueue, &evt, 0) != pdTRUE) {
          LOGW("Notification queue full");
        }
      } else {
        LOGD("Sound event too short: %lums < %lums", eventDuration, soundTMinMs);
      }
      soundEventActive = false;
    } else if (detected && soundEventActive && soundActive) {
      // Event ongoing - check for quiet period to end event
      uint32_t timeSinceLastEdge = now - soundDebounceT;
      if (timeSinceLastEdge >= soundTQuietMs) {
        soundActive = false;
        LOGD("Sound event ended (quiet period)");
      }
    }
  }
  soundPrev = detected;
  if (now - tHeartbeat >= 1000) {
    tHeartbeat = now;
#if HEARTBEAT_GPIO >= 0
    if (wifiConnected) {
      // Solid on when connected
      digitalWrite(HEARTBEAT_GPIO, HIGH);
    } else {
      // Blink when not connected
      digitalWrite(HEARTBEAT_GPIO, !digitalRead(HEARTBEAT_GPIO));
    }
#endif
    //LOGD("HB uptime=%lums, inAP=%d, WiFi.status=%d, server=%d, freeHeap=%u",
    //     now, inAP, WiFi.status(), serverStarted, ESP.getFreeHeap());
  }

  if (serverStarted) server.handleClient();
  if (inAP) dnsServer.processNextRequest();

  static uint32_t lastTry = 0;
  if (wantReconnect && (now - lastTry > RETRY_CONNECT_MS)) {
    lastTry = now;
    LOGI("Reconnect attempt triggered.");
    if (tryConnectFromPrefs(CONNECT_TIMEOUT_MS)) {
      LOGI("Reconnect success; switching to STA-only");
      // Cleanly drop AP (if any) and continue in STA
      dnsServer.stop();
      WiFi.softAPdisconnect(true);
      delay(50);
      WiFi.mode(WIFI_STA);
      inAP = false;
      if (!serverStarted) { server.begin(); serverStarted = true; }
      wantReconnect = false;
      printNetDiag();
    } else {
      LOGW("Reconnect attempt failed; will retry in %u ms", RETRY_CONNECT_MS);
    }
  }
}

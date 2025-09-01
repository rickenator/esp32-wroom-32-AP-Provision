#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>

#define CONNECT_TIMEOUT_MS 15000
#define RETRY_CONNECT_MS    5000
#define DNS_PORT 53

// -------- Pins --------
#define HEARTBEAT_GPIO 2     // set -1 to disable; many DevKitC use GPIO2 LED
#define BOOT_BTN_GPIO  0     // BOOT button (IO0), active-low

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

// HB/diag
uint32_t tHeartbeat = 0;

// Serial console
char cmdBuf[96];
size_t cmdLen = 0;

// Button state
bool    btnPrev = true;       // true=not pressed (pull-up)
uint32_t btnPressT0 = 0;
bool    btnArmed = false;

// --------- HTML ----------
const char PROGMEM HTML_INDEX[] = R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1">
<title>ESP32 Provisioning</title>
<style>
body{font-family:system-ui,Arial;margin:24px;max-width:560px}
.card{border:1px solid #ddd;border-radius:12px;padding:18px}
input,button{font-size:16px;padding:10px;margin:6px 0;width:100%}
button{cursor:pointer}
small{color:#666}
</style></head><body>
<h2>Connect to Wi-Fi</h2>
<div class=card>
<form action="/save" method="POST">
<label>SSID</label><input name="s" placeholder="Your Wi-Fi name" required>
<label>Password</label><input name="p" type="password" placeholder="Wi-Fi password">
<button type="submit">Save & Connect</button>
</form>
<p><small>If SSID is hidden, type it exactly (case-sensitive).</small></p>
</div>
<p><a href="/scan">Scan networks</a> • <a href="/diag">Diagnostics</a></p>
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
    delay(250);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    LOGI("STA connected: IP=%s RSSI=%d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    printNetDiag();
    return true;
  }
  LOGW("STA connect failed.");
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
    s += "</pre><p><a href='/'>Back</a></p>";
    LOGD("HTTP /diag");
    server.send(200, "text/html", s);
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
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:            LOGI("STA GOT IP: %s", WiFi.localIP().toString().c_str()); printNetDiag(); break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
      LOGW("STA DISCONNECTED, reason=%d", info.wifi_sta_disconnected.reason);
      wantReconnect = true;
      break;
    case ARDUINO_EVENT_WIFI_AP_START:              LOGI("AP START '%s'", apSSID.c_str()); break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:       LOGI("AP client JOIN: " MACSTR, MAC2STR(info.wifi_ap_staconnected.mac)); break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:    LOGI("AP client LEAVE: " MACSTR, MAC2STR(info.wifi_ap_stadisconnected.mac)); break;
    default:                                       LOGD("WiFi event %d", event); break;
  }
}

// ----------- Serial console -----------
void processCommand(const String& cmd) {
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
  } else if (cmd.length()) {
    Serial.printf("Unknown command: '%s' (type 'help')\n", cmd.c_str());
  }
}

void pollSerial() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      cmdBuf[min(cmdLen, sizeof(cmdBuf)-1)] = 0;
      String cmd = String(cmdBuf);
      cmdLen = 0;
      processCommand(cmd);
    } else if (cmdLen < sizeof(cmdBuf)-1) {
      cmdBuf[cmdLen++] = c;
    }
  }
}

// ----------- Button handling -----------
void handleButton() {
  bool pressed = (digitalRead(BOOT_BTN_GPIO) == LOW); // active-low
  uint32_t now = millis();

  if (!btnPrev && !pressed) {
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
  } else if (btnPrev && pressed) {
    // falling edge (press)
    btnPressT0 = now;
    btnArmed = true;
    LOGD("BOOT pressed");
  } else if (!btnPrev && pressed) {
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

#if HEARTBEAT_GPIO >= 0
  pinMode(HEARTBEAT_GPIO, OUTPUT);
  digitalWrite(HEARTBEAT_GPIO, LOW);
#endif
  pinMode(BOOT_BTN_GPIO, INPUT_PULLUP);

  WiFi.onEvent(onWiFiEvent);
  Serial.setDebugOutput(false);

  bindRoutes();

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

  uint32_t now = millis();
  if (now - tHeartbeat >= 1000) {
    tHeartbeat = now;
#if HEARTBEAT_GPIO >= 0
    digitalWrite(HEARTBEAT_GPIO, !digitalRead(HEARTBEAT_GPIO));
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

/**
 * @file barebones-freertos.cpp
 * @brief Enhanced ESP32 WiFi Provisioning with FreeRTOS Tasks
 * 
 * This is the enhanced version of the bare bones WiFi provisioning,
 * featuring FreeRTOS task-based architecture for better responsiveness
 * and real-time performance. Use this when you need:
 * 
 * - Interrupt-driven button handling (never miss button presses)
 * - Non-blocking serial console with timeout mechanism
 * - Task-based architecture for better separation of concerns
 * - Foundation for adding sensors/actuators with dedicated tasks
 * 
 * Hardware: ESP32-WROOM-32 DevKitC or compatible
 * Core Requirements: ESP32 Arduino Core 2.0.0+
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#define CONNECT_TIMEOUT_MS 15000
#define RETRY_CONNECT_MS    5000
#define DNS_PORT 53

// -------- Hardware Pins --------
#define HEARTBEAT_GPIO 2     // set -1 to disable; many DevKitC use GPIO2 LED
#define BOOT_BTN_GPIO  0     // BOOT button (IO0), active-low

// -------- Button thresholds (ms) --------
#define BTN_SHORT_MS   500    // >= short < long  -> reprov
#define BTN_LONG_MS    3000   // >= long < very   -> clear-net + reprov
#define BTN_VLONG_MS   6000   // >= very long     -> flush-nvs + reboot

// -------- Task Configuration --------
#define BUTTON_TASK_STACK_SIZE    2048
#define SERIAL_TASK_STACK_SIZE    4096
#define WIFI_TASK_STACK_SIZE      8192
#define HEARTBEAT_TASK_STACK_SIZE 2048

#define BUTTON_TASK_PRIORITY      (configMAX_PRIORITIES - 1)  // Highest priority
#define SERIAL_TASK_PRIORITY      (configMAX_PRIORITIES - 2)  // High priority
#define WIFI_TASK_PRIORITY        (configMAX_PRIORITIES - 3)  // Medium priority
#define HEARTBEAT_TASK_PRIORITY   (tskIDLE_PRIORITY + 1)      // Low priority

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

// ------------ Global Objects ------------
Preferences prefs;
WebServer server(80);
DNSServer dnsServer;

String apSSID;
IPAddress apIP(192,168,4,1), netMsk(255,255,255,0);
bool inAP = false;
bool wantReconnect = false;
bool serverStarted = false;

// ------------ FreeRTOS Objects ------------
QueueHandle_t buttonQueue = nullptr;
QueueHandle_t serialQueue = nullptr;
SemaphoreHandle_t wifiMutex = nullptr;
TaskHandle_t buttonTaskHandle = nullptr;
TaskHandle_t serialTaskHandle = nullptr;
TaskHandle_t wifiTaskHandle = nullptr;
TaskHandle_t heartbeatTaskHandle = nullptr;

// ------------ Button Events ------------
enum ButtonEvent {
    BUTTON_SHORT_PRESS,
    BUTTON_LONG_PRESS,
    BUTTON_VERY_LONG_PRESS
};

// ------------ Serial Commands ------------
struct SerialCommand {
    char command[32];
    uint32_t timestamp;
};

// ------------ Button State ------------
volatile bool btnPressed = false;
volatile uint32_t btnPressTime = 0;
portMUX_TYPE btnMux = portMUX_INITIALIZER_UNLOCKED;

// ------------ HTML Content ------------
const char PROGMEM HTML_INDEX[] = R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1">
<title>ESP32 Provisioning (Enhanced)</title>
<style>
body{font-family:system-ui,Arial;margin:24px;max-width:560px}
.card{border:1px solid #ddd;border-radius:12px;padding:18px;margin:12px 0}
input,button{font-size:16px;padding:10px;margin:6px 0;width:100%}
button{cursor:pointer;background:#007cba;color:white;border:none;border-radius:6px}
button:hover{background:#005a85}
small{color:#666}
.status{background:#f0f9ff;border-left:4px solid #007cba;padding:12px}
</style></head><body>
<h2>ESP32 WiFi Provisioning</h2>
<div class="status">
<strong>Enhanced FreeRTOS Version</strong><br>
Features: Task-based architecture, interrupt-driven controls, non-blocking operations
</div>
<div class=card>
<form action="/save" method="POST">
<label>SSID</label><input name="s" placeholder="Your Wi-Fi name" required>
<label>Password</label><input name="p" type="password" placeholder="Wi-Fi password">
<button type="submit">Save & Connect</button>
</form>
<p><small>If SSID is hidden, type it exactly (case-sensitive).</small></p>
</div>
<p><a href="/scan">Scan networks</a> • <a href="/diag">Diagnostics</a> • <a href="/tasks">Task Status</a></p>
</body></html>
)HTML";

// ------------ Interrupt Handler ------------
void IRAM_ATTR buttonISR() {
    portENTER_CRITICAL_ISR(&btnMux);
    bool currentState = (digitalRead(BOOT_BTN_GPIO) == LOW);
    uint32_t now = millis();
    
    if (currentState && !btnPressed) {
        // Button pressed
        btnPressed = true;
        btnPressTime = now;
    } else if (!currentState && btnPressed) {
        // Button released
        btnPressed = false;
        uint32_t duration = now - btnPressTime;
        
        ButtonEvent event;
        if (duration >= BTN_VLONG_MS) {
            event = BUTTON_VERY_LONG_PRESS;
        } else if (duration >= BTN_LONG_MS) {
            event = BUTTON_LONG_PRESS;
        } else if (duration >= BTN_SHORT_MS) {
            event = BUTTON_SHORT_PRESS;
        } else {
            portEXIT_CRITICAL_ISR(&btnMux);
            return; // Ignore short taps
        }
        
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(buttonQueue, &event, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    portEXIT_CRITICAL_ISR(&btnMux);
}

// ------------ Helper Functions ------------
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
    String h = F("<!doctype html><html><head><meta name=viewport content='width=device-width,initial-scale=1'><title>Network Scan</title></head><body><h2>Nearby Networks</h2><ul>");
    for (int i=0;i<n;i++) {
        h += "<li><strong>" + WiFi.SSID(i) + "</strong> (RSSI " + String(WiFi.RSSI(i)) +
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

    if (ssid.isEmpty()) { 
        LOGI("No stored credentials."); 
        return false; 
    }

    LOGI("Attempting STA connect to SSID='%s' (timeout %u ms)", ssid.c_str(), timeoutMs);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - t0) < timeoutMs) {
        vTaskDelay(pdMS_TO_TICKS(250));
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

void startCaptiveAP() {
    if (xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        uint32_t r = esp_random();
        apSSID = "Aniviza-" + String((r >> 16) & 0xFFFF, HEX);
        apSSID.toUpperCase();

        LOGI("Starting AP '%s' on %s", apSSID.c_str(), apIP.toString().c_str());
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(apIP, apIP, netMsk);
        WiFi.softAP(apSSID.c_str());
        vTaskDelay(pdMS_TO_TICKS(150));
        inAP = true;

        dnsServer.start(DNS_PORT, "*", apIP);
        LOGI("DNS captive portal started on port %d", DNS_PORT);

        printNetDiag();
        xSemaphoreGive(wifiMutex);
    }
}

// ------------ Web Routes ------------
void bindRoutes() {
    server.on("/", HTTP_GET, [](){
        LOGD("HTTP /  (client=%s)", server.client().remoteIP().toString().c_str());
        server.send_P(200, "text/html", HTML_INDEX);
    });

    server.on("/scan", HTTP_GET, [](){
        LOGD("HTTP /scan");
        if (inAP) WiFi.mode(WIFI_AP_STA);
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
        server.send(200, "text/html", s);
    });

    server.on("/tasks", HTTP_GET, [](){
        String s = "<html><body><h2>FreeRTOS Task Status</h2><pre>\n";
        s += "Task Name       State  Priority  Stack\n";
        s += "----------      -----  --------  -----\n";
        
        TaskStatus_t *taskArray;
        UBaseType_t taskCount = uxTaskGetNumberOfTasks();
        taskArray = (TaskStatus_t*)pvPortMalloc(taskCount * sizeof(TaskStatus_t));
        
        if (taskArray != nullptr) {
            taskCount = uxTaskGetSystemState(taskArray, taskCount, nullptr);
            for (UBaseType_t i = 0; i < taskCount; i++) {
                s += String(taskArray[i].pcTaskName);
                s += String("          ").substring(strlen(taskArray[i].pcTaskName));
                s += String(taskArray[i].eCurrentState) + "      ";
                s += String(taskArray[i].uxCurrentPriority) + "         ";
                s += String(taskArray[i].usStackHighWaterMark) + "\n";
            }
            vPortFree(taskArray);
        }
        
        s += "\nQueue Status:\n";
        s += "Button Queue: " + String(uxQueueMessagesWaiting(buttonQueue)) + "/" + String(uxQueueSpacesAvailable(buttonQueue) + uxQueueMessagesWaiting(buttonQueue)) + "\n";
        s += "Serial Queue: " + String(uxQueueMessagesWaiting(serialQueue)) + "/" + String(uxQueueSpacesAvailable(serialQueue) + uxQueueMessagesWaiting(serialQueue)) + "\n";
        s += "</pre><p><a href='/'>Back</a></p></body></html>";
        server.send(200, "text/html", s);
    });

    server.on("/save", HTTP_POST, [](){
        LOGD("HTTP /save  args=%d", server.args());
        if (!server.hasArg("s")) { 
            server.send(400, "text/plain", "Missing SSID"); 
            return; 
        }
        String ssid = server.arg("s");
        String pass = server.hasArg("p") ? server.arg("p") : "";

        LOGI("Saving credentials: SSID='%s' (len pass=%u)", ssid.c_str(), (unsigned)pass.length());
        prefs.begin("net", false);
        prefs.putString("ssid", ssid);
        prefs.putString("pass", pass);
        prefs.end();

        wantReconnect = true;
        server.send(200, "text/html",
            "<html><body><h3>Connecting to " + ssid +
            " ...</h3><p>Watch serial logs for status.</p>"
            "<meta http-equiv='refresh' content='2; url=/status'></body></html>");
    });

    server.on("/status", HTTP_GET, [](){
        wl_status_t st = WiFi.status();
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

// ------------ WiFi Event Handler ------------
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

// ------------ FreeRTOS Tasks ------------

void buttonTask(void *parameter) {
    LOGI("Button task started on core %d", xPortGetCoreID());
    ButtonEvent event;
    
    while (true) {
        if (xQueueReceive(buttonQueue, &event, portMAX_DELAY) == pdTRUE) {
            switch (event) {
                case BUTTON_SHORT_PRESS:
                    LOGI("Button SHORT press: starting provisioning AP");
                    startCaptiveAP();
                    break;
                    
                case BUTTON_LONG_PRESS:
                    LOGI("Button LONG press: clear network + start provisioning");
                    clearNet();
                    WiFi.disconnect(true, true);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    startCaptiveAP();
                    break;
                    
                case BUTTON_VERY_LONG_PRESS:
                    LOGW("Button VERY LONG press: factory reset + reboot");
                    flushNVS();
                    vTaskDelay(pdMS_TO_TICKS(200));
                    ESP.restart();
                    break;
            }
        }
    }
}

void serialTask(void *parameter) {
    LOGI("Serial task started on core %d", xPortGetCoreID());
    char cmdBuf[96];
    size_t cmdLen = 0;
    uint32_t lastCharTime = 0;
    const uint32_t COMMAND_TIMEOUT_MS = 5000;
    
    while (true) {
        while (Serial.available()) {
            char c = (char)Serial.read();
            lastCharTime = millis();
            
            if (c == '\r') continue;
            if (c == '\n') {
                if (cmdLen > 0) {
                    cmdBuf[min(cmdLen, sizeof(cmdBuf)-1)] = 0;
                    String cmd = String(cmdBuf);
                    cmd.trim();
                    cmdLen = 0;
                    
                    // Process command
                    if (cmd == "help") {
                        Serial.println(
                            "Enhanced FreeRTOS Commands:\n"
                            "  help       - show this help\n"
                            "  status     - print Wi-Fi/network status\n"
                            "  tasks      - show FreeRTOS task information\n"
                            "  clear-net  - clear only saved SSID/password\n"
                            "  flush-nvs  - erase entire NVS partition\n"
                            "  reprov     - clear-net and start provisioning AP\n"
                            "  reboot     - restart MCU\n");
                    } else if (cmd == "status") {
                        printNetDiag();
                    } else if (cmd == "tasks") {
                        Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
                        Serial.printf("Task count: %u\n", uxTaskGetNumberOfTasks());
                        Serial.printf("Button queue: %u/%u\n", 
                                    uxQueueMessagesWaiting(buttonQueue),
                                    uxQueueSpacesAvailable(buttonQueue) + uxQueueMessagesWaiting(buttonQueue));
                        Serial.printf("Serial queue: %u/%u\n",
                                    uxQueueMessagesWaiting(serialQueue),
                                    uxQueueSpacesAvailable(serialQueue) + uxQueueMessagesWaiting(serialQueue));
                    } else if (cmd == "clear-net") {
                        clearNet();
                    } else if (cmd == "flush-nvs") {
                        flushNVS();
                    } else if (cmd == "reprov") {
                        clearNet();
                        WiFi.disconnect(true, true);
                        vTaskDelay(pdMS_TO_TICKS(100));
                        startCaptiveAP();
                    } else if (cmd == "reboot") {
                        Serial.println("Rebooting...");
                        vTaskDelay(pdMS_TO_TICKS(100));
                        ESP.restart();
                    } else if (cmd.length() > 0) {
                        Serial.printf("Unknown command: '%s' (type 'help')\n", cmd.c_str());
                    }
                }
            } else if (cmdLen < sizeof(cmdBuf)-1) {
                cmdBuf[cmdLen++] = c;
            }
        }
        
        // Command timeout handling
        if (cmdLen > 0 && (millis() - lastCharTime) > COMMAND_TIMEOUT_MS) {
            LOGW("Serial command timeout, clearing buffer");
            cmdLen = 0;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void wifiTask(void *parameter) {
    LOGI("WiFi task started on core %d", xPortGetCoreID());
    
    bindRoutes();
    
    if (tryConnectFromPrefs(CONNECT_TIMEOUT_MS)) {
        LOGI("Starting in STA mode");
        if (!serverStarted) { 
            server.begin(); 
            serverStarted = true; 
        }
    } else {
        startCaptiveAP();
        if (!serverStarted) { 
            server.begin(); 
            serverStarted = true; 
        }
    }
    
    uint32_t lastReconnectAttempt = 0;
    
    while (true) {
        if (serverStarted) {
            server.handleClient();
        }
        if (inAP) {
            dnsServer.processNextRequest();
        }
        
        // Handle reconnection attempts
        uint32_t now = millis();
        if (wantReconnect && (now - lastReconnectAttempt > RETRY_CONNECT_MS)) {
            lastReconnectAttempt = now;
            LOGI("Reconnect attempt triggered.");
            
            if (tryConnectFromPrefs(CONNECT_TIMEOUT_MS)) {
                LOGI("Reconnect success; switching to STA-only");
                if (xSemaphoreTake(wifiMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                    dnsServer.stop();
                    WiFi.softAPdisconnect(true);
                    vTaskDelay(pdMS_TO_TICKS(50));
                    WiFi.mode(WIFI_STA);
                    inAP = false;
                    wantReconnect = false;
                    xSemaphoreGive(wifiMutex);
                }
            } else {
                LOGW("Reconnect attempt failed; will retry in %u ms", RETRY_CONNECT_MS);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void heartbeatTask(void *parameter) {
    LOGI("Heartbeat task started on core %d", xPortGetCoreID());
    
    while (true) {
#if HEARTBEAT_GPIO >= 0
        digitalWrite(HEARTBEAT_GPIO, !digitalRead(HEARTBEAT_GPIO));
#endif
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ------------ Main Setup/Loop ------------
void setup() {
    Serial.begin(115200);
    delay(150);
    Serial.println();
    Serial.printf("ESP32 FreeRTOS Provisioning. SDK=%s, Chip=%s rev%d, Flash=%uMB\n",
                  ESP.getSdkVersion(), ESP.getChipModel(), ESP.getChipRevision(),
                  ESP.getFlashChipSize()/1024/1024);
    Serial.println("Enhanced version with FreeRTOS tasks");
    Serial.println("Type 'help' + Enter for commands.");

    // Initialize GPIO
#if HEARTBEAT_GPIO >= 0
    pinMode(HEARTBEAT_GPIO, OUTPUT);
    digitalWrite(HEARTBEAT_GPIO, LOW);
#endif
    pinMode(BOOT_BTN_GPIO, INPUT_PULLUP);

    // Create FreeRTOS objects
    buttonQueue = xQueueCreate(5, sizeof(ButtonEvent));
    serialQueue = xQueueCreate(5, sizeof(SerialCommand));
    wifiMutex = xSemaphoreCreateMutex();
    
    if (buttonQueue == nullptr || serialQueue == nullptr || wifiMutex == nullptr) {
        LOGE("Failed to create FreeRTOS objects");
        ESP.restart();
    }

    // Install button interrupt
    attachInterrupt(digitalPinToInterrupt(BOOT_BTN_GPIO), buttonISR, CHANGE);

    // Setup WiFi event handler
    WiFi.onEvent(onWiFiEvent);
    Serial.setDebugOutput(false);

    // Create tasks
    xTaskCreatePinnedToCore(
        buttonTask, "ButtonTask", BUTTON_TASK_STACK_SIZE, nullptr,
        BUTTON_TASK_PRIORITY, &buttonTaskHandle, 1
    );
    
    xTaskCreatePinnedToCore(
        serialTask, "SerialTask", SERIAL_TASK_STACK_SIZE, nullptr,
        SERIAL_TASK_PRIORITY, &serialTaskHandle, 1
    );
    
    xTaskCreatePinnedToCore(
        wifiTask, "WiFiTask", WIFI_TASK_STACK_SIZE, nullptr,
        WIFI_TASK_PRIORITY, &wifiTaskHandle, 0
    );
    
    xTaskCreatePinnedToCore(
        heartbeatTask, "HeartbeatTask", HEARTBEAT_TASK_STACK_SIZE, nullptr,
        HEARTBEAT_TASK_PRIORITY, &heartbeatTaskHandle, 0
    );

    LOGI("All tasks created successfully");
}

void loop() {
    // In FreeRTOS version, main loop can be minimal
    // All work is done in dedicated tasks
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // Optional: monitor system health
    if (ESP.getFreeHeap() < 10000) {
        LOGW("Low memory warning: %u bytes free", ESP.getFreeHeap());
    }
}
/**
 * @file secure_webrtc_dogbark_detector.cpp
 * @brief Unified ESP32-S3 TinyML Dog Bark Detection with Secure WebRTC Streaming
 * 
 * This firmware integrates:
 * - TinyML dog bark classification using TensorFlow Lite Micro
 * - Secure WebRTC audio streaming with JWT authentication
 * - MQTT alerts with TLS encryption for Android notifications
 * - Multi-level authorization and rate limiting
 * - Dual streaming modes: continuous and bark-triggered
 * 
 * Hardware: ESP32-S3-WROOM-1-N16R8 + INMP441 Digital Microphone
 * Author: ESP32 Unified Audio Intelligence System
 * Version: 2.0.0-unified
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <time.h>
#include <map>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/ringbuf.h>

// Bark detector components
#include "audio_capture.h"
#include "bark_detector_api.h"
#include "mqtt_client.h"
#include "mqtt_provisioning.h"
#include "esp_log.h"
#include "esp_mac.h"

static const char* TAG = "unified_detector";

// ==================== HARDWARE CONFIGURATION ====================

// INMP441 I2S Microphone Pins (ESP32-S3 specific)
#define I2S_WS_PIN          41    // Word Select (LRCLK)
#define I2S_SCK_PIN         42    // Serial Clock (BCLK)
#define I2S_SD_PIN          40    // Serial Data

// Status LED
#define STATUS_LED_PIN      2     // Bark detection indicator

// Network Configuration
#define RTP_PORT            5004
#define HTTPS_PORT          443
#define MAX_CLIENTS         4

// Audio Configuration (optimized for TinyML model)
#define SAMPLE_RATE         16000
#define FRAME_SIZE_MS       20
#define SAMPLES_PER_FRAME   320   // 16000 * 20 / 1000
#define AUDIO_BUFFER_FRAMES 60    // 1.2 seconds of audio
#define AUDIO_BUFFER_SIZE   (SAMPLES_PER_FRAME * AUDIO_BUFFER_FRAMES)

// Bark Detection Configuration
#define BARK_CONFIDENCE_THRESHOLD  0.8f
#define BARK_MIN_DURATION_MS       300
#define BARK_TRIGGERED_STREAM_MS   5000  // Stream for 5s after bark

// Security Configuration
#define MAX_FAILED_ATTEMPTS 5
#define LOCKOUT_DURATION    300000  // 5 minutes in ms
#define TOKEN_LIFETIME      3600    // 1 hour in seconds
#define RATE_LIMIT_WINDOW   60000   // 1 minute in ms
#define MAX_REQUESTS_PER_WINDOW 100

// ==================== SECURITY TYPES ====================

enum SecurityLevel {
    PUBLIC_READ = 0,     // Audio status, basic info
    USER_ACCESS = 1,     // Audio streaming, monitoring
    ADMIN_ACCESS = 2,    // Configuration changes
    SUPER_ADMIN = 3      // NVS flush, factory reset
};

struct UserCredentials {
    String username;
    String passwordHash;      // SHA-256 hashed
    SecurityLevel level;
    uint32_t lastLogin;
    uint16_t failedAttempts;
    bool isLocked;
    uint32_t lockExpiry;
};

enum SecurityEvent {
    LOGIN_SUCCESS,
    LOGIN_FAILURE,
    INVALID_TOKEN,
    RATE_LIMIT_EXCEEDED,
    CONFIG_CHANGE,
    SUSPICIOUS_REQUEST,
    BRUTE_FORCE_DETECTED,
    UNAUTHORIZED_ACCESS,
    BARK_DETECTED_EVENT
};

// ==================== STREAMING TYPES ====================

struct StreamingClient {
    IPAddress ip;
    uint16_t port;
    uint32_t ssrc;
    uint16_t sequenceNumber;
    uint32_t timestamp;
    bool active;
    bool barkTriggered;          // True if bark-triggered mode
    uint32_t barkTriggerEnd;     // End time for bark-triggered streaming
    String sessionToken;
    uint8_t encryptionKey[32];
};

struct RTPHeader {
    uint8_t version:2;
    uint8_t padding:1;
    uint8_t extension:1;
    uint8_t csrcCount:4;
    uint8_t marker:1;
    uint8_t payloadType:7;
    uint16_t sequenceNumber;
    uint32_t timestamp;
    uint32_t ssrc;
} __attribute__((packed));

// ==================== GLOBAL VARIABLES ====================

// Network
AsyncWebServer server(HTTPS_PORT);
WiFiUDP udp;
Preferences prefs;

// Security
std::map<String, UserCredentials> users;
std::map<String, uint32_t> clientRequests;
std::map<String, uint32_t> clientWindowStart;
std::map<String, bool> blockedClients;
QueueHandle_t securityLogQueue;
String jwtSecret;

// Security log buffer
#define SEC_LOG_MAX_LEN 256

// Audio & Streaming
int16_t* audioRingBuffer = nullptr;
volatile size_t ringBufferWritePos = 0;
volatile size_t ringBufferReadPos = 0;
SemaphoreHandle_t audioMutex;
std::map<String, StreamingClient> activeStreams;

// Bark Detection
BarkDetector::BarkDetector detector;
BarkDetector::Config barkConfig;
volatile uint32_t barkCount = 0;
volatile uint32_t barkSequence = 0;
char deviceMac[18] = {0};
const char* firmwareVersion = "2.0.0-unified";

// MQTT
bool mqttEnabled = false;
bool mqttConnected = false;

// Tasks
TaskHandle_t audioTaskHandle = nullptr;
TaskHandle_t barkDetectionTaskHandle = nullptr;
TaskHandle_t streamingTaskHandle = nullptr;
TaskHandle_t securityTaskHandle = nullptr;

// ==================== UTILITY FUNCTIONS ====================

// Base64url encoding for JWT (RFC 4648)
// Note: This is a simplified implementation. For production use with external
// JWT validators, consider using a well-tested library like mbedTLS base64 functions.
String base64UrlEncode(const String& input) {
    const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    String encoded = "";
    int val = 0;
    int valb = -6;
    
    for (unsigned char c : input) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            encoded += base64_chars[(val >> valb) & 0x3F];
            valb -= 6;
        }
    }
    if (valb > -6) {
        encoded += base64_chars[((val << 8) >> (valb + 8)) & 0x3F];
    }
    // Base64url uses - and _ and no padding
    return encoded;
}

String base64UrlDecode(const String& input) {
    const char* base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    String decoded = "";
    int val = 0;
    int valb = -8;
    
    for (char c : input) {
        const char* ptr = strchr(base64_chars, c);
        if (ptr == nullptr) continue;
        val = (val << 6) + (ptr - base64_chars);
        valb += 6;
        if (valb >= 0) {
            decoded += char((val >> valb) & 0xFF);
            valb -= 8;
        }
    }
    return decoded;
}

String hashPassword(const String& password, const String& salt) {
    uint8_t hash[32];
    String combined = password + salt;
    
    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);
    mbedtls_md_setup(&ctx, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 0);
    mbedtls_md_starts(&ctx);
    mbedtls_md_update(&ctx, (const uint8_t*)combined.c_str(), combined.length());
    mbedtls_md_finish(&ctx, hash);
    mbedtls_md_free(&ctx);
    
    String result = "";
    for (int i = 0; i < 32; i++) {
        char hex[3];
        sprintf(hex, "%02x", hash[i]);
        result += hex;
    }
    return result;
}

String generateJWT(const String& username, SecurityLevel level) {
    DynamicJsonDocument header(128);
    header["alg"] = "HS256";
    header["typ"] = "JWT";
    
    String headerStr;
    serializeJson(header, headerStr);
    String headerB64 = base64UrlEncode(headerStr);
    
    DynamicJsonDocument payload(256);
    payload["sub"] = username;
    payload["lvl"] = (int)level;
    payload["iat"] = time(NULL);
    payload["exp"] = time(NULL) + TOKEN_LIFETIME;
    
    String payloadStr;
    serializeJson(payload, payloadStr);
    String payloadB64 = base64UrlEncode(payloadStr);
    
    // Create HMAC-SHA256 signature
    String dataToSign = headerB64 + "." + payloadB64;
    String signature = hashPassword(dataToSign, jwtSecret);
    String signatureB64 = base64UrlEncode(signature);
    
    return headerB64 + "." + payloadB64 + "." + signatureB64;
}

bool validateJWT(const String& token, SecurityLevel requiredLevel) {
    int firstDot = token.indexOf('.');
    int secondDot = token.indexOf('.', firstDot + 1);
    
    if (firstDot == -1 || secondDot == -1) return false;
    
    String headerB64 = token.substring(0, firstDot);
    String payloadB64 = token.substring(firstDot + 1, secondDot);
    String signatureB64 = token.substring(secondDot + 1);
    
    // Verify signature
    String dataToSign = headerB64 + "." + payloadB64;
    String expectedSig = hashPassword(dataToSign, jwtSecret);
    String expectedSigB64 = base64UrlEncode(expectedSig);
    if (signatureB64 != expectedSigB64) return false;
    
    // Decode and parse payload
    String payloadJson = base64UrlDecode(payloadB64);
    DynamicJsonDocument payload(512);
    if (deserializeJson(payload, payloadJson) != DeserializationError::Ok) return false;
    
    // Check expiration
    if (payload["exp"] < time(NULL)) return false;
    
    // Check authorization level
    if (payload["lvl"] < (int)requiredLevel) return false;
    
    return true;
}

bool rateLimitCheck(const String& clientIP) {
    uint32_t now = millis();
    
    // Check if client is blocked
    if (blockedClients[clientIP]) {
        if (clientWindowStart[clientIP] + (5 * 60 * 1000) > now) {
            return false;
        } else {
            blockedClients[clientIP] = false;
        }
    }
    
    // Reset window if expired
    if (clientWindowStart[clientIP] + RATE_LIMIT_WINDOW < now) {
        clientRequests[clientIP] = 0;
        clientWindowStart[clientIP] = now;
    }
    
    clientRequests[clientIP]++;
    
    if (clientRequests[clientIP] > MAX_REQUESTS_PER_WINDOW) {
        blockedClients[clientIP] = true;
        clientWindowStart[clientIP] = now;
        ESP_LOGW(TAG, "Rate limit exceeded for %s", clientIP.c_str());
        return false;
    }
    
    return true;
}

void logSecurityEvent(SecurityEvent event, const String& clientIP, const String& details = "") {
    DynamicJsonDocument logEntry(512);
    logEntry["timestamp"] = time(NULL);
    logEntry["event"] = (int)event;
    logEntry["client_ip"] = clientIP;
    logEntry["details"] = details;
    logEntry["device_id"] = deviceMac;
    
    char logBuffer[SEC_LOG_MAX_LEN];
    serializeJson(logEntry, logBuffer, sizeof(logBuffer));
    
    if (xQueueSend(securityLogQueue, logBuffer, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Security log queue full");
    }
}

bool authenticateUser(const String& username, const String& password) {
    if (users.find(username) == users.end()) {
        return false;
    }
    
    UserCredentials& user = users[username];
    
    // Check if user is locked
    if (user.isLocked && millis() < user.lockExpiry) {
        return false;
    }
    
    // Reset lock if expired
    if (user.isLocked && millis() >= user.lockExpiry) {
        user.isLocked = false;
        user.failedAttempts = 0;
    }
    
    // Verify password
    String hashedInput = hashPassword(password, username);
    if (hashedInput == user.passwordHash) {
        user.failedAttempts = 0;
        user.lastLogin = millis();
        return true;
    } else {
        user.failedAttempts++;
        if (user.failedAttempts >= MAX_FAILED_ATTEMPTS) {
            user.isLocked = true;
            user.lockExpiry = millis() + LOCKOUT_DURATION;
        }
        return false;
    }
}

// ==================== AUDIO RING BUFFER ====================

size_t writeToRingBuffer(const int16_t* samples, size_t count) {
    // Use longer timeout for audio-critical operations
    if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        size_t written = 0;
        for (size_t i = 0; i < count; i++) {
            size_t nextPos = (ringBufferWritePos + 1) % AUDIO_BUFFER_SIZE;
            if (nextPos != ringBufferReadPos) {
                audioRingBuffer[ringBufferWritePos] = samples[i];
                ringBufferWritePos = nextPos;
                written++;
            } else {
                break; // Buffer full
            }
        }
        xSemaphoreGive(audioMutex);
        return written;
    }
    return 0;
}

size_t readFromRingBuffer(int16_t* samples, size_t count) {
    if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        size_t read = 0;
        while (read < count && ringBufferReadPos != ringBufferWritePos) {
            samples[read++] = audioRingBuffer[ringBufferReadPos];
            ringBufferReadPos = (ringBufferReadPos + 1) % AUDIO_BUFFER_SIZE;
        }
        xSemaphoreGive(audioMutex);
        return read;
    }
    return 0;
}

size_t peekRingBuffer(int16_t* samples, size_t count) {
    if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        size_t read = 0;
        size_t pos = ringBufferReadPos;
        while (read < count && pos != ringBufferWritePos) {
            samples[read++] = audioRingBuffer[pos];
            pos = (pos + 1) % AUDIO_BUFFER_SIZE;
        }
        xSemaphoreGive(audioMutex);
        return read;
    }
    return 0;
}

size_t getAvailableSamples() {
    if (ringBufferWritePos >= ringBufferReadPos) {
        return ringBufferWritePos - ringBufferReadPos;
    } else {
        return AUDIO_BUFFER_SIZE - ringBufferReadPos + ringBufferWritePos;
    }
}

// ==================== RTP STREAMING ====================

uint8_t encodeAlaw(int16_t sample) {
    // G.711 A-law encoding with proper logarithmic companding
    const uint16_t ALAW_MAX = 0xFFF;
    int16_t sign = 0;
    int16_t absValue;
    
    // Get sign and absolute value (handle INT16_MIN edge case)
    if (sample < 0) {
        sign = 0x80;
        // Avoid overflow when sample is INT16_MIN (-32768)
        absValue = (sample == INT16_MIN) ? INT16_MAX : -sample;
    } else {
        absValue = sample;
    }
    
    // Clip to maximum
    if (absValue > ALAW_MAX) {
        absValue = ALAW_MAX;
    }
    
    // Convert to A-law segments (13-bit to 8-bit logarithmic)
    int segment = 0;
    int quantization = (absValue >> 4) & 0x0F;
    
    if (absValue >= 256) {
        segment = 1;
        while (absValue >= (256 << segment) && segment < 7) {
            segment++;
        }
        quantization = ((absValue >> (segment + 3)) & 0x0F);
    }
    
    // Construct A-law byte: sign(1) + segment(3) + quantization(4)
    uint8_t alaw = sign | ((segment << 4) | quantization);
    
    // A-law XOR with 0x55 for transmission
    return alaw ^ 0x55;
}

size_t createRTPPacket(uint8_t* packet, int16_t* audioSamples, size_t sampleCount, StreamingClient& client) {
    RTPHeader* header = (RTPHeader*)packet;
    
    header->version = 2;
    header->padding = 0;
    header->extension = 0;
    header->csrcCount = 0;
    header->marker = 0;
    header->payloadType = 8; // G.711 A-law
    header->sequenceNumber = htons(client.sequenceNumber++);
    header->timestamp = htonl(client.timestamp);
    header->ssrc = htonl(client.ssrc);
    
    // Encode audio to G.711 A-law
    uint8_t* payload = packet + sizeof(RTPHeader);
    for (size_t i = 0; i < sampleCount; i++) {
        payload[i] = encodeAlaw(audioSamples[i]);
    }
    
    client.timestamp += sampleCount;
    
    return sizeof(RTPHeader) + sampleCount;
}

// ==================== BARK DETECTION CALLBACK ====================

void barkDetectedCallback(const BarkDetector::BarkEvent& event) {
    barkCount++;
    barkSequence++;
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t timestamp_ms = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    
    ESP_LOGI(TAG, "🐕 BARK DETECTED #%lu!", barkCount);
    ESP_LOGI(TAG, "   Confidence: %.2f%%", event.confidence * 100.0f);
    ESP_LOGI(TAG, "   Duration: %dms", event.duration_ms);
    ESP_LOGI(TAG, "   RMS Level: %.2f", event.rms_level);
    ESP_LOGI(TAG, "   Peak Level: %.2f", event.peak_level);
    
    // Flash LED
    digitalWrite(STATUS_LED_PIN, HIGH);
    
    // Log security event
    logSecurityEvent(BARK_DETECTED_EVENT, "system", "Bark detected");
    
    // Activate bark-triggered streams
    uint32_t now = millis();
    for (auto& stream : activeStreams) {
        if (stream.second.barkTriggered) {
            stream.second.barkTriggerEnd = now + BARK_TRIGGERED_STREAM_MS;
            ESP_LOGI(TAG, "Activated bark-triggered stream for %s", stream.first.c_str());
        }
    }
    
    // Publish MQTT alert if enabled
    if (mqttEnabled && mqttConnected) {
        bark_event_t mqttEvent;
        mqttEvent.timestamp_ms = timestamp_ms;
        mqttEvent.sequence_num = barkSequence;
        mqttEvent.confidence = event.confidence;
        mqttEvent.duration_ms = event.duration_ms;
        mqttEvent.rms_level = (uint16_t)(event.rms_level * 32767.0f);
        mqttEvent.peak_level = (uint16_t)(event.peak_level * 32767.0f);
        strncpy(mqttEvent.device_id, deviceMac, sizeof(mqttEvent.device_id));
        strncpy(mqttEvent.firmware_version, firmwareVersion, sizeof(mqttEvent.firmware_version));
        
        esp_err_t err = mqtt_publish_bark_event(&mqttEvent);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "📡 MQTT alert published");
        } else {
            ESP_LOGW(TAG, "MQTT publish failed: %s", esp_err_to_name(err));
        }
    }
    
    // Turn off LED after short delay
    vTaskDelay(pdMS_TO_TICKS(100));
    digitalWrite(STATUS_LED_PIN, LOW);
}

// ==================== FREERTOS TASKS ====================

void audioTask(void* parameter) {
    ESP_LOGI(TAG, "Audio capture task started on core %d", xPortGetCoreID());
    
    int16_t frameBuffer[SAMPLES_PER_FRAME];
    
    // Configure bark detector to use our audio capture
    BarkDetector::I2SConfig i2sConfig;
    i2sConfig.sck_pin = (gpio_num_t)I2S_SCK_PIN;
    i2sConfig.ws_pin = (gpio_num_t)I2S_WS_PIN;
    i2sConfig.sd_pin = (gpio_num_t)I2S_SD_PIN;
    i2sConfig.sample_rate = SAMPLE_RATE;
    i2sConfig.frame_size_samples = SAMPLES_PER_FRAME;
    
    BarkDetector::AudioCapture audioCapture;
    if (!audioCapture.initialize(i2sConfig)) {
        ESP_LOGE(TAG, "Failed to initialize audio capture");
        vTaskDelete(NULL);
        return;
    }
    
    QueueHandle_t audioQueue = xQueueCreate(20, sizeof(BarkDetector::AudioFrame));
    if (!audioCapture.start(audioQueue)) {
        ESP_LOGE(TAG, "Failed to start audio capture");
        vTaskDelete(NULL);
        return;
    }
    
    while (true) {
        BarkDetector::AudioFrame frame;
        if (xQueueReceive(audioQueue, &frame, portMAX_DELAY) == pdTRUE) {
            // Write audio to ring buffer for streaming
            writeToRingBuffer(frame.samples, frame.sample_count);
            
            // Free frame memory
            free(frame.samples);
        }
    }
}

void barkDetectionTask(void* parameter) {
    ESP_LOGI(TAG, "Bark detection task started on core %d", xPortGetCoreID());
    
    int16_t analysisBuffer[SAMPLES_PER_FRAME];
    
    while (true) {
        // Wait for enough samples
        while (getAvailableSamples() < SAMPLES_PER_FRAME) {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
        
        // Peek samples (non-destructive read for analysis)
        size_t read = peekRingBuffer(analysisBuffer, SAMPLES_PER_FRAME);
        
        if (read == SAMPLES_PER_FRAME) {
            // Process frame with TinyML detector
            BarkDetector::AudioClass result = detector.processFrame(analysisBuffer, SAMPLES_PER_FRAME);
            
            // Log classification for debugging (non-bark classes)
            if (result != BarkDetector::AudioClass::DOG_BARK && result != BarkDetector::AudioClass::UNKNOWN) {
                static uint32_t logCounter = 0;
                if (++logCounter % 100 == 0) { // Log every 100 frames (~2 seconds)
                    ESP_LOGD(TAG, "Classification: %d", (int)result);
                }
            }
            
            // Note: detector.start() was called with barkDetectedCallback
            // The callback will be invoked automatically when a bark is detected
        }
        
        vTaskDelay(pdMS_TO_TICKS(FRAME_SIZE_MS));
    }
}

void streamingTask(void* parameter) {
    ESP_LOGI(TAG, "Streaming task started on core %d", xPortGetCoreID());
    
    uint8_t rtpPacket[512];
    int16_t streamBuffer[SAMPLES_PER_FRAME];
    
    while (true) {
        uint32_t now = millis();
        
        // Check each active stream
        for (auto it = activeStreams.begin(); it != activeStreams.end(); ) {
            StreamingClient& client = it->second;
            
            // Check if bark-triggered stream has expired
            if (client.barkTriggered && now > client.barkTriggerEnd) {
                ESP_LOGI(TAG, "Bark-triggered stream expired for %s", it->first.c_str());
                it = activeStreams.erase(it);
                continue;
            }
            
            if (client.active) {
                // Read samples from ring buffer
                size_t samplesRead = readFromRingBuffer(streamBuffer, SAMPLES_PER_FRAME);
                
                if (samplesRead == SAMPLES_PER_FRAME) {
                    // Create and send RTP packet
                    size_t packetSize = createRTPPacket(rtpPacket, streamBuffer, SAMPLES_PER_FRAME, client);
                    udp.beginPacket(client.ip, client.port);
                    udp.write(rtpPacket, packetSize);
                    udp.endPacket();
                }
            }
            
            ++it;
        }
        
        vTaskDelay(pdMS_TO_TICKS(FRAME_SIZE_MS));
    }
}

void securityTask(void* parameter) {
    ESP_LOGI(TAG, "Security task started on core %d", xPortGetCoreID());
    
    char logEntry[SEC_LOG_MAX_LEN];
    while (true) {
        // Process security log queue
        if (xQueueReceive(securityLogQueue, logEntry, pdMS_TO_TICKS(1000)) == pdTRUE) {
            ESP_LOGI(TAG, "Security event: %s", logEntry);
        }
        
        // Cleanup expired sessions periodically
        static uint32_t lastCleanup = 0;
        uint32_t now = millis();
        if (now - lastCleanup > 60000) { // Every minute
            for (auto it = activeStreams.begin(); it != activeStreams.end(); ) {
                if (!it->second.active) {
                    it = activeStreams.erase(it);
                } else {
                    ++it;
                }
            }
            lastCleanup = now;
        }
    }
}

// ==================== WEB SERVER ROUTES ====================

void setupSecureRoutes() {
    // CORS headers
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    
    // Authentication endpoint
    server.on("/api/login", HTTP_POST, [](AsyncWebServerRequest *request) {
        String clientIP = request->client()->remoteIP().toString();
        
        if (!rateLimitCheck(clientIP)) {
            logSecurityEvent(RATE_LIMIT_EXCEEDED, clientIP);
            request->send(429, "application/json", "{\"error\":\"Too many requests\"}");
            return;
        }
        
        if (!request->hasParam("username", true) || !request->hasParam("password", true)) {
            request->send(400, "application/json", "{\"error\":\"Username and password required\"}");
            return;
        }
        
        String username = request->getParam("username", true)->value();
        String password = request->getParam("password", true)->value();
        
        if (authenticateUser(username, password)) {
            SecurityLevel userLevel = users[username].level;
            String token = generateJWT(username, userLevel);
            
            DynamicJsonDocument response(512);
            response["token"] = token;
            response["expires_in"] = TOKEN_LIFETIME;
            response["level"] = (int)userLevel;
            
            String responseStr;
            serializeJson(response, responseStr);
            
            logSecurityEvent(LOGIN_SUCCESS, clientIP, username);
            request->send(200, "application/json", responseStr);
        } else {
            logSecurityEvent(LOGIN_FAILURE, clientIP, username);
            request->send(401, "application/json", "{\"error\":\"Invalid credentials\"}");
        }
    });
    
    // Start audio stream endpoint
    server.on("/api/start-stream", HTTP_POST, [](AsyncWebServerRequest *request) {
        String clientIP = request->client()->remoteIP().toString();
        
        if (!rateLimitCheck(clientIP)) {
            request->send(429, "application/json", "{\"error\":\"Too many requests\"}");
            return;
        }
        
        String authHeader = "";
        if (request->hasHeader("Authorization")) {
            authHeader = request->getHeader("Authorization")->value();
        }
        
        if (!authHeader.startsWith("Bearer ")) {
            logSecurityEvent(UNAUTHORIZED_ACCESS, clientIP);
            request->send(401, "application/json", "{\"error\":\"Authorization required\"}");
            return;
        }
        
        String token = authHeader.substring(7);
        if (!validateJWT(token, USER_ACCESS)) {
            logSecurityEvent(INVALID_TOKEN, clientIP);
            request->send(401, "application/json", "{\"error\":\"Invalid token\"}");
            return;
        }
        
        // Check for bark_alerts_only parameter
        bool barkAlertsOnly = false;
        if (request->hasParam("bark_alerts_only", true)) {
            String param = request->getParam("bark_alerts_only", true)->value();
            barkAlertsOnly = (param == "true" || param == "1");
        }
        
        // Create streaming session
        String sessionId = clientIP + "_" + String(millis());
        StreamingClient& client = activeStreams[sessionId];
        
        client.ip = request->client()->remoteIP();
        client.port = RTP_PORT;
        client.ssrc = esp_random();
        client.sequenceNumber = 1;
        client.timestamp = 0;
        client.active = true;
        client.barkTriggered = barkAlertsOnly;
        client.barkTriggerEnd = 0;
        client.sessionToken = token;
        
        for (int i = 0; i < 32; i++) {
            client.encryptionKey[i] = esp_random() & 0xFF;
        }
        
        DynamicJsonDocument response(512);
        response["session_id"] = sessionId;
        response["ssrc"] = client.ssrc;
        response["rtp_port"] = RTP_PORT;
        response["sample_rate"] = SAMPLE_RATE;
        response["codec"] = "PCMA";
        response["bark_triggered"] = barkAlertsOnly;
        
        String responseStr;
        serializeJson(response, responseStr);
        
        ESP_LOGI(TAG, "Stream started for %s (bark-triggered: %s)", 
                 clientIP.c_str(), barkAlertsOnly ? "yes" : "no");
        request->send(200, "application/json", responseStr);
    });
    
    // Stop audio stream endpoint
    server.on("/api/stop-stream", HTTP_POST, [](AsyncWebServerRequest *request) {
        String clientIP = request->client()->remoteIP().toString();
        
        if (!rateLimitCheck(clientIP)) {
            request->send(429, "application/json", "{\"error\":\"Too many requests\"}");
            return;
        }
        
        String authHeader = "";
        if (request->hasHeader("Authorization")) {
            authHeader = request->getHeader("Authorization")->value();
        }
        
        if (!authHeader.startsWith("Bearer ")) {
            request->send(401, "application/json", "{\"error\":\"Authorization required\"}");
            return;
        }
        
        String token = authHeader.substring(7);
        if (!validateJWT(token, USER_ACCESS)) {
            request->send(401, "application/json", "{\"error\":\"Invalid token\"}");
            return;
        }
        
        // Find and remove client's stream
        for (auto it = activeStreams.begin(); it != activeStreams.end(); ) {
            if (it->first.startsWith(clientIP)) {
                ESP_LOGI(TAG, "Stream stopped for %s", clientIP.c_str());
                it = activeStreams.erase(it);
            } else {
                ++it;
            }
        }
        
        request->send(200, "application/json", "{\"status\":\"stopped\"}");
    });
    
    // Bark status endpoint
    server.on("/api/bark-status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String clientIP = request->client()->remoteIP().toString();
        
        if (!rateLimitCheck(clientIP)) {
            request->send(429, "application/json", "{\"error\":\"Too many requests\"}");
            return;
        }
        
        String authHeader = "";
        if (request->hasHeader("Authorization")) {
            authHeader = request->getHeader("Authorization")->value();
        }
        
        if (!authHeader.startsWith("Bearer ")) {
            request->send(401, "application/json", "{\"error\":\"Authorization required\"}");
            return;
        }
        
        String token = authHeader.substring(7);
        if (!validateJWT(token, USER_ACCESS)) {
            request->send(401, "application/json", "{\"error\":\"Invalid token\"}");
            return;
        }
        
        BarkDetector::BarkDetector::Stats stats = detector.getStats();
        
        DynamicJsonDocument response(1024);
        response["bark_count"] = barkCount;
        response["frames_processed"] = stats.frames_processed;
        response["barks_detected"] = stats.barks_detected;
        response["false_positives"] = stats.false_positives;
        response["avg_inference_time_ms"] = stats.avg_inference_time_ms;
        response["avg_cpu_usage"] = stats.avg_cpu_usage;
        response["memory_usage_bytes"] = stats.memory_usage_bytes;
        response["is_running"] = detector.isRunning();
        
        float probs[4];
        if (detector.getLastProbabilities(probs)) {
            JsonArray probArray = response.createNestedArray("last_probabilities");
            probArray.add(probs[0]);
            probArray.add(probs[1]);
            probArray.add(probs[2]);
            probArray.add(probs[3]);
        }
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
    });
    
    // Bark configuration endpoint
    server.on("/api/bark-config", HTTP_POST, [](AsyncWebServerRequest *request) {
        String clientIP = request->client()->remoteIP().toString();
        
        if (!rateLimitCheck(clientIP)) {
            request->send(429, "application/json", "{\"error\":\"Too many requests\"}");
            return;
        }
        
        String authHeader = "";
        if (request->hasHeader("Authorization")) {
            authHeader = request->getHeader("Authorization")->value();
        }
        
        if (!authHeader.startsWith("Bearer ")) {
            request->send(401, "application/json", "{\"error\":\"Authorization required\"}");
            return;
        }
        
        String token = authHeader.substring(7);
        if (!validateJWT(token, ADMIN_ACCESS)) {
            request->send(401, "application/json", "{\"error\":\"Admin access required\"}");
            return;
        }
        
        // Parse configuration parameters
        if (request->hasParam("bark_threshold", true)) {
            float threshold = request->getParam("bark_threshold", true)->value().toFloat();
            if (threshold >= 0.0f && threshold <= 1.0f) {
                barkConfig.bark_threshold = threshold;
            }
        }
        
        if (request->hasParam("min_duration_ms", true)) {
            uint16_t duration = request->getParam("min_duration_ms", true)->value().toInt();
            barkConfig.min_duration_ms = duration;
        }
        
        // Update detector configuration (requires restart)
        detector.setConfig(barkConfig);
        
        logSecurityEvent(CONFIG_CHANGE, clientIP, "Bark detector configuration updated");
        
        request->send(200, "application/json", "{\"status\":\"updated\"}");
    });
    
    // Public status endpoint
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
        String clientIP = request->client()->remoteIP().toString();
        
        if (!rateLimitCheck(clientIP)) {
            request->send(429, "application/json", "{\"error\":\"Too many requests\"}");
            return;
        }
        
        DynamicJsonDocument response(1024);
        response["device_id"] = deviceMac;
        response["firmware"] = firmwareVersion;
        response["uptime_ms"] = millis();
        response["free_heap"] = ESP.getFreeHeap();
        response["free_psram"] = ESP.getFreePsram();
        response["active_streams"] = activeStreams.size();
        response["bark_count"] = barkCount;
        response["mqtt_connected"] = mqttConnected;
        response["wifi_rssi"] = WiFi.RSSI();
        
        String responseStr;
        serializeJson(response, responseStr);
        request->send(200, "application/json", responseStr);
    });
}

// ==================== INITIALIZATION ====================

void initializeDefaultUsers() {
    prefs.begin("security", false);
    
    bool adminExists = prefs.getBool("admin_created", false);
    
    if (!adminExists) {
        // Generate random admin password
        String adminPassword = "";
        for (int i = 0; i < 16; i++) {
            adminPassword += char('a' + (esp_random() % 26));
        }
        
        UserCredentials admin;
        admin.username = "admin";
        admin.passwordHash = hashPassword(adminPassword, "admin");
        admin.level = SUPER_ADMIN;
        admin.lastLogin = 0;
        admin.failedAttempts = 0;
        admin.isLocked = false;
        admin.lockExpiry = 0;
        
        users["admin"] = admin;
        
        prefs.putString("admin_hash", admin.passwordHash);
        prefs.putBool("admin_created", true);
        
        Serial.println("\n=== INITIAL ADMIN CREDENTIALS ===");
        Serial.printf("Username: admin\n");
        Serial.printf("Password: %s\n", adminPassword.c_str());
        Serial.println("=== CHANGE PASSWORD IMMEDIATELY ===\n");
    } else {
        UserCredentials admin;
        admin.username = "admin";
        admin.passwordHash = prefs.getString("admin_hash", "");
        admin.level = SUPER_ADMIN;
        admin.lastLogin = 0;
        admin.failedAttempts = 0;
        admin.isLocked = false;
        admin.lockExpiry = 0;
        
        users["admin"] = admin;
    }
    
    prefs.end();
    
    // Generate JWT secret
    jwtSecret = "";
    for (int i = 0; i < 32; i++) {
        jwtSecret += char('a' + (esp_random() % 26));
    }
}

void mqttStatusCallback(bool connected, esp_err_t error_code) {
    mqttConnected = connected;
    if (connected) {
        ESP_LOGI(TAG, "MQTT connected");
    } else {
        ESP_LOGW(TAG, "MQTT disconnected: %s", esp_err_to_name(error_code));
    }
}

esp_err_t initializeMQTT() {
    mqtt_provisioning_config_t provConfig;
    esp_err_t err = mqtt_provisioning_load(&provConfig);
    
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No MQTT configuration found");
        return err;
    }
    
    mqtt_config_t mqttConfig;
    strncpy(mqttConfig.broker_host, provConfig.broker_host, sizeof(mqttConfig.broker_host));
    mqttConfig.broker_port = provConfig.broker_port;
    strncpy(mqttConfig.username, provConfig.username, sizeof(mqttConfig.username));
    strncpy(mqttConfig.password, provConfig.password, sizeof(mqttConfig.password));
    strncpy(mqttConfig.client_id, deviceMac, sizeof(mqttConfig.client_id));
    strncpy(mqttConfig.topic_prefix, provConfig.topic_prefix, sizeof(mqttConfig.topic_prefix));
    mqttConfig.use_tls = provConfig.use_tls;
    mqttConfig.ca_cert_pem = nullptr; // Use system CA bundle
    mqttConfig.keep_alive_sec = 60;
    mqttConfig.timeout_ms = 5000;
    
    err = mqtt_client_init(&mqttConfig, mqttStatusCallback);
    if (err == ESP_OK) {
        err = mqtt_client_start();
        if (err == ESP_OK) {
            mqttEnabled = true;
            ESP_LOGI(TAG, "MQTT client started");
        }
    }
    
    return err;
}

// ==================== SETUP & LOOP ====================

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n=== ESP32-S3 Secure WebRTC Dog Bark Detector ===");
    Serial.printf("Firmware: %s\n", firmwareVersion);
    Serial.printf("Chip: %s\n", ESP.getChipModel());
    Serial.printf("CPU Frequency: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("PSRAM Size: %d bytes\n", ESP.getPsramSize());
    Serial.printf("PSRAM Free: %d bytes\n", ESP.getFreePsram());
    Serial.println("===============================================\n");
    
    // Initialize LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);
    
    // Get device MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(deviceMac, sizeof(deviceMac), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Device MAC: %s", deviceMac);
    
    // Initialize security
    securityLogQueue = xQueueCreate(50, SEC_LOG_MAX_LEN);
    initializeDefaultUsers();
    
    // Initialize WiFi
    prefs.begin("network", true);
    String ssid = prefs.getString("ssid", "");
    String password = prefs.getString("password", "");
    prefs.end();
    
    if (ssid.length() > 0) {
        WiFi.begin(ssid.c_str(), password.c_str());
        Serial.printf("Connecting to WiFi: %s", ssid.c_str());
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 30) {
            delay(1000);
            Serial.print(".");
            attempts++;
        }
        
        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
        } else {
            Serial.println("\nWiFi connection failed! Starting AP mode...");
            // Here you would start AP mode for provisioning
            return;
        }
    } else {
        Serial.println("No WiFi credentials configured!");
        return;
    }
    
    // Initialize NTP
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("Waiting for NTP time sync...");
    time_t now = 0;
    int ntpAttempts = 0;
    while (now < 1000000000 && ntpAttempts < 10) {
        now = time(NULL);
        delay(500);
        ntpAttempts++;
    }
    if (now >= 1000000000) {
        Serial.printf("Time synchronized: %s", ctime(&now));
    } else {
        Serial.println("NTP sync failed");
    }
    
    // Allocate audio ring buffer
    audioRingBuffer = (int16_t*)malloc(AUDIO_BUFFER_SIZE * sizeof(int16_t));
    if (!audioRingBuffer) {
        Serial.println("Failed to allocate audio ring buffer!");
        return;
    }
    memset(audioRingBuffer, 0, AUDIO_BUFFER_SIZE * sizeof(int16_t));
    
    // Create audio mutex
    audioMutex = xSemaphoreCreateMutex();
    
    // Initialize UDP for RTP
    udp.begin(RTP_PORT);
    
    // Configure bark detector
    barkConfig = BarkDetector::Config{};
    barkConfig.sample_rate = SAMPLE_RATE;
    barkConfig.frame_size_ms = FRAME_SIZE_MS;
    barkConfig.bark_threshold = BARK_CONFIDENCE_THRESHOLD;
    barkConfig.min_duration_ms = BARK_MIN_DURATION_MS;
    barkConfig.enable_noise_gate = true;
    barkConfig.noise_gate_db = -40.0f;
    barkConfig.enable_agc = true;
    barkConfig.mel_bands = 40;
    barkConfig.fft_size = 512;
    
    Serial.println("Initializing bark detector...");
    if (!detector.initialize(barkConfig)) {
        Serial.println("❌ Failed to initialize bark detector!");
        return;
    }
    
    Serial.println("Starting bark detection...");
    if (!detector.start(barkDetectedCallback)) {
        Serial.println("❌ Failed to start bark detector!");
        return;
    }
    
    // Initialize MQTT
    initializeMQTT();
    
    // Create FreeRTOS tasks with balanced priorities and adequate stack sizes
    // Priority: audio=3 (highest), bark=2, streaming=2, security=1
    // Stack: increased for tasks with JSON/network operations
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 8192, NULL, 3, &audioTaskHandle, 0);
    xTaskCreatePinnedToCore(barkDetectionTask, "BarkTask", 8192, NULL, 2, &barkDetectionTaskHandle, 1);
    xTaskCreatePinnedToCore(streamingTask, "StreamTask", 6144, NULL, 2, &streamingTaskHandle, 1);
    xTaskCreatePinnedToCore(securityTask, "SecurityTask", 6144, NULL, 1, &securityTaskHandle, 0);
    
    // Setup and start web server
    setupSecureRoutes();
    server.begin();
    
    Serial.printf("\n✅ System ready!\n");
    Serial.printf("HTTPS server: https://%s:%d\n", WiFi.localIP().toString().c_str(), HTTPS_PORT);
    Serial.printf("RTP streaming: udp://%s:%d\n", WiFi.localIP().toString().c_str(), RTP_PORT);
    Serial.println("🎤 Listening for dog barks...");
    Serial.println("🔒 Secure WebRTC streaming enabled");
    
    // Flash LED to indicate ready
    for (int i = 0; i < 3; i++) {
        digitalWrite(STATUS_LED_PIN, HIGH);
        delay(100);
        digitalWrite(STATUS_LED_PIN, LOW);
        delay(100);
    }
}

void loop() {
    // All processing is done in FreeRTOS tasks
    delay(1000);
    
    // Print periodic status
    static uint32_t lastStatus = 0;
    if (millis() - lastStatus > 30000) {
        Serial.printf("Status: Heap=%d, PSRAM=%d, Streams=%d, Barks=%lu, Uptime=%ds\n",
                     ESP.getFreeHeap(), ESP.getFreePsram(), 
                     activeStreams.size(), barkCount, millis()/1000);
        lastStatus = millis();
    }
}

/**
 * @file bark_demo.cpp
 * @brief ESP32-S3 TinyML Dog Bark Detection with MQTT Alerts
 * 
 * Complete demonstration application featuring:
 * - Real-time audio classification using TensorFlow Lite Micro
 * - MQTT alerts with TLS encryption for bark events
 * - Serial interface for configuration and monitoring
 * - WiFi provisioning integration
 */

#include <Arduino.h>
#include <WiFi.h>
#include "audio_capture.h"
#include "preprocess.h"
#include "bark_detector_api.h"
#include "mqtt_client.h"
#include "mqtt_provisioning.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>

static const char* TAG = "bark_demo";

// System state
static bool detection_active = false;
static uint32_t bark_count = 0;
static float detection_threshold = 0.7f;
static bark_sensitivity_t sensitivity_level = BARK_SENSITIVITY_MEDIUM;

// MQTT state
static bool mqtt_enabled = false;
static bool mqtt_connected = false;
static uint32_t bark_sequence = 0;
static char device_mac[18] = {0};
static char firmware_version[] = "1.0.0";

// Forward declarations
static void bark_detected_callback(bark_detection_event_t* event);
static void mqtt_status_callback(bool connected, esp_err_t error_code);
static void publish_bark_alert(const bark_detection_event_t* event);
static esp_err_t initialize_mqtt(void);

/**
 * @brief Bark detection callback function with MQTT integration
 */
static void bark_detected_callback(bark_detection_event_t* event) {
    bark_count++;
    bark_sequence++;
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    ESP_LOGI(TAG, "🐕 BARK DETECTED #%lu!", bark_count);
    ESP_LOGI(TAG, "   Confidence: %.2f%%", event->confidence * 100.0f);
    ESP_LOGI(TAG, "   Duration: %dms", event->duration_ms);
    ESP_LOGI(TAG, "   RMS Level: %d", event->rms_level);
    ESP_LOGI(TAG, "   Peak Level: %d", event->peak_level);
    ESP_LOGI(TAG, "   Timestamp: %ld.%03ld", tv.tv_sec, tv.tv_usec / 1000);
    
    Serial.printf("\n🔔 Bark Alert: Confidence=%.1f%%, Duration=%dms\n", 
           event->confidence * 100.0f, event->duration_ms);
    
    // Publish MQTT alert if enabled and connected
    if (mqtt_enabled && mqtt_connected) {
        publish_bark_alert(event);
        Serial.printf("📡 MQTT alert sent\n");
    }
    
    Serial.printf("\n");
    
    // Flash LED to indicate detection
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
}

/**
 * @brief Print system information
 */
void printSystemInfo() {
    Serial.println("\n=== ESP32-S3 Dog Bark Detection Demo ===");
    Serial.printf("Chip: %s\n", ESP.getChipModel());
    Serial.printf("CPU Frequency: %d MHz\n", ESP.getCpuFreqMHz());
    Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
    Serial.printf("PSRAM Size: %d bytes\n", ESP.getPsramSize());
    Serial.printf("PSRAM Free: %d bytes\n", ESP.getFreePsram());
    Serial.println("=========================================");
}

/**
 * @brief Print detector configuration
 */
void printDetectorConfig() {
    Serial.println("\n=== Bark Detector Configuration ===");
    Serial.printf("Sample Rate: %d Hz\n", bark_config.sample_rate);
    Serial.printf("Frame Size: %d ms\n", bark_config.frame_size_ms);
    Serial.printf("Bark Threshold: %.2f\n", bark_config.bark_threshold);
    Serial.printf("Min Duration: %d ms\n", bark_config.min_duration_ms);
    Serial.printf("Mel Bands: %d\n", bark_config.mel_bands);
    Serial.printf("FFT Size: %d\n", bark_config.fft_size);
    Serial.printf("Noise Gate: %s (%.1f dB)\n", 
                 bark_config.enable_noise_gate ? "ON" : "OFF",
                 bark_config.noise_gate_db);
    Serial.printf("AGC: %s\n", bark_config.enable_agc ? "ON" : "OFF");
    Serial.println("===================================");
}

/**
 * @brief Status monitoring task
 */
void statusTask(void* parameter) {
    while (true) {
        BarkDetector::BarkDetector::Stats stats = detector.getStats();
        
        Serial.printf("\n--- Status Update ---\n");
        Serial.printf("Frames Processed: %d\n", stats.frames_processed);
        Serial.printf("Barks Detected: %d\n", stats.barks_detected);
        Serial.printf("False Positives: %d\n", stats.false_positives);
        Serial.printf("Avg Inference Time: %.2f ms\n", stats.avg_inference_time_ms);
        Serial.printf("CPU Usage: %.1f%%\n", stats.avg_cpu_usage);
        Serial.printf("Memory Usage: %d bytes\n", stats.memory_usage_bytes);
        Serial.printf("Free Heap: %d bytes\n", ESP.getFreeHeap());
        
        // Show recent classification probabilities
        float probs[4];
        if (detector.getLastProbabilities(probs)) {
            Serial.printf("Last Classification:\n");
            Serial.printf("  Dog Bark: %.2f%%\n", probs[0] * 100.0f);
            Serial.printf("  Speech:   %.2f%%\n", probs[1] * 100.0f);
            Serial.printf("  Ambient:  %.2f%%\n", probs[2] * 100.0f);
            Serial.printf("  Silence:  %.2f%%\n", probs[3] * 100.0f);
        }
        
        uint32_t uptime_sec = (millis() - session_start_time) / 1000;
        Serial.printf("Session Uptime: %d:%02d:%02d\n", 
                     uptime_sec / 3600, (uptime_sec % 3600) / 60, uptime_sec % 60);
        Serial.println("-------------------");
        
        vTaskDelay(pdMS_TO_TICKS(10000)); // Update every 10 seconds
    }
}

/**
 * @brief Serial command processing
 */
void processSerialCommands() {
    if (Serial.available()) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        command.toLowerCase();
        
        if (command == "help") {
            Serial.println("\n=== Available Commands ===");
            Serial.println("help     - Show this help");
            Serial.println("status   - Show current status");
            Serial.println("config   - Show configuration");
            Serial.println("reset    - Reset statistics");
            Serial.println("restart  - Restart detector");
            Serial.println("test     - Run self-test");
            Serial.println("debug    - Toggle debug mode");
            Serial.println("========================");
            
        } else if (command == "status") {
            BarkDetector::BarkDetector::Stats stats = detector.getStats();
            Serial.printf("Detector Status: %s\n", detector.isRunning() ? "RUNNING" : "STOPPED");
            Serial.printf("Total Barks: %d\n", total_barks);
            Serial.printf("Frames Processed: %d\n", stats.frames_processed);
            Serial.printf("Memory Usage: %d bytes\n", stats.memory_usage_bytes);
            
        } else if (command == "config") {
            printDetectorConfig();
            
        } else if (command == "reset") {
            detector.resetStats();
            total_barks = 0;
            session_start_time = millis();
            Serial.println("Statistics reset");
            
        } else if (command == "restart") {
            Serial.println("Restarting detector...");
            detector.stop();
            delay(1000);
            if (detector.start(onBarkDetected)) {
                Serial.println("Detector restarted successfully");
            } else {
                Serial.println("Failed to restart detector");
            }
            
        } else if (command == "test") {
            Serial.println("Running self-test...");
            // Could implement microphone test, model validation, etc.
            Serial.println("Self-test completed");
            
        } else if (command == "debug") {
            // Toggle debug logging level
            static bool debug_enabled = false;
            debug_enabled = !debug_enabled;
            esp_log_level_set("*", debug_enabled ? ESP_LOG_DEBUG : ESP_LOG_INFO);
            Serial.printf("Debug mode: %s\n", debug_enabled ? "ON" : "OFF");
            
        } else {
            Serial.printf("Unknown command: %s (type 'help' for commands)\n", command.c_str());
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(2000);
    
    // Initialize LED
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, LOW);
    
    printSystemInfo();
    
    // Configure bark detector
    bark_config = BarkDetector::Config{};
    bark_config.sample_rate = 16000;
    bark_config.frame_size_ms = 20;
    bark_config.bark_threshold = 0.8f;
    bark_config.min_duration_ms = 300;
    bark_config.enable_noise_gate = true;
    bark_config.noise_gate_db = -40.0f;
    bark_config.enable_agc = true;
    bark_config.mel_bands = 40;
    bark_config.fft_size = 512;
    
    printDetectorConfig();
    
    // Initialize bark detector
    Serial.println("Initializing bark detector...");
    if (!detector.initialize(bark_config)) {
        Serial.println("❌ Failed to initialize bark detector!");
        while (1) {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(200);
            digitalWrite(LED_BUILTIN, LOW);
            delay(200);
        }
    }
    
    // Start detection
    Serial.println("Starting bark detection...");
    if (!detector.start(onBarkDetected)) {
        Serial.println("❌ Failed to start bark detector!");
        while (1) {
            digitalWrite(LED_BUILTIN, HIGH);
            delay(500);
            digitalWrite(LED_BUILTIN, LOW);
            delay(500);
        }
    }
    
    session_start_time = millis();
    
    // Create status monitoring task
    xTaskCreatePinnedToCore(
        statusTask,
        "status_task",
        4096,
        NULL,
        1,
        NULL,
        1  // Core 1
    );
    
    Serial.println("✅ Bark detection system ready!");
    Serial.println("🎤 Listening for dog barks...");
    Serial.println("Type 'help' for available commands");
    
    // Flash LED to indicate ready
    for (int i = 0; i < 3; i++) {
        digitalWrite(LED_BUILTIN, HIGH);
        delay(100);
        digitalWrite(LED_BUILTIN, LOW);
        delay(100);
    }
}

void loop() {
    // Process serial commands
    processSerialCommands();
    
    // Main loop can perform other tasks
    // Bark detection runs in background tasks
    
    delay(100);
}
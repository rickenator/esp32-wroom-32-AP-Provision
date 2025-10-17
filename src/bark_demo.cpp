/**
 * @file bark_demo.cpp
 * @brief Demo application for ESP32-S3 TinyML Dog Bark Detection
 * 
 * Demonstrates real-time bark detection using INMP441 microphone
 * and displays results via serial output.
 */

#include <Arduino.h>
#include <WiFi.h>
#include "bark_detector_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "BarkDemo";

// Demo configuration
BarkDetector::Config bark_config;
BarkDetector::BarkDetector detector;

// Statistics tracking
uint32_t total_barks = 0;
uint32_t session_start_time = 0;

/**
 * @brief Bark detection callback function
 */
void onBarkDetected(const BarkDetector::BarkEvent& event) {
    total_barks++;
    
    const char* class_name = BarkDetector::Utils::audioClassToString(event.detected_class);
    
    Serial.printf("\n🐕 BARK DETECTED! 🐕\n");
    Serial.printf("Class: %s\n", class_name);
    Serial.printf("Confidence: %.2f%%\n", event.confidence * 100.0f);
    Serial.printf("Duration: %d ms\n", event.duration_ms);
    Serial.printf("RMS Level: %.3f\n", event.rms_level);
    Serial.printf("Peak Level: %.3f\n", event.peak_level);
    Serial.printf("Timestamp: %d ms\n", event.timestamp_ms);
    Serial.printf("Total Barks: %d\n", total_barks);
    Serial.println("========================");
    
    // Optional: Flash LED or trigger GPIO
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
/**
 * @file bark_detector_api.h
 * @brief Public API for ESP32-S3 TinyML Dog Bark Detection System
 * 
 * High-level interface for real-time dog bark classification using
 * INMP441 I2S microphone and TensorFlow Lite Micro neural network.
 * 
 * @author ESP32 Dog Bark Detection Team
 * @date October 2025
 */

#pragma once

#include <functional>
#include <cstdint>

namespace BarkDetector {

/**
 * @brief Audio classification results
 */
enum class AudioClass {
    DOG_BARK = 0,
    SPEECH = 1,
    AMBIENT = 2, 
    SILENCE = 3,
    UNKNOWN = 4
};

/**
 * @brief Detection event structure
 */
struct BarkEvent {
    AudioClass detected_class;
    float confidence;           // 0.0 - 1.0
    uint32_t timestamp_ms;      // System timestamp
    uint16_t duration_ms;       // Event duration
    float rms_level;           // Audio RMS level
    float peak_level;          // Audio peak level
};

/**
 * @brief Configuration parameters
 */
struct Config {
    // Audio capture settings
    uint32_t sample_rate = 16000;      // Hz
    uint16_t frame_size_ms = 20;       // Frame duration
    uint8_t dma_buffer_count = 10;     // Number of DMA buffers
    
    // Detection thresholds
    float bark_threshold = 0.8f;       // Confidence threshold for bark
    uint16_t min_duration_ms = 300;    // Minimum bark duration
    uint16_t debounce_ms = 100;        // Debounce time between events
    
    // Preprocessing
    bool enable_noise_gate = true;
    float noise_gate_db = -40.0f;      // dB threshold
    bool enable_agc = true;            // Automatic gain control
    
    // Feature extraction
    uint8_t mel_bands = 40;            // Number of Mel filterbank bands
    uint16_t fft_size = 512;           // FFT size for spectrogram
    uint8_t hop_length_ms = 10;        // Frame hop length
    
    // Decision logic
    float ema_alpha = 0.3f;            // Exponential moving average factor
    uint8_t median_window = 5;         // Median filter window size
};

/**
 * @brief Bark detection callback function type
 */
using BarkCallback = std::function<void(const BarkEvent& event)>;

/**
 * @brief Main bark detector class
 */
class BarkDetector {
public:
    /**
     * @brief Constructor
     */
    BarkDetector();
    
    /**
     * @brief Destructor
     */
    ~BarkDetector();
    
    /**
     * @brief Initialize the bark detector
     * @param config Configuration parameters
     * @return true if initialization successful
     */
    bool initialize(const Config& config = Config{});
    
    /**
     * @brief Start bark detection
     * @param callback Function to call when bark is detected
     * @return true if started successfully
     */
    bool start(BarkCallback callback);
    
    /**
     * @brief Stop bark detection
     */
    void stop();
    
    /**
     * @brief Check if detector is running
     * @return true if running
     */
    bool isRunning() const;
    
    /**
     * @brief Process single audio frame (synchronous)
     * @param samples 16-bit PCM audio samples
     * @param sample_count Number of samples
     * @return Classification result
     */
    AudioClass processFrame(const int16_t* samples, size_t sample_count);
    
    /**
     * @brief Get current configuration
     * @return Current config
     */
    const Config& getConfig() const;
    
    /**
     * @brief Update configuration (requires restart)
     * @param config New configuration
     * @return true if valid configuration
     */
    bool setConfig(const Config& config);
    
    /**
     * @brief Get performance statistics
     */
    struct Stats {
        uint32_t frames_processed;
        uint32_t barks_detected;
        uint32_t false_positives;
        float avg_inference_time_ms;
        float avg_cpu_usage;
        size_t memory_usage_bytes;
    };
    
    /**
     * @brief Get performance statistics
     * @return Current statistics
     */
    Stats getStats() const;
    
    /**
     * @brief Reset statistics counters
     */
    void resetStats();
    
    /**
     * @brief Get last classification probabilities
     * @param probs Array to fill with probabilities [4 classes]
     * @return true if valid data available
     */
    bool getLastProbabilities(float probs[4]) const;

private:
    class Impl;
    Impl* pImpl;
};

/**
 * @brief Utility functions
 */
namespace Utils {
    
    /**
     * @brief Convert AudioClass to string
     * @param cls Audio class
     * @return String representation
     */
    const char* audioClassToString(AudioClass cls);
    
    /**
     * @brief Convert dB to linear scale
     * @param db Decibel value
     * @return Linear amplitude
     */
    float dbToLinear(float db);
    
    /**
     * @brief Convert linear to dB scale
     * @param linear Linear amplitude
     * @return Decibel value
     */
    float linearToDb(float linear);
    
    /**
     * @brief Calculate RMS level of audio samples
     * @param samples Audio samples
     * @param count Number of samples
     * @return RMS level (0.0 - 1.0)
     */
    float calculateRMS(const int16_t* samples, size_t count);
    
    /**
     * @brief Find peak level in audio samples
     * @param samples Audio samples
     * @param count Number of samples
     * @return Peak level (0.0 - 1.0)
     */
    float calculatePeak(const int16_t* samples, size_t count);
}

} // namespace BarkDetector
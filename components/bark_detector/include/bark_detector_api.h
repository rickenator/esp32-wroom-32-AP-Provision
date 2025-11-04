#ifndef BARK_DETECTOR_API_H
#define BARK_DETECTOR_API_H

#include <cstdint>
#include <cstddef>

/**
 * @brief Bark detection classes
 */
enum class BarkClass : uint8_t {
    DOG_BARK = 0,
    SPEECH = 1,
    AMBIENT = 2,
    SILENCE = 3,
    UNKNOWN = 255
};

/**
 * @brief Detection result structure
 */
struct DetectionResult {
    BarkClass detected_class;
    float confidence;           // 0.0 to 1.0
    uint32_t timestamp_ms;
    bool is_bark;              // true if detected_class == DOG_BARK and confidence > threshold
};

/**
 * @brief Performance statistics
 */
struct PerformanceStats {
    uint32_t inference_time_us;     // Last inference time in microseconds
    uint32_t avg_inference_time_us; // Running average
    uint32_t preprocessing_time_us;
    uint32_t total_inferences;
    uint32_t bark_detections;
    float memory_usage_kb;
};

/**
 * @brief Configuration parameters
 */
struct BarkDetectorConfig {
    float confidence_threshold;     // Default: 0.7
    uint32_t min_bark_duration_ms; // Default: 300ms
    float agc_target_level;        // Default: 0.5 (normalized)
    float noise_gate_threshold;    // Default: -40 dB
    bool enable_temporal_filter;   // Default: true
    float ema_alpha;               // EMA smoothing factor (0.1-0.5)
    uint8_t median_filter_size;    // Default: 3
};

/**
 * @brief BarkDetector API
 * 
 * Main interface for bark detection using TensorFlow Lite Micro.
 * Supports 4-class classification: DOG_BARK, SPEECH, AMBIENT, SILENCE
 */
class BarkDetector {
public:
    /**
     * @brief Construct a new BarkDetector object
     */
    BarkDetector();
    
    /**
     * @brief Destroy the BarkDetector object
     */
    ~BarkDetector();
    
    /**
     * @brief Initialize the detector with model data
     * 
     * @param model_data Pointer to TFLite model data (aligned)
     * @param model_size Size of model data in bytes
     * @param tensor_arena Pointer to tensor arena memory
     * @param arena_size Size of tensor arena in bytes (recommend 32KB-64KB)
     * @return true if initialization successful
     */
    bool initialize(const void* model_data, size_t model_size,
                   void* tensor_arena, size_t arena_size);
    
    /**
     * @brief Initialize with default configuration
     * 
     * @param config Configuration parameters
     * @return true if initialization successful
     */
    bool initialize(const BarkDetectorConfig& config);
    
    /**
     * @brief Process audio samples and detect barks
     * 
     * @param audio_samples Pointer to audio samples (16-bit PCM)
     * @param num_samples Number of samples (should match model input requirement)
     * @param sample_rate Sample rate in Hz (default 16000)
     * @return DetectionResult with classification and confidence
     */
    DetectionResult process(const int16_t* audio_samples, size_t num_samples, 
                           uint32_t sample_rate = 16000);
    
    /**
     * @brief Extract features from audio samples
     * 
     * @param audio_samples Pointer to audio samples
     * @param num_samples Number of samples
     * @param features Output buffer for features (will be filled with mel spectrogram)
     * @param feature_size Expected feature size
     * @return true if feature extraction successful
     */
    bool extract_features(const int16_t* audio_samples, size_t num_samples,
                         float* features, size_t feature_size);
    
    /**
     * @brief Update configuration parameters
     * 
     * @param config New configuration
     */
    void update_config(const BarkDetectorConfig& config);
    
    /**
     * @brief Get current configuration
     * 
     * @return BarkDetectorConfig Current configuration
     */
    BarkDetectorConfig get_config() const;
    
    /**
     * @brief Get performance statistics
     * 
     * @return PerformanceStats Current performance metrics
     */
    PerformanceStats get_stats() const;
    
    /**
     * @brief Reset statistics counters
     */
    void reset_stats();
    
    /**
     * @brief Check if detector is initialized and ready
     * 
     * @return true if ready to process audio
     */
    bool is_ready() const;
    
    /**
     * @brief Get the input size required by the model
     * 
     * @return size_t Number of samples required per inference
     */
    size_t get_input_size() const;
    
    /**
     * @brief Get the feature dimensions (mel bands x time frames)
     * 
     * @param mel_bands Output: number of mel frequency bands
     * @param time_frames Output: number of time frames
     */
    void get_feature_dimensions(size_t& mel_bands, size_t& time_frames) const;

private:
    // pImpl pattern - implementation details hidden
    class Impl;
    Impl* pImpl;
};

// Helper function to convert BarkClass to string
inline const char* bark_class_to_string(BarkClass cls) {
    switch (cls) {
        case BarkClass::DOG_BARK: return "DOG_BARK";
        case BarkClass::SPEECH: return "SPEECH";
        case BarkClass::AMBIENT: return "AMBIENT";
        case BarkClass::SILENCE: return "SILENCE";
        default: return "UNKNOWN";
    }
}

#endif // BARK_DETECTOR_API_H

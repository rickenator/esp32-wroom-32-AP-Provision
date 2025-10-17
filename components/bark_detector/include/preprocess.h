/**
 * @file preprocess.h
 * @brief Audio preprocessing module for bark detection
 * 
 * Implements DC-block filtering, normalization, noise gating,
 * and windowing functions optimized for ESP32-S3.
 */

#pragma once

#include <cstdint>
#include <cmath>

namespace BarkDetector {

/**
 * @brief Preprocessing configuration
 */
struct PreprocessConfig {
    // DC-block filter
    bool enable_dc_block = true;
    float dc_block_alpha = 0.995f;     // High-pass cutoff (~80 Hz @ 16kHz)
    
    // Automatic Gain Control (AGC)
    bool enable_agc = true;
    float agc_target_level = 0.3f;     // Target RMS level
    float agc_attack_time = 0.001f;    // Attack time in seconds
    float agc_release_time = 0.1f;     // Release time in seconds
    float agc_max_gain = 8.0f;         // Maximum gain multiplier
    
    // Noise gate
    bool enable_noise_gate = true;
    float noise_gate_threshold = 0.001f; // Linear threshold (0.0-1.0)
    float noise_gate_ratio = 10.0f;    // Compression ratio below threshold
    
    // Pre-emphasis filter (optional)
    bool enable_pre_emphasis = false;
    float pre_emphasis_alpha = 0.97f;  // Pre-emphasis coefficient
    
    // Window function
    enum WindowType {
        RECTANGULAR = 0,
        HAMMING = 1,
        HANNING = 2,
        BLACKMAN = 3
    };
    WindowType window_type = HAMMING;
};

/**
 * @brief Audio preprocessing class
 */
class Preprocess {
public:
    /**
     * @brief Constructor
     */
    Preprocess();
    
    /**
     * @brief Destructor  
     */
    ~Preprocess();
    
    /**
     * @brief Initialize preprocessor
     * @param config Preprocessing configuration
     * @param sample_rate Sample rate in Hz
     * @param frame_size Frame size in samples
     * @return true if successful
     */
    bool initialize(const PreprocessConfig& config, uint32_t sample_rate, size_t frame_size);
    
    /**
     * @brief Process audio frame
     * @param input Input samples (16-bit PCM)
     * @param output Output samples (float, -1.0 to 1.0)
     * @param sample_count Number of samples
     * @return true if successful
     */
    bool processFrame(const int16_t* input, float* output, size_t sample_count);
    
    /**
     * @brief Apply window function to frame
     * @param samples Float samples to window
     * @param sample_count Number of samples
     */
    void applyWindow(float* samples, size_t sample_count);
    
    /**
     * @brief Get current configuration
     * @return Current config
     */
    const PreprocessConfig& getConfig() const;
    
    /**
     * @brief Update configuration
     * @param config New configuration
     * @return true if valid
     */
    bool setConfig(const PreprocessConfig& config);
    
    /**
     * @brief Get preprocessing statistics
     */
    struct Stats {
        uint32_t frames_processed;
        float avg_input_level;
        float avg_output_level;
        float current_agc_gain;
        uint32_t noise_gate_activations;
    };
    
    /**
     * @brief Get statistics
     * @return Current stats
     */
    Stats getStats() const;
    
    /**
     * @brief Reset statistics
     */
    void resetStats();

private:
    // Filter implementations
    void dcBlockFilter(const int16_t* input, float* output, size_t count);
    void automaticGainControl(float* samples, size_t count);
    void noiseGate(float* samples, size_t count);
    void preEmphasisFilter(float* samples, size_t count);
    
    // Window function generation
    void generateWindow();
    
    // Member variables
    PreprocessConfig config_;
    uint32_t sample_rate_;
    size_t frame_size_;
    
    // Filter states
    float dc_block_state_;         // DC-block filter state
    float agc_gain_;              // Current AGC gain
    float agc_envelope_;          // AGC envelope follower
    float pre_emphasis_state_;    // Pre-emphasis filter state
    
    // Window coefficients
    float* window_coeffs_;
    
    // Statistics
    Stats stats_;
    
    // Constants for optimization
    static constexpr float INT16_TO_FLOAT = 1.0f / 32768.0f;
    static constexpr float FLOAT_TO_INT16 = 32768.0f;
};

/**
 * @brief Utility functions for audio processing
 */
namespace AudioUtils {
    
    /**
     * @brief Generate Hamming window coefficients
     * @param coeffs Output coefficients array
     * @param size Window size
     */
    void generateHammingWindow(float* coeffs, size_t size);
    
    /**
     * @brief Generate Hanning window coefficients
     * @param coeffs Output coefficients array  
     * @param size Window size
     */
    void generateHanningWindow(float* coeffs, size_t size);
    
    /**
     * @brief Generate Blackman window coefficients
     * @param coeffs Output coefficients array
     * @param size Window size
     */
    void generateBlackmanWindow(float* coeffs, size_t size);
    
    /**
     * @brief Calculate envelope follower
     * @param input Current input level
     * @param envelope Current envelope state
     * @param attack_coeff Attack coefficient
     * @param release_coeff Release coefficient
     * @return New envelope value
     */
    float envelopeFollower(float input, float envelope, 
                          float attack_coeff, float release_coeff);
    
    /**
     * @brief Apply soft knee compression
     * @param input Input level
     * @param threshold Threshold level
     * @param ratio Compression ratio
     * @param knee_width Knee width
     * @return Compressed level
     */
    float softKneeCompress(float input, float threshold, 
                          float ratio, float knee_width = 0.1f);
}

} // namespace BarkDetector
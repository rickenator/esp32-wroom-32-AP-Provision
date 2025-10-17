/**
 * @file feature_extractor.h
 * @brief Audio feature extraction for bark detection
 * 
 * Implements Log-Mel spectrogram and MFCC computation optimized
 * for ESP32-S3 SIMD instructions and bark classification.
 */

#pragma once

#include <cstdint>
#include <complex>

namespace BarkDetector {

/**
 * @brief Feature extraction configuration
 */
struct FeatureConfig {
    // FFT parameters
    uint16_t fft_size = 512;           // FFT size (power of 2)
    uint16_t hop_length = 160;         // Hop length in samples (10ms @ 16kHz)
    uint16_t window_length = 400;      // Window length (25ms @ 16kHz)
    
    // Mel filterbank parameters
    uint8_t mel_bands = 40;            // Number of Mel bands
    float mel_low_freq = 80.0f;        // Low frequency (Hz)
    float mel_high_freq = 8000.0f;     // High frequency (Hz)
    
    // MFCC parameters
    uint8_t mfcc_coeffs = 13;          // Number of MFCC coefficients
    bool use_log_energy = true;        // Include log energy as first coeff
    bool use_delta = false;            // Include delta coefficients
    bool use_delta_delta = false;      // Include delta-delta coefficients
    
    // Normalization
    bool normalize_features = true;
    bool apply_liftering = true;       // MFCC liftering
    uint8_t lifter_coeff = 22;         // Liftering coefficient
    
    // Output format
    enum FeatureType {
        LOG_MEL_SPECTROGRAM = 0,
        MFCC = 1,
        BOTH = 2
    };
    FeatureType feature_type = LOG_MEL_SPECTROGRAM;
};

/**
 * @brief Feature extraction class optimized for ESP32-S3
 */
class FeatureExtractor {
public:
    /**
     * @brief Constructor
     */
    FeatureExtractor();
    
    /**
     * @brief Destructor
     */
    ~FeatureExtractor();
    
    /**
     * @brief Initialize feature extractor
     * @param config Feature extraction configuration
     * @param sample_rate Sample rate in Hz
     * @return true if successful
     */
    bool initialize(const FeatureConfig& config, uint32_t sample_rate);
    
    /**
     * @brief Extract features from audio frame
     * @param audio_samples Input audio samples (float, windowed)
     * @param sample_count Number of samples
     * @param features Output feature matrix (row-major)
     * @return true if successful
     */
    bool extractFeatures(const float* audio_samples, size_t sample_count, float* features);
    
    /**
     * @brief Get feature matrix dimensions
     * @param rows Number of time frames
     * @param cols Number of feature dimensions
     */
    void getFeatureDimensions(uint16_t& rows, uint16_t& cols) const;
    
    /**
     * @brief Get current configuration
     * @return Current config
     */
    const FeatureConfig& getConfig() const;
    
    /**
     * @brief Update configuration (requires reinitialization)
     * @param config New configuration
     * @return true if valid
     */
    bool setConfig(const FeatureConfig& config);
    
    /**
     * @brief Get processing statistics
     */
    struct Stats {
        uint32_t frames_processed;
        float avg_processing_time_ms;
        uint32_t fft_computations;
        uint32_t mel_computations;
        uint32_t mfcc_computations;
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
    // Core processing functions
    bool computeFFT(const float* input, std::complex<float>* output, size_t size);
    bool computeMelFilterbank(const std::complex<float>* fft_data, float* mel_features);
    bool computeMFCC(const float* mel_features, float* mfcc_features);
    bool computeLogMelSpectrogram(const float* mel_features, float* log_mel);
    
    // SIMD-optimized functions for ESP32-S3
    void simdVectorMultiply(const float* a, const float* b, float* result, size_t size);
    void simdVectorAdd(const float* a, const float* b, float* result, size_t size);
    void simdComplexMagnitude(const std::complex<float>* input, float* output, size_t size);
    
    // Initialization helpers
    bool initializeFFT();
    bool initializeMelFilterbank();
    bool initializeDCT();
    void generateMelFilters();
    void generateDCTMatrix();
    
    // Member variables
    FeatureConfig config_;
    uint32_t sample_rate_;
    
    // FFT state
    float* fft_input_;
    std::complex<float>* fft_output_;
    float* magnitude_spectrum_;
    
    // Mel filterbank
    float** mel_filters_;              // Mel filter matrix
    uint16_t* mel_filter_starts_;      // Filter start indices
    uint16_t* mel_filter_lengths_;     // Filter lengths
    float* mel_features_;              // Mel filterbank output
    
    // DCT matrix for MFCC
    float* dct_matrix_;                // DCT transform matrix
    
    // Working buffers
    float* window_buffer_;             // Windowed audio samples
    float* feature_buffer_;            // Temporary feature buffer
    
    // Statistics
    Stats stats_;
    uint32_t processing_start_time_;
    
    // Constants
    static constexpr float MEL_SCALE_FACTOR = 1127.01048f;
    static constexpr float LOG_OFFSET = 1e-10f;  // Small offset for log computation
};

/**
 * @brief Audio processing utilities optimized for ESP32-S3
 */
namespace FeatureUtils {
    
    /**
     * @brief Convert frequency to Mel scale
     * @param freq Frequency in Hz
     * @return Mel scale value
     */
    float frequencyToMel(float freq);
    
    /**
     * @brief Convert Mel scale to frequency
     * @param mel Mel scale value
     * @return Frequency in Hz
     */
    float melToFrequency(float mel);
    
    /**
     * @brief Generate Mel-spaced frequency points
     * @param low_freq Low frequency (Hz)
     * @param high_freq High frequency (Hz)
     * @param num_bands Number of bands
     * @param mel_points Output array for Mel points
     */
    void generateMelPoints(float low_freq, float high_freq, uint8_t num_bands, float* mel_points);
    
    /**
     * @brief Apply Hamming window optimized for ESP32-S3
     * @param input Input samples
     * @param output Windowed output
     * @param size Window size
     */
    void applyHammingWindowSIMD(const float* input, float* output, size_t size);
    
    /**
     * @brief Compute power spectrum from complex FFT
     * @param fft_data Complex FFT output
     * @param power_spectrum Output power spectrum
     * @param size FFT size
     */
    void computePowerSpectrum(const std::complex<float>* fft_data, 
                             float* power_spectrum, size_t size);
    
    /**
     * @brief Apply log with small offset to prevent -inf
     * @param input Input values
     * @param output Log output
     * @param size Array size
     */
    void safeLog(const float* input, float* output, size_t size);
    
    /**
     * @brief Normalize feature matrix (mean=0, std=1)
     * @param features Feature matrix
     * @param rows Number of rows
     * @param cols Number of columns
     */
    void normalizeFeatures(float* features, uint16_t rows, uint16_t cols);
    
    /**
     * @brief Apply liftering to MFCC coefficients
     * @param mfcc_coeffs MFCC coefficients
     * @param num_coeffs Number of coefficients
     * @param lifter_param Liftering parameter
     */
    void applyLiftering(float* mfcc_coeffs, uint8_t num_coeffs, uint8_t lifter_param);
}

} // namespace BarkDetector
/**
 * @file bark_detector.cpp
 * @brief BarkDetector implementation with TensorFlow Lite Micro
 * 
 * DESIGN NOTES:
 * 
 * 1. Memory Management:
 *    - Dynamic allocation is used for audio processing buffers
 *    - ESP32 typically has sufficient heap for these allocations
 *    - Allocation failures will be caught during initialization
 *    - Consider using static allocation if heap is constrained
 * 
 * 2. Error Handling:
 *    - Errors are logged via LOG_ERROR macros
 *    - Can be adapted to ESP_LOGE or platform-specific logging
 *    - Failed operations return false/null results
 * 
 * 3. Platform Compatibility:
 *    - Works with both Arduino and ESP-IDF frameworks
 *    - Timing functions (millis/micros) handle overflow as expected
 *    - All memory allocated is freed in destructor
 * 
 * 4. Model Integration:
 *    - Requires user-trained model via model_data.h
 *    - BARK_DETECTOR_MODEL_DATA_AVAILABLE flag must be set after training
 *    - See TRAINING.md for complete training pipeline
 */

#include "bark_detector_api.h"
#include <cmath>
#include <cstring>
#include <cstdlib>  // for std::abs
#include <algorithm>

// Arduino/ESP32 framework for millis() and micros()
#ifdef ARDUINO
#include <Arduino.h>
#else
// For ESP-IDF, these are available from esp_timer.h
#include "esp_timer.h"
// Compatibility wrappers for ESP-IDF
// Note: These will overflow after ~49 days (millis) and ~71 minutes (micros)
// This matches Arduino behavior and is expected for ESP32 applications
inline uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000ULL); }
inline uint32_t micros() { return (uint32_t)esp_timer_get_time(); }
#endif

// TensorFlow Lite Micro includes
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/system_setup.h"
#include "tensorflow/lite/schema/schema_generated.h"

// Audio processing constants
#define SAMPLE_RATE 16000
#define FFT_SIZE 512
#define HOP_LENGTH 256
#define MEL_BANDS 40
#define N_FFT_BINS (FFT_SIZE / 2 + 1)
#define MAX_TIME_FRAMES 32
#define MAX_MEDIAN_FILTER_SIZE 10  // Maximum supported median filter size

// Mathematical constants
#define M_PI 3.14159265358979323846
#define M_SQRT2 1.41421356237309504880

// TODO: Replace this placeholder with your trained model data
// After training, generate model_data.h using convert_to_tflite.py
// and copy it to this directory
#include "model_data.h"  // Contains: extern const unsigned char g_model_data[]; extern const int g_model_data_len;

// Helper macro for logging (can be replaced with ESP_LOG or custom logging)
#ifndef LOG_DEBUG
#define LOG_DEBUG(fmt, ...) // printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_INFO
#define LOG_INFO(fmt, ...) // printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#endif
#ifndef LOG_ERROR
#define LOG_ERROR(fmt, ...) printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
#endif

/**
 * @brief Utility functions for audio processing
 */
namespace AudioUtils {

// Calculate RMS (Root Mean Square) of audio samples
inline float calculate_rms(const int16_t* samples, size_t num_samples) {
    if (num_samples == 0) return 0.0f;
    
    float sum = 0.0f;
    for (size_t i = 0; i < num_samples; i++) {
        float normalized = samples[i] / 32768.0f;
        sum += normalized * normalized;
    }
    return sqrtf(sum / num_samples);
}

// Calculate peak amplitude
inline float calculate_peak(const int16_t* samples, size_t num_samples) {
    int16_t max_val = 0;
    for (size_t i = 0; i < num_samples; i++) {
        // Use std::abs to handle INT16_MIN correctly
        int16_t abs_val = std::abs(samples[i]);
        if (abs_val > max_val) max_val = abs_val;
    }
    return max_val / 32768.0f;
}

// Convert amplitude to dB
inline float amplitude_to_db(float amplitude) {
    if (amplitude < 1e-10f) return -100.0f;
    return 20.0f * log10f(amplitude);
}

// Apply Hamming window
inline void apply_hamming_window(float* signal, size_t length) {
    for (size_t i = 0; i < length; i++) {
        float w = 0.54f - 0.46f * cosf(2.0f * M_PI * i / (length - 1));
        signal[i] *= w;
    }
}

// Normalize audio with AGC (Automatic Gain Control)
void apply_agc(int16_t* samples, size_t num_samples, float target_level) {
    float rms = calculate_rms(samples, num_samples);
    if (rms < 1e-6f) return; // Avoid division by zero
    
    float gain = target_level / rms;
    // Limit gain to prevent clipping
    if (gain > 4.0f) gain = 4.0f;
    
    for (size_t i = 0; i < num_samples; i++) {
        float sample = samples[i] * gain;
        // Clip to int16 range
        if (sample > 32767.0f) sample = 32767.0f;
        if (sample < -32768.0f) sample = -32768.0f;
        samples[i] = (int16_t)sample;
    }
}

// Apply noise gate
bool apply_noise_gate(const int16_t* samples, size_t num_samples, float threshold_db) {
    float rms = calculate_rms(samples, num_samples);
    float db = amplitude_to_db(rms);
    return db > threshold_db;
}

} // namespace AudioUtils

/**
 * @brief FFT implementation (simplified for ESP32)
 */
namespace FFT {

// Complex number structure
struct Complex {
    float real;
    float imag;
    
    Complex() : real(0), imag(0) {}
    Complex(float r, float i) : real(r), imag(i) {}
    
    Complex operator+(const Complex& other) const {
        return Complex(real + other.real, imag + other.imag);
    }
    
    Complex operator-(const Complex& other) const {
        return Complex(real - other.real, imag - other.imag);
    }
    
    Complex operator*(const Complex& other) const {
        return Complex(
            real * other.real - imag * other.imag,
            real * other.imag + imag * other.real
        );
    }
    
    float magnitude() const {
        return sqrtf(real * real + imag * imag);
    }
};

// Cooley-Tukey FFT algorithm (radix-2 DIT)
void fft(Complex* data, int n) {
    // Bit-reversal permutation
    int j = 0;
    for (int i = 0; i < n - 1; i++) {
        if (i < j) {
            Complex temp = data[i];
            data[i] = data[j];
            data[j] = temp;
        }
        int k = n / 2;
        while (k <= j) {
            j -= k;
            k /= 2;
        }
        j += k;
    }
    
    // FFT computation
    for (int len = 2; len <= n; len *= 2) {
        float angle = -2.0f * M_PI / len;
        Complex wlen(cosf(angle), sinf(angle));
        
        for (int i = 0; i < n; i += len) {
            Complex w(1, 0);
            for (int j = 0; j < len / 2; j++) {
                Complex u = data[i + j];
                Complex v = w * data[i + j + len / 2];
                data[i + j] = u + v;
                data[i + j + len / 2] = u - v;
                w = w * wlen;
            }
        }
    }
}

} // namespace FFT

/**
 * @brief Mel filterbank for feature extraction
 */
class MelFilterbank {
public:
    MelFilterbank() : initialized(false) {}
    
    bool initialize(int sample_rate, int n_fft, int n_mels) {
        this->sample_rate = sample_rate;
        this->n_fft = n_fft;
        this->n_mels = n_mels;
        this->n_fft_bins = n_fft / 2 + 1;
        
        // Create mel filterbank
        create_mel_filters();
        initialized = true;
        return true;
    }
    
    void apply(const float* power_spectrum, float* mel_energies) {
        if (!initialized) return;
        
        for (int i = 0; i < n_mels; i++) {
            mel_energies[i] = 0.0f;
            for (int j = 0; j < n_fft_bins; j++) {
                int filter_idx = i * n_fft_bins + j;
                if (filter_idx < MAX_FILTER_BANKS) {
                    mel_energies[i] += power_spectrum[j] * mel_filters[filter_idx];
                }
            }
            // Add small epsilon to avoid log(0)
            if (mel_energies[i] < 1e-10f) mel_energies[i] = 1e-10f;
            // Convert to log scale (dB)
            mel_energies[i] = 10.0f * log10f(mel_energies[i]);
        }
    }
    
private:
    static const int MAX_FILTER_BANKS = MEL_BANDS * N_FFT_BINS; // 40 * 257
    float mel_filters[MAX_FILTER_BANKS];
    int sample_rate;
    int n_fft;
    int n_mels;
    int n_fft_bins;
    bool initialized;
    
    float hz_to_mel(float hz) {
        return 2595.0f * log10f(1.0f + hz / 700.0f);
    }
    
    float mel_to_hz(float mel) {
        return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
    }
    
    void create_mel_filters() {
        // Initialize to zero
        memset(mel_filters, 0, sizeof(mel_filters));
        
        // Create mel-spaced frequency points
        float mel_min = hz_to_mel(0);
        float mel_max = hz_to_mel(sample_rate / 2.0f);
        float* mel_points = new float[n_mels + 2];
        
        for (int i = 0; i < n_mels + 2; i++) {
            float mel = mel_min + (mel_max - mel_min) * i / (n_mels + 1);
            mel_points[i] = mel_to_hz(mel);
        }
        
        // Create triangular filters
        float* fft_freqs = new float[n_fft_bins];
        for (int i = 0; i < n_fft_bins; i++) {
            fft_freqs[i] = i * sample_rate / (float)n_fft;
        }
        
        for (int i = 0; i < n_mels; i++) {
            float left = mel_points[i];
            float center = mel_points[i + 1];
            float right = mel_points[i + 2];
            
            for (int j = 0; j < n_fft_bins; j++) {
                float freq = fft_freqs[j];
                float weight = 0.0f;
                
                if (freq >= left && freq <= center) {
                    weight = (freq - left) / (center - left);
                } else if (freq > center && freq <= right) {
                    weight = (right - freq) / (right - center);
                }
                
                mel_filters[i * n_fft_bins + j] = weight;
            }
        }
        
        delete[] mel_points;
        delete[] fft_freqs;
    }
};

/**
 * @brief Implementation class (pImpl pattern)
 */
class BarkDetector::Impl {
public:
    // TensorFlow Lite Micro components
    const tflite::Model* model;
    tflite::MicroInterpreter* interpreter;
    TfLiteTensor* input_tensor;
    TfLiteTensor* output_tensor;
    tflite::MicroMutableOpResolver<10>* op_resolver;
    
    // Memory management
    uint8_t* tensor_arena;
    size_t tensor_arena_size;
    bool owns_tensor_arena;  // True if we allocated the arena and should free it
    
    // Audio processing
    MelFilterbank mel_filterbank;
    FFT::Complex* fft_buffer;
    float* power_spectrum;
    float* mel_energies;
    
    // Configuration
    BarkDetectorConfig config;
    
    // State
    bool initialized;
    size_t input_size;
    size_t mel_bands;
    size_t time_frames;
    
    // Temporal filtering
    float ema_confidence[4];  // EMA for each class
    float* median_buffer;
    size_t median_buffer_idx;
    
    // Statistics
    PerformanceStats stats;
    uint32_t inference_time_accumulator;
    
    Impl() : model(nullptr), 
             interpreter(nullptr),
             input_tensor(nullptr),
             output_tensor(nullptr),
             op_resolver(nullptr),
             tensor_arena(nullptr),
             tensor_arena_size(0),
             owns_tensor_arena(false),
             fft_buffer(nullptr),
             power_spectrum(nullptr),
             mel_energies(nullptr),
             initialized(false),
             input_size(0),
             mel_bands(MEL_BANDS),
             time_frames(MAX_TIME_FRAMES),
             median_buffer(nullptr),
             median_buffer_idx(0),
             inference_time_accumulator(0) {
        
        // Default configuration
        config.confidence_threshold = 0.7f;
        config.min_bark_duration_ms = 300;
        config.agc_target_level = 0.5f;
        config.noise_gate_threshold = -40.0f;
        config.enable_temporal_filter = true;
        config.ema_alpha = 0.3f;
        config.median_filter_size = 3;
        
        // Initialize EMA
        for (int i = 0; i < 4; i++) {
            ema_confidence[i] = 0.0f;
        }
        
        // Initialize stats
        memset(&stats, 0, sizeof(stats));
    }
    
    ~Impl() {
        cleanup();
    }
    
    void cleanup() {
        if (interpreter) {
            delete interpreter;
            interpreter = nullptr;
        }
        if (op_resolver) {
            delete op_resolver;
            op_resolver = nullptr;
        }
        if (tensor_arena && owns_tensor_arena) {
            free(tensor_arena);
            tensor_arena = nullptr;
            owns_tensor_arena = false;
        }
        if (fft_buffer) {
            delete[] fft_buffer;
            fft_buffer = nullptr;
        }
        if (power_spectrum) {
            delete[] power_spectrum;
            power_spectrum = nullptr;
        }
        if (mel_energies) {
            delete[] mel_energies;
            mel_energies = nullptr;
        }
        if (median_buffer) {
            delete[] median_buffer;
            median_buffer = nullptr;
        }
        initialized = false;
    }
    
    bool init_model(const void* model_data, size_t model_size,
                   void* arena, size_t arena_size, bool take_ownership = false) {
        if (initialized) {
            cleanup();
        }
        
        LOG_INFO("Initializing BarkDetector...");
        
        // Store tensor arena
        tensor_arena = (uint8_t*)arena;
        tensor_arena_size = arena_size;
        owns_tensor_arena = take_ownership;
        
        // Load model
        model = tflite::GetModel(model_data);
        if (model->version() != TFLITE_SCHEMA_VERSION) {
            LOG_ERROR("Model schema version mismatch: %d vs %d",
                     model->version(), TFLITE_SCHEMA_VERSION);
            return false;
        }
        
        // Create op resolver and add required operations
        op_resolver = new tflite::MicroMutableOpResolver<10>();
        op_resolver->AddConv2D();
        op_resolver->AddMaxPool2D();
        op_resolver->AddFullyConnected();
        op_resolver->AddSoftmax();
        op_resolver->AddReshape();
        op_resolver->AddRelu();
        op_resolver->AddQuantize();
        op_resolver->AddDequantize();
        
        // Create interpreter
        interpreter = new tflite::MicroInterpreter(
            model, *op_resolver, tensor_arena, tensor_arena_size);
        
        // Allocate tensors
        TfLiteStatus allocate_status = interpreter->AllocateTensors();
        if (allocate_status != kTfLiteOk) {
            LOG_ERROR("Failed to allocate tensors");
            cleanup();
            return false;
        }
        
        // Get input and output tensors
        input_tensor = interpreter->input(0);
        output_tensor = interpreter->output(0);
        
        if (!input_tensor || !output_tensor) {
            LOG_ERROR("Failed to get input/output tensors");
            cleanup();
            return false;
        }
        
        // Determine input size from tensor dimensions
        // Expected: [batch, time_frames, mel_bands, channels]
        if (input_tensor->dims->size >= 3) {
            time_frames = input_tensor->dims->data[1];
            mel_bands = input_tensor->dims->data[2];
        }
        
        input_size = time_frames * mel_bands;
        
        LOG_INFO("Model loaded: input_size=%d, mel_bands=%d, time_frames=%d",
                input_size, mel_bands, time_frames);
        
        // Allocate audio processing buffers
        fft_buffer = new (std::nothrow) FFT::Complex[FFT_SIZE];
        power_spectrum = new (std::nothrow) float[N_FFT_BINS];
        mel_energies = new (std::nothrow) float[mel_bands];
        median_buffer = new (std::nothrow) float[config.median_filter_size * 4]; // For all 4 classes
        
        if (!fft_buffer || !power_spectrum || !mel_energies || !median_buffer) {
            LOG_ERROR("Failed to allocate audio processing buffers");
            cleanup();
            return false;
        }
        
        // Initialize mel filterbank
        if (!mel_filterbank.initialize(SAMPLE_RATE, FFT_SIZE, mel_bands)) {
            LOG_ERROR("Failed to initialize mel filterbank");
            cleanup();
            return false;
        }
        
        initialized = true;
        LOG_INFO("BarkDetector initialized successfully");
        return true;
    }
    
    bool extract_mel_spectrogram(const int16_t* audio_samples, size_t num_samples,
                                float* output_features) {
        if (!initialized) return false;
        
        size_t samples_per_frame = FFT_SIZE;
        size_t required_samples = (time_frames - 1) * HOP_LENGTH + samples_per_frame;
        
        if (num_samples < required_samples) {
            LOG_ERROR("Insufficient samples: %d < %d", num_samples, required_samples);
            return false;
        }
        
        // Extract mel spectrogram for each time frame
        for (size_t frame = 0; frame < time_frames; frame++) {
            size_t start_idx = frame * HOP_LENGTH;
            
            // Copy samples to FFT buffer and convert to float
            for (size_t i = 0; i < samples_per_frame; i++) {
                if (start_idx + i < num_samples) {
                    fft_buffer[i].real = audio_samples[start_idx + i] / 32768.0f;
                    fft_buffer[i].imag = 0.0f;
                } else {
                    fft_buffer[i].real = 0.0f;
                    fft_buffer[i].imag = 0.0f;
                }
            }
            
            // Apply Hamming window
            for (size_t i = 0; i < samples_per_frame; i++) {
                float w = 0.54f - 0.46f * cosf(2.0f * M_PI * i / (samples_per_frame - 1));
                fft_buffer[i].real *= w;
            }
            
            // Compute FFT
            FFT::fft(fft_buffer, FFT_SIZE);
            
            // Compute power spectrum
            for (int i = 0; i < N_FFT_BINS; i++) {
                power_spectrum[i] = fft_buffer[i].real * fft_buffer[i].real +
                                   fft_buffer[i].imag * fft_buffer[i].imag;
            }
            
            // Apply mel filterbank
            mel_filterbank.apply(power_spectrum, mel_energies);
            
            // Copy to output (transposed: [time_frames, mel_bands] -> [mel_bands, time_frames])
            for (size_t i = 0; i < mel_bands; i++) {
                output_features[frame * mel_bands + i] = mel_energies[i];
            }
        }
        
        return true;
    }
    
    void apply_temporal_filter(float* confidences, size_t num_classes) {
        if (!config.enable_temporal_filter) return;
        
        // Apply EMA (Exponential Moving Average)
        for (size_t i = 0; i < num_classes; i++) {
            ema_confidence[i] = config.ema_alpha * confidences[i] +
                               (1.0f - config.ema_alpha) * ema_confidence[i];
            confidences[i] = ema_confidence[i];
        }
        
        // Apply median filter
        if (config.median_filter_size > 1) {
            // Store current confidences in circular buffer
            for (size_t i = 0; i < num_classes; i++) {
                median_buffer[median_buffer_idx * num_classes + i] = confidences[i];
            }
            median_buffer_idx = (median_buffer_idx + 1) % config.median_filter_size;
            
            // Calculate median for each class
            for (size_t i = 0; i < num_classes; i++) {
                float values[MAX_MEDIAN_FILTER_SIZE];
                size_t count = std::min((size_t)config.median_filter_size, stats.total_inferences);
                
                for (size_t j = 0; j < count; j++) {
                    values[j] = median_buffer[j * num_classes + i];
                }
                
                // Simple sorting for median
                std::sort(values, values + count);
                confidences[i] = values[count / 2];
            }
        }
    }
    
    DetectionResult run_inference(const int16_t* audio_samples, size_t num_samples) {
        DetectionResult result;
        result.detected_class = BarkClass::UNKNOWN;
        result.confidence = 0.0f;
        result.timestamp_ms = 0; // Should be set by caller
        result.is_bark = false;
        
        if (!initialized) {
            LOG_ERROR("Detector not initialized");
            return result;
        }
        
        uint32_t start_time = micros();
        
        // Preprocessing
        uint32_t preproc_start = micros();
        
        // Apply AGC
        int16_t* mutable_samples = new (std::nothrow) int16_t[num_samples];
        if (!mutable_samples) {
            LOG_ERROR("Failed to allocate memory for audio preprocessing");
            return result;
        }
        memcpy(mutable_samples, audio_samples, num_samples * sizeof(int16_t));
        AudioUtils::apply_agc(mutable_samples, num_samples, config.agc_target_level);
        
        // Apply noise gate
        if (!AudioUtils::apply_noise_gate(mutable_samples, num_samples, 
                                         config.noise_gate_threshold)) {
            delete[] mutable_samples;
            result.detected_class = BarkClass::SILENCE;
            result.confidence = 1.0f;
            return result;
        }
        
        // Extract mel spectrogram features
        float* features = input_tensor->data.f;
        if (!extract_mel_spectrogram(mutable_samples, num_samples, features)) {
            delete[] mutable_samples;
            return result;
        }
        
        delete[] mutable_samples;
        
        stats.preprocessing_time_us = micros() - preproc_start;
        
        // Run inference
        TfLiteStatus invoke_status = interpreter->Invoke();
        if (invoke_status != kTfLiteOk) {
            LOG_ERROR("Inference failed");
            return result;
        }
        
        // Get output confidences
        float* output = output_tensor->data.f;
        size_t num_classes = output_tensor->dims->data[output_tensor->dims->size - 1];
        
        // Apply temporal filtering
        apply_temporal_filter(output, num_classes);
        
        // Find class with highest confidence
        float max_confidence = output[0];
        uint8_t max_class = 0;
        
        for (size_t i = 1; i < num_classes; i++) {
            if (output[i] > max_confidence) {
                max_confidence = output[i];
                max_class = i;
            }
        }
        
        // Set result
        result.detected_class = static_cast<BarkClass>(max_class);
        result.confidence = max_confidence;
        result.is_bark = (result.detected_class == BarkClass::DOG_BARK) &&
                        (result.confidence >= config.confidence_threshold);
        
        // Update statistics
        uint32_t inference_time = micros() - start_time;
        stats.inference_time_us = inference_time;
        stats.total_inferences++;
        inference_time_accumulator += inference_time;
        stats.avg_inference_time_us = inference_time_accumulator / stats.total_inferences;
        
        if (result.is_bark) {
            stats.bark_detections++;
        }
        
        // Estimate memory usage (rough approximation)
        stats.memory_usage_kb = tensor_arena_size / 1024.0f;
        
        return result;
    }
};

// BarkDetector public API implementation

BarkDetector::BarkDetector() : pImpl(new Impl()) {
}

BarkDetector::~BarkDetector() {
    delete pImpl;
}

bool BarkDetector::initialize(const void* model_data, size_t model_size,
                              void* tensor_arena, size_t arena_size) {
    return pImpl->init_model(model_data, model_size, tensor_arena, arena_size);
}

bool BarkDetector::initialize(const BarkDetectorConfig& config) {
    pImpl->config = config;
    
    // TODO: User must provide their trained model data
    // The model_data.h file should be generated using the training scripts
    // For now, we use placeholder data that needs to be replaced
    
    // Allocate tensor arena (32KB default)
    // NOTE: This memory is owned by the BarkDetector and will persist for its lifetime
    const size_t arena_size = 32 * 1024;
    void* arena = malloc(arena_size);
    if (!arena) {
        LOG_ERROR("Failed to allocate tensor arena");
        return false;
    }
    
    // TODO: Replace with actual model data after training
    // This will fail until user provides trained model
    #ifdef BARK_DETECTOR_MODEL_DATA_AVAILABLE
    // Pass true to take ownership of the allocated arena
    bool success = pImpl->init_model(g_model_data, g_model_data_len, arena, arena_size, true);
    if (!success) {
        // Free arena on initialization failure (init_model won't take ownership if it fails)
        free(arena);
    }
    // Note: On success, arena ownership transfers to pImpl
    #else
    LOG_ERROR("Model data not available. Please train model and include model_data.h");
    LOG_ERROR("Follow instructions in TRAINING.md to train and convert your model");
    free(arena);  // Free arena since initialization failed
    bool success = false;
    #endif
    
    return success;
}

DetectionResult BarkDetector::process(const int16_t* audio_samples, size_t num_samples,
                                      uint32_t sample_rate) {
    if (sample_rate != SAMPLE_RATE) {
        LOG_ERROR("Unsupported sample rate: %d (expected %d)", sample_rate, SAMPLE_RATE);
        DetectionResult error_result;
        error_result.detected_class = BarkClass::UNKNOWN;
        error_result.confidence = 0.0f;
        error_result.is_bark = false;
        return error_result;
    }
    
    DetectionResult result = pImpl->run_inference(audio_samples, num_samples);
    result.timestamp_ms = millis(); // Use Arduino millis()
    return result;
}

bool BarkDetector::extract_features(const int16_t* audio_samples, size_t num_samples,
                                   float* features, size_t feature_size) {
    if (feature_size != pImpl->input_size) {
        LOG_ERROR("Feature size mismatch: %d != %d", feature_size, pImpl->input_size);
        return false;
    }
    
    return pImpl->extract_mel_spectrogram(audio_samples, num_samples, features);
}

void BarkDetector::update_config(const BarkDetectorConfig& config) {
    pImpl->config = config;
}

BarkDetectorConfig BarkDetector::get_config() const {
    return pImpl->config;
}

PerformanceStats BarkDetector::get_stats() const {
    return pImpl->stats;
}

void BarkDetector::reset_stats() {
    memset(&pImpl->stats, 0, sizeof(PerformanceStats));
    pImpl->inference_time_accumulator = 0;
}

bool BarkDetector::is_ready() const {
    return pImpl->initialized;
}

size_t BarkDetector::get_input_size() const {
    return pImpl->input_size;
}

void BarkDetector::get_feature_dimensions(size_t& mel_bands, size_t& time_frames) const {
    mel_bands = pImpl->mel_bands;
    time_frames = pImpl->time_frames;
}

// Note: micros() and millis() are provided by the Arduino/ESP-IDF framework
// No need to define them here - they are already available on ESP32

/**
 * @file preprocess.cpp
 * @brief Audio preprocessing implementation
 */

#include "preprocess.h"
#include <esp_log.h>
#include <string.h>
#include <algorithm>

static const char* TAG = "Preprocess";

namespace BarkDetector {

Preprocess::Preprocess() 
    : sample_rate_(0)
    , frame_size_(0)
    , dc_block_state_(0.0f)
    , agc_gain_(1.0f)
    , agc_envelope_(0.0f)
    , pre_emphasis_state_(0.0f)
    , window_coeffs_(nullptr) {
    
    memset(&stats_, 0, sizeof(stats_));
}

Preprocess::~Preprocess() {
    if (window_coeffs_) {
        free(window_coeffs_);
    }
}

bool Preprocess::initialize(const PreprocessConfig& config, uint32_t sample_rate, size_t frame_size) {
    config_ = config;
    sample_rate_ = sample_rate;
    frame_size_ = frame_size;
    
    // Generate window coefficients
    if (window_coeffs_) {
        free(window_coeffs_);
    }
    
    window_coeffs_ = (float*)malloc(frame_size * sizeof(float));
    if (!window_coeffs_) {
        ESP_LOGE(TAG, "Failed to allocate window coefficients");
        return false;
    }
    
    generateWindow();
    
    // Initialize filter states
    dc_block_state_ = 0.0f;
    agc_gain_ = 1.0f;
    agc_envelope_ = 0.0f;
    pre_emphasis_state_ = 0.0f;
    
    ESP_LOGI(TAG, "Preprocessor initialized: %d Hz, %d samples", sample_rate, frame_size);
    return true;
}

bool Preprocess::processFrame(const int16_t* input, float* output, size_t sample_count) {
    if (!input || !output || sample_count > frame_size_) {
        return false;
    }
    
    // Convert to float and apply DC-block filter
    if (config_.enable_dc_block) {
        dcBlockFilter(input, output, sample_count);
    } else {
        // Simple conversion without filtering
        for (size_t i = 0; i < sample_count; i++) {
            output[i] = input[i] * INT16_TO_FLOAT;
        }
    }
    
    // Apply pre-emphasis filter
    if (config_.enable_pre_emphasis) {
        preEmphasisFilter(output, sample_count);
    }
    
    // Apply automatic gain control
    if (config_.enable_agc) {
        automaticGainControl(output, sample_count);
    }
    
    // Apply noise gate
    if (config_.enable_noise_gate) {
        noiseGate(output, sample_count);
    }
    
    // Update statistics
    float input_level = 0.0f, output_level = 0.0f;
    for (size_t i = 0; i < sample_count; i++) {
        float input_sample = input[i] * INT16_TO_FLOAT;
        input_level += input_sample * input_sample;
        output_level += output[i] * output[i];
    }
    
    stats_.frames_processed++;
    stats_.avg_input_level = (stats_.avg_input_level * 0.99f) + (sqrtf(input_level / sample_count) * 0.01f);
    stats_.avg_output_level = (stats_.avg_output_level * 0.99f) + (sqrtf(output_level / sample_count) * 0.01f);
    stats_.current_agc_gain = agc_gain_;
    
    return true;
}

void Preprocess::applyWindow(float* samples, size_t sample_count) {
    if (!samples || !window_coeffs_ || sample_count > frame_size_) {
        return;
    }
    
    for (size_t i = 0; i < sample_count; i++) {
        samples[i] *= window_coeffs_[i];
    }
}

const PreprocessConfig& Preprocess::getConfig() const {
    return config_;
}

bool Preprocess::setConfig(const PreprocessConfig& config) {
    config_ = config;
    
    // Regenerate window if type changed
    if (window_coeffs_) {
        generateWindow();
    }
    
    return true;
}

Preprocess::Stats Preprocess::getStats() const {
    return stats_;
}

void Preprocess::resetStats() {
    memset(&stats_, 0, sizeof(stats_));
}

void Preprocess::dcBlockFilter(const int16_t* input, float* output, size_t count) {
    const float alpha = config_.dc_block_alpha;
    
    for (size_t i = 0; i < count; i++) {
        float input_sample = input[i] * INT16_TO_FLOAT;
        
        // High-pass filter: y[n] = x[n] - x[n-1] + α * y[n-1]
        output[i] = input_sample - dc_block_state_ + alpha * (i > 0 ? output[i-1] : 0.0f);
        dc_block_state_ = input_sample;
    }
}

void Preprocess::automaticGainControl(float* samples, size_t count) {
    const float attack_time = config_.agc_attack_time;
    const float release_time = config_.agc_release_time;
    const float target_level = config_.agc_target_level;
    const float max_gain = config_.agc_max_gain;
    
    // Calculate attack and release coefficients
    const float attack_coeff = expf(-1.0f / (attack_time * sample_rate_));
    const float release_coeff = expf(-1.0f / (release_time * sample_rate_));
    
    for (size_t i = 0; i < count; i++) {
        float input_level = fabsf(samples[i]);
        
        // Update envelope follower
        agc_envelope_ = AudioUtils::envelopeFollower(input_level, agc_envelope_, 
                                                    attack_coeff, release_coeff);
        
        // Calculate desired gain
        float desired_gain = (agc_envelope_ > 0.001f) ? target_level / agc_envelope_ : max_gain;
        desired_gain = std::min(desired_gain, max_gain);
        
        // Smooth gain changes
        agc_gain_ += (desired_gain - agc_gain_) * 0.01f;
        
        // Apply gain
        samples[i] *= agc_gain_;
        
        // Prevent clipping
        samples[i] = std::max(-1.0f, std::min(1.0f, samples[i]));
    }
}

void Preprocess::noiseGate(float* samples, size_t count) {
    const float threshold = config_.noise_gate_threshold;
    const float ratio = config_.noise_gate_ratio;
    
    for (size_t i = 0; i < count; i++) {
        float level = fabsf(samples[i]);
        
        if (level < threshold) {
            // Apply compression below threshold
            float compressed_level = AudioUtils::softKneeCompress(level, threshold, ratio);
            float gain = (level > 0.0001f) ? compressed_level / level : 0.0f;
            samples[i] *= gain;
            
            stats_.noise_gate_activations++;
        }
    }
}

void Preprocess::preEmphasisFilter(float* samples, size_t count) {
    const float alpha = config_.pre_emphasis_alpha;
    
    for (size_t i = 0; i < count; i++) {
        float filtered = samples[i] - alpha * pre_emphasis_state_;
        pre_emphasis_state_ = samples[i];
        samples[i] = filtered;
    }
}

void Preprocess::generateWindow() {
    if (!window_coeffs_) return;
    
    switch (config_.window_type) {
        case PreprocessConfig::HAMMING:
            AudioUtils::generateHammingWindow(window_coeffs_, frame_size_);
            break;
        case PreprocessConfig::HANNING:
            AudioUtils::generateHanningWindow(window_coeffs_, frame_size_);
            break;
        case PreprocessConfig::BLACKMAN:
            AudioUtils::generateBlackmanWindow(window_coeffs_, frame_size_);
            break;
        case PreprocessConfig::RECTANGULAR:
        default:
            for (size_t i = 0; i < frame_size_; i++) {
                window_coeffs_[i] = 1.0f;
            }
            break;
    }
}

// Utility functions implementation
namespace AudioUtils {

void generateHammingWindow(float* coeffs, size_t size) {
    const float N = size - 1;
    for (size_t i = 0; i < size; i++) {
        coeffs[i] = 0.54f - 0.46f * cosf(2.0f * M_PI * i / N);
    }
}

void generateHanningWindow(float* coeffs, size_t size) {
    const float N = size - 1;
    for (size_t i = 0; i < size; i++) {
        coeffs[i] = 0.5f * (1.0f - cosf(2.0f * M_PI * i / N));
    }
}

void generateBlackmanWindow(float* coeffs, size_t size) {
    const float N = size - 1;
    for (size_t i = 0; i < size; i++) {
        float x = 2.0f * M_PI * i / N;
        coeffs[i] = 0.42f - 0.5f * cosf(x) + 0.08f * cosf(2.0f * x);
    }
}

float envelopeFollower(float input, float envelope, float attack_coeff, float release_coeff) {
    if (input > envelope) {
        // Attack
        return envelope + (input - envelope) * (1.0f - attack_coeff);
    } else {
        // Release
        return envelope + (input - envelope) * (1.0f - release_coeff);
    }
}

float softKneeCompress(float input, float threshold, float ratio, float knee_width) {
    if (input <= threshold - knee_width / 2.0f) {
        return input;
    } else if (input >= threshold + knee_width / 2.0f) {
        return threshold + (input - threshold) / ratio;
    } else {
        // Soft knee region
        float knee_ratio = 1.0f + (ratio - 1.0f) * 
            powf((input - threshold + knee_width / 2.0f) / knee_width, 2.0f);
        return threshold + (input - threshold) / knee_ratio;
    }
}

} // namespace AudioUtils

} // namespace BarkDetector
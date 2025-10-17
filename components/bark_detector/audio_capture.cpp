/**
 * @file audio_capture.cpp
 * @brief I2S audio capture implementation for ESP32-S3
 */

#include "audio_capture.h"
#include <esp_log.h>
#include <esp_err.h>
#include <string.h>
#include <math.h>

static const char* TAG = "AudioCapture";

namespace BarkDetector {

AudioCapture::AudioCapture() 
    : task_handle_(nullptr)
    , frame_queue_(nullptr)
    , ring_buffer_(nullptr)
    , running_(false)
    , frame_pool_index_(0)
    , mutex_(nullptr) {
    
    // Initialize statistics
    memset(&stats_, 0, sizeof(stats_));
    
    // Create mutex
    mutex_ = xSemaphoreCreateMutex();
    
    // Initialize frame pool
    for (size_t i = 0; i < FRAME_POOL_SIZE; i++) {
        sample_buffers_[i] = (int16_t*)malloc(config_.frame_size_samples * sizeof(int16_t));
        if (!sample_buffers_[i]) {
            ESP_LOGE(TAG, "Failed to allocate sample buffer %d", i);
        }
        frame_pool_[i].samples = sample_buffers_[i];
        frame_pool_[i].sample_count = config_.frame_size_samples;
    }
}

AudioCapture::~AudioCapture() {
    stop();
    
    // Free sample buffers
    for (size_t i = 0; i < FRAME_POOL_SIZE; i++) {
        if (sample_buffers_[i]) {
            free(sample_buffers_[i]);
        }
    }
    
    if (mutex_) {
        vSemaphoreDelete(mutex_);
    }
}

bool AudioCapture::initialize(const I2SConfig& config) {
    if (running_) {
        ESP_LOGW(TAG, "Cannot configure while running");
        return false;
    }
    
    config_ = config;
    
    // Configure I2S
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = config_.sample_rate,
        .bits_per_sample = config_.bits_per_sample,
        .channel_format = config_.channel_format,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL2,
        .dma_buf_count = config_.dma_buf_count,
        .dma_buf_len = config_.dma_buf_len,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };
    
    // Pin configuration
    i2s_pin_config_t pin_config = {
        .bck_io_num = config_.sck_pin,
        .ws_io_num = config_.ws_pin,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = config_.sd_pin
    };
    
    // Install I2S driver
    esp_err_t ret = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install I2S driver: %s", esp_err_to_name(ret));
        return false;
    }
    
    // Set pin configuration
    ret = i2s_set_pin(I2S_NUM_0, &pin_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set I2S pins: %s", esp_err_to_name(ret));
        i2s_driver_uninstall(I2S_NUM_0);
        return false;
    }
    
    // Clear DMA buffers
    ret = i2s_zero_dma_buffer(I2S_NUM_0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to clear DMA buffers: %s", esp_err_to_name(ret));
    }
    
    // Create ring buffer for internal buffering
    ring_buffer_ = xRingbufferCreate(config_.frame_size_samples * 4 * sizeof(int16_t), RINGBUF_TYPE_BYTEBUF);
    if (!ring_buffer_) {
        ESP_LOGE(TAG, "Failed to create ring buffer");
        i2s_driver_uninstall(I2S_NUM_0);
        return false;
    }
    
    ESP_LOGI(TAG, "I2S initialized: %d Hz, %d samples/frame", 
             config_.sample_rate, config_.frame_size_samples);
    
    return true;
}

bool AudioCapture::start(QueueHandle_t frame_queue) {
    if (running_) {
        ESP_LOGW(TAG, "Already running");
        return true;
    }
    
    if (!frame_queue) {
        ESP_LOGE(TAG, "Frame queue is null");
        return false;
    }
    
    frame_queue_ = frame_queue;
    
    // Create capture task on Core 0
    BaseType_t ret = xTaskCreatePinnedToCore(
        captureTask,
        "audio_capture",
        8192,                // Stack size
        this,               // Parameter
        5,                  // Priority (high)
        &task_handle_,      // Task handle
        0                   // Core 0
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create capture task");
        return false;
    }
    
    running_ = true;
    ESP_LOGI(TAG, "Audio capture started");
    return true;
}

void AudioCapture::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // Wait for task to finish
    if (task_handle_) {
        vTaskDelete(task_handle_);
        task_handle_ = nullptr;
    }
    
    // Uninstall I2S driver
    i2s_driver_uninstall(I2S_NUM_0);
    
    // Clean up ring buffer
    if (ring_buffer_) {
        vRingbufferDelete(ring_buffer_);
        ring_buffer_ = nullptr;
    }
    
    ESP_LOGI(TAG, "Audio capture stopped");
}

bool AudioCapture::isRunning() const {
    return running_;
}

const I2SConfig& AudioCapture::getConfig() const {
    return config_;
}

AudioCapture::Stats AudioCapture::getStats() const {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        Stats stats = stats_;
        xSemaphoreGive(mutex_);
        return stats;
    }
    return Stats{};
}

void AudioCapture::resetStats() {
    if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(10)) == pdTRUE) {
        memset(&stats_, 0, sizeof(stats_));
        xSemaphoreGive(mutex_);
    }
}

void AudioCapture::captureTask(void* param) {
    AudioCapture* capture = static_cast<AudioCapture*>(param);
    capture->runCaptureTask();
}

void AudioCapture::runCaptureTask() {
    const size_t read_bytes = config_.frame_size_samples * sizeof(int16_t);
    int16_t* read_buffer = (int16_t*)malloc(read_bytes);
    
    if (!read_buffer) {
        ESP_LOGE(TAG, "Failed to allocate read buffer");
        return;
    }
    
    ESP_LOGI(TAG, "Capture task started on core %d", xPortGetCoreID());
    
    while (running_) {
        size_t bytes_read = 0;
        
        // Read from I2S
        esp_err_t ret = i2s_read(I2S_NUM_0, read_buffer, read_bytes, &bytes_read, portMAX_DELAY);
        
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "I2S read error: %s", esp_err_to_name(ret));
            continue;
        }
        
        if (bytes_read != read_bytes) {
            ESP_LOGW(TAG, "Partial read: %d/%d bytes", bytes_read, read_bytes);
            continue;
        }
        
        // Get frame from pool
        AudioFrame* frame = &frame_pool_[frame_pool_index_];
        frame_pool_index_ = (frame_pool_index_ + 1) % FRAME_POOL_SIZE;
        
        // Copy samples and calculate levels
        memcpy(frame->samples, read_buffer, bytes_read);
        frame->sample_count = bytes_read / sizeof(int16_t);
        frame->timestamp_ms = esp_timer_get_time() / 1000;
        
        calculateLevels(frame->samples, frame->sample_count, 
                       frame->rms_level, frame->peak_level);
        
        // Send frame to processing queue
        if (xQueueSend(frame_queue_, &frame, 0) != pdTRUE) {
            // Queue full - increment error counter
            if (xSemaphoreTake(mutex_, 0) == pdTRUE) {
                stats_.queue_full_errors++;
                xSemaphoreGive(mutex_);
            }
        } else {
            // Update statistics
            if (xSemaphoreTake(mutex_, 0) == pdTRUE) {
                stats_.frames_captured++;
                stats_.total_samples += frame->sample_count;
                xSemaphoreGive(mutex_);
            }
        }
    }
    
    free(read_buffer);
    ESP_LOGI(TAG, "Capture task finished");
}

void AudioCapture::calculateLevels(const int16_t* samples, size_t count, 
                                  float& rms, float& peak) {
    if (!samples || count == 0) {
        rms = peak = 0.0f;
        return;
    }
    
    float sum_squares = 0.0f;
    int16_t max_val = 0;
    
    for (size_t i = 0; i < count; i++) {
        int16_t sample = samples[i];
        sum_squares += (float)sample * sample;
        
        int16_t abs_sample = (sample < 0) ? -sample : sample;
        if (abs_sample > max_val) {
            max_val = abs_sample;
        }
    }
    
    // Calculate RMS (0.0 - 1.0)
    rms = sqrtf(sum_squares / count) / 32768.0f;
    
    // Calculate peak (0.0 - 1.0)
    peak = (float)max_val / 32768.0f;
}

} // namespace BarkDetector
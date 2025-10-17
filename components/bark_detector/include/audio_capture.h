/**
 * @file audio_capture.h
 * @brief I2S audio capture module for ESP32-S3
 * 
 * Handles INMP441/SPH0645 I2S microphone interface with DMA buffering
 * optimized for real-time audio processing on Core 0.
 */

#pragma once

#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/ringbuf.h>
#include <cstdint>

namespace BarkDetector {

/**
 * @brief Audio frame structure for inter-task communication
 */
struct AudioFrame {
    int16_t* samples;           // PCM samples (16-bit)
    size_t sample_count;        // Number of samples in frame
    uint32_t timestamp_ms;      // Capture timestamp
    float rms_level;           // Pre-calculated RMS level
    float peak_level;          // Pre-calculated peak level
};

/**
 * @brief I2S configuration for ESP32-S3
 */
struct I2SConfig {
    // GPIO pins
    gpio_num_t sck_pin = GPIO_NUM_14;    // Serial Clock (BCLK)
    gpio_num_t ws_pin = GPIO_NUM_15;     // Word Select (LRCLK) 
    gpio_num_t sd_pin = GPIO_NUM_32;     // Serial Data (DIN)
    
    // Audio parameters
    uint32_t sample_rate = 16000;        // Sample rate in Hz
    i2s_bits_per_sample_t bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    i2s_channel_fmt_t channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    
    // DMA configuration
    uint16_t dma_buf_len = 320;          // DMA buffer length (samples)
    uint8_t dma_buf_count = 10;          // Number of DMA buffers
    
    // Processing parameters
    uint16_t frame_size_samples = 320;   // Samples per frame (20ms @ 16kHz)
    uint8_t frame_queue_size = 20;       // Audio frame queue depth
};

/**
 * @brief Audio capture task class
 */
class AudioCapture {
public:
    /**
     * @brief Constructor
     */
    AudioCapture();
    
    /**
     * @brief Destructor
     */
    ~AudioCapture();
    
    /**
     * @brief Initialize I2S interface
     * @param config I2S configuration
     * @return true if successful
     */
    bool initialize(const I2SConfig& config);
    
    /**
     * @brief Start audio capture task
     * @param frame_queue Queue for sending audio frames
     * @return true if started successfully
     */
    bool start(QueueHandle_t frame_queue);
    
    /**
     * @brief Stop audio capture
     */
    void stop();
    
    /**
     * @brief Check if capture is running
     * @return true if running
     */
    bool isRunning() const;
    
    /**
     * @brief Get current configuration
     * @return Current I2S config
     */
    const I2SConfig& getConfig() const;
    
    /**
     * @brief Get capture statistics
     */
    struct Stats {
        uint32_t frames_captured;
        uint32_t buffer_overruns;
        uint32_t queue_full_errors;
        float avg_fill_level;
        uint32_t total_samples;
    };
    
    /**
     * @brief Get capture statistics
     * @return Current stats
     */
    Stats getStats() const;
    
    /**
     * @brief Reset statistics
     */
    void resetStats();

private:
    // Task function
    static void captureTask(void* param);
    void runCaptureTask();
    
    // Audio level calculation
    void calculateLevels(const int16_t* samples, size_t count, 
                        float& rms, float& peak);
    
    // Member variables
    I2SConfig config_;
    TaskHandle_t task_handle_;
    QueueHandle_t frame_queue_;
    RingbufHandle_t ring_buffer_;
    bool running_;
    Stats stats_;
    
    // Memory pool for audio frames
    static constexpr size_t FRAME_POOL_SIZE = 32;
    AudioFrame frame_pool_[FRAME_POOL_SIZE];
    int16_t* sample_buffers_[FRAME_POOL_SIZE];
    size_t frame_pool_index_;
    
    // Mutex for thread safety
    SemaphoreHandle_t mutex_;
};

} // namespace BarkDetector
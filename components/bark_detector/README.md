# Bark Detector Component

ESP32 bark detection component using TensorFlow Lite Micro for real-time dog bark classification.

## Overview

This component provides a high-level API for detecting dog barks in audio streams using a trained neural network model. It supports 4-class classification:

- **DOG_BARK** - Dog barking sounds
- **SPEECH** - Human speech
- **AMBIENT** - Background noise (traffic, music, etc.)
- **SILENCE** - Very low audio levels

## Features

- **Real-time Classification**: <100ms inference time per 1-second audio clip
- **TensorFlow Lite Micro Integration**: Optimized for ESP32
- **Audio Preprocessing**: AGC, noise gate, and temporal filtering
- **Feature Extraction**: 40-band Mel spectrogram at 16kHz
- **Performance Monitoring**: Track inference time and statistics
- **Configurable Parameters**: Thresholds, filtering, and detection settings

## Directory Structure

```
bark_detector/
├── include/
│   └── bark_detector_api.h    # Public API header
├── src/
│   ├── bark_detector.cpp      # Implementation
│   └── model_data.h           # Model data (to be replaced after training)
└── README.md                  # This file
```

## Requirements

### Hardware
- ESP32 or ESP32-S3
- I2S microphone (INMP441 or similar)
- At least 128KB RAM free

### Software Dependencies
- TensorFlow Lite Micro for ESP32
- ESP-IDF or Arduino framework
- Standard C++ library

## Installation

### Using PlatformIO

Add to `platformio.ini`:

```ini
[env:esp32]
platform = espressif32
framework = arduino
lib_deps = 
    tflm-esp32
build_flags = 
    -I components/bark_detector/include
```

### Using ESP-IDF

Add to `CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "components/bark_detector/src/bark_detector.cpp"
    INCLUDE_DIRS "components/bark_detector/include"
    REQUIRES tensorflow-lite
)
```

## Usage

### Basic Example

```cpp
#include "components/bark_detector/include/bark_detector_api.h"

// Create detector instance
BarkDetector detector;

void setup() {
    Serial.begin(115200);
    
    // Configure detector
    BarkDetectorConfig config;
    config.confidence_threshold = 0.75f;      // 75% confidence required
    config.min_bark_duration_ms = 300;        // 300ms minimum bark duration
    config.agc_target_level = 0.5f;           // AGC target level
    config.noise_gate_threshold = -40.0f;     // -40 dB noise gate
    config.enable_temporal_filter = true;     // Enable smoothing
    config.ema_alpha = 0.3f;                  // EMA smoothing factor
    config.median_filter_size = 3;            // Median filter size
    
    // Initialize detector
    if (!detector.initialize(config)) {
        Serial.println("Failed to initialize bark detector!");
        Serial.println("Make sure you've trained and added model_data.h");
        return;
    }
    
    Serial.println("Bark detector initialized successfully!");
}

void loop() {
    // Get audio samples from I2S microphone
    int16_t audio_buffer[16000];  // 1 second at 16kHz
    size_t samples_read = read_i2s_audio(audio_buffer, 16000);
    
    // Process audio and detect barks
    DetectionResult result = detector.process(audio_buffer, samples_read);
    
    // Check for bark detection
    if (result.is_bark) {
        Serial.printf("🐕 BARK DETECTED! Confidence: %.2f%%\n", 
                     result.confidence * 100);
    }
    
    // Print classification results
    Serial.printf("Class: %s, Confidence: %.2f%%\n",
                 bark_class_to_string(result.detected_class),
                 result.confidence * 100);
    
    // Get performance statistics
    PerformanceStats stats = detector.get_stats();
    Serial.printf("Inference: %d us, Preprocessing: %d us\n",
                 stats.inference_time_us,
                 stats.preprocessing_time_us);
    
    delay(100);  // Small delay between detections
}
```

### Advanced Example: Custom Model

```cpp
// Load custom model from SPIFFS or external memory
#include "model_data.h"  // Your trained model

BarkDetector detector;

// Allocate tensor arena (adjust size based on your model)
const size_t arena_size = 64 * 1024;  // 64KB
uint8_t* tensor_arena = (uint8_t*)malloc(arena_size);

// Initialize with custom model
if (detector.initialize(g_model_data, g_model_data_len, 
                       tensor_arena, arena_size)) {
    Serial.println("Custom model loaded!");
}
```

### Feature Extraction Only

```cpp
BarkDetector detector;
detector.initialize(config);

// Extract features without classification
int16_t audio[16000];
float features[32 * 40];  // 32 time frames × 40 mel bands

if (detector.extract_features(audio, 16000, features, sizeof(features)/sizeof(float))) {
    // Use features for other purposes
    // (visualization, logging, etc.)
}
```

### Dynamic Configuration

```cpp
BarkDetector detector;
detector.initialize(config);

// Update configuration at runtime
BarkDetectorConfig new_config = detector.get_config();
new_config.confidence_threshold = 0.85f;  // More strict
detector.update_config(new_config);

// Reset statistics
detector.reset_stats();
```

## Training Your Own Model

**IMPORTANT**: Before using this component, you must train your own model!

The included `model_data.h` is a placeholder. Follow these steps:

1. **Read the Training Guide**: See `TRAINING.md` in the repository root
2. **Set up GPU Environment**: Install CUDA 11.8 + cuDNN 8.6
3. **Collect Audio Data**: Gather dog barks, speech, ambient sounds, silence
4. **Run Training Pipeline**: Use provided Python scripts
5. **Convert to TFLite**: Generate `model_data.h` using conversion script
6. **Replace Placeholder**: Copy `model_data.h` to `components/bark_detector/src/`
7. **Build and Flash**: Compile and upload to ESP32

### Quick Start Training

```bash
# See TRAINING.md for detailed instructions
python verify_gpu.py
python prepare_dataset_gpu.py --input dataset/raw --output dataset/processed --augment
python extract_features_gpu.py --input dataset/processed --output dataset/features
python train_model_gpu.py --features dataset/features --output models --batch-size 1024
python convert_to_tflite.py --model models/best_model.h5 --output models/tflite
cp models/tflite/model_data.h components/bark_detector/src/
```

## API Reference

### Classes

#### `BarkDetector`
Main detector class using pImpl pattern.

**Methods:**
- `bool initialize(const BarkDetectorConfig& config)` - Initialize with config
- `bool initialize(const void* model, size_t size, void* arena, size_t arena_size)` - Initialize with custom model
- `DetectionResult process(const int16_t* samples, size_t count, uint32_t sample_rate)` - Process audio
- `bool extract_features(const int16_t* samples, size_t count, float* features, size_t size)` - Extract features
- `void update_config(const BarkDetectorConfig& config)` - Update configuration
- `BarkDetectorConfig get_config() const` - Get current configuration
- `PerformanceStats get_stats() const` - Get performance statistics
- `void reset_stats()` - Reset statistics
- `bool is_ready() const` - Check if initialized
- `size_t get_input_size() const` - Get required input size
- `void get_feature_dimensions(size_t& mels, size_t& frames) const` - Get feature dimensions

### Structures

#### `BarkDetectorConfig`
Configuration parameters for the detector.

```cpp
struct BarkDetectorConfig {
    float confidence_threshold;      // Default: 0.7
    uint32_t min_bark_duration_ms;   // Default: 300ms
    float agc_target_level;          // Default: 0.5
    float noise_gate_threshold;      // Default: -40 dB
    bool enable_temporal_filter;     // Default: true
    float ema_alpha;                 // Default: 0.3
    uint8_t median_filter_size;      // Default: 3
};
```

#### `DetectionResult`
Result of bark detection.

```cpp
struct DetectionResult {
    BarkClass detected_class;   // Detected class
    float confidence;           // 0.0 to 1.0
    uint32_t timestamp_ms;      // Timestamp
    bool is_bark;              // True if bark detected
};
```

#### `PerformanceStats`
Performance statistics.

```cpp
struct PerformanceStats {
    uint32_t inference_time_us;      // Last inference time (µs)
    uint32_t avg_inference_time_us;  // Average inference time (µs)
    uint32_t preprocessing_time_us;  // Preprocessing time (µs)
    uint32_t total_inferences;       // Total inferences
    uint32_t bark_detections;        // Total bark detections
    float memory_usage_kb;           // Memory usage (KB)
};
```

### Enumerations

#### `BarkClass`
Classification labels.

```cpp
enum class BarkClass : uint8_t {
    DOG_BARK = 0,   // Dog barking
    SPEECH = 1,     // Human speech
    AMBIENT = 2,    // Background noise
    SILENCE = 3,    // Quiet/silence
    UNKNOWN = 255   // Unknown/error
};
```

### Helper Functions

```cpp
const char* bark_class_to_string(BarkClass cls);
```

## Performance Characteristics

### Typical Performance (ESP32 @ 240MHz)

| Metric | Value |
|--------|-------|
| Inference Time | 50-80 ms |
| Preprocessing | 10-20 ms |
| Total Latency | 60-100 ms |
| Memory Usage | 32-64 KB (tensor arena) |
| CPU Usage | 20-30% |
| Accuracy | >90% (on good training data) |

### Optimization Tips

1. **Increase CPU Frequency**: Use 240MHz for best performance
2. **Optimize Tensor Arena**: Adjust size based on actual model needs
3. **Batch Processing**: Process multiple clips if needed
4. **Disable Temporal Filter**: If milliseconds matter
5. **Use Quantized Model**: INT8 quantization for faster inference

## Troubleshooting

### Initialization Fails

**Problem**: `initialize()` returns false

**Solutions**:
- Check that `model_data.h` contains your trained model
- Verify `BARK_DETECTOR_MODEL_DATA_AVAILABLE` is defined in `model_data.h`
- Ensure sufficient memory is available (check free heap)
- Verify TensorFlow Lite Micro is properly installed

### Low Accuracy

**Problem**: Many false positives or misclassifications

**Solutions**:
- Increase `confidence_threshold` (try 0.8-0.9)
- Enable temporal filtering
- Retrain model with more diverse data
- Check microphone gain and positioning
- Verify feature extraction parameters match training

### High Inference Time

**Problem**: Inference takes >150ms

**Solutions**:
- Use INT8 quantized model
- Increase CPU frequency to 240MHz
- Reduce model complexity during training
- Optimize tensor arena size
- Check for memory fragmentation

### Doesn't Detect Distant Barks

**Problem**: Only detects nearby sounds

**Solutions**:
- Increase AGC target level
- Lower noise gate threshold (-50 dB)
- Use higher-gain microphone
- Train model with varied distance samples
- Check microphone placement and orientation

## License

See repository root for license information.

## Support

- **Documentation**: See `TRAINING.md` for complete training guide
- **Issues**: Report bugs on GitHub
- **Questions**: Open a discussion on GitHub

## Credits

Built with TensorFlow Lite Micro for embedded ML inference on ESP32.

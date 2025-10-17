idf_component_register(
    SRCS 
        "audio_capture.cpp"
        "preprocess.cpp" 
        "feature_extractor.cpp"
        "classifier.cpp"
        "decision_logic.cpp"
        "bark_detector_api.cpp"
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        "driver"
        "esp_common"
        "freertos"
        "esp_psram"
        "tflite-micro"
)

# Add compiler flags for ESP32-S3 SIMD optimization
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -ffast-math
    -DESP32_S3
    -DUSE_SIMD_OPTIMIZATIONS
)
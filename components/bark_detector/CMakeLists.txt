idf_component_register(
    SRCS 
        "src/bark_detector.cpp"
        "src/audio_capture.cpp"
        "src/preprocess.cpp" 
        "src/mqtt_client.cpp"
        "src/mqtt_provisioning.cpp"
    INCLUDE_DIRS 
        "include"
    REQUIRES 
        "driver"
        "esp_common"
        "freertos"
        "esp_psram"
        "mqtt"
        "json"
        "nvs_flash"
        "esp_wifi"
        "esp_timer"
)

# Add compiler flags for ESP32-S3 SIMD optimization
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O3
    -ffast-math
    -DESP32_S3
    -DUSE_SIMD_OPTIMIZATIONS
)
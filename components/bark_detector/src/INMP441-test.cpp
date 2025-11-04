// INMP441 Hardware Test Sketch
// Use this to verify INMP441 I2S microphone connections before running main firmware

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

// INMP441 I2S pins
constexpr int I2S_SCK = 26;    // Serial Clock
constexpr int I2S_WS  = 25;    // Word Select (L/R Clock)
constexpr int I2S_SD  = 33;    // Serial Data
constexpr int I2S_PORT = I2S_NUM_0;

// Audio parameters
constexpr int SAMPLE_RATE = 16000;
constexpr int BITS_PER_SAMPLE = 16;
constexpr int BUFFER_SIZE = 1024;

int32_t samples[BUFFER_SIZE];
float rmsHistory[10] = {0};
int historyIndex = 0;

void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = BUFFER_SIZE,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_SD
  };

  esp_err_t result = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (result != ESP_OK) {
    Serial.printf("❌ I2S driver install failed: %s\n", esp_err_to_name(result));
    return;
  }

  result = i2s_set_pin(I2S_PORT, &pin_config);
  if (result != ESP_OK) {
    Serial.printf("❌ I2S pin config failed: %s\n", esp_err_to_name(result));
    return;
  }

  Serial.println("✅ I2S driver initialized successfully");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== INMP441 I2S Microphone Test ===");
  Serial.printf("Testing I2S pins: SCK=GPIO%d, WS=GPIO%d, SD=GPIO%d\n", I2S_SCK, I2S_WS, I2S_SD);
  Serial.printf("Sample rate: %d Hz, Buffer size: %d samples\n", SAMPLE_RATE, BUFFER_SIZE);
  Serial.println("Make sound near microphone - watch for audio level changes");
  
  setupI2S();
}

void loop() {
  size_t bytes_read = 0;
  esp_err_t result = i2s_read(I2S_PORT, samples, sizeof(samples), &bytes_read, portMAX_DELAY);
  
  if (result != ESP_OK) {
    Serial.printf("❌ I2S read failed: %s\n", esp_err_to_name(result));
    delay(1000);
    return;
  }

  int samples_read = bytes_read / sizeof(int32_t);
  if (samples_read == 0) {
    Serial.println("⚠️  No samples read from I2S");
    delay(1000);
    return;
  }

  // Calculate RMS and peak values
  long long sum_squares = 0;
  int32_t peak = 0;
  int32_t min_val = INT32_MAX;
  int32_t max_val = INT32_MIN;

  for (int i = 0; i < samples_read; i++) {
    // Convert from 32-bit to 16-bit and remove DC bias
    int16_t sample = (samples[i] >> 16) & 0xFFFF;
    
    min_val = min(min_val, (int32_t)sample);
    max_val = max(max_val, (int32_t)sample);
    peak = max(peak, abs(sample));
    sum_squares += (long long)sample * sample;
  }

  float rms = sqrt((double)sum_squares / samples_read);
  
  // Update rolling average
  rmsHistory[historyIndex] = rms;
  historyIndex = (historyIndex + 1) % 10;
  
  float avgRms = 0;
  for (int i = 0; i < 10; i++) {
    avgRms += rmsHistory[i];
  }
  avgRms /= 10;

  // Display results
  Serial.printf("Samples: %4d | RMS: %8.1f | Peak: %6d | Range: [%6d, %6d] | Avg: %8.1f\n", 
                samples_read, rms, peak, min_val, max_val, avgRms);

  // Status indicators
  if (avgRms < 100) {
    Serial.println("🔇 Very quiet - check microphone connection and power");
  } else if (avgRms > 1000) {
    Serial.println("🔊 Good audio level - microphone responding well!");
  } else if (avgRms > 500) {
    Serial.println("🎤 Moderate audio level detected");
  }

  // Check for clipping
  if (peak > 30000) {
    Serial.println("⚠️  Audio clipping detected - reduce input level");
  }

  // Check for stuck data (no variation)
  if (max_val == min_val) {
    Serial.println("❌ No audio variation - check I2S connections");
  }

  delay(500); // Update twice per second
}
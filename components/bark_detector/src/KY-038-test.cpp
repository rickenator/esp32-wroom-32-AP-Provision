// KY-038 Hardware Test Sketch
// Use this to verify sensor connections before running main firmware

#include <Arduino.h>
#include <math.h>

constexpr int PIN_DO = 27;          // Digital output (KY-038 D0)
constexpr int PIN_AO = 34;          // Analog output (KY-038 A0)
constexpr int SR     = 8000;        // Sample rate for testing

void setup() {
 
    Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== KY-038 Hardware Test ===");

  pinMode(PIN_DO, INPUT_PULLUP);           // KY-038 DO is open-collector
  analogReadResolution(12);
  analogSetPinAttenuation(PIN_AO, ADC_11db);

  Serial.printf("Testing pins: DO=GPIO%d, AO=GPIO%d\n", PIN_DO, PIN_AO);
  Serial.println("Make sound near sensor - watch for DO edges and AO changes");
}

static int lastDO = -1;

void loop() {
  // Sample 200ms worth (1600 samples @ 8kHz)
  const int N = 1600;
  int minv = 4095, maxv = 0;
  long long acc = 0;

  for (int i = 0; i < N; ++i) {
    int raw = analogRead(PIN_AO);     // 0..4095
    minv = raw < minv ? raw : minv;
    maxv = raw > maxv ? raw : maxv;
    int centered = raw - 2048;        // Crude DC removal for RMS
    acc += 1LL * centered * centered;
    delayMicroseconds(1000000 / SR);  // Pacing for testing
  }

  float rms = sqrt((double)acc / N);
  int doNow = digitalRead(PIN_DO);    // HIGH = idle, LOW = triggered

  if (doNow != lastDO) {
    Serial.printf("DO edge: now=%d (0=triggered, 1=idle)\n", doNow);
    lastDO = doNow;
  }

  Serial.printf("AO: min=%d max=%d rms=%.1f range=%d\n",
                minv, maxv, rms, maxv - minv);

  // Status indicators
  if (rms < 50) {
    Serial.println("⚠️  AO: Very quiet - check microphone connection");
  } else if (rms > 200) {
    Serial.println("✅ AO: Good signal - sensor responding to sound");
  }

  if (doNow == 0) {
    Serial.println("🔊 DO: Triggered - sound detected!");
  }

  delay(1000); // Update every second
}

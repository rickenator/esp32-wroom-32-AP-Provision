Here’s a **Request for Feature Enhancement (RFE)** document draft you can submit to your engineering team for the **ESP32-S3-WROOM-1 (N16R8) Bark Detector** feature branch.
It focuses purely on the **sound classification architecture**, not provisioning or WebRTC.

---

# **RFE: Dog Bark Detection Feature for ESP32-S3-WROOM-1**

**Project:** esp32-wroom-32-AP-Provision
**New Feature Branch:** `feature-bark-detector-esp32s3`
**Author:** Rick Goldberg
**Date:** October 2025

---

## **1. Objective**

Develop a **dog bark detection subsystem** leveraging the ESP32-S3-WROOM-1 N16R8 MCU’s additional PSRAM, vector instruction set, and AI acceleration capabilities.
The system should classify incoming microphone audio in real-time and discriminate dog barks from other acoustic events using a lightweight embedded neural network or DSP-based classifier.

Target deliverable is a modular C++ component that can run standalone on the S3 and expose a clean API for external event consumers (e.g., bark → MQTT publish, GPIO toggle, or HTTP callback).

---

## **2. Hardware Platform**

| Component        | Description                                                              |
| ---------------- | ------------------------------------------------------------------------ |
| **MCU**          | ESP32-S3-WROOM-1-N16R8 (dual-core LX7, 240 MHz, 8 MB PSRAM, 16 MB Flash) |
| **Microphone**   | INMP441 or SPH0645 I²S digital MEMS mic                                  |
| **Interface**    | I²S peripheral, mono 16 kHz sampling, DMA buffering                      |
| **Power Domain** | 3.3 V regulated, low-noise analog supply recommended                     |
| **Optional**     | GPIO LED (status), GPIO relay output, UART debug                         |

---

## **3. System Overview**

```
 ┌─────────────────────────────────────────────────────────────┐
 │                    Bark Detection Subsystem                 │
 ├─────────────────────────────────────────────────────────────┤
 │ I²S Mic → DMA Buffer → Pre-processing → Feature Extraction  │
 │       → TinyML Model Inference → Event Classifier → API Out │
 └─────────────────────────────────────────────────────────────┘
```

### Data Flow

1. **Audio Acquisition**
   I²S interrupt/DMA provides continuous PCM stream (16 kHz, 16-bit mono).
   Buffer size ≈ 20 ms frames (320 samples) × 10 buffers for ~200 ms window.

2. **Pre-processing**

   * DC-block filter and amplitude normalization.
   * Optional noise gate (energy floor detection).
   * Optional A-weighting filter to approximate human hearing.

3. **Feature Extraction**

   * Sliding window (1 s, 50 % overlap).
   * Compute **log-Mel spectrogram** or **MFCC (13–20 coeffs)** per frame.
   * Vectorized computation using ESP32-S3’s **SIMD MAC instructions**.
   * Output: 2D tensor (e.g., 49 × 13) fed to model.

4. **Classification Core**

   * **Model type:**

     * Option A: 1D CNN or small CRNN (Conv + GRU).
     * Option B: Keyword-spotting-style fully-connected DNN (simpler).
   * Framework: TensorFlow Lite Micro (TFLM) or EdgeImpulse SDK.
   * Model size: ≤ 250 kB weights; RAM ≤ 1.5 MB; inference ≤ 30 ms/frame.
   * Classes: { **dog_bark**, **speech**, **ambient**, **silence** }.
   * Output: probability vector normalized to 0–1.

5. **Decision Layer**

   * Temporal smoothing with exponential moving average (EMA) or 3/5-frame median.
   * Detection threshold (e.g., P(bark) > 0.8 for ≥ 300 ms).
   * Generate “bark event” via callback or queue message.

6. **System Output API**

   * C++ class exposes `bool detectBark(const int16_t* frame)` returning boolean.
   * Optional asynchronous queue emitting events to other subsystems.

---

## **4. Software Architecture**

### Module Breakdown

| Module               | Responsibility                                        |
| -------------------- | ----------------------------------------------------- |
| **AudioCaptureTask** | Configures I²S, manages DMA ring buffers.             |
| **Preprocess**       | Filtering, gain normalization, windowing (Hamming).   |
| **FeatureExtractor** | Mel-filterbank or MFCC computation using vector MACs. |
| **Classifier**       | Runs TinyML model, produces probability vector.       |
| **DecisionLogic**    | Aggregates inference results, generates event.        |
| **BarkDetectorAPI**  | Public C++ interface and callback registration.       |

### Threading Model

* **Core 0:** I²S capture + preprocessing.
* **Core 1:** Feature extraction + inference.
* Queue (FreeRTOS) for frame hand-off.
* ISR-safe ring buffer from I²S → preprocessing task.

---

## **5. Memory & Performance Estimates**

| Stage              | RAM Use     | CPU Load                   | Notes                  |
| ------------------ | ----------- | -------------------------- | ---------------------- |
| I²S DMA buffers    | ~32 kB      | negligible                 | 10×1024×16 bit         |
| Preprocessing      | < 16 kB     | ~5 %                       | simple filters         |
| Feature Extraction | ~256 kB     | ~25 %                      | MFCC window buffers    |
| Model Inference    | ~1 MB       | ~40 %                      | CNN 50k params         |
| Decision Logic     | < 8 kB      | ~2 %                       | EMA                    |
| **Total**          | ~1.3 MB RAM | **< 80 % dual-core total** | fits within S3 + PSRAM |

---

## **6. Training & Model Pipeline (off-device)**

* **Dataset:** Dog bark vs non-bark samples (urban noise, speech, mechanical sounds).
* **Augmentation:** Pitch shift ±2 semitones, background noise mix, volume jitter.
* **Features:** 40-band Mel spectrogram, 25 ms window, 10 ms stride.
* **Model example (Keras):**

  ```python
  model = Sequential([
      Conv2D(8, (3,3), activation='relu', input_shape=(49,40,1)),
      MaxPooling2D((2,2)),
      Conv2D(16, (3,3), activation='relu'),
      Flatten(),
      Dense(32, activation='relu'),
      Dense(4, activation='softmax')
  ])
  ```
* Export to TFLite → convert to **TFLM** for ESP32-S3.
* Quantize to **int8** to reduce size and speed inference.
* Validate on-device latency (< 100 ms) and accuracy (> 90 % TPR @ < 5 % FPR).

---

## **7. Integration & Interfaces**

* **Event callback:**

  ```cpp
  void onBarkDetected();
  BarkDetector detector(onBarkDetected);
  ```
* **Optional logging:** UART or BLE GATT characteristic for debug.
* **Configuration:** Thresholds, debounce time, enable/disable via NVS parameters.

---

## **8. Deliverables**

1. New branch `feature-bark-detector-esp32s3`.
2. Standalone `bark_detector` component in `components/`.
3. Demo application `main/bark_demo.cpp` showing real-time bark detection via serial prints.
4. Pre-trained quantized model file (`bark_model_int8.tflite`).
5. Unit tests for DSP feature extraction and model output consistency.
6. Performance metrics document (latency, memory, confusion matrix).

---

## **9. Future Enhancements**

* Multi-class pet sound detection (growl, whine, meow).
* Adaptive thresholding based on ambient noise level.
* OTA-updatable model weights.
* Optional hybrid DSP+NN classifier (energy + zero-cross + CNN).
* Integration with cloud retraining loop.

---

### **Summary**

This feature leverages the ESP32-S3’s expanded PSRAM and SIMD to implement a compact embedded AI model for **dog bark classification**. The modular design isolates DSP and ML layers for re-use in future acoustic-event projects. Real-time performance is achievable within < 1.5 MB RAM and < 80 ms latency per inference frame.

---

Would you like me to generate the corresponding **component file layout** (CMake + header skeletons and stub code) for the new branch next?

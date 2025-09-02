# esp32-wroom-32-AP-Provision

Feature branch: feature-webrtc

This repository demonstrates a captive-portal provisioning flow for ESP32 devices and a planned
extension on the `feature-webrtc` branch to capture microphone audio and stream it via WebRTC.

Hardware & intent
- Target: ESP32-WROOM-32 (DevKitC)
- Microphone/module: KY-038 module (LM393 comparator + analog output) — audio input can be read from the module's A0 (analog) or D0 (digital comparator) pin and forwarded to a WebRTC stack.

What this branch needs (high level)
- audio capture: ADC or I2S-based sampling of the LM383 output, circular buffers
- encoding: Opus (or other) encoder to compress PCM for WebRTC
- WebRTC: a lightweight WebRTC implementation or port (SDP/ICE signaling + RTP transport)
- signaling: a server or mechanism (HTTP/WS) to exchange SDP/ICE with peers
- resource tuning: stack selection and configuration to fit ESP32 RAM/CPU

Quick usage
- Use the captive AP to provision Wi‑Fi credentials via the built-in web UI.
- After provisioning the device should connect in STA mode; WebRTC components will initialize once implemented.

Notes
- Stored Wi‑Fi credentials are kept in NVS namespace `net`. `flush-nvs` erases the entire NVS partition.
- WebRTC on ESP32 is experimental and may require an external DSP/SoC or careful tuning.
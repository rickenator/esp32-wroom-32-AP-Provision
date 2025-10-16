Good baseline. Here are the concrete corrections, gaps, and priority tweaks I recommend before you proceed.

# Top fixes (apply now)

1. **Clock & power policy belongs in Phase 1 (not Phase 2):**
   Move “disable Wi-Fi power save,” “lock CPU to 240 MHz,” and “light sleep off” into Phase 1. These affect ISR timing, debounce accuracy, and notification latency even before audio streaming.

2. **Time source for logs/webhooks/MQTT:**
   Add **NTP sync on boot** (and periodic resync) so detection timestamps are real, not millis-based.

3. **Analog front-end stability:**
   Keep the 0.1 µF decoupler, and **add a simple anti-alias RC** at the ADC pin: **1 kΩ series + 39 nF to GND** (fc≈4.1 kHz @ 8 kHz Fs). This belongs in Phase 1 (hardware is cheap, improves D0/ADC correlation).

4. **Boot safety for DO:**
   Either move DO off **GPIO15** or guarantee it cannot hold an unsafe strap level during reset. Add a one-line acceptance check: “Cold boot succeeds with KY-038 connected.”

5. **Deterministic sampling target stated explicitly:**
   Make “I2S-ADC DMA @ 8 kHz, 20 ms frames (160 samples), producer→consumer via ring buffer, capture on core 1” the **single source of truth**. Keep busy-wait only as a fallback test path.

# Phase adjustments (minimal edits)

* **Phase 1 (HIGH)** — add:

  * Disable Wi-Fi PS (`WiFi.setSleep(false); esp_wifi_set_ps(WIFI_PS_NONE);`)
  * CPU lock + no light sleep (`esp_pm_configure(...)`)
  * NTP time sync
  * RC anti-alias (1 k/39 nF) + decoupler note
  * Debounce spec (see below)
  * “Boot with sensor attached” criterion

* **Phase 2 (MEDIUM)** — clarify:

  * I2S-ADC DMA is the **primary** capture path; timer/analogRead is deprecated.
  * Ring buffer size = **at least 6×20 ms frames** (≥120 ms) to absorb Wi-Fi jitter.
  * Producer on **core 1** (high prio), network/transport on **core 0**.

# Detection path details (make it explicit)

* **Debounce/aggregation**

  * Rising-edge on DO starts an event; event ends after **quiet ≥ T\_quiet** (e.g., 300 ms) with **min duration ≥ T\_min** (e.g., 100 ms).
  * Count events per minute; expose **RMS/peak** from ADC over the same window.
  * Parameters configurable: `T_min`, `T_quiet`, `min_gap`, `level_threshold` (ADC RMS).

* **Config storage (NVS) schema**

  ```
  sound: {
    do_active_high: bool,
    t_min_ms: u16,
    t_quiet_ms: u16,
    level_threshold: u16,    // ADC RMS
  }
  net: {
    mqtt_host, mqtt_port, mqtt_user, mqtt_pass, topic_base,
    webhook_url, webhook_secret
  }
  sys: { timezone, ntp_server, device_name }
  ```

# Notification semantics (MQTT + Webhook)

* **MQTT**

  * **QoS 1**, no retain for events; **retain true** for a compact `status` topic.
  * **LWT** on `status` (e.g., `{"state":"offline"}`), publish `{"state":"online","fw":"x.y.z"}` on boot.
  * **Topics** (example):

    * `home/esp32/<id>/status`
    * `home/esp32/<id>/sound/event`
    * `home/esp32/<id>/sound/level` (periodic RMS/peak if enabled)
  * **Event payload** (uniform with webhook):

    ```json
    {
      "ts":"2025-09-02T18:12:45Z",
      "seq": 1234,
      "duration_ms": 420,
      "rms": 812,
      "peak": 2014,
      "do_edges": 3,
      "fw":"0.2.0",
      "id":"esp32-xxxx"
    }
    ```

* **Webhook**

  * `POST` JSON with same schema; include `X-Signature` HMAC (webhook\_secret).
  * **Retry/backoff**: 3 attempts @ 0s/2s/10s, then queue up to N events in RAM; drop oldest on overflow.

# Provisioning UI upgrades (what to add)

* **Calibration page:** live DO state + ADC meters (RMS/peak over 200 ms), sliders bound to `level_threshold`, `T_min`, `T_quiet`.
* **Connectivity test buttons:** “Test MQTT,” “Test Webhook” (fire sample payload).
* **Time zone & NTP:** select TZ, display current UTC/local.

# Audio/streaming track (keep lean and testable)

* **Frame contract:** 20 ms mono PCM16 @ 8 kHz.
* **Stats:** per-minute counters: frames produced, frames sent, encoder busy-overruns, max queue depth.
* **Drop policy:** if consumer is late, **drop oldest frame**, not newest (keeps latency bounded).
* **Test endpoints (before WebRTC):**

  * `/pcm?ms=200` → base64 PCM burst for quick validation
  * `/levels` → JSON RMS/peak sliding window

# Security & ops

* **Provisioning:** unique default password; **disable captive portal** after Wi-Fi is provisioned.
* **MQTT:** prefer TLS if broker supports; otherwise allow plain with warning.
* **Factory reset:** hold GPIO-X low on boot to wipe NVS `sound`/`net`.

# Acceptance criteria (tighten)

* **Phase 1:**

  * Event detection latency **≤150 ms** p95 (DO edge → MQTT/webhook receive on LAN).
  * False positives **<1/hour** in a quiet room at default thresholds.
  * Cold boot with sensor attached passes 20/20 cycles.

* **Phase 2:**

  * ADC jitter **<5%** p95 (frame timestamps vs. schedule).
  * No buffer overrun at **±20% Wi-Fi jitter** for ≥10 min runs.
  * CPU usage: capture+HPF **<25%** on core 1 at 240 MHz.

* **Phase 3:**

  * Continuous 20 ms frames, end-to-end audio latency **≤250 ms** p95 on LAN.
  * Encoder CPU **<30%**, zero underflows over 15 min.

# Small text edits to your TODO

* Phase 1 → add bullets: **Wi-Fi PS off**, **CPU lock**, **NTP**, **RC anti-alias**.
* Phase 2 → mark **I2S-ADC DMA** as the canonical path; ring buffer sizing; **core pinning**.
* Phase 3 → state **G.711 A-law @ 8 kHz** and **RTP 20 ms** explicitly.

If you want, I’ll return a clean, drop-in **TODO.md patch** with these changes folded in and a minimal JSON schema + example payloads you can paste into the docs and the provisioning UI.

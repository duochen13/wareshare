# ESP32-S3 Voice Assistant — Design Spec

**Date:** 2026-06-10 (rev 3 — incorporates an independent technical review; adds an echo milestone before the LLM)
**Board:** Waveshare ESP32-S3-AUDIO-Board
**Goal:** A hands-free, wake-word-triggered voice assistant. Speak to the board, get a spoken GPT reply.

---

## 1. Summary

Build a custom voice assistant on the Waveshare ESP32-S3-AUDIO-Board. The board handles
wake-word detection, audio capture, and playback locally. A small Python server on the
user's Mac orchestrates the AI pipeline using OpenAI cloud APIs.

Pipeline:

```
Wake word → record → Whisper (STT) → GPT (LLM) → TTS → speaker
```

Trigger is a **built-in wake word** (e.g. "Hi ESP" / "Alexa") — no button, hands-free.

**v1 is half-duplex and turn-taking:** the device does not listen while it is speaking, so
there is no acoustic echo to cancel. This deliberately removes the project's hardest subsystem
(AEC / full-duplex audio). Barge-in is a later stretch goal, not v1.

**First full milestone is an echo loop (no LLM):** `audio → Whisper (text) → TTS → speaker` — the
device repeats back exactly what you said. This proves the entire audio + cloud round-trip before
GPT is introduced. GPT is then added as a single call inserted between the transcript and the TTS.

---

## 2. Hardware (confirmed)

| Component | Part | Role |
|---|---|---|
| MCU | ESP32-S3R8 (Xtensa LX7 dual-core, 240 MHz, 8 MB PSRAM, 16 MB flash) | Runs firmware + wake word |
| Mic ADC | ES7210 (4-ch) | Captures the dual-mic array (+ an echo-reference channel). **Capture only — no DSP.** |
| Speaker DAC | ES8311 | Audio codec for output |
| Amp | NS4150B (mono Class-D) | Drives the single onboard speaker (**mono**) |
| Lighting | RGB LEDs | State feedback (idle / listening / thinking / speaking) |
| Connectivity | 2.4 GHz Wi-Fi + BLE 5 | Wi-Fi used; **2.4 GHz only** |
| Power | USB Type-C **or** onboard 3.7 V Li-ion (MX1.25 connector + charger) | Power + flashing over USB-C; battery option exists |

Noise reduction / AEC / VAD are **software** (ESP-SR AFE on the LX7 cores), not features of the
ES7210 chip.

---

## 3. Architecture

Two halves communicating over Wi-Fi:

```
  ESP32-S3-AUDIO-Board                         Mac (Python server)
 ┌──────────────────────┐                     ┌────────────────────────────┐
 │ ES7210 ADC → mics     │   WebSocket (Wi-Fi)  │  receives PCM audio        │
 │ ESP-SR AFE (software):│  ──── audio up ────► │   → Whisper  (transcribe)  │
 │   noise reduction +   │                     │   → GPT      (reply text)  │
 │   VAD (end-of-talk)   │                     │   → TTS      (reply audio) │
 │ WakeNet wake word     │  ◄─── audio down ─── │  streams PCM reply back     │
 │ ES8311 → NS4150B →    │                     │  (holds 1 OpenAI API key)  │
 │   speaker             │                     └────────────────────────────┘
 │ RGB = state feedback  │
 └──────────────────────┘
```

**Audio path detail:** a single ESP-SR AFE instance processes the mic input; WakeNet consumes
the AFE's *processed* output, and the same processed stream is what gets recorded and uploaded
after wake. Do not build two parallel capture paths.

### Why split device + server (decision)
The ESP32-S3 *could* call OpenAI directly over HTTPS (this is what firmware like XiaoZhi does).
We keep a laptop server for v1 because:
1. **Iteration speed** — the AI pipeline (prompts, streaming, models) is far easier to change in
   Python than to reflash firmware.
2. **Secret safety** — the OpenAI API key stays on the Mac, never on a device that lives in a car
   and could be lost or extracted.

Direct-to-OpenAI (no laptop) is a legitimate later path once the pipeline is stable.

---

## 4. Key technical decisions

- **Firmware base:** Start from a working example with correct ES8311/ES7210 pin mapping and codec
  init rather than a blank project. **Phase 0 verifies which example to use** — Waveshare's own
  ESP-IDF example if it exists and is complete; otherwise Espressif's **ESP32-S3-Korvo-2**
  (ESP-ADF) board support, which uses the same codecs.
- **Framework:** ESP-IDF (with **ESP-ADF** evaluated in Phase 0, as it pre-wires codecs + audio
  pipelines + ESP-SR). **Verify ESP-SR's supported ESP-IDF version before pinning** — ESP-SR is
  version-sensitive; v5.4.1 is a starting assumption, not a guarantee.
- **Wake word:** ESP-SR / **WakeNet**, on-device, using a **built-in** wake word
  ("Hi ESP" / "Alexa"). Custom wake words require a paid Espressif service — out of scope.
- **End-of-speech:** ESP-SR **VAD** + explicit end-pointing logic (e.g. N consecutive non-speech
  frames = end of utterance) and a max-utterance timeout. VAD is a signal, not a turnkey
  "they finished talking" event.
- **Duplex:** **half-duplex.** The board stops mic/wake processing while playing TTS, then resumes.
  No AEC.
- **Capture format:** 16 kHz, 16-bit, mono PCM (Whisper-friendly).
- **Playback format:** request OpenAI TTS as **raw PCM** (`response_format=pcm`, which is fixed at
  **24 kHz** 16-bit mono). **Run the playback I2S/codec path at 24 kHz** to match it — this avoids
  writing real-time resampling in C. No on-device audio decoder needed.
- **Transport:** a single **WebSocket** (board → server) with a small framing protocol:
  JSON control messages + binary audio frames.
- **AI services:** all **OpenAI cloud APIs** — Whisper (`/v1/audio/transcriptions`),
  Chat Completions (GPT), TTS (`/v1/audio/speech`, `response_format=pcm`).
- **Latency:** stream every stage (see §6) and target **< ~2.5 s to first audio**.

---

## 5. Behavior / data flow (one turn)

1. Board idles, listening **locally** for the wake word. **No audio leaves the device.**
2. Wake word detected → RGB → "listening"; board records the AFE-processed stream.
3. VAD end-pointing detects the pause (or max-timeout) → recording stops.
4. Board streams the recorded PCM clip to the server over WebSocket; RGB → "thinking".
5. Server: wrap PCM in a WAV header → Whisper transcribes → GPT generates a reply (with short
   conversation history) → TTS renders the reply to 24 kHz PCM.
   *(Echo milestone, Phase 5: skip the GPT step — feed the Whisper transcript straight to TTS.)*
6. Server **streams** PCM back as it is produced; board plays it (RGB → "speaking"). **Mic/wake
   processing is paused during playback** (half-duplex).
7. Playback ends → resume wake-word listening → idle.

**Privacy property:** audio only leaves the device *after* the wake word, for the duration of
one utterance.

---

## 6. Latency budget & streaming

Naive (non-streaming) round trips run **4–8 s** — too slow. To hit < ~2.5 s to first audio:

- **VAD close** fast (tune the silence threshold).
- **Whisper:** unavoidable ~1–2 s; send as soon as the utterance ends.
- **GPT → TTS:** stream GPT tokens into TTS as they arrive (don't wait for the full reply).
- **TTS → device:** stream PCM chunks to the board and **start playback on the first chunk**.

Alternative to evaluate later: the **OpenAI Realtime API** collapses STT+LLM+TTS into one
streaming socket and can cut latency substantially, at higher cost.

---

## 7. Components

### Device firmware (ESP-IDF / ESP-ADF, C)
- **Codec init** — ES7210 (capture) + ES8311 (playback) from the chosen reference example.
- **Audio front-end (AFE)** — ESP-SR: noise reduction + VAD (no AEC in v1).
- **Wake word** — ESP-SR WakeNet, built-in model, fed from the AFE output.
- **Wi-Fi client** — joins a 2.4 GHz network; credentials in NVS.
- **mDNS resolver** — finds the Mac by hostname (e.g. `voiceserver.local`) instead of a hard IP.
- **WebSocket client** — streams captured PCM up, receives PCM down.
- **Playback** — feed received 24 kHz PCM to ES8311; pause capture while playing.
- **RGB state machine** — idle / listening / thinking / speaking / error.

### Server (Python, on the Mac)
- **WebSocket server** — accepts the board connection; handles the framing protocol.
- **Audio glue** — assemble incoming PCM into a clip and **wrap it in a WAV header** for Whisper.
- **OpenAI client** — one API key; streams Whisper → Chat → TTS.
- **Conversation state** — short rolling message history per session.

---

## 8. Deployment / networking

### Home / desk (development)
Board and Mac both on home Wi-Fi (2.4 GHz). Board resolves the Mac via mDNS (`*.local`).

### Car
No home Wi-Fi, so use a **phone hotspot**:

```
   ESP32 board ──Wi-Fi──► Phone hotspot ◄──Wi-Fi── Mac
                              │
                         (cellular) ──► Internet ──► OpenAI
```

- Board and Mac both join the **phone's hotspot**; board reaches the Mac via mDNS.
- Phone provides internet for the OpenAI calls.
- **Power:** board runs off car USB, a power bank, or its onboard Li-ion battery.
- **Client isolation risk:** some phone hotspots block client-to-client traffic, which would stop
  the board from reaching the Mac. **Test this early**; if blocked, use a different phone/hotspot
  or a travel router.

**Config at deploy time:** board Wi-Fi SSID/password (NVS), the Mac's mDNS hostname, and the
OpenAI API key (on the Mac only, never on the device).

### Caveats (set expectations)
- Cloud dependency: only works with cell signal; weak signal → higher latency.
- Road noise at highway speed is a genuinely hard far-field environment; noise reduction helps but
  expect occasional missed/false wake-ups. (Note: AEC would not help here — it cancels the
  device's *own* output, not ambient road noise.)
- Laptop-in-car is fine for testing; later the server can move to a Raspberry Pi / cloud host, or
  the board can go direct-to-OpenAI (Phase 6+, out of scope for v1).

### Cost (rough, hobby use)
Whisper ~$0.006/min; a GPT turn is a few cents; TTS ~$15 / 1M characters. Pennies per
conversation. The Realtime API is markedly more expensive if adopted later.

---

## 9. Phased build plan

Each phase ends in something testable, so problems are isolated.

| Phase | Build | Done when |
|---|---|---|
| **0** | Install ESP-IDF on macOS; **confirm the right reference example & ESP-SR/IDF version**; build + flash it | Board boots; flashing works |
| **1a** | Play a known WAV/tone to the speaker | You hear it — DAC/amp/I2S TX proven |
| **1b** | Capture mic audio to a buffer; inspect/upload it | Captured audio is intelligible — ADC/I2S RX proven |
| **2** | ESP-SR AFE (NR + VAD) + WakeNet wake word | LED/log fires reliably on the wake word *(hardest phase)* |
| **3** | Python WebSocket server; board joins Wi-Fi & connects via mDNS | Test message round-trips board ↔ Mac |
| **4** | Record-after-wake (VAD end-point) → stream → Whisper (server wraps PCM→WAV) | Your speech prints as text on the Mac |
| **5** | **Echo loop (no LLM):** transcript → TTS (24 kHz PCM, streamed) → speaker; pause capture while speaking | **Device speaks back exactly what you said** |
| **6** | Insert GPT between the transcript and TTS (+ short conversation history) | **Full spoken conversation loop (half-duplex)** |
| **7** | RGB states, latency tuning, error polish | Feels like a product |

You first **hear the device talk** at the end of **Phase 5** (echo loop), and get a full **GPT
conversation** at **Phase 6** — which is just one extra API call on top of Phase 5.
**Phase 2 is the steepest** — budget extra time there. Stretch goals (separate, not v1): AEC + full-duplex + barge-in;
BLE/SoftAP Wi-Fi provisioning; direct-to-OpenAI firmware.

---

## 10. Error handling (v1, kept simple)

- **Wi-Fi drop:** board retries; RGB indicates disconnected.
- **mDNS/WebSocket failure:** board retries resolution + reconnect; in-flight turn is abandoned.
- **OpenAI API error/timeout:** server logs it and returns a short spoken "sorry, try again" (or an
  error tone); board returns to idle.
- **No speech after wake word (VAD timeout):** cancel the turn, return to idle.
- **Secrets:** OpenAI key lives only on the Mac (env var / `.env`), never compiled into firmware.

---

## 11. Testing

- **1a/1b:** playback audible; captured audio intelligible on upload.
- **Phase 2:** wake word triggers reliably in a quiet room; count false positives over several
  minutes of silence + speech; confirm AFE memory fits in PSRAM (watch for OOM).
- **Phase 3:** scripted ping/pong over the WebSocket; mDNS resolves on home Wi-Fi and on a hotspot.
- **Phase 4:** transcript accuracy on a handful of known phrases.
- **Phase 5 (echo):** device speaks back the same words you said; end-to-end latency
  (wake → first audio) measured against the < ~2.5 s target.
- **Phase 6 (GPT):** replies are coherent and on-topic; conversation history carries across turns.
- **Phase 7:** car trial — wake reliability with road noise; hotspot stability + client-isolation.

---

## 12. Out of scope (v1)

- AEC / full-duplex / **barge-in** (deferred stretch goal).
- Custom wake word (paid Espressif service).
- Display and camera (board supports them; unused here).
- Fully local / offline AI (we use OpenAI cloud).
- Moving the server off the laptop (Pi / cloud) or going direct-to-OpenAI.
- Push-to-talk button (wake-word only, per decision).
- BLE/SoftAP Wi-Fi provisioning (credentials hard-set in NVS for v1).

---

## 13. Prerequisites the user provides later

- **OpenAI API key** (Phases 4–5).
- **Wi-Fi SSID/password** for the target network — home Wi-Fi for dev, phone hotspot for car
  (Phase 3).

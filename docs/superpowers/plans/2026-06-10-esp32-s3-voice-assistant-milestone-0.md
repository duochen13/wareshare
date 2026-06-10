# ESP32-S3 Voice Assistant — Implementation Plan: Milestone 0 (Dev Environment & Flashing)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **NOTE — hardware in the loop:** this milestone needs the physical board plugged into the Mac and a human to watch LEDs / read serial output and press the BOOT button if asked. Software steps (install, build) can be automated; flashing + observation steps require the human.

**Goal:** From a clean Mac, install the ESP-IDF toolchain, flash a stock example to prove the board + toolchain + USB flashing all work, then identify and successfully build the audio reference example we'll build the assistant on top of.

**Architecture:** Two-part project — `firmware/` (ESP32-S3, C, built with ESP-IDF) and `server/` (Python, on the Mac). Milestone 0 only touches the toolchain and `firmware/`; the server arrives in Milestone 3.

**Tech Stack:** macOS, Homebrew, ESP-IDF v5.4.x, ESP-SR (later), Python 3 (later). Board: Waveshare ESP32-S3-AUDIO-Board (ESP32-S3R8, ES7210 mic ADC, ES8311 DAC, NS4150B amp).

---

## File Structure (created in this milestone)

```
wareshare/
├── firmware/                 # ESP-IDF project(s) live here (populated in Task 6/7)
├── server/                   # Python server (empty for now; Milestone 3)
├── docs/superpowers/
│   ├── specs/                # design spec (already exists)
│   └── plans/                # this plan
├── DECISIONS.md              # running log of resolved technical decisions
└── README.md                 # project overview + setup notes
```

---

## Task 1: Project repo scaffold

**Files:**
- Create: `README.md`
- Create: `DECISIONS.md`
- Create: `.gitignore`
- Create: `firmware/.gitkeep`, `server/.gitkeep`

- [ ] **Step 1: Initialize git and the directory layout**

Run (from `/Users/duochen/Desktop/career/wareshare`):
```bash
git init
mkdir -p firmware server
touch firmware/.gitkeep server/.gitkeep
```
Expected: `Initialized empty Git repository ...`

- [ ] **Step 2: Create `.gitignore`**

```gitignore
# ESP-IDF build artifacts
firmware/**/build/
firmware/**/sdkconfig
firmware/**/sdkconfig.old
firmware/**/managed_components/
firmware/**/dependencies.lock

# Python
server/.venv/
server/__pycache__/
server/**/__pycache__/
.env

# macOS
.DS_Store
```

- [ ] **Step 3: Create `README.md`**

```markdown
# ESP32-S3 Voice Assistant

Wake-word voice assistant on the Waveshare ESP32-S3-AUDIO-Board.
Pipeline: wake word → record → Whisper → (GPT) → TTS → speaker.

- Design spec: `docs/superpowers/specs/2026-06-10-esp32-s3-voice-assistant-design.md`
- Plans: `docs/superpowers/plans/`
- Decisions log: `DECISIONS.md`

## Layout
- `firmware/` — ESP32-S3 firmware (ESP-IDF)
- `server/`  — Python orchestration server (runs on a Mac)

## Setup
See the Milestone 0 plan for toolchain install + first flash.
```

- [ ] **Step 4: Create `DECISIONS.md`**

```markdown
# Decisions Log

| Date | Decision | Rationale |
|---|---|---|
| 2026-06-10 | Half-duplex v1, no AEC | Removes hardest subsystem for a beginner |
| 2026-06-10 | Laptop server for v1 (not direct-to-OpenAI) | Easier iteration; API key off-device |
| 2026-06-10 | First milestone after audio = echo loop (no LLM) | Validate full audio round-trip before GPT |

## Open (resolved in Milestone 0)
- [ ] Which firmware base: Waveshare ESP-IDF example vs ESP-ADF (Korvo-2)?
- [ ] ESP-SR version and its required ESP-IDF version
```

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "chore: scaffold project repo and decisions log"
```

---

## Task 2: Install Homebrew prerequisites

**Files:** none (system setup).

- [ ] **Step 1: Confirm Homebrew is installed**

Run: `brew --version`
Expected: `Homebrew 4.x.x`. If "command not found", install from https://brew.sh first.

- [ ] **Step 2: Install ESP-IDF system dependencies**

Run: `brew install cmake ninja dfu-util python3`
Expected: each installs or reports "already installed". (ESP-IDF brings its own Python virtualenv; this `python3` is just the bootstrap.)

- [ ] **Step 3: Verify versions**

Run: `cmake --version && ninja --version && python3 --version`
Expected: cmake ≥ 3.24, ninja present, Python ≥ 3.9.

---

## Task 3: Install ESP-IDF v5.4.x

**Files:** installs to `~/esp/esp-idf` (outside the repo — correct; the SDK is not vendored).

- [ ] **Step 1: Clone ESP-IDF**

Run:
```bash
mkdir -p ~/esp
cd ~/esp
git clone -b v5.4.1 --recursive https://github.com/espressif/esp-idf.git
```
Expected: clone completes; `~/esp/esp-idf` exists with submodules. (This is large; takes a few minutes.)

- [ ] **Step 2: Run the installer for the ESP32-S3 target**

Run:
```bash
cd ~/esp/esp-idf
./install.sh esp32s3
```
Expected: ends with "All done! You can now run: . ./export.sh".

- [ ] **Step 3: Add a convenience alias to your shell**

Run:
```bash
echo "alias get_idf='. \$HOME/esp/esp-idf/export.sh'" >> ~/.zshrc
```
Then open a new terminal (or `source ~/.zshrc`).

- [ ] **Step 4: Activate the environment and verify**

Run: `get_idf && idf.py --version`
Expected: `ESP-IDF v5.4.1` (or v5.4.x). The `get_idf` step prints "Done! You can now compile ESP-IDF projects."

> You must run `get_idf` in every new terminal before using `idf.py`.

---

## Task 4: Identify the board's USB serial port

**Files:** none. Output is the port path you'll use for flashing.

- [ ] **Step 1: List serial devices with the board UNPLUGGED**

Run: `ls /dev/cu.*`
Note the list (e.g. `/dev/cu.Bluetooth-Incoming-Port`).

- [ ] **Step 2: Plug the board into the Mac via USB-C, then list again**

Run: `ls /dev/cu.*`
Expected: a NEW entry appears — typically `/dev/cu.usbmodemXXXX` (ESP32-S3 native USB) or `/dev/cu.usbserial-XXXX` / `/dev/cu.wchusbserialXXXX` (UART bridge). Record this path — call it `$PORT`.

- [ ] **Step 3: If no new device appears**

The board likely uses a CH34x/CP210x UART bridge needing a driver:
- CH34x: install the WCH macOS driver (https://www.wch-ic.com/downloads/CH34XSER_MAC_ZIP.html), reboot, re-check.
- CP210x: install Silicon Labs VCP driver.
Then repeat Step 2. (Many ESP32-S3 boards use native USB and need no driver — try without first.)

---

## Task 5: Build & flash `hello_world` (prove toolchain + board + flashing)

This is the critical "does anything work at all" gate. No custom code yet.

**Files:** uses the stock ESP-IDF example (no repo files changed).

- [ ] **Step 1: Copy the example into the repo so we don't edit the SDK**

Run:
```bash
get_idf
cp -r ~/esp/esp-idf/examples/get-started/hello_world ~/Desktop/career/wareshare/firmware/hello_world
cd ~/Desktop/career/wareshare/firmware/hello_world
```

- [ ] **Step 2: Set the chip target**

Run: `idf.py set-target esp32s3`
Expected: ends with "Build files have been written to: .../build".

- [ ] **Step 3: Build**

Run: `idf.py build`
Expected: ends with "Project build complete." and prints the `esptool.py ... write_flash` command hint.

- [ ] **Step 4: Flash and open the serial monitor**

Run (substitute your port from Task 4): `idf.py -p /dev/cu.usbmodemXXXX flash monitor`
Expected: "Hash of data verified.", then the chip reboots and the monitor prints chip info plus:
```
Hello world!
This is esp32s3 chip with 2 CPU core(s)...
Restarting in 10 seconds...
```
The message repeats every ~10s.

- [ ] **Step 5: If flashing fails to connect**

Hold the **BOOT** button on the board, run the flash command, release BOOT when "Connecting..." appears. Some boards need this to enter download mode.

- [ ] **Step 6: Exit the monitor**

Press `Ctrl-]`.

- [ ] **Step 7: Record success in DECISIONS.md and commit**

Add a line under the table noting the working `$PORT` and that hello_world flashed.
```bash
cd ~/Desktop/career/wareshare
git add -A
git commit -m "chore: verify ESP-IDF toolchain by flashing hello_world"
```

> ✅ **Gate:** if you saw "Hello world!" in the monitor, your toolchain, USB, and flashing all work. This is the foundation everything else stands on.

---

## Task 6: Resolve the audio firmware base (RESEARCH — unblocks all later milestones)

**Goal:** decide which example/framework gives us correct ES8311 + ES7210 init for THIS board, and confirm ESP-SR will work with our ESP-IDF version. Output is a documented decision + a downloaded, building example.

**Files:**
- Modify: `DECISIONS.md` (record the choice)
- Create: `firmware/<chosen-example>/` (the working audio example)

- [ ] **Step 1: Look for Waveshare's official ESP-IDF demo for the board**

Open the board wiki's demo/AI-tutorial section: https://www.waveshare.com/wiki/ESP32-S3-AUDIO-Board
Look specifically for a downloadable **ESP-IDF** example (not only Arduino, not only a pre-built XiaoZhi flash). Note the link and what it contains (does it init ES8311 + ES7210? does it include ESP-SR?).

- [ ] **Step 2: Check Espressif's reference as a fallback**

The ESP32-S3-Korvo-2 board uses the **same** ES8311 + ES7210 codecs and is fully supported in ESP-ADF. Note the ESP-ADF repo (https://github.com/espressif/esp-adf) and its Korvo-2 board config as a known-good source of codec init code.

- [ ] **Step 3: Decide the base and write it down**

Choose ONE and record in `DECISIONS.md` with rationale:
- **Option A — Waveshare ESP-IDF example** if it exists and cleanly inits both codecs (closest pin match, least risk).
- **Option B — ESP-ADF + Korvo-2 config** if Waveshare's example is thin/Arduino-only (richest audio framework, but you may need to adjust pin mappings to match this board's schematic).

Capture the board's audio pin map (I2C SDA/SCL for codec control; I2S MCLK/BCLK/WS/DOUT/DIN) from the Waveshare schematic/wiki into `DECISIONS.md` — you'll need these constants in every later milestone.

- [ ] **Step 4: Verify ESP-SR ↔ ESP-IDF compatibility**

In whichever example you'll extend, check its `main/idf_component.yml` (or run `idf.py add-dependency espressif/esp-sr` in a scratch copy) and read the resolved `esp-sr` version's README for its supported ESP-IDF range. Confirm it includes v5.4.x. If it does NOT, record the ESP-IDF version ESP-SR requires — we will re-pin Task 3 to that version before Milestone 2.

- [ ] **Step 5: Commit the decision**

```bash
git add DECISIONS.md
git commit -m "docs: choose firmware base and record board audio pin map"
```

---

## Task 7: Build & flash the chosen audio example (prove the codecs initialize)

**Goal:** get the audio example from Task 6 building and flashing, and confirm in the serial log that both codec chips are detected over I2C — without writing any of our own audio code yet.

**Files:**
- Create: `firmware/<chosen-example>/` (copied in, built)

- [ ] **Step 1: Copy the chosen example into `firmware/`**

Copy the example from Task 6 into `~/Desktop/career/wareshare/firmware/`. (Exact path depends on the choice; for ESP-ADF you'll clone esp-adf and copy a board example, for Waveshare you'll copy their demo folder.)

- [ ] **Step 2: Build it**

Run (in the example dir): `get_idf && idf.py set-target esp32s3 && idf.py build`
Expected: "Project build complete." If it fails on a missing component, run `idf.py add-dependency` for the named component or follow the example's README, then rebuild.

- [ ] **Step 3: Flash and monitor**

Run: `idf.py -p /dev/cu.usbmodemXXXX flash monitor`
Expected: in the boot log, the codec drivers report success — look for `ES8311` and `ES7210` init lines and **no** repeated I2C error/timeout messages (e.g. `i2c: ... timeout` or `codec ... failed` would mean the pin map is wrong → revisit Task 6 Step 3).

- [ ] **Step 4: Exercise whatever the example does**

If the example plays a tone or echoes the mic, confirm you hear it / see VU output. (If the example does nothing audible, that's fine — the goal here is only that it boots and inits the codecs cleanly. Audio I/O is Milestone 1.)

- [ ] **Step 5: Record result and commit**

Note in `DECISIONS.md` whether codecs initialized cleanly and any pin-map corrections you had to make.
```bash
cd ~/Desktop/career/wareshare
git add -A
git commit -m "chore: build and flash audio reference example; codecs initialize"
```

> ✅ **Milestone 0 complete when:** `hello_world` flashed AND the chosen audio example boots with ES8311 + ES7210 initializing cleanly, AND `DECISIONS.md` records the firmware base, the audio pin map, and the confirmed ESP-SR/IDF versions.

---

## Roadmap for later milestones (to be written as their own plans)

Each will be a full bite-sized plan once Milestone 0 resolves the framework/pin map. Brief shape:

- **Milestone 1a — Playback:** play a bundled WAV/tone through ES8311 → NS4150B at 24 kHz. *Done when you hear it clearly.*
- **Milestone 1b — Capture:** record mic (ES7210) to a buffer, dump it over serial/Wi-Fi, listen on the Mac. *Done when the recording is intelligible.*
- **Milestone 2 — Wake word:** ESP-SR AFE (NR + VAD) + WakeNet built-in wake word; LED on detect. *Done when the wake word fires reliably in a quiet room. (Steepest milestone.)*
- **Milestone 3 — Server + connectivity:** Python WebSocket server; board joins Wi-Fi (NVS creds) and resolves the Mac via mDNS. *Done when a test message round-trips.*
- **Milestone 4 — Whisper:** record-after-wake (VAD end-point) → stream PCM → server wraps WAV → Whisper. *Done when your speech prints as text on the Mac.*
- **Milestone 5 — Echo loop (no LLM):** transcript → OpenAI TTS (24 kHz PCM, streamed) → speaker; pause capture while speaking. *Done when the device speaks back exactly what you said.*
- **Milestone 6 — GPT:** insert one Chat Completions call between transcript and TTS, with short history. *Done when it holds a conversation.*
- **Milestone 7 — Polish:** RGB state machine, latency tuning to < ~2.5 s to first audio, error handling, car/hotspot trial.

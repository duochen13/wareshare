#!/usr/bin/env python3
"""Reset the board, read its base64 PCM dump over serial, write a WAV.

Usage: python tools/capture_mic.py [port] [out.wav]
Then play it (macOS):  afplay mic.wav
"""
import sys, time, base64, wave
import serial   # pip install pyserial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbmodem101"
OUT  = sys.argv[2] if len(sys.argv) > 2 else "mic.wav"

ser = serial.Serial(PORT, 115200, timeout=1)
# Reset the board (RTS pulse) so it re-runs app_main and records fresh.
ser.setDTR(False); ser.setRTS(True); time.sleep(0.12); ser.setRTS(False)

rate, capturing, data = 24000, False, bytearray()
deadline = time.time() + 30
while time.time() < deadline:
    line = ser.readline().decode(errors="replace").strip()
    if not line:
        continue
    if line.startswith("---RECORD-START---"):
        print(">>> SPEAK NOW <<<", line)
        continue
    if line.startswith("---PCM-BEGIN---"):
        for tok in line.split():
            if tok.startswith("rate="):
                rate = int(tok.split("=")[1])
        capturing = True
        print("recording stream...", line)
        continue
    if line.startswith("---PCM-END---"):
        break
    if capturing:
        try:
            data += base64.b64decode(line)
        except Exception:
            pass   # skip any non-data line that slipped in
ser.close()

if not data:
    print("ERROR: no audio captured (is a monitor holding the port? is the board running 1b?)")
    sys.exit(1)

with wave.open(OUT, "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(bytes(data))

secs = len(data) / 2 / rate
print(f"wrote {OUT}: {len(data)} bytes, {secs:.1f}s @ {rate} Hz mono")
print(f"play it:  afplay {OUT}")

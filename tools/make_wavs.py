#!/usr/bin/env python3
"""
make_wavs.py - generate placeholder sound effects for BIN-CHAD.

    python tools/make_wavs.py

Writes 16-bit mono 22050 Hz WAV files into firmware/BinChad/data/, which is
the folder the ESP32 LittleFS uploader flashes to the board.

These are synthesised beeps and blips, not voice lines. They exist so that the
audio path can be built and tested on day one, before anyone has recorded
anything. Replace them whenever you like: keep the filenames, keep the format
(16-bit mono PCM), and keep every clip under one second. A long clip kills the
pace of the interaction and the audience stops watching the bin.

Standard library only - no numpy, no dependencies.
"""

import math
import os
import struct
import wave

RATE = 22050
OUT = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "firmware", "BinChad", "data")


# ---------------------------------------------------------------------------
# Tiny synthesis helpers. Everything returns a list of floats in -1.0..1.0.
# ---------------------------------------------------------------------------
def silence(ms):
    return [0.0] * int(RATE * ms / 1000)


def tone(freq, ms, shape="sine", vol=0.6):
    n = int(RATE * ms / 1000)
    out = []
    for i in range(n):
        t = i / RATE
        p = (freq * t) % 1.0
        if shape == "sine":
            v = math.sin(2 * math.pi * p)
        elif shape == "square":
            v = 1.0 if p < 0.5 else -1.0
        elif shape == "saw":
            v = 2.0 * p - 1.0
        else:  # triangle
            v = 4.0 * abs(p - 0.5) - 1.0
        out.append(v * vol)
    return out


def sweep(f0, f1, ms, shape="square", vol=0.5):
    """Linear frequency sweep. Phase is integrated so it does not click."""
    n = int(RATE * ms / 1000)
    out, phase = [], 0.0
    for i in range(n):
        f = f0 + (f1 - f0) * (i / max(1, n - 1))
        phase = (phase + f / RATE) % 1.0
        if shape == "square":
            v = 1.0 if phase < 0.5 else -1.0
        elif shape == "saw":
            v = 2.0 * phase - 1.0
        else:
            v = math.sin(2 * math.pi * phase)
        out.append(v * vol)
    return out


def noise(ms, vol=0.4, seed=1):
    """Deterministic pseudo-noise, so regenerating gives identical files."""
    n = int(RATE * ms / 1000)
    out, x = [], seed or 1
    for _ in range(n):
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        out.append(((x / 0xFFFFFFFF) * 2.0 - 1.0) * vol)
    return out


def envelope(samples, attack_ms=4, release_ms=25):
    """Attack/release ramp. Without this every clip starts with a click."""
    n = len(samples)
    a = min(int(RATE * attack_ms / 1000), n // 2)
    r = min(int(RATE * release_ms / 1000), n // 2)
    out = list(samples)
    for i in range(a):
        out[i] *= i / max(1, a)
    for i in range(r):
        out[n - 1 - i] *= i / max(1, r)
    return out


def mix(*parts):
    """Overlay equal-length-ish parts, clipped to the longest."""
    n = max(len(p) for p in parts)
    out = [0.0] * n
    for p in parts:
        for i, v in enumerate(p):
            out[i] += v
    return [max(-1.0, min(1.0, v)) for v in out]


def seq(*parts):
    out = []
    for p in parts:
        out.extend(p)
    return out


def write(name, samples):
    path = os.path.join(OUT, name)
    frames = b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s)) * 32000))
                      for s in samples)
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(frames)
    ms = len(samples) * 1000 // RATE
    print(f"  {name:<16} {ms:>4} ms  {len(frames) + 44:>6} bytes")


# ---------------------------------------------------------------------------
# The clips. Names and order match AudioClip in src/hardware/audio.h.
# ---------------------------------------------------------------------------
CLIPS = {
    # Confident three-note power-up.
    "boot.wav": lambda: envelope(seq(
        tone(392, 90, "square", 0.45),
        tone(523, 90, "square", 0.45),
        tone(784, 160, "square", 0.5))),

    # Short interrogative blip - "I see you".
    "detected.wav": lambda: envelope(sweep(500, 1100, 110)),

    # Rising servo-ish whir.
    "open.wav": lambda: envelope(mix(sweep(220, 520, 220, "saw", 0.35),
                                     noise(220, 0.10, seed=7))),

    # Falling counterpart.
    "close.wav": lambda: envelope(mix(sweep(520, 200, 260, "saw", 0.35),
                                      noise(260, 0.10, seed=11))),

    # Descending "wah wah" - you missed.
    "miss.wav": lambda: envelope(seq(
        tone(330, 130, "square", 0.5),
        tone(294, 130, "square", 0.5),
        tone(233, 240, "square", 0.5))),

    # Bright ascending arpeggio.
    "success.wav": lambda: envelope(seq(
        tone(523, 70, "square", 0.45),
        tone(659, 70, "square", 0.45),
        tone(880, 180, "square", 0.5))),

    # Harsh buzz.
    "angry.wav": lambda: envelope(mix(tone(110, 320, "square", 0.5),
                                      tone(116, 320, "square", 0.4))),

    # Wobbling question mark.
    "confused.wav": lambda: envelope(seq(
        sweep(400, 620, 120, "sine", 0.5),
        sweep(620, 380, 120, "sine", 0.5),
        sweep(380, 700, 160, "sine", 0.5))),

    # Four descending chuckle blips.
    "laugh.wav": lambda: envelope(seq(
        tone(700, 60, "square", 0.4), silence(30),
        tone(640, 60, "square", 0.4), silence(30),
        tone(580, 60, "square", 0.4), silence(30),
        tone(520, 90, "square", 0.4))),

    # Flat, bureaucratic rejection.
    "denied.wav": lambda: envelope(seq(
        tone(196, 160, "square", 0.55), silence(40),
        tone(196, 220, "square", 0.55))),

    # Fake-computation warble.
    "ai.wav": lambda: envelope(seq(
        sweep(800, 1400, 90, "square", 0.32),
        sweep(700, 1300, 90, "square", 0.32),
        sweep(900, 1600, 90, "square", 0.32),
        sweep(600, 1800, 180, "square", 0.35))),

    # Two-tone fault alarm.
    "error.wav": lambda: envelope(seq(
        tone(880, 140, "square", 0.5),
        tone(440, 140, "square", 0.5),
        tone(880, 140, "square", 0.5))),

    # Power-down slide.
    "shutdown.wav": lambda: envelope(sweep(700, 120, 420, "saw", 0.45)),
}


def main():
    os.makedirs(OUT, exist_ok=True)
    print(f"writing placeholder clips to {os.path.relpath(OUT)}")
    total = 0
    for name, build in CLIPS.items():
        samples = build()
        write(name, samples)
        total += len(samples) * 2 + 44
    print(f"\n{len(CLIPS)} clips, {total / 1024:.1f} KB total")
    print("Upload with: Arduino IDE -> Tools -> ESP32 Sketch Data Upload")
    print("Replace with real recordings any time - same names, 16-bit mono PCM, under 1 s.")


if __name__ == "__main__":
    main()

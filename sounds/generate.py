#!/usr/bin/env python3
"""Synthesize the typewriter sound effects.

The sounds are generated rather than recorded so they carry no third-party
license. Run this script to regenerate the WAV files next to it; it only needs
the Python standard library and is deterministic, so the output is stable.
"""

import math
import os
import random
import struct
import wave

RATE = 44100
HERE = os.path.dirname(os.path.abspath(__file__))


def silence(seconds):
    return [0.0] * int(RATE * seconds)


def mix(target, source, offset_seconds=0.0, gain=1.0):
    offset = int(RATE * offset_seconds)
    needed = offset + len(source)
    if needed > len(target):
        target.extend([0.0] * (needed - len(target)))
    for i, sample in enumerate(source):
        target[offset + i] += sample * gain
    return target


def decaying_noise(rng, seconds, tau):
    return [rng.uniform(-1, 1) * math.exp(-(i / RATE) / tau)
            for i in range(int(RATE * seconds))]


def resonate(signal, frequency, bandwidth):
    """Two-pole resonator: rings the input at one frequency, like a struck part."""
    r = math.exp(-math.pi * bandwidth / RATE)
    c1 = 2 * r * math.cos(2 * math.pi * frequency / RATE)
    c2 = -r * r
    out, y1, y2 = [], 0.0, 0.0
    for x in signal:
        y = x * (1 - r) + c1 * y1 + c2 * y2
        out.append(y)
        y1, y2 = y, y1
    return out


def high_pass(signal):
    out, previous = [], 0.0
    for x in signal:
        out.append(x - previous)
        previous = x
    return out


def thump(frequency, seconds, tau):
    return [math.sin(2 * math.pi * frequency * i / RATE) * math.exp(-(i / RATE) / tau)
            for i in range(int(RATE * seconds))]


def normalize(signal, peak=0.8):
    loudest = max(abs(s) for s in signal) or 1.0
    return [s * peak / loudest for s in signal]


def strike(rng, body_frequency, thump_frequency, weight=1.0):
    """A typebar hitting the platen: a sharp click, a woody body, a low thump,
    and a quieter rebound as the key returns."""
    sound = silence(0.14)
    mix(sound, high_pass(decaying_noise(rng, 0.02, 0.0025)), gain=1.0)
    mix(sound, resonate(decaying_noise(rng, 0.08, 0.012), body_frequency, 260), gain=6.0)
    mix(sound, thump(thump_frequency, 0.08, 0.02), gain=0.35 * weight)
    rebound = rng.uniform(0.045, 0.06)
    mix(sound, high_pass(decaying_noise(rng, 0.012, 0.0015)), rebound, gain=0.25)
    return normalize(sound)


def carriage_return(rng):
    """The ratchet of the carriage sliding back, ending on the margin bell."""
    sound = silence(0.9)
    for step in range(9):
        when = 0.02 + step * 0.026 + rng.uniform(-0.003, 0.003)
        mix(sound, high_pass(decaying_noise(rng, 0.01, 0.0015)), when, gain=0.35)
        mix(sound, resonate(decaying_noise(rng, 0.03, 0.006), 1400, 400), when, gain=1.5)
    mix(sound, strike(rng, 900, 110, weight=1.4), 0.26, gain=0.7)

    bell = []
    for i in range(int(RATE * 0.6)):
        t = i / RATE
        envelope = math.exp(-t / 0.22)
        bell.append(envelope * (math.sin(2 * math.pi * 2093 * t)
                                + 0.45 * math.sin(2 * math.pi * 2093 * 2.76 * t)
                                + 0.2 * math.sin(2 * math.pi * 2093 * 5.4 * t)))
    mix(sound, bell, 0.28, gain=0.22)
    return normalize(sound)


def write(name, signal):
    path = os.path.join(HERE, name)
    with wave.open(path, "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s)) * 32767))
                                 for s in signal))


def main():
    rng = random.Random(1879)  # Deterministic output; the year of the shift key.
    for index, body in enumerate((1750, 1900, 1650, 2050)):
        write(f"key{index + 1}.wav", strike(rng, body, rng.uniform(130, 160)))
    write("space.wav", strike(rng, 720, 85, weight=2.0))
    write("backspace.wav", strike(rng, 1300, 120, weight=0.6))
    write("return.wav", carriage_return(rng))


if __name__ == "__main__":
    main()

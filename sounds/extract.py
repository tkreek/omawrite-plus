#!/usr/bin/env python3
"""Cut the typewriter sound effects out of two CC0 field recordings.

Sources (both Creative Commons 0, public domain):
  - "Typewriter, IBM Selectric II" by secretmojo
    https://freesound.org/people/secretmojo/sounds/224012/
  - "Typewriter bell & carriage reset" by knufds
    https://freesound.org/people/knufds/sounds/345955/

Run this script to regenerate the WAV files next to it. It downloads the
public Freesound previews, so it needs network access and ffmpeg.
"""

import array
import math
import os
import subprocess
import tempfile
import urllib.request
import wave

RATE = 44100
HERE = os.path.dirname(os.path.abspath(__file__))
TYPING_URL = "https://cdn.freesound.org/previews/224/224012_82274-hq.ogg"
BELL_URL = "https://cdn.freesound.org/previews/345/345955_191884-hq.ogg"

# Rough times (seconds) of single strokes in the Selectric recording, picked
# because nothing else sounds in the 250 ms around them.
KEYS = [35.08, 37.934, 39.52, 45.826, 53.937, 72.006, 83.281, 104.822]
# The space bar only trips the escapement, so it is a softer single thump.
SPACES = [54.187, 77.594]
BACKSPACE = 31.648


def decode(url, directory):
    source = os.path.join(directory, os.path.basename(url))
    urllib.request.urlretrieve(url, source)
    target = source + ".wav"
    # A typewriter's click lives well above 180 Hz. What sits below it is the
    # desk and the machine's body thumping, which the bass boost on laptop
    # speaker correction (such as Asahi's) turns into an audible pop. The
    # top end above 11 kHz is mostly lossy-preview hiss.
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", source, "-ac", "1", "-ar",
                    str(RATE), "-af",
                    "highpass=f=180:poles=2,highpass=f=180:poles=2,lowpass=f=11000",
                    target], check=True)
    with wave.open(target) as audio:
        return [s / 32768 for s in array.array("h", audio.readframes(audio.getnframes()))]


def cut(samples, start, length, fade_in=0.002, fade_out=0.05, lead=0.008):
    first = int((start - lead) * RATE)
    clip = samples[first:first + int(length * RATE)]
    fade_in_samples, fade_out_samples = int(fade_in * RATE), int(fade_out * RATE)
    for i in range(fade_in_samples):
        clip[i] *= i / fade_in_samples
    for i in range(fade_out_samples):
        clip[-1 - i] *= i / fade_out_samples
    return clip


def strike(samples, around, length):
    """Cuts a stroke starting right at its first transient, so the sound does
    not lag the key press: the first millisecond louder than a third of the
    loudest one near the given time."""
    first = int((around - 0.05) * RATE)
    window = samples[first:first + int(0.3 * RATE)]
    step = RATE // 1000
    levels = [sum(abs(s) for s in window[i:i + step]) for i in range(0, len(window) - step, step)]
    onset = next(i for i, level in enumerate(levels) if level > max(levels) / 3)
    return cut(samples, (first + onset * step) / RATE, length, lead=0.003)


def normalize(clip, level):
    """Brings a clip to a common loudness rather than a common peak, then
    rounds off the sharpest part of the strike so it cannot crack."""
    rms = math.sqrt(sum(s * s for s in clip) / len(clip)) or 1.0
    return [math.tanh(s * level / rms) * 0.85 for s in clip]


def mix(target, source, offset):
    start = int(offset * RATE)
    if start + len(source) > len(target):
        target.extend([0.0] * (start + len(source) - len(target)))
    for i, sample in enumerate(source):
        target[start + i] += sample
    return target


def write(name, clip):
    with wave.open(os.path.join(HERE, name), "wb") as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(RATE)
        out.writeframes(array.array(
            "h", (int(max(-1.0, min(1.0, s)) * 32767) for s in clip)).tobytes())


def main():
    with tempfile.TemporaryDirectory() as directory:
        typing = decode(TYPING_URL, directory)
        bell = decode(BELL_URL, directory)

    for index, start in enumerate(KEYS):
        write(f"key{index + 1}.wav", normalize(strike(typing, start, 0.18), 0.1))
    for index, start in enumerate(SPACES):
        write(f"space{index + 1}.wav", normalize(strike(typing, start, 0.18), 0.09))
    write("backspace.wav", normalize(strike(typing, BACKSPACE, 0.18), 0.075))

    # The margin bell, then the carriage sliding home and hitting its stop,
    # with the gap where the typist reaches for the lever taken out.
    ring = cut(bell, 0.0, 0.75, fade_out=0.2, lead=0.0)
    carriage = cut(bell, 1.65, 1.2, fade_in=0.05, fade_out=0.1, lead=0.0)
    write("return.wav", normalize(mix(ring, carriage, 0.55), 0.08))


if __name__ == "__main__":
    main()

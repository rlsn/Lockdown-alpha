"""Lockdown layered combat and UI sounds: recorded CC0/CC BY layers mixed into game sounds.

Authoring tool (the import and validation never run it; they check its sha256, the hashes of its inputs and of its
outputs). Each sound is a transient, a body and a tail cut from recorded layers (Kenney CC0 packs, Free Modular Magic
SFX by Le Binh Son, CC BY 4.0), aligned on their attacks; synthesis is only a low sub sweetener. Takes differ by layer
choice and a seeded +/-3 % pitch. `--inputs` prints every input file the recipes read.
"""
import argparse
import random
from pathlib import Path

import numpy as np
import soundfile
from scipy.signal import butter, resample_poly, sosfilt

RATE = 48000
OUT = Path(__file__).resolve().parent
ROOT = OUT.parent
IMPACT, INTERFACE = "Kenney/ImpactSounds", "Kenney/InterfaceSounds"
RPG, SCIFI, MAGIC = "Kenney/RpgAudio", "Kenney/SciFiSounds", "FabMagic"
READ = set()


def load(relative):
    READ.add(relative)
    samples, rate = soundfile.read(ROOT / relative, dtype="float64", always_2d=True)
    samples = samples.mean(axis=1)
    if rate != RATE:
        g = np.gcd(rate, RATE)
        samples = resample_poly(samples, RATE // g, rate // g)
    return samples


def cut(x, length=None, fade_out=0.03, fade_in=0.002):
    """From the layer's attack (2 ms before it first reaches 5 % of its peak), at most length seconds."""
    peak = np.max(np.abs(x)) or 1
    a = max(0, int(np.argmax(np.abs(x) > 0.05 * peak)) - int(0.002 * RATE))
    y = x[a:] if length is None else x[a:a + int(length * RATE)]
    y = y.copy()
    fi, fo = int(fade_in * RATE), int(fade_out * RATE)
    if fi:
        y[:fi] *= np.linspace(0, 1, fi)
    if fo and fo < len(y):
        y[-fo:] *= np.linspace(1, 0, fo)
    return y


def band(x, lo=None, hi=None):
    if lo and hi:
        sos = butter(4, [lo, hi], "bandpass", fs=RATE, output="sos")
    elif lo:
        sos = butter(4, lo, "highpass", fs=RATE, output="sos")
    else:
        sos = butter(4, hi, "lowpass", fs=RATE, output="sos")
    return sosfilt(sos, x)


def sub(freq, length, drop=0.6):
    t = np.arange(int(length * RATE)) / RATE
    f = freq * (1 + drop * np.exp(-t * 40))
    return np.sin(2 * np.pi * np.cumsum(f) / RATE) * np.exp(-t * 28)


def mix(*layers):
    """(offset seconds, samples, gain dB) layers."""
    n = max(int(o * RATE) + len(s) for o, s, g in layers)
    y = np.zeros(n)
    for o, s, g in layers:
        a = int(o * RATE)
        y[a:a + len(s)] += s * 10 ** (g / 20)
    return y


def finish(y, rms_db, peak_db=-1.0):
    """The first 300 ms at rms_db, peaks held at peak_db; MeasureDemoAudioLoudness sets the playback volume."""
    r = np.sqrt(np.mean(y[: int(0.3 * RATE)] ** 2)) or 1
    y = y * 10 ** (rms_db / 20) / r
    p = np.max(np.abs(y)) or 1
    limit = 10 ** (peak_db / 20)
    return y * limit / p if p > limit else y


def pitch(y, ratio):
    return resample_poly(y, int(round(1000 / ratio)), 1000)


# Ordinary hit: a leather punch crack, a soft body thud, a low sub and a short cloth flap.
def hit_basic(n):
    return finish(mix((0, cut(load(f"{IMPACT}/impactPunch_medium_00{n % 5}.ogg"), 0.18), 0),
                      (0.004, band(cut(load(f"{IMPACT}/impactSoft_medium_00{(n + 2) % 5}.ogg"), 0.22), hi=2500), -4),
                      (0, sub(60, 0.1), -8),
                      (0.03, band(cut(load(f"{RPG}/cloth{n % 4 + 1}.ogg"), 0.16), lo=1500), -16)), -14)


# Finisher: the heavy punch, a wooden crack for bite, a deep boom and a dusty tail.
def hit_finisher(n):
    return finish(mix((0, cut(load(f"{IMPACT}/impactPunch_heavy_00{n % 5}.ogg"), 0.25), 0),
                      (0.002, band(cut(load(f"{IMPACT}/impactWood_heavy_00{(n + 1) % 5}.ogg"), 0.2), lo=800), -7),
                      (0, band(cut(load(f"{SCIFI}/lowFrequency_explosion_00{n % 2}.ogg"), 0.45, 0.25), hi=400), -5),
                      (0, sub(48, 0.16), -6),
                      (0.02, band(cut(load(f"{MAGIC}/SW_MagicImpact_Barrier_{n % 3 + 1}.wav"), 0.5, 0.3), lo=2500), -20)), -12)


# Electric hit: a snapped lightning strike on a short punch, the arc tail kept brief.
def hit_electric(n):
    zap = cut(load(f"{MAGIC}/SW_MagicImpact_LightningStrike_{n % 4 + 1}.wav"), 0.35, 0.15)
    return finish(mix((0, zap, 0), (0, cut(load(f"{IMPACT}/impactPunch_medium_00{(n + 3) % 5}.ogg"), 0.12), -6),
                      (0.01, band(zap, lo=4000), -10)), -15)


# Fire hit: a fire impact whoosh on a soft thud, with the crackle tail.
def hit_fire(n):
    return finish(mix((0, cut(load(f"{MAGIC}/SW_MagicImpact_Fire_{n % 5 + 1}.wav"), 0.45, 0.2), 0),
                      (0, band(cut(load(f"{IMPACT}/impactSoft_heavy_00{n % 5}.ogg"), 0.15), hi=1800), -6)), -15)


# Ice shatter reaction: crystalline impact, a glass break on top, a low crunch underneath.
def react_shatter(n):
    return finish(mix((0, cut(load(f"{MAGIC}/SW_MagicImpact_Crystal_{n % 4 + 1}.wav"), 0.55, 0.25), 0),
                      (0.004, cut(load(f"{IMPACT}/impactGlass_heavy_00{n % 5}.ogg"), 0.4, 0.15), -5),
                      (0, band(cut(load(f"{SCIFI}/explosionCrunch_00{n % 5}.ogg"), 0.25), hi=900), -9)), -14)


# UI choose: the clean confirmation with a faint glassy sparkle.
def ui_choose(n):
    return finish(mix((0, cut(load(f"{INTERFACE}/confirmation_00{n % 4 + 1}.ogg"), None, 0.02), 0),
                      (0.01, band(cut(load(f"{INTERFACE}/glass_00{n % 6 + 1}.ogg"), 0.25, 0.1), lo=3000), -14)), -18)


SOUNDS = {"hit_basic": hit_basic, "hit_finisher": hit_finisher, "hit_electric": hit_electric, "hit_fire": hit_fire,
          "react_shatter": react_shatter, "ui_choose": ui_choose}
TAKES = 3


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--only", default="", help="write only the sounds whose name contains this text")
    parser.add_argument("--inputs", action="store_true", help="list every input file the recipes read, write nothing")
    args = parser.parse_args()
    for name, make in SOUNDS.items():
        if args.only and args.only not in name:
            continue
        rng = random.Random(name)
        for n in range(TAKES):
            samples = pitch(make(n), rng.uniform(0.97, 1.03))
            if not args.inputs:
                soundfile.write(OUT / f"{name}_{n + 1}.wav", samples.astype(np.float64), RATE, subtype="PCM_16")
                print(f"{name}_{n + 1:<14} {len(samples) / RATE * 1000:6.0f} ms")
    if args.inputs:
        print("\n".join(sorted(READ)))


if __name__ == "__main__":
    main()

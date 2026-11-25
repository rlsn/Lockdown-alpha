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


def fab(name, n, count, length=None, fade_out=0.03):
    return cut(load(f"{MAGIC}/SW_{name}_{n % count + 1}.wav"), length, fade_out)


def kenney(folder, stem, n, count, length=None, fade_out=0.03, digits=3):
    return cut(load(f"{folder}/{stem}{n % count:0{digits}d}.ogg"), length, fade_out)


# Water hit: a water impact slap on a soft thud, a short splash tail.
def hit_water(n):
    return finish(mix((0, fab("MagicImpact_Water", n, 5, 0.4, 0.2), 0),
                      (0, band(kenney(IMPACT, "impactSoft_medium_", n + 2, 5, 0.15), hi=2000), -6),
                      (0.03, band(fab("MagicCast_Liquid_Water", n, 5, 0.3, 0.2), lo=1500), -12)), -15)


# Magic hit: a bright metallic magic impact on a light punch, a shimmering fade.
def hit_magic(n):
    return finish(mix((0, fab("MagicImpact_Metallic", n, 8, 0.35, 0.18), 0),
                      (0, kenney(IMPACT, "impactPunch_medium_", n + 1, 5, 0.1), -9),
                      (0.02, band(fab("MagicFade_FadeOut1", n, 3, 0.35, 0.25), lo=2500), -14)), -16)


# Cast release: a sharp magic shot with a low push.
def cast_release(n):
    return finish(mix((0, fab("MagicFade_Shoot1", n, 3, 0.35, 0.15), 0),
                      (0, band(fab("MagicCast_Generic1", n, 4, 0.3, 0.15), hi=1200), -8)), -16)


# Body fall: a heavy soft body slam, a leather drop, a low dust thump.
def body_fall(n):
    return finish(mix((0, kenney(IMPACT, "impactSoft_heavy_", n, 5, 0.3), 0),
                      (0.05, cut(load(f"{RPG}/dropLeather.ogg"), 0.3), -6),
                      (0, sub(52, 0.18), -8),
                      (0.04, band(fab("MagicSpell_EarthQuake", n, 6, 0.45, 0.3), hi=700), -14)), -14)


# Element states: one short onset each, no loops (they would stack in a brawl).
def state_burn(n):
    return finish(mix((0, fab("MagicSpell_FireTrail", n, 5, 0.45, 0.25), 0),
                      (0, band(fab("MagicImpact_Fire", n + 2, 5, 0.2, 0.1), lo=2000), -10)), -19)


def state_wet(n):
    return finish(mix((0, fab("MagicCast_Liquid_Water", n + 1, 5, 0.4, 0.2), 0)), -19)


def state_shock(n):
    return finish(mix((0, band(fab("MagicChant_LightningCharge", n, 3, 0.4, 0.2), lo=1200), 0),
                      (0.02, band(fab("MagicImpact_LightningStrike", n + 1, 4, 0.12, 0.06), lo=3000), -8)), -19)


def state_chill(n):
    return finish(mix((0, fab("MagicCast_IceCrystal1", n, 3, 0.45, 0.25), 0)), -19)


def state_rot(n):
    return finish(mix((0, fab("MagicCast_Liquid_Gore", n, 6, 0.45, 0.25), 0),
                      (0.05, band(fab("MagicCast_ThrowToxin", n, 4, 0.3, 0.2), hi=2500), -9)), -19)


# Element reactions: two elements meeting, each told by its own pair of layers.
def react_fog(n):
    return finish(mix((0, band(fab("MagicBeam_Water1", n, 4, 0.8, 0.4), lo=800), 0),
                      (0, fab("MagicFade_FadeIn1", n, 3, 0.8, 0.4), -6)), -16)


def react_conduct(n):
    return finish(mix((0, fab("MagicImpact_LightningStrikeMulti", n, 6, 0.6, 0.3), 0),
                      (0.02, band(fab("MagicImpact_Water", n, 5, 0.3, 0.2), hi=3000), -8)), -15)


def react_overload(n):
    return finish(mix((0, fab("MagicSpell_RockExplosion1", n, 3, 0.7, 0.35), 0),
                      (0, fab("MagicImpact_LightningStrike", n + 2, 4, 0.3, 0.15), -5),
                      (0, sub(45, 0.2), -6)), -13)


def react_melt(n):
    return finish(mix((0, fab("MagicImpact_Fire", n + 1, 5, 0.5, 0.25), 0),
                      (0.05, fab("MagicCast_Liquid_Water", n + 3, 5, 0.5, 0.3), -5)), -15)


def react_methane(n):
    return finish(mix((0, fab("MagicCast_FireBall1", n, 3, 0.6, 0.3), 0),
                      (0.03, band(fab("MagicCast_Liquid_Gore", n + 2, 6, 0.4, 0.25), hi=2000), -7),
                      (0, sub(50, 0.2), -8)), -14)


def react_freeze(n):
    return finish(mix((0, fab("MagicSpell_FreezingWind", n, 4, 0.7, 0.35), 0),
                      (0.02, fab("MagicImpact_Crystal", n + 1, 4, 0.4, 0.2), -4)), -15)


def react_ferment(n):
    return finish(mix((0, fab("MagicCast_ThrowToxin", n + 1, 4, 0.6, 0.3), 0),
                      (0.04, band(fab("MagicCast_Liquid_Gore", n + 4, 6, 0.5, 0.3), hi=1800), -5)), -15)


def react_corrode(n):
    return finish(mix((0, fab("MagicCast_ThrowToxin", n + 2, 4, 0.6, 0.3), 0),
                      (0.02, band(fab("MagicImpact_Metallic", n + 3, 8, 0.4, 0.25), lo=1500), -9)), -15)


def react_frostbite(n):
    return finish(mix((0, fab("MagicCast_IceCrystal2", n, 3, 0.6, 0.3), 0),
                      (0, kenney(IMPACT, "impactPunch_medium_", n + 4, 5, 0.12), -7),
                      (0.03, band(fab("MagicImpact_Crystal", n + 2, 4, 0.35, 0.2), lo=2500), -10)), -15)


SOUNDS = {"hit_basic": hit_basic, "hit_finisher": hit_finisher, "hit_electric": hit_electric, "hit_fire": hit_fire,
          "react_shatter": react_shatter, "ui_choose": ui_choose,
          "hit_water": hit_water, "hit_magic": hit_magic, "cast_release": cast_release, "body_fall": body_fall,
          "state_burn": state_burn, "state_wet": state_wet, "state_shock": state_shock, "state_chill": state_chill,
          "state_rot": state_rot, "react_fog": react_fog, "react_conduct": react_conduct, "react_overload": react_overload,
          "react_melt": react_melt, "react_methane": react_methane, "react_freeze": react_freeze,
          "react_ferment": react_ferment, "react_corrode": react_corrode, "react_frostbite": react_frostbite}
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

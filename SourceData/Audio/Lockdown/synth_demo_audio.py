"""Generate Lockdown's original combat sounds into SourceData/Audio/Lockdown/*.wav.

Authoring tool (the import and validation never run it; they check its sha256 and the hashes of its outputs).
    python -m pip install numpy scipy soundfile
    python SourceData/Audio/Lockdown/synth_demo_audio.py            # writes every sound
    python SourceData/Audio/Lockdown/synth_demo_audio.py --only zap # writes the sounds whose name contains "zap"

Every sound is synthesised from seeded noise and oscillators; a few hit bodies layer the CC0 Kenney impact files already in
SourceData/Audio/Kenney (CC0 allows it; the manifest keeps their provenance). Same script + same seed = same bytes.
Design rules (docs/systems/AUDIO_AND_FEEL.md): the transient sits where the sync table puts the event (a swing peaks at
40 ms = its pre-roll, a hit at 0 ms), tails fade out within the table's dissipation, and weight comes from low-mid body
and length, not from level. Files are 48 kHz mono 16-bit, peak-normalised to -1 dBFS; the mix lives in the manifest.
"""

import argparse
from pathlib import Path

import numpy as np
import soundfile
from scipy.signal import butter, sosfilt, resample_poly

RATE = 48000
OUT = Path(__file__).resolve().parent
KENNEY = OUT.parent / "Kenney"
PEAK = 10 ** (-1 / 20)


def t_axis(seconds):
    return np.arange(int(seconds * RATE)) / RATE


def noise(rng, seconds):
    return rng.standard_normal(int(seconds * RATE))


def band(x, lo, hi, order=2):
    return sosfilt(butter(order, [lo, hi], btype="bandpass", fs=RATE, output="sos"), x)


def low(x, fc, order=2):
    return sosfilt(butter(order, fc, btype="lowpass", fs=RATE, output="sos"), x)


def high(x, fc, order=2):
    return sosfilt(butter(order, fc, btype="highpass", fs=RATE, output="sos"), x)


def sweep_band(x, centres, q=1.6, block=256):
    """Band-pass x with a centre frequency that follows centres (one value per sample), block by block."""
    out = np.zeros_like(x)
    zi = None
    for start in range(0, len(x), block):
        c = float(np.clip(centres[min(start + block // 2, len(centres) - 1)], 60, RATE / 2.4))
        bw = c / q
        sos = butter(2, [max(c - bw / 2, 30), min(c + bw / 2, RATE / 2.1)], btype="bandpass", fs=RATE, output="sos")
        if zi is None or zi.shape[0] != sos.shape[0]:
            zi = np.zeros((sos.shape[0], 2))
        out[start:start + block], zi = sosfilt(sos, x[start:start + block], zi=zi)
    return out


def env_ad(n, attack, decay, curve=3.0):
    """Attack (s) rising with a smooth curve, then exponential decay with time constant decay (s)."""
    t = np.arange(n) / RATE
    a = np.clip(t / max(attack, 1e-4), 0, 1) ** 1.6
    d = np.exp(-np.clip(t - attack, 0, None) / max(decay, 1e-4))
    return a * d


def fade_tail(x, seconds=0.02):
    n = min(len(x), int(seconds * RATE))
    if n > 0:
        x[-n:] *= np.linspace(1, 0, n) ** 2
    return x


def chirp(f0, f1, seconds, shape=1.0):
    t = t_axis(seconds)
    f = f0 + (f1 - f0) * (t / seconds) ** shape
    return np.sin(2 * np.pi * np.cumsum(f) / RATE)


def pad(x, n):
    return np.pad(x, (0, max(0, n - len(x))))[:n]


def mix(length, *layers):
    out = np.zeros(int(round(length * RATE)))
    for offset, layer, gain in layers:
        start = int(round(offset * RATE))
        # Every layer ends with a short fade so no truncated oscillator clicks.
        seg = fade_tail(np.array(layer[: max(0, len(out) - start)], dtype=np.float64), 0.006)
        out[start:start + len(seg)] += gain * seg
    return out


def kenney(name, pitch=1.0):
    data, rate = soundfile.read(KENNEY / name, dtype="float64", always_2d=True)
    mono = data.mean(axis=1)
    if rate != RATE:
        mono = resample_poly(mono, RATE, rate)
    if pitch != 1.0:
        up, down = int(round(1000 / pitch)), 1000
        mono = resample_poly(mono, up, down)
    # Start on the transient: the pack files carry a little leading silence.
    onset = int(np.argmax(np.abs(mono) > 0.05 * np.abs(mono).max()))
    return mono[max(0, onset - 24):]


def soft_clip(x, drive=1.5):
    return np.tanh(drive * x) / np.tanh(drive)


def finish(x, tail=0.015):
    x = x - np.mean(x[: min(len(x), 64)]) * 0  # keep DC handling explicit: the high-pass below removes it
    x = high(x, 30, 2)
    x = fade_tail(x, tail)
    peak = np.max(np.abs(x))
    return x * (PEAK / peak) if peak > 0 else x


# ---------------------------------------------------------------- swings (peak at 40 ms = the pre-roll)

def swing_light(seed):
    rng = np.random.default_rng(seed)
    length = 0.30
    n = int(length * RATE)
    t = np.arange(n) / RATE
    peak = 0.040
    lo, hi = 520 + 80 * rng.random(), 2300 + 500 * rng.random()
    # The centre rises to the peak (the hand accelerating) and falls after it (the air closing).
    centres = np.where(t < peak, lo + (hi - lo) * (t / peak) ** 1.5, hi * np.exp(-(t - peak) / 0.09) + 700)
    air = sweep_band(noise(rng, length), centres, q=1.4)
    air *= env_ad(n, peak, 0.065)
    cloth = low(noise(rng, length), 380) * env_ad(n, peak * 0.9, 0.05)
    return finish(air + 0.55 * cloth)


def swing_finisher(seed):
    rng = np.random.default_rng(seed)
    length = 0.42
    n = int(length * RATE)
    t = np.arange(n) / RATE
    peak = 0.040
    centres = np.where(t < peak, 300 + 1300 * (t / peak) ** 1.4, 1600 * np.exp(-(t - peak) / 0.14) + 380)
    air = sweep_band(noise(rng, length), centres, q=1.1) * env_ad(n, peak, 0.11)
    body = low(noise(rng, length), 260) * env_ad(n, peak, 0.09)
    whomp = chirp(95, 48, 0.16, 0.7) * env_ad(int(0.16 * RATE), 0.012, 0.05)
    return finish(mix(length, (0, air, 1.0), (0, body, 0.8), (peak - 0.03, whomp, 0.45)))


# ---------------------------------------------------------------- hits (transient at 0 ms)

def hit_basic(seed):
    rng = np.random.default_rng(seed)
    length = 0.30
    click = high(noise(rng, 0.004), 2500) * np.linspace(1, 0, int(0.004 * RATE)) ** 2
    thump = chirp(135 + 20 * rng.random(), 58, 0.11, 0.5) * env_ad(int(0.11 * RATE), 0.002, 0.035)
    smack = band(noise(rng, 0.05), 900, 3200) * env_ad(int(0.05 * RATE), 0.001, 0.012)
    tail = low(noise(rng, 0.2), 900) * env_ad(int(0.2 * RATE), 0.004, 0.045)
    punch = kenney("ImpactSounds/impactPunch_medium_000.ogg", pitch=0.92 + 0.16 * rng.random())
    return finish(soft_clip(mix(length, (0, click, 0.5), (0, thump, 0.9), (0, smack, 0.55), (0.002, tail, 0.35),
                               (0, punch, 0.8)), 1.3))


def hit_finisher(seed):
    rng = np.random.default_rng(seed)
    length = 0.48
    click = high(noise(rng, 0.005), 2000) * np.linspace(1, 0, int(0.005 * RATE)) ** 2
    thump = chirp(110, 40, 0.22, 0.45) * env_ad(int(0.22 * RATE), 0.002, 0.07)
    crack = band(noise(rng, 0.08), 1400, 5200) * env_ad(int(0.08 * RATE), 0.001, 0.018)
    tail = low(noise(rng, 0.4), 600) * env_ad(int(0.4 * RATE), 0.006, 0.09)
    punch = kenney("ImpactSounds/impactPunch_medium_000.ogg", pitch=0.78 + 0.06 * rng.random())
    return finish(soft_clip(mix(length, (0, click, 0.55), (0, thump, 1.0), (0, crack, 0.5), (0.003, tail, 0.45),
                               (0, punch, 0.85)), 1.5))


# ---------------------------------------------------------------- electric (warm, crackling; approved chain A+ timing)

def crackle(rng, seconds, rate_start, rate_end, lo=1800, hi=6500, decay=None):
    """Sparse random impulses (Poisson rate falling from rate_start to rate_end per second) through a resonant band."""
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    rate = rate_start + (rate_end - rate_start) * (t / seconds)
    hits = rng.random(n) < rate / RATE
    impulses = hits * rng.uniform(0.3, 1.0, n) * rng.choice([-1, 1], n)
    body = band(impulses, lo, hi, 2) * 6
    if decay:
        body *= np.exp(-t / decay)
    return body


def buzz(f, seconds, rng):
    t = t_axis(seconds)
    jitter = 1 + 0.02 * low(rng.standard_normal(len(t)), 40) * 20
    phase = 2 * np.pi * np.cumsum(f * jitter) / RATE
    wave = np.sign(np.sin(phase)) * 0.5 + 0.5 * np.sin(2 * phase)
    return low(wave, 2400)


def hit_electric(seed):
    rng = np.random.default_rng(seed)
    length = 0.32
    n = int(length * RATE)
    t = np.arange(n) / RATE
    snap = high(noise(rng, 0.012), 2800) * np.exp(-np.arange(int(0.012 * RATE)) / (0.003 * RATE))
    # The hop: bright crackle and a 110 Hz buzz that hold through the bright chain (130 ms) and cut at 165 ms ...
    hold = np.clip((0.165 - t) / 0.035, 0, 1) ** 1.5
    crack = crackle(rng, length, 900, 250, 1500, 5200) * hold
    hum = buzz(108 + 10 * rng.random(), length, rng) * hold * env_ad(n, 0.004, 0.11)
    # ... then the sizzle the body arcs leave crawling for 0.3 s.
    sizzle = high(noise(rng, length), 4500) * np.clip((t - 0.12) / 0.03, 0, 1) * np.exp(-np.clip(t - 0.15, 0, None) / 0.05)
    sizzle *= (rng.random(n) < 0.35)
    # Warm, not hissy: the approved chain is warm white to pale gold, so the hop keeps its energy below 8 kHz.
    return finish(low(mix(length, (0, snap, 1.0), (0, crack, 0.6), (0, hum, 0.42), (0, sizzle, 0.2)), 7500, 4))


def zap_release(seed):
    rng = np.random.default_rng(seed)
    length = 0.26
    n = int(length * RATE)
    t = np.arange(n) / RATE
    snap = high(noise(rng, 0.008), 3000) * np.exp(-np.arange(int(0.008 * RATE)) / (0.002 * RATE))
    zip_ = chirp(700, 2600, 0.07, 0.6) * env_ad(int(0.07 * RATE), 0.003, 0.03)
    crack = crackle(rng, length, 700, 80, 2200, 7000) * np.exp(-t / 0.07)
    hum = buzz(120, length, rng) * env_ad(n, 0.002, 0.05)
    return finish(low(mix(length, (0, snap, 0.9), (0, zip_, 0.35), (0, crack, 0.55), (0, hum, 0.25)), 9500))


# ---------------------------------------------------------------- other elements, cast charge and release

def hit_fire(seed):
    rng = np.random.default_rng(seed)
    length = 0.42
    n = int(length * RATE)
    t = np.arange(n) / RATE
    whoomp = sweep_band(noise(rng, length), 2600 * np.exp(-t / 0.06) + 380, q=0.9) * env_ad(n, 0.006, 0.11)
    pops = crackle(rng, length, 220, 40, 900, 4200) * np.exp(-t / 0.18)
    thump = chirp(120, 60, 0.1, 0.6) * env_ad(int(0.1 * RATE), 0.002, 0.03)
    return finish(mix(length, (0, whoomp, 1.0), (0, pops, 0.45), (0, thump, 0.55)))


def hit_water(seed):
    rng = np.random.default_rng(seed)
    length = 0.40
    n = int(length * RATE)
    t = np.arange(n) / RATE
    splash = band(noise(rng, length), 900, 6500) * env_ad(n, 0.004, 0.07)
    drops = np.zeros(n)
    for _ in range(14):
        start = int(rng.uniform(0.01, 0.26) * RATE)
        dur = rng.uniform(0.012, 0.03)
        blip = chirp(rng.uniform(700, 1400), rng.uniform(1600, 3200), dur, 2.0) * env_ad(int(dur * RATE), 0.001, dur / 3)
        drops[start:start + len(blip)] += rng.uniform(0.2, 0.5) * blip[: n - start]
    thump = chirp(110, 55, 0.09, 0.5) * env_ad(int(0.09 * RATE), 0.002, 0.03)
    return finish(mix(length, (0, splash, 0.9), (0, drops, 0.6), (0, thump, 0.5)))


def hit_magic(seed):
    rng = np.random.default_rng(seed)
    length = 0.45
    n = int(length * RATE)
    t = np.arange(n) / RATE
    base = 520 + 60 * rng.random()
    chime = sum(np.sin(2 * np.pi * base * r * t) * np.exp(-t / d) * g for r, d, g in ((1, 0.18, 1), (2.01, 0.11, 0.5), (3.02, 0.07, 0.25)))
    thump = chirp(140, 70, 0.08, 0.6) * env_ad(int(0.08 * RATE), 0.002, 0.025)
    air = band(noise(rng, length), 2000, 7000) * env_ad(n, 0.002, 0.04)
    return finish(mix(length, (0, chime, 0.45), (0, thump, 0.8), (0, air, 0.3)))


def cast_charge(seed):
    rng = np.random.default_rng(seed)
    length = 0.5
    n = int(length * RATE)
    t = np.arange(n) / RATE
    rise = (t / length) ** 1.3
    tone = chirp(180, 520, length, 1.4) * (0.6 + 0.4 * np.sin(2 * np.pi * np.cumsum(8 + 22 * rise) / RATE))
    air = sweep_band(noise(rng, length), 500 + 3000 * rise, q=2.0)
    x = (0.35 * tone + 0.9 * air) * rise * np.clip(t / 0.04, 0, 1)
    return finish(x, 0.03)


def cast_release(seed):
    rng = np.random.default_rng(seed)
    length = 0.28
    n = int(length * RATE)
    t = np.arange(n) / RATE
    puff = sweep_band(noise(rng, length), 3200 * np.exp(-t / 0.05) + 500, q=1.2) * env_ad(n, 0.003, 0.06)
    blip = chirp(420, 880, 0.06, 0.8) * env_ad(int(0.06 * RATE), 0.002, 0.025)
    return finish(mix(length, (0, puff, 1.0), (0, blip, 0.35)))


# ---------------------------------------------------------------- the owner's confirmation accents

def dealt_crit(seed):
    rng = np.random.default_rng(seed)
    length = 0.42
    t = t_axis(length)
    ring = sum(np.sin(2 * np.pi * 1180 * r * t) * np.exp(-t / d) * g for r, d, g in ((1, 0.16, 1), (2.76, 0.08, 0.6), (5.4, 0.04, 0.35)))
    click = high(noise(rng, 0.004), 3000) * np.linspace(1, 0, int(0.004 * RATE))
    return finish(mix(length, (0, ring, 0.6), (0, click, 0.8)))


def dealt_kill(seed):
    rng = np.random.default_rng(seed)
    length = 0.7
    n = int(length * RATE)
    t = np.arange(n) / RATE
    boom = chirp(75, 34, 0.5, 0.5) * env_ad(int(0.5 * RATE), 0.003, 0.16)
    crack = band(noise(rng, 0.06), 1200, 5000) * env_ad(int(0.06 * RATE), 0.001, 0.015)
    ring = (np.sin(2 * np.pi * 660 * t) + 0.7 * np.sin(2 * np.pi * 990 * t)) * np.exp(-t / 0.22) * np.clip(t / 0.01, 0, 1)
    return finish(soft_clip(mix(length, (0, boom, 1.0), (0, crack, 0.6), (0.01, ring, 0.22)), 1.4))


# ---------------------------------------------------------------- skill releases (transient at 0 = the authoritative release)
# Each skill is built from what it does (shove, grab, dash, shield, throw, zone, element), never a re-pitch of another.

def whoosh(rng, seconds, lo, hi, peak=0.02, q=1.3, decay=0.06):
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    centres = np.where(t < peak, lo + (hi - lo) * (t / max(peak, 1e-3)), hi * np.exp(-(t - peak) / (decay * 1.5)) + lo * 0.8)
    return sweep_band(noise(rng, seconds), centres, q=q) * env_ad(n, peak, decay)


def thud(f0, f1, seconds, decay):
    return chirp(f0, f1, seconds, 0.5) * env_ad(int(seconds * RATE), 0.002, decay)


def click(rng, seconds=0.004, lo=2500):
    n = int(seconds * RATE)
    return high(noise(rng, seconds), lo) * np.linspace(1, 0, n) ** 2


def cloth(rng, seconds, decay):
    return band(noise(rng, seconds), 300, 2200) * env_ad(int(seconds * RATE), 0.004, decay)


def ring(freqs, seconds, decays, gains):
    t = t_axis(seconds)
    return sum(np.sin(2 * np.pi * f * t) * np.exp(-t / d) * g for f, d, g in zip(freqs, decays, gains))


def skill_elbow(seed):  # 滚远点: a short elbow shove that knocks back: tight whoosh into a chest thud
    rng = np.random.default_rng(seed)
    return finish(soft_clip(mix(0.38, (0, whoosh(rng, 0.12, 500, 2000, 0.015, 1.2, 0.03), 0.6), (0.01, thud(120, 50, 0.2, 0.06), 1.0),
                                (0.01, cloth(rng, 0.15, 0.04), 0.5), (0.01, click(rng), 0.4)), 1.4))


def skill_pants_down(seed):  # 扒裤子: a grab line that roots: a fast cloth rip with a waistband snap
    rng = np.random.default_rng(seed)
    n = int(0.32 * RATE)
    t = np.arange(n) / RATE
    rip = band(noise(rng, 0.32), 900, 5000) * (0.5 + 0.5 * np.sign(np.sin(2 * np.pi * (60 + 140 * t) * t))) * env_ad(n, 0.01, 0.08)
    snap = thud(300, 90, 0.06, 0.015)
    return finish(mix(0.4, (0, rip, 1.0), (0.12, snap, 0.7), (0.12, click(rng, 0.003, 3500), 0.5)))


def skill_crotch_grab(seed):  # 掏蛋子: a low dash that grabs: ground-level rush, then a tight squeeze slap
    rng = np.random.default_rng(seed)
    return finish(mix(0.42, (0, whoosh(rng, 0.25, 250, 1100, 0.02, 1.0, 0.07), 0.7), (0.05, band(noise(rng, 0.03), 1200, 4000) * env_ad(int(0.03 * RATE), 0.001, 0.008), 1.0),
                      (0.05, thud(160, 90, 0.08, 0.02), 0.7)))


def skill_unplug(seed):  # 拧你手腕: a parry stance: a guarded leather creak and a ready click
    rng = np.random.default_rng(seed)
    n = int(0.36 * RATE)
    t = np.arange(n) / RATE
    creak = band(noise(rng, 0.36) * (rng.random(n) < 0.04), 400, 1600) * 4 * env_ad(n, 0.05, 0.12)
    return finish(mix(0.4, (0, click(rng, 0.005, 1800), 0.9), (0, creak, 0.8), (0.02, ring([1600, 2420], 0.25, [0.05, 0.03], [0.3, 0.15]), 1.0)))


def skill_snark_bolt(seed):  # 甩你一鞋: a slipper flung: a flick whoosh with a flapping sole
    rng = np.random.default_rng(seed)
    n = int(0.3 * RATE)
    t = np.arange(n) / RATE
    flap = band(noise(rng, 0.3), 600, 2500) * (0.5 + 0.5 * np.sin(2 * np.pi * 28 * t)) * env_ad(n, 0.01, 0.09)
    return finish(mix(0.34, (0, whoosh(rng, 0.2, 800, 3200, 0.012, 1.5, 0.05), 0.8), (0.02, flap, 0.6), (0, click(rng, 0.003, 2000), 0.4)))


def skill_seethe(seed):  # 烫你屁股: fire bursts around the body: a rising ignition whoomp with crackle
    rng = np.random.default_rng(seed)
    n = int(0.6 * RATE)
    t = np.arange(n) / RATE
    roar = sweep_band(noise(rng, 0.6), 400 + 2200 * np.exp(-t / 0.15), q=0.8) * env_ad(n, 0.03, 0.18)
    return finish(mix(0.62, (0, roar, 1.0), (0, crackle(rng, 0.6, 260, 30, 900, 4500) * np.exp(-t / 0.3), 0.5), (0, thud(90, 45, 0.25, 0.08), 0.7)))


def skill_quick_meeting(seed):  # 脚底开锅: a boiling zone under the feet: a lid clank and a burst of bubbling
    rng = np.random.default_rng(seed)
    n = int(0.6 * RATE)
    bubbles = np.zeros(n)
    for _ in range(26):
        start = int(rng.uniform(0.02, 0.45) * RATE)
        dur = rng.uniform(0.015, 0.04)
        blip = chirp(rng.uniform(250, 500), rng.uniform(600, 1100), dur, 2.0) * env_ad(int(dur * RATE), 0.002, dur / 3)
        bubbles[start:start + len(blip)] += rng.uniform(0.3, 0.7) * blip[: n - start]
    hiss = band(noise(rng, 0.6), 2500, 6000) * env_ad(n, 0.02, 0.2)
    return finish(mix(0.62, (0, ring([380, 1010, 1730], 0.3, [0.12, 0.06, 0.04], [0.5, 0.3, 0.15]), 0.6), (0, bubbles, 0.8), (0, hiss, 0.25)))


def skill_bail_out(seed):  # 贴脸补一拳: a dash that empowers the next blow: a quick rush and a knuckle crack
    rng = np.random.default_rng(seed)
    return finish(mix(0.32, (0, whoosh(rng, 0.2, 600, 2600, 0.03, 1.2, 0.05), 0.8), (0.09, click(rng, 0.003, 1500), 1.0),
                      (0.1, click(rng, 0.002, 2500), 0.7), (0.09, thud(180, 120, 0.04, 0.01), 0.4)))


def skill_not_mad(seed):  # 锅盖挡脸: a shield up: a pot-lid clang (inharmonic metal ring)
    rng = np.random.default_rng(seed)
    clang = ring([410, 1060, 1870, 2950], 0.6, [0.25, 0.14, 0.09, 0.05], [0.6, 0.5, 0.3, 0.2])
    return finish(mix(0.62, (0, click(rng, 0.004, 1500), 0.8), (0, clang, 1.0), (0, thud(160, 100, 0.08, 0.02), 0.4)))


def skill_stare_down(seed):  # 来打老子啊: a taunt: a chest slap and a low swell that dares
    rng = np.random.default_rng(seed)
    n = int(0.6 * RATE)
    t = np.arange(n) / RATE
    swell = low(noise(rng, 0.6), 300) * np.clip(t / 0.15, 0, 1) * np.exp(-np.clip(t - 0.2, 0, None) / 0.12)
    slap = band(noise(rng, 0.03), 900, 3500) * env_ad(int(0.03 * RATE), 0.001, 0.008)
    return finish(mix(0.62, (0, slap, 1.0), (0, thud(110, 60, 0.15, 0.05), 0.8), (0.02, swell, 0.6), (0.12, slap, 0.6)))


def skill_triple_combo(seed):  # 左右开抽: three blows primed: three quick snaps, left-right-left
    rng = np.random.default_rng(seed)
    snap = lambda: band(noise(rng, 0.03), 1200, 4500) * env_ad(int(0.03 * RATE), 0.001, 0.007)
    return finish(mix(0.32, (0, snap(), 1.0), (0.07, snap(), 0.8), (0.14, snap(), 1.0), (0, whoosh(rng, 0.22, 900, 2800, 0.02, 1.6, 0.05), 0.35)))


def skill_calculus_drop(seed):  # 板砖糊脸: a brick slammed to the ground: a falling whoosh into a heavy crack
    rng = np.random.default_rng(seed)
    crack = band(noise(rng, 0.08), 600, 4000) * env_ad(int(0.08 * RATE), 0.001, 0.02)
    debris = crackle(rng, 0.4, 120, 10, 1500, 6000) * np.exp(-t_axis(0.4) / 0.15)
    return finish(soft_clip(mix(0.62, (0, crack, 1.0), (0, thud(90, 38, 0.35, 0.11), 1.0), (0.01, debris, 0.5)), 1.5))


def skill_flyer_trap(seed):  # 踩一脚试试: a paper trap laid: a rustle and a springy arm click
    rng = np.random.default_rng(seed)
    n = int(0.36 * RATE)
    rustle = band(noise(rng, 0.36), 1500, 7000) * (rng.random(n) < 0.3) * env_ad(n, 0.02, 0.1)
    return finish(mix(0.4, (0, click(rng, 0.003, 2000), 1.0), (0, ring([2200], 0.12, [0.03], [0.4]), 1.0), (0.01, low(rustle, 6000), 0.45)))


def skill_open_mic(seed):  # 扯嗓子喷你: a shout ring that shoves: a vowel-like roar and a pushing boom
    rng = np.random.default_rng(seed)
    n = int(0.6 * RATE)
    t = np.arange(n) / RATE
    source = buzz(130, 0.6, rng)
    vowel = band(source, 600, 900, 2) * 1.2 + band(source, 1100, 1400, 2) * 0.8 + band(source, 2400, 2800, 2) * 0.4
    vowel *= np.clip(t / 0.02, 0, 1) * np.exp(-np.clip(t - 0.15, 0, None) / 0.12)
    return finish(soft_clip(mix(0.62, (0, vowel, 1.0), (0, thud(80, 40, 0.3, 0.1), 0.8), (0, whoosh(rng, 0.4, 300, 1500, 0.02, 0.9, 0.1), 0.4)), 1.6))


def skill_hose_down(seed):  # 滋你一脸: a pressurised water jet: a nozzle pop and a hard spray hiss
    rng = np.random.default_rng(seed)
    n = int(0.5 * RATE)
    t = np.arange(n) / RATE
    spray = band(noise(rng, 0.5), 1800, 9000) * (0.85 + 0.15 * np.sin(2 * np.pi * 37 * t)) * np.clip(t / 0.01, 0, 1) * np.exp(-t / 0.2)
    return finish(mix(0.52, (0, thud(200, 120, 0.04, 0.01), 0.7), (0, spray, 1.0), (0.05, hit_water(seed)[: int(0.3 * RATE)], 0.3)))


def skill_charged_ram(seed):  # 蛮牛顶: a charging shoulder with a shield: heavy stomp and a low rushing body
    rng = np.random.default_rng(seed)
    return finish(soft_clip(mix(0.6, (0, thud(70, 40, 0.25, 0.08), 1.0), (0, whoosh(rng, 0.5, 200, 900, 0.06, 0.8, 0.12), 0.9),
                                (0.02, cloth(rng, 0.3, 0.08), 0.4)), 1.4))


def skill_brace(seed):  # 顶着揍: bracing for blows: a firm plant and a tight creak
    rng = np.random.default_rng(seed)
    n = int(0.34 * RATE)
    creak = band(noise(rng, 0.34) * (rng.random(n) < 0.06), 250, 1200) * 4 * env_ad(n, 0.03, 0.1)
    return finish(mix(0.36, (0, thud(100, 70, 0.12, 0.04), 1.0), (0, creak, 0.7), (0, cloth(rng, 0.2, 0.05), 0.5)))


def skill_cola_shake(seed):  # 冰可乐摇一摇: a shaken can bursting cold: a can rattle, a tab hiss and a fizzing spray
    rng = np.random.default_rng(seed)
    n = int(0.6 * RATE)
    t = np.arange(n) / RATE
    rattle = band(noise(rng, 0.12), 2000, 6000) * (0.5 + 0.5 * np.sign(np.sin(2 * np.pi * 18 * t_axis(0.12)))) * env_ad(int(0.12 * RATE), 0.005, 0.04)
    fizz = high(noise(rng, 0.6), 2500) * (rng.random(n) < 0.5) * np.clip(t / 0.01, 0, 1) * np.exp(-t / 0.2)
    tink = ring([3100, 4700], 0.2, [0.06, 0.04], [0.3, 0.2])
    return finish(mix(0.62, (0, click(rng, 0.004, 3000), 0.8), (0, tink, 0.6), (0, low(fizz, 7000), 0.55), (0, rattle, 0.5)))


def skill_desk_fan(seed):  # 电风扇: a desk fan blowing: a switch click and a motor spinning up into wind
    rng = np.random.default_rng(seed)
    n = int(0.6 * RATE)
    t = np.arange(n) / RATE
    hum = np.sin(2 * np.pi * np.cumsum(40 + 80 * (1 - np.exp(-t / 0.15))) / RATE) * 0.5 + 0.3 * np.sin(2 * np.pi * np.cumsum(80 + 160 * (1 - np.exp(-t / 0.15))) / RATE)
    blades = band(noise(rng, 0.6), 500, 3000) * (0.6 + 0.4 * np.sin(2 * np.pi * np.cumsum(5 + 25 * (1 - np.exp(-t / 0.15))) / RATE))
    shape = np.clip(t / 0.08, 0, 1) * np.exp(-np.clip(t - 0.3, 0, None) / 0.12)
    return finish(mix(0.62, (0, click(rng, 0.004, 1500), 0.8), (0, hum * shape, 0.4), (0, blades * shape, 0.8)))


def skill_stale_takeout(seed):  # 过期外卖: rotten takeout thrown: a wet squelch and a buzz of flies
    rng = np.random.default_rng(seed)
    n = int(0.6 * RATE)
    t = np.arange(n) / RATE
    squelch = low(noise(rng, 0.12), 900) * (0.5 + 0.5 * np.sin(2 * np.pi * np.cumsum(30 + 60 * t_axis(0.12)) / RATE)) * env_ad(int(0.12 * RATE), 0.005, 0.04)
    flies = sum(np.sin(2 * np.pi * (f + 15 * np.sin(2 * np.pi * r * t)) * t) for f, r in ((180, 7.0), (225, 9.3), (205, 5.1))) / 3
    flies = band(flies + 0.3 * np.sign(flies), 150, 2000) * np.clip((t - 0.05) / 0.05, 0, 1) * np.exp(-np.clip(t - 0.15, 0, None) / 0.15)
    return finish(mix(0.62, (0, whoosh(rng, 0.15, 500, 1800, 0.01, 1.2, 0.04), 0.4), (0, squelch, 1.0), (0.05, flies, 0.18)))


def skill_school_bag(seed):  # 占座书包: a heavy school bag dropped to claim a seat: a zipper and a heavy book thud
    rng = np.random.default_rng(seed)
    n = int(0.16 * RATE)
    t = np.arange(n) / RATE
    zip_ = band(noise(rng, 0.16), 2000, 7000) * (rng.random(n) < 0.25) * np.clip(t / 0.02, 0, 1)
    return finish(soft_clip(mix(0.55, (0, thud(95, 45, 0.25, 0.07), 1.0), (0, band(noise(rng, 0.1), 300, 2000) * env_ad(int(0.1 * RATE), 0.002, 0.03), 0.7), (0.15, zip_, 0.3)), 1.3))


def skill_flurry_rush(seed):  # 疯狗乱拳: a frenzy of blows: a rapid burst of short punches and whooshes
    rng = np.random.default_rng(seed)
    layers = []
    for i in range(6):
        at = max(0.0, i * 0.055 + rng.uniform(-0.01, 0.01))
        layers.append((at, whoosh(rng, 0.08, 800, 2600, 0.01, 1.5, 0.025), 0.5))
        layers.append((at + 0.02, band(noise(rng, 0.02), 900, 3500) * env_ad(int(0.02 * RATE), 0.001, 0.006), 0.7))
    return finish(mix(0.5, *layers))


def skill_cordon_tape(seed):  # 警戒封条: elastic tape strung: a stretching squeak and a taut snap
    rng = np.random.default_rng(seed)
    squeak = chirp(500, 1400, 0.15, 1.5) * env_ad(int(0.15 * RATE), 0.02, 0.06)
    squeak = band(squeak + 0.2 * np.sign(squeak), 400, 4000)
    snap = band(noise(rng, 0.03), 1500, 6000) * env_ad(int(0.03 * RATE), 0.001, 0.006)
    return finish(mix(0.36, (0, snap, 1.0), (0, ring([180], 0.15, [0.05], [0.6]), 1.0), (0.02, squeak, 0.35)))


SKILL_SOUNDS = {
    "active_elbow": skill_elbow, "active_pants_down": skill_pants_down, "active_crotch_grab": skill_crotch_grab,
    "active_unplug": skill_unplug, "active_snark_bolt": skill_snark_bolt, "active_seethe": skill_seethe,
    "active_quick_meeting": skill_quick_meeting, "active_bail_out": skill_bail_out, "active_not_mad": skill_not_mad,
    "active_stare_down": skill_stare_down, "active_triple_combo": skill_triple_combo, "active_calculus_drop": skill_calculus_drop,
    "active_flyer_trap": skill_flyer_trap, "active_open_mic": skill_open_mic, "active_hose_down": skill_hose_down,
    "active_charged_ram": skill_charged_ram, "active_brace": skill_brace, "active_cola_shake": skill_cola_shake,
    "active_desk_fan": skill_desk_fan, "active_stale_takeout": skill_stale_takeout, "active_school_bag": skill_school_bag,
    "active_flurry_rush": skill_flurry_rush, "active_cordon_tape": skill_cordon_tape,
}


SOUNDS = {
    "dealt_crit": (dealt_crit, 111),
    "dealt_kill": (dealt_kill, 121),
    "skill_zap_1": (zap_release, 131), "skill_zap_2": (zap_release, 132),
}
# Two takes per skill release, seeded per skill.
for _index, (_ability, _make) in enumerate(SKILL_SOUNDS.items()):
    for _take in (1, 2):
        # Releases sit in the combat mid-band: no harsh air above 8.5 kHz.
        SOUNDS[f"skill_{_ability[len('active_'):]}_{_take}"] = ((lambda make: lambda seed: finish(low(make(seed), 8500, 4)))(_make), 1000 + 10 * _index + _take)


# ---------------------------------------------------------------- skill hits (on the struck body, transient at 0)
# What the blow is made of: bone, rubber, brick, water, ice, rot... layered over the approved body-hit grammar
# (a click, a short low body, a tail that is gone by 350 ms).

def body(rng, weight=1.0):
    """The approved body-hit core: click + low thud + short tail."""
    return mix(0.3, (0, click(rng), 0.5), (0, thud(130 - 30 * weight, 50, 0.11 + 0.05 * weight, 0.03 + 0.02 * weight), 0.9),
               (0.002, low(noise(rng, 0.2), 900) * env_ad(int(0.2 * RATE), 0.004, 0.04), 0.3))


def hit_elbow(seed):  # bony elbow into ribs: a hard crack over a heavy body
    rng = np.random.default_rng(seed)
    crack = band(noise(rng, 0.03), 1500, 5000) * env_ad(int(0.03 * RATE), 0.001, 0.006)
    return mix(0.36, (0, body(rng, 1.3), 1.0), (0, crack, 0.8))


def hit_slap(seed):  # an open-hand / cloth slap (pants down)
    rng = np.random.default_rng(seed)
    slap = band(noise(rng, 0.05), 900, 4500) * env_ad(int(0.05 * RATE), 0.001, 0.01)
    return mix(0.3, (0, slap, 1.0), (0, body(rng, 0.6), 0.6), (0.01, cloth(rng, 0.15, 0.04), 0.4))


def hit_squeeze(seed):  # a grab that squeezes (crotch grab): a muffled squish and a tight squeak
    rng = np.random.default_rng(seed)
    squish = low(noise(rng, 0.12), 700) * env_ad(int(0.12 * RATE), 0.004, 0.03)
    squeak = chirp(900, 1500, 0.08, 1.2) * env_ad(int(0.08 * RATE), 0.005, 0.02)
    return mix(0.3, (0, body(rng, 0.8), 0.8), (0, squish, 0.8), (0.01, squeak, 0.25))


def hit_slipper(seed):  # a rubber slipper smack: a flat, bright slap with a rubber wobble
    rng = np.random.default_rng(seed)
    n = int(0.12 * RATE)
    t = np.arange(n) / RATE
    smack = band(noise(rng, 0.12), 700, 3800) * env_ad(n, 0.001, 0.015) * (1 + 0.4 * np.sin(2 * np.pi * 55 * t))
    return mix(0.3, (0, smack, 1.0), (0, thud(170, 110, 0.06, 0.015), 0.5))


def hit_scald(seed):  # a scald (seethe, the boiling zone): a hit that sears with a short sizzle
    rng = np.random.default_rng(seed)
    n = int(0.4 * RATE)
    t = np.arange(n) / RATE
    sizzle = band(noise(rng, 0.4), 2500, 7000) * (rng.random(n) < 0.4) * np.clip(t / 0.01, 0, 1) * np.exp(-t / 0.12)
    return mix(0.42, (0, body(rng, 0.7), 0.7), (0, hit_fire(seed)[: int(0.3 * RATE)], 0.45), (0.01, sizzle, 0.5))


def hit_brick(seed):  # a brick on the head: a hard crack, a heavy low body and crumbling grit
    rng = np.random.default_rng(seed)
    crack = band(noise(rng, 0.05), 800, 5000) * env_ad(int(0.05 * RATE), 0.001, 0.01)
    grit = crackle(rng, 0.3, 160, 10, 2000, 6000) * np.exp(-t_axis(0.3) / 0.1)
    return soft_clip(mix(0.42, (0, body(rng, 1.6), 1.0), (0, crack, 1.0), (0.01, grit, 0.5)), 1.4)


def hit_trap(seed):  # a snap trap closing on a foot: a tight double clack over a light body
    rng = np.random.default_rng(seed)
    clack = lambda: ring([1900, 3300], 0.06, [0.015, 0.01], [0.6, 0.3]) + pad(click(rng, 0.003, 2500), int(0.06 * RATE))
    return mix(0.3, (0, clack(), 1.0), (0.025, clack(), 0.7), (0, body(rng, 0.5), 0.5))


def hit_shout(seed):  # the shout's blast arriving: a low air whump with a shove of wind, no crack
    rng = np.random.default_rng(seed)
    return mix(0.4, (0, thud(85, 45, 0.25, 0.08), 1.0), (0, whoosh(rng, 0.35, 250, 1200, 0.01, 0.8, 0.09), 0.7))


def hit_jet(seed):  # a water jet slamming in: a splat and a spray that sprays off
    rng = np.random.default_rng(seed)
    splat = band(noise(rng, 0.06), 500, 3000) * env_ad(int(0.06 * RATE), 0.001, 0.012)
    return mix(0.42, (0, splat, 1.0), (0, hit_water(seed)[: int(0.4 * RATE)], 0.7), (0, body(rng, 0.6), 0.4))


def hit_ram(seed):  # a charging shoulder slam: the heaviest body, a grunt of air and a scuff
    rng = np.random.default_rng(seed)
    return soft_clip(mix(0.45, (0, body(rng, 2.0), 1.0), (0, whoosh(rng, 0.2, 200, 800, 0.005, 0.8, 0.05), 0.5),
                         (0.02, cloth(rng, 0.2, 0.05), 0.5)), 1.5)


def hit_ice(seed):  # an icy hit (the cola cone's chill): a glassy crack, frost crackle and a fizz
    rng = np.random.default_rng(seed)
    n = int(0.4 * RATE)
    t = np.arange(n) / RATE
    glass = ring([2600, 4100, 5900], 0.25, [0.05, 0.035, 0.02], [0.5, 0.35, 0.2])
    frost = crackle(rng, 0.4, 500, 60, 3000, 8000) * np.exp(-t / 0.12)
    fizz = high(noise(rng, 0.4), 3000) * (rng.random(n) < 0.4) * np.exp(-t / 0.15)
    return mix(0.42, (0, click(rng, 0.004, 3000), 0.8), (0, glass, 0.7), (0, frost, 0.5), (0, low(fizz, 7000), 0.3), (0, body(rng, 0.5), 0.5))


def hit_rot(seed):  # rotten takeout splatting: a wet slap, a squelch and a few drips
    rng = np.random.default_rng(seed)
    n = int(0.42 * RATE)
    splat = low(noise(rng, 0.1), 1500) * env_ad(int(0.1 * RATE), 0.001, 0.025)
    squelch = low(noise(rng, 0.2), 600) * (0.5 + 0.5 * np.sin(2 * np.pi * np.cumsum(25 + 50 * t_axis(0.2)) / RATE)) * env_ad(int(0.2 * RATE), 0.01, 0.05)
    drips = np.zeros(n)
    for _ in range(5):
        start = int(rng.uniform(0.08, 0.35) * RATE)
        blip = chirp(rng.uniform(500, 800), rng.uniform(900, 1400), 0.02, 2.0) * env_ad(int(0.02 * RATE), 0.001, 0.006)
        drips[start:start + len(blip)] += 0.4 * blip[: n - start]
    return mix(0.42, (0, splat, 1.0), (0.01, squelch, 0.7), (0, drips, 0.5), (0, body(rng, 0.5), 0.4))


def hit_bag(seed):  # a stuffed school bag: a padded heavy thump and a rattle of pencils
    rng = np.random.default_rng(seed)
    rattle = crackle(rng, 0.15, 300, 50, 2500, 6000) * np.exp(-t_axis(0.15) / 0.05)
    return mix(0.36, (0, thud(100, 50, 0.2, 0.06), 1.0), (0, low(noise(rng, 0.1), 1200) * env_ad(int(0.1 * RATE), 0.002, 0.02), 0.6), (0.01, rattle, 0.4))


def hit_tape(seed):  # elastic tape lashing: a whip crack and a twang
    rng = np.random.default_rng(seed)
    whip = band(noise(rng, 0.02), 2000, 7000) * env_ad(int(0.02 * RATE), 0.0005, 0.004)
    return mix(0.3, (0, whip, 1.0), (0, ring([150, 300], 0.2, [0.06, 0.04], [0.5, 0.25]), 0.8), (0, body(rng, 0.4), 0.4))


def hit_flurry(seed):  # a frenzy wave's blows: four quick light hits in a patter
    rng = np.random.default_rng(seed)
    return mix(0.36, *[(i * 0.045, body(rng, 0.4), 0.8 - 0.1 * i) for i in range(4)])


def hit_twist(seed):  # a wrist twisted in the counter: a knuckle crack and a strained creak
    rng = np.random.default_rng(seed)
    n = int(0.25 * RATE)
    creak = band(noise(rng, 0.25) * (rng.random(n) < 0.08), 300, 1400) * 4 * env_ad(n, 0.01, 0.06)
    return mix(0.3, (0, click(rng, 0.003, 1500), 1.0), (0.01, click(rng, 0.002, 2500), 0.7), (0, creak, 0.7), (0, body(rng, 0.6), 0.5))


def hit_gust(seed):  # a fan's gust buffeting a body: a soft wind slam, no blow
    rng = np.random.default_rng(seed)
    return mix(0.4, (0, whoosh(rng, 0.38, 300, 1600, 0.01, 0.7, 0.1), 1.0), (0, thud(110, 70, 0.08, 0.02), 0.3))


SKILL_HITS = {
    "active_pants_down": hit_slap, "active_crotch_grab": hit_squeeze, "active_unplug": hit_twist, "active_snark_bolt": hit_slipper,
    "active_seethe": hit_scald, "active_elbow": hit_elbow, "active_quick_meeting": hit_scald, "active_calculus_drop": hit_brick,
    "active_flyer_trap": hit_trap, "active_open_mic": hit_shout, "active_hose_down": hit_jet, "active_charged_ram": hit_ram,
    "active_cola_shake": hit_ice, "active_desk_fan": hit_gust, "active_stale_takeout": hit_rot, "active_school_bag": hit_bag,
    "active_flurry_rush": hit_flurry, "active_cordon_tape": hit_tape,
}
for _index, (_ability, _make) in enumerate(SKILL_HITS.items()):
    for _take in (1, 2):
        SOUNDS[f"skillhit_{_ability[len('active_'):]}_{_take}"] = ((lambda make: lambda seed: finish(low(make(seed), 8500, 4)))(_make), 2000 + 10 * _index + _take)


# ---------------------------------------------------------------- element reactions and states

def r_fog(seed):  # wet + fire: a burst of steam hissing out
    rng = np.random.default_rng(seed)
    n = int(0.6 * RATE)
    t = np.arange(n) / RATE
    steam = sweep_band(noise(rng, 0.6), 5000 * np.exp(-t / 0.2) + 1500, q=0.9) * np.clip(t / 0.01, 0, 1) * np.exp(-t / 0.22)
    return mix(0.62, (0, thud(140, 80, 0.06, 0.015), 0.6), (0, low(steam, 8000), 1.0))


def r_conduct(seed):  # wet + shock: the splash field: a wide crackling discharge that spreads
    rng = np.random.default_rng(seed)
    n = int(0.6 * RATE)
    t = np.arange(n) / RATE
    spread = crackle(rng, 0.6, 1400, 150, 1500, 5500) * np.clip(t / 0.03, 0, 1) * np.exp(-t / 0.25)
    return mix(0.62, (0, hit_electric(seed)[: int(0.3 * RATE)], 0.6), (0, spread, 0.9), (0, buzz(100, 0.6, rng) * np.exp(-t / 0.2), 0.3), (0, thud(90, 50, 0.2, 0.06), 0.5))


def r_overload(seed):  # fire + shock: a short explosive pop with sparks
    rng = np.random.default_rng(seed)
    sparks = crackle(rng, 0.5, 600, 40, 1800, 6000) * np.exp(-t_axis(0.5) / 0.15)
    return soft_clip(mix(0.55, (0, thud(80, 35, 0.3, 0.09), 1.0), (0, band(noise(rng, 0.08), 400, 3000) * env_ad(int(0.08 * RATE), 0.001, 0.02), 0.9), (0, sparks, 0.6)), 1.6)


def r_melt(seed):  # fire + chill: ice cracking as it melts, a hiss and drips
    rng = np.random.default_rng(seed)
    return mix(0.6, (0, ring([2300, 3700], 0.2, [0.04, 0.03], [0.4, 0.2]), 0.7), (0, crackle(rng, 0.4, 300, 30, 2000, 6000) * np.exp(-t_axis(0.4) / 0.12), 0.6),
               (0.05, low(high(noise(rng, 0.5), 2500), 7000) * env_ad(int(0.5 * RATE), 0.03, 0.15), 0.35), (0.1, hit_water(seed)[: int(0.3 * RATE)], 0.3))


def r_methane(seed):  # fire + rot: a gas pocket catching: a whoompf that rolls
    rng = np.random.default_rng(seed)
    n = int(0.6 * RATE)
    t = np.arange(n) / RATE
    roll = sweep_band(noise(rng, 0.6), 300 + 1200 * np.exp(-t / 0.08), q=0.7) * np.clip(t / 0.015, 0, 1) * np.exp(-t / 0.2)
    return soft_clip(mix(0.62, (0, roll, 1.0), (0, thud(70, 35, 0.35, 0.1), 0.9), (0.05, crackle(rng, 0.5, 200, 20, 900, 4000) * np.exp(-t_axis(0.5) / 0.2), 0.4)), 1.4)


def r_freeze(seed):  # wet + chill: water seizing into ice: a fast crystallising creak and a locking crack
    rng = np.random.default_rng(seed)
    n = int(0.5 * RATE)
    t = np.arange(n) / RATE
    grow = crackle(rng, 0.5, 600, 1500, 3000, 8000) * np.clip(0.3 + t / 0.1, 0, 1) * np.exp(-np.clip(t - 0.1, 0, None) / 0.06)
    lock = ring([1800, 2900, 4300], 0.25, [0.06, 0.04, 0.03], [0.6, 0.4, 0.25])
    return mix(0.55, (0, click(rng, 0.004, 2500), 0.5), (0, grow, 0.7), (0.1, lock, 1.0), (0.1, click(rng, 0.004, 2500), 0.8))


def r_ferment(seed):  # wet + rot: a bubbling fermenting gurgle
    rng = np.random.default_rng(seed)
    n = int(0.6 * RATE)
    gurgle = np.zeros(n)
    for _ in range(18):
        start = int(rng.uniform(0.0, 0.45) * RATE)
        dur = rng.uniform(0.02, 0.06)
        blip = chirp(rng.uniform(150, 300), rng.uniform(300, 600), dur, 2.0) * env_ad(int(dur * RATE), 0.003, dur / 3)
        gurgle[start:start + len(blip)] += rng.uniform(0.3, 0.8) * blip[: n - start]
    return mix(0.62, (0, low(gurgle, 2000), 1.0), (0, low(noise(rng, 0.3), 500) * env_ad(int(0.3 * RATE), 0.01, 0.08), 0.4))


def r_shatter(seed):  # chill + a blow: frozen body shattering: a big glass crack and falling shards
    rng = np.random.default_rng(seed)
    n = int(0.6 * RATE)
    shards = np.zeros(n)
    for _ in range(16):
        start = int(rng.uniform(0.04, 0.5) * RATE)
        f = rng.uniform(2500, 6000)
        shard = ring([f, f * 1.47], 0.06, [0.015, 0.01], [0.5, 0.3])
        shards[start:start + len(shard)] += rng.uniform(0.2, 0.6) * shard[: n - start]
    return mix(0.62, (0, ring([1500, 2700, 4100], 0.3, [0.05, 0.035, 0.025], [0.7, 0.5, 0.3]), 0.9), (0, band(noise(rng, 0.05), 1500, 7000) * env_ad(int(0.05 * RATE), 0.001, 0.01), 0.9),
               (0, shards, 0.6), (0, thud(120, 60, 0.12, 0.03), 0.5))


def r_corrode(seed):  # shock + rot: a corroding fizz: acid sizzle with a sour crackle
    rng = np.random.default_rng(seed)
    n = int(0.6 * RATE)
    t = np.arange(n) / RATE
    fizz = band(noise(rng, 0.6), 2000, 6500) * (rng.random(n) < 0.5) * np.clip(t / 0.02, 0, 1) * np.exp(-t / 0.25)
    return mix(0.62, (0, fizz, 0.9), (0, crackle(rng, 0.6, 300, 60, 900, 3000) * np.exp(-t / 0.25), 0.6), (0, buzz(70, 0.6, rng) * np.exp(-t / 0.15), 0.2))


def r_frostbite(seed):  # chill + rot: a numbing bite: a cold creak and a low ache
    rng = np.random.default_rng(seed)
    n = int(0.55 * RATE)
    t = np.arange(n) / RATE
    ache = np.sin(2 * np.pi * np.cumsum(220 - 60 * t / 0.55) / RATE) * np.clip(t / 0.03, 0, 1) * np.exp(-t / 0.2)
    return mix(0.55, (0, ring([2100, 3400], 0.2, [0.05, 0.03], [0.5, 0.3]), 0.7), (0, ache, 0.5), (0, crackle(rng, 0.3, 300, 30, 2500, 7000) * np.exp(-t_axis(0.3) / 0.1), 0.5))


def s_burn(seed):  # catching fire: a quick flare-up
    rng = np.random.default_rng(seed)
    n = int(0.35 * RATE)
    t = np.arange(n) / RATE
    return mix(0.36, (0, sweep_band(noise(rng, 0.35), 400 + 2500 * np.clip(t / 0.06, 0, 1) * np.exp(-t / 0.15), q=1.0) * env_ad(n, 0.03, 0.08), 1.0),
               (0, crackle(rng, 0.35, 150, 40, 1000, 4000) * np.exp(-t / 0.15), 0.4))


def s_wet(seed):  # soaked: a cold drench and drips
    rng = np.random.default_rng(seed)
    return mix(0.36, (0, band(noise(rng, 0.2), 800, 5000) * env_ad(int(0.2 * RATE), 0.005, 0.04), 0.7), (0.03, hit_water(seed)[: int(0.3 * RATE)], 0.5))


def s_shock(seed):  # electrified: a tingling crackle that ticks
    rng = np.random.default_rng(seed)
    n = int(0.35 * RATE)
    t = np.arange(n) / RATE
    ticks = crackle(rng, 0.35, 400, 80, 2000, 5500) * np.exp(-t / 0.15)
    return mix(0.36, (0, ticks, 1.0), (0, buzz(115, 0.35, rng) * np.exp(-t / 0.08), 0.3))


def s_chill(seed):  # chilled: frost creeping over the skin
    rng = np.random.default_rng(seed)
    n = int(0.35 * RATE)
    t = np.arange(n) / RATE
    return mix(0.36, (0, crackle(rng, 0.35, 150, 900, 3000, 8000) * np.clip(t / 0.2, 0, 1) * np.exp(-np.clip(t - 0.2, 0, None) / 0.05), 0.8),
               (0.18, ring([3000], 0.12, [0.03], [0.5]), 0.6))


def s_rot(seed):  # rotting: a sour squelch and a fly buzz
    rng = np.random.default_rng(seed)
    n = int(0.4 * RATE)
    t = np.arange(n) / RATE
    fly = band(np.sin(2 * np.pi * (200 + 20 * np.sin(2 * np.pi * 8 * t)) * t) + 0.3 * np.sign(np.sin(2 * np.pi * 200 * t)), 150, 2000) * np.clip(t / 0.05, 0, 1) * np.exp(-t / 0.15)
    return mix(0.42, (0, low(noise(rng, 0.1), 700) * env_ad(int(0.1 * RATE), 0.005, 0.03), 0.9), (0.02, fly, 0.3))


def match_start(seed):  # the field opening: a crowd-less starting whistle with a deep hit
    rng = np.random.default_rng(seed)
    n = int(0.9 * RATE)
    t = np.arange(n) / RATE
    whistle = np.sin(2 * np.pi * np.cumsum(2900 + 120 * np.sin(2 * np.pi * 32 * t)) / RATE) * np.clip(t / 0.02, 0, 1) * np.exp(-np.clip(t - 0.35, 0, None) / 0.08)
    whistle = band(whistle + 0.15 * band(noise(rng, 0.9), 2500, 4000), 1500, 6000)
    return mix(0.95, (0, thud(70, 40, 0.4, 0.12), 0.9), (0.02, whistle, 0.45))


for _name, _make, _seed in (("match_start", match_start, 3201),):
    SOUNDS[_name] = ((lambda make: lambda seed: finish(low(make(seed), 9000, 4)))(_make), _seed)


# ---------------------------------------------------------------- match events and own moments (street, not beeps)

def spray(rng, seconds, rattle=True):
    """A spray can: a bead rattle then a pressurised hiss."""
    n = int(seconds * RATE)
    t = np.arange(n) / RATE
    hiss = band(noise(rng, seconds), 2500, 8000) * np.clip((t - 0.12) / 0.03, 0, 1) * np.exp(-np.clip(t - 0.4, 0, None) / 0.15)
    clack = np.zeros(n)
    if rattle:
        for i in range(4):
            start = int((0.02 + i * 0.028) * RATE)
            hit = ring([2400 + 300 * i, 3900], 0.03, [0.008, 0.006], [0.6, 0.3])
            clack[start:start + len(hit)] += hit[: n - start]
    return clack + 0.7 * hiss


def capture_opened(seed):  # the flagpole opening: a deep bell-iron hit and a rising crowd-less swell
    rng = np.random.default_rng(seed)
    n = int(1.2 * RATE)
    t = np.arange(n) / RATE
    bell = ring([196, 392, 588, 951], 1.2, [0.5, 0.35, 0.2, 0.12], [0.8, 0.5, 0.3, 0.2])
    swell = sweep_band(noise(rng, 1.2), 400 + 1600 * np.clip(t / 0.8, 0, 1), q=1.5) * np.clip(t / 0.6, 0, 1) * np.exp(-np.clip(t - 0.8, 0, None) / 0.15)
    return mix(1.2, (0, thud(80, 45, 0.4, 0.12), 0.8), (0, bell, 0.6), (0, swell, 0.35))


def capture_started(seed):  # someone starts spraying the flag: a can rattle and a spray burst
    rng = np.random.default_rng(seed)
    return mix(0.7, (0, spray(rng, 0.7), 1.0))


def capture_contested(seed):  # the flag contested: two tense low knocks and a broken spray
    rng = np.random.default_rng(seed)
    knock = lambda: thud(150, 80, 0.12, 0.03) + pad(click(rng, 0.004, 1500), int(0.12 * RATE))
    hiss = band(noise(rng, 0.4), 2500, 7000) * (rng.random(int(0.4 * RATE)) < 0.5) * env_ad(int(0.4 * RATE), 0.01, 0.08)
    return mix(0.6, (0, knock(), 1.0), (0.16, knock(), 0.9), (0.2, hiss, 0.4))


def level_up(seed):  # growing stronger: a warm rising major chord swell with a light sparkle on top
    rng = np.random.default_rng(seed)
    n = int(0.9 * RATE)
    t = np.arange(n) / RATE
    chord = sum(np.sin(2 * np.pi * f * t * (1 + 0.04 * np.clip(t / 0.3, 0, 1))) * g for f, g in ((262, 0.6), (330, 0.45), (392, 0.4), (523, 0.3)))
    chord *= np.clip(t / 0.18, 0, 1) * np.exp(-np.clip(t - 0.35, 0, None) / 0.2)
    sparkle = crackle(rng, 0.9, 30, 120, 4000, 9000) * np.clip((t - 0.2) / 0.2, 0, 1) * np.exp(-np.clip(t - 0.4, 0, None) / 0.2)
    return mix(0.95, (0, low(chord, 3000), 0.7), (0, sparkle, 0.25), (0, thud(110, 70, 0.15, 0.05), 0.4))


def self_death(seed):  # going down: a heavy body drop and a falling low tone
    rng = np.random.default_rng(seed)
    n = int(0.9 * RATE)
    t = np.arange(n) / RATE
    fall = np.sin(2 * np.pi * np.cumsum(180 * np.exp(-t / 0.4) + 50) / RATE) * np.clip(t / 0.02, 0, 1) * np.exp(-t / 0.35)
    return mix(0.95, (0, body(rng, 2.0), 0.9), (0, low(fall, 1200), 0.6), (0.05, cloth(rng, 0.3, 0.08), 0.4))


def self_dash(seed):  # the innate dash: a fast low rush with a sneaker squeak
    rng = np.random.default_rng(seed)
    squeak = band(chirp(1800, 2600, 0.05, 1.0) + 0.2 * np.sign(chirp(1800, 2600, 0.05, 1.0)), 1500, 5000) * env_ad(int(0.05 * RATE), 0.003, 0.012)
    return mix(0.36, (0, whoosh(rng, 0.32, 300, 1800, 0.03, 1.1, 0.07), 1.0), (0, squeak, 0.5))


def perfect_dodge(seed):  # a perfect dodge: a clean air slip and a bright snap of timing
    rng = np.random.default_rng(seed)
    return mix(0.45, (0, whoosh(rng, 0.3, 1200, 4000, 0.02, 2.0, 0.05), 0.8), (0.03, ring([2800, 4200], 0.25, [0.06, 0.04], [0.4, 0.25]), 0.6))


def medal_fuse(seed):  # two element medals fusing: a metallic clink pair and a rising shimmer
    rng = np.random.default_rng(seed)
    n = int(0.8 * RATE)
    t = np.arange(n) / RATE
    shimmer = sum(np.sin(2 * np.pi * f * t) for f in (1320, 1980, 2640)) * np.clip(t / 0.3, 0, 1) * np.exp(-np.clip(t - 0.3, 0, None) / 0.18)
    return mix(0.85, (0, ring([1500, 2380], 0.25, [0.05, 0.03], [0.6, 0.3]), 0.8), (0.08, ring([1700, 2690], 0.25, [0.05, 0.03], [0.6, 0.3]), 0.8),
               (0.1, shimmer, 0.15))


def body_fall(seed):  # another body knocked out: a heavy drop onto the street and a puff of dust
    rng = np.random.default_rng(seed)
    n = int(0.7 * RATE)
    t = np.arange(n) / RATE
    dust = sweep_band(noise(rng, 0.7), 2000 * np.exp(-t / 0.1) + 400, q=0.8) * np.clip(t / 0.01, 0, 1) * np.exp(-t / 0.18)
    return soft_clip(mix(0.72, (0, body(rng, 2.2), 1.0), (0.04, thud(70, 40, 0.3, 0.09), 0.7), (0.01, dust, 0.5), (0.02, cloth(rng, 0.3, 0.08), 0.4)), 1.4)


for _name, _make, _seed in (("capture_opened", capture_opened, 4001), ("capture_started", capture_started, 4002),
                            ("capture_contested", capture_contested, 4003), ("level_up", level_up, 4004),
                            ("self_death", self_death, 4005), ("self_dash", self_dash, 4006),
                            ("perfect_dodge", perfect_dodge, 4007), ("medal_fuse", medal_fuse, 4008)):
    SOUNDS[_name] = ((lambda make: lambda seed: finish(low(make(seed), 9000, 4)))(_make), _seed)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--only", default="", help="write only the sounds whose name contains this text")
    args = parser.parse_args()
    for name, (make, seed) in SOUNDS.items():
        if args.only and args.only not in name:
            continue
        samples = make(seed)
        soundfile.write(OUT / f"{name}.wav", samples.astype(np.float64), RATE, subtype="PCM_16")
        print(f"{name:<20} {len(samples) / RATE * 1000:6.0f} ms")


if __name__ == "__main__":
    main()

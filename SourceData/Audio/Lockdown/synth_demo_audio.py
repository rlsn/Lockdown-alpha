"""Generate Lockdown's synthesised sounds into SourceData/Audio/Lockdown/*.wav: match events, the owner's accents and
the approved zap release (combat, element and skill sounds are layered recordings: SourceData/Audio/Layered).

Authoring tool (the import and validation never run it; they check its sha256 and the hashes of its outputs).
    python -m pip install numpy scipy soundfile
    python SourceData/Audio/Lockdown/synth_demo_audio.py            # writes every sound
    python SourceData/Audio/Lockdown/synth_demo_audio.py --only zap # writes the sounds whose name contains "zap"

Every sound is synthesised from seeded noise and oscillators. Same script + same seed = same bytes.
Design rules (docs/systems/AUDIO_AND_FEEL.md): the transient sits where the sync table puts the event (a swing peaks at
40 ms = its pre-roll, a hit at 0 ms), tails fade out within the table's dissipation, and weight comes from low-mid body
and length, not from level. Files are 48 kHz mono 16-bit, peak-normalised to -1 dBFS; the mix lives in the manifest.
"""

import argparse
from pathlib import Path

import numpy as np
import soundfile
from scipy.signal import butter, sosfilt

RATE = 48000
OUT = Path(__file__).resolve().parent
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


def soft_clip(x, drive=1.5):
    return np.tanh(drive * x) / np.tanh(drive)


def finish(x, tail=0.015):
    x = x - np.mean(x[: min(len(x), 64)]) * 0  # keep DC handling explicit: the high-pass below removes it
    x = high(x, 30, 2)
    x = fade_tail(x, tail)
    peak = np.max(np.abs(x))
    return x * (PEAK / peak) if peak > 0 else x


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


# ---------------------------------------------------------------- building blocks of the remaining sounds

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


SOUNDS = {
    "dealt_crit": (dealt_crit, 111),
    "dealt_kill": (dealt_kill, 121),
    "skill_zap_1": (zap_release, 131), "skill_zap_2": (zap_release, 132),
}


def body(rng, weight=1.0):
    """The approved body-hit core: click + low thud + short tail."""
    return mix(0.3, (0, click(rng), 0.5), (0, thud(130 - 30 * weight, 50, 0.11 + 0.05 * weight, 0.03 + 0.02 * weight), 0.9),
               (0.002, low(noise(rng, 0.2), 900) * env_ad(int(0.2 * RATE), 0.004, 0.04), 0.3))


# ---------------------------------------------------------------- match start

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


for _name, _make, _seed in (("capture_opened", capture_opened, 4001), ("capture_started", capture_started, 4002),
                            ("capture_contested", capture_contested, 4003), ("level_up", level_up, 4004),
                            ("self_death", self_death, 4005), ("self_dash", self_dash, 4006)):
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

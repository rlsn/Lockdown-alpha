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


# Light swing: a short airy whoosh with a cloth flick (the hit itself carries the weight).
def swing_light(n):
    return finish(mix((0, band(fab("MagicWeapon_Swing", n, 4, 0.2, 0.08), lo=700), 0),
                      (0.01, band(cut(load(f"{RPG}/cloth{n % 4 + 1}.ogg"), 0.14), lo=1500), -10)), -20)


# Finisher swing: a heavier, lower thrust whoosh with a belt flap.
def swing_finisher(n):
    return finish(mix((0, band(fab("MagicWeapon_Thrust", n, 4, 0.3, 0.1), lo=250), 0),
                      (0, band(fab("MagicWeapon_Swing", n + 2, 4, 0.25, 0.1), hi=3000), -6),
                      (0.02, band(cut(load(f"{RPG}/clothBelt{'' if n % 2 == 0 else '2'}.ogg"), 0.16), lo=1200), -12)), -17)


# Cast charge: a rising chant charge, cut by the game 30 ms into the release.
def cast_charge(n):
    return finish(mix((0, fab("MagicChant_Charge1", n, 3, 0.9, 0.2), 0),
                      (0, band(fab("MagicRiser_Charge1", n, 3, 0.9, 0.2), lo=1500), -10)), -20)


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


def rpg(names, n, length=None, fade_out=0.03):
    """One of the listed RPG Audio files (their names do not share one numbering)."""
    return cut(load(f"{RPG}/{names[n % len(names)]}.ogg"), length, fade_out)


def scifi(stem, n, count, length=None, fade_out=0.03):
    return cut(load(f"{SCIFI}/{stem}_{n % count:03d}.ogg"), length, fade_out)


def punch(n, weight="medium", length=0.12):
    return kenney(IMPACT, f"impactPunch_{weight}_", n, 5, length)


def rapid(n, count, gap, weight="medium", gain_step=-1.5):
    """count quick punches gap seconds apart, alternating takes."""
    return mix(*[(i * gap, punch(n + i, weight), i * gain_step) for i in range(count)])


CLOTH = ["cloth1", "cloth2", "cloth3", "cloth4"]
BELT = ["clothBelt", "clothBelt2"]
STEP = ["footstep00", "footstep02", "footstep04", "footstep06"]
POT = ["metalPot1", "metalPot2", "metalPot3"]
LEATHER = ["handleSmallLeather", "handleSmallLeather2"]
CREAK = ["creak1", "creak2", "creak3"]
PAPER = ["bookFlip1", "bookFlip2", "bookFlip3"]
BOOK = ["bookPlace1", "bookPlace2", "bookPlace3"]
COINS = ["handleCoins", "handleCoins2"]
SLICE = ["knifeSlice", "knifeSlice2"]


# Skill releases (the caster's motion) and skill hits (on the target): each told by the object or body part of its name.
def skill_pants_down(n):  # cloth yanked twice and an elastic snap
    return finish(mix((0, rpg(BELT, n, 0.18), 0), (0, band(rpg(CLOTH, n, 0.18), lo=600), -3),
                      (0.06, band(rpg(CLOTH, n + 1, 0.18), lo=600), -6)), -17)


def skill_pants_down_hit(n):  # a slap and cloth
    return finish(mix((0, punch(n, "medium", 0.14), 0), (0.01, band(rpg(CLOTH, n + 2, 0.16), lo=1500), -8)), -15)


def skill_crotch_grab(n):  # a tight leather grab on a low dash
    return finish(mix((0, rpg(LEATHER, n, 0.2), 0), (0, band(fab("MagicWeapon_Thrust", n, 4, 0.25, 0.1), hi=2500), -4),
                      (0, band(rpg(STEP, n, 0.15), lo=300), -6)), -17)


def skill_crotch_grab_hit(n):  # a squeezed thud and a squeak
    return finish(mix((0, band(kenney(IMPACT, "impactSoft_medium_", n, 5, 0.18), hi=2200), 0),
                      (0.02, pitch(band(rpg(CREAK, n, 0.2), lo=1200), 0.7), -6)), -15)


def skill_snark_bolt(n):  # a shoe flung: a swing whoosh and a flapping sole
    return finish(mix((0, band(fab("MagicWeapon_Swing", n, 4, 0.25, 0.1), lo=500), 0),
                      (0.03, band(rpg(CLOTH, n + 1, 0.15), lo=900), -6)), -17)


def skill_snark_bolt_hit(n):  # a rubber sole slapping a face
    return finish(mix((0, band(punch(n + 1, "heavy", 0.14), hi=3500), 0), (0, band(kenney(IMPACT, "impactSoft_medium_", n + 3, 5, 0.12), lo=600), -4)), -15)


def skill_seethe(n):  # flaring up in flames
    return finish(mix((0, fab("MagicCast_FireBall2", n, 3, 0.5, 0.25), 0), (0, band(fab("MagicCast_Buff1", n, 4, 0.4, 0.2), hi=1500), -6),
                      (0.1, fab("MagicSpell_FireTrail", n + 2, 5, 0.4, 0.25), -8)), -16)


def skill_seethe_hit(n):  # a burn hiss
    return finish(mix((0, fab("MagicImpact_Fire", n + 3, 5, 0.3, 0.15), 0), (0.02, band(scifi("thrusterFire", n, 5, 0.3, 0.2), lo=2000), -6)), -16)


def skill_bail_out(n):  # a dash and a knuckle crack
    return finish(mix((0, band(fab("MagicWeapon_Thrust", n + 1, 4, 0.25, 0.1), lo=300), 0), (0, band(rpg(STEP, n + 1, 0.12), lo=300), -6),
                      (0, band(punch(n + 2, "medium", 0.08), lo=1500), -4)), -17)


def skill_elbow(n):  # a short swing into a chest thud
    return finish(mix((0, band(kenney(IMPACT, "impactSoft_heavy_", n, 5, 0.2), hi=2500), 0),
                      (0, band(fab("MagicWeapon_Swing", n + 1, 4, 0.15, 0.06), lo=700), -6)), -16)


def skill_elbow_hit(n):  # bone on bone
    return finish(mix((0, punch(n, "heavy", 0.18), 0), (0, band(kenney(IMPACT, "impactWood_heavy_", n + 2, 5, 0.15), lo=700), -5),
                      (0, sub(70, 0.08), -10)), -14)


def skill_not_mad(n):  # a pot lid raised: a metal ring under a barrier swell
    return finish(mix((0, rpg(POT, n, 0.6, 0.3), 0), (0, band(fab("MagicCast_CreatingBarrier1", n, 3, 0.6, 0.3), hi=3000), -8)), -16)


def skill_stare_down(n):  # two chest slaps and a low taunting shout
    return finish(mix((0, band(kenney(IMPACT, "impactSoft_heavy_", n, 5, 0.16), hi=2500), 0),
                      (0.22, band(kenney(IMPACT, "impactSoft_heavy_", n + 1, 5, 0.16), hi=2500), -1),
                      (0.05, band(pitch(fab("MagicSpell_Shout", n, 5, 0.6, 0.3), 1.15), hi=1200), -8)), -16)


def skill_quick_meeting(n):  # a pot lid clank and boiling bubbles
    return finish(mix((0, rpg(POT, n + 1, 0.3), 0), (0.05, scifi("slime", n, 2, 0.5, 0.3), -5),
                      (0.1, band(fab("MagicCast_Liquid_Water", n + 2, 5, 0.4, 0.3), hi=2500), -9)), -16)


def skill_quick_meeting_hit(n):  # a scald hiss
    return finish(mix((0, band(scifi("thrusterFire", n + 1, 5, 0.35, 0.2), lo=1800), 0), (0, band(fab("MagicImpact_Fire", n, 5, 0.25, 0.15), hi=2500), -5)), -16)


def skill_unplug(n):  # a parry stance: leather tightening and a click into place
    return finish(mix((0, rpg(["metalClick"], n, 0.12), 0), (0, rpg(LEATHER, n + 1, 0.2), -3),
                      (0.05, rpg(["beltHandle1", "beltHandle2"], n, 0.15), -6)), -17)


def skill_unplug_hit(n):  # the counter: a knuckle crack and a tight twist
    return finish(mix((0, band(punch(n + 3, "medium", 0.1), lo=1200), 0), (0.03, rpg(LEATHER, n, 0.18), -4),
                      (0.05, band(pitch(rpg(CREAK, n + 1, 0.15), 0.8), lo=900), -9)), -15)


def skill_triple_combo(n):  # left, right, left: three quick slaps
    return finish(mix((0, rapid(n, 3, 0.11), 0), (0, band(rpg(CLOTH, n, 0.35), lo=1200), -10)), -15)


def skill_open_mic(n):  # a shout resonance and the air it pushes
    return finish(mix((0, fab("MagicSpell_Shout", n + 1, 5, 0.7, 0.35), 0), (0.05, band(scifi("lowFrequency_explosion", n, 2, 0.5, 0.3), hi=500), -6)), -15)


def skill_open_mic_hit(n):  # a dull blast of air
    return finish(mix((0, band(fab("MagicBeam_HoldStrongBlast", n, 4, 0.3, 0.2), hi=2500), 0),
                      (0, band(kenney(IMPACT, "impactSoft_heavy_", n + 2, 5, 0.15), hi=1500), -4)), -15)


def skill_calculus_drop(n):  # a brick smashed down: a crack, a heavy thud, crunching debris
    return finish(mix((0, kenney(IMPACT, "impactMining_", n, 5, 0.3), 0), (0, band(kenney(IMPACT, "impactWood_heavy_", n + 1, 5, 0.2), hi=1500), -4),
                      (0.02, band(scifi("explosionCrunch", n + 1, 5, 0.3, 0.2), hi=1200), -8)), -14)


def skill_calculus_drop_hit(n):  # brick cracking on a head and grit falling
    return finish(mix((0, kenney(IMPACT, "impactMining_", n + 2, 5, 0.25), 0), (0, punch(n, "heavy", 0.12), -5),
                      (0.03, band(kenney(IMPACT, "impactGlass_light_", n, 5, 0.25), lo=2500), -12)), -14)


def skill_flyer_trap(n):  # a spring trap set and a rustle of paper
    return finish(mix((0, rpg(["metalLatch"], n, 0.2), 0), (0.06, band(rpg(PAPER, n, 0.25), lo=800), -5)), -17)


def skill_flyer_trap_hit(n):  # the trap snapping: two clicks
    return finish(mix((0, rpg(["metalClick"], n, 0.1), 0), (0.07, rpg(["metalLatch"], n, 0.15), -2),
                      (0, band(punch(n + 4, "medium", 0.1), hi=2500), -8)), -15)


def skill_hose_down(n):  # a nozzle opened into high-pressure spray
    return finish(mix((0, rpg(["metalClick"], n, 0.08), -3), (0.03, fab("MagicBeam_Water1", n, 4, 0.6, 0.3), 0),
                      (0.05, band(fab("MagicCast_Liquid_Water", n, 5, 0.5, 0.3), lo=2000), -8)), -16)


def skill_hose_down_hit(n):  # a water jet slapping and splashing
    return finish(mix((0, fab("MagicImpact_Water", n + 2, 5, 0.35, 0.2), 0), (0.02, band(fab("MagicBeam_Water2", n, 4, 0.35, 0.25), lo=1500), -6)), -15)


def skill_charged_ram(n):  # two heavy stamps and a low charge
    return finish(mix((0, band(rpg(STEP, n + 2, 0.15), hi=2000), 0), (0.18, band(rpg(STEP, n + 3, 0.15), hi=2000), 0),
                      (0.1, band(fab("MagicSpell_EarthQuake", n + 1, 6, 0.5, 0.3), hi=600), -5), (0.18, sub(45, 0.2), -6)), -15)


def skill_charged_ram_hit(n):  # the heaviest body blow in the game
    return finish(mix((0, punch(n + 2, "heavy", 0.25), 0), (0, band(kenney(IMPACT, "impactPlate_heavy_", n, 5, 0.25), hi=2000), -6),
                      (0, sub(42, 0.22), -3), (0, band(scifi("lowFrequency_explosion", n + 1, 2, 0.4, 0.25), hi=300), -6)), -12)


def skill_brace(n):  # planting the feet and tensing up
    return finish(mix((0, band(rpg(STEP, n, 0.12), hi=2500), 0), (0.06, rpg(BELT, n + 1, 0.15), -3),
                      (0.05, band(fab("MagicCast_Buff2", n, 3, 0.45, 0.25), hi=1200), -8)), -17)


def skill_cola_shake(n):  # the tab cracked and an icy hiss
    return finish(mix((0, rpg(["metalClick"], n, 0.08), 0),
                      (0.02, band(fab("MagicCast_Liquid_Water", n + 4, 5, 0.4, 0.3), lo=2500), -4),
                      (0.05, band(fab("MagicCast_IceCrystal1", n, 3, 0.35, 0.25), lo=2000), -10)), -16)


def skill_cola_shake_hit(n):  # ice cracking, frost crackle and fizz
    return finish(mix((0, fab("MagicImpact_Crystal", n + 3, 4, 0.35, 0.2), 0), (0.01, kenney(IMPACT, "impactGlass_light_", n + 1, 5, 0.2), -6),
                      (0.04, band(scifi("slime", n + 1, 2, 0.3, 0.2), lo=2000), -10)), -15)


def skill_desk_fan(n):  # a switch and a motor spinning up with wind
    return finish(mix((0, rpg(["metalClick"], n, 0.08), 0), (0.05, band(scifi("engineCircular", n, 5, 0.6, 0.3), hi=3000), -4),
                      (0.1, fab("MagicChant_WindCharge", n, 3, 0.55, 0.3), -6)), -17)


def skill_desk_fan_hit(n):  # a gust slamming into a body
    return finish(mix((0, fab("MagicSpell_FreezingWind", n + 1, 4, 0.35, 0.25), 0), (0, band(kenney(IMPACT, "impactSoft_medium_", n + 4, 5, 0.12), hi=2000), -6)), -16)


def skill_stale_takeout(n):  # a soggy box thrown down
    return finish(mix((0, band(kenney(IMPACT, "impactSoft_medium_", n + 1, 5, 0.2), hi=2500), 0), (0.02, scifi("slime", n, 2, 0.45, 0.3), -3),
                      (0.1, band(fab("MagicCast_ThrowToxin", n + 3, 4, 0.4, 0.3), hi=3000), -9)), -16)


def skill_stale_takeout_hit(n):  # a wet slap, a squish and drips
    return finish(mix((0, band(kenney(IMPACT, "impactSoft_heavy_", n + 3, 5, 0.15), hi=2500), 0), (0, fab("MagicCast_Liquid_Gore", n + 1, 6, 0.35, 0.25), -3),
                      (0.12, band(fab("MagicImpact_Water", n + 4, 5, 0.25, 0.2), lo=1500), -10)), -15)


def skill_school_bag(n):  # a heavy bag dropped down and a zip
    return finish(mix((0, cut(load(f"{RPG}/dropLeather.ogg"), 0.3), 0), (0.02, rpg(BOOK, n, 0.25), -4),
                      (0.2, band(rpg(["drawKnife1", "drawKnife2", "drawKnife3"], n, 0.25), lo=1500), -9)), -16)


def skill_school_bag_hit(n):  # a soft heavy whack and a pencil case rattle
    return finish(mix((0, band(kenney(IMPACT, "impactSoft_heavy_", n + 4, 5, 0.18), hi=2200), 0), (0.01, rpg(BOOK, n + 1, 0.2), -4),
                      (0.05, rpg(COINS, n + 1, 0.3), -9)), -15)


def skill_flurry_rush(n):  # a string of quick punches with swings
    return finish(mix((0, rapid(n, 5, 0.085, "medium", -1.0), 0), (0, band(fab("MagicWeapon_Swing", n + 3, 4, 0.4, 0.15), lo=700), -10)), -14)


def skill_flurry_rush_hit(n):  # four quick hits landing
    return finish(mix((0, rapid(n + 1, 4, 0.09, "medium", -1.0), 0), (0, band(kenney(IMPACT, "impactSoft_medium_", n, 5, 0.4), hi=2000), -8)), -14)


def skill_cordon_tape(n):  # tape snapped taut, its stretch ringing on
    return finish(mix((0, rpg(BELT, n, 0.15), 0), (0.01, rpg(["metalLatch"], n, 0.1), -8),
                      (0.02, band(pitch(rpg(CREAK, n + 2, 0.3), 1.4), lo=800), -8)), -17)


def skill_cordon_tape_hit(n):  # a whip crack and a recoil
    return finish(mix((0, band(rpg(SLICE, n, 0.12), lo=1500), 0), (0, band(punch(n + 1, "medium", 0.08), lo=1500), -3),
                      (0.05, rpg(BELT, n + 1, 0.15), -6)), -15)


# Build UI (the workshop and its offers): short, dry, readable over combat; each told by the object the panel shows.
def ui(stem, n, count, length=None, fade_out=0.02):
    """Kenney Interface files are numbered from 001."""
    return cut(load(f"{INTERFACE}/{stem}_{n % count + 1:03d}.ogg"), length, fade_out)


def ui_pickup(n):  # a sticker peeled off
    return finish(mix((0, ui("scratch", n, 5, 0.2), 0), (0, band(rpg(CLOTH, n, 0.1), lo=2500), -10)), -19)


def ui_install(n):  # a part snapping into place
    return finish(mix((0, rpg(["metalClick"], n, 0.1), 0), (0, ui("click", n + 2, 5, 0.08), -4),
                      (0, band(kenney(IMPACT, "impactPlate_light_", n, 5, 0.1), hi=2500), -10)), -18)


def ui_reject(n):  # a low refusal
    return finish(mix((0, ui("error", n + 3, 8, 0.3), 0), (0, band(kenney(IMPACT, "impactSoft_medium_", n, 5, 0.12), hi=700), -6)), -19)


def ui_workshop_open(n):  # a roller shutter going up
    return finish(mix((0, band(pitch(rpg(CREAK, n, 0.35, 0.15), 1.25), lo=500), 0), (0.03, ui("maximize", n + 5, 9, 0.3), -5)), -21)


def ui_workshop_close(n):  # the shutter coming down
    return finish(mix((0, band(pitch(rpg(CREAK, n + 1, 0.35, 0.15), 0.85), lo=400), 0), (0.08, ui("minimize", n + 5, 9, 0.3), -5),
                      (0.25, band(kenney(IMPACT, "impactPlank_medium_", n, 5, 0.12), hi=2000), -8)), -21)


def ui_offer_deal(n):  # three cards dealt onto the table
    return finish(mix(*[(i * 0.07, band(rpg(PAPER, n + i, 0.12, 0.05), lo=900), -i) for i in range(3)]), -19)


def ui_gain(n):  # dropped into the bag
    return finish(mix((0, rpg(LEATHER, n, 0.2), -2), (0, ui("pluck", n + 1, 2, 0.25), 0)), -19)


def ui_add_point(n):  # a short rise with a spark
    return finish(mix((0, ui("toggle", n + 1, 4, 0.15), 0), (0.01, band(fab("MagicCast_Buff1", n, 4, 0.25, 0.15), lo=2500), -9)), -19)


def ui_hold_charge(n):  # holding to confirm: a rising charge
    return finish(mix((0, fab("MagicRiser_Charge1", n, 3, 0.9, 0.2), 0), (0, ui("maximize", n + 1, 9, 0.4), -8)), -21)


def ui_final_lock(n):  # the final build locked in: a heavy latch and a ringing seal
    return finish(mix((0, rpg(["metalLatch"], n, 0.2), 0), (0, ui("bong", n, 1, 0.6, 0.3), -3),
                      (0, band(scifi("impactMetal", n, 5, 0.3, 0.2), hi=2500), -6), (0, sub(55, 0.15), -10)), -16)


def ui_medal_fuse(n):  # two element stones clinking together and fusing with a shimmer
    return finish(mix((0, kenney(IMPACT, "impactMetal_light_", n, 5, 0.12), 0), (0.09, kenney(IMPACT, "impactMetal_light_", n + 2, 5, 0.12), -2),
                      (0.12, band(fab("MagicFade_Appear1", n, 3, 0.45, 0.25), lo=1500), -6)), -18)


# Ground-state loops (one quiet loop while a puddle, ice, fire, miasma or electric field is shown): the steady middle of a
# long recording, its tail crossfaded into its head so the loop has no seam.
def segment(relative, start, length):
    x = load(relative)
    return x[int(start * RATE):int((start + length) * RATE)]


def seamless(y, xfade=0.5):
    n = int(xfade * RATE)
    t = np.linspace(0, np.pi / 2, n)
    head = y[:n] * np.sin(t) + y[-n:] * np.cos(t)
    return np.concatenate([head, y[n:-n]])


def loop_finish(y, rms_db=-26.0):
    r = np.sqrt(np.mean(y ** 2)) or 1
    y = y * 10 ** (rms_db / 20) / r
    p = np.max(np.abs(y))
    return y * 0.5 / p if p > 0.5 else y


def ground_puddle(n):  # water lapping
    bed = band(segment(f"{MAGIC}/SW_MagicBeam_Water1_1.wav", 0.8, 3.5), hi=2200)
    lap = band(segment(f"{MAGIC}/SW_MagicCast_Liquid_Water_2.wav", 0.0, 1.0), lo=600, hi=4000)
    return loop_finish(seamless(mix((0, bed, 0), (0.9, lap, -10), (2.3, lap[::-1].copy(), -12))))


def ground_ice(n):  # a cold wind over ice with a faint crystal shimmer
    bed = band(segment(f"{MAGIC}/SW_MagicSpell_FreezingWind_2.wav", 1.0, 3.5), hi=3500)
    tink = band(fab("MagicImpact_Crystal", 1, 4, 0.4, 0.25), lo=3000)
    return loop_finish(seamless(mix((0, bed, 0), (0.8, tink, -18), (2.2, tink, -20))))


def ground_fire(n):  # a crackling fire with a low roar
    bed = segment(f"{MAGIC}/SW_MagicSpell_FireTrail_5.wav", 2.0, 3.5)
    roar = band(segment(f"{SCIFI}/thrusterFire_000.ogg", 1.0, 3.5), hi=900)
    return loop_finish(seamless(mix((0, bed, 0), (0, roar, -8))))


def ground_miasma(n):  # a low toxic drone with slow bubbles
    drone = band(segment(f"{SCIFI}/spaceEngineLow_000.ogg", 1.0, 3.5), hi=450)
    bubble = band(fab("MagicCast_Liquid_Gore", 2, 6, 0.6, 0.3), hi=2500)
    return loop_finish(seamless(mix((0, drone, 0), (0.4, bubble, -6), (1.7, scifi("slime", 1, 2, 0.5, 0.3), -9),
                                    (2.6, bubble[::-1].copy(), -8))))


def ground_field(n):  # an electric hum with crackle
    hum = band(segment(f"{MAGIC}/SW_MagicChant_LightningCharge_1.wav", 1.5, 3.5), lo=150, hi=6000)
    field = band(scifi("forceField", 0, 5, 0.9, 0.3), hi=3000)
    return loop_finish(seamless(mix((0, hum, 0), (0.6, field, -10), (2.0, field, -12))))


def on_frame(make):
    """A skill release or hit plays on its frame (AUDIO_AND_FEEL sync table: transient within 16 ms): the lead-in before
    the sound first reaches half its peak is trimmed to 5 ms."""
    def framed(n):
        y = make(n)
        a = max(0, int(np.argmax(np.abs(y) > 0.5 * (np.max(np.abs(y)) or 1))) - int(0.005 * RATE))
        y = y[a:].copy()
        y[: int(0.002 * RATE)] *= np.linspace(0, 1, int(0.002 * RATE))
        return y
    return framed


SKILLS = ["pants_down", "crotch_grab", "snark_bolt", "seethe", "bail_out", "elbow", "not_mad", "stare_down",
          "quick_meeting", "unplug", "triple_combo", "open_mic", "calculus_drop", "flyer_trap", "hose_down",
          "charged_ram", "brace", "cola_shake", "desk_fan", "stale_takeout", "school_bag", "flurry_rush", "cordon_tape"]
SKILL_HITS = ["pants_down", "crotch_grab", "snark_bolt", "seethe", "elbow", "quick_meeting", "unplug", "open_mic",
              "calculus_drop", "flyer_trap", "hose_down", "charged_ram", "cola_shake", "desk_fan", "stale_takeout",
              "school_bag", "flurry_rush", "cordon_tape"]


SOUNDS = {"hit_basic": hit_basic, "hit_finisher": hit_finisher, "hit_electric": hit_electric, "hit_fire": hit_fire,
          "react_shatter": react_shatter, "ui_choose": ui_choose,
          "hit_water": hit_water, "hit_magic": hit_magic, "swing_light": swing_light, "swing_finisher": swing_finisher,
          "cast_charge": cast_charge, "cast_release": cast_release, "body_fall": body_fall,
          "state_burn": state_burn, "state_wet": state_wet, "state_shock": state_shock, "state_chill": state_chill,
          "state_rot": state_rot, "react_fog": react_fog, "react_conduct": react_conduct, "react_overload": react_overload,
          "react_melt": react_melt, "react_methane": react_methane, "react_freeze": react_freeze,
          "react_ferment": react_ferment, "react_corrode": react_corrode, "react_frostbite": react_frostbite,
          "ui_pickup": ui_pickup, "ui_install": ui_install, "ui_reject": ui_reject, "ui_workshop_open": ui_workshop_open,
          "ui_workshop_close": ui_workshop_close, "ui_offer_deal": ui_offer_deal, "ui_gain": ui_gain,
          "ui_add_point": ui_add_point, "ui_hold_charge": ui_hold_charge, "ui_final_lock": ui_final_lock,
          "ui_medal_fuse": ui_medal_fuse,
          "ground_puddle": (ground_puddle, 1), "ground_ice": (ground_ice, 1), "ground_fire": (ground_fire, 1),
          "ground_miasma": (ground_miasma, 1), "ground_field": (ground_field, 1)}
TAKES = 3
# Peaky sounds the loudness pass could not raise to their mix level without clipping (a click or snap far above the
# body): a gentle soft limiter lowers the peaks so the playback volume can reach the level (about 4-6 dB).
DENSE = {"ui_install", "ui_offer_deal", "ui_gain", "ui_final_lock", "skill_crotch_grab", "skill_crotch_grab_hit", "skill_unplug",
         "skill_flyer_trap", "skill_flyer_trap_hit", "skill_brace", "skill_cola_shake", "skill_desk_fan", "skill_desk_fan_hit", "skill_stale_takeout"}


def soft_limit(y, drive=2.5, peak_db=-1.0):
    p = np.max(np.abs(y)) or 1
    z = np.tanh(drive * y / p) / np.tanh(drive)
    return z * 10 ** (peak_db / 20)
# Skill sounds keep two takes each (the import table's per-skill count).
SOUNDS.update({f"skill_{name}": (on_frame(globals()[f"skill_{name}"]), 2) for name in SKILLS})
SOUNDS.update({f"skill_{name}_hit": (on_frame(globals()[f"skill_{name}_hit"]), 2) for name in SKILL_HITS})


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--only", default="", help="write only the sounds whose name contains this text")
    parser.add_argument("--inputs", action="store_true", help="list every input file the recipes read, write nothing")
    args = parser.parse_args()
    for name, entry in SOUNDS.items():
        make, takes = entry if isinstance(entry, tuple) else (entry, TAKES)
        if args.only and args.only not in name:
            continue
        rng = random.Random(name)
        for n in range(takes):
            ratio = rng.uniform(0.97, 1.03)
            # A loop keeps its own pitch: resampling is not circular and would open a seam.
            samples = make(n) if name.startswith("ground_") else pitch(make(n), ratio)
            if name in DENSE:
                samples = soft_limit(samples)
            if not args.inputs:
                soundfile.write(OUT / f"{name}_{n + 1}.wav", samples.astype(np.float64), RATE, subtype="PCM_16")
                print(f"{name}_{n + 1:<14} {len(samples) / RATE * 1000:6.0f} ms")
    if args.inputs:
        print("\n".join(sorted(READ)))


if __name__ == "__main__":
    main()

"""Exact approved content baseline, world contract, and world-element function tests on synthetic geometry.

Not a native or play-balance test, and nothing here pins the current map layout.
"""
from copy import deepcopy
from decimal import Decimal
from fractions import Fraction
import json
import math
from pathlib import Path
import re
import shutil
import tempfile
import unittest
from unittest.mock import patch

from CatalogTools import encode
import DemoContent as content
import DemoWorld as world
import SyncDemoContentData as content_sync
import SyncDemoWorldData as world_sync

# Most tests here recompile a fixture whose world bytes they did not change. The worker process keeps one reuse scope
# open for its lifetime, so an identical world payload is validated and bound once per process instead of per compile.
_WORLD_REUSE = world.reuse_binding_reports()
_WORLD_REUSE.__enter__()


def amount(value):
    return content.quantity(value)


# #148: memes that rule text no longer uses; entry names that contain them (他急了, 无影大比兜) are kept.
RETIRED_RULE_TERMS = ("精神状态", "电量", "力大砖飞", "阴阳怪气", "嘴硬", "心大", "手速", "润速", "整活频率", "物理超度",
                      "精神攻击", "心理防线", "大比兜", "提裤子", "捂裆", "CPU 干烧", "瞅你咋地", "腿软", "上头", "蹲到了",
                      "破防了", "免死金牌", "回暖", "回电")


def r(text):
    return Fraction(text)


def author_units(value):
    """A projected effect in author units (points/hp/mp, seconds, ratios, cm), without its op and opcode."""
    if type(value) in (int, str, bool, tuple):
        return value
    if "unit" in value:
        return amount(value)
    return {key: author_units(child) for key, child in value.items() if key not in ("op", "opcode")}


# #198 actives (CONTENT_AND_BALANCE, issue #198 approval 2026-11-10): one value set each at G=1, in S order.
ACTIVE_IDS = ("active_elbow", "active_pants_down", "active_crotch_grab", "active_unplug",
              "active_snark_bolt", "active_seethe", "active_quick_meeting", "active_hose_down", "active_zap",
              "active_bail_out", "active_charged_ram", "active_not_mad", "active_stare_down",
              "active_brace", "active_triple_combo", "active_calculus_drop", "active_flyer_trap",
              "active_open_mic", "active_cola_shake", "active_desk_fan", "active_stale_takeout",
              "active_school_bag", "active_flurry_rush", "active_cordon_tape")
# (target mode, range cm or None, windup s, momentum cost, cooldown s, in the opening pool).
ACTIVE_FRAMES = {
    "active_elbow": ("Unit", 220, r(".15"), 25, r("4.2"), True),
    "active_pants_down": ("DirectionLine", 440, r(".14"), 25, 6, True),
    "active_crotch_grab": ("DirectionDash", None, r(".12"), 30, r("6.3"), False),
    "active_unplug": ("Self", None, 0, 20, r("6.3"), False),
    "active_snark_bolt": ("DirectionProjectile", 850, r(".18"), 25, r("4.2"), True),
    "active_seethe": ("Self", None, r(".25"), 30, r("4.9"), True),
    "active_quick_meeting": ("GroundPoint", 650, r(".25"), 35, r("5.6"), True),
    "active_hose_down": ("DirectionLine", 600, r(".2"), 25, r("4.9"), True),
    "active_zap": ("Unit", 650, r(".25"), 30, r("4.9"), True),
    "active_bail_out": ("DirectionDash", None, 0, 20, r("4.9"), True),
    "active_charged_ram": ("DirectionDash", None, r(".1"), 30, r("6.3"), True),
    "active_not_mad": ("Self", None, 0, 25, 7, True),
    "active_stare_down": ("Self", None, r(".2"), 30, r("8.4"), False),
    "active_brace": ("Self", None, r(".1"), 25, r("8.4"), False),
    "active_triple_combo": ("Self", None, 0, 20, r("6.3"), True),
    "active_calculus_drop": ("GroundPoint", 650, r(".25"), 35, r("5.6"), True),
    "active_flyer_trap": ("GroundPoint", 550, r(".15"), 30, r("6.3"), False),
    "active_open_mic": ("Self", None, r(".15"), 25, r("5.6"), True),
    "active_cola_shake": ("DirectionCone", 400, r(".15"), 25, r("4.2"), True),
    "active_desk_fan": ("GroundPoint", 500, r(".2"), 35, 7, False),
    "active_stale_takeout": ("DirectionProjectile", 700, r(".2"), 30, r("5.6"), False),
    "active_school_bag": ("GroundPoint", 450, r(".2"), 30, r("7.7"), True),
    "active_flurry_rush": ("Self", None, 0, 25, 7, False),
    "active_cordon_tape": ("GroundPoint", 560, r(".16"), 30, r("6.8"), False),
}


def magical(base, ap, hp=0, ad=0, **extra):
    return ("damage", {"ad_ratio": r(ad), "ap_ratio": r(ap), "base": base, "damage_type": "Magical", "max_hp_ratio": r(hp), **extra})


def adaptive(base, ratio, damage_type="Magical"):
    return ("damage", {"ad_ratio": 0, "adaptive_ratio": r(ratio), "ap_ratio": 0, "base": base, "damage_type": damage_type, "max_hp_ratio": 0})


def physical(base, ad, ap=0, hp=0, **extra):
    return ("damage", {"ad_ratio": r(ad), "ap_ratio": r(ap), "base": base, "damage_type": "Physical", "max_hp_ratio": r(hp), **extra})


# Every active's effects in authored order and author units; "base" grows with the owner's level at runtime.
ACTIVE_EFFECTS = {
    "active_elbow": [physical(65, ".85"), ("knockback", {"distance": 300, "duration": r(".25")})],
    "active_pants_down": [physical(55, ".7", ".5"), ("root", {"duration": r("1.1")}), ("breach", {"stacks": 3}),
                          ("grab_line", {"extra_duration": r(".3")})],
    "active_crotch_grab": [
        ("dash", {"distance": 280, "duration": r(".16")}), ("dash_contact", {"targets": "First"}),
        adaptive(72, ".95", "Physical"), ("slow", {"duration": r("1.5"), "ratio": r(".35")}),
        ("attack_slow", {"duration": r("1.5"), "ratio": r(".35")}),
        ("backstab_lunge", {"angle": 150, "stun_duration": r("1.1")})],
    "active_unplug": [
        ("parry_stance", {"ad_ratio": r(".75"), "base": 65, "buff_duration": 3, "combo_charges": 1, "cut": r(".65"),
                          "disarm_duration": r("1.3"), "duration": r(".45"), "miss_refund": r(".3"), "radius": 280,
                          "resist_ratio": r(".65"), "stats": {"AttackSpeedBonusRatio": r(".35")}})],
    "active_snark_bolt": [
        adaptive(85, ".9", "Physical"), ("slow", {"duration": r("1.5"), "ratio": r(".25")}),
        ("slipper_pickup", {"buff_duration": 3, "duration": 3, "radius": 110, "refund_ratio": r(".55"),
                            "stats": {"AttackSpeedBonusRatio": r(".2"), "MoveSpeedBonusRatio": r(".15")}})],
    "active_seethe": [("self_area", {"radius": 300}), magical(60, ".6", ".02"), ("burn", {"stacks": 2})],
    "active_quick_meeting": [
        ("zone", {"count": 3, "delay": 0, "duration": 3, "interval": 1, "radius": 240}),
        ("zone_damage", {"ad_ratio": 0, "ap_ratio": r(".25"), "base": 30, "damage_type": "Magical"}),
        ("zone_slow", {"duration": r(".5"), "interval": 1, "ratio": r(".2")}),
        ("zone_burn", {"interval": 1, "stacks": 1})],
    "active_hose_down": [magical(55, ".55"), ("knockback", {"distance": 180, "duration": r(".15")}), ("wet", {})],
    "active_zap": [magical(60, ".65"), ("electrify", {}),
                   ("chain_bounce", {"count": 2, "interval": r(".15"), "radius": 350, "ratio": r(".7")})],
    "active_bail_out": [
        ("dash", {"distance": 350, "duration": r(".2")}),
        ("empower_basic", {"ad_ratio": r(".75"), "ap_ratio": r(".45"), "base": 55, "damage_type": "Physical", "duration": 3})],
    "active_charged_ram": [
        ("dash", {"distance": 460, "duration": r(".28")}),
        ("shield", {"ad_ratio": r(".4"), "ap_ratio": 0, "base": 70, "duration": r("2.5"), "max_hp_ratio": r(".06"), "resist_ratio": r(".55")}),
        ("dash_contact", {"targets": "Every"}), physical(60, ".6", ".4", ".03", resist_ratio=r(".45")),
        ("knockback", {"distance": 180, "duration": r(".16")})],
    "active_not_mad": [
        ("shield", {"ad_ratio": r(".65"), "ap_ratio": r(".6"), "base": 95, "duration": 3, "max_hp_ratio": r(".08"), "resist_ratio": r(".5")}),
        ("dispel_slow", {}), ("dispel_root", {})],
    "active_stare_down": [
        ("taunt", {"duration": 1, "radius": 300}),
        ("timed_stat_bonus", {"duration": 3, "stats": {"ArmorFlat": 14, "MagicResistFlat": 14}})],
    "active_brace": [
        ("brace", {"base": 50, "duration": 3, "limit": 120, "radius": 300, "ratio": r(".2"), "record_ratio": r(".6")})],
    "active_triple_combo": [
        ("empower_basic", {"ad_ratio": r(".28"), "ap_ratio": r(".18"), "base": 24, "charges": 3, "damage_type": "Physical", "duration": 5,
                           "stats": {"AttackSpeedBonusRatio": r(".35")}})],
    "active_calculus_drop": [
        ("ground_impact", {"delay": r(".55"), "inner_radius": 80, "outer_ratio": r(".6"), "radius": 200}), adaptive(110, "1")],
    "active_flyer_trap": [
        ("contact_trap", {"delay": r(".45"), "duration": 6, "radius": 180, "trigger_radius": 100}), magical(100, ".75"),
        ("slow", {"duration": r("1.5"), "ratio": r(".3")})],
    "active_open_mic": [
        ("self_area", {"radius": 340}),
        magical(65, ".5", ".03", ".55", monster_multiplier=r("1.6")),
        ("knockback", {"distance": 160, "duration": r(".15")}),
        ("shield", {"ad_ratio": 0, "ap_ratio": r(".45"), "base": 75, "duration": 3, "max_hp_ratio": r(".06"), "resist_ratio": r(".6")}),
        ("warcry_ring", {"shield_per_hit_ratio": r(".25")})],
    "active_cola_shake": [("channel_cone", {"count": 1, "interval": r(".25")}), magical(55, ".5"), ("chill", {}), ("knockback", {"distance": 100, "duration": r(".12")})],
    "active_desk_fan": [("fan", {"distance": 60, "duration": 4, "interval": r(".5"), "length": 600, "width": 200})],
    "active_stale_takeout": [magical(75, ".7", ad=".7"), ("rot", {}), ("ground_state", {"kind": "miasma"}), ("knockback", {"distance": 100, "duration": r(".12")})],
    "active_school_bag": [("bag", {"count": 1, "duration": 4, "hp_base": 150, "hp_per_level": 20, "radius": 200, "size": 40}),
                          magical(85, ".65", 0, ".7", monster_multiplier=r("1.4"))],
    "active_flurry_rush": [
        ("frenzy_waves", {"adaptive_ratio": r(".38"), "base": 32, "distance": 420, "duration": 4, "radius": 65, "speed": 1900,
                          "stats": {"AttackSpeedBonusRatio": r(".35"), "MoveSpeedBonusRatio": r(".18")}})],
    "active_cordon_tape": [
        ("cordon_line", {"ally_amp": r(".25"), "distance": 180, "duration": 5, "length": 500, "vulnerable_duration": r("2.5"),
                         "vulnerable_ratio": r(".15"), "width": 70}),
        adaptive(65, ".7"), ("slow", {"duration": r("1.5"), "ratio": r(".3")})],
}
# #198 five point-bought stats: native id and the bonuses of one point.
STAT_POINTS = {
    "stat_physique": (27, {"MaxHPFlat": 70, "HPRegenFlat": r(".08")}),
    "stat_thick_skin": (28, {"ArmorFlat": r("3.5"), "MagicResistFlat": r("3.5")}),
    "stat_strength": (29, {"ADFlat": r("3.5"), "CritChanceBonusRatio": r(".006")}),
    "stat_brains": (30, {"APFlat": 6, "CooldownHasteFlat": 1, "MomentumRegenFlat": r(".06")}),
    "stat_agility": (31, {"AttackSpeedBonusRatio": r(".02"), "MoveSpeedBonusRatio": r(".005")}),
}


# Spec §7 P2 cards in native ID order: name, form and type.
CARD_FRAMES = {
    "card_third_breath": ("第三拳回口气", "body", "trigger"),
    "card_punch_think": ("边打边想", "body", "trigger"),
    "card_solid_punch": ("拳拳到肉", "body", "trigger"),
    "card_third_shove": ("第三拳滚蛋", "body", "trigger"),
    "card_long_arm": ("隔空抽你", "body", "modify"),
    "card_both_hands": ("左右开弓", "body", "modify"),
    "card_wide_swing": ("抡圆了打", "body", "modify"),
    "card_fist_skill": ("拳里带招", "body", "modify"),
    "card_quick_hands": ("手快有手慢无", "body", "convert"),
    "card_slow_craft": ("慢工出细活", "body", "convert"),
    "card_scholar_punch": ("读书人打架", "body", "convert"),
    "card_heated_up": ("越打越上头", "body", "grow"),
    "card_muscle_memory": ("熟能生巧", "body", "grow"),
    "card_breach_fist": ("越揍越不经打", "body", "grow"),
    "card_next_up": ("打完换下一招", "skill", "trigger"),
    "card_pay_back": ("一招回本", "skill", "trigger"),
    "card_quick_takedown": ("收人头不用等", "skill", "trigger"),
    "card_setup_line": ("先垫一句", "skill", "trigger"),
    "card_pin_amp": ("按住了就放大", "skill", "trigger"),
    "card_encore": ("再来一遍", "skill", "modify"),
    "card_crowd_hit": ("一群人一起挨", "skill", "modify"),
    "card_again_now": ("再来一下别喘", "skill", "modify"),
    "card_big_one": ("攒够一顿狠的", "skill", "modify"),
    "card_long_reach": ("站远点照样抽你", "skill", "modify"),
    "card_hold_longer": ("按住多揍一会", "skill", "modify"),
    "card_blood_price": ("拿命换", "skill", "convert"),
    "card_lend_fist": ("拳头借给技能", "skill", "convert"),
    "card_held_breath": ("憋着的气", "skill", "convert"),
    "card_hurt_heal": ("打疼了我回血", "skill", "convert"),
    "card_held_shot": ("憋着一发狠的", "skill", "grow"),
    "card_practice": ("越用越顺手", "skill", "grow"),
    "card_combo_chain": ("一招接一招", "skill", "grow"),
    "card_blast_reload": ("炸了接着放", "skill", "element"),
    "card_bump_reset": ("撞着就刷新", "dash", "trigger"),
    "card_bump_again": ("撞着了再撞", "dash", "trigger"),
    "card_dash_punch": ("蹿过去抡一拳", "dash", "trigger"),
    "card_double_dash": ("两段冲刺", "dash", "modify"),
    "card_pass_through": ("穿人而过", "dash", "modify"),
    "card_head_down": ("闷头撞到底", "dash", "modify"),
    "card_paid_dash": ("冲刺耗劲", "dash", "convert"),
    "card_ice_trail": ("一路结冰", "dash", "element"),
    "card_move_turf": ("挪地盘", "dash", "element"),
    "card_spiky_hide": ("打我也硌手", "body", "trigger"),
    "card_takedown": ("人头刷新", "body", "trigger"),
    "card_hang_on": ("死扛", "body", "trigger"),
    "card_reaction_refund": ("反应回本", "body", "trigger"),
    "card_wave_after": ("一波接一波", "body", "trigger"),
    "card_shell_burst": ("壳碎了也崩你", "body", "trigger"),
    "card_hard_skin": ("挨揍起硬皮", "body", "trigger"),
    "card_ball_kick": ("拿你当球踢", "body", "trigger"),
    "card_skill_crit": ("技能也能暴击", "body", "modify"),
    "card_skill_drain": ("吸血对技能生效", "body", "modify"),
    "card_double_stall": ("双摊位", "body", "modify"),
    "card_thick_hide": ("皮糙肉厚", "body", "convert"),
    "card_overfed": ("吃撑了", "body", "convert"),
    "card_berserk": ("残血狂暴", "body", "convert"),
    "card_quick_brain": ("脑子转得快", "body", "convert"),
    "card_punch_heal": ("揍人续命", "body", "convert"),
    "card_run_hit": ("跑起来打", "body", "convert"),
    "card_grow_fat": ("越吃越壮", "body", "grow"),
    "card_kill_streak": ("越杀越猛", "body", "grow"),
    "card_tough_skin": ("越挨越硬", "body", "grow"),
    "card_charge_up": ("攒个大的", "body", "grow"),
    "card_turf_growth": ("地头蛇", "body", "grow"),
    "card_tally": ("揍一下记一笔", "body", "grow"),
    "card_live_wire": ("浑身带电", "body", "element"),
    "card_long_soak": ("久泡", "body", "element"),
    "card_louder_blast": ("炸得更响", "body", "element"),
    "card_endless_burn": ("烧不完", "body", "element"),
    "card_burn_guard": ("烧着还挺扛揍", "body", "element"),
    # The element medals.
    "medal_fire": ("烫勋章", "medal", "element"),
    "medal_water": ("湿勋章", "medal", "element"),
    "medal_electric": ("麻勋章", "medal", "element"),
    "medal_chill": ("冻勋章", "medal", "element"),
    "medal_rot": ("馊勋章", "medal", "element"),
    # Issue #345 cross-axis synergy unlock cards.
    "card_brain_speed": ("脑转手快", "body", "convert"),
    "card_spellblade_echo": ("咒刃回响", "body", "trigger"),
    "card_heavy_mallet": ("千斤重锤", "body", "convert"),
    "card_let_bullets_fly": ("让子弹飞", "body", "modify"),
    "card_eagle_stance": ("鹰眼架枪", "body", "modify"),
    "card_runaway_train": ("失控列车", "dash", "convert"),
    "card_skate_blade": ("冰刀割裂", "body", "trigger"),
    "card_tonnage_crush": ("吨位碾压", "body", "convert"),
    "card_hulking_presence": ("大块头有威慑", "body", "convert"),
    "card_iron_hedgehog": ("铁刺猬", "body", "convert"),
    "card_reactive_plating": ("反应装甲", "body", "trigger"),
    "card_desperado": ("亡命之徒", "body", "modify"),
    "card_fuel_the_fire": ("火上浇油", "skill", "trigger"),
    "card_rogue_works": ("违章工事", "body", "modify"),
    "card_martial_scholar": ("文武双全", "body", "trigger"),
}


class ApprovedDemoData(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.author = content.compile_author()
        cls.catalog = cls.author.catalog
        cls.world = world.compile_world(author=cls.author)
        cls.root_bundle = content.compile_content()
        cls.data = cls.catalog.data
        cls.geometry = world.Geometry(cls.world.world)
        cls.anchors = {row["id"]: row for row in cls.world.world["anchors"]}

    def test_showdown_rejects_impossible_objective_and_life_clocks(self):
        for key, value in (("SHOWDOWN.RespawnCutoffSec", 240000),
                           ("SHOWDOWN.ClosureStartsSec", 240000),
                           ("CAPTURE.GuardSec", 240000),
                           ("SHOWDOWN.RespawnLimit", 3)):
            with self.subTest(key=key):
                data = json.loads(self.root_bundle.outputs[content.CONTENT_OUTPUT])
                if key == "SHOWDOWN.RespawnLimit":
                    data["parameters"][key] = value
                else:
                    data["parameters"][key]["ms"] = value
                with self.assertRaisesRegex(content.ContentError, "InvalidTiming.*SHOWDOWN"):
                    content.validate_numbers(data)

    def test_approval_and_complete_selected_directory(self):
        approval = self.data["identity"]["approval"]
        self.assertEqual(approval["status"], "approved")
        # A content-only approval does not rewrite the independently approved world geometry.
        self.assertEqual(self.world.world["approval"]["status"], "approved")
        for text in ("2026-11-02", "D01-D03", "2026-11-04", "#82", "playtest", "not a balance"):
            self.assertIn(text, approval["decision"])
        self.assertNotIn("TEST ONLY", approval["decision"])
        self.assertIn("camera 2250", approval["decision"])
        for text in ("2026-11-07 movement -25%", "flagpole 180s", "vision/leash 1050", "#147 build v7",
                     "2026-11-08 #166 level 16/QWE", "#97 camp growth stages, Elite camp tier",
                     "2026-11-09 #144 equipment, gold and shops removed", "#225: PK +50%",
                     "2026-11-10 #198 v0.11 workshop growth (stats/points/single-level actives) approved: 全部按当前规范批准，进入运行数据"):
            self.assertIn(text, approval["decision"])
        self.assertEqual((len(self.data["abilities"]), len(self.data["stats"]), len(self.data["camps"]["monsters"])), (24, 5, 12))
        self.assertEqual([row["id"] for row in self.data["abilities"]], sorted(ACTIVE_IDS))
        self.assertFalse({"attributes", "opening_cards"} & set(self.data))
        self.assertEqual((self.data["identity"]["schema_version"], self.data["identity"]["handler_revision"],
                          self.data["identity"]["projection_revision"]), (8, 15, 11))
        self.assertIn("#292 P5 cards, vouchers, rest shop, pick tickets and chains approved", approval["decision"])
        self.assertIn("#287 P2 build cards and P4 elements, reactions, ground states and N1-N3 approved", approval["decision"])
        self.assertFalse({"components", "workshop_capacity"} & (set(self.data) | set(self.data["progression"])))
        self.assertEqual(self.data["character"]["basic_attack"]["interval_ms"], 1177)
        self.assertEqual(self.data["character"]["basic_attack"]["damage_type"], "Physical")
        self.assertEqual(self.data["character"]["basic_attack"]["ad_ratio"]["bp"], 10000)

    def test_every_approved_parameter_value_and_unit(self):
        expected = {}
        for stat, unit, values in (
            ("MaxHP", "hp", ("860", "125", "1", "20000")),
            ("AD", "points", ("70", "3.2", "0", "2000")),
            ("AP", "points", ("0", None, "0", "3000")),
            ("Armor", "points", ("18", "2", "0", "1000")),
            ("MagicResist", "points", ("18", "2", "0", "1000")),
            ("HPRegen", "hp/s", ("1.60", "0.12", "0", "12")),
        ):
            for suffix, value in zip(("Base", "PerLevel", "Min", "Max"), values):
                if value is not None:
                    expected[f"STAT.{stat}.{suffix}"] = (unit, value)
        for key, unit, value in (
            ("STAT.BaseAttackSpeed", "attacks/s", ".85"), ("STAT.AttackSpeedPerLevelRatio", "ratio", ".02"),
            ("STAT.AttackSpeed.Min", "attacks/s", ".30"), ("STAT.AttackSpeed.Max", "attacks/s", "5"),
            ("STAT.CritChance.Base", "ratio", "0"), ("STAT.CritMultiplier", "ratio", "2"),
            ("STAT.CooldownHaste.Base", "points", "0"), ("STAT.CooldownHaste.Max", "points", "400"),
            # Momentum, combo, overdrive and skill ranks (issue #287 spec §3.3-§3.6, approved with every P1 number).
            ("SKILL.GrowthPerLevel", "ratio", ".04"), ("SKILL.CooldownFloorSec", "ms", ".3"),
            ("MOMENTUM.Max", "momentum", "100"), ("MOMENTUM.Start", "momentum", "50"),
            ("MOMENTUM.CombatPerSec", "momentum/s", "5"), ("MOMENTUM.BasicPerRefInterval", "momentum", "10"),
            ("MOMENTUM.BasicRefIntervalSec", "ms", "1.334"), ("MOMENTUM.SkillHit", "momentum", "5"),
            ("MOMENTUM.Reaction", "momentum", "10"), ("MOMENTUM.PerfectDodge", "momentum", "20"),
            ("MOMENTUM.PerfectDodgeWindowSec", "ms", ".3"), ("MOMENTUM.DashDodgeWindowSec", "ms", ".12"),
            ("MOMENTUM.DashDodgeCutRatio", "ratio", ".5"), ("MOMENTUM.DashDodgeCooldownRefundRatio", "ratio", ".3"),
            ("MOMENTUM.IdleDelaySec", "ms", "5"),
            ("MOMENTUM.IdleReturnPerSec", "momentum/s", "10"), ("COMBO.ResetSec", "ms", "2.5"),
            ("AI.DodgeChance", "ratio", ".5"), ("AI.EnemyGroundPathCost", "ratio", "3"), ("AI.BreakoutChance", "ratio", "1"),
            ("AI.CardSkipChance", "ratio", ".3"),
            ("CHAIN.EndSec", "ms", "3"), ("CARDS.IdleCarrySec", "ms", "5"),
            ("DAMAGE.FactorTechCap", "ratio", "1000"), ("AIRDROP.Momentum", "momentum", "40"),
            ("UNIT.MoveSpeedCmPerSec", "cm/s", "240"), ("STAT.MoveSpeed.Min", "cm/s", "138.4"), ("STAT.MoveSpeed.Max", "cm/s", "540"),
            ("UNIT.CollisionRadiusCm", "cm", "40"), ("UNIT.BasicAttackRangeCm", "cm", "250"),
            ("STAT.AttackRange.Min", "cm", "80"), ("STAT.AttackRange.Max", "cm", "1000"),
            ("UNIT.BasicAttackWindupSec", "ms", ".180"), ("VISION.RadiusCm", "cm", "1120"),
            ("VISION.NeutralHeightCm", "cm", "35"), ("VISION.FullHeightCm", "cm", "120"),
            ("VISION.UphillRadiusScale", "ratio", ".75"), ("VISION.DownhillRadiusScale", "ratio", "1.25"),
            ("VISION.SightGraceSec", "ms", ".3"), ("CAPTURE.RadiusCm", "cm", "450"),
            ("CAPTURE.SpraySec", "ms", "3.5"), ("CAPTURE.GuardSec", "ms", "20"), ("CAPTURE.GuardCheckpointSec", "ms", "10"), ("CAPTURE.GuardResumeSec", "ms", "8"),
            ("SHOWDOWN.UnlockDelaySec", "ms", "10"), ("SHOWDOWN.DurationSec", "ms", "150"),
            ("SHOWDOWN.FirstRespawnSec", "ms", "18"), ("SHOWDOWN.SecondRespawnSec", "ms", "24"),
            ("SHOWDOWN.RespawnCutoffSec", "ms", "90"), ("SHOWDOWN.ClosureStartsSec", "ms", "75"), ("SHOWDOWN.KillGuardBonusSec", "ms", "4"),
            # Issue #287 spec §10 (P6): three 160 s field segments and 25 s rests.
            ("MATCH.FieldSegmentSec", "ms", "160"),
            ("REST.OpeningBuildSec", "ms", "35"), ("REST.BuildSec", "ms", "65"), ("REST.FinalBuildSec", "ms", "65"),
            ("REST.ReturnSec", "ms", "3"),
            ("REST.EntryReadyTimeoutSec", "ms", "30"),
            # The rest PK (R1 and R2): sign-up inside the build step, then draw+betting, ready, fight, and settlement+payout.
            ("REST.SignupSec", "ms", "10"), ("REST.DrawSec", "ms", "0"), ("REST.BettingSec", "ms", "10"),
            ("REST.PKReadySec", "ms", "3"), ("REST.PKMaxSec", "ms", "45"), ("REST.SettlementSec", "ms", "3"),
            ("REST.PayoutSec", "ms", "0"),
            ("PK.ArenaRadiusCm", "cm", "1800"), ("PK.RingMidRadiusCm", "cm", "600"), ("PK.RingFinalRadiusCm", "cm", "150"),
            ("PK.RingShrinkStartSec", "ms", "5"), ("PK.RingMidSec", "ms", "25"), ("PK.RingFinalSec", "ms", "38"),
            ("PK.RingPreviewSec", "ms", "5"), ("PK.RingDamageTickSec", "ms", ".25"),
            ("PK.RingDamageEarlyMaxHPRatioPerSec", "ratio", ".05"), ("PK.RingDamageMidMaxHPRatioPerSec", "ratio", ".12"),
            ("PK.RingDamageFinalMaxHPRatioPerSec", "ratio", ".25"), ("PK.TeamFormatRatio", "ratio", ".5"),
            ("PK.RewardMultiplier", "ratio", "1.5"),
            ("THROW.CooldownSec", "ms", "10"), ("THROW.TelegraphSec", "ms", ".8"), ("THROW.RadiusCm", "cm", "120"),
            ("THROW.RangeCm", "cm", "4000"), ("THROW.SlowRatio", "ratio", ".2"), ("THROW.SlowSec", "ms", "1"),
            ("THROW.ExposeSec", "ms", "2"),
            # Issue #287 spec §10-§11 (P6): the showdown ring, respawn, objectives, hotspots, affixes and hidden things.
            ("SHOWDOWN.RingRadiusCm", "cm", "1800"), ("SHOWDOWN.RingFinalRadiusCm", "cm", "600"), ("SHOWDOWN.RingPreviewSec", "ms", "10"),
            ("SHOWDOWN.RingDamageTickSec", "ms", "0.25"), ("SHOWDOWN.RingDamageEarlyMaxHPRatioPerSec", "ratio", "0.05"), ("SHOWDOWN.RingDamageLateMaxHPRatioPerSec", "ratio", "0.12"), ("SHOWDOWN.KillHealMaxHPRatio", "ratio", ".35"), ("SHOWDOWN.KillMomentum", "momentum", "50"),
            ("LIFE.RespawnBaseSec", "ms", "6"), ("LIFE.RespawnPerLevelSec", "ms", "0.5"), ("LIFE.SpawnChoiceSec", "ms", "6"),
            ("OBJECTIVE.StartSec", "ms", "100"), ("OBJECTIVE.AnnounceLeadSec", "ms", "10"), ("OBJECTIVE.FlagWindowSec", "ms", "30"),
            ("OBJECTIVE.FlagRadiusCm", "cm", "200"), ("OBJECTIVE.FlagSpraySec", "ms", "3"), ("OBJECTIVE.ArenaSec", "ms", "40"),
            ("OBJECTIVE.ArenaRadiusCm", "cm", "900"), ("DAILY.RainIntervalSec", "ms", "20"), ("DAILY.RainPuddleSec", "ms", "10"),
            ("DAILY.RainWetBonusSec", "ms", "2"), ("DAILY.ColdFreezeSec", "ms", "20"), ("DAILY.ColdChillBonusSec", "ms", "2"),
            ("DAILY.BlackoutVisionCut", "ratio", "0.25"), ("DAILY.BlackoutShockScale", "ratio", "2"), ("DAILY.TyphoonIntervalSec", "ms", "30"),
            ("DAILY.TyphoonPushCm", "cm", "150"), ("DAILY.HeatBurnTickScale", "ratio", "1.3"), ("DAILY.SalePriceScale", "ratio", "0.5"),
            ("DAILY.DeliveryExpireSec", "ms", "15"), ("DAILY.PatrolBossAppearSec", "ms", "120"), ("DAILY.ExamMonsterXPScale", "ratio", "1.3"),
            ("DAILY.ExamRespawnBonusSec", "ms", "3"), ("DAILY.GymDashCooldownCut", "ratio", "0.3"), ("AFFIX.Chance", "ratio", "0.3"), ("MEDAL.LevelChance", "ratio", "0.08"), ("MEDAL.CampLowChance", "ratio", "0"), ("MEDAL.CampHighChance", "ratio", "0.02"), ("MEDAL.CampEliteChance", "ratio", "0.08"), ("MEDAL.CampAncientChance", "ratio", "0.20"), ("MEDAL.BossChance", "ratio", "1"), ("MEDAL.EventChance", "ratio", "0.25"), ("MEDAL.Tier2ReactionBonusRatio", "ratio", "0.35"),
            ("AFFIX.OvertimeAttackSpeed", "ratio", "0.3"), ("AFFIX.TeambuildXPScale", "ratio", "1.5"),
            ("AFFIX.BonusHP", "ratio", "0.4"), ("AFFIX.LayoffAttack", "ratio", "0.3"), ("AFFIX.AuditTelegraphCut", "ratio", "0.3"),
            ("EXPLORE.LockerRadiusCm", "cm", "150"), ("EXPLORE.LockerChannelSec", "ms", "3"),
            ("EXPLORE.LockerEarliestSec", "ms", "30"), ("EXPLORE.LockerLatestSec", "ms", "420"), ("EXPLORE.LockerStaySec", "ms", "45"),
            ("EXPLORE.PeddlerEarliestSec", "ms", "120"), ("EXPLORE.PeddlerLatestSec", "ms", "400"), ("EXPLORE.PeddlerOffsetCm", "cm", "600"),
            ("EXPLORE.PeddlerHP", "hp", "1800"), ("EXPLORE.PeddlerMoveSpeed", "cm/s", "224"), ("EXPLORE.PeddlerStaySec", "ms", "60"),
            ("AI.ObjectiveEntryHPRatio", "ratio", "0.6"), ("AI.ObjectiveReachCm", "cm", "2500"),
            ("LIFE.ProtectionSec", "ms", "3"),
            ("REGEN.OutOfCombatDelaySec", "ms", "5"), ("REGEN.OutOfCombatMaxHPRatioPerSec", "ratio", ".015"),
            ("DORM.RadiusCm", "cm", "400"), ("DORM.OutOfCombatDelaySec", "ms", "3"),
            ("DORM.MaxHPRatioPerSec", "ratio", ".1"),
            ("CAMP.ResetArrivalRadiusCm", "cm", "100"),
            ("CAMP.ReturnDeadEndSec", "ms", "6"),
            ("CAMP.GrowthHPRatioPerStage", "ratio", ".12"), ("CAMP.GrowthAttackRatioPerStage", "ratio", ".08"),
            ("CAMP.GrowthResistPerStage", "points", "2"), ("CAMP.GrowthRewardXPRatioPerStage", "ratio", ".06"), ("CAMP.ClearHealMaxRatio", "ratio", ".15"), ("CAMP.ClearHealFlat", "hp", "60"), ("CAMP.ClearOrbRadiusCm", "cm", "95"), ("CAMP.ClearMomentum", "momentum", "25"), ("CAMP.ClearOrbSec", "ms", "20"),
            ("AIRDROP.ExpireSec", "ms", "30"), ("CAMPMARK.RevealSec", "ms", "11"),
            ("FIELD.SpawnSeparationCm", "cm", "2400"),
            ("FIELD.SpawnCaptureDistanceCm", "cm", "2000"),
            ("FIELD.SpawnPatrolMarginCm", "cm", "600"),
            ("FIELD.MapRecoverySeparationCm", "cm", "1400"),
            ("FIELD.MapRecoverySightCm", "cm", "1400"),
            ("AIRDROP.FieldClearanceCm", "cm", "200"),
            ("AIRDROP.FieldSeparationCm", "cm", "1800"),
            ("AIRDROP.AnnounceLeadSec", "ms", "15"), ("AIRDROP.PickupRadiusCm", "cm", "150"),
            ("AIRDROP.HealMaxHPRatio", "ratio", ".20"),
            ("AIRDROP.MoveSpeedRatio", "ratio", ".20"), ("AIRDROP.MoveSpeedSec", "ms", "6"),
            ("LEADER.RefreshSec", "ms", "10"), ("LEADER.SurvivalPointSec", "ms", "60"), ("LEADER.CircleRadiusCm", "cm", "1500"),
            ("LEADER.CircleMaxOffsetCm", "cm", "1000"),
            ("REWARD.AssistWindowSec", "ms", "15"), ("REWARD.AssistXPRatio", "ratio", ".5"), ("REWARD.LevelGapXPStep", "ratio", ".12"), ("REWARD.LevelGapXPMin", "ratio", ".4"), ("REWARD.LevelGapXPMax", "ratio", "1.6"), ("REWARD.KillMedalChance", "ratio", ".25"), ("GROWTH.BuildSafeWindowSec", "ms", "4"),
            ("AI.RetreatHPRatio", "ratio", ".25"), ("AI.ResumeFarmHPRatio", "ratio", ".45"),
            ("AI.HighCampEntryHPRatio", "ratio", ".80"), ("AI.HighCampReserveHPRatio", "ratio", ".35"),
            ("AI.LowCampReserveHPRatio", "ratio", ".15"), ("AI.CaptureReactionBudgetSec", "ms", "2"),
            ("AI.DecisionIntervalSec", "ms", ".25"), ("AI.RetryPolicy.InitialBackoffSec", "ms", ".25"),
            ("AI.RetryPolicy.MaxBackoffSec", "ms", ".50"), ("AI.ExploreRevisitIntervalSec", "ms", "20"),
            ("AI.EngagementBudgetSec", "ms", "40"),
            ("COMBAT.BreachPerStack", "points", "3"), ("COMBAT.BreachSec", "ms", "4"),
            ("COMBAT.BurnPerSec", "points", "4.5"), ("COMBAT.BurnAPRatioPerSec", "ratio", ".03"),
            ("COMBAT.BurnTickSec", "ms", ".5"), ("COMBAT.BurnSec", "ms", "3"),
            ("COMBAT.KeystonePhysiqueHealToShieldRatio", "ratio", ".5"),
            ("COMBAT.KeystonePhysiqueOutOfCombatCut", "ratio", ".5"),
            ("COMBAT.KeystonePhysiqueShieldMaxHPRatio", "ratio", ".12"),
            ("COMBAT.KeystonePhysiqueShieldDurationSec", "ms", "4"),
            ("COMBAT.KeystonePhysiqueIntervalSec", "ms", "4"),
            ("COMBAT.KeystoneAgilityDashCooldownCut", "ratio", ".4"),
            ("COMBAT.KeystoneThickSkinGuardPoints", "points", "45"),
            ("COMBAT.KeystoneThickSkinGuardCapRatio", "ratio", ".22"),
            ("COMBAT.KeystoneThickSkinIntervalSec", "ms", "5"), ("COMBAT.KeystoneThickSkinSteadySec", "ms", "1.5"), ("COMBAT.KeystoneThickSkinKnockbackRatio", "ratio", ".5"),
            ("COMBAT.KeystoneThickSkinBleedThresholdRatio", "ratio", ".35"), ("COMBAT.KeystoneThickSkinBleedSec", "ms", "2"), ("COMBAT.KeystoneThickSkinBleedTickSec", "ms", ".5"),
            ("COMBAT.KeystoneStrengthDamageRatio", "ratio", ".14"),
            ("COMBAT.KeystoneBrainsMomentumRefundRatio", "ratio", ".3"),
            ("COMBAT.KeystoneBrainsReactionMomentum", "momentum", "15"), ("COMBAT.KeystoneBrainsCooldownCutSec", "ms", "1"),
            ("COMBAT.KeystoneAgilityDamageBase", "points", "35"),
            ("COMBAT.KeystoneAgilityADRatio", "ratio", ".45"),
            ("COMBAT.KeystoneAgilityEmpowerDurationSec", "ms", "3"), ("COMBAT.KeystoneAgilityMoveSpeedRatio", "ratio", ".25"),
            # #198 elements and reactions (CONTENT_AND_BALANCE §4.4).
            ("COMBAT.WetSec", "ms", "4"), ("COMBAT.WetSlow", "ratio", ".15"),
            ("COMBAT.ReactionBase", "points", "30"), ("COMBAT.ReactionPerLevel", "points", "4"),
            ("COMBAT.ReactionAPRatio", "ratio", ".25"), ("COMBAT.ReactionLockSec", "ms", ".5"), ("COMBAT.FogRadiusCm", "cm", "250"), ("COMBAT.FogSec", "ms", "3"),
            ("COMBAT.FogIntervalSec", "ms", "1"), ("COMBAT.ConductRadiusCm", "cm", "450"), ("COMBAT.ConductSlow", "ratio", ".3"),
            ("COMBAT.ConductSlowSec", "ms", "1.5"), ("COMBAT.OverloadRadiusCm", "cm", "200"),
            ("COMBAT.ShockSec", "ms", "4"),
            ("COMBAT.ShockRatio", "ratio", ".5"),
            ("COMBAT.ShockSplashRadiusCm", "cm", "350"),
            ("COMBAT.ChillSec", "ms", "4"),
            ("COMBAT.ChillAttackSlow", "ratio", ".15"),
            ("COMBAT.ChillMoveSlow", "ratio", ".1"),
            ("COMBAT.RotSec", "ms", "4"),
            ("COMBAT.RotHealCut", "ratio", ".25"),
            ("COMBAT.RotPerSec", "points", "3"),
            ("COMBAT.RotAPRatioPerSec", "ratio", ".03"),
            ("COMBAT.RotTickSec", "ms", ".5"),
            ("COMBAT.PushSec", "ms", ".12"),
            ("COMBAT.ConductRatio", "ratio", "1.5"),
            ("COMBAT.OverloadRatio", "ratio", "1.5"),
            ("COMBAT.MeltRatio", "ratio", "2"),
            ("COMBAT.MethaneRatio", "ratio", "1.5"),
            ("COMBAT.MethaneRadiusCm", "cm", "250"),
            ("COMBAT.FreezeRootSec", "ms", "1.2"),
            ("COMBAT.FermentRotSec", "ms", "8"),
            ("COMBAT.FermentHealCut", "ratio", ".6"),
            ("COMBAT.ShatterSkillDamage", "ratio", ".5"),
            ("COMBAT.ShatterSec", "ms", "4"),
            ("COMBAT.CorrodeBreachSec", "ms", "6"),
            ("COMBAT.FrostbiteAttackSlow", "ratio", ".4"),
            ("COMBAT.FrostbiteSec", "ms", "4"),
            ("GROUND.PuddleRadiusCm", "cm", "180"),
            ("GROUND.PuddleSec", "ms", "6"),
            ("GROUND.PuddleTickSec", "ms", ".5"),
            ("GROUND.IceRadiusCm", "cm", "180"),
            ("GROUND.IceSec", "ms", "5"),
            ("GROUND.IceAccelScale", "ratio", ".5"),
            ("GROUND.IceDisplacementScale", "ratio", "1.5"),
            ("GROUND.FireRadiusCm", "cm", "180"),
            ("GROUND.FireSec", "ms", "4"),
            ("GROUND.FireTickSec", "ms", "1"),
            ("GROUND.MiasmaRadiusCm", "cm", "200"),
            ("GROUND.MiasmaSec", "ms", "4"),
            ("GROUND.MiasmaTickSec", "ms", ".5"),
            ("GROUND.FieldRadiusCm", "cm", "180"),
            ("GROUND.FieldSec", "ms", "4"),
            ("GROUND.FieldTickSec", "ms", ".5"),
            ("GROUND.FieldRatio", "ratio", ".3"),
            ("CARDS.VocabWeight", "ratio", "1.2"),
            ("SHOP.VocabWeight", "ratio", "1.8"),
        ):
            expected[key] = (unit, value)
        for key, value in (
            ("XP.BaseToNext", 60), ("XP.IncreasePerLevel", 20), ("XP.SteepFromLevel", 5), ("XP.SteepExtraPerLevel", 30),
            ("MEDAL.MaxTier", 3),
            ("SHOWDOWN.RespawnLimit", 2),
            ("REWARD.PlayerXP.Base", 70), ("REWARD.PlayerXP.PerLevel", 20), ("REWARD.BountyXP", 150), ("REWARD.BountyCardOffer", 3), ("AI.RetryPolicy.MaxAttempts", 3),
            ("FIELD.SpawnPoolMin", 18), ("FIELD.MapRecoveryCount", 10),
            ("CAMP.ReturnDeadEndPlans", 3), ("CAMP.GrowthStagesPerSegment", 3), ("CAMP.GrowthMaxStage", 10), ("AIRDROP.RewardXP", 200),
            ("LEADER.MinLevel", 3),
            ("COMBAT.BreachMaxStacks", 5), ("COMBAT.BurnMaxStacks", 5), ("COMBAT.OverloadBreachStacks", 2),
            ("COMBAT.ShockEveryNthHit", 3), ("COMBAT.MethaneBurnStacks", 2), ("COMBAT.CorrodeBreachStacks", 5), ("GROUND.FireBurnStacks", 1), ("GROUND.MaxPerOwner", 6), ("GROUND.MaxTotal", 40),
            ("GROWTH.OpeningPoints", 3), ("GROWTH.PointsPerLevel", 3), ("GROWTH.MaxLevelPointsXP", 400), ("GROWTH.MaxLevelPoints", 2),
            ("PK.ParticipationPoints", 1), ("PK.WinnerPointsMax", 3), ("PK.BettingPoolMaxPoints", 2),
            # Issue #287 spec §10-§11 (P6) counts.
            ("LIFE.SpawnChoiceCount", 3), ("OBJECTIVE.FlagCardOffer", 3), ("OBJECTIVE.ArenaCardOffer", 5),
            ("DAILY.Count", 2), ("DAILY.RainPuddles", 6), ("DAILY.RumorLeaders", 2), ("AFFIX.FridgeCardOffer", 3),
            ("AFFIX.BonusCardOffer", 5), ("AFFIX.AuditSkillPointsMax", 1), ("EXPLORE.LockerCount", 4),
            ("EXPLORE.LockerCardOffer", 3), ("EXPLORE.PeddlerCardOffer", 5),
            ("GROWTH.KeystoneThresholdPoints", 18), ("GROWTH.ActiveSkipPoints", 1), ("GROWTH.BlueprintSellPoints", 1),
            # Spec §3.8, §3.12 and §13 (P2): card capacity, offers, rerolls, lock, vocabulary weight, the chain guard and the AI.
            ("CARDS.CapacityBase", 6), ("CARDS.LevelsPerSlot", 3), ("CARDS.OfferSize", 3), ("CARDS.FreeRerolls", 3),
            ("CARDS.RerollsPerRest", 1),
            ("CHAIN.MaxDepth", 16), ("CHAIN.MaxEffectsPerStep", 256), ("AI.CardVocabScore", 2), ("AI.LeaderHuntMaxLevelGap", 2), ("AI.BreakoutCardScore", 5),
            # Spec §3.13, §6.5 and §13 (P5): offer skips, the rest shop (in stat points), chain ends and the AI's skip.
            ("CARDS.SkipPoints", 1), ("CARDS.OfferEveryLevels", 3), ("SHOP.CardSlots", 4), ("SHOP.VocabCardSlots", 2), ("SHOP.SkillSlots", 2), ("SHOP.CardPrice", 2), ("SHOP.SkillPrice", 2), ("SHOP.RefreshPrice", 0), ("SHOP.RefreshStep", 1), ("SHOP.SkillPointPrice", 3), ("SHOP.SkillPointMaxPerMatch", 2), ("SHOP.MedalPrice", 4), ("SHOP.MedalMaxPerMatch", 1), ("CHAIN.MinKeySteps", 3),
        ):
            expected[key] = (None, value)
        expected["SKILL.PointLevels"] = (None, (3, 6, 9, 12, 15))
        expected["SKILL.RankBaseScale"] = ("ratio", ("1", "1.35", "1.7"))
        expected["SKILL.RankRatioScale"] = ("ratio", ("1", "1.25", "1.5"))
        expected["COMBO.StepRatios"] = ("ratio", (".9", ".9", "1.3"))
        expected["AIRDROP.LandSec"] = ("ms", ("40", "110", "200", "270", "360", "430"))
        expected["DAILY.DeliveryLandSec"] = ("ms", ("40", "75", "110", "155", "200", "235", "270", "315", "360", "395", "430", "460"))
        expected["SHOWDOWN.SprayRatchetSec"] = ("ms", ("1.15", "2.3"))
        expected["SHOWDOWN.GuardRatchetSec"] = ("ms", ("7", "14"))
        self.assertEqual(set(self.data["parameters"]), set(expected))
        for key, (unit, value) in expected.items():
            with self.subTest(key=key):
                actual = self.catalog.parameter(key)
                if unit is None and type(value) is tuple:
                    self.assertEqual(tuple(actual), value)
                elif unit is None:
                    self.assertIs(type(actual), int)
                    self.assertEqual(actual, value)
                elif type(value) is tuple:
                    self.assertEqual([row["unit"] for row in actual], [unit] * len(value))
                    self.assertEqual([amount(row) for row in actual], [Fraction(item) for item in value])
                else:
                    self.assertEqual(actual["unit"], unit)
                    self.assertEqual(amount(actual), Fraction(value))

    def test_explicit_formulas_and_fixed_product_policies(self):
        self.assertEqual(self.data["formulas"]["AttackMoveAcquireRadius"], "MinVisionTwiceAttackRange")
        self.assertEqual(self.data["formulas"]["Resistance"], "PointsOverScalePlusPoints")
        self.assertEqual(self.data["formulas"]["ResourceMaxChange"], "FloorFinalRatioRejectAliveUnderflow")
        self.assertEqual(amount(self.data["formulas"]["ResistanceScale"]), 100)
        self.assertEqual(amount(self.data["formulas"]["HasteScale"]), 100)
        self.assertEqual(self.data["policies"]["RootBlocks"], ("Move", "Dash"))
        self.assertEqual(self.data["policies"]["CampAggroTrigger"], "ConfirmedCombatHit")
        self.assertEqual(self.data["policies"]["MissingSourceCooldown"], "ContinueServerTime")
        self.assertEqual(min(amount(self.catalog.parameter("VISION.RadiusCm")),
                             2 * amount(self.catalog.parameter("UNIT.BasicAttackRangeCm"))), 500)

    def test_respawn_protection_is_display_metadata_not_an_effect_or_duration_change(self):
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        self.assertEqual(ids["statuses"], {"root": 1, "shield": 2, "camp_returning": 3, "respawn_protection": 4,
                                           "slow": 5, "stat_boost": 6, "exposed": 9,
                                           "taunt": 10, "disarm": 12, "stun": 13, "empowered": 14,
                                           "knockback": 20, "silence": 25, "burn": 26,
                                           "attack_slowed": 27, "wet": 30, "electrified": 31, "braced": 32,
                                           "camp_enraged": 34, "camp_guarding": 35, "chilled": 36, "rotten": 37})
        self.assertEqual(ids["effect_ops"], {"damage": 1, "shield": 2, "dispel_root": 3, "root": 4, "lifesteal": 7, "dash": 8, "slow": 12, "timed_stat_bonus": 13, "self_area": 14, "taunt": 18, "disarm": 20, "stun": 21, "empower_basic": 22, "thorns": 24, "triggered_shield": 31, "dispel_slow": 37, "knockback": 40, "zone": 50, "zone_slow": 52, "zone_damage": 54, "silence": 69, "breach": 70, "burn": 71, "zone_burn": 77, "channel_cone": 78, "ground_impact": 79, "attack_slow": 80, "contact_trap": 81, "dash_contact": 82, "wet": 90, "electrify": 91, "chain_bounce": 92, "brace": 93, "zone_wet": 94, "zone_haste": 95, "basic_element": 101, "idle_ready_amp": 103, "cooldown_cost": 130, "knockback_impact": 138, "skill_lifesteal": 146, "projectile_return": 152, "projectile_pierce": 153, "echo_impact": 155, "hit_haste_other": 156, "setup_mark": 157, "reaction_haste": 158, "zone_tether": 160, "final_knockback": 162, "empower_reach": 163, "pull": 164, "vulnerable": 170, "next_basic_amp": 171, "momentum_gain": 172, "momentum_steal": 173, "follow_up": 174, "recast": 175, "dash_back": 176, "self_buff": 177, "burn_burst": 178, "pulse": 179, "zone_guard": 180, "zone_momentum": 181, "overcharge": 182, "follow_basic": 183, "shield_burst_break": 184, "basic_rider": 185, "delayed_burst": 186, "shield_reflect": 187, "shield_end_heal": 188, "shield_hit_bonus": 189, "brace_stun": 190, "brace_heal": 191, "brace_stack": 192, "brace_shove": 193, "brace_shield": 194, "combo_momentum": 196, "basic_cooldown_cut": 197, "combo_skill_hit": 198, "combo_knockback": 199, "combo_double": 200, "overcap_extra_hit": 201, "basic_ap_rider": 202, "focus_attack_speed": 203, "skill_hit_momentum": 204, "auto_recast": 205, "skill_damage_factor": 206, "hp_cost": 207, "skill_ad_rider": 208, "skill_growth": 209, "skill_element": 210, "dash_contact_reset": 211, "dash_split": 212, "dash_phase": 213, "dash_contact_element": 214, "takedown_reset": 215, "last_stand": 216, "reaction_momentum": 217, "reaction_element": 218, "skill_crit": 219, "lifesteal_skills": 220, "armor_to_hp": 221, "overheal_shield": 222, "missing_hp_attack_speed": 223, "haste_momentum": 224, "heal_growth": 225, "kill_growth": 226, "control_growth": 227, "idle_charge": 228, "element_aura": 229, "element_duration": 230, "reaction_factor": 231, "burn_cap": 232, "rot": 233, "chill": 234, "ground_state": 236, "fan": 237, "dash_ice": 238, "ground_relocate": 239, "double_stall": 240, "ground_growth": 241, "bag": 242, "basic_projectile": 243, "combo_sweep": 244, "combo_cast": 245, "speed_factor": 246, "hit_growth": 247, "control_amp": 248, "reshape": 249, "shield_spend": 250, "recent_cast_amp": 251, "dash_ram": 253, "tally": 255, "grab_line": 256, "backstab_lunge": 257, "parry_stance": 258, "warcry_ring": 259, "slipper_pickup": 260, "frenzy_waves": 261, "cordon_line": 262, "brain_speed": 263, "spellblade_echo": 264, "heavy_mallet": 265, "projectile_flight_amp": 266, "eagle_stance": 267, "runaway_train": 268, "skate_blade": 270, "tonnage_crush": 271, "hulking_presence": 272, "iron_hedgehog": 273, "reactive_plating": 274, "desperado": 275, "fuel_the_fire": 276, "rogue_works": 277, "martial_scholar": 278})
        entries = [row for row in self.data["display"] if row["id"] == "respawn_protection"]
        self.assertEqual(len(entries), 1)
        self.assertEqual({key: value for key, value in entries[0].items() if not key.endswith("_text")}, {
            "id": "respawn_protection", "namespace": "DemoContent",
            "name_key": "respawn_protection.name", "description_key": "respawn_protection.description",
            "icon_id": "icon_respawn_protection"})
        self.assertEqual(entries[0]["name_text"], "复活保护")
        self.assertEqual(self.catalog.parameter("LIFE.ProtectionSec"), {"unit": "ms", "ms": 3000})
        for row in self.data["abilities"]:
            self.assertNotIn("respawn_protection", {effect["op"] for effect in content.all_effects(row)})

    def test_display_names_are_the_approved_fog_harbor_names(self):
        # #111 雾港 v2 entry names (CONTENT_AND_BALANCE 3.2/4/5/6.2/12.5); #148 status names use generic game terms.
        # Skill branch steps (issue #287 spec §5.1) are checked on their own below.
        step = re.compile(r"_(2a|2b|3a1|3a2|3b1|3b2)$")
        steps = {row["id"]: row["name_text"] for row in self.data["display"] if step.search(row["id"])}
        self.assertEqual(len(steps), 24 * 6)
        self.assertEqual((steps["active_elbow_2a"], steps["active_elbow_3b2"], steps["active_open_mic_2b"]), ("给我过来", "踹出内伤", "破音大嗓门"))
        self.assertEqual({row["id"]: row["name_text"] for row in self.data["display"] if not step.search(row["id"])}, {
            "character_demo": "脆皮大学生",
            "active_elbow": "滚远点", "active_pants_down": "扒裤子", "active_crotch_grab": "掏蛋子",
            "active_unplug": "拧你手腕", "active_snark_bolt": "甩你一鞋",
            "active_seethe": "烫你屁股", "active_quick_meeting": "脚底开锅", "active_bail_out": "贴脸补一拳",
            "active_not_mad": "锅盖挡脸", "active_stare_down": "来打老子啊",
            "active_triple_combo": "左右开抽", "active_calculus_drop": "板砖糊脸", "active_flyer_trap": "踩一脚试试",
            "active_open_mic": "扯嗓子喷你", "active_hose_down": "滋你一脸", "active_zap": "电你个哆嗦",
            "active_charged_ram": "蛮牛顶", "active_brace": "顶着揍",
            "active_cola_shake": "冰可乐摇一摇", "active_desk_fan": "电风扇", "active_stale_takeout": "过期外卖", "active_school_bag": "占座书包",
            "active_flurry_rush": "疯狗乱拳", "active_cordon_tape": "警戒封条",
            "stat_physique": "体格", "stat_thick_skin": "脸皮", "stat_strength": "力道", "stat_brains": "脑力", "stat_agility": "身手",
            # Spec §7 P2 cards.
            **{identity: frame[0] for identity, frame in CARD_FRAMES.items()},
            "final_hit_and_run": "打一枪换个地方", "final_clear_out": "清场", "final_kill_seconds": "人头换秒",
            "final_usurp": "篡位", "final_carry_flag": "扛旗跑路", "final_cage": "铁笼",
            "final_private_pole": "私人旗杆", "final_double_yellow": "双黄线",
            "monster_scavenger": "已读不回的 HR", "monster_guardian": "追着办卡的私教", "monster_dean": "画饼老板",
            "monster_advisor": "催毕设的导师",
            "monster_promoter":"拉群的地推", "monster_courier":"催单的骑手", "monster_agent":"带你看房的中介", "monster_guard":"轮班的保安",
            "monster_auditor":"突击检查的审计", "monster_client":"要你改稿的甲方", "monster_director":"年终绩效总监",
            "monster_final_client":"终审大甲方", "monster_property":"坐镇的物业主任",
            "camp_enraged":"同事离职", "camp_guarding":"正面防守",
            "root": "定身", "shield": "护盾", "camp_returning": "回位", "respawn_protection": "复活保护",
            "slow": "减速", "stat_boost": "增益", "exposed": "破防",
            "taunt": "嘲讽", "disarm": "缴械", "stun": "眩晕", "empowered": "强化普攻",
            "knockback": "击退", "silence": "沉默", "burn": "灼烧", "attack_slowed": "攻速降低",
            "wet": "潮湿", "electrified": "感电", "braced": "顶着揍中", "chilled": "冻", "rotten": "馊",
        })
        self.assertIn("「来打老子啊」", next(row for row in self.data["display"] if row["id"] == "taunt")["description_text"])
        for row in self.data["display"]:
            with self.subTest(identity=row["id"]):
                self.assertEqual(row["namespace"], "DemoContent")
                self.assertTrue(row["description_text"])
                self.assertIsNone(content.DISPLAY_DESCRIPTION_DIGIT.search(row["description_text"]))
                self.assertIsNone(content.DISPLAY_TEXT_FORBIDDEN.search(row["description_text"]))
                # #148: rule text names attributes, resources, damage, statuses, the basic attack and the dash generically.
                self.assertEqual([term for term in RETIRED_RULE_TERMS if term in row["description_text"]], [])
                # #198: cards have one line; actives and stats read their numbers from placeholders.
                self.assertFalse({"advanced_key", "advanced_text"} & set(row))
                if row["id"] in ACTIVE_FRAMES or row["id"] in STAT_POINTS:
                    self.assertIn("[", row["description_text"])
                if row["id"] in CARD_FRAMES:
                    self.assertEqual(row["icon_id"], "icon_" + row["id"])
        # A final mod's card face is one rule sentence with no extra effect, cost or notes line.
        for identity in content.FINAL_MOD_CONTRACTS:
            row = next(row for row in self.data["display"] if row["id"] == identity)
            with self.subTest(identity=identity):
                self.assertFalse({"effect_key", "cost_key", "notes_key"} & set(row))
                self.assertEqual(row["description_text"].count("。"), 1)

    def test_respawn_protection_requires_exact_display_keys_without_runtime_state(self):
        author = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
        schema = content.AuthorSchema(content.parse_json((content.ROOT / content.SCHEMA_SOURCE).read_bytes(), "content schema"))
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        schema.validate(author)
        projected = content.project_author(schema, author)
        projected["display"] = [row for row in projected["display"] if row["id"] != "respawn_protection"]
        with self.assertRaisesRegex(content.ContentError, "DisplayReference"):
            content.validate_definitions(projected, ids)
        projected = content.project_author(schema, author)
        projected["display"].append(dict(next(row for row in projected["display"] if row["id"] == "respawn_protection")))
        with self.assertRaisesRegex(content.ContentError, "DuplicateId"):
            content.validate_definitions(projected, ids)
        for field in ("name_key", "description_key", "icon_id"):
            projected = content.project_author(schema, author)
            next(row for row in projected["display"] if row["id"] == "respawn_protection")[field] = "unresolved_reference"
            with self.subTest(field=field), self.assertRaisesRegex(content.ContentError, "DisplayReference"):
                content.validate_definitions(projected, ids)
        for field in ("ExpiresAtServerMs", "RespawnAt", "Life", "BodyIdentity", "Source"):
            candidate = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
            next(row for row in candidate["display"] if row["id"] == "respawn_protection")[field] = 1
            with self.subTest(field=field), self.assertRaisesRegex(content.ContentError, "UnknownField"):
                schema.validate(candidate)

    def test_respawn_protection_native_mapping_rejects_omission_alias_and_renumbering(self):
        for change in (
            lambda statuses: statuses.pop("respawn_protection"),
            lambda statuses: statuses.update(respawn_protection=3),
            lambda statuses: statuses.update(respawn_protection=5),
            lambda statuses: statuses.update(respawn_protection=True),
            lambda statuses: statuses.update(unknown_protection=statuses.pop("respawn_protection")),
        ):
            ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
            change(ids["statuses"])
            with self.subTest(statuses=ids["statuses"]), self.assertRaises(content.ContentError):
                content.validate_native_ids(content.ROOT, ids)

    def test_all_active_values(self):
        # #198: every ability is a single-level active carrying one value set at G=1.
        for identity, (mode, reach, windup, mana, cooldown, opening) in ACTIVE_FRAMES.items():
            with self.subTest(identity=identity):
                row = self.catalog.definition(identity)
                self.assertFalse({"ranks", "kind", "draftable"} & set(row))
                self.assertEqual((row["target"]["mode"], amount(row["windup"]), row["cooldown_family"], row["opening"]),
                                 (mode, windup, "cd_" + identity[len("active_"):], opening))
                self.assertEqual(amount(row["target"]["range"]) if "range" in row["target"] else None, reach)
                self.assertEqual((amount(row["momentum_cost"]), amount(row["cooldown"])), (mana, cooldown))
                self.assertEqual([(effect["op"], author_units(effect)) for effect in row["effects"]], ACTIVE_EFFECTS[identity])
        bolt = self.catalog.definition("active_snark_bolt")["target"]
        self.assertEqual((amount(bolt["projectile_radius"]), amount(bolt["projectile_speed"])), (55, 1650))
        takeout = self.catalog.definition("active_stale_takeout")["target"]
        self.assertEqual((amount(takeout["projectile_radius"]), amount(takeout["projectile_speed"])), (60, 1400))
        self.assertEqual(amount(self.catalog.definition("active_hose_down")["target"]["half_width"]), 80)
        self.assertEqual(amount(self.catalog.definition("active_pants_down")["target"]["half_width"]), 65)
        self.assertEqual(amount(self.catalog.definition("active_cola_shake")["target"]["angle"]), 60)

    def test_opening_pool_and_active_offer_levels(self):
        self.assertEqual({row["id"] for row in self.data["abilities"] if row["opening"]},
                         {identity for identity, frame in ACTIVE_FRAMES.items() if frame[-1]})
        self.assertEqual({row["id"] for row in self.data["abilities"] if not row["opening"]},
                         {"active_crotch_grab", "active_unplug", "active_stare_down", "active_brace", "active_flyer_trap",
                          "active_desk_fan", "active_stale_takeout", "active_flurry_rush", "active_cordon_tape"})
        self.assertEqual(sum(1 for row in self.data["abilities"] if row["opening"]), 15)
        self.assertEqual(self.data["progression"]["active_offer_levels"], (2, 5, 9))
        self.assertEqual(set(self.data["progression"]),
                         {"max_level", "active_slots", "xp_start", "xp_thresholds", "xp_to_next", "active_offer_levels"})

    def test_all_cards_in_native_order(self):
        self.assertEqual([row["id"] for row in self.data["cards"]], list(CARD_FRAMES))
        previous = 199
        for identity, (_, form, kind) in CARD_FRAMES.items():
            with self.subTest(identity=identity):
                row = self.catalog.definition(identity)
                self.assertTrue(previous < row["native_id"] <= 305)
                previous = row["native_id"]
                self.assertEqual((row["form"], row["type"]), (form, kind))
                self.assertTrue(1 <= len(row["vocab"]) <= 4 and set(row["vocab"]) <= set(content.CARD_VOCAB))
                self.assertEqual("choose_element" in row, identity == "card_live_wire")
        self.assertEqual(len(CARD_FRAMES), 90)

    def test_retired_content_ids_stay_reserved_and_absent(self):
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        self.assertEqual(ids["retired_ranges"]["abilities"], [[0, 91], [101, 101], [104, 113], [116, 116], [118, 125], [130, 130]])
        self.assertEqual(ids["retired_ranges"]["stats"], [[0, 26]])
        self.assertIn([28, 29], ids["retired_ranges"]["statuses"])
        self.assertFalse({"attributes"} & set(ids) | {"attributes"} & set(ids["retired_ranges"]))
        self.assertFalse({"active_sock_shot", "active_account_banned", "active_dirty_puddle", "active_apology_slide"} & set(ids["abilities"]))
        self.assertFalse([identity for identity in ids["abilities"] if identity.startswith("passive_") or identity == "active_cower"])
        self.assertFalse({"cowering", "contact_guard"} & set(ids["statuses"]))
        self.assertNotIn("STAT.AbilityCritMultiplier", self.data["parameters"])
        self.assertEqual(next(row for row in self.data["display"] if row["id"] == "character_demo")["description_text"],
                         "已成年，一碰就碎但嘴很硬。开局只会普攻；升级攒属性点、挑技能、抽牌。")
        self.assertFalse([row["id"] for row in self.data["display"]
                          if row["id"].startswith(("passive_", "attr_", "opening_")) or row["id"] == "active_cower"])

    def test_camp_perception_fits_inside_vision(self):
        # #97: a camp member's perception (its leash) beyond VISION.RadiusCm would fault the camp decision at T=0.
        author = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
        schema = content.AuthorSchema(content.parse_json((content.ROOT / content.SCHEMA_SOURCE).read_bytes(), "content schema"))
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        schema.validate(author)
        projected = content.project_author(schema, author)
        guardian = next(row for row in projected["camps"]["monsters"] if row["id"] == "monster_guardian")
        guardian["leash_radius"].update(numerator=1450, denominator=1)
        content.validate_definitions(projected, ids)
        with self.assertRaisesRegex(content.ContentError, "InvalidGeometry"):
            content.validate_numbers(projected)

    def test_grown_skill_bases_fit_inside_int64_at_the_top_level(self):
        # #147/#198: a base that fits ungrown but overflows once grown for the top level is refused at import,
        # never served as a missing value set.
        author = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
        schema = content.AuthorSchema(content.parse_json((content.ROOT / content.SCHEMA_SOURCE).read_bytes(), "content schema"))
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        schema.validate(author)
        projected = content.project_author(schema, author)
        content.validate_definitions(projected, ids)
        bolt = next(row for row in projected["abilities"] if row["id"] == "active_snark_bolt")
        bolt["effects"][0]["base"]["p2"] = 4 * 10 ** 14
        with self.assertRaises(content.ContentError):
            content.validate_numbers(projected)

    def approved_projection_inputs(self):
        author = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
        schema = content.AuthorSchema(content.parse_json((content.ROOT / content.SCHEMA_SOURCE).read_bytes(), "content schema"))
        ids = content.parse_json((content.ROOT / content.IDS_SOURCE).read_bytes(), "native IDs")
        schema.validate(author)
        return author, schema, ids

    def test_skill_target_and_domain_refusals(self):
        author, schema, ids = self.approved_projection_inputs()

        def ability(data, identity):
            return next(row for row in data["abilities"] if row["id"] == identity)

        def first(data, identity, op):
            return next(effect for effect in ability(data, identity)["effects"] if effect["op"] == op)

        def display(data, identity):
            return next(row for row in data["display"] if row["id"] == identity)

        for name, mutate, code in (
            ("area on a unit target", lambda d: ability(d, "active_elbow")["effects"].append(
                {"op": "self_area", "radius": {"unit": "cm", "numerator": 100, "denominator": 1}}), "TargetEffectMismatch"),
            ("slow that stops the body", lambda d: first(d, "active_snark_bolt", "slow")["ratio"].update(bp=10000), "NumericDomain"),
            ("zero-radius area", lambda d: first(d, "active_seethe", "self_area")["radius"].update(numerator=0), "NumericDomain"),
            ("timed resource maximum", lambda d: first(d, "active_stare_down", "timed_stat_bonus")["stats"].update(
                MaxHPFlat={"unit": "hp", "p2": 100}), "UnknownField"),
            ("zone payload without its zone", lambda d: ability(d, "active_quick_meeting")["effects"].remove(
                first(d, "active_quick_meeting", "zone")), "MissingHandler"),
            ("ineffective zone damage", lambda d: [first(d, "active_quick_meeting", "zone_damage")[field].update(**{key: 0})
                                                   for field, key in (("base", "p2"), ("ad_ratio", "bp"), ("ap_ratio", "bp"))],
             "IneffectiveEffect"),
            ("zone slow that stops the body", lambda d: first(d, "active_quick_meeting", "zone_slow")["ratio"].update(bp=10000),
             "NumericDomain"),
            ("zone pulses past its duration", lambda d: first(d, "active_quick_meeting", "zone").update(count=5), "InvalidTiming"),
            ("card placeholder naming an absent effect", lambda d: display(d, "active_snark_bolt").update(
                description_text="眩晕 [stun.duration] 秒。"), "CardTemplate"),
            ("stat placeholder on an active", lambda d: display(d, "active_snark_bolt").update(
                description_text="多 [per_point.APFlat]。"), "CardTemplate"),
            ("stat card naming another stat's bonus", lambda d: display(d, "stat_strength").update(
                description_text="每点 +[per_point.APFlat]。"), "CardTemplate"),
            ("opening pool below one offer", lambda d: [row.update(opening=False) for row in d["abilities"]], "OpeningPool"),
            ("unreachable active offer level", lambda d: d["progression"].update(active_offer_levels=[6, 3]), "ActiveOfferLevels"),
        ):
            projected = content.project_author(schema, author)
            mutate(projected)
            with self.subTest(case=name), self.assertRaisesRegex(content.ContentError, code):
                content.validate_definitions(projected, ids)

    def test_point_bought_stats(self):
        # #198: five stats bought with points; one point adds these flat and additive-percent bonuses.
        self.assertEqual({row["id"]: row["native_id"] for row in self.data["stats"]},
                         {identity: native for identity, (native, _) in STAT_POINTS.items()})
        for identity, (_, bonuses) in STAT_POINTS.items():
            with self.subTest(identity=identity):
                row = self.catalog.definition(identity)
                self.assertEqual(set(row), {"id", "native_id", "per_point"})
                self.assertEqual({key: amount(value) for key, value in row["per_point"].items()}, bonuses)
        texts = {row["id"]: row["description_text"] for row in self.data["display"]}
        self.assertEqual(texts["stat_brains"],
                         "每点法术强度 +[per_point.APFlat]、技能急速 +[per_point.CooldownHasteFlat]、交战时劲回复 +[per_point.MomentumRegenFlat]/秒。")

    def test_expanded_skill_geometry_and_recovery_refusals(self):
        author, schema, ids = self.approved_projection_inputs()
        cases = (
            ("active_calculus_drop", "ground_impact", "inner_radius", {"unit": "cm", "numerator": 201, "denominator": 1}, "InvalidGeometry"),
            ("active_calculus_drop", "ground_impact", "outer_ratio", {"unit": "ratio", "bp": 10000}, "NumericDomain"),
            ("active_flyer_trap", "contact_trap", "trigger_radius", {"unit": "cm", "numerator": 0, "denominator": 1}, "InvalidGeometry"),
            ("active_zap", "chain_bounce", "radius", {"unit": "cm", "numerator": 0, "denominator": 1}, "InvalidGeometry"),
            ("active_zap", "chain_bounce", "ratio", {"unit": "ratio", "bp": 10000}, "NumericDomain"),
            ("active_brace", "brace", "ratio", {"unit": "ratio", "bp": 10000}, "NumericDomain"),
            ("active_brace", "brace", "record_ratio", {"unit": "ratio", "bp": 0}, "IneffectiveEffect"),
            ("active_desk_fan", "fan", "width", {"unit": "cm", "numerator": 0, "denominator": 1}, "InvalidGeometry"),
            ("active_cordon_tape", "cordon_line", "width", {"unit": "cm", "numerator": 0, "denominator": 1}, "InvalidGeometry"),
        )
        for identity, op, field, value, error in cases:
            projected = content.project_author(schema, author)
            row = next(item for item in projected["abilities"] if item["id"] == identity)
            effect = next(item for item in row["effects"] if item["op"] == op)
            if value is None:
                del effect[field]
            else:
                effect[field] = value
            with self.subTest(identity=identity, field=field), self.assertRaisesRegex(content.ContentError, error):
                content.validate_definitions(projected, ids)

    def test_innate_dash(self):
        # D19/D20: the innate D dash belongs to the character.
        dash = self.data["character"]["dash"]
        self.assertNotIn("momentum_cost", dash)
        self.assertEqual((dash["cooldown"]["ms"], amount(dash["distance"]), dash["duration"]["ms"]), (9000, 450, 180))

    def test_retired_mechanics_refuse_on_actives(self):
        # #198 deletes the advanced-rank changes end to end: their operations and fields no longer exist.
        author, schema, ids = self.approved_projection_inputs()
        for identity, effect in (
            ("active_crotch_grab", {"op": "execute", "ratio": {"unit": "ratio", "bp": 3000}, "hp_below": {"unit": "ratio", "bp": 5000}}),
            ("active_snark_bolt", {"op": "pierce"}), ("active_not_mad", {"op": "dispel_disarm"}),
            ("active_quick_meeting", {"op": "zone_root", "duration": {"unit": "ms", "ms": 1000}}),
            ("active_seethe", {"op": "echo", "delay": {"unit": "ms", "ms": 1000}, "ratio": {"unit": "ratio", "bp": 5000}}),
            ("active_zap", {"op": "breach", "stacks": 3}),
        ):
            projected = content.project_author(schema, author)
            next(item for item in projected["abilities"] if item["id"] == identity)["effects"].append(effect)
            with self.subTest(identity=identity, op=effect["op"]), self.assertRaisesRegex(content.ContentError, "MissingHandler"):
                content.validate_definitions(projected, ids)
        for identity, op, field, value in (
            ("active_pants_down", "root", "slowed_stun", {"unit": "s", "value": 1.2}),
            ("active_elbow", "knockback", "wall_stun", {"unit": "s", "value": 1.2}),
            ("active_triple_combo", "empower_basic", "basic_stacks", 2),
            ("active_quick_meeting", "zone_slow", "linger", {"unit": "s", "value": 0.5}),
        ):
            candidate = content.parse_json((content.ROOT / content.CONTENT_SOURCE).read_bytes(), "content author")
            row = next(item for item in candidate["abilities"] if item["id"] == identity)
            next(item for item in row["effects"] if item["op"] == op)[field] = value
            with self.subTest(identity=identity, field=field), self.assertRaisesRegex(content.ContentError, "SchemaVariant"):
                schema.validate(candidate)

    def test_only_seethe_damage_scales_with_the_casters_max_hp(self):
        scaled = {row["id"] for row in self.data["abilities"] for effect in row["effects"]
                  if effect["op"] == "damage" and effect["max_hp_ratio"]["bp"] != 0}
        self.assertEqual(scaled, {"active_seethe", "active_charged_ram", "active_open_mic"})

    def test_self_area_damage_refuses_without_its_area(self):
        author, schema, ids = self.approved_projection_inputs()
        projected = content.project_author(schema, author)
        seethe = next(row for row in projected["abilities"] if row["id"] == "active_seethe")
        seethe["effects"] = [effect for effect in seethe["effects"] if effect["op"] != "self_area"]
        with self.assertRaisesRegex(content.ContentError, "TargetEffectMismatch"):
            content.validate_definitions(projected, ids)

    def test_v7_contracts_reject_misplaced_mechanics(self):
        author, schema, ids = self.approved_projection_inputs()
        for identity, change in (
            ("active_zap", lambda effects: effects.pop(0)),
            ("active_unplug", lambda effects: effects.append({"op": "stun", "duration": {"unit": "ms", "ms": 500}})),
            ("active_bail_out", lambda effects: effects.pop(0)),
            ("active_seethe", lambda effects: next(e for e in effects if e["op"] == "burn").update(basic_stacks=1)),
            ("active_stare_down", lambda effects: effects.append({"op": "stat_bonus", "stats": {"ADFlat": {"unit": "points", "p2": 100}}})),
        ):
            projected = content.project_author(schema, author)
            change(next(row for row in projected["abilities"] if row["id"] == identity)["effects"])
            with self.subTest(identity=identity), self.assertRaises(content.ContentError):
                content.validate_definitions(projected, ids)

    def test_monster_and_progression_values(self):
        self.assertEqual(self.data["progression"]["xp_thresholds"],
                         (0, 60, 140, 240, 390, 590, 840, 1140, 1490, 1890, 2340, 2840, 3390, 3990, 4640, 5340))
        self.assertEqual(self.data["progression"]["xp_to_next"],
                         (60, 80, 100, 150, 200, 250, 300, 350, 400, 450, 500, 550, 600, 650, 700))
        self.assertEqual((self.data["progression"]["max_level"], self.data["progression"]["active_slots"]), (16, 3))
        for identity, tier, count, hp, resist, attack, rate, windup, reach, speed, leash, grace, xp, interval in (
            ("monster_scavenger", "Low", 3, 90, 8, 14, ".80", 350, 150, 192, 900, 3000, 15, 1250),
            ("monster_guardian", "High", 2, 315, 20, 50, ".65", 500, 175, 208, 1050, 4000, 44, 1539),
            ("monster_advisor", "Elite", 1, 1350, 30, 126, ".60", 500, 175, 208, 1050, 4000, 196, 1667),
            ("monster_director", "Ancient", 1, 2100, 40, 182, ".55", 500, 175, 208, 1050, 4000, 405, 1819),
        ):
            row = self.catalog.definition(identity)
            self.assertEqual((row["tier"], row["count_per_camp"], row["reward_xp"]), (tier, count, xp))
            for key, expected in (("hp", hp), ("armor", resist), ("magic_resist", resist), ("attack", attack),
                                  ("attack_speed", Fraction(rate)), ("attack_range", reach), ("move_speed", speed),
                                  ("leash_radius", leash), ("perception_radius", leash), ("collision_radius", 40)):
                self.assertEqual(amount(row[key]), expected)
            self.assertEqual((row["attack_windup"]["ms"], row["lose_target_grace"]["ms"],
                              row["attack_interval_ms"]), (windup, grace, interval))
            self.assertTrue(all(amount(row[key]) == 0 for key in ("hp_regen", "ap", "crit_chance")))

    def test_approved_boss(self):
        boss = self.data["boss"]
        self.assertEqual(boss["id"], "monster_dean")
        for key, expected in (("hp", 2800), ("armor", 30), ("magic_resist", 30), ("attack", 210), ("attack_range", 250),
                              ("move_speed", 180), ("collision_radius", 40), ("aggro_radius", 400)):
            self.assertEqual(amount(boss[key]), expected, key)
        self.assertEqual((boss["attack_interval"]["ms"], boss["attack_windup"]["ms"], boss["attacker_memory"]["ms"],
                          boss["appear_at"]["ms"], boss["leave_at"]["ms"], boss["reward_xp"]), (1500, 500, 5000, 210000, 480000, 320))
        self.assertNotIn("approval", boss)

    def test_world_identity_and_shared_values(self):
        self.assertEqual(self.world.world["map_id"], "demo_world")
        self.assertEqual((self.world.world["schema_version"], self.world.world["projection_revision"]), (5, 5))
        self.assertEqual(content._wire_fraction(self.world.world["geometry"]["sight_sample_height_cm"], "sight"), 120)
        self.assertEqual(content._wire_fraction(self.world.world["geometry"]["camp_clearance_cm"], "clearance"), 300)

    def test_final_arena_anchor_ring_and_capture(self):
        anchors = content.rows_by_id(self.world.world["anchors"], "world anchors")
        self.assertEqual(sorted(identity for identity, row in anchors.items() if row["kind"] == "FinalSpawn"),
                         [f"final_spawn_{slot}" for slot in range(8)])
        self.assertEqual([identity for identity, row in anchors.items() if row["kind"] == "FinalCapture"],
                         ["final_capture"])
        centre = world.point(anchors["final_capture"]["position_cm"], "final_capture")
        ring = [world.point(anchors[f"final_spawn_{slot}"]["position_cm"], f"final_spawn_{slot}")
                for slot in range(8)]
        radii = [math.hypot(float(point[0] - centre[0]), float(point[1] - centre[1])) for point in ring]
        self.assertLessEqual(max(radii) - min(radii), 5)
        angles = [math.degrees(math.atan2(float(point[1] - centre[1]), float(point[0] - centre[0])) % (2 * math.pi))
                  for point in ring]
        for index, angle in enumerate(angles):
            self.assertAlmostEqual((angles[(index + 1) % len(angles)] - angle) % 360, 45, delta=1)

        moved = json.loads(self.world.outputs[content.WORLD_OUTPUT])
        next(row for row in moved["anchors"] if row["id"] == "final_spawn_0")["position_cm"]["x"]["numerator"] += 100
        with self.assertRaises(content.ContentError):
            world.validate_bindings(moved, self.data)

    def test_exact_placement_and_parameter_references(self):
        placement = self.world.world["placement"]
        self.assertEqual([world.point(row, "offset") for row in placement["spawn_offsets_cm"]],
                         [(0, 0, 0), (100, 0, 0), (50, 87, 0), (-50, 87, 0), (-100, 0, 0), (-50, -87, 0), (50, -87, 0)])
        self.assertEqual(content._wire_fraction(placement["spawn_validation_radius_cm"], "envelope"), 200)
        self.assertEqual(self.world.world["parameter_refs"], (
            "AIRDROP.PickupRadiusCm", "CAMP.ResetArrivalRadiusCm", "CAPTURE.RadiusCm", "DORM.RadiusCm",
            "EXPLORE.LockerRadiusCm", "OBJECTIVE.FlagRadiusCm", "UNIT.BasicAttackRangeCm", "UNIT.CollisionRadiusCm", "VISION.RadiusCm"))

    def test_byte_provenance_and_acyclic_producer_order(self):
        self.assertEqual(self.world.world["source_sha256"], content.sha256((content.ROOT / content.WORLD_SOURCE).read_bytes()))
        self.assertEqual(self.world.world["author_schema_sha256"], content.sha256((content.ROOT / content.WORLD_SCHEMA_SOURCE).read_bytes()))
        self.assertFalse(set(world.OUTPUT_PATHS).intersection(self.world.inputs))
        self.assertFalse(set(content.OUTPUT_PATHS).intersection(self.world.inputs))
        with patch.object(content, "compile_content", side_effect=AssertionError("world depends on generated content root")):
            self.assertEqual(world.compile_world().outputs, self.world.outputs)
        self.assertEqual(set(self.world.world), {
            "schema_version", "projection_revision", "map_id", "layout_revision", "geometry_revision",
            "source_sha256", "author_schema_sha256", "approval", "geometry", "placement", "parameter_refs", "anchors"})
        self.assertNotIn(self.root_bundle.digest.hex(), self.world.outputs[content.WORLD_OUTPUT].decode("utf-8"))
        self.assertEqual(content.verify_runtime_files(content.ROOT, self.root_bundle.digest), self.root_bundle.digest)
        content.check_outputs(content.ROOT, self.root_bundle.outputs)
        world_sync.synchronize(check=True)


def wire(value):
    value = Fraction(value)
    return {"numerator": value.numerator, "denominator": value.denominator}


def vec(x, y, z=0):
    return {"x": wire(x), "y": wire(y), "z": wire(z)}


def box(x0, y0, x1, y1, z=0):
    return [vec(x0, y0, z), vec(x1, y0, z), vec(x1, y1, z), vec(x0, y1, z)]


class WorldReuseScope(unittest.TestCase):
    """reuse_binding_reports as a fresh process sees it: this module's own open scope is set aside for each test."""

    def setUp(self):
        for name, value in (("_reuse", None), ("_reuse_depth", 0)):
            patcher = patch.object(world, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)
        self.validated = []

        def validate(raw):
            self.validated.append(raw)
            return {"anchors": [{"id": raw.decode("ascii")}]}, {"a": {"members": [1, 2]}}

        patcher = patch.object(world, "_validate_payload", validate)
        patcher.start()
        self.addCleanup(patcher.stop)

    def test_outside_a_scope_every_call_validates(self):
        world.validate_payload(b"one")
        world.validate_payload(b"one")
        self.assertEqual(self.validated, [b"one", b"one"])

    def test_identical_payload_validates_once_and_another_payload_validates_again(self):
        with world.reuse_binding_reports():
            first = world.validate_payload(b"one")
            self.assertIs(world.validate_payload(b"one"), first)
            other = world.validate_payload(b"two")
            self.assertEqual(self.validated, [b"one", b"two"])
            self.assertEqual((first[0]["anchors"][0]["id"], other[0]["anchors"][0]["id"]), ("one", "two"))

    def test_a_nested_scope_shares_the_cache_and_the_outermost_exit_drops_it(self):
        with world.reuse_binding_reports():
            world.validate_payload(b"one")
            with world.reuse_binding_reports():
                world.validate_payload(b"one")
            world.validate_payload(b"one")
            self.assertEqual(self.validated, [b"one"])
        self.assertIsNone(world._reuse)
        self.assertEqual(world._reuse_depth, 0)
        with world.reuse_binding_reports():
            world.validate_payload(b"one")
        self.assertEqual(self.validated, [b"one", b"one"])

    def test_a_failing_block_still_closes_its_scope(self):
        with self.assertRaises(RuntimeError), world.reuse_binding_reports():
            with world.reuse_binding_reports():
                world.validate_payload(b"one")
                raise RuntimeError("inside")
        self.assertIsNone(world._reuse)
        self.assertEqual(world._reuse_depth, 0)
        world.validate_payload(b"one")
        self.assertEqual(self.validated, [b"one", b"one"])

    def test_shared_results_are_read_only_at_every_depth(self):
        with world.reuse_binding_reports():
            payload, anchors = world.validate_payload(b"one")
            with self.assertRaises(TypeError):
                payload["anchors"] = ()
            with self.assertRaises(TypeError):
                payload["anchors"][0]["id"] = "changed"
            with self.assertRaises(TypeError):
                anchors["a"]["members"] = ()
            self.assertIsInstance(payload["anchors"], tuple)
            self.assertIsInstance(anchors["a"]["members"], tuple)

    def test_a_non_bytes_payload_is_never_cached(self):
        with world.reuse_binding_reports():
            raw = bytearray(b"one")
            world.validate_payload(raw)
            world.validate_payload(raw)
        self.assertEqual(len(self.validated), 2)


class WorldGeometryFunctions(unittest.TestCase):
    """What each world element does, on a small synthetic world instead of the current map layout."""

    @classmethod
    def setUpClass(cls):
        def polygon(identity, points, height, sight):
            return {"id": identity, "shape": "polygon", "polygon_cm": points, "height_cm": wire(height),
                    "blocks_movement": True, "blocks_sight": sight}
        blockers = [
            polygon("wall", box(-100, -1000, 100, 1000), 200, True),
            polygon("fence", box(-1000, 2000, 1000, 2020), 100, False),
            {"id": "tree", "shape": "cylinder", "center_cm": vec(-2000, -3000), "radius_cm": wire(50), "height_cm": wire(200),
             "blocks_movement": True, "blocks_sight": True},
            {"id": "high_tree", "shape": "cylinder", "center_cm": vec(2200, 300, 150), "radius_cm": wire(50),
             "height_cm": wire(200), "blocks_movement": True, "blocks_sight": True},
        ]
        elevation = {
            "level_step_cm": wire(150),
            "plateaus": [{"id": "hill_1", "level": 1, "polygon_cm": box(2000, -500, 3000, 500, 150)},
                         {"id": "hill_2", "level": 2, "polygon_cm": box(2400, -200, 2800, 200, 300)}],
            "ramps": [{"id": "ramp_0", "lower_level": 0,
                       "polygon_cm": [vec(1700, 100), vec(1700, -100), vec(2000, -100, 150), vec(2000, 100, 150)]}],
        }
        cls.geometry = world.Geometry({"geometry": {
            "surfaces": [{"id": "ground", "min_cm": vec(-5000, -5000), "max_cm": vec(5000, 5000)}],
            "elevation": elevation, "blockers": blockers}})
        cls.radius = Fraction(40)

    def test_wall_blocks_walking_and_sight(self):
        g = self.geometry
        self.assertFalse(g.legal((0, 0, 0), self.radius))
        self.assertFalse(g.legal((-139, 0, 0), self.radius))
        self.assertTrue(g.legal((-140, 0, 0), self.radius))
        self.assertFalse(g.sight_clear((-500, 0, 0), (500, 0, 0)))
        self.assertTrue(g.sight_clear((-500, 1500, 0), (500, 1500, 0)))

    def test_fence_blocks_walking_but_not_sight(self):
        g = self.geometry
        self.assertFalse(g.legal((0, 2010, 0), self.radius))
        self.assertFalse(g.legal((0, 1961, 0), self.radius))
        self.assertTrue(g.legal((0, 1960, 0), self.radius))
        self.assertTrue(g.sight_clear((0, 1500, 0), (0, 2500, 0)))

    def test_tree_blocks_walking_within_body_radius_and_sight_through_its_trunk(self):
        g = self.geometry
        self.assertFalse(g.legal((-2000, -3000 + 89, 0), self.radius))
        self.assertTrue(g.legal((-2000, -3000 + 90, 0), self.radius))
        self.assertFalse(g.sight_clear((-2300, -3000, 0), (-1700, -3000, 0)))
        self.assertTrue(g.sight_clear((-2300, -2950, 0), (-1700, -2950, 0)))

    def test_standing_follows_the_level_under_the_whole_body(self):
        g = self.geometry
        self.assertTrue(g.legal((2500, -400, 150), self.radius))
        self.assertFalse(g.legal((2500, -400, 0), self.radius))
        # A body across a cliff edge stands on no single level.
        self.assertFalse(g.legal((2010, -400, 150), self.radius))
        self.assertFalse(g.legal((1990, -400, 0), self.radius))
        self.assertEqual([g.terrain.tier(p) for p in ((1000, 0), (1850, 0), (2100, 0), (2600, 0))], [0, 1, 2, 4])

    def test_high_ground_sees_low_ground_not_the_reverse(self):
        clear = self.geometry.sight_clear
        ground, ramp, low, high = (1000, 0, 0), (1850, 0, 75), (2100, 0, 150), (2600, 0, 300)
        self.assertEqual((clear(ground, low), clear(low, ground)), (False, True))
        self.assertEqual((clear(ground, ramp), clear(ramp, ground)), (False, True))
        self.assertEqual((clear(ramp, low), clear(low, ramp)), (False, True))
        self.assertEqual((clear(low, high), clear(high, low)), (False, True))
        self.assertTrue(clear(high, (1500, 0, 0)))
        # A plateau between two ground points hides them from each other.
        self.assertFalse(clear((1500, 0, 0), (3500, 0, 0)))
        self.assertTrue(clear((1500, 700, 0), (3500, 700, 0)))

    def test_obstacles_hide_only_from_their_level_and_below(self):
        clear = self.geometry.sight_clear
        self.assertFalse(clear((1000, 0, 0), (-300, 0, 0)))
        self.assertTrue(clear((2100, 0, 150), (-300, 0, 0)))
        self.assertFalse(clear((2050, 300, 150), (2350, 300, 150)))
        self.assertTrue(clear((2500, 150, 300), (2100, 380, 150)))


class AuthorContractUnknownFields(unittest.TestCase):
    """The producer checks each authored object kind with its own field list; an unknown field at every one of them is
    refused. Direct contract calls, no import: the full-import refusal keeps one case per schema node kind."""

    def test_every_author_object_refuses_an_unconsumed_field(self):
        source = content.parse_json((content.ROOT / content.WORLD_SOURCE).read_bytes(), "world author")
        world.validate_author_contract(deepcopy(source))
        blockers = source["geometry"]["blockers"]
        polygon = next(i for i, row in enumerate(blockers) if row["shape"] == "polygon")
        cylinder = next(i for i, row in enumerate(blockers) if row["shape"] == "cylinder")
        place = next(i for i, row in enumerate(source["anchors"]) if "members" not in row)
        camp = next(i for i, row in enumerate(source["anchors"]) if len(row.get("members", ())) == 2)
        for path in ((), ("approval",), ("geometry",), ("geometry", "surfaces", 0), ("geometry", "surfaces", 0, "min_cm"),
                     ("geometry", "surfaces", 0, "max_cm"), ("geometry", "blockers", polygon),
                     ("geometry", "blockers", polygon, "polygon_cm", 3), ("geometry", "blockers", cylinder),
                     ("geometry", "blockers", cylinder, "center_cm"), ("placement",), ("placement", "spawn_offsets_cm", 6),
                     ("anchors", place), ("anchors", place, "position_cm"), ("anchors", camp),
                     ("anchors", camp, "members", 1), ("anchors", camp, "members", 1, "position_cm")):
            with self.subTest(path=path):
                author = deepcopy(source)
                ApprovedDemoRefusals.at(author, path)["unconsumed_value"] = 1
                with self.assertRaisesRegex(content.ContentError, "AuthorField.*unconsumed_value"):
                    world.validate_author_contract(author)


class ApprovedDemoRefusals(unittest.TestCase):
    def setUp(self):
        base = content.ROOT / "Artifacts" / "DemoContent"
        base.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(prefix="approved-test-", dir=base)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        for relative in (*content.AUTHOR_INPUT_PATHS, content.WORLD_SOURCE, content.WORLD_SCHEMA_SOURCE):
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(content.ROOT / relative, target)
        self.world = world_sync.synchronize(self.root)
        self.content = content_sync.synchronize(self.root)
        self.originals = {**self.world.outputs, **self.content.outputs}

    def edit(self, relative, change):
        path = self.root / relative
        data = json.loads(path.read_bytes(), parse_float=Decimal)
        change(data)
        # These mutations only alter integer geometry or string metadata.
        path.write_bytes(encode(data))

    def unchanged(self):
        for relative, expected in self.originals.items():
            self.assertEqual((self.root / relative).read_bytes(), expected, str(relative))

    def editable_schema_accepts(self):
        schema = content.parse_json((self.root / content.WORLD_SCHEMA_SOURCE).read_bytes(), "test schema")
        author = content.parse_json((self.root / content.WORLD_SOURCE).read_bytes(), "test author")
        content.AuthorSchema(schema).validate(author)

    @staticmethod
    def at(value, path):
        for key in path:
            value = value[key]
        return value

    def assert_native_author_refusal(self, pattern):
        self.editable_schema_accepts()
        for synchronize in (world_sync.synchronize, content_sync.synchronize):
            with self.assertRaisesRegex(content.ContentError, pattern):
                synchronize(self.root)
            self.unchanged()

    def test_schema_and_author_cannot_add_unconsumed_friction(self):
        self.edit(content.WORLD_SCHEMA_SOURCE,
                  lambda d: d["properties"]["geometry"]["properties"].update(friction={"type": "integer"}))
        self.edit(content.WORLD_SOURCE, lambda d: d["geometry"].update(friction=1))
        self.assert_native_author_refusal("AuthorField.*friction")

    def test_schema_and_author_cannot_change_centimeters_to_meters(self):
        self.edit(content.WORLD_SCHEMA_SOURCE,
                  lambda d: d["properties"]["coordinate_unit"].update(const="m"))
        self.edit(content.WORLD_SOURCE, lambda d: d.update(coordinate_unit="m"))
        self.assert_native_author_refusal("WorldUnit")

    def test_coordinated_schema_extensions_reject_every_nested_author_object_kind(self):
        source_raw = (self.root / content.WORLD_SOURCE).read_bytes()
        schema_raw = (self.root / content.WORLD_SCHEMA_SOURCE).read_bytes()
        # One case per kind of schema node (root object, nested object, array item, each oneOf branch, the shared
        # vector definition); the refusal is the same author-field check at every depth, so more rows repeat it.
        source = json.loads(source_raw)
        cylinder = next(i for i, row in enumerate(source["geometry"]["blockers"]) if row["shape"] == "cylinder")
        place = next(i for i, row in enumerate(source["anchors"]) if "members" not in row)
        camp = next(i for i, row in enumerate(source["anchors"]) if len(row.get("members", ())) == 2)
        cases = (
            ((), ()),
            (("geometry", "surfaces", 0), ("properties", "geometry", "properties", "surfaces", "items")),
            (("geometry", "blockers", cylinder), ("$defs", "blocker", "oneOf", 1)),
            (("anchors", place, "position_cm"), ("$defs", "vector")),
            (("anchors", camp, "members", 1), ("$defs", "anchor", "oneOf", 1, "properties", "members", "items")),
        )
        for author_path, schema_path in cases:
            with self.subTest(path=author_path):
                (self.root / content.WORLD_SOURCE).write_bytes(source_raw)
                (self.root / content.WORLD_SCHEMA_SOURCE).write_bytes(schema_raw)
                self.edit(content.WORLD_SCHEMA_SOURCE,
                          lambda d: self.at(d, schema_path)["properties"].update(unconsumed_value={"type": "integer"}))
                self.edit(content.WORLD_SOURCE, lambda d: self.at(d, author_path).update(unconsumed_value=1))
                self.assert_native_author_refusal("AuthorField.*unconsumed_value")

    def test_schema_cannot_coerce_boolean_or_decimal_author_coordinates(self):
        source_raw = (self.root / content.WORLD_SOURCE).read_bytes()
        schema_raw = (self.root / content.WORLD_SCHEMA_SOURCE).read_bytes()
        for token in (b"true", b"1.0", b"1e0"):
            with self.subTest(token=token):
                (self.root / content.WORLD_SOURCE).write_bytes(source_raw)
                (self.root / content.WORLD_SCHEMA_SOURCE).write_bytes(schema_raw)
                self.edit(content.WORLD_SCHEMA_SOURCE, lambda d: d["$defs"].update(
                    coordinate={"oneOf": [{"type": "number"}, {"type": "boolean"}]}))
                self.edit(content.WORLD_SOURCE,
                          lambda d: d["anchors"][9]["position_cm"].update(x="COORDINATE_TOKEN"))
                path = self.root / content.WORLD_SOURCE
                path.write_bytes(path.read_bytes().replace(b'"COORDINATE_TOKEN"', token))
                self.assert_native_author_refusal("IntegerType")

    def test_schema_descriptions_only_change_schema_provenance(self):
        self.edit(content.WORLD_SCHEMA_SOURCE, lambda d: d.update(
            title="Documented Demo world contract", description="Metadata only; it cannot expand producer semantics."))
        self.edit(content.WORLD_SCHEMA_SOURCE,
                  lambda d: d["$defs"]["vector"].update(description="Integer centimeter coordinates."))
        self.editable_schema_accepts()
        updated = world_sync.synchronize(self.root)
        expected, actual = content.canonical(self.world.world), content.canonical(updated.world)
        self.assertNotEqual(expected["author_schema_sha256"], actual["author_schema_sha256"])
        actual["author_schema_sha256"] = expected["author_schema_sha256"]
        self.assertEqual(actual, expected)
        updated_content = content_sync.synchronize(self.root)
        self.assertEqual(updated_content.outputs[content.CONTENT_OUTPUT], self.content.outputs[content.CONTENT_OUTPUT])
        self.assertNotEqual(updated_content.digest, self.content.digest)
        self.assertEqual(content.verify_runtime_files(self.root, updated_content.digest), updated_content.digest)

    def test_bad_full_world_input_refuses_without_partial_outputs(self):
        original = (self.root / content.WORLD_SOURCE).read_bytes()
        source = json.loads(original)
        rows = source["geometry"]["blockers"]
        # Cases pick their rows by kind, not by a position in one layout's file.
        # The boundary wall reaches farthest from the centre; without it a ground edge stays open.
        reach = lambda row: max(abs(v[a]) for v in row["polygon_cm"] for a in "xy")
        wall = max((i for i, row in enumerate(rows) if row["shape"] == "polygon"), key=lambda i: reach(rows[i]))
        polygon = next(i for i, row in enumerate(rows) if row["shape"] == "polygon")
        tree = next(i for i, row in enumerate(rows) if row["shape"] == "cylinder")
        capture = next(row for row in source["anchors"] if row["kind"] == "Capture")["position_cm"]
        for change in (
            lambda d: d.update(coordinate_unit="m"),
            lambda d: d["approval"].update(status="pending"),
            lambda d: d["geometry"]["blockers"].pop(wall),
            lambda d: d["geometry"]["blockers"][polygon]["polygon_cm"].reverse(),
            lambda d: d["geometry"]["blockers"][polygon]["polygon_cm"][0].update(z=1),
            lambda d: d["geometry"]["blockers"][polygon].update(height_cm=0),
            lambda d: d["geometry"]["blockers"][tree]["center_cm"].update(z=1),
            lambda d: d["geometry"]["blockers"][tree].update(radius_cm=0),
            lambda d: d["geometry"]["blockers"][tree].update(shape="polygon"),
            lambda d: d["geometry"]["blockers"][tree]["center_cm"].update(x=capture["x"], y=capture["y"]),
            lambda d: d["placement"]["spawn_offsets_cm"][1].update(x=300),
            lambda d: d["placement"]["spawn_offsets_cm"][1].update(x=True),
            lambda d: d["parameter_refs"].append("UNAPPROVED.NumericDefault"),
            lambda d: d["geometry"]["surfaces"][0]["min_cm"].update(x=content.INT64_MIN),
        ):
            (self.root / content.WORLD_SOURCE).write_bytes(original)
            with self.subTest(change=change):
                self.edit(content.WORLD_SOURCE, change)
                with self.assertRaises(content.ContentError):
                    world_sync.synchronize(self.root)
                self.unchanged()
        (self.root / content.WORLD_SOURCE).write_bytes(original)

    def test_unknown_schema_and_old_world_projection_are_rejected(self):
        self.edit(content.WORLD_SCHEMA_SOURCE, lambda d: d.update(unknown_keyword=True))
        with self.assertRaisesRegex(content.ContentError, "InvalidSchema"):
            world_sync.synchronize(self.root)
        self.unchanged()
        raw = json.loads(self.world.outputs[content.WORLD_OUTPUT])
        raw["projection_revision"] = 1
        with self.assertRaises(content.ContentError):
            world.validate_payload(encode(content.canonical(raw)))

    def test_root_refuses_self_consistent_but_unauthored_world_bytes(self):
        data = json.loads(self.world.outputs[content.WORLD_OUTPUT])
        next(row for row in data["anchors"] if row["id"] == "s00")["position_cm"]["x"]["numerator"] += 1
        raw = encode(content.canonical(data))
        (self.root / content.WORLD_OUTPUT).write_bytes(raw)
        (self.root / content.WORLD_CONTRACT).write_bytes(world.world_contract_bytes(raw))
        with self.assertRaisesRegex(content.ContentError, "StaleWorldOutput"):
            content_sync.synchronize(self.root)
        for relative, expected in self.content.outputs.items():
            self.assertEqual((self.root / relative).read_bytes(), expected)

    def test_world_contract_write_failure_rolls_back_world_bundle(self):
        self.edit(content.WORLD_SOURCE, lambda d: d.update(layout_revision=3))
        compilation = world.compile_world(self.root)
        install = content_sync._install_output

        def fail(protected, raw):
            if protected.path == self.root / content.WORLD_CONTRACT:
                raise OSError("injected world contract failure")
            install(protected, raw)

        with patch.object(content_sync, "_install_output", side_effect=fail), self.assertRaises(OSError):
            content_sync.write_outputs(self.root, compilation)
        self.unchanged()
        self.assertFalse((self.root / content.DATA / ".compile.lock").exists())

    def test_world_and_content_generation_share_the_same_recovery_gate(self):
        with patch.object(content_sync, "_remove_lock", side_effect=OSError("injected world cleanup failure")):
            with self.assertRaisesRegex(content.ContentError, "RecoveryRequired"):
                content_sync.write_outputs(self.root, self.world)
        journal = json.loads((self.root / content.DATA / ".compile.lock").read_bytes())
        self.assertEqual(journal["bundle_kind"], "world")
        self.assertNotIn("expected_catalog_sha256", journal)
        self.assertEqual(journal["expected_world_sha256"], self.world.digest.hex())
        with self.assertRaisesRegex(content.ContentError, "CompilerBusy"):
            content_sync.write_outputs(self.root, self.content)
        with self.assertRaisesRegex(content.ContentError, "CompilerBusy"):
            world_sync.synchronize(self.root, check=True)
        self.unchanged()


if __name__ == "__main__":
    unittest.main()

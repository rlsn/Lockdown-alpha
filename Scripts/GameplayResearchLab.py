"""Lockdown Gameplay Research & Experimentation Lab.

Deterministic static analysis, interaction-graph extraction, Monte Carlo draft/shop
agency simulation, discrete-tick combat matchup simulation, and spatial PvPvE FFA
strategy tournament engine grounded directly in SourceData/Demo/content.json and
Content/Lockdown/Data/Demo/world.json.

All simulation outputs explicitly record commit SHA, catalog manifest SHA-256,
random seeds, sample sizes, bot information constraints, and calibration boundaries.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from dataclasses import dataclass, field
import hashlib
import json
import math
from pathlib import Path
import random
import statistics
import subprocess
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
UNREACHABLE_CAMPS_ISSUE_353 = frozenset({
    "e01", "h04", "h08", "h12", "l09", "l10", "l19", "authored_group_046"
})
ZERO_KILL_REACHABLE_CAMPS_ISSUE_353 = frozenset({
    "e03", "h01", "h03", "h05", "h06", "h07", "e06", "e07", "e08", "instance_LDLayoutPoint_305"
})

REACTIONS_SPEC = [
    {"id": "reaction_fog", "pair": ("fire", "water"), "name": "起雾", "ratio": 1.0, "ground": "puddle", "cc_sec": 0.0, "role": "zone_blind_obscure"},
    {"id": "reaction_overload", "pair": ("fire", "electric"), "name": "过载", "ratio": 1.5, "ground": "fire", "breach": 2, "aoe_cm": 200, "role": "aoe_burst_breach"},
    {"id": "reaction_melt", "pair": ("fire", "chill"), "name": "融化", "ratio": 2.0, "ground": "puddle", "cc_sec": 0.0, "role": "single_burst_amp"},
    {"id": "reaction_methane", "pair": ("fire", "rot"), "name": "沼气爆", "ratio": 1.5, "ground": "fire", "burn_stacks": 2, "aoe_cm": 250, "role": "aoe_dot_detonation"},
    {"id": "reaction_conduct", "pair": ("water", "electric"), "name": "导电", "ratio": 1.5, "ground": "electric_field", "slow": 0.30, "aoe_cm": 450, "role": "wide_chain_slow"},
    {"id": "reaction_freeze", "pair": ("water", "chill"), "name": "冻住", "ratio": 1.0, "ground": "ice", "cc_sec": 1.2, "role": "hard_root_ice_setup"},
    {"id": "reaction_ferment", "pair": ("water", "rot"), "name": "发酵", "ratio": 1.0, "ground": "miasma", "heal_cut": 0.60, "role": "anti_heal_attrition"},
    {"id": "reaction_shatter", "pair": ("electric", "chill"), "name": "碎冰", "ratio": 1.2, "ground": "ice", "skill_vuln": 0.50, "role": "skill_burst_window"},
    {"id": "reaction_corrode", "pair": ("electric", "rot"), "name": "腐蚀", "ratio": 1.2, "ground": "miasma", "breach": 5, "role": "max_armor_shred"},
    {"id": "reaction_frostbite", "pair": ("chill", "rot"), "name": "冻疮", "ratio": 1.1, "ground": "miasma", "as_slow": 0.40, "role": "anti_attack_speed_cripple"},
]

TIER3_SECONDARY_ELEMENT = {
    "fire": "electric",
    "water": "electric",
    "electric": "chill",
    "chill": "water",
    "rot": "fire",
}


def num_val(obj: Any, default: float = 0.0) -> float:
    if obj is None:
        return default
    if isinstance(obj, (int, float)) and not isinstance(obj, bool):
        return float(obj)
    if isinstance(obj, dict):
        if "numerator" in obj and "denominator" in obj:
            return float(obj["numerator"]) / float(obj["denominator"])
        if "value" in obj:
            return float(obj["value"])
    return default


def git_head(root: Path = ROOT) -> str:
    try:
        return subprocess.check_output(
            ["git", "--no-pager", "rev-parse", "HEAD"],
            cwd=str(root), encoding="utf-8", stderr=subprocess.DEVNULL
        ).strip()
    except Exception:
        return "unknown"


@dataclass
class CatalogModel:
    root: Path
    content: dict[str, Any]
    world: dict[str, Any]
    manifest_sha256: str
    commit_sha: str
    display_map: dict[str, dict[str, str]] = field(default_factory=dict)
    abilities_by_id: dict[str, dict[str, Any]] = field(default_factory=dict)
    cards_by_id: dict[str, dict[str, Any]] = field(default_factory=dict)
    stats_by_id: dict[str, dict[str, Any]] = field(default_factory=dict)
    monsters_by_id: dict[str, dict[str, Any]] = field(default_factory=dict)
    anchors_by_id: dict[str, dict[str, Any]] = field(default_factory=dict)

    @classmethod
    def load(cls, root: Path = ROOT) -> "CatalogModel":
        content_path = root / "SourceData" / "Demo" / "content.json"
        world_path = root / "Content" / "Lockdown" / "Data" / "Demo" / "world.json"
        manifest_path = root / "Content" / "Lockdown" / "Data" / "Demo" / "manifest.json"
        content = json.loads(content_path.read_text(encoding="utf-8"))
        world = json.loads(world_path.read_text(encoding="utf-8"))
        manifest_sha = hashlib.sha256(manifest_path.read_bytes()).hexdigest() if manifest_path.is_file() else "missing"
        model = cls(
            root=root,
            content=content,
            world=world,
            manifest_sha256=manifest_sha,
            commit_sha=git_head(root),
        )
        for row in content.get("display", []):
            model.display_map[row["id"]] = {
                "name": row.get("name_text", row["id"]),
                "desc": row.get("description_text", ""),
                "namespace": row.get("namespace", ""),
            }
        for ab in content["abilities"]:
            model.abilities_by_id[ab["id"]] = ab
        for card in content["cards"]:
            model.cards_by_id[card["id"]] = card
        for st in content["stats"]:
            model.stats_by_id[st["id"]] = st
        for mon in content["camps"]["monsters"]:
            model.monsters_by_id[mon["id"]] = mon
        for anc in world["anchors"]:
            model.anchors_by_id[anc["id"]] = anc
        return model

    def name_of(self, item_id: str) -> str:
        return self.display_map.get(item_id, {}).get("name", item_id)

    def param(self, key: str, default: float = 0.0) -> float:
        return num_val(self.content["parameters"].get(key), default)

    def anchor_xy(self, anchor_id: str) -> tuple[float, float, float]:
        anc = self.anchors_by_id.get(anchor_id)
        if not anc:
            return (0.0, 0.0, 0.0)
        pos = anc["position_cm"]
        return (num_val(pos["x"]), num_val(pos["y"]), num_val(pos["z"]))

    def build_cards_pool(self) -> list[dict[str, Any]]:
        return [c for c in self.content["cards"] if c["form"] != "medal"]

    def medals_pool(self) -> list[dict[str, Any]]:
        return [c for c in self.content["cards"] if c["form"] == "medal"]

    def card_weight(self, card: dict[str, Any]) -> int:
        if card["form"] == "medal":
            return 0
        return int(card.get("drop_weight", 100))

    def xp_for_level(self, level: int) -> int:
        thresholds = self.content["progression"]["xp_thresholds"]
        idx = max(0, min(level - 1, len(thresholds) - 1))
        return int(thresholds[idx])

    def level_for_xp(self, xp: float) -> int:
        thresholds = self.content["progression"]["xp_thresholds"]
        lvl = 1
        for idx, th in enumerate(thresholds):
            if xp >= th:
                lvl = idx + 1
        return lvl

    def total_stat_points_from_xp(self, xp: float) -> int:
        lvl = self.level_for_xp(xp)
        pts = int(self.param("GROWTH.OpeningPoints", 3)) + (lvl - 1) * int(self.param("GROWTH.PointsPerLevel", 3))
        max_xp = self.xp_for_level(16)
        if xp > max_xp:
            overflow_step = int(self.param("GROWTH.MaxLevelPointsXP", 400))
            pts += int((xp - max_xp) // overflow_step) * int(self.param("GROWTH.MaxLevelPoints", 2))
        return pts

    def card_capacity_at_level(self, level: int) -> int:
        base = int(self.param("CARDS.CapacityBase", 6))
        per = int(self.param("CARDS.LevelsPerSlot", 3))
        return base + max(0, (level - 1) // per)


# ---------------------------------------------------------------------------
# 1. Machine-Readable Gameplay Interaction Graph
# ---------------------------------------------------------------------------

ABILITY_OP_PRODUCES = {
    "damage": ["skill_hit", "damage_instance"],
    "zone_damage": ["skill_hit", "damage_instance", "area"],
    "ground_impact": ["skill_hit", "damage_instance", "area"],
    "self_area": ["skill_hit", "damage_instance", "area"],
    "channel_cone": ["skill_hit", "damage_instance", "area"],
    "frenzy_waves": ["skill_hit", "damage_instance", "area"],
    "chain_bounce": ["skill_hit", "damage_instance"],
    "knockback": ["knockback", "wall_impact"],
    "pull": ["knockback", "control"],
    "grab_line": ["control"],
    "root": ["control"],
    "stun": ["control"],
    "taunt": ["control", "stance"],
    "parry_stance": ["stance", "shield"],
    "brace": ["stance", "shield"],
    "shield": ["shield", "shield_break"],
    "dash": ["dash"],
    "dash_contact": ["dash_contact", "skill_hit"],
    "empower_basic": ["basic_hit", "combo3"],
    "burn": ["element_apply", "dot_burn"],
    "zone_burn": ["element_apply", "dot_burn", "ground_state"],
    "wet": ["element_apply"],
    "electrify": ["element_apply"],
    "chill": ["element_apply", "slow"],
    "rot": ["element_apply", "dot_rot", "anti_heal"],
    "ground_state": ["ground_state"],
    "zone": ["ground_state", "area"],
    "contact_trap": ["ground_state", "construct"],
    "fan": ["ground_state", "construct", "knockback"],
    "bag": ["ground_state", "construct", "area"],
    "cordon_line": ["ground_state", "slow"],
    "breach": ["armor_shred"],
    "slow": ["slow"],
    "zone_slow": ["slow"],
}

CARD_OP_EDGES: dict[str, tuple[list[str], list[str], str]] = {
    "combo_momentum": (["combo3"], ["momentum"], "resource_gain"),
    "basic_cooldown_cut": (["basic_hit"], ["cooldown_refund"], "cooldown_loop"),
    "combo_skill_hit": (["combo3"], ["skill_hit", "momentum"], "bridge_trigger"),
    "combo_knockback": (["combo3"], ["knockback", "wall_impact"], "displacement"),
    "basic_projectile": (["basic_hit"], ["range_projectile"], "form_transform"),
    "combo_double": (["combo3"], ["basic_hit"], "hit_multiplication"),
    "combo_sweep": (["combo3"], ["area"], "aoe_transform"),
    "combo_cast": (["combo3"], ["skill_cast", "skill_hit"], "auto_cast_bridge"),
    "overcap_extra_hit": (["attack_speed", "basic_hit"], ["basic_hit"], "hit_multiplication"),
    "speed_factor": (["attack_speed", "move_speed"], ["damage_amp"], "stat_conversion"),
    "basic_ap_rider": (["basic_hit", "ap_stat"], ["damage_instance"], "hybrid_scaling"),
    "focus_attack_speed": (["basic_hit"], ["attack_speed"], "ramp_loop"),
    "hit_growth": (["basic_hit", "perfect_dodge"], ["permanent_stats", "attack_speed", "move_speed"], "permanent_growth"),
    "breach": (["basic_hit"], ["armor_shred", "damage_amp"], "mitigation_shred"),
    "hit_haste_other": (["skill_hit", "dash_contact"], ["cooldown_refund"], "cooldown_loop"),
    "skill_hit_momentum": (["skill_hit"], ["momentum"], "resource_gain"),
    "takedown_reset": (["kill_takedown"], ["cooldown_refund", "momentum"], "reset_snowball"),
    "setup_mark": (["skill_hit"], ["damage_amp"], "combo_sequencing"),
    "control_amp": (["control"], ["damage_amp"], "cc_payoff"),
    "auto_recast": (["skill_cast"], ["skill_cast", "skill_hit"], "cast_multiplication"),
    "reshape": (["skill_cast"], ["area", "range_projectile", "control"], "geometry_mod"),
    "cooldown_cost": (["skill_cast", "dash", "momentum"], ["cooldown_refund"], "resource_for_cd"),
    "skill_damage_factor": (["skill_hit"], ["damage_amp"], "damage_multiplier"),
    "hp_cost": (["skill_cast", "max_hp"], ["low_hp", "damage_amp", "momentum_bypass"], "blood_magic"),
    "skill_ad_rider": (["skill_hit", "ad_stat"], ["damage_instance"], "hybrid_scaling"),
    "shield_spend": (["skill_hit", "shield"], ["damage_instance"], "shield_to_damage"),
    "skill_lifesteal": (["skill_hit", "damage_instance"], ["heal"], "sustain_loop"),
    "idle_ready_amp": (["idle"], ["damage_amp"], "burst_window"),
    "skill_growth": (["skill_hit"], ["permanent_stats", "damage_amp"], "permanent_growth"),
    "recent_cast_amp": (["skill_cast"], ["damage_amp"], "combo_sequencing"),
    "reaction_haste": (["reaction"], ["cooldown_refund"], "reaction_cd_loop"),
    "dash_contact_reset": (["dash_contact"], ["cooldown_refund", "momentum"], "dash_reset_loop"),
    "empower_basic": (["dash"], ["basic_hit", "damage_instance"], "dash_weave"),
    "dodge_free_cast": (["perfect_dodge"], ["momentum_bypass"], "dodge_payoff"),
    "dash_split": (["dash"], ["dash", "dash_contact"], "mobility_multiplication"),
    "dash_phase": (["dash"], ["dash_contact", "basic_hit"], "bridge_trigger"),
    "dash_ram": (["dash"], ["dash_contact", "knockback", "wall_impact"], "displacement"),
    "dash_ice": (["dash"], ["ground_state", "ice_surface"], "ground_creation"),
    "ground_relocate": (["dash", "ground_state"], ["ground_state"], "zone_reposition"),
    "thorns": (["damaged", "shield"], ["damage_instance"], "retaliation"),
    "last_stand": (["low_hp", "damaged"], ["survival_gate"], "anti_burst"),
    "reaction_momentum": (["reaction"], ["momentum"], "reaction_resource_loop"),
    "reaction_element": (["reaction"], ["element_apply", "reaction"], "recursive_reaction"),
    "shield_burst_break": (["shield_break", "max_hp"], ["damage_instance", "area"], "shield_payoff"),
    "triggered_shield": (["damaged"], ["shield", "shield_break"], "defensive_trigger"),
    "knockback_impact": (["knockback", "wall_impact"], ["control", "damage_instance"], "terrain_payoff"),
    "skill_crit": (["skill_hit", "crit_stat"], ["damage_amp"], "crit_unlock"),
    "lifesteal_skills": (["skill_hit", "heal"], ["heal"], "sustain_bridge"),
    "overdrive_short": (["momentum"], ["overdrive", "damage_amp"], "overdrive_enabler"),
    "double_stall": (["ground_state", "construct"], ["ground_state", "construct"], "zone_multiplication"),
    "armor_to_hp": (["armor_stat"], ["max_hp"], "stat_conversion"),
    "overheal_shield": (["heal", "dot_burn"], ["shield", "shield_break"], "heal_to_shield_bridge"),
    "missing_hp_attack_speed": (["low_hp"], ["attack_speed"], "berserk_loop"),
    "haste_momentum": (["haste_stat", "skill_hit"], ["momentum"], "resource_gain"),
    "lifesteal": (["basic_hit"], ["heal"], "sustain_loop"),
    "heal_growth": (["heal"], ["permanent_stats", "max_hp"], "permanent_growth"),
    "kill_growth": (["kill_takedown"], ["permanent_stats", "ad_stat", "ap_stat"], "permanent_growth"),
    "control_growth": (["damaged", "control"], ["permanent_stats", "armor_stat"], "permanent_growth"),
    "idle_charge": (["idle"], ["damage_amp"], "burst_window"),
    "ground_growth": (["ground_state"], ["damage_amp"], "zone_payoff"),
    "tally": (["basic_hit", "skill_hit"], ["damage_instance"], "hit_payoff"),
    "element_aura": (["damaged"], ["element_apply"], "defensive_primer"),
    "element_duration": (["element_apply"], ["element_apply"], "status_extension"),
    "reaction_factor": (["reaction"], ["damage_amp"], "reaction_amp"),
    "burn_cap": (["dot_burn"], ["dot_burn", "damage_instance"], "dot_cap_unlock"),
    "brain_speed": (["ap_stat", "haste_stat", "basic_hit"], ["attack_speed", "damage_instance"], "hybrid_scaling"),
    "spellblade_echo": (["skill_cast", "basic_hit"], ["area", "element_apply", "damage_instance"], "bridge_trigger"),
    "heavy_mallet": (["attack_speed", "combo3"], ["damage_amp", "crit_stat", "knockback"], "anti_as_conversion"),
    "projectile_flight_amp": (["range_projectile", "skill_hit"], ["damage_amp"], "distance_payoff"),
    "eagle_stance": (["idle"], ["range_projectile"], "stance_range"),
    "runaway_train": (["dash_contact", "move_speed", "wall_impact"], ["damage_instance", "cooldown_refund"], "dash_wall_loop"),
    "phantom_molt": (["perfect_dodge"], ["area", "damage_instance", "slow"], "dodge_payoff"),
    "skate_blade": (["ice_surface", "knockback"], ["damage_instance", "attack_speed", "move_speed"], "narrow_surface_payoff"),
    "tonnage_crush": (["max_hp", "combo3", "area"], ["damage_instance"], "tank_to_damage"),
    "hulking_presence": (["max_hp"], ["area"], "tank_geometry"),
    "iron_hedgehog": (["armor_stat", "damaged", "stance"], ["damage_instance"], "retaliation_amp"),
    "reactive_plating": (["shield"], ["damage_instance", "area", "crit_stat"], "shield_stack_loop"),
    "desperado": (["low_hp", "heal"], ["damage_amp", "heal"], "berserk_sustain_loop"),
    "fuel_the_fire": (["skill_hit", "dot_burn", "dot_rot"], ["damage_instance"], "dot_detonator"),
    "rogue_works": (["construct", "ground_state", "max_hp"], ["damage_instance", "element_apply"], "narrow_construct_payoff"),
    "martial_scholar": (["ad_stat", "ap_stat", "skill_hit", "basic_hit"], ["momentum", "armor_shred", "damage_amp"], "hybrid_weave_loop"),
    "skill_element": (["skill_hit", "combo3"], ["element_apply"], "medal_primer"),
}


def extract_interaction_graph(catalog: CatalogModel) -> dict[str, Any]:
    nodes: list[dict[str, Any]] = []
    edges: list[dict[str, Any]] = []
    bus_producers: dict[str, list[str]] = defaultdict(list)
    bus_consumers: dict[str, list[str]] = defaultdict(list)

    # Abilities + rank branches
    elemental_base_abilities = 0
    elemental_branch_abilities = set()
    for ab in catalog.content["abilities"]:
        aid = ab["id"]
        elem = ab.get("element", "none")
        if elem != "none":
            elemental_base_abilities += 1
        ops = [e["op"] for e in ab.get("effects", [])]
        produces = {"skill_cast"}
        for op in ops:
            produces.update(ABILITY_OP_PRODUCES.get(op, []))
        if elem != "none":
            produces.add("element_apply")
            if elem == "fire":
                produces.add("dot_burn")
            elif elem == "rot":
                produces.add("dot_rot")
        branch_elements = []
        for rname, rbody in ab.get("ranks", {}).items():
            for patch in rbody.get("patches", []):
                eff = patch.get("effect")
                if isinstance(eff, dict):
                    bop = eff.get("op", "")
                    produces.update(ABILITY_OP_PRODUCES.get(bop, []))
                    if bop in ("burn", "wet", "electrify", "chill", "rot"):
                        branch_elements.append(f"{rname}:{bop}")
                        elemental_branch_abilities.add(aid)
        nodes.append({
            "id": aid,
            "name": catalog.name_of(aid),
            "kind": "ability",
            "opening": bool(ab.get("opening")),
            "base_element": elem,
            "branch_elements": branch_elements,
            "cooldown_s": num_val(ab.get("cooldown")),
            "windup_s": num_val(ab.get("windup")),
            "momentum_cost": num_val(ab.get("momentum_cost")),
            "ops": ops,
            "produces": sorted(produces),
        })
        for bus in sorted(produces):
            bus_producers[bus].append(aid)
            edges.append({"source": aid, "target": f"bus:{bus}", "relation": "produces", "mechanism": ",".join(ops)})

    # Cards & Medals
    vocab_mismatches = []
    for card in catalog.content["cards"]:
        cid = card["id"]
        ops = [e["op"] for e in card.get("effects", [])]
        consumes: set[str] = set()
        produces: set[str] = set()
        mechanisms: list[str] = []
        for op in ops:
            c_in, c_out, mech = CARD_OP_EDGES.get(op, ([], [], "custom"))
            consumes.update(c_in)
            produces.update(c_out)
            mechanisms.append(mech)
        vocab = list(card.get("vocab", []))
        # Check whether vocab omits a primary mechanical dependency or advertises a tag absent from mechanics
        if "ice_surface" in consumes and "element" not in vocab:
            vocab_mismatches.append({
                "card_id": cid,
                "name": catalog.name_of(cid),
                "vocab": vocab,
                "issue": "Requires ice_surface (Freeze reaction or card_ice_trail), but vocab uses broad 'ground' without 'element', biasing toward non-ice ground skills.",
            })
        if "construct" in consumes and "ground" in vocab and len(vocab) >= 1:
            vocab_mismatches.append({
                "card_id": cid,
                "name": catalog.name_of(cid),
                "vocab": vocab,
                "issue": "Hard-coded to 3 specific actives (school_bag, desk_fan, flyer_trap) yet shares generic 'ground'/'max_hp'/'element' vocab, polluting shop slots for unrelated builds.",
            })
        if cid == "card_solid_punch":
            vocab_mismatches.append({
                "card_id": cid,
                "name": catalog.name_of(cid),
                "vocab": vocab,
                "issue": "Bridge catalyst: turns every 3rd basic attack into a synthetic skill_hit, unlocking skill_hit cards for pure basic-attack builds.",
            })

        nodes.append({
            "id": cid,
            "name": catalog.name_of(cid),
            "kind": "medal" if card["form"] == "medal" else "card",
            "form": card["form"],
            "card_type": card["type"],
            "drop_weight": catalog.card_weight(card),
            "vocab": vocab,
            "ops": ops,
            "consumes": sorted(consumes),
            "produces": sorted(produces),
            "mechanisms": mechanisms,
        })
        for bus in sorted(consumes):
            bus_consumers[bus].append(cid)
            edges.append({"source": f"bus:{bus}", "target": cid, "relation": "triggers_or_scales", "mechanism": ",".join(ops)})
        for bus in sorted(produces):
            bus_producers[bus].append(cid)
            edges.append({"source": cid, "target": f"bus:{bus}", "relation": "produces", "mechanism": ",".join(ops)})

    # Stats & Keystones
    keystone_specs = [
        ("stat_physique", "keystone_physique", ["max_hp", "heal"], ["shield", "shield_break"], "Periodic 12% MaxHP shield + 50% overheal-to-shield"),
        ("stat_thick_skin", "keystone_thick_skin", ["armor_stat", "damaged"], ["damage_instance", "knockback"], "Flat damage guard + bleed reflect on heavy hit + knockback resist"),
        ("stat_strength", "keystone_strength", ["ad_stat", "crit_stat"], ["damage_amp"], "+14% universal damage multiplier"),
        ("stat_brains", "keystone_brains", ["ap_stat", "haste_stat", "skill_cast", "reaction"], ["cooldown_refund", "momentum"], "1s CD cut + 30% momentum refund + 15 reaction momentum"),
        ("stat_agility", "keystone_agility", ["attack_speed", "move_speed", "dash"], ["cooldown_refund", "basic_hit", "move_speed"], "40% dash CD cut + post-dash speed & empowered basic"),
    ]
    for sid, kid, k_in, k_out, desc in keystone_specs:
        nodes.append({"id": kid, "name": f"{catalog.name_of(sid)}关键石(18点)", "kind": "keystone", "consumes": k_in, "produces": k_out, "desc": desc})
        for bus in k_in:
            bus_consumers[bus].append(kid)
            edges.append({"source": f"bus:{bus}", "target": kid, "relation": "triggers_or_scales", "mechanism": "keystone_18pt"})
        for bus in k_out:
            bus_producers[bus].append(kid)
            edges.append({"source": kid, "target": f"bus:{bus}", "relation": "produces", "mechanism": "keystone_18pt"})

    # Reactions
    for rx in REACTIONS_SPEC:
        rid = rx["id"]
        r_out = ["reaction", "damage_instance", "momentum", "ground_state"]
        if rx["ground"] == "ice":
            r_out.append("ice_surface")
        if rx.get("cc_sec", 0) > 0:
            r_out.append("control")
        if rx.get("breach", 0) > 0:
            r_out.append("armor_shred")
        nodes.append({"id": rid, "name": rx["name"], "kind": "reaction", "pair": list(rx["pair"]), "ground": rx["ground"], "produces": r_out, "role": rx["role"]})
        edges.append({"source": "bus:element_apply", "target": rid, "relation": "triggers_or_scales", "mechanism": "+".join(rx["pair"])})
        for bus in r_out:
            bus_producers[bus].append(rid)
            edges.append({"source": rid, "target": f"bus:{bus}", "relation": "produces", "mechanism": rx["ground"]})

    # Identify closed feedback loops & structural bottlenecks
    closed_loops = [
        {
            "id": "loop_cd_engine",
            "name": "技能冷却永动环 (Skill Cooldown Engine)",
            "chain": ["skill_cast", "skill_hit", "card_next_up (-1.0s other CDs) / keystone_brains (-1.0s)", "card_encore (0.6x free recast)", "skill_cast"],
            "classification": "Bounded Positive Feedback",
            "bound_mechanism": "SKILL.CooldownFloorSec = 0.3s + windups (0.15-0.25s) + momentum: every release pays its momentum_cost (25-35) and is refused below it (LDCombatActions.cpp:1593-1594/1716-1717, LDCombatSources.cpp:280).",
            "design_note": "card_again_now (-35% CD, +50% momentum cost) trades cooldown for momentum; because casting is momentum-gated (income ~12.5/s in combat), its downside is real. CONTENT_AND_BALANCE.md:70 '技能不耗劲' refers to camp monsters only.",
        },
        {
            "id": "loop_combo_cast_bridge",
            "name": "普攻三段触发技能桥接环 (Combo3 -> Auto Q & Synthetic Skill Hit)",
            "chain": ["basic_hit x3 -> combo3", "card_both_hands (+extra combo3 strikes)", "card_fist_skill (45% free Q cast) + card_solid_punch (synthetic skill_hit)", "card_punch_think (-0.5s CD) + card_next_up (-1.0s CD)"],
            "classification": "High-Roll Emergent Synergy",
            "bound_mechanism": "card_fist_skill has drop_weight=6 (0.086% per slot!), making the most exciting hybrid loop nearly unobtainable in organic matches.",
        },
        {
            "id": "loop_blood_shield_plating",
            "name": "血魔过量治疗护盾迸射环 (Blood Price -> Lifesteal -> Overfed Shield -> Reactive Plating)",
            "chain": ["card_blood_price (pay HP, +20% dmg)", "low_hp -> card_desperado (+dmg/+heal) & card_berserk (+AS)", "card_punch_heal / card_skill_drain -> heal", "card_overfed / keystone_physique -> shield gain", "card_reactive_plating (35% shield AoE burst) + card_grow_fat (+18 permanent MaxHP per 100 heal)"],
            "classification": "Unbounded Permanent Scaling + Sustain Tank",
            "bound_mechanism": "Ferment reaction (60% heal cut) or Rot (25% heal cut), BUT 18/24 skills lack elements and only 1 non-opening skill has Rot!",
        },
        {
            "id": "loop_t3_medal_reaction_chain",
            "name": "三阶勋章单键自触发反应环 (Tier-3 Medal Self-Reaction + WaveAfter + BlastReload)",
            "chain": ["Single Skill Hit with Tier-3 Medal", "Applies Primary + Deterministic Secondary Element -> Instant Reaction", "card_blast_reload (-3s CD) + card_reaction_refund (+15 Momentum) + card_wave_after (applies 3rd random element bypassing ReactionLockSec=0.5s once)"],
            "classification": "Degenerate Skill-Compression Loop",
            "bound_mechanism": "Acquiring 3 identical medals is ~1-3% RNG gated, NOT skill-gated. Once assembled, collapses 2-skill elemental combo setup into 1-button spam.",
        },
        {
            "id": "loop_dash_pinball",
            "name": "冲刺穿人刷新列车环 (Dash Contact Reset + Pass Through + Runaway Train)",
            "chain": ["Dash (D)", "card_pass_through (phase through enemies + basic_hit)", "card_bump_reset (100% CD reset on contender contact) + card_bump_again (-1.5s all skill CDs)"],
            "classification": "High-Skill / High-Risk Mobility Loop",
            "bound_mechanism": "CC root/stun interrupts dash; requires contender contact (50% refund on monsters); single miss puts D on 9.0s CD.",
        },
    ]

    # Bus bottleneck report
    bus_summary = {}
    for bus in sorted(set(bus_producers) | set(bus_consumers)):
        prods = bus_producers.get(bus, [])
        cons = bus_consumers.get(bus, [])
        ab_prods = [p for p in prods if p.startswith("active_")]
        bus_summary[bus] = {
            "producer_count": len(prods),
            "ability_producer_count": len(ab_prods),
            "consumer_count": len(cons),
            "producers_sample": prods[:8],
            "consumers_sample": cons[:8],
        }

    # Audit card_again_now cost anomaly!
    abilities_zero_base_cost_note = {
        "abilities_total": len(catalog.content["abilities"]),
        "abilities_with_base_element": elemental_base_abilities,
        "abilities_without_base_element": len(catalog.content["abilities"]) - elemental_base_abilities,
        "abilities_gaining_element_via_branch": len(elemental_branch_abilities),
        "cards_by_weight": dict(Counter(catalog.card_weight(c) for c in catalog.build_cards_pool())),
        "medals_count": len(catalog.medals_pool()),
    }

    return {
        "metadata": {
            "commit": catalog.commit_sha,
            "manifest_sha256": catalog.manifest_sha256,
            "node_count": len(nodes),
            "edge_count": len(edges),
        },
        "catalog_structure": abilities_zero_base_cost_note,
        "bus_summary": bus_summary,
        "closed_feedback_loops": closed_loops,
        "vocab_vs_mechanics_findings": vocab_mismatches,
        "nodes": nodes,
        "edges": edges,
    }


# ---------------------------------------------------------------------------
# 2. Archetype Definitions & Draft / Shop Agency Monte Carlo Simulation
# ---------------------------------------------------------------------------

ARCHETYPE_DEFS: list[dict[str, Any]] = [
    {
        "id": "skill_engine",
        "name": "技能永动机 (CD Reset Caster)",
        "primary_stat": "stat_brains",
        "secondary_stat": "stat_physique",
        "preferred_actives": {"active_snark_bolt", "active_zap", "active_calculus_drop", "active_hose_down", "active_quick_meeting"},
        "core_cards": {"card_next_up", "card_again_now", "card_combo_chain", "card_quick_brain"},
        "catalyst_cards": {"card_encore", "card_practice"},
        "viable_min_core": 2,
        "transformative_min_core": 3,
        "role": "ranged_poke_caster",
    },
    {
        "id": "attack_speed_onhit",
        "name": "疯狗连击手 (On-Hit Combo Flurry)",
        "primary_stat": "stat_agility",
        "secondary_stat": "stat_strength",
        "preferred_actives": {"active_triple_combo", "active_bail_out", "active_flurry_rush", "active_elbow"},
        "core_cards": {"card_heated_up", "card_quick_hands", "card_both_hands", "card_muscle_memory", "card_breach_fist"},
        "catalyst_cards": {"card_fist_skill", "card_solid_punch"},
        "viable_min_core": 2,
        "transformative_min_core": 3,
        "role": "melee_dps",
    },
    {
        "id": "lifesteal_bruiser",
        "name": "血魔永生流 (Blood-Price Drain Tank)",
        "primary_stat": "stat_physique",
        "secondary_stat": "stat_brains",
        "preferred_actives": {"active_seethe", "active_quick_meeting", "active_charged_ram", "active_open_mic"},
        "core_cards": {"card_punch_heal", "card_hurt_heal", "card_skill_drain", "card_overfed", "card_grow_fat"},
        "catalyst_cards": {"card_blood_price", "card_desperado"},
        "viable_min_core": 2,
        "transformative_min_core": 4,
        "role": "drain_bruiser",
    },
    {
        "id": "one_shot_artillery",
        "name": "憋大招一击流 (Idle-Charge Nuke)",
        "primary_stat": "stat_strength",
        "secondary_stat": "stat_brains",
        "preferred_actives": {"active_calculus_drop", "active_snark_bolt", "active_stale_takeout"},
        "core_cards": {"card_big_one", "card_long_reach", "card_let_bullets_fly", "card_setup_line"},
        "catalyst_cards": {"card_charge_up", "card_held_shot", "card_skill_crit"},
        "viable_min_core": 2,
        "transformative_min_core": 3,
        "role": "burst_artillery",
    },
    {
        "id": "thorn_fortress",
        "name": "铁刺猬肉山 (Shield-Reflect Juggernaut)",
        "primary_stat": "stat_thick_skin",
        "secondary_stat": "stat_physique",
        "preferred_actives": {"active_not_mad", "active_stare_down", "active_brace", "active_open_mic"},
        "core_cards": {"card_spiky_hide", "card_thick_hide", "card_hard_skin", "card_tough_skin", "card_reactive_plating"},
        "catalyst_cards": {"card_iron_hedgehog", "card_shell_burst", "card_tonnage_crush"},
        "viable_min_core": 2,
        "transformative_min_core": 4,
        "role": "zone_tank",
    },
    {
        "id": "elemental_reactor",
        "name": "元素连锁师 (Multi-Element Reactor)",
        "primary_stat": "stat_brains",
        "secondary_stat": "stat_thick_skin",
        "preferred_actives": {"active_seethe", "active_hose_down", "active_zap", "active_cola_shake", "active_stale_takeout", "active_quick_meeting"},
        "core_cards": {"card_reaction_refund", "card_blast_reload", "card_long_soak", "card_fuel_the_fire", "card_endless_burn"},
        "catalyst_cards": {"card_wave_after", "card_louder_blast"},
        "viable_min_core": 2,
        "transformative_min_core": 3,
        "role": "elemental_control",
    },
    {
        "id": "dash_ghost",
        "name": "冲刺鬼影列车 (Dash-Reset Pinball)",
        "primary_stat": "stat_agility",
        "secondary_stat": "stat_strength",
        "preferred_actives": {"active_bail_out", "active_crotch_grab", "active_charged_ram"},
        "core_cards": {"card_pass_through", "card_dash_punch", "card_head_down", "card_paid_dash", "card_bump_again"},
        "catalyst_cards": {"card_bump_reset", "card_double_dash", "card_runaway_train"},
        "viable_min_core": 2,
        "transformative_min_core": 3,
        "role": "hyper_mobility",
    },
    {
        "id": "snowball_reaper",
        "name": "收割滚雪球 (Takedown Reset Assassin)",
        "primary_stat": "stat_strength",
        "secondary_stat": "stat_agility",
        "preferred_actives": {"active_crotch_grab", "active_pants_down", "active_bail_out", "active_elbow"},
        "core_cards": {"card_quick_takedown", "card_pin_amp", "card_ball_kick"},
        "catalyst_cards": {"card_takedown", "card_kill_streak"},
        "viable_min_core": 2,
        "transformative_min_core": 2,
        "role": "cleanup_assassin",
    },
    {
        "id": "low_hp_berserk",
        "name": "残血亡命徒 (Last-Stand Berserker)",
        "primary_stat": "stat_strength",
        "secondary_stat": "stat_physique",
        "preferred_actives": {"active_triple_combo", "active_unplug", "active_flurry_rush"},
        "core_cards": {"card_berserk", "card_hang_on", "card_punch_heal"},
        "catalyst_cards": {"card_desperado", "card_blood_price"},
        "viable_min_core": 2,
        "transformative_min_core": 3,
        "role": "clutch_duelist",
    },
    {
        "id": "turf_engineer",
        "name": "违章阵地王 (Construct & Turf Controller)",
        "primary_stat": "stat_physique",
        "secondary_stat": "stat_brains",
        "preferred_actives": {"active_school_bag", "active_desk_fan", "active_flyer_trap", "active_cordon_tape", "active_quick_meeting"},
        "core_cards": {"card_double_stall", "card_turf_growth", "card_move_turf", "card_rogue_works", "card_ice_trail"},
        "catalyst_cards": {"card_skate_blade", "card_double_stall"},
        "viable_min_core": 2,
        "transformative_min_core": 3,
        "role": "objective_zoner",
    },
    {
        "id": "heavy_mallet_juggernaut",
        "name": "千斤重锤一拳流 (Slow-AS Crit Slam)",
        "primary_stat": "stat_strength",
        "secondary_stat": "stat_agility",
        "preferred_actives": {"active_elbow", "active_pants_down", "active_charged_ram"},
        "core_cards": {"card_slow_craft", "card_wide_swing", "card_third_shove", "card_ball_kick"},
        "catalyst_cards": {"card_heavy_mallet", "card_tonnage_crush"},
        "viable_min_core": 2,
        "transformative_min_core": 3,
        "role": "heavy_bruiser",
    },
    {
        "id": "hybrid_spellblade",
        "name": "文武双全魔剑士 (Hybrid AP/AD Weave)",
        "primary_stat": "stat_brains",
        "secondary_stat": "stat_strength",
        "preferred_actives": {"active_bail_out", "active_zap", "active_snark_bolt", "active_seethe"},
        "core_cards": {"card_brain_speed", "card_spellblade_echo", "card_scholar_punch", "card_tally"},
        "catalyst_cards": {"card_martial_scholar", "card_lend_fist"},
        "viable_min_core": 2,
        "transformative_min_core": 3,
        "role": "hybrid_skirmisher",
    },
]


def _draw_weighted_without_replacement(
    rng: random.Random,
    pool: list[dict[str, Any]],
    weights: list[float],
    k: int,
) -> list[dict[str, Any]]:
    chosen: list[dict[str, Any]] = []
    avail = list(zip(pool, weights))
    for _ in range(min(k, len(avail))):
        total = sum(w for _, w in avail)
        if total <= 0:
            break
        r = rng.random() * total
        acc = 0.0
        pick_idx = len(avail) - 1
        for idx, (_, w) in enumerate(avail):
            acc += w
            if r <= acc:
                pick_idx = idx
                break
        chosen.append(avail.pop(pick_idx)[0])
    return chosen


def simulate_draft_and_formation(
    catalog: CatalogModel,
    seed: int = 20261128,
    trials_per_regime: int = 3000,
) -> dict[str, Any]:
    cards_pool = catalog.build_cards_pool()
    opening_actives = [a["id"] for a in catalog.content["abilities"] if a.get("opening")]
    all_actives = [a["id"] for a in catalog.content["abilities"]]
    regimes = [
        "S0_CurrentBaseline",
        "S1_PureUniformNoVocab",
        "S2_PhasedRarityAndTagFocus",
        "S3_DecoupledShopCurrency",
        "S4_HybridGuidedWithPivot",
    ]

    results: dict[str, Any] = {}
    for r_idx, regime in enumerate(regimes):
        rng = random.Random(seed + r_idx * 10007)
        arch_stats = {a["id"]: {"attempts": 0, "viable": 0, "transformative": 0, "breakout": 0} for a in ARCHETYPE_DEFS}
        legendaries_seen_total = 0
        offers_total = 0
        pivots_needed = 0
        pivots_succeeded = 0
        optimizer_scores: list[float] = []
        beginner_scores: list[float] = []

        for t in range(trials_per_regime):
            target = ARCHETYPE_DEFS[t % len(ARCHETYPE_DEFS)]
            arch_stats[target["id"]]["attempts"] += 1

            # Active drafts: R0 (15->3), L2, L5, L9 (24->3)
            r0_opts = rng.sample(opening_actives, 3)
            held_actives = [next((x for x in r0_opts if x in target["preferred_actives"]), r0_opts[0])]
            for _ in range(3):
                rem = [a for a in all_actives if a not in held_actives]
                opts = rng.sample(rem, 3)
                pick = next((x for x in opts if x in target["preferred_actives"]), opts[0])
                if len(held_actives) < 3:
                    held_actives.append(pick)
                elif pick in target["preferred_actives"] and held_actives[-1] not in target["preferred_actives"]:
                    held_actives[-1] = pick

            active_hits = sum(1 for a in held_actives if a in target["preferred_actives"])
            forced_pivot = active_hits == 0
            if forced_pivot:
                pivots_needed += 1
                # Pick best alternative archetype matching held_actives
                alt = max(ARCHETYPE_DEFS, key=lambda ar: sum(1 for a in held_actives if a in ar["preferred_actives"]))
                if regime == "S4_HybridGuidedWithPivot":
                    target_eval = alt
                else:
                    target_eval = alt
            else:
                target_eval = target

            # SIM BOUNDARY: offer count is assumed (~L10-11 reached). Code: one opening offer (LDDevelopmentDirector.cpp:363) plus
            # one per crossed multiple of CARDS.OfferEveryLevels (L3/6/9/12/15, LDProgressionRules.cpp:1331-1334) plus event
            # offers (flags, arena, lockers, peddler, bounty). Viability/formation thresholds come from the lab's own
            # ARCHETYPE_DEFS, not from any native definition, so formation rates are model-defined.
            # Typical 8-player match reaches ~L10-L11: natural card offers at R0, L3, L6, L9 (4) + ~1.5 map events = 5-6 offers
            # Under S2/S4, offers occur every 2 levels (R0, L2, L4, L6, L8, L10 = 6 natural + 1.5 event = 7-8 offers)
            if regime in ("S2_PhasedRarityAndTagFocus", "S4_HybridGuidedWithPivot"):
                natural_offers = 7 + (1 if rng.random() < 0.5 else 0)
            else:
                natural_offers = 5 + (1 if rng.random() < 0.5 else 0)

            rerolls_left = 3 + 3  # 3 opening + 1 per rest x3
            acquired_cards: list[str] = []
            current_vocab: Counter[str] = Counter()

            def card_weights_for_phase(progress_ratio: float, shop_mode: bool = False) -> list[float]:
                out_w = []
                for c in cards_pool:
                    base_w = float(catalog.card_weight(c))
                    if regime in ("S2_PhasedRarityAndTagFocus", "S4_HybridGuidedWithPivot"):
                        if base_w == 22:
                            base_w = 22.0 + 26.0 * progress_ratio
                        elif base_w == 6:
                            base_w = 6.0 + 16.0 * progress_ratio
                    if regime == "S1_PureUniformNoVocab":
                        mult = 1.0
                    else:
                        shared = sum(1 for v in c.get("vocab", []) if current_vocab[v] > 0)
                        if regime in ("S2_PhasedRarityAndTagFocus", "S4_HybridGuidedWithPivot"):
                            mult = 2.6 if shared >= 2 else (1.7 if shared == 1 else 1.0)
                        else:
                            if shop_mode:
                                mult = 1.8 if shared >= 1 else 1.0
                            else:
                                mult = 1.8 if shared >= 2 else (1.2 if shared == 1 else 1.0)
                    out_w.append(base_w * mult)
                return out_w

            def score_card(cid: str, arch: dict[str, Any]) -> float:
                if cid in arch["catalyst_cards"]:
                    return 4.5
                if cid in arch["core_cards"]:
                    return 2.5
                c_obj = catalog.cards_by_id[cid]
                arch_vocab = set()
                for ref_id in arch["core_cards"] | arch["catalyst_cards"]:
                    if ref_id in catalog.cards_by_id:
                        arch_vocab.update(catalog.cards_by_id[ref_id].get("vocab", []))
                overlap = len(set(c_obj.get("vocab", [])) & arch_vocab)
                return 0.6 + 0.35 * overlap

            # Simulate offers for Optimizer
            for off_i in range(natural_offers):
                prog = off_i / max(1, natural_offers - 1)
                offer = _draw_weighted_without_replacement(rng, cards_pool, card_weights_for_phase(prog), 3)
                offers_total += 1
                if any(catalog.card_weight(c) == 6 for c in offer):
                    legendaries_seen_total += 1
                best_c = max(offer, key=lambda c: score_card(c["id"], target_eval))
                if score_card(best_c["id"], target_eval) < 2.0 and rerolls_left > 0:
                    rerolls_left -= 1
                    offer = _draw_weighted_without_replacement(rng, cards_pool, card_weights_for_phase(prog), 3)
                    offers_total += 1
                    if any(catalog.card_weight(c) == 6 for c in offer):
                        legendaries_seen_total += 1
                    best_c = max(offer, key=lambda c: score_card(c["id"], target_eval))
                acquired_cards.append(best_c["id"])
                for v in best_c.get("vocab", []):
                    current_vocab[v] += 1

            # Shop simulation across R1, R2, R3
            # Under S0/S1/S2, spending Stat Points delays 18-pt Keystone, so optimal players buy at most 0-1 high-synergy card before L11!
            # Under S3/S4 (decoupled shop currency), players earn ~9-12 shop cred across 3 segments -> can buy 3-4 cards + free refreshes!
            # SIM BOUNDARY: shop purchase budget/threshold are behavioural assumptions (shop spends unspent stat points:
            # card 2/3/4 by rarity, LDProgressionRules.cpp:1906-1912; skill 2, skill point 3, medal 4).
            shop_buys_budget = 4 if regime in ("S3_DecoupledShopCurrency", "S4_HybridGuidedWithPivot") else 1
            buys_done = 0
            for rest_idx in range(3):
                prog = (rest_idx + 1) / 3.0
                for _refresh in range(2):  # 1 initial shelf + 1 free refresh
                    if buys_done >= shop_buys_budget:
                        break
                    if regime in ("S2_PhasedRarityAndTagFocus", "S4_HybridGuidedWithPivot", "S1_PureUniformNoVocab"):
                        shelf = _draw_weighted_without_replacement(rng, cards_pool, card_weights_for_phase(prog, shop_mode=True), 4)
                    else:
                        # Current rule: only the first SHOP.VocabCardSlots (2) of SHOP.CardSlots (4) are vocab-weighted (x1.8 if
                        # any tag is shared); the rest use drop_weight only, all distinct (LDProgressionRules.cpp:1844-1866).
                        vocab_slots = int(catalog.param("SHOP.VocabCardSlots", 2))
                        shelf = _draw_weighted_without_replacement(rng, cards_pool, card_weights_for_phase(prog, shop_mode=True), vocab_slots)
                        rest_pool = [c for c in cards_pool if c not in shelf]
                        shelf += _draw_weighted_without_replacement(rng, rest_pool, [float(catalog.card_weight(c)) for c in rest_pool], int(catalog.param("SHOP.CardSlots", 4)) - vocab_slots)
                    best_s = max(shelf, key=lambda c: score_card(c["id"], target_eval))
                    threshold = 2.4 if shop_buys_budget > 1 else 4.0  # with stat-point cost, only catalysts beat raw stats!
                    if score_card(best_s["id"], target_eval) >= threshold:
                        acquired_cards.append(best_s["id"])
                        for v in best_s.get("vocab", []):
                            current_vocab[v] += 1
                        buys_done += 1

            opt_ranked = sorted(acquired_cards, key=lambda cid: score_card(cid, target_eval), reverse=True)[:9]
            beg_cards = [rng.choice(_draw_weighted_without_replacement(rng, cards_pool, [catalog.card_weight(c) for c in cards_pool], 3))["id"]
                         for _ in range(natural_offers)]
            opt_val = sum(score_card(cid, target_eval) for cid in opt_ranked)
            beg_val = sum(score_card(cid, target_eval) for cid in beg_cards[:9])
            optimizer_scores.append(opt_val)
            beginner_scores.append(beg_val)

            held_set = set(opt_ranked)  # L10-12 capacity ~9 slots with intelligent weak-card replacement
            core_hits = len(held_set & target_eval["core_cards"])
            cat_hits = len(held_set & target_eval["catalyst_cards"])
            a_hit = sum(1 for a in held_actives if a in target_eval["preferred_actives"])

            is_viable = (a_hit >= 1) and (core_hits + cat_hits >= target_eval["viable_min_core"])
            is_trans = (a_hit >= 1) and (core_hits + cat_hits >= target_eval["transformative_min_core"]) and (cat_hits >= 1 or core_hits >= 3)
            is_breakout = (a_hit >= 2) and (cat_hits >= 1) and (core_hits + cat_hits >= target_eval["transformative_min_core"] + 1)

            if forced_pivot and is_viable:
                pivots_succeeded += 1
            if not forced_pivot:
                if is_viable:
                    arch_stats[target["id"]]["viable"] += 1
                if is_trans:
                    arch_stats[target["id"]]["transformative"] += 1
                if is_breakout:
                    arch_stats[target["id"]]["breakout"] += 1
            else:
                # Record against original intent AND pivot recovery separately
                if is_viable and regime == "S4_HybridGuidedWithPivot":
                    arch_stats[target["id"]]["viable"] += 1

        total_att = sum(v["attempts"] for v in arch_stats.values())
        results[regime] = {
            "trials": trials_per_regime,
            "viable_formation_rate": round(sum(v["viable"] for v in arch_stats.values()) / total_att, 4),
            "transformative_formation_rate": round(sum(v["transformative"] for v in arch_stats.values()) / total_att, 4),
            "breakout_formation_rate": round(sum(v["breakout"] for v in arch_stats.values()) / total_att, 4),
            "legendary_offer_sighting_rate": round(legendaries_seen_total / max(1, offers_total), 4),
            "forced_pivot_rate": round(pivots_needed / trials_per_regime, 4),
            "pivot_recovery_rate": round(pivots_succeeded / max(1, pivots_needed), 4),
            "optimizer_mean_synergy_score": round(statistics.fmean(optimizer_scores), 2),
            "beginner_mean_synergy_score": round(statistics.fmean(beginner_scores), 2),
            "skill_decision_leverage_ratio": round(statistics.fmean(optimizer_scores) / max(0.01, statistics.fmean(beginner_scores)), 2),
            "by_archetype": {
                aid: {
                    "viable_rate": round(st["viable"] / max(1, st["attempts"]), 3),
                    "transformative_rate": round(st["transformative"] / max(1, st["attempts"]), 3),
                    "breakout_rate": round(st["breakout"] / max(1, st["attempts"]), 3),
                }
                for aid, st in arch_stats.items()
            },
        }

    return {
        "metadata": {"seed": seed, "trials_per_regime": trials_per_regime, "commit": catalog.commit_sha},
        "regimes": results,
    }


# ---------------------------------------------------------------------------
# 3. Deterministic Combat, TTK, Overdrive/Dodge & Matchup Matrix Simulator
# ---------------------------------------------------------------------------

@dataclass
class CombatantSpec:
    id: str
    name: str
    level: int
    stat_points: dict[str, int]
    actives: list[str]
    skill_ranks: list[int]  # 1, 2, or 3
    cards: list[str]
    medal_tier: int = 0
    medal_element: str = "none"
    use_overdrive: bool = True
    dodge_skill: float = 0.25  # probability of landing perfect dodge on eligible telegraph


def project_stats(catalog: CatalogModel, spec: CombatantSpec) -> dict[str, float]:
    k = spec.level - 1
    pts = spec.stat_points
    p_phy = pts.get("stat_physique", 0)
    p_skin = pts.get("stat_thick_skin", 0)
    p_str = pts.get("stat_strength", 0)
    p_brn = pts.get("stat_brains", 0)
    p_agi = pts.get("stat_agility", 0)

    armor = 18.0 + 2.0 * k + 3.5 * p_skin
    mr = 18.0 + 2.0 * k + 3.5 * p_skin
    bonus_hp = 70.0 * p_phy + (6.0 * armor if "card_thick_hide" in spec.cards else 0.0)
    max_hp = min(20000.0, 860.0 + 125.0 * k + bonus_hp)
    ad = min(2000.0, 70.0 + 3.2 * k + 3.5 * p_str)
    ap = min(3000.0, 6.0 * p_brn)
    haste = min(400.0, 1.0 * p_brn)
    crit = min(1.0, 0.006 * p_str + (0.15 if "card_skill_crit" in spec.cards else 0.0))

    as_bonus = 0.02 * k + 0.02 * p_agi
    if "card_brain_speed" in spec.cards:
        as_bonus += (ap // 25.0) * 0.04
    raw_as = 0.85 * (1.0 + as_bonus)
    attack_speed = max(0.30, min(5.00, raw_as))
    heavy_mallet_bonus = 0.0
    if "card_heavy_mallet" in spec.cards and attack_speed > 0.75:
        heavy_mallet_bonus = ((attack_speed - 0.75) / 0.01) * 0.018
        attack_speed = 0.75

    move_speed = max(138.4, min(540.0, 240.0 * (1.0 + 0.005 * p_agi)))
    hp_regen = min(12.0, 1.60 + 0.12 * k + 0.08 * p_phy)
    mom_regen = 5.0 + 0.06 * p_brn

    return {
        "max_hp": max_hp,
        "bonus_hp": bonus_hp,
        "ad": ad,
        "bonus_ad": ad - (70.0 + 3.2 * k),
        "ap": ap,
        "armor": min(200.0, armor),
        "mr": min(200.0, mr),
        "haste": haste,
        "crit": crit,
        "attack_speed": attack_speed,
        "heavy_mallet_bonus": heavy_mallet_bonus,
        "move_speed": move_speed,
        "hp_regen": hp_regen,
        "mom_regen": mom_regen,
        "ks_physique": 1.0 if p_phy >= 18 else 0.0,
        "ks_thick_skin": 1.0 if p_skin >= 18 else 0.0,
        "ks_strength": 1.0 if p_str >= 18 else 0.0,
        "ks_brains": 1.0 if p_brn >= 18 else 0.0,
        "ks_agility": 1.0 if p_agi >= 18 else 0.0,
    }


def make_archetype_combatant(catalog: CatalogModel, arch_id: str, level: int, spent_fraction: float = 1.0) -> CombatantSpec:
    total_pts = int(round((3 + (level - 1) * 3) * spent_fraction))
    if arch_id == "bare":
        return CombatantSpec("bare", f"L{level} 裸构筑", level, {}, ["active_elbow", "active_snark_bolt", "active_not_mad"][:max(1, min(3, 1 + (level >= 2) + (level >= 5)))], [1, 1, 1], [], use_overdrive=True, dodge_skill=0.15)
    arch = next(a for a in ARCHETYPE_DEFS if a["id"] == arch_id)
    p1 = min(24, max(0, int(total_pts * 0.62)))
    if total_pts >= 18 and p1 < 18:
        p1 = 18
    p2 = max(0, total_pts - p1)
    pts = {arch["primary_stat"]: p1, arch["secondary_stat"]: p2}
    acts = sorted(arch["preferred_actives"])[:3]
    while len(acts) < 3:
        acts.append("active_elbow")
    # Skill points by level: [3, 6, 9, 12, 15] -> up to 5 natural points -> ranks e.g. [3, 3, 2] at L15+
    sp = sum(1 for lv in (3, 6, 9, 12, 15) if level >= lv)
    ranks = [1, 1, 1]
    for idx in (0, 0, 1, 1, 2, 2):
        if sp > 0 and ranks[idx] < 3:
            ranks[idx] += 1
            sp -= 1
    cap = catalog.card_capacity_at_level(level)
    cards = (sorted(arch["catalyst_cards"]) + sorted(arch["core_cards"]))[:min(cap, 6 if level >= 12 else 4)]
    medal_tier = 2 if arch_id == "elemental_reactor" and level >= 10 else (1 if level >= 9 else 0)
    return CombatantSpec(arch_id, arch["name"], level, pts, acts, ranks, cards, medal_tier=medal_tier, medal_element="electric" if medal_tier else "none")


def run_single_duel(
    catalog: CatalogModel,
    a_spec: CombatantSpec,
    b_spec: CombatantSpec,
    seed: int,
    timeout_s: float = 45.0,
) -> dict[str, Any]:
    rng = random.Random(seed)
    # SIM BOUNDARY (whole duel): no positions/movement — both fighters are always in range, so range, kiting, skill
    # geometry/hit chance, knockback distance and dash spacing are absent. Skill rank branches (2A/2B patches) are
    # replaced by flat rank scales; per-element overdrive extras (burn/slow/root/shock/rot) are not modelled; card
    # effects keep the lab's own approximations (not audited in T05-03). Native measurement: L16 bare-vs-bare TTK 21.38s.
    dt = 0.05  # 50ms discrete step (20 Hz combat integration)
    steps = int(timeout_s / dt)

    def init_fighter(spec: CombatantSpec) -> dict[str, Any]:
        st = project_stats(catalog, spec)
        return {
            "spec": spec,
            "st": st,
            "hp": st["max_hp"],
            # T05-03: shields are StackToLatestExpiry layers [(amount, expires_at)]; the physique keystone no longer grants
            # a free opening/periodic 12% shield (LDCombatMechanicsDirector.cpp:119-140 converts 50% of healing instead).
            "shield_layers": [],
            "momentum": float(catalog.param("MOMENTUM.Start", 50.0)),
            "mom_time_sum": 0.0,
            "mom_ge_od_s": 0.0,
            "last_paid": 0.0,
            "ks_phy_ready": 0.0,
            "ks_skin_ready": 0.0,
            "str_hp_ready": 0.0,
            "slowed_until": -1.0,
            "agi_empower_until": -1.0,
            "skill_lock_until": 0.0,
            "last_basic_t": -10.0,
            "next_basic_t": 0.0,
            "combo_step": 0,
            "skill_ready": [0.0, 0.0, 0.0],
            "dash_ready": 0.0,
            "cc_until": 0.0,
            "cc_dr_stage": 0,
            "cc_dr_until": -10.0,
            "cc_lockout_total": 0.0,
            "hold_until": 0.0,
            "pending_overdrive_slot": -1,
            "elements_on_self": [],
            "reaction_lock_until": -10.0,
            "breach_stacks": 0,
            "focus_stacks": 0,
            "recent_casts": [],
            "dmg_dealt": 0.0,
            "dmg_2s": 0.0,
            "heal_done": 0.0,
            "casts": 0,
            "overdrives": 0,
            "reactions": 0,
            "last_stand_used": False,
            "last_stand_until": -1.0,
        }

    fa = init_fighter(a_spec)
    fb = init_fighter(b_spec)

    def shield_total(f: dict[str, Any], t: float) -> float:
        f["shield_layers"] = [(amt, exp) for amt, exp in f["shield_layers"] if exp > t and amt > 0]
        return sum(amt for amt, _ in f["shield_layers"])

    def add_shield(f: dict[str, Any], amount: float, duration: float, t: float) -> None:
        if amount > 0 and duration > 0:
            f["shield_layers"].append((amount, t + duration))

    def absorb_shield(f: dict[str, Any], amount: float, t: float) -> float:
        # Earliest-expiring layer first (CONTENT_AND_BALANCE.md:703 ShieldLayers).
        shield_total(f, t)
        f["shield_layers"].sort(key=lambda row: row[1])
        absorbed = 0.0
        layers = []
        for amt, exp in f["shield_layers"]:
            take = min(amt, amount - absorbed)
            absorbed += take
            if amt - take > 0:
                layers.append((amt - take, exp))
        f["shield_layers"] = layers
        return absorbed

    def is_impaired(f: dict[str, Any], t: float) -> bool:
        # Strength keystone condition: slowed, attack-slowed, knocked back or hard-controlled (CONTENT_AND_BALANCE.md:286).
        return t < f["slowed_until"] or t < f["cc_until"]

    def apply_damage(att: dict[str, Any], dfn: dict[str, Any], raw: float, magical: bool, t: float, is_basic: bool = False, is_skill: bool = False) -> float:
        if raw <= 0 or dfn["hp"] <= 0:
            return 0.0
        amp = 1.0
        if att["st"]["ks_strength"] and is_impaired(dfn, t):
            amp += catalog.param("COMBAT.KeystoneStrengthDamageRatio", 0.14)
        if "card_desperado" in att["spec"].cards and (att["hp"] / att["st"]["max_hp"]) < 0.45:
            missing_pct = 1.0 - (att["hp"] / att["st"]["max_hp"])
            amp += (missing_pct / 0.02) * 0.015
        if is_basic and att["st"]["heavy_mallet_bonus"] > 0:
            amp += att["st"]["heavy_mallet_bonus"]
        if is_basic and "card_slow_craft" in att["spec"].cards and att["st"]["attack_speed"] < 1.2:
            amp += ((1.2 - att["st"]["attack_speed"]) / 0.1) * 0.08
        if is_skill and "card_combo_chain" in att["spec"].cards:
            recent = sum(1 for ct in att["recent_casts"] if t - ct <= 4.0)
            amp += 0.20 * recent

        resist = (dfn["st"]["mr"] if magical else dfn["st"]["armor"]) - 3.0 * dfn["breach_stacks"]
        resist = max(0.0, min(200.0, resist))
        mitigated = raw * amp * (100.0 / (100.0 + resist))
        # Thick-skin keystone: once per 5s, a direct enemy hit of >=30 post-resist loses Max(45, 22%) before shields and
        # grants 10 momentum (CONTENT_AND_BALANCE.md:285). Replaces the former invented flat 8% skill reduction.
        # SIM BOUNDARY: the 35%-max-HP delayed bleed and the 1.5s steady (slow/knockback) part are not modelled.
        if dfn["st"]["ks_thick_skin"] and (is_basic or is_skill) and mitigated >= 30.0 and t >= dfn["ks_skin_ready"]:
            mitigated -= min(mitigated, max(catalog.param("COMBAT.KeystoneThickSkinGuardPoints", 45.0),
                                            mitigated * catalog.param("COMBAT.KeystoneThickSkinGuardCapRatio", 0.22)))
            dfn["ks_skin_ready"] = t + catalog.param("COMBAT.KeystoneThickSkinIntervalSec", 5.0)
            dfn["momentum"] = min(100.0, dfn["momentum"] + 10.0)
        if t < dfn["last_stand_until"]:
            mitigated = min(mitigated, 1.0)

        absorbed = absorb_shield(dfn, mitigated, t)
        hp_loss = min(dfn["hp"], mitigated - absorbed)
        dfn["hp"] -= hp_loss

        if dfn["hp"] > 0 and "card_hang_on" in dfn["spec"].cards and not dfn["last_stand_used"]:
            if (dfn["hp"] / dfn["st"]["max_hp"]) <= 0.15:
                dfn["last_stand_used"] = True
                dfn["last_stand_until"] = t + 2.0

        att["dmg_dealt"] += hp_loss
        if t <= 2.0:
            att["dmg_2s"] += hp_loss

        # Lifesteal
        vamp_ratio = 0.0
        if is_basic and "card_punch_heal" in att["spec"].cards:
            vamp_ratio += 0.15
        if is_skill and "card_hurt_heal" in att["spec"].cards:
            vamp_ratio += 0.25
        if is_skill and "card_skill_drain" in att["spec"].cards and "card_punch_heal" in att["spec"].cards:
            vamp_ratio += 0.15
        if vamp_ratio > 0 and hp_loss > 0 and att["hp"] > 0:
            heal = hp_loss * vamp_ratio
            over = max(0.0, (att["hp"] + heal) - att["st"]["max_hp"])
            healed = min(att["st"]["max_hp"], att["hp"] + heal) - att["hp"]
            att["hp"] += healed
            att["heal_done"] += heal
            # Physique keystone: 50% of effective healing -> shield, capped at 12% max HP, 4s duration, 4s internal
            # cooldown (LDCombatMechanicsDirector.cpp:119-140).
            if att["st"]["ks_physique"] and healed > 0 and t >= att["ks_phy_ready"]:
                add_shield(att, min(healed * catalog.param("COMBAT.KeystonePhysiqueHealToShieldRatio", 0.5),
                                    att["st"]["max_hp"] * catalog.param("COMBAT.KeystonePhysiqueShieldMaxHPRatio", 0.12)),
                           catalog.param("COMBAT.KeystonePhysiqueShieldDurationSec", 4.0), t)
                att["ks_phy_ready"] = t + catalog.param("COMBAT.KeystonePhysiqueIntervalSec", 4.0)
            if over > 0 and "card_overfed" in att["spec"].cards:
                # SIM BOUNDARY: card_overfed shield duration/cap are lab approximations (card mechanics not audited in T05-03).
                gain_sh = min(over * 0.5, max(0.0, att["st"]["max_hp"] * 0.35 - shield_total(att, t)))
                add_shield(att, gain_sh, 4.0, t)
                if "card_reactive_plating" in att["spec"].cards:
                    dfn["hp"] = max(0.0, dfn["hp"] - gain_sh * 0.35 * (100.0 / (100.0 + dfn["st"]["armor"])))

        # Thorns
        if (is_basic or is_skill) and "card_spiky_hide" in dfn["spec"].cards and att["hp"] > 0:
            thorn_Count = sum(1 for c in dfn["spec"].cards if c == "card_spiky_hide")
            thorn_dmg = mitigated * (0.25 * thorn_Count)
            if "card_iron_hedgehog" in dfn["spec"].cards:
                thorn_dmg += (dfn["st"]["armor"] + dfn["st"]["mr"]) * 0.40
            att["hp"] = max(0.0, att["hp"] - thorn_dmg * (100.0 / (100.0 + att["st"]["mr"])))
            dfn["dmg_dealt"] += thorn_dmg * (100.0 / (100.0 + att["st"]["mr"]))
        return hp_loss

    def apply_cc(dfn: dict[str, Any], dur: float, t: float) -> None:
        # Hard-control DR: 100%/60%/30%, floor min(0.2s, dur); chain continues while the previous control runs or within
        # 2s after it ends (LDCombatControlRules.cpp:88-96, CONTENT_AND_BALANCE.md:698-699).
        if dur <= 0:
            return
        if t >= dfn["cc_dr_until"]:
            dfn["cc_dr_stage"] = 0
        else:
            dfn["cc_dr_stage"] = min(2, dfn["cc_dr_stage"] + 1)
        scales = [1.0, 0.6, 0.3]
        eff = dur if dfn["cc_dr_stage"] == 0 else max(min(0.2, dur), dur * scales[dfn["cc_dr_stage"]])
        added = max(0.0, (t + eff) - max(t, dfn["cc_until"]))
        dfn["cc_lockout_total"] += added
        dfn["cc_until"] = max(dfn["cc_until"], t + eff)
        dfn["cc_dr_until"] = dfn["cc_until"] + 2.0
        if dfn["hold_until"] > t:
            dfn["hold_until"] = 0.0
            dfn["pending_overdrive_slot"] = -1

    def apply_knockback(dfn: dict[str, Any], dur: float, t: float) -> None:
        # Knockback blocks move/basic/cast for its duration and does not advance hard-control DR (CONTENT_AND_BALANCE.md:700-702).
        # SIM BOUNDARY: displacement, wall impact and range loss after the push are not modelled (no positions in the duel).
        if dur <= 0:
            return
        dfn["cc_lockout_total"] += max(0.0, (t + dur) - max(t, dfn["cc_until"]))
        dfn["cc_until"] = max(dfn["cc_until"], t + dur)
        if dfn["hold_until"] > t:
            dfn["hold_until"] = 0.0
            dfn["pending_overdrive_slot"] = -1

    def apply_element(att: dict[str, Any], dfn: dict[str, Any], elem: str, t: float, bypass_lock: bool = False) -> None:
        if not elem or elem == "none":
            return
        active_elems = [e for e, exp in dfn["elements_on_self"] if exp > t]
        dfn["elements_on_self"] = [(e, exp) for e, exp in dfn["elements_on_self"] if exp > t]
        other = next((e for e in active_elems if e != elem), None)
        if other and (bypass_lock or t >= dfn["reaction_lock_until"]):
            dfn["elements_on_self"] = [(e, exp) for e, exp in dfn["elements_on_self"] if e != other]
            dfn["reaction_lock_until"] = t + 0.5
            att["reactions"] += 1
            # Reaction +10 momentum; brains keystone refunds 30% of this release's paid momentum (15 if it paid nothing)
            # and cuts every cooling learned skill by 1s (CONTENT_AND_BALANCE.md:287). SIM BOUNDARY: "once per release" is
            # approximated by the reaction lock; refund uses the attacker's last paid cost.
            mom_gain = catalog.param("MOMENTUM.Reaction", 10.0) + (15.0 if "card_reaction_refund" in att["spec"].cards else 0.0)
            if att["st"]["ks_brains"]:
                mom_gain += (catalog.param("COMBAT.KeystoneBrainsMomentumRefundRatio", 0.3) * att["last_paid"]) if att["last_paid"] > 0 else catalog.param("COMBAT.KeystoneBrainsReactionMomentum", 15.0)
                cut = catalog.param("COMBAT.KeystoneBrainsCooldownCutSec", 1.0)
                for s_idx in range(3):
                    att["skill_ready"][s_idx] = max(t, att["skill_ready"][s_idx] - cut)
            att["momentum"] = min(100.0, att["momentum"] + mom_gain)
            # Reaction damage = 30 + 4*(level-1) + 0.25*AP (CONTENT_AND_BALANCE.md:563).
            base_rx = catalog.param("COMBAT.ReactionBase", 30.0) + catalog.param("COMBAT.ReactionPerLevel", 4.0) * (att["spec"].level - 1) + catalog.param("COMBAT.ReactionAPRatio", 0.25) * att["st"]["ap"]
            pair = frozenset({elem, other})
            rx = next((r for r in REACTIONS_SPEC if frozenset(r["pair"]) == pair), REACTIONS_SPEC[0])
            rx_mult = rx["ratio"] * (1.35 if att["spec"].medal_tier >= 2 else 1.0) * (1.45 if "card_louder_blast" in att["spec"].cards else 1.0)
            apply_damage(att, dfn, base_rx * rx_mult, True, t)
            if rx.get("cc_sec", 0) > 0:
                apply_cc(dfn, rx["cc_sec"], t)
            if rx.get("breach", 0) > 0:
                dfn["breach_stacks"] = min(5, dfn["breach_stacks"] + rx["breach"])
            if "card_blast_reload" in att["spec"].cards:
                att["skill_ready"][0] = max(t, att["skill_ready"][0] - 3.0)
            if "card_wave_after" in att["spec"].cards:
                rem_elems = [x for x in ("fire", "water", "electric", "chill", "rot") if x not in pair]
                apply_element(att, dfn, rng.choice(rem_elems), t, bypass_lock=True)
        else:
            dur = 8.0 if "card_long_soak" in att["spec"].cards else 4.0
            dfn["elements_on_self"].append((elem, t + dur))

    def cast_skill(att: dict[str, Any], dfn: dict[str, Any], slot: int, overdrive: bool, t: float) -> None:
        if slot >= len(att["spec"].actives):
            return
        aid = att["spec"].actives[slot]
        ab = catalog.abilities_by_id[aid]
        rank = att["spec"].skill_ranks[slot] if slot < len(att["spec"].skill_ranks) else 1
        # Skill growth: grown base = base * (1 + 0.04*(level-1)) * RankBaseScale; ratios * RankRatioScale
        # (LDSkillGrowth.cpp:27-34, 47-98).
        growth = 1.0 + catalog.param("SKILL.GrowthPerLevel", 0.04) * (att["spec"].level - 1)
        base_scale = [1.0, 1.35, 1.70][min(2, rank - 1)] * growth
        ratio_scale = [1.0, 1.25, 1.50][min(2, rank - 1)]

        cd_base = num_val(ab.get("cooldown"), 5.5)
        if slot == 0 and "card_again_now" in att["spec"].cards:
            cd_base *= 0.65
        if slot == 0 and "card_big_one" in att["spec"].cards:
            cd_base *= 1.50
        # Brains keystone cuts cooldowns on the user's reactions only (apply_element), not on every cast.
        cd_eff = max(catalog.param("SKILL.CooldownFloorSec", 0.30), cd_base * 100.0 / (100.0 + att["st"]["haste"]))
        att["skill_ready"][slot] = t + cd_eff
        att["casts"] += 1
        att["recent_casts"].append(t)
        # A release's windup is an action: no basic attack starts until it ends (LDCombatActions.cpp:1018, ActiveGate busy).
        # SIM BOUNDARY: the effect still resolves at cast time instead of at the end of the windup.
        att["next_basic_t"] = max(att["next_basic_t"], t + num_val(ab.get("windup"), 0.0))
        att["skill_lock_until"] = t + num_val(ab.get("windup"), 0.0)

        # Every release pays its momentum_cost; an overdrive pays max(threshold, cost) in total
        # (LDCombatActions.cpp:1593-1594, 1714-1717). Brains keystone does not refund overdrive spend.
        paid = skill_cost(att, slot)
        od_mult = 1.0
        if overdrive:
            paid = max(od_threshold(att), paid)
            od_mult = 1.40 if "card_short_breath" in att["spec"].cards else catalog.param("OVERDRIVE.Scale", 1.35)
            att["overdrives"] += 1
        att["momentum"] = max(0.0, att["momentum"] - paid)
        att["last_paid"] = paid

        if slot == 0 and "card_blood_price" in att["spec"].cards:
            hp_cost = att["st"]["max_hp"] * 0.06
            if att["hp"] > hp_cost + 20.0:
                att["hp"] -= hp_cost

        # SIM BOUNDARY: perfect dodge is a per-cast probability (dodge_skill) on telegraphed skills with windup >= 0.15s.
        # Native rules also allow a graze dodge of any direct hit/projectile in the dash's first 0.12s
        # (CONTENT_AND_BALANCE.md:63-65; LDCombatEffects.cpp:296-341); reaction time and geometry are not modelled.
        wind = num_val(ab.get("windup"), 0.15)
        dodged_mult = 1.0
        if wind >= 0.15 and t >= dfn["dash_ready"] and t >= dfn["cc_until"] and rng.random() < dfn["spec"].dodge_skill:
            dfn["dash_ready"] = t + 9.0 * (0.6 if dfn["st"]["ks_agility"] else 1.0) * 0.7
            dfn["momentum"] = min(100.0, dfn["momentum"] + catalog.param("MOMENTUM.PerfectDodge", 20.0))
            if dfn["st"]["ks_agility"]:
                dfn["agi_empower_until"] = t + catalog.param("COMBAT.KeystoneAgilityEmpowerDurationSec", 3.0)
            dodged_mult = 0.5

        # Resolve ability effects
        raw_dmg = 0.0
        magical = False
        cc_dur = 0.0
        knock_dur = 0.0
        slow_dur = 0.0
        bonus_ad = att["st"]["bonus_ad"]
        ap = att["st"]["ap"]
        for eff in ab.get("effects", []):
            op = eff.get("op")
            if op in ("damage", "zone_damage", "ground_impact", "self_area", "channel_cone"):
                b = num_val(eff.get("base"), 65.0) * base_scale
                adr = num_val(eff.get("ad_ratio"), 0.0) * ratio_scale
                apr = num_val(eff.get("ap_ratio"), 0.0) * ratio_scale
                adapt = num_val(eff.get("adaptive_ratio"), 0.0) * ratio_scale
                hpr = num_val(eff.get("max_hp_ratio"), 0.0) * ratio_scale
                # Adaptive ratio scales bonus AD when bonusAD*1.4 >= AP (physical) else AP (magical) (LDTacticalRules.cpp:452-458).
                adaptive_physical = bonus_ad * 14.0 >= ap * 10.0
                adaptive_term = adapt * (bonus_ad if adaptive_physical else ap)
                # SIM BOUNDARY: zone/channel effects are approximated as 3 full ticks landing on the target.
                ticks = 3.0 if op in ("zone_damage", "channel_cone") else 1.0
                raw_dmg += ticks * (b + adr * att["st"]["ad"] + apr * ap + adaptive_term + hpr * att["st"]["bonus_hp"])
                if eff.get("damage_type") == "Magical" or apr > adr or (adapt > 0 and not adaptive_physical):
                    magical = True
            elif op == "shield":
                sh = (num_val(eff.get("base"), 110.0) * base_scale
                      + ratio_scale * (num_val(eff.get("max_hp_ratio"), 0.08) * att["st"]["max_hp"]
                                       + num_val(eff.get("ad_ratio"), 0.0) * att["st"]["ad"]
                                       + num_val(eff.get("ap_ratio"), 0.0) * ap)) * od_mult
                add_shield(att, sh, num_val(eff.get("duration"), 3.0), t)
            elif op in ("root", "stun", "taunt"):
                cc_dur = max(cc_dur, num_val(eff.get("duration"), 0.9))
            elif op in ("knockback", "pull"):
                knock_dur = max(knock_dur, num_val(eff.get("duration"), 0.25))
            elif op in ("slow", "zone_slow", "chill"):
                slow_dur = max(slow_dur, num_val(eff.get("duration"), 1.5))
            elif op == "empower_basic":
                raw_dmg += (45.0 * base_scale + 0.85 * ratio_scale * att["st"]["ad"])

        if raw_dmg == 0.0 and aid in ("active_triple_combo", "active_flurry_rush", "active_school_bag", "active_desk_fan", "active_flyer_trap", "active_cordon_tape"):
            raw_dmg = (85.0 * base_scale + 0.90 * ratio_scale * max(att["st"]["ad"], att["st"]["ap"] * 0.8))

        if slot == 0 and "card_big_one" in att["spec"].cards:
            raw_dmg *= 1.80
        if slot == 0 and "card_blood_price" in att["spec"].cards:
            raw_dmg *= 1.20
        if "card_skill_crit" in att["spec"].cards and rng.random() < att["st"]["crit"]:
            raw_dmg *= 2.0

        if raw_dmg > 0:
            apply_damage(att, dfn, raw_dmg * od_mult * dodged_mult, magical, t, is_skill=True)
            att["momentum"] = min(100.0, att["momentum"] + 5.0 + (6.0 if "card_pay_back" in att["spec"].cards else 0.0))
            if "card_next_up" in att["spec"].cards:
                for s_other in range(3):
                    if s_other != slot:
                        att["skill_ready"][s_other] = max(t, att["skill_ready"][s_other] - 1.0)
            elem = ab.get("element", "none")
            if elem == "none" and att["spec"].medal_tier >= 1:
                elem = att["spec"].medal_element
            if elem != "none":
                apply_element(att, dfn, elem, t)
                if att["spec"].medal_tier >= 3:
                    apply_element(att, dfn, TIER3_SECONDARY_ELEMENT.get(elem, "fire"), t)

            if slot == 2 and "card_encore" in att["spec"].cards and dfn["hp"] > 0:
                apply_damage(att, dfn, raw_dmg * 0.60 * dodged_mult, magical, t, is_skill=True)

        if cc_dur > 0 and dodged_mult > 0.5:
            apply_cc(dfn, cc_dur, t)
        if knock_dur > 0 and dodged_mult > 0.5:
            apply_knockback(dfn, knock_dur, t)
        if slow_dur > 0 and dodged_mult > 0.5:
            dfn["slowed_until"] = max(dfn["slowed_until"], t + slow_dur)

    def skill_cost(att: dict[str, Any], slot: int) -> float:
        ab = catalog.abilities_by_id[att["spec"].actives[slot]]
        cost = num_val(ab.get("momentum_cost"), 25.0)
        if slot == 0 and "card_again_now" in att["spec"].cards:
            cost *= 1.5  # K08: cooldown -35%, momentum cost +50% (CONTENT_AND_BALANCE.md:492)
        return cost

    def od_threshold(att: dict[str, Any]) -> float:
        return 35.0 if "card_short_breath" in att["spec"].cards else catalog.param("OVERDRIVE.Threshold", 60.0)

    t_end = timeout_s
    for step_i in range(steps):
        t = step_i * dt
        if fa["hp"] <= 0 or fb["hp"] <= 0:
            t_end = t
            break

        for me, opp in ((fa, fb), (fb, fa)):
            if me["hp"] <= 0 or opp["hp"] <= 0:
                continue
            # Passive combat regen (base HPRegen ticks in combat; CONTENT_AND_BALANCE.md:50)
            me["hp"] = min(me["st"]["max_hp"], me["hp"] + me["st"]["hp_regen"] * dt)
            me["momentum"] = min(100.0, me["momentum"] + me["st"]["mom_regen"] * dt)
            me["mom_time_sum"] += me["momentum"] * dt
            if me["momentum"] >= od_threshold(me):
                me["mom_ge_od_s"] += dt

            if t < me["cc_until"]:
                continue

            # Finish pending overdrive hold. The charge does not stop basic attacks (the server's basic gating never reads
            # OverdriveCharge: LDCombatMomentumDirector.cpp:136-167); a bar that fell below the threshold releases normally
            # (LDCombatActions.cpp:1513-1524).
            if me["pending_overdrive_slot"] >= 0 and t >= me["hold_until"]:
                slot = me["pending_overdrive_slot"]
                me["pending_overdrive_slot"] = -1
                if me["momentum"] >= skill_cost(me, slot):
                    cast_skill(me, opp, slot, me["momentum"] >= od_threshold(me), t)

            # Dash offensive usage for dash builds
            if t >= me["dash_ready"] and ("card_dash_punch" in me["spec"].cards or "card_pass_through" in me["spec"].cards):
                cd_d = 9.0 * (0.4 if "card_paid_dash" in me["spec"].cards else 1.0) * (0.6 if me["st"]["ks_agility"] else 1.0)
                if "card_bump_reset" in me["spec"].cards:
                    cd_d = max(1.8, cd_d * 0.25)  # bounded contact reset cadence with turn/re-engage
                me["dash_ready"] = t + cd_d
                if me["st"]["ks_agility"]:
                    me["agi_empower_until"] = t + catalog.param("COMBAT.KeystoneAgilityEmpowerDurationSec", 3.0)
                dash_dmg = me["st"]["ad"] * (1.0 if "card_dash_punch" in me["spec"].cards else 0.7) + (60.0 if "card_head_down" in me["spec"].cards else 0.0)
                apply_damage(me, opp, dash_dmg, False, t, is_basic=True)
                if "card_bump_again" in me["spec"].cards:
                    for s_idx in range(3):
                        me["skill_ready"][s_idx] = max(t, me["skill_ready"][s_idx] - 1.5)

            # Cast ready skill: a release needs momentum >= its cost (LDCombatSources.cpp:280 MomentumShort) and no other
            # action in progress. Overdrive policy mirrors the native AI: bar >= threshold and target HP > 40%
            # (LDOpponentPolicy.cpp:696-702). SIM BOUNDARY: human charge timing/choice is not modelled.
            if me["pending_overdrive_slot"] < 0 and t >= me["skill_lock_until"]:
                for slot in range(len(me["spec"].actives)):
                    if t >= me["skill_ready"][slot] and me["momentum"] >= skill_cost(me, slot):
                        if me["spec"].use_overdrive and me["momentum"] >= od_threshold(me) and opp["hp"] > 0.4 * opp["st"]["max_hp"]:
                            me["pending_overdrive_slot"] = slot
                            me["hold_until"] = t + catalog.param("OVERDRIVE.HoldSec", 0.35)
                        else:
                            cast_skill(me, opp, slot, False, t)
                        break

            # Basic attack
            if t >= me["next_basic_t"] and opp["hp"] > 0:
                eff_as = me["st"]["attack_speed"]
                if "card_heated_up" in me["spec"].cards:
                    eff_as = min(5.0, eff_as * (1.0 + 0.04 * me["focus_stacks"]))
                if "card_berserk" in me["spec"].cards:
                    eff_as = min(5.0, eff_as * (1.0 + (1.0 - me["hp"] / me["st"]["max_hp"])))
                interval = max(0.20, 1.0 / eff_as)
                me["next_basic_t"] = t + interval
                me["focus_stacks"] += 1
                if t - me["last_basic_t"] > catalog.param("COMBO.ResetSec", 2.5):
                    me["combo_step"] = 0
                me["last_basic_t"] = t
                step_ratio = [0.9, 0.9, 1.3][me["combo_step"]]
                is_combo3 = me["combo_step"] == 2
                me["combo_step"] = (me["combo_step"] + 1) % 3

                is_crit = (is_combo3 and "card_heavy_mallet" in me["spec"].cards) or (rng.random() < me["st"]["crit"])
                raw_b = me["st"]["ad"] * step_ratio * (2.0 if is_crit else 1.0)
                if t < me["agi_empower_until"]:
                    # Agility keystone empowered basic: +35 + 0.45 AD (CONTENT_AND_BALANCE.md:288).
                    raw_b += catalog.param("COMBAT.KeystoneAgilityDamageBase", 35.0) + catalog.param("COMBAT.KeystoneAgilityADRatio", 0.45) * me["st"]["ad"]
                    me["agi_empower_until"] = -1.0
                if me["st"]["ks_strength"] and (is_crit or is_combo3) and t >= me["str_hp_ready"]:
                    # Strength keystone: crit or 3rd hit on a contender adds 4% current HP physical damage, 1.5s per target.
                    apply_damage(me, opp, 0.04 * opp["hp"], False, t, is_basic=True)
                    me["str_hp_ready"] = t + 1.5
                strikes = 2 if (is_combo3 and "card_both_hands" in me["spec"].cards) else 1
                if "card_quick_hands" in me["spec"].cards and eff_as > 1.35:
                    extra_p = ((eff_as - 1.35) / 0.1) * 0.18
                    if rng.random() < extra_p:
                        strikes += 1
                for _ in range(strikes):
                    apply_damage(me, opp, raw_b * (0.8 if strikes > 1 and is_combo3 and "card_both_hands" in me["spec"].cards else 1.0), False, t, is_basic=True)
                    if "card_scholar_punch" in me["spec"].cards:
                        apply_damage(me, opp, 0.40 * me["st"]["ap"], True, t, is_basic=True)
                    if "card_breach_fist" in me["spec"].cards:
                        opp["breach_stacks"] = min(5, opp["breach_stacks"] + 1)
                    if "card_punch_think" in me["spec"].cards:
                        me["skill_ready"][0] = max(t, me["skill_ready"][0] - 0.5)
                me["momentum"] = min(100.0, me["momentum"] + 10.0 * (interval / 1.334))
                if is_combo3:
                    if "card_third_breath" in me["spec"].cards:
                        me["momentum"] = min(100.0, me["momentum"] + 10.0)
                    if "card_fist_skill" in me["spec"].cards:
                        apply_damage(me, opp, (70.0 + 0.85 * me["st"]["ad"]) * 0.45, False, t, is_skill=True)
                    if "card_tonnage_crush" in me["spec"].cards:
                        apply_damage(me, opp, me["st"]["bonus_hp"] * 0.035, False, t, is_basic=True)

    winner = 0
    if fa["hp"] > 0 and fb["hp"] <= 0:
        winner = 1
    elif fb["hp"] > 0 and fa["hp"] <= 0:
        winner = -1
    elif fa["hp"] / fa["st"]["max_hp"] > fb["hp"] / fb["st"]["max_hp"] + 0.05:
        winner = 1
    elif fb["hp"] / fb["st"]["max_hp"] > fa["hp"] / fa["st"]["max_hp"] + 0.05:
        winner = -1

    return {
        "winner": winner,
        "ttk_s": round(t_end, 2),
        "timed_out": fa["hp"] > 0 and fb["hp"] > 0,
        "a_dmg": round(fa["dmg_dealt"], 1),
        "b_dmg": round(fb["dmg_dealt"], 1),
        "a_burst_2s_ratio": round(fa["dmg_2s"] / fb["st"]["max_hp"], 3),
        "a_cc_lockout_s": round(fb["cc_lockout_total"], 2),
        "a_heal_ratio": round(fa["heal_done"] / fa["st"]["max_hp"], 3),
        "a_overdrives": fa["overdrives"],
        "a_reactions": fa["reactions"],
        "a_casts": fa["casts"],
        "a_mean_momentum": round(fa["mom_time_sum"] / max(dt, t_end), 1),
        "a_momentum_ge_overdrive_share": round(fa["mom_ge_od_s"] / max(dt, t_end), 3),
    }


def simulate_combat_matrix(catalog: CatalogModel, seed: int = 20261128, duels_per_pair: int = 16) -> dict[str, Any]:
    # 1. Bare TTK baseline across L1, L6, L10, L16
    bare_ttk_by_level = {}
    for lv in (1, 6, 10, 16):
        b1 = make_archetype_combatant(catalog, "bare", lv)
        b2 = make_archetype_combatant(catalog, "bare", lv)
        runs = [run_single_duel(catalog, b1, b2, seed + lv * 100 + i) for i in range(duels_per_pair)]
        ttks = [r["ttk_s"] for r in runs]
        bare_ttk_by_level[f"L{lv}"] = {
            "mean_ttk_s": round(statistics.fmean(ttks), 2),
            "median_ttk_s": round(statistics.median(ttks), 2),
            "min_ttk_s": round(min(ttks), 2),
            "max_ttk_s": round(max(ttks), 2),
            "mean_dps_a": round(statistics.fmean(r["a_dmg"] / max(0.5, r["ttk_s"]) for r in runs), 1),
            "mean_momentum_a": round(statistics.fmean(r["a_mean_momentum"] for r in runs), 1),
            "momentum_at_or_above_threshold_share_a": round(statistics.fmean(r["a_momentum_ge_overdrive_share"] for r in runs), 3),
            "mean_overdrives_a": round(statistics.fmean(r["a_overdrives"] for r in runs), 2),
        }

    # 2. Archetype vs L16 Bare (mirroring p3-breakout-samples.json!) AND realistic L12 full-stat matchups
    vs_bare_l16 = {}
    bare16 = make_archetype_combatant(catalog, "bare", 16)
    for arch in ARCHETYPE_DEFS:
        aid = arch["id"]
        # Test BOTH unallocated-points preset (like LDBreakoutPresets bug!) AND properly stat-invested L16 build
        unspent_spec = make_archetype_combatant(catalog, aid, 16, spent_fraction=0.0)
        full_spec = make_archetype_combatant(catalog, aid, 16, spent_fraction=1.0)
        r_unspent = [run_single_duel(catalog, unspent_spec, bare16, seed + 5000 + i) for i in range(duels_per_pair)]
        r_full = [run_single_duel(catalog, full_spec, bare16, seed + 6000 + i) for i in range(duels_per_pair)]
        bare_ttk = bare_ttk_by_level["L16"]["mean_ttk_s"]
        u_ttk = statistics.fmean(r["ttk_s"] for r in r_unspent)
        f_ttk = statistics.fmean(r["ttk_s"] for r in r_full)
        vs_bare_l16[aid] = {
            "name": arch["name"],
            "preset_no_stats_ttk_s": round(u_ttk, 2),
            "preset_no_stats_speedup_x": round(bare_ttk / max(0.5, u_ttk), 2),
            "full_l16_ttk_s": round(f_ttk, 2),
            "full_l16_speedup_x": round(bare_ttk / max(0.5, f_ttk), 2),
            "win_rate_vs_bare": round(sum(1 for r in r_full if r["winner"] == 1) / duels_per_pair, 3),
        }

    # 3. 12x12 Pairwise Matchup Matrix at L12 (realistic field endgame level)
    arch_ids = [a["id"] for a in ARCHETYPE_DEFS]
    specs_l12 = {aid: make_archetype_combatant(catalog, aid, 12, spent_fraction=1.0) for aid in arch_ids}
    matrix: dict[str, dict[str, float]] = {aid: {} for aid in arch_ids}
    summary_by_arch: dict[str, dict[str, Any]] = {}
    all_ttks: list[float] = []
    all_cc: list[float] = []

    for i, a1 in enumerate(arch_ids):
        wins = 0.0
        matches = 0
        bursts = []
        ttks_a = []
        heals_a = []
        for j, a2 in enumerate(arch_ids):
            if i == j:
                matrix[a1][a2] = 0.5
                continue
            pair_wins = 0.0
            for k in range(duels_per_pair):
                res = run_single_duel(catalog, specs_l12[a1], specs_l12[a2], seed + i * 1000 + j * 50 + k)
                if res["winner"] == 1:
                    pair_wins += 1.0
                elif res["winner"] == 0:
                    pair_wins += 0.5
                ttks_a.append(res["ttk_s"])
                bursts.append(res["a_burst_2s_ratio"])
                heals_a.append(res["a_heal_ratio"])
                all_ttks.append(res["ttk_s"])
                all_cc.append(res["a_cc_lockout_s"])
            wr = round(pair_wins / duels_per_pair, 3)
            matrix[a1][a2] = wr
            wins += pair_wins
            matches += duels_per_pair
        counters = [a2 for a2, wr in matrix[a1].items() if a2 != a1 and wr < 0.40]
        favored = [a2 for a2, wr in matrix[a1].items() if a2 != a1 and wr > 0.60]
        summary_by_arch[a1] = {
            "name": specs_l12[a1].name,
            "overall_win_rate_l12": round(wins / max(1, matches), 3),
            "mean_ttk_s": round(statistics.fmean(ttks_a), 2),
            "mean_burst_2s_hp_share": round(statistics.fmean(bursts), 3),
            "mean_heal_hp_share": round(statistics.fmean(heals_a), 3),
            "countered_by_count": len(counters),
            "countered_by": counters,
            "favored_against_count": len(favored),
        }

    # 4. Overdrive 0.35s hold DPS trade-off micro-experiment
    overdrive_ab = {}
    for aid in ("skill_engine", "attack_speed_onhit", "one_shot_artillery"):
        s_on = make_archetype_combatant(catalog, aid, 12, 1.0)
        s_off = make_archetype_combatant(catalog, aid, 12, 1.0)
        s_off.use_overdrive = False
        opp = make_archetype_combatant(catalog, "lifesteal_bruiser", 12, 1.0)
        r_on = [run_single_duel(catalog, s_on, opp, seed + 9000 + k) for k in range(20)]
        r_off = [run_single_duel(catalog, s_off, opp, seed + 9000 + k) for k in range(20)]
        overdrive_ab[aid] = {
            "with_overdrive_hold_dps": round(statistics.fmean(r["a_dmg"] / max(0.5, r["ttk_s"]) for r in r_on), 1),
            "without_overdrive_hold_dps": round(statistics.fmean(r["a_dmg"] / max(0.5, r["ttk_s"]) for r in r_off), 1),
            "with_overdrive_win_rate": round(sum(1 for r in r_on if r["winner"] == 1) / 20.0, 2),
            "without_overdrive_win_rate": round(sum(1 for r in r_off if r["winner"] == 1) / 20.0, 2),
            "with_overdrive_mean_overdrives": round(statistics.fmean(r["a_overdrives"] for r in r_on), 2),
            "without_overdrive_mean_momentum": round(statistics.fmean(r["a_mean_momentum"] for r in r_off), 1),
            "without_overdrive_time_share_at_or_above_threshold": round(statistics.fmean(r["a_momentum_ge_overdrive_share"] for r in r_off), 3),
        }

    # 5. Level-lead snowball combat power table (how much stronger is +1, +2, +4, +6 levels?)
    level_gap_table = []
    for l_hi, l_lo in ((6, 5), (8, 6), (10, 7), (12, 8), (16, 10)):
        hi = make_archetype_combatant(catalog, "attack_speed_onhit", l_hi, 1.0)
        lo = make_archetype_combatant(catalog, "attack_speed_onhit", l_lo, 1.0)
        runs = [run_single_duel(catalog, hi, lo, seed + l_hi * 31 + k) for k in range(20)]
        level_gap_table.append({
            "matchup": f"L{l_hi} vs L{l_lo} (+{l_hi - l_lo} lvls)",
            "higher_level_win_rate": round(sum(1 for r in runs if r["winner"] == 1) / 20.0, 3),
            "mean_ttk_s": round(statistics.fmean(r["ttk_s"] for r in runs), 2),
        })

    return {
        "metadata": {"seed": seed, "duels_per_pair": duels_per_pair, "commit": catalog.commit_sha},
        "bare_ttk_by_level": bare_ttk_by_level,
        "breakout_vs_bare_l16": vs_bare_l16,
        "l12_archetype_summary": summary_by_arch,
        "l12_matchup_matrix": matrix,
        "overdrive_hold_tradeoff": overdrive_ab,
        "level_gap_snowball": level_gap_table,
        "global_combat_distributions": {
            "ttk_p10_s": round( sorted(all_ttks)[int(0.10 * len(all_ttks))], 2),
            "ttk_median_s": round(statistics.median(all_ttks), 2),
            "ttk_mean_s": round(statistics.fmean(all_ttks), 2),
            "ttk_p90_s": round(sorted(all_ttks)[int(0.90 * len(all_ttks))], 2),
            "cc_lockout_mean_s": round(statistics.fmean(all_cc), 2),
            "cc_lockout_max_s": round(max(all_cc), 2),
        },
    }


# ---------------------------------------------------------------------------
# 4. Spatial PvE, Economy & 8-Strategy-Bot FFA + Showdown Simulator
# ---------------------------------------------------------------------------

def guard_ratchet_floor(guard_s: float) -> float:
    """Latched showdown guard floor from SHOWDOWN.GuardRatchetSec [7, 14] (LDCaptureReducer.cpp:70-95, 461-478)."""
    return 14.0 if guard_s >= 14.0 else (7.0 if guard_s >= 7.0 else 0.0)


BOT_PROFILES = {
    "Farmer": {"farm_w": 1.8, "pvp_w": 0.3, "third_party_w": 0.4, "event_w": 0.9, "shop_iq": 0.6, "micro_skill": 0.55, "showdown_style": "late_wipe"},
    "Aggressor": {"farm_w": 0.6, "pvp_w": 1.8, "third_party_w": 1.1, "event_w": 1.2, "shop_iq": 0.6, "micro_skill": 0.70, "showdown_style": "brawl_kills"},
    "Opportunist": {"farm_w": 1.0, "pvp_w": 0.4, "third_party_w": 2.2, "event_w": 1.3, "shop_iq": 0.7, "micro_skill": 0.65, "showdown_style": "poach_ratchet"},
    "ObjectiveHunter": {"farm_w": 1.0, "pvp_w": 0.8, "third_party_w": 0.9, "event_w": 2.1, "shop_iq": 0.75, "micro_skill": 0.60, "showdown_style": "early_spray_guard"},
    "Turtle": {"farm_w": 1.1, "pvp_w": 0.15, "third_party_w": 0.2, "event_w": 0.6, "shop_iq": 0.65, "micro_skill": 0.50, "showdown_style": "tank_circle_timeout"},
    "ComboSpecialist": {"farm_w": 1.2, "pvp_w": 0.9, "third_party_w": 0.8, "event_w": 1.0, "shop_iq": 0.95, "micro_skill": 0.75, "showdown_style": "burst_zone"},
    "BuildOptimizer": {"farm_w": 1.45, "pvp_w": 0.85, "third_party_w": 1.35, "event_w": 1.5, "shop_iq": 1.0, "micro_skill": 0.72, "showdown_style": "adaptive"},
    "Beginner": {"farm_w": 0.75, "pvp_w": 0.7, "third_party_w": 0.3, "event_w": 0.5, "shop_iq": 0.15, "micro_skill": 0.20, "showdown_style": "naive_rush"},
}


def compute_static_pve_economy(catalog: CatalogModel) -> dict[str, Any]:
    """Exact arithmetic audit of camp XP budget, clear times, and single-currency stat-point ROI."""
    tier_rows = {}
    for mon in catalog.content["camps"]["monsters"]:
        tier = mon["tier"]
        if tier in tier_rows:
            continue
        cnt = int(mon["count_per_camp"])
        hp = num_val(mon["hp"])
        armor = num_val(mon["armor"])
        ad = num_val(mon["attack"])
        aspd = num_val(mon["attack_speed"])
        xp = int(mon["reward_xp"])
        eff_hp_total = cnt * hp * (1.0 + armor / 100.0)
        tier_rows[tier] = {
            "count_per_camp": cnt,
            "unit_hp": hp,
            "armor_mr": armor,
            "total_camp_ehp_stage0": round(eff_hp_total, 1),
            "camp_dps_stage0": round(cnt * ad * aspd, 1),
            "camp_xp_stage0": cnt * xp,
            "xp_per_100_ehp": round((cnt * xp) / (eff_hp_total / 100.0), 2),
        }

    # SIM BOUNDARY: 34 = content.json camps.placements (LDDemoContentImport.cpp:3218-3224); L_Demo.umap authors 64 camp
    # actors (LDStaticScene.cpp:508-526). Which count is authoritative is disputed (S2); the lab uses the content 34.
    counts_on_map = {"Low": 18, "High": 10, "Elite": 4, "Ancient": 2}
    # SIM BOUNDARY: unreachable camps are a snapshot of issue #353, not read from navigation data.
    unreachable_by_tier = {"Low": 3, "High": 4, "Elite": 1, "Ancient": 0}  # from #353 (plus h09 nav mismatch)
    seg_stages = [0, 3, 6]
    xp_growth = catalog.param("CAMP.GrowthRewardXPRatioPerStage", 0.06)
    hp_growth = catalog.param("CAMP.GrowthHPRatioPerStage", 0.12)

    segment_xp_ideal = []
    segment_xp_live_bug353 = []
    for stg in seg_stages:
        mult = 1.0 + stg * xp_growth
        ideal = sum(counts_on_map[t] * tier_rows[t]["camp_xp_stage0"] * mult for t in counts_on_map)
        live = sum((counts_on_map[t] - unreachable_by_tier[t]) * tier_rows[t]["camp_xp_stage0"] * mult for t in counts_on_map)
        segment_xp_ideal.append(round(ideal, 1))
        segment_xp_live_bug353.append(round(live, 1))

    total_ideal = sum(segment_xp_ideal)
    total_live = sum(segment_xp_live_bug353)
    per_player_by_lobby = {}
    for p_cnt in (2, 4, 6, 8):
        xp_id = total_ideal / p_cnt
        xp_lv = total_live / p_cnt
        per_player_by_lobby[f"{p_cnt}_players"] = {
            "max_camp_xp_per_player_ideal": round(xp_id, 1),
            "level_from_camps_only_ideal": catalog.level_for_xp(xp_id),
            "max_camp_xp_per_player_with_issue353": round(xp_lv, 1),
            "level_from_camps_only_with_issue353": catalog.level_for_xp(xp_lv),
            "share_of_l16_xp_5340": round(xp_lv / 5340.0, 3),
        }

    # Stat-point vs Shop-card marginal ROI calculation at L8 (base HP 1735, AD 92.4, Armor 32)
    # 1 Stat Point in Physique = +70 HP (+4.03% base HP); 1 pt in Strength = +3.5 AD (+3.79% base AD) + 0.6% crit + 1/18 of +14% keystone (~0.78% amp) -> ~4.8% DPS/pt!
    # Buying a Common card costs 2 pts (~9.6% raw DPS or +140 HP sacrificed), Rare costs 3 pts (~14.4% DPS or +210 HP sacrificed), Random Medal costs 4 pts (+280 HP sacrificed for 20% chance of desired element!).
    economy_roi = {
        "stat_point_gold_value_conflict": {
            "1_pt_physique": "+70 HP, +0.08 HP/s (+1/18 of 12% MaxHP periodic shield keystone)",
            "1_pt_strength": "+3.5 AD, +0.6% Crit (+1/18 of +14% universal damage keystone)",
            "1_pt_brains": "+6 AP, +1 Haste, +0.06 Mom/s (+1/18 of -1s CD & 30% Mom refund keystone)",
            "common_card_cost_pts": 2,
            "rare_card_cost_pts": 3,
            "legendary_card_cost_pts": 4,
            "skill_point_cost_pts": 3,
            "random_medal_cost_pts": 4,
            "break_even_note": "Before reaching the first 18-point Keystone (requires Level 6 with 0 shop spend, or Level 8-9 if buying 2 shop items), spending 3-4 stat points in R1 Shop reduces immediate EHP/DPS unless the purchased card provides >15% unconditional output.",
        },
        "camp_tiers": tier_rows,
        "segment_camp_xp_ideal_34_camps": segment_xp_ideal,
        "segment_camp_xp_live_26_reachable": segment_xp_live_bug353,
        "total_match_camp_xp_ideal": round(total_ideal, 1),
        "total_match_camp_xp_live_issue353": round(total_live, 1),
        "per_player_camp_ceiling_by_lobby_size": per_player_by_lobby,
        "hp_growth_stage6_multiplier": round(1.0 + 6 * hp_growth, 2),
        "xp_growth_stage6_multiplier": round(1.0 + 6 * xp_growth, 2),
        "f3_farming_efficiency_decay": round((1.0 + 6 * xp_growth) / (1.0 + 6 * hp_growth), 3),
    }
    return economy_roi


def simulate_ffa_tournament(
    catalog: CatalogModel,
    seed: int = 20261128,
    matches_per_setting: int = 250,
) -> dict[str, Any]:
    # Prepare real camp spatial data
    camps_template = []
    for p in catalog.content["camps"]["placements"]:
        aid = p["anchor_id"]
        mon = catalog.monsters_by_id[p["monster_id"]]
        x, y, _ = catalog.anchor_xy(aid)
        camps_template.append({
            "id": aid,
            "tier": mon["tier"],
            "xp": int(mon["reward_xp"]) * int(mon["count_per_camp"]),
            "ehp": num_val(mon["hp"]) * int(mon["count_per_camp"]) * (1.0 + num_val(mon["armor"]) / 100.0),
            "x": x,
            "y": y,
            "unreachable_353": aid in UNREACHABLE_CAMPS_ISSUE_353,
        })

    bot_names = list(BOT_PROFILES.keys())

    def run_setting(
        player_count: int,
        victory_mode: str = "V1_CurrentSoloFlag",
        camp_respawn_s: float | None = None,
        decoupled_shop: bool = False,
        fix_issue353: bool = False,
        include_oracle: bool = False,
    ) -> dict[str, Any]:
        mode_seed = int(hashlib.sha256(victory_mode.encode("utf-8")).hexdigest()[:8], 16) % 100000
        rng = random.Random(seed + player_count * 997 + mode_seed + (17 if camp_respawn_s else 0) + (31 if decoupled_shop else 0) + (53 if fix_issue353 else 0))
        wins_by_bot: Counter[str] = Counter()
        appearances_by_bot: Counter[str] = Counter()
        f1_leader_won = 0
        comebacks_from_bottom_half = 0
        third_party_kills = 0
        total_pvp_kills = 0
        end_reasons: Counter[str] = Counter()
        final_levels: list[int] = []
        lead_changes_list: list[int] = []
        spike_times_s: list[float] = []
        combat_share_list: list[float] = []
        travel_share_list: list[float] = []
        rest_share_list: list[float] = []
        pvp_per_match: list[int] = []

        for m_idx in range(matches_per_setting):
            # Assign bots to seats
            roster_bots = [bot_names[(m_idx + s) % len(bot_names)] for s in range(player_count)]
            if include_oracle and player_count >= 4:
                roster_bots[0] = "OracleFullInfo"

            players = []
            for pid, bname in enumerate(roster_bots):
                appearances_by_bot[bname] += 1
                prof = BOT_PROFILES["BuildOptimizer"] if bname == "OracleFullInfo" else BOT_PROFILES[bname]
                ang = 2.0 * math.pi * pid / player_count
                players.append({
                    "id": pid,
                    "bot": bname,
                    "oracle": bname == "OracleFullInfo",
                    "prof": prof,
                    "xp": 0.0,
                    "level": 1,
                    "bonus_pts": 0,
                    "cards_quality": 1.0,
                    "x": 2800.0 * math.cos(ang),
                    "y": 2800.0 * math.sin(ang),
                    "hp_ratio": 1.0,
                    "kills_seg": 0,
                    "kills_total": 0,
                    "deaths": 0,
                    "spike_t": None,
                    "combat_s": 0.0,
                    "travel_s": 0.0,
                    "idle_wait_s": 0.0,
                    "guard_s": 0.0,
                    "showdown_kills": 0,
                    "showdown_dmg": 0.0,
                    "private_pole": False,
                })

            f1_ranks: dict[int, int] = {}
            # 3 Field segments x 160s (simulate in 10s macro-slices)
            for seg in range(3):
                stage = seg * 3
                xp_mult = 1.0 + stage * 0.06
                hp_mult = 1.0 + stage * 0.12
                camps_state = []
                for c in camps_template:
                    avail = fix_issue353 or (not c["unreachable_353"])
                    camps_state.append({**c, "alive": avail, "respawn_at": 9999.0})
                for p in players:
                    p["kills_seg"] = 0
                    p["hp_ratio"] = 1.0

                for slice_i in range(16):
                    t_seg = slice_i * 10.0
                    t_global = seg * 160.0 + t_seg
                    if camp_respawn_s is not None:
                        for c in camps_state:
                            # Only Low and High camps respawn intra-segment; Elite and Ancient remain strategic 1-per-segment objectives
                            if not c["alive"] and c["tier"] in ("Low", "High") and (fix_issue353 or not c["unreachable_353"]) and t_seg >= c["respawn_at"]:
                                c["alive"] = True

                    # Airdrop at 40s, 110s of each segment (AIRDROP.LandSec). SIM BOUNDARY: landing points are uniform random
                    # instead of the authored airdrop points; claim odds are bot-profile assumptions.
                    airdrop_active = t_seg in (40.0, 110.0)
                    ax, ay = rng.uniform(-2200, 2200), rng.uniform(-2200, 2200)

                    order = list(range(player_count))
                    rng.shuffle(order)
                    skirmish_pairs: list[tuple[int, int]] = []

                    for pid in order:
                        p = players[pid]
                        prof = p["prof"]
                        # Power score
                        lvl = catalog.level_for_xp(p["xp"])
                        p["level"] = lvl
                        if p["spike_t"] is None and (lvl >= 6 or p["cards_quality"] >= 2.2):
                            p["spike_t"] = t_global

                        # Event contest check
                        if airdrop_active and (p["oracle"] or math.hypot(p["x"] - ax, p["y"] - ay) < 2600.0 * prof["event_w"]):
                            p["x"], p["y"] = ax, ay
                            p["travel_s"] += 6.0
                            p["combat_s"] += 4.0
                            if rng.random() < 0.45 * prof["event_w"]:
                                p["xp"] += catalog.param("AIRDROP.RewardXP", 200.0)
                                # SIM BOUNDARY: the airdrop's build draw (50% active blueprint / 50% scarce component,
                                # LDGameModeAirdrop.cpp:71-73) is abstracted as +0.35 card quality.
                                p["cards_quality"] += 0.35
                                airdrop_active = False
                                continue

                        # SIM BOUNDARY: 10s macro-slices. 1600cm*1.8 camp search and 1350cm PvP contact radii fold movement
                        # into one step; they are NOT the 1120cm VISION.RadiusCm (VISION_AND_UI.md:9), and fog, camp marks
                        # and leader reveals are not modelled.
                        vis_r = 99999.0 if p["oracle"] else 1600.0  # local sweep radius over 10s movement
                        live_camps = [c for c in camps_state if c["alive"]]
                        near_camps = [c for c in live_camps if math.hypot(c["x"] - p["x"], c["y"] - p["y"]) <= vis_r * 1.8]
                        if not near_camps and live_camps:
                            # Must roam to search
                            p["travel_s"] += 7.5
                            p["idle_wait_s"] += 2.5
                            ang = rng.uniform(0, 2 * math.pi)
                            p["x"] = max(-5200.0, min(5200.0, p["x"] + 1800.0 * math.cos(ang)))
                            p["y"] = max(-5200.0, min(5200.0, p["y"] + 1800.0 * math.sin(ang)))
                            near_camps = [c for c in live_camps if math.hypot(c["x"] - p["x"], c["y"] - p["y"]) <= 1800.0]

                        # Check nearby players for PvP or third-party
                        near_enemies = [
                            q for q in players
                            if q["id"] != pid and math.hypot(q["x"] - p["x"], q["y"] - p["y"]) <= (3000.0 if p["oracle"] else 1350.0)
                        ]
                        wounded_enemies = [q for q in near_enemies if q["hp_ratio"] < 0.62]

                        if wounded_enemies and rng.random() < 0.38 * prof["third_party_w"]:
                            victim = min(wounded_enemies, key=lambda q: q["hp_ratio"])
                            skirmish_pairs.append((pid, victim["id"]))
                            p["combat_s"] += 6.5
                            p["travel_s"] += 3.5
                        elif near_enemies and rng.random() < 0.22 * prof["pvp_w"]:
                            victim = rng.choice(near_enemies)
                            skirmish_pairs.append((pid, victim["id"]))
                            p["combat_s"] += 7.0
                            p["travel_s"] += 3.0
                        elif near_camps:
                            # Pick camp: Oracle picks max XP/time; others pick nearest
                            if p["oracle"]:
                                camp = max(near_camps, key=lambda c: c["xp"] / max(200.0, math.hypot(c["x"] - p["x"], c["y"] - p["y"])))
                            else:
                                camp = min(near_camps, key=lambda c: math.hypot(c["x"] - p["x"], c["y"] - p["y"]))
                            dist = math.hypot(camp["x"] - p["x"], camp["y"] - p["y"])
                            # Move speed does not grow with level (UNIT.MoveSpeedCmPerSec 240; only agility adds 0.5%/pt).
                            walk_s = min(6.5, dist / catalog.param("UNIT.MoveSpeedCmPerSec", 240.0))
                            # SIM BOUNDARY: camp clear DPS is a level-linear estimate, not the native combat model.
                            dps_est = (75.0 + 18.0 * p["level"]) * (0.8 + 0.25 * p["cards_quality"])
                            clear_s = min(9.0, (camp["ehp"] * hp_mult) / max(40.0, dps_est))
                            p["travel_s"] += walk_s
                            p["combat_s"] += max(0.0, 10.0 - walk_s)
                            p["x"], p["y"] = camp["x"], camp["y"]
                            if walk_s + clear_s <= 11.5:
                                camp["alive"] = False
                                if camp_respawn_s is not None:
                                    camp["respawn_at"] = t_seg + camp_respawn_s
                                p["xp"] += camp["xp"] * xp_mult
                                p["hp_ratio"] = max(0.55, min(1.0, p["hp_ratio"] - 0.06 * (1 if camp["tier"] == "Low" else 2) + 0.15))
                        else:
                            # Map starved of camps! Forced roam or mid brawl
                            p["travel_s"] += 8.0
                            p["idle_wait_s"] += 2.0
                            p["x"] *= 0.65
                            p["y"] *= 0.65

                    # Resolve PvP skirmishes in this slice
                    for att_id, def_id in skirmish_pairs:
                        att = players[att_id]
                        dfn = players[def_id]
                        was_third_party = dfn["hp_ratio"] < 0.65
                        # SIM BOUNDARY: skirmish power is an abstract level/card/micro score, not the duel simulator, and
                        # the 58% kill-vs-escape split is an assumption.
                        att_pow = (1.0 + 0.13 * att["level"]) * (0.85 + 0.18 * att["cards_quality"]) * (0.8 + 0.4 * att["prof"]["micro_skill"]) * att["hp_ratio"]
                        dfn_pow = (1.0 + 0.13 * dfn["level"]) * (0.85 + 0.18 * dfn["cards_quality"]) * (0.8 + 0.4 * dfn["prof"]["micro_skill"]) * dfn["hp_ratio"]
                        if rng.random() < att_pow / max(0.1, att_pow + dfn_pow):
                            if rng.random() < 0.58:  # kill secured (vs escape)
                                total_pvp_kills += 1
                                if was_third_party:
                                    third_party_kills += 1
                                # Kill XP = 70 + 20*(victimLevel-1), scaled by 1 - 0.12*(killer-victim) in [0.4, 1.6]
                                # (LDProgressionRules.cpp:206-234). SIM BOUNDARY: assist XP, bounty and kill-medal chance not modelled;
                                # the per-kill card-quality/bonus-point rewards below are lab abstractions with no native counterpart.
                                base_kxp = catalog.param("REWARD.PlayerXP.Base", 70.0) + catalog.param("REWARD.PlayerXP.PerLevel", 20.0) * (dfn["level"] - 1)
                                gap = dfn["level"] - att["level"]
                                scale = max(0.40, min(1.60, 1.0 + 0.12 * gap))
                                att["xp"] += base_kxp * scale
                                att["kills_seg"] += 1
                                att["kills_total"] += 1
                                if att["kills_seg"] <= 2:
                                    att["cards_quality"] += 0.28
                                elif att["kills_seg"] <= 5:
                                    att["bonus_pts"] += 1
                                dfn["deaths"] += 1
                                dfn["idle_wait_s"] += 6.0 + 0.5 * dfn["level"]
                                dfn["hp_ratio"] = 1.0
                            else:
                                dfn["hp_ratio"] = max(0.25, dfn["hp_ratio"] - 0.35)
                                att["hp_ratio"] = max(0.40, att["hp_ratio"] - 0.20)
                        else:
                            att["hp_ratio"] = max(0.30, att["hp_ratio"] - 0.35)

                # End of segment: update levels, record F1 ranking, run Rest Shop + PK
                for p in players:
                    p["level"] = catalog.level_for_xp(p["xp"])
                    # Natural card offers from level-ups
                    p["cards_quality"] = max(p["cards_quality"], 1.0 + 0.22 * ((p["level"] - 1) // 3) * (0.5 + 0.6 * p["prof"]["shop_iq"]))

                if seg == 0:
                    ranked_f1 = sorted(players, key=lambda r: (r["xp"], r["kills_total"]), reverse=True)
                    for r_pos, row in enumerate(ranked_f1):
                        f1_ranks[row["id"]] = r_pos + 1

                # Rest room + Shop + PK (R1, R2)
                if decoupled_shop:
                    for p in players:
                        p["cards_quality"] += 0.32 * (0.4 + 0.6 * p["prof"]["shop_iq"])
                else:
                    for p in players:
                        # Spending stat points trades off against stats unless already past 18-pt keystone
                        if p["level"] >= 9 and p["prof"]["shop_iq"] > 0.65:
                            p["cards_quality"] += 0.22
                            p["bonus_pts"] -= 2

                if seg in (0, 1):
                    # Optional PK in R1/R2
                    pk_entrants = [p for p in players if p["prof"]["pvp_w"] >= 0.85 or p["oracle"]]
                    if len(pk_entrants) >= 2:
                        d1, d2 = rng.sample(pk_entrants, 2)
                        d1["bonus_pts"] += 1
                        d2["bonus_pts"] += 1
                        w = d1 if (d1["level"] + d1["prof"]["micro_skill"] * 3 + rng.random() * 2) >= (d2["level"] + d2["prof"]["micro_skill"] * 3 + rng.random() * 2) else d2
                        w["bonus_pts"] += 2
                        w["combat_s"] += 22.0
                        # SIM BOUNDARY: PK entry policy, the 22s fight length and PK point rewards are lab assumptions
                        # (code: R1/R2 PK only if >=2 sign up, LDMatchTimeline.cpp:220-236). The PK block time is counted once
                        # for every player in rest_s below, so spectators no longer get an extra idle charge (was double-counted).

            # R3 Final Mod selection
            # SIM BOUNDARY: final mods are player choices; the lab hands final_private_pole (spray 3.5s, guard 28s,
            # radius 280cm, content.json final_mods) to 35% of Turtle/ObjectiveHunter bots and models no other mod.
            for p in players:
                p["level"] = catalog.level_for_xp(p["xp"])
                final_levels.append(p["level"])
                if victory_mode == "V1_CurrentSoloFlag" and p["bot"] in ("Turtle", "ObjectiveHunter") and rng.random() < 0.35:
                    p["private_pole"] = True  # simulates final_mod private flagpole / alt capture mod

            # Showdown (150s in 5s steps = 30 steps)
            holder_id: int | None = None
            lead_changes = 0
            alive_respawns = {p["id"]: 2 for p in players}
            dead_until = {p["id"]: 0.0 for p in players}
            scores = {p["id"]: 0.0 for p in players}
            winner_id: int | None = None
            win_reason = "ObjectiveTimeout"

            for sd_step in range(2, 30):  # first 10s locked
                t_sd = sd_step * 5.0
                active_p = [p for p in players if t_sd >= dead_until[p["id"]] and alive_respawns[p["id"]] >= 0]
                if not active_p:
                    break
                # A clear-out win exists only for the holder of final_clear_out (LDCaptureReducer.cpp:839-856), which the lab
                # never assigns; the former generic "last one standing after 90s wins" rule had no native counterpart.

                # Combat power in showdown
                # SIM BOUNDARY: showdown power, Turtle's 1.18 power bonus and 0.42 survive chance, circle-entry odds and
                # the fight frequency are invented bot-behaviour assumptions; Turtle/timeout results inherit them.
                powers = {
                    p["id"]: (1.0 + 0.135 * p["level"] + 0.025 * p["bonus_pts"]) * (0.85 + 0.22 * p["cards_quality"]) * (0.8 + 0.35 * p["prof"]["micro_skill"])
                    * (1.18 if p["bot"] == "Turtle" else 1.0)
                    for p in active_p
                }

                # Determine who steps into central 450cm circle (or contests objective)
                final_ring_r = 950.0 if victory_mode == "V5_MinimalTunedFlag" else 600.0
                ring_r = 1800.0 if t_sd < 75.0 else max(final_ring_r, 1800.0 - ((1800.0 - final_ring_r) * (t_sd - 75.0) / 75.0))
                in_circle = []
                for p in active_p:
                    base_enter_p = 0.78 if ring_r <= 850.0 else (0.55 if p["prof"]["showdown_style"] in ("early_spray_guard", "tank_circle_timeout") else 0.34)
                    if rng.random() < base_enter_p:
                        in_circle.append(p)

                # Combat death resolution in 5s window
                if len(active_p) >= 2 and rng.random() < min(0.85, 0.22 * len(active_p)):
                    fighters = rng.sample(active_p, 2)
                    f_a, f_b = fighters[0], fighters[1]
                    pa, pb = powers[f_a["id"]], powers[f_b["id"]]
                    killer, victim = (f_a, f_b) if rng.random() < pa / (pa + pb) else (f_b, f_a)
                    # Turtles survive burst more often
                    survive_chance = 0.42 if victim["bot"] == "Turtle" else 0.20
                    if rng.random() > survive_chance:
                        killer["showdown_kills"] += 1
                        killer["showdown_dmg"] += 950.0
                        if victory_mode == "V1_CurrentSoloFlag" and holder_id == killer["id"]:
                            killer["guard_s"] += 4.0
                        elif victory_mode == "V5_MinimalTunedFlag":
                            if holder_id == killer["id"]:
                                killer["guard_s"] += 6.0
                            else:
                                holder_id = killer["id"]
                                lead_changes += 1
                                killer["guard_s"] = max(killer["guard_s"], 3.0)
                        elif victory_mode == "V4_MultiPathMatchPoint":
                            scores[killer["id"]] += 22.0

                        if t_sd <= 90.0 and alive_respawns[victim["id"]] > 0:
                            wait = 18.0 if alive_respawns[victim["id"]] == 2 else 24.0
                            alive_respawns[victim["id"]] -= 1
                            dead_until[victim["id"]] = t_sd + wait
                        else:
                            alive_respawns[victim["id"]] = -1
                        if victory_mode == "V1_CurrentSoloFlag":
                            # Death ends progress above the latched ratchet floor (LDCaptureReducer.cpp:461-478).
                            victim["guard_s"] = guard_ratchet_floor(victim["guard_s"])
                        in_circle = [x for x in in_circle if x["id"] != victim["id"]]

                # Objective progression by victory_mode
                if victory_mode == "V1_CurrentSoloFlag":
                    # Private pole progress for private_pole mod holders when off-center
                    for p in active_p:
                        if p["private_pole"] and p not in in_circle and rng.random() < 0.45:
                            p["guard_s"] += 3.2
                            if p["guard_s"] >= 28.0:
                                winner_id = p["id"]
                                win_reason = "GuardSucceeded_PrivatePole"
                                break
                    if winner_id is not None:
                        break
                    if len(in_circle) == 1:
                        sole = in_circle[0]
                        if holder_id != sole["id"]:
                            holder_id = sole["id"]
                            lead_changes += 1
                            sole["guard_s"] = guard_ratchet_floor(sole["guard_s"])
                        else:
                            sole["guard_s"] += 5.0
                            if sole["guard_s"] >= catalog.param("CAPTURE.GuardSec", 20.0):
                                winner_id = sole["id"]
                                win_reason = "GuardSucceeded"
                                break
                    # Contested circle pauses guard progress (LDCaptureReducer.cpp:459-488).
                    # SIM BOUNDARY: the 3.5s spray is folded into the 5s step.

                elif victory_mode == "V2_RotatingMultiTurf":
                    # 2 active zones -> splits crowd so 1v1/2v1 fights resolve cleanly and build identity shines
                    for zone_i in range(2):
                        z_cand = [p for idx, p in enumerate(active_p) if idx % 2 == zone_i]
                        if z_cand:
                            best_z = max(z_cand, key=lambda x: powers[x["id"]] * rng.uniform(0.75, 1.25))
                            prev_top = max(scores, key=scores.get)
                            scores[best_z["id"]] += 12.5
                            new_top = max(scores, key=scores.get)
                            if new_top != prev_top:
                                lead_changes += 1
                            if scores[best_z["id"]] >= 100.0:
                                winner_id = best_z["id"]
                                win_reason = "MultiTurfScoreReached"
                                break
                    if winner_id is not None:
                        break

                elif victory_mode == "V3_KingCrownCarrier":
                    if holder_id is None or holder_id not in [x["id"] for x in active_p]:
                        holder_id = max(active_p, key=lambda x: powers[x["id"]] * rng.uniform(0.7, 1.3))["id"]
                        lead_changes += 1
                    carrier = next(x for x in active_p if x["id"] == holder_id)
                    # Holding crown draws focus from top 2 rivals; fighting charges faster than running
                    rivals = [x for x in active_p if x["id"] != holder_id]
                    if rivals and rng.random() < 0.45:
                        chal = max(rivals, key=lambda x: powers[x["id"]])
                        if rng.random() < powers[chal["id"]] * 1.15 / (powers[carrier["id"]] + powers[chal["id"]] * 1.15):
                            holder_id = chal["id"]
                            lead_changes += 1
                        else:
                            scores[carrier["id"]] += 18.0
                    else:
                        scores[carrier["id"]] += 11.0
                    if scores[holder_id] >= 100.0:
                        winner_id = holder_id
                        win_reason = "CrownCharged"
                        break

                elif victory_mode == "V4_MultiPathMatchPoint":
                    if len(in_circle) == 1:
                        scores[in_circle[0]["id"]] += 14.0
                    elif len(in_circle) >= 2:
                        dom = max(in_circle, key=lambda x: powers[x["id"]] * rng.uniform(0.8, 1.2))
                        scores[dom["id"]] += 9.0
                    top_p = max(active_p, key=lambda x: scores[x["id"]])
                    if scores[top_p["id"]] >= 100.0:
                        winner_id = top_p["id"]
                        win_reason = "MatchPointConverted"
                        break

                elif victory_mode == "V5_MinimalTunedFlag":
                    # Exact spray ownership + Active Combat Tug-of-War Guard:
                    # - Solo in circle: claims ownership (if unowned) or advances +5.0s guard per 5s step.
                    # - Owner + 1 passive/evading challenger (e.g. Opportunist/Turtle stalling edge without out-trading): advances +2.0s guard.
                    # - Owner + 1 active combatant dealing damage OR >=2 challengers: 0.0s frozen until fight resolves!
                    if len(in_circle) == 1:
                        sole = in_circle[0]
                        if holder_id != sole["id"]:
                            holder_id = sole["id"]
                            lead_changes += 1
                            sole["guard_s"] = 7.0 if sole["guard_s"] >= 7.0 else 0.0
                        else:
                            sole["guard_s"] += 5.0
                    elif len(in_circle) == 2 and holder_id in [x["id"] for x in in_circle]:
                        owner_p = next(x for x in in_circle if x["id"] == holder_id)
                        chal_p = next(x for x in in_circle if x["id"] != holder_id)
                        # If challenger only stalls passively without engaging, owner still ticks +2.0s; active duelists freeze the clock!
                        if chal_p["prof"]["pvp_w"] < 0.45 and powers[owner_p["id"]] >= powers[chal_p["id"]]:
                            owner_p["guard_s"] += 2.0
                    if holder_id is not None:
                        h_obj = next(x for x in players if x["id"] == holder_id)
                        if h_obj["guard_s"] >= 20.0:
                            winner_id = holder_id
                            win_reason = "GuardSucceeded"
                            break

            if winner_id is None:
                surv = [p for p in players if alive_respawns[p["id"]] >= 0]
                cand = surv if surv else players
                if victory_mode in ("V2_RotatingMultiTurf", "V3_KingCrownCarrier", "V4_MultiPathMatchPoint"):
                    win_p = max(cand, key=lambda x: (scores[x["id"]], x["showdown_kills"], x["xp"]))
                else:
                    keyf = lambda x: (max(x["guard_s"] / 20.0, (x["guard_s"] / 28.0) if x["private_pole"] else 0.0), x["showdown_kills"], x["showdown_dmg"])
                    ranked = sorted(cand, key=keyf, reverse=True)
                    win_p = ranked[0]
                    # Timeout: normalized guard, then showdown kills, then damage; an exact tie has no champion
                    # (LDCaptureReducer.cpp:237-265).
                    if len(ranked) > 1 and keyf(ranked[0]) == keyf(ranked[1]):
                        win_p = None
                        win_reason = "NoChampion"
                winner_id = win_p["id"] if win_p is not None else None

            end_reasons[win_reason] += 1
            win_obj = next((p for p in players if p["id"] == winner_id), None)
            if win_obj is not None:
                wins_by_bot[win_obj["bot"]] += 1
            if winner_id is not None and f1_ranks.get(winner_id) == 1:
                f1_leader_won += 1
            if winner_id is not None and f1_ranks.get(winner_id, 1) > max(1, player_count // 2):
                comebacks_from_bottom_half += 1
            lead_changes_list.append(lead_changes)
            pvp_per_match.append(sum(p["kills_total"] for p in players))
            # Rest timeline from REST.* (LDMatchTimeline.cpp:119-135, 220-240): R0 build+return, R1/R2 build + betting +
            # PK ready + PK (assumed 22s, max REST.PKMaxSec) + settlement + payout + return, R3 final build + return.
            p_ret = catalog.param("REST.ReturnSec", 3.0)
            pk_block = catalog.param("REST.BettingSec", 10.0) + catalog.param("REST.PKReadySec", 3.0) + 22.0 + catalog.param("REST.SettlementSec", 3.0) + catalog.param("REST.PayoutSec", 0.0)
            rest_s = (catalog.param("REST.OpeningBuildSec", 35.0) + p_ret
                      + 2.0 * (catalog.param("REST.BuildSec", 65.0) + pk_block + p_ret)
                      + catalog.param("REST.FinalBuildSec", 65.0) + p_ret)
            tot_s = 3.0 * catalog.param("MATCH.FieldSegmentSec", 160.0) + rest_s + catalog.param("SHOWDOWN.DurationSec", 150.0)
            for p in players:
                if p["spike_t"] is not None:
                    spike_times_s.append(p["spike_t"])
                # SIM BOUNDARY: +75s showdown combat per player is an assumption.
                combat_share_list.append((p["combat_s"] + 75.0) / tot_s)
                travel_share_list.append(p["travel_s"] / tot_s)
                rest_share_list.append((rest_s + p["idle_wait_s"]) / tot_s)

        return {
            "player_count": player_count,
            "victory_mode": victory_mode,
            "matches": matches_per_setting,
            "bot_win_rates": {b: round(wins_by_bot[b] / max(1, appearances_by_bot[b]), 3) for b in sorted(appearances_by_bot)},
            "f1_leader_conversion_rate": round(f1_leader_won / matches_per_setting, 3),
            "bottom_half_comeback_rate": round(comebacks_from_bottom_half / matches_per_setting, 3),
            "third_party_kill_share": round(third_party_kills / max(1, total_pvp_kills), 3),
            "mean_field_pvp_kills": round(statistics.fmean(pvp_per_match), 2),
            "mean_final_level": round(statistics.fmean(final_levels), 2),
            "median_final_level": round(statistics.median(final_levels), 1),
            "end_reason_shares": {k: round(v / matches_per_setting, 3) for k, v in end_reasons.most_common()},
            "mean_showdown_lead_changes": round(statistics.fmean(lead_changes_list), 2),
            "mean_time_to_first_spike_s": round(statistics.fmean(spike_times_s), 1) if spike_times_s else -1.0,
            "attention_budget_shares": {
                "active_combat_share": round(statistics.fmean(combat_share_list), 3),
                "walking_search_share": round(statistics.fmean(travel_share_list), 3),
                "rest_pk_spectate_respawn_wait_share": round(statistics.fmean(rest_share_list), 3),
            },
        }

    by_player_count = {f"P{pc}": run_setting(pc) for pc in (2, 4, 6, 8)}
    by_victory_mode_8p = {
        vm: run_setting(8, victory_mode=vm)
        for vm in ("V1_CurrentSoloFlag", "V2_RotatingMultiTurf", "V3_KingCrownCarrier", "V4_MultiPathMatchPoint", "V5_MinimalTunedFlag")
    }
    redesign_ab_8p = {
        "1_Baseline_Bug353_NoRespawn_StatShop": by_player_count["P8"],
        "2_FixIssue353_Only": run_setting(8, fix_issue353=True),
        "3_Fix353_Plus_LowHighRespawn65s": run_setting(8, camp_respawn_s=65.0, fix_issue353=True),
        "4_Fix353_LowHighRespawn65s_DecoupledShop": run_setting(8, camp_respawn_s=65.0, decoupled_shop=True, fix_issue353=True),
        "5_FullStack_With_TunedFlag_V5": run_setting(8, victory_mode="V5_MinimalTunedFlag", camp_respawn_s=65.0, decoupled_shop=True, fix_issue353=True),
        "6_FullInfoOracle_Benchmark_8p": run_setting(8, include_oracle=True),
    }

    return {
        "metadata": {
            "seed": seed,
            "matches_per_setting": matches_per_setting,
            "commit": catalog.commit_sha,
            "note": "Model-based strategic simulation grounded in content.json/world.json parameters; separate from Unreal 60Hz native execution.",
        },
        "static_pve_and_economy_audit": compute_static_pve_economy(catalog),
        "player_count_sensitivity": by_player_count,
        "showdown_victory_comparison_8p": by_victory_mode_8p,
        "economy_and_map_redesign_ab_8p": redesign_ab_8p,
    }


def run_all_experiments(
    root: Path = ROOT,
    seed: int = 20261128,
    graph_out: Path | None = None,
    results_out: Path | None = None,
) -> dict[str, Any]:
    catalog = CatalogModel.load(root)
    graph = extract_interaction_graph(catalog)
    draft = simulate_draft_and_formation(catalog, seed=seed, trials_per_regime=3000)
    combat = simulate_combat_matrix(catalog, seed=seed, duels_per_pair=16)
    ffa = simulate_ffa_tournament(catalog, seed=seed, matches_per_setting=250)

    bundle = {
        "schema": "lockdown.gameplay_research.results",
        "schema_version": 1,
        "commit": catalog.commit_sha,
        "catalog_manifest_sha256": catalog.manifest_sha256,
        "seed": seed,
        "interaction_graph_summary": {
            "metadata": graph["metadata"],
            "catalog_structure": graph["catalog_structure"],
            "closed_feedback_loops": graph["closed_feedback_loops"],
            "vocab_vs_mechanics_findings": graph["vocab_vs_mechanics_findings"],
            "bus_summary": graph["bus_summary"],
        },
        "draft_and_agency": draft,
        "combat_and_matchups": combat,
        "ffa_economy_and_showdown": ffa,
    }
    if graph_out is not None:
        graph_out.parent.mkdir(parents=True, exist_ok=True)
        graph_out.write_text(json.dumps(graph, ensure_ascii=False, indent=2), encoding="utf-8")
    if results_out is not None:
        results_out.parent.mkdir(parents=True, exist_ok=True)
        results_out.write_text(json.dumps(bundle, ensure_ascii=False, indent=2), encoding="utf-8")
    return bundle


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run Lockdown Gameplay Research Lab analyses and simulations.")
    parser.add_argument("--seed", type=int, default=20261128)
    parser.add_argument("--graph-out", type=Path, default=ROOT / "docs" / "research" / "gameplay" / "data" / "interaction_graph.json")
    parser.add_argument("--results-out", type=Path, default=ROOT / "docs" / "research" / "gameplay" / "data" / "experiment_results.json")
    args = parser.parse_args(argv)
    bundle = run_all_experiments(ROOT, seed=args.seed, graph_out=args.graph_out, results_out=args.results_out)
    print(f"GameplayResearchLab complete: commit={bundle['commit'][:12]} catalog={bundle['catalog_manifest_sha256'][:12]}")
    print(f"  Interaction graph -> {args.graph_out}")
    print(f"  Experiment results -> {args.results_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

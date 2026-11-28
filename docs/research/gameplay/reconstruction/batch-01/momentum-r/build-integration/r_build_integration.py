"""S4 Build Integration: L1 inventory of how existing content could drive a build-driven R.

Reads SourceData/Demo/content.json (read-only) and combines it with hand-authored design
classifications (marked DESIGN below). Prints Markdown tables and writes r_build_integration.json
next to this script.

Usage (from repo root):  python -B docs/research/gameplay/reconstruction/batch-01/momentum-r/build-integration/r_build_integration.py
"""
import json
import math
import pathlib
from collections import Counter, defaultdict

HERE = pathlib.Path(__file__).resolve().parent
ROOT = next(p for p in HERE.parents if (p / "SourceData" / "Demo" / "content.json").exists())
CONTENT = json.loads((ROOT / "SourceData" / "Demo" / "content.json").read_text(encoding="utf-8"))

# DESIGN: primary class of each active when it is the R host (see BUILD_INTEGRATION.md §3).
#   strike = 攻（直接伤害/控制）  guard = 守（护盾/招架/减伤/嘲讽）  move = 动（位移技能）
#   fist = 拳（强化普攻）          works = 阵（工事/区域/陷阱）
SKILL_CLASS = {
    "active_elbow": "strike", "active_pants_down": "strike", "active_crotch_grab": "move",
    "active_unplug": "guard", "active_snark_bolt": "strike", "active_seethe": "strike",
    "active_quick_meeting": "works", "active_bail_out": "fist", "active_not_mad": "guard",
    "active_stare_down": "guard", "active_triple_combo": "fist", "active_calculus_drop": "strike",
    "active_flyer_trap": "works", "active_open_mic": "guard", "active_hose_down": "strike",
    "active_zap": "strike", "active_charged_ram": "move", "active_brace": "guard",
    "active_cola_shake": "strike", "active_desk_fan": "works", "active_stale_takeout": "strike",
    "active_school_bag": "works", "active_flurry_rush": "fist", "active_cordon_tape": "works",
}
# Host -> handler. 本体 uses the fist handler, D uses the dash handler.
HANDLER_OF_CLASS = {"strike": "strike", "guard": "guard", "move": "dash", "fist": "fist", "works": "works"}

# DESIGN: which handler automatically engages a card through the bus events the handler emits.
VOCAB_HANDLER = {
    "basic_hit": "fist", "combo3": "fist", "attack_speed": "fist", "crit": "fist",
    "skill_hit": "strike", "skill_cast": "strike", "control": "strike",
    "dash": "dash", "dash_contact": "dash", "move_speed": "dash", "wall_impact": "dash",
    "shield": "guard", "shield_break": "guard", "damaged": "guard", "heal": "guard", "armor": "guard",
    "ground": "works",
    "reaction": "element", "element": "element",
    "momentum": "charge", "overdrive": "charge",
}
REMOVED_BY_M02 = {"card_dodge_strike", "card_slippery", "card_phantom_molt"}

# DESIGN: can the 2A and 2B branches of a skill be applied together (candidate "双分支" rewriter)?
#   ok = additive patches; conflict = contradictory displacement/shape/multiplier patches.
BRANCH_UNION = {
    "active_elbow": ("conflict", "拉近 vs 击退 500"), "active_pants_down": ("ok", "射程 620 + 命中拉近"),
    "active_crotch_grab": ("ok", "长突刺 + 命中后翻滚"), "active_unplug": ("ok", "踏步回劲 + 长招架护盾"),
    "active_snark_bolt": ("ok", "大号拉人 + 回旋"), "active_seethe": ("ok", "4 层灼烧 + 余烫"),
    "active_quick_meeting": ("conflict", "跟随区域 vs 一次性爆炸"), "active_bail_out": ("ok", "存 3 次 + 长冲刺"),
    "active_not_mad": ("conflict", "护盾 ×1.5 vs ×0.6 拍脸"), "active_stare_down": ("conflict", "长嘲讽 vs 拉近短嘲讽"),
    "active_triple_combo": ("conflict", "5×75% vs 2×180%"), "active_calculus_drop": ("conflict", "双砖 vs 只砸中心"),
    "active_flyer_trap": ("ok", "双陷阱 + 大地雷"), "active_open_mic": ("ok", "余音光环 + 大震退"),
    "active_hose_down": ("conflict", "拉近 vs 击退 300"), "active_zap": ("conflict", "多弹 vs 不弹"),
    "active_charged_ram": ("ok", "重甲 + 远冲"), "active_brace": ("ok", "高减伤 + 叠普攻"),
    "active_cola_shake": ("ok", "冰面 + 三连喷"), "active_desk_fan": ("ok", "大风扇 + 摇头"),
    "active_stale_takeout": ("ok", "大毒雾 + 二次爆炸"), "active_school_bag": ("conflict", "两个书包 vs 炸药包生命减半"),
    "active_flurry_rush": ("ok", "宽拳风 + 高攻速"), "active_cordon_tape": ("ok", "长封条眩晕 + 加速门"),
}

# DESIGN: fate of momentum-coupled cards under the four resource architectures (S2 owns the decision).
MOMENTUM_CARD_FATE = {
    "card_third_breath": ("R 充能", "R 充能", "R 充能", "推高劲态"),
    "card_pay_back": ("R 充能", "R 充能", "R 充能", "推高劲态"),
    "card_quick_brain": ("R 充能", "R 充能", "R 充能", "推高劲态"),
    "card_reaction_refund": ("R 充能", "R 充能", "R 充能", "推高劲态"),
    "card_martial_scholar": ("偷劲=压制对手 R", "偷劲=压制对手 R", "同左", "拉低对手劲态"),
    "card_takedown": ("劲回满=击杀后可再 R", "同左", "同左", "直达高劲态"),
    "card_again_now": ("代价成立", "代价消失→须改", "按类别", "代价=劲态下滑"),
    "card_blood_price": ("代价成立", "无意义→须改", "按类别", "绕过劲态"),
    "card_paid_dash": ("代价成立", "仍成立（冲刺耗劲）", "按类别", "代价=劲态下滑"),
    "card_short_breath": ("迁移为 R 门槛/倍率牌", "同左", "同左", "同左"),
    "card_dodge_strike": ("M-02 删除触发→须迁移", "同左", "同左", "同左"),
}


# DESIGN + L1: op-level listener audit. events = what the op listens to (or produces for "派生生产");
# rule = handling of R-marked events (the detonating action itself and every derived event inherit the R mark,
# never the payload); now = current handling of derived hits read from LDCombatCards.cpp (base e62ff304).
OPEN, ONCE, CLOSED, CAP1, PER_TARGET, STATIC, ACTION_ONLY, NOT_R = (
    "开放", "链内一次", "关闭", "每次引爆≤1", "按人数递减", "不监听", "仅引爆动作本身", "非 R 事件")
OP_AUDIT = {
    "combo_momentum": ({"combo3"}, CLOSED, "不排除追加打击：A06/A07/A09 复制品段数仍为 3，各回一次劲（:1381）"),
    "basic_cooldown_cut": ({"basic_hit"}, ONCE, "不排除追加打击，每个复制品各减一次（:1384-1402）"),
    "combo_skill_hit": ({"combo3"}, CAP1, "排除追加打击（:1406）；扇出到全部已学技能的技能命中"),
    "combo_knockback": ({"combo3"}, OPEN, "复制品清击退（:676/715/751）"),
    "combo_double": ({"combo3"}, "派生生产", "复制主命中（段数 3），bExtraStrike（:672-680）"),
    "combo_sweep": ({"combo3"}, "派生生产", "复制主命中给扇形内其他人（:741-755）"),
    "combo_cast": ({"combo3"}, CAP1, "排除追加打击（:2174）；派生施放"),
    "overcap_extra_hit": ({"basic_hit"}, "派生生产", "整份复制主命中含段数（:713-716）"),
    "basic_ap_rider": ({"basic_hit"}, OPEN, "并入主击"),
    "focus_attack_speed": ({"basic_hit"}, ACTION_ONLY, "主干不排除；集成分支 3f22d22e 排除 A09 复制品"),
    "hit_growth": ({"basic_hit"}, ACTION_ONLY, "A13 同上；D09 属精准闪避（M-02）"),
    "breach": ({"basic_hit"}, OPEN, "待审计"),
    "hit_haste_other": ({"skill_hit", "dash_contact"}, ONCE, "待审计"),
    "skill_hit_momentum": ({"skill_hit"}, CLOSED, "待审计"),
    "takedown_reset": ({"kill"}, CLOSED, "待审计"),
    "setup_mark": ({"skill_hit"}, OPEN, "待审计"),
    "control_amp": ({"control"}, OPEN, "待审计"),
    "auto_recast": ({"skill_cast"}, CAP1, "bAutoRecast 护栏（屏蔽回劲、减冷却、脑力关键石）"),
    "skill_ad_rider": ({"skill_hit"}, OPEN, "ChainAllows 护栏"),
    "shield_spend": ({"skill_hit"}, OPEN, "待审计"),
    "skill_lifesteal": ({"skill_hit"}, OPEN, "待审计"),
    "idle_ready_amp": ({"skill_cast"}, OPEN, "乘区；攻类载荷用加法预算（§3.9）"),
    "skill_growth": ({"skill_hit"}, ACTION_ONLY, "待审计"),
    "recent_cast_amp": ({"skill_cast"}, OPEN, "待审计"),
    "reaction_haste": ({"reaction"}, CLOSED, "返还宿主自身冷却"),
    "dash_contact_reset": ({"dash_contact"}, CLOSED, "刷新冲刺并退还冲刺劲（LDCombatSkillDirector.cpp:741）"),
    "empower_basic": ({"dash"}, OPEN, "每次冲刺一次"),
    "dash_split": ({"dash"}, "派生生产", "只有第一段引爆"),
    "dash_phase": ({"dash_contact"}, PER_TARGET, "每穿一人一次普攻（MakeBasicStrike，bExtraStrike）"),
    "dash_ram": ({"dash_contact"}, PER_TARGET, "每个接触者"),
    "runaway_train": ({"dash_contact", "wall_impact"}, PER_TARGET, "撞墙返还冲刺冷却部分 = 关闭"),
    "dash_ice": ({"dash"}, OPEN, "—"),
    "ground_relocate": ({"dash"}, OPEN, "—"),
    "thorns": ({"damaged"}, NOT_R, "反伤（bReflect）"),
    "last_stand": ({"low_hp"}, NOT_R, "—"),
    "reaction_momentum": ({"reaction"}, CLOSED, "待审计"),
    "reaction_element": ({"reaction"}, ONCE, "豁免反应锁一次"),
    "shield_burst_break": ({"shield_break"}, ONCE, "每打空一层计一次打破 → 同一伤害事件一次"),
    "triggered_shield": ({"damaged"}, NOT_R, "—"),
    "knockback_impact": ({"knockback", "wall_impact"}, OPEN, "待审计"),
    "lifesteal_skills": ({"skill_hit"}, OPEN, "—"),
    "lifesteal": ({"basic_hit"}, OPEN, "—"),
    "overheal_shield": ({"heal"}, OPEN, "Y29 上限已修（398d2928，集成分支）"),
    "haste_momentum": ({"skill_hit"}, CLOSED, "待审计"),
    "heal_growth": ({"heal"}, ACTION_ONLY, "待审计"),
    "kill_growth": ({"kill"}, OPEN, "击杀本身计入"),
    "control_growth": ({"controlled"}, NOT_R, "—"),
    "idle_charge": ({"damage"}, OPEN, "乘区；同 idle_ready_amp"),
    "ground_growth": ({"ground"}, OPEN, "—"),
    "tally": ({"basic_hit", "skill_hit"}, OPEN, "排除反伤/灼烧/周期/附加命中"),
    "element_aura": ({"damaged"}, NOT_R, "—"),
    "spellblade_echo": ({"skill_cast"}, OPEN, "待审计"),
    "skate_blade": ({"ground"}, OPEN, "—"),
    "tonnage_crush": ({"combo3"}, OPEN, "待审计"),
    "iron_hedgehog": ({"damaged"}, NOT_R, "反伤"),
    "reactive_plating": ({"shield"}, ONCE, "同一伤害/施放事件一次"),
    "fuel_the_fire": ({"skill_hit"}, OPEN, "待审计"),
    "rogue_works": ({"ground"}, OPEN, "待审计"),
    "martial_scholar": ({"basic_hit", "skill_hit"}, OPEN, "偷劲部分 = 关闭；排除追加打击（:1343/1364）"),
}
# Events each processor's payload emits (besides damage/element that any can emit).
PROCESSOR_EVENTS = {
    "fist": {"basic_hit", "combo3", "knockback", "kill", "heal", "damage"},
    "strike": {"skill_cast", "skill_hit", "control", "reaction", "kill", "knockback", "wall_impact", "heal", "damage"},
    "guard": {"skill_cast", "shield", "shield_break", "heal", "damage"},
    "dash": {"dash", "dash_contact", "basic_hit", "wall_impact", "knockback", "kill", "damage"},
    "works": {"skill_cast", "skill_hit", "ground", "control", "kill", "damage"},
}
# DESIGN: the 9 representative builds (BUILD_INTEGRATION.md §3). host -> (cards on host, skill rank, medal tier).
BUILDS = {
    "3.1 普攻攻速": {"Q 左右开抽": (0, 2, 0), "本体": (4, 0, 1)},
    "3.2 慢速重击": {"Q 拧你手腕": (0, 3, 0), "本体": (4, 0, 0)},
    "3.3 技能循环": {"Q 甩你一鞋": (1, 3, 0), "W 电你个哆嗦": (1, 2, 0), "E 板砖糊脸": (1, 3, 0), "本体": (1, 0, 0)},
    "3.4 元素反应": {"Q 滋你一脸": (0, 3, 2), "W 冰可乐摇一摇": (0, 2, 0), "E 电你个哆嗦": (1, 3, 0), "本体": (2, 0, 0)},
    "3.5 冲刺机动": {"Q 贴脸补一拳": (0, 3, 0), "D": (5, 0, 0)},
    "3.6 护盾防反": {"Q 锅盖挡脸": (0, 3, 0), "W 扯嗓子喷你": (1, 3, 0), "本体": (3, 0, 0)},
    "3.7 召唤工事": {"Q 占座书包": (0, 3, 0), "W 电风扇": (0, 3, 0), "E 脚底开锅": (0, 1, 0), "本体": (2, 0, 0)},
    "3.8 文武混合": {"Q 甩你一鞋": (0, 2, 0), "W 过期外卖": (0, 1, 0), "本体": (4, 0, 1)},
    "3.9 憋大招": {"Q 板砖糊脸": (2, 3, 0), "本体": (2, 0, 0)},
}
HOST_ORDER = ["Q", "W", "E", "本体", "D"]


def auto_pin(hosts, include_body):
    best, best_score = None, -1
    for name in sorted(hosts, key=lambda h: HOST_ORDER.index(h.split()[0])):
        if name == "本体" and not include_body:
            continue
        cards, rank, medal = hosts[name]
        score = cards + max(0, rank - 1) + medal
        if score > best_score:
            best, best_score = name, score
    return best or "本体", best_score


def weight(card):
    return card.get("drop_weight", 100)


def card_handlers(card):
    hs = {VOCAB_HANDLER[v] for v in card.get("vocab", []) if v in VOCAB_HANDLER}
    if card["form"] == "dash":
        hs.add("dash")
    if card["form"] == "skill":
        hs.add("host_skill")  # engaged whenever its host skill is the R host
    return hs


def p_see_specific(card_weight, pool_weight, picks):
    """P(a specific card appears at least once in `picks` independent weighted single draws)."""
    return 1 - (1 - card_weight / pool_weight) ** picks


def main():
    abilities = {a["id"]: a for a in CONTENT["abilities"]}
    cards = [c for c in CONTENT["cards"] if c["form"] != "medal"]
    medals = [c for c in CONTENT["cards"] if c["form"] == "medal"]
    pool_w = sum(weight(c) for c in cards)
    out = {"source": "SourceData/Demo/content.json", "card_count": len(cards), "medal_count": len(medals),
           "pool_weight": pool_w}

    # 1. Skill classes and opening availability (C3 form source).
    cls_rows = defaultdict(lambda: {"all": [], "opening": [], "element": []})
    for aid, a in abilities.items():
        c = SKILL_CLASS[aid]
        cls_rows[c]["all"].append(aid)
        if a["opening"]:
            cls_rows[c]["opening"].append(aid)
        if a["element"] != "none":
            cls_rows[c]["element"].append(aid)
    n_open = sum(1 for a in abilities.values() if a["opening"])
    print("## 1. 技能宿主类别（R 处理器）与开局可得性\n")
    print("| 类别 | 处理器 | 技能数 | 开局池 | 原生元素 | 开局三选一至少见到 1 个（超几何近似，忽略分桶） |")
    print("| --- | --- | --- | --- | --- | --- |")
    out["skill_classes"] = {}
    for c, r in sorted(cls_rows.items(), key=lambda kv: -len(kv[1]["all"])):
        k = len(r["opening"])
        p = 1 - math.comb(n_open - k, 3) / math.comb(n_open, 3)
        out["skill_classes"][c] = {"handler": HANDLER_OF_CLASS[c], "skills": r["all"], "opening": r["opening"],
                                   "p_offer_opening": round(p, 3)}
        print(f"| {c} | {HANDLER_OF_CLASS[c]} | {len(r['all'])} | {k} | {len(r['element'])} | {p:.0%} |")

    # 2. Cards automatically engaged by each handler (C5 depth without new content).
    print("\n## 2. 每个处理器的“候选接入”牌（按 vocab，选牌加权标签，不是监听关系；以 §7 按 op 审计为准）\n")
    eng = defaultdict(list)
    for c in cards:
        if c["id"] in REMOVED_BY_M02:
            eng["removed_m02"].append(c["id"])
            continue
        hs = card_handlers(c) or {"neutral"}
        for h in hs:
            eng[h].append(c["id"])
    print("| 处理器 | 牌数 | 权重占比 | 其中稀有/传说 |")
    print("| --- | --- | --- | --- |")
    out["handler_cards"] = {}
    for h in ["fist", "strike", "host_skill", "dash", "guard", "works", "element", "charge", "neutral", "removed_m02"]:
        ids = eng.get(h, [])
        w = sum(weight(c) for c in cards if c["id"] in ids)
        rare = sum(1 for c in cards if c["id"] in ids and weight(c) < 100)
        out["handler_cards"][h] = {"count": len(ids), "weight_share": round(w / pool_w, 3), "rare_or_legendary": rare,
                                   "ids": ids}
        print(f"| {h} | {len(ids)} | {w / pool_w:.1%} | {rare} |")
    print(f"\n元素勋章 {len(medals)} 枚作为宿主元素载荷，对所有处理器生效。")

    # 3. Form-from-card reliability (C1) vs. skill (C3) vs. keystone (C2) timing.
    print("\n## 3. 形态来源的可得性（C1 / C2 / C3）\n")
    picks_by = {"L3（开局+L3 两组）": 6, "L6（三组+R1 商店 4 格）": 13, "L9（四组+R1/R2 商店）": 20}
    out["c1_form_card"] = {}
    print("| 时点 | 指定一张普通形态牌至少出现一次 | 指定一张稀有形态牌 | 指定一张传说形态牌 |")
    print("| --- | --- | --- | --- |")
    for label, n in picks_by.items():
        row = [p_see_specific(w, pool_w + 600, n) for w in (100, 22, 6)]
        out["c1_form_card"][label] = [round(x, 3) for x in row]
        print(f"| {label} | {row[0]:.0%} | {row[1]:.0%} | {row[2]:.1%} |")
    print("\n假设：新增 6 张形态牌（各 100 权重）进池；抽取近似为独立加权单抽，未计词汇 ×1.2–1.8 与重抽。")
    pts = {L: 3 + 3 * (L - 1) for L in range(1, 17)}
    first_ks = next(L for L, p in pts.items() if p >= 18)
    out["c2_keystone"] = {"points_by_level": pts, "earliest_all_in_level": first_ks,
                          "keystone_share_of_L16_formula_points": round(18 / pts[16], 3),
                          "keystone_share_of_observed_L16_points": round(18 / 81, 3)}
    print(f"\nC2：全投一维最早 L{first_ks} 达 18 点；18 点占 L16 公式点数 {pts[16]} 的 {18 / pts[16]:.1%}，"
          f"占 EXP-01 真人 L16 实际 81 点的 {18 / 81:.0%}（L5，n=1，含击杀/营地/事件额外点）。")
    print("C3：开局技能即宿主，L1 起就有形态；L3 第一个技能点起“主修”可区分。")

    # 4. Branch union compatibility for the candidate "双分支" rewriter.
    ok = [k for k, v in BRANCH_UNION.items() if v[0] == "ok"]
    out["branch_union"] = {k: {"status": v[0], "note": v[1]} for k, v in BRANCH_UNION.items()}
    print(f"\n## 4. “双分支”改写候选：2A+2B 可同时生效 {len(ok)}/{len(BRANCH_UNION)}\n")
    print("冲突：" + "、".join(f"{k.replace('active_', '')}（{v[1]}）" for k, v in BRANCH_UNION.items()
                            if v[0] == "conflict"))

    # 5. Momentum-coupled cards under M1–M4.
    print("\n## 5. 劲相关牌在 M1–M4 下的含义\n")
    print("| 牌 | M1 共享消耗 | M2 战斗积累 | M3 混合 | M4 状态型 |")
    print("| --- | --- | --- | --- | --- |")
    for cid, fate in MOMENTUM_CARD_FATE.items():
        print(f"| `{cid}` | " + " | ".join(fate) + " |")
    out["momentum_card_fate"] = MOMENTUM_CARD_FATE

    # 6. Maintenance proxies for C1–C6: distinct R rule implementations, mandatory new content entries,
    #    and whether a cross-source conflict rule is required.
    n_tags = len(cards) + len(abilities) + 5
    print("\n## 6. 维护成本代理\n")
    models = {
        "C1 卡牌驱动（覆盖 8 构筑约需 12 张 R 牌）": (12, 12, "要：形态牌互斥"),
        "C2 关键石驱动": (5, 5, "要：可同时激活 2–3 块关键石"),
        "C3 技能驱动·逐类别": (5, 0, "要：主修并列"),
        "C3 技能驱动·逐技能": (24, 24, "要：主修并列"),
        "C3 技能驱动·逐分支": (24 * 7, 24 * 7, "要：主修并列"),
        "C4 显式原型 + 成长节点": (6, 6 + 1, "否（单选）"),
        f"C5 涌现积分（{n_tags} 个组件打标签）": (6, n_tags, "要：并列/抖动"),
        "C6 宿主印记（推荐）": (5, 0, "否（印记单宿主）；另需一次性监听者审计（§7）"),
    }
    out["maintenance_proxy"] = {}
    print("| 架构 | 独立 R 处理器 | 必需新增内容条目 | 跨来源冲突规则 |")
    print("| --- | --- | --- | --- |")
    for k, (n, s, conflict) in models.items():
        out["maintenance_proxy"][k] = {"handlers": n, "new_entries": s, "conflict_rule": conflict}
        print(f"| {k} | {n} | {s} | {conflict} |")

    # 7. Op-level listener audit (first-version cost of "接入 via events").
    by_op = defaultdict(list)
    for c in cards:
        for e in c["effects"]:
            by_op[e.get("op")].append(c["id"])
    missing = sorted(op for op in by_op if op not in OP_AUDIT and not any(cid in REMOVED_BY_M02 for cid in by_op[op]))
    print("\n## 7. 按 op 的监听者审计（派生事件继承 R 标记、不继承载荷）\n")
    print("| op | 牌 | 监听事件 | R 标记下规则 | 派生命中现状（L1） |")
    print("| --- | --- | --- | --- | --- |")
    out["listener_audit"] = {}
    rule_count = Counter()
    for op, (events, rule, now) in OP_AUDIT.items():
        ids = [cid for cid in by_op.get(op, []) if cid not in REMOVED_BY_M02]
        if not ids:
            continue
        rule_count[rule] += len(ids)
        out["listener_audit"][op] = {"cards": ids, "events": sorted(events), "rule": rule, "now": now}
        short = "、".join(cid.replace("card_", "") for cid in ids)
        print(f"| `{op}` | {short} | {'/'.join(sorted(events))} | {rule} | {now} |")
    static_ids = [cid for op, ids in by_op.items() if op not in OP_AUDIT for cid in ids if cid not in REMOVED_BY_M02]
    out["listener_rule_counts"] = dict(rule_count)
    out["static_cards"] = sorted(set(static_ids))
    print("\n按规则计数（牌）：" + "，".join(f"{k} {v}" for k, v in rule_count.most_common())
          + f"；不监听（静态/改造/转换）{len(set(static_ids))}")
    todo = sum(1 for _, (_, r, now) in OP_AUDIT.items() if now == "待审计")
    print(f"派生命中处理仍“待审计”的 op：{todo}/{len(OP_AUDIT)}")
    out["unaudited_ops"] = missing
    print("\n按 op 的候选接入（监听事件 ∩ 处理器发出的事件，不含非 R 事件与静态牌）：")
    out["op_engaged"] = {}
    for proc, evs in PROCESSOR_EVENTS.items():
        ids = sorted({cid for op, (events, rule, _) in OP_AUDIT.items() if rule != NOT_R and events & evs
                      for cid in by_op.get(op, []) if cid not in REMOVED_BY_M02})
        out["op_engaged"][proc] = ids
        print(f"- {proc}: {len(ids)} 张")

    # 8. Auto pin: original score vs. body excluded from auto.
    print("\n## 8. 自动印记：原打分 vs 本体只能手动\n")
    print("| 构筑 | 原打分（含本体） | 修订（自动只在 Q/W/E/D 中选） |")
    print("| --- | --- | --- |")
    out["auto_pin"] = {}
    body_old = 0
    for b, hosts in BUILDS.items():
        old, os_ = auto_pin(hosts, True)
        new, ns = auto_pin(hosts, False)
        body_old += old == "本体"
        out["auto_pin"][b] = {"old": old, "new": new}
        print(f"| {b} | {old}（{os_}） | {new}（{ns}） |")
    print(f"\n原打分下本体当选 {body_old}/{len(BUILDS)}（示例按技能设计，偏低估）；修订后 0/{len(BUILDS)}。"
          f" 本体牌占非勋章牌 {sum(1 for c in cards if c['form'] == 'body')}/{len(cards)}。")

    # 9. Distinct R behaviours vs. host variety.
    print(f"\n## 9. R 行为数：5 个处理器 × 6 种元素载荷（无 + 五元素）= 30 种；"
          f"其余变化来自宿主技能自身（{len(abilities)} 技能 × 7 分支路径）。")

    (HERE / "r_build_integration.json").write_text(json.dumps(out, ensure_ascii=False, indent=1), encoding="utf-8")


if __name__ == "__main__":
    main()

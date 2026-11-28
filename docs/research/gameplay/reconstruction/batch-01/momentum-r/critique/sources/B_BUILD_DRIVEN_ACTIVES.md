# Track B：构筑驱动的主动能力与组合系统（外部研究）

范围：Hades / Hades II、Risk of Rain 2、Path of Exile 1/2，以及 Heroes of the Storm 英雄技、Deadlock imbue 两个补充对照。
目的：为 Lockdown「单一 R 键，行为由构筑决定」提供外部证据与可迁移原则。**不批准任何具体设计或数值。**
抓取日期：2026-11-28。Noita、Slay the Spire、Brotato、Backpack Battles、LoL Arena、Hades 的 boon 槽位已在 `docs/research/gameplay/EXTERNAL_BENCHMARKS.md`（下称 EB）§7–§11 覆盖，本文只引用、不重复。

## 0. 证据标签与先说清的事

| 标签 | 含义 |
| --- | --- |
| [外部证据-A] | 一手：开发者补丁说明、开发者博客/日志、官方论坛补丁帖 |
| [外部证据-B] | 可靠二手：媒体/攻略站对一手的整理 |
| [外部证据-C] | Wiki / 社区 |
| [设计推断] | 本文据证据推出，不是来源原话 |
| [原创假设] | 对 Lockdown 的提案，待原型验证 |

**核查结论（避免误引）：**

1. **Hades II 的 Ω（Omega）招式：没有找到 Supergiant 对其设计意图或 Magick 经济的一手论述。** 能读到的一手材料只有 Early Access → 1.0 → Post-Launch 的补丁说明（Steam 公告 API 全量抓取后按 `Omega|Magick|Hex` 过滤）。"Ω 是按住输入蓄力、花 Magick 的强化版"这一定义来自 B 级媒体（GameRant 2024-05-06）。Kasavin 的访谈（RPG Site）搜索摘要里没有涉及 Ω。本文只据补丁说明做"开发者做了什么"的推断。
2. **"PoE 3.15/3.16 trigger 大改"：未能在官方补丁中证实。** 3.16.0（2021-10-22）官方补丁摘要里没有找到触发技能专项条目。能证实的 PoE1 触发改动是：Cast on Critical Strike 曾改为"500ms 冷却且每帧最多触发一个法术"、后来冷却为 150ms（来源为 Wiki 版本记录，[外部证据-C]，版本号未取到）；Spellslinger 3.12.0 冷却 0.5s→0.6s（[外部证据-C]）。**GGG 针对触发体系最清晰的一手论述在 PoE2 0.1.0d（2024-12-12）**，见 §4.3。
3. **PoE 官方论坛与 poewiki 的 `action=raw` 对脚本抓取返回 403/反爬页。** PoE2 0.1.0d / 0.3.0 的 GGG 原文只拿到：Maxroll 转载全文（0.1.0d）与论坛搜索摘要（0.3.0）。已在来源表标注。
4. **RoR2 装备（Equipment）"单一主动槽"的设计原话没有找到。** 能证实的是开发者日志里关于 proc、连锁、装备新增策略的若干陈述（§3）。
5. **Vampire Survivors 进化未研究**（与 R 键关联弱，EB 已有 Brotato/StS 作同类对照）。

## 1. Hades（1）：Call / God Gauge

来源：[H1] Steam 公告 API（app 1145360）按 `God Gauge|Call|Aid` 过滤；[H2] Hades Wiki「Call」「Combat」。

### 1.1 一个输入如何承载多种行为
- 一个固定的 Call 键 + 一个固定的资源结构：God Gauge 分 4 格，**用 Call 消耗 1 格；满 4 格时整条消耗，换成更强的 Greater Call**。有 Aid boon 才显示槽位。[外部证据-C]（Hades Wiki Call，页面更新 2026-11-06）
- Call 的**内容**完全由 boon 决定（Zeus' Aid、Athena's Aid、Ares' Aid、Demeter's Aid 等）。Greater Call 的效果由 boon 的 "Max Gauge Bonus" 描述，通常是持续更长。[外部证据-C]
- 槽位身份与 EB §7.1 一致：一个槽位同时只有一个槽位 boon。

### 1.2 分层：基础形态 / 修饰 / 冲突
- 基础形态 = 神的 Aid。修饰 = 其他 boon 直接改 Call 的参数：
  - Rip Current（Poseidon）"让 Poseidon's Aid 持续更久并拉拽敌人"（Blood Price，2020-06-23）[外部证据-A]
  - Aether Net "每场遭遇开始时 God Gauge 已有一部分"[外部证据-A]
  - Quick Favor（Hermes）"God Gauge 缓慢自动充能"；Smoldering Air（Zeus×Aphrodite）"自动充能，但上限 25%"[外部证据-A]
- **充能来源被明确分成"门槛"**：自动充能 boon 带封顶（25%），不能替代战斗内的伤害驱动充能。[外部证据-A，封顶是事实；"目的是不让它替代战斗充能"为 [设计推断]]
- 冲突：同一槽位只有一个 Aid（同槽再次提供只升稀有度，EB §7.1 [外部证据-C]）。因此"多个 Call 修饰同时叠加"的冲突主要由**一槽一神**消解，不靠运行时优先级。

### 1.3 如何限制失控和功率尖峰
- 数值层：开发者逐个调 Aid 的时间/伤害/消耗，而不是加全局上限。
  - 2020-01-21：Athena's Aid "提高 God Gauge 消耗与效果持续时间（不能那么频繁使用）"；Ares' Aid "降低伤害，提高移速"；Zeus' Aid "降低稀有度缩放" [外部证据-A]
  - 2020-06-23：Ares' Aid "降低持续时间，提高伤害作补偿" [外部证据-A]
  - 2020-09-17（1.0）：Zeus' Aid "降低伤害"、Demeter's Aid "降低 Poms 缩放" [外部证据-A]
- **供给层**：2020-01-28 "降低 Olympian Aid boon 的提供率（总体上不应出现得这么早、这么频繁）" [外部证据-A]。
- **前置**：2020-03-10 "Greater Recall（Hermes）现在需先拥有 Hermes 的 Cast 影响类 boon" [外部证据-A]。
- 部分使用 vs 完整使用：把"分档"做成一个玩家能理解的二选一——1 格=快速、小；4 格=慢、大。[设计推断]

### 1.4 玩家如何读懂与预测
- 2020-01-21 "降低 Zeus' Aid 的视觉效果（让局面更容易看清）" [外部证据-A]。
- 2020-06-30 "调整多个 1 格 Olympian Call 的启动音效（有些比别的安静得多）" [外部证据-A]。
- 2020-01-24 修复 Demeter's Aid Greater Call 与某些特效的显示问题。[外部证据-A]
- 开发者把"强力主动技的视觉噪声"当作需要回头压低的问题，而不是继续加特效。[设计推断]

### 1.5 内容与维护成本信号
- Aid 类 boon 数量有限（每神一个），但每次平衡补丁都要逐个重调伤害/持续/消耗/缩放。每个 Aid 是**手工内容**，不是由规则生成。[设计推断，依据 §1.3 的逐个调整记录]

## 2. Hades II：Hex、Magick、Ω 招式

来源：[H3] Steam 公告 API（app 1145350）按 `Omega|Magick|Hex` 过滤，覆盖 2024-05-16 至 2026-07-28 全部公告；[H4] GameRant "Hades 2: Omega Variant Attacks Explained"（2024-05-06）；[H5] TheGamer Ω 攻略（2024-05-25）。

### 2.1 一个输入如何承载多种行为
- **Ω 不是通用强化，是"每个武器/Aspect 各自写一套"。** 补丁说明里 Ω 总是带武器与 Aspect 名出现：Witch's Staff Omega Special、Umbral Flames Omega Attack、Moonstone Axe Omega Special、Argent Skull Omega Attack（EA Patch 2，2024-06-04）[外部证据-A]。按住 Attack/Special/Cast 蓄满条后松手释放，Magick 费用写在条下，默认各 Ω 不同。[外部证据-B]（GameRant）
- **Hex**（Selene 的月之魔法）是另一条主动线，其充能与"Magick 花费量"挂钩：多个 Hex 的 "Magick-spend requirement" 被反复调高/调低（EA2 2024-06-04：Twilight Curse/Lunar Ray "大幅降低"；EA3 2024-06-26：Total Eclipse "大幅降低"等）[外部证据-A]。
- Hex 另有 "Path of Stars / Godsent Hexes" 升级层：2025-06-17 "Godsent Hexes：Path of Stars 现在可为每个 Selene Hex 注入 Olympian 之力"[外部证据-A]。我没有取到 Hex 选择方式的一手说明，故不在此断言。

### 2.2 分层：基础形态 / 修饰 / 冲突
- **修饰按"槽位/语义"而不是按具体技能写**：
  - 通用 Ω 修饰：Nimble Mind（Hermes）"你的 Ω 招式更快"、Blood Line（Hera）"Ω 招式额外制造伤害裂隙，附加 Magick 费用"、Splash Fount（Poseidon）"Ω 招式在第一个受伤敌人附近制造水爆"（EA3，2024-06-26）[外部证据-A]
  - Ocean Swell（Poseidon）"Ω 也发射水浪，但消耗更多 Magick"（2025-06-17）[外部证据-A]
  - Arc Flash（Zeus）"Ω 招式触发你的 Blitz 效果并使其更强"（2025-02-19）[外部证据-A]
  - Death 改写："Ω 招式在上一次用的不同时有概率暴击"（EA3）——条件绑定在**Ω 的序列**上[外部证据-A]
- **同一修饰在不同 Aspect 下的差异化内容**：Witch's Staff（Momus）"每个 Ω 自动连续发射数次"（EA2）；Skull "Helheim Charge：Ω Attack 连发两次"（2025-07-02，EA11 又改为"再多发一次"）[外部证据-A]。
- **冲突解决靠"回头补丁"，不是规则预先裁定**：大量条目是"X 现在也影响 Ω"（Rapid Moonshot、Dual Moonshot、Shimmering Moonshot、Colossus Driver、Secret Crush、Looming Ignition 等，2024-06-04 / 2025-02-19）[外部证据-A]。**这是 Ω 作为"独立语义对象"后，每一条旧 boon 都要补一次"是否影响 Ω"的决定。**
- 明确的抗叠加规则：EA3 "你不能在 Hex 效果仍在进行时再次为 Hex 充能"（2024-06-26）[外部证据-A]。

### 2.3 如何限制失控和功率尖峰
- **费用旋钮**：几乎每个 Ω 相关修饰都带 Magick 费用或回复条款作为对价（Blood Line "added Magick cost"、Ocean Swell "uses more Magick"、Killing Stroke "prime some Magick"）[外部证据-A]。
- **回复侧旋钮也大量出现**：Mist Veil、Flood Gain（"Ω 招式短暂返还已用 Magick，延迟后可重复"）、Tranquil Gain、Hearth Gain、Lucid Gain（1.0 又改为"不再要求站在 Cast 里，但回复更少"）、Grisly Gain（1.0 提高回复；PLP2 再提高概率）[外部证据-A]。→ **一旦 Ω 以资源为门，资源回复/消耗就成为最大的一类修饰内容与调参面。**
- **硬失败的整理**：EA3 "Executioner's Chop：不再因 Magick 不足而阻止你挥击"；"修复了训练场里 Ω 在 Magick 不足时仍可使用"（EA3）；PLP2 Hotfix 5（2026-07-28）"修复 Glorious Disaster 在 Magick 不足时的 UI 反馈"[外部证据-A]。
- 具体能力的失控处理：Sister Blades "Ω Attack 不能再被敌人打断"（EA10，2025-07-02）、Black Coat "Dash 不再取消 Ω Attack 蓄力，并在 Dash 后直接释放"（2025-02-19）[外部证据-A]。这说明**输入层冲突（Ω 蓄力 vs Dash/受击）靠逐 Aspect 补丁收敛**。
- 修复 bug 的类型能反映复杂度：2024-06-04 "Ω 招式被 Polyphemus 咬住后偶尔失去响应"、2025-02-26 "某些效果没有按预期提升 Moonstone Axe Ω Special 的蓄力速度"、2026-06-03 "某些 Ω 在 Dark Side 效果下仍可使用"、PLP1 "Mist Veil 现在恢复任何原因损失的 Magick（不仅是 Ω）"[外部证据-A]。

### 2.4 玩家如何读懂与预测
- 蓄力条 + Magick 费用显示在角色头顶（B）；1.0 更新了"Ω 蓄力时的 Magick 指示器美术"（2025-09-25），EA11 修复"Magick Indicator 与 Black Coat (Nyx) 的视觉问题"[外部证据-A]。
- **单人 PvE 读状态靠自己的屏幕；FFA 里别人读不到你的 Magick 与蓄力** [设计推断]。

### 2.5 内容与维护成本信号
- 自 2024-06 起，**几乎每个内容或平衡补丁都有 Ω 条目**（2024-06-04、06-26、10-16、2025-02-19、06-17、07-02、07-23、09-25、2026-04-14、06-03 都出现）[外部证据-A]。
- 这是 Lockdown 删除"通用长按爆劲"后最需警惕的一个对照：Hades II 的 Ω 之所以能持续成立，是因为**每个武器 Aspect 都有手写的 Ω 内容，开发团队长期按武器逐个维护**；通用规则只提供输入语法。[设计推断]
- 开发者对 Ω 本身的意图未公开解释，此处不构成对"Ω 要不要通用化"的一手结论。

## 3. Risk of Rain 2：装备槽、proc 系数与连锁

来源：[R1] Steam 公告 API（app 632360），Hopoo Games Development Thoughts #1/#3/#16/#17，补丁说明；[R2] Dev Diary 7（2024-12-10）；[R3] Wiki（Proc Coefficient）[外部证据-C]；[R4] GameRant 解释（2024-09-06）[外部证据-B]。

### 3.1 一个输入如何承载多种行为
- 装备是**单一主动使用槽**，行为完全由所持装备定义（Forgive Me Please、Gesture of the Drowned、Sawmarang、Super Massive Leech 等）。"单槽 + 冷却 + 装备自带行为"来自常识与 Wiki，**未取到设计原话** [外部证据-C]。
- 开发者在 1.0 的选择：**所有 1.0 新物品都是 Lunar、Equipment 或 Boss 掉落**，理由："这些类别在追逐型物品与核心物品之间取得了平衡；也不想用一堆依赖协同的物品污染标准物品池"（Dev Thoughts #16，2020-08-04）[外部证据-A]。**含义：依赖协同的"高组合"内容被放进稀少、可见、非核心的专用槽。** [设计推断]

### 3.2 分层与 proc 系数（如何约束递归）
- **定义**："每次攻击有一个 proc coefficient，本质上定义 on-hit 触发有多强/多频繁。MUL-T 的 Nailgun 每颗钉 0.4，对比多数角色的 1.0"（Dev Thoughts #1，2019-03-30）[外部证据-A]。
- 效果（Wiki 示例）：系数同时乘触发概率和持续时间；DoT（流血/燃烧）系数为 0，**不能触发 on-hit**[外部证据-B/C]。例：Ukulele 闪电系数 0.2 → 触发 AtG 概率仅 2%[外部证据-B]。
- 开发者补过的"漏洞"（都是同一原则：**派生事件不能无限回流成触发源**）：
  - 2019-04-03：MUL-T 按住主攻击 + Ocular HUD "不再造成无限暴击伤害"[外部证据-A]
  - 2019-06-25：暴击不再在 proc 链上"多次翻倍"，开发者说这是"意外的、长期不可持续的"，并把 on-hit 改为按**总伤害**百分比，补偿此修复 [外部证据-A]
  - 2019-04-26 / 04-08：已知问题 "Gesture of the Drowned 每帧触发装备"、"某些装备通过网络重复多次触发" [外部证据-A]
  - 2020-08-04（Thoughts #16）：**"流血不再随 proc 链放大"**，并改成"对大伤害攻击必定触发 + 内部冷却"。开发者称这一个小改动明显提升了流血/攻速/高 proc 率技能的价值，且让 proc 链叠加与流血叠加"需要不同属性来最大化" [外部证据-A]
  - 2020-08-13（Thoughts #17）："玩家可用 Forgive Me Please 与 Soulbound Catalyst 创建无限循环，今天已热修" [外部证据-A]；2024-05-20 又修 "Forgive Me Please 不再连续触发击杀效果" [外部证据-A]
  - 2021-03-25：Nano-Bomb 触须不再触发 Ice/Fire Bands（改为触须频率×2、伤害÷2）[外部证据-A]；2022-05-26（1.2.4）"Singularity Bands 不能再触发自己" [外部证据-A]
- **模式**：每次发现回流路径，不是做全局递归检测，而是（a）给派生事件 0/低系数，（b）改成内部冷却，（c）列出具体"不能触发自己"的例外。[设计推断]

### 3.3 功率尖峰
- "无限流血"在 1.0 后仍被降速：叠到"无限"所需的匕首数量增加，使其与达到 100% 暴击所需眼镜数量相当（2021-03-25 Developer Notes）[外部证据-A]。

### 3.4 玩家读状态
- Dev Diary 7（2024-12-10）：**Noxious Thorn "机制对玩家不清晰，常难以判断物品是否生效"，被几乎完全重做**；Warped Echo "新增了可用时的视觉指示"；Chronic Expansion 改成"更清楚的连击表机制"并配"即将失效的音频警告"[外部证据-A]。
- 一个例外条款：Electric Boomerang "移除了携带 Sawmarang 装备时会施加流血的交互"[外部证据-A]——**开发者会直接删除令人困惑的跨物品交互**。

### 3.5 内容与维护成本信号
- proc 系数是**每个技能一个标量**的接口，新技能只需填写系数而不是为每个物品写特例；但 2019 补丁里仍然出现 "Artificer 系数过低是 bug"、"一些角色技能系数不一致"（Thoughts #2，2019-04-08）[外部证据-A]。→ 标量接口降低了成本，但仍需**逐个技能审系数**。[设计推断]

## 4. Path of Exile 1 / 2

来源：[P1] PoE Wiki「Trigger」、Cast On Critical Strike Support、Spellslinger（版本记录）[外部证据-C]；[P2] PoE2 Wiki「Energy」[外部证据-C]；[P3] GGG 0.1.0d 补丁说明（Kieren_GGG，2024-12-12，论坛 `view-thread/3621299`；本文读的是 Maxroll 转载的全文）[外部证据-A，经 B 转载]；[P4] GGG 0.3.0 The Third Edict 补丁帖（论坛 `view-thread/3826682`，仅搜索摘要）[外部证据-A，摘要级]；[P5] Out of Games 对 Jonathan Rogers 的采访（2023-07-29）、Wowhead 采访（2024-06-18）[外部证据-B]；[P6] Mobalytics / GameRant PoE2 Meta Gem 指南 [外部证据-B]。

### 4.1 一个输入/槽位如何承载多种行为
- **技能宝石 = 主形态；辅助宝石 = 参数/行为修饰；Meta 宝石 = 容器**。PoE2：Meta Gem 里可插技能宝石和辅助，"Meta Gem 永远不能被插进另一个 Meta Gem"[外部证据-C，PoE2 Wiki Energy 页]。
- PoE2 的 **Invocation（召唤）** 技能最贴近"R 键"：能量先**存储**，直到你主动使用该技能，才按存储的能量**触发所插技能若干次**；Ritual Cadence 关键石把它改为"每 2 秒触发一次技能，且触发消耗更少能量"（0.3.0，GGG）[外部证据-C 机制 + A 摘要]。
- Rogers 采访：技能宝石系统的改动是"打开设计空间最多的决定"；每个技能最多 5 个辅助，**每个辅助宝石全局只能用一次**，所以辅助宝石要多 [外部证据-B]。

### 4.2 分层与冲突解决（PoE1）
- **触发有硬规则，违反时失败闭合**："一个技能永远不能有两个触发源；给它多个触发条件会使其被禁用"[外部证据-C，PoE Wiki Trigger，PoE2 Wiki 同样有此语句]。
- 触发技能：不能被通道技能、Vaal 技能、有预留的技能触发；不能支持图腾/陷阱/地雷等"自带触发语义"的技能 [外部证据-C]。
- 触发辅助自带成本：Cast on Critical Strike "Supported Skills deal (19–10)% less Spell Damage"，成本倍率 120%，冷却 0.15s；Cast when Damage Taken "deal (65–27)% less Damage"，成本倍率 250%，冷却 0.25s [外部证据-C]。**触发 = 低伤害折扣 + 成本倍率 + 内部冷却。**
- 多触发技能的结算有**确定顺序**：触发"主手 1 → 护身符 → 头盔 1…"再到各装备的第二个孔，因此要把优先级高的宝石放前面 [外部证据-C，PoEDB]。
- 触发技能"不算使用/施放/攻击"，因此"当你施放时"类效果不生效 [外部证据-C]——**触发源与"手动使用"在语义上被硬隔离。**

### 4.3 GGG 为什么重做触发体系（一手证据最清楚的部分）
0.1.0d（2024-12-12）[外部证据-A，经 Maxroll 转载全文]：
> "Trigger gem energy gain was not in the right place and it was far too easy to use ailments to trigger spells. One problem with the previous system is that if we balanced it to make triggering happen in boss fights then the triggering would happen far too often during clearing. In order to address this we are now making 'Monster Power' part of the calculation for energy gain... Another issue was using low level skills with unconditional ailment applications such as Flame Wall to ignite enemies for triggering. In order to make the source of the ignite matter we are making the energy gain be dependent on the strength of the ignite."
- 同一天后续热修：Cast on Critical 能量随"造成的伤害"缩放，防止低级技能制造大量能量 [外部证据-B，Maxroll 摘要]。
- Maxroll 评语：这是"对多数触发流派的重大削弱"；独特怪给的触发数大致不变，整体触发率大幅下降 [外部证据-B]。
- 0.3.0（The Third Edict）：Cast on Freeze / Shock / Ignite 合并为单一宝石 Cast on Elemental Ailment（100 Spirit，最低 14 级，合并三者的能量条件）[外部证据-A，摘要级]。→ **用合并减少内容数量。**
- 触发依赖的资源：Spirit 预留（Meta 通常 ≥60）、能量上限 = 所插技能总基础施法时间之和 [外部证据-B]；**"能量不能从被触发技能的直接效果获得"** [外部证据-C]；多个相同 Meta 宝石各自独立存能量，溢出的能量在重置时丢弃 [外部证据-C]。

### 4.4 如何控制组合爆炸（归纳）
| 手段 | PoE 证据 | 级别 |
| --- | --- | --- |
| 类型/标签限制 | "Cannot support totems/traps/mines"；通道、Vaal、预留技能不能被触发 | C |
| 不允许嵌套 | Meta Gem 不能插进 Meta Gem | C |
| 双触发即禁用 | 技能不能有两个触发源 | C |
| 成本/折扣 | 触发辅助有"less damage"、成本倍率 | C |
| 冷却/帧限 | CoC 冷却；"每帧最多触发一个法术" | C |
| 能量与来源强度挂钩 | Monster Power、点燃强度、伤害缩放 | A（0.1.0d） |
| 资源总量门 | Spirit | B |
| 每个辅助宝石全局唯一 | Rogers 采访 | B |

### 4.5 玩家读状态与维护成本
- PoE 的可读性来自**装备界面可读的宝石链**（静态构筑可见），而不是战斗中的状态；这在单人 ARPG 里成立，**在 FFA 竞技里不成立** [设计推断]。
- 成本：宝石数量巨大且每期联盟都要重调；0.3.0 的合并说明开发者也在用合并削减变体 [设计推断]。

## 5. 补充对照（只取新增原则）

### 5.1 Heroes of the Storm：英雄技由玩家在固定节点选择（C4）
来源：Dustin Browder，"From the Bullpen: Designing the Talent System"（Blizzard News，2014-07-02）[外部证据-A]。
- 早期用过**通用道具商店**，"商店里所有道具必须对所有英雄都成立，我们一直在削弱道具以免它们破坏某些英雄"；最终决定"一刀切的定制系统不适合我们的游戏"，改为**每英雄独有的选择**，代价是"难得多，因为必须逐个英雄手工设计有趣选择"[外部证据-A]。
- 英雄技（Heroic）在 10 级二选一（例：Muradin 的 Avatar / Haymaker）；Tab 屏幕让玩家看到**敌我双方的天赋构筑**，"用这些信息决定怎么构筑"[外部证据-A]。
- 对 Lockdown 的启示：**通用卡数值修饰与"每个主动各自的 R"是两件事**（这条与 EB §7.1 对"改写操作 vs 数值牌"的区分一致）。

### 5.2 Deadlock：Imbue（把物品效果绑定到一个自选主动上）
来源：Deadlock Wiki「Imbue」（更新 2026-09-07）[外部证据-C]；SenseOS 指南（2026-10-25，"2026-10-22 游戏数据中有 9 件 imbue 物品"）[外部证据-B/C]。
- 购买时由玩家**选择**被强化的技能；**一旦绑定，更换只能卖掉重买**；多数物品不能绑在被动上；Echo Shard 同时是 imbue 与主动，其冷却取决于被绑技能，且不能绑终极技能 [外部证据-C]。
- 这是"效果绑定对象在获取时锁定 + 少数物品对终极技能做明确排除"的实例。我没有取到 Valve 对此的一手设计论述，此处只作为机制示例。

## 6. 对 R1–R5 与 C1–C6 的证据映射

| 候选 | 最相近的外部证据 | 要点 |
| --- | --- | --- |
| R1 强化下一次行动 | Hades II Ω（按武器逐个手写）；Hades II Moon Water→"Saturation：Hex 同时发射 Ω Cast"（2024-10-16）| 一旦"强化"成为独立语义对象，每条旧内容都要补"是否影响它" |
| R2 可构筑终极 | Hades Call（boon 定义内容、1 格/4 格分档）；RoR2 装备；HotS 英雄技 | 内容手写、数量有限、供给收紧；功率靠逐个调参 |
| R3 可构筑状态 | Hades Aid 中的持续型（Athena、Ares：补丁里调"持续时间"）；PoE2 Ritual Cadence | 持续时间是主要调参轴；状态期间的叠加需明确规则（Hex 不能在效果中重复充能） |
| R4 R 武装状态、构筑决定强化什么 | **PoE2 Invocation**（能量存储→手动释放→按存储量触发所插技能）；Deadlock imbue | 最接近的是"容器 + 载荷"；需要不嵌套/不递归规则 |
| R5 由组件生成 R | PoE 技能宝石 + 辅助；Meta 宝石 | 关键石定主形态，卡牌只加子效果≈"主技能 + 辅助"；但 PoE 靠**界面可读的静态构筑**，竞技场不适用 |
| C1 卡牌 | Hades boon（修饰 Ω/Call）；PoE 辅助宝石 | 修饰条目随 R 语义对象数量线性增长 |
| C2 关键石 | PoE2 Ritual Cadence（关键石改 Invocation 行为）；EB §8 | 关键石是改规则的合适位置 |
| C3 技能分支 | HotS 天赋、Hades Aspects | 每主动单独设计，成本最高但最可读 |
| C4 显式核心选择 | HotS Heroic 二选一；Deadlock imbue 购买时选 | 可读、可预测；缺点是选择空间窄 |
| C5 涌现 | Noita（EB §8）；PoE Triggers | 涌现靠**可读 + 硬封顶**才站得住，本轮外部证据不支持在 FFA 里以涌现作主机制 |
| C6 混合 | PoE（主形态 + 修饰）、Hades（槽位 + 内容） | 外部证据最多的结构 |

## 7. 可迁移原则（12 条）

每条格式：**原则**｜支持证据（级别）｜前提｜Lockdown 是否满足｜误用的失败模式。

### P1. 输入语法固定，内容数据化：R 只定义"武装→选择载荷→消耗→冷却"，载荷由构筑给
- 证据：Hades Call 固定 4 格/满格分档、内容由 boon 定（A/C）；PoE 容器—载荷（C）；HotS 每英雄手写选择（A）
- 前提：载荷必须用统一的数据字段（目标类型/次数/持续/形状）描述，不能让规则读到具体 archetype。
- Lockdown：**部分满足。** 24 主动 + 94 卡已是数据驱动；R 的载荷字段要新增。
- 失败：为每个 archetype 写特判（违背"禁止 per-archetype 硬编码"），或载荷字段太宽导致每张卡自带一套新规则。

### P2. 只有一个"主形态权威"，其余作者只能是带类型的子效果
- 证据：PoE 技能宝石定主形态、辅助只修饰；Hades 一槽一神；HotS 10 级只选一个 Heroic（A/C）
- 前提：主形态来源必须可在构筑界面一眼看到；子效果只能改声明过的参数。
- Lockdown：**满足条件取决于选 C2 / C4 / C6。** 关键石（18 点）天然是"每局最多一个"的权威。
- 失败：卡牌也能改主形态，两张卡各改一次导致冲突无法判定；或"所有来源都能改所有东西"，退回 Noita 式不可读。

### P3. 修饰按"参数轴"声明，而不是按具体技能写"也影响 R"
- 证据：Hades II 补丁里大量"X 现在也影响 Ω"逐条补丁（A）；RoR2 proc 系数是单一标量接口（A）；PoE 标签限制（C）
- 前提：R 载荷暴露一组有限参数轴（如：次数、半径、持续、目标选择、附带效果槽），卡牌只能写"+某轴"。
- Lockdown：**需新建。** 现有牌按本体/技能/冲刺宿主；R 要新增一个宿主类别（"R 载荷"）。
- 失败：每张旧牌被迫逐个审计是否影响 R（Hades II 2024-06-04 的补丁形态）；新牌与旧牌的交互矩阵 N×M 增长。

### P4. R 的充能只来自"玩家起源的事件"，派生事件系数为 0
- 证据：RoR2 DoT 系数 0、Singularity Bands 不能触发自己（A）；PoE2 "能量不能从被触发技能直接效果获得"（C）；Hades 自动充能 boon 封顶 25%（A）
- 前提：每个事件携带"起源标签"（玩家手动/被动命中/派生）。
- Lockdown：**应满足。** 刚修的"同帧触发风暴"说明事件系统已有同步传播问题；起源标签是最低成本的防护。
- 失败：只靠"概率低"压制，等于等玩家找到链条；或给派生事件一律 0 导致 on-hit 类卡牌失去存在感（RoR2 2019 同时把 on-hit 改成按总伤害百分比作补偿）。

### P5. 充能与来源强度挂钩，而不是按"命中次数"
- 证据：GGG 0.1.0d "如果按 Boss 战平衡触发，清怪时就会触发得太多"，改为 Monster Power、点燃强度、伤害缩放（A）；Hades II Hex 按 Magick 花费充能（A）
- 前提：玩家能理解"强的事件给更多 R"，并且来源强度可数值化。
- Lockdown：**应改写。** FFA 里不存在"小怪 vs Boss"，但存在"打 1 个满血 vs 打一堆残血"、"刷 AoE 的低伤害技能"：充能应按**造成/承受的有效伤害或对玩家的威胁**，而不是命中数。
- 失败：用命中数充能 → 攻速卡 + 额外打击卡（刚刚出过的指数触发风暴）直接变成 R 充能机器。

### P6. 触发/派生效果付"折扣税"，且必须在界面写明
- 证据：PoE 触发辅助 "less damage" + 成本倍率 + 冷却（C）；RoR2 proc 系数 0.2~0.6（A/B）；Hades II Blood Line "附加 Magick 费用"（A）
- 前提：折扣与派生来源一一对应，可在文本里看到。
- Lockdown：**可满足。**
- 失败：折扣隐藏在代码中，玩家不理解为什么同样的卡"这次没触发"（RoR2 Noxious Thorn 因"难以判断是否生效"被重做，A）。

### P7. 递归/循环守卫四件套（失败闭合）
- 证据：PoE（双触发即禁用、Meta 不嵌套、每帧最多一个、冷却）（C）；RoR2（逐例外 + 热修）（A）；Hades II（Hex 效果中不可充能）（A）
- 做法：①事件起源/深度标签；②每来源每帧最多一次、每来源冷却；③同一载荷中"R 效果不能再触发 R"；④违规构筑在构筑界面标红并**禁用该效果**，而不是静默忽略。
- Lockdown：**已有深度上限 16、每步 256 后续效果**（见 EB §8.2）；缺的是"来源级"限制与"违规禁用"。
- 失败：只靠全局深度上限 → 上限内仍能放大 16 层；只靠热修 → 每次新卡都会出一个新环路（RoR2 Forgive Me Please 2020、2024 两次）。

### P8. 资源不足时不阻断基础动作，且必须有明确反馈
- 证据：Hades II "Executioner's Chop 不再因 Magick 不足阻止挥击"、PLP2 Hotfix 5 修"Magick 不足时 UI 反馈"（A）
- 前提：R 与普攻/技能解耦。
- Lockdown：**应满足。**
- 失败：R 的失败状态吞掉点击或延迟基础动作，在点击移动 + 锁定攻击的操作里体现为"角色突然不动"。

### P9. R 的燃料独立于"属性点既是属性又是商店货币"
- 证据：Hades II 的 Magick 回复/费用已是最大的修饰面（A）；EB 的 Brotato / Backpack 条目关于"货币与属性互相转换"（引用）
- 前提：R 的燃料来源要与经济货币隔离，否则买商店 = 买 R。
- Lockdown：**风险点。** 属性点既是属性又是货币，若 R 费用或燃料吃属性点，会让"买牌"与"开 R"互相挤占。[设计推断；无外部直接对照]
- 失败：玩家为开 R 不买牌，或卖属性换 R；价值观压缩后构筑多样性下降。

### P10. 绑定在获取时锁定，变更有代价
- 证据：Deadlock imbue（卖掉重买才能改）（C）；HotS 固定节点选择（A）；Hades 一槽一神（C）
- 前提：构筑界面必须在选择前展示结果。
- Lockdown：**可满足**，与关键石一致。
- 失败：允许随时重绑 → 对手无法预测对手构筑；全锁死 → 抽到不好的牌就废局（EB §7.5 已有重抽讨论）。

### P11. R-granting 内容稀少、晚出、可见
- 证据：Hades 2020-01-28 降 Aid 提供率；Greater Recall 加前置（A）；RoR2 把高协同内容放进专用装备/Lunar/Boss 类别（A）
- 前提：玩家不会在第 1 分钟就拿满 R 修饰。
- Lockdown：**需用卡池权重而非前置实现。** EB §7.2 已指出 Lockdown 用 drop_weight，缺少"可规划前置"。
- 失败：R 太早满配 → 开局滚雪球；R 太稀有 → 构筑"没有 R"的局数过多。

### P12. 维护成本随"R 语义对象数 × 修饰源数"增长，所以用合并与标量接口压缩
- 证据：Hades II 逐条补丁（A）；GGG 合并 Cast on Freeze/Shock/Ignite（A，摘要级）；HotS "逐个英雄设计更难"（A）
- 做法：新增 R 修饰先问"是否能并入已有参数轴"；不能则合并为新轴而不是新机制。
- Lockdown：**需主动治理。** 94 张卡 × 5 个 R 宿主类 = 470 个潜在交互。
- 失败：补丁说明里反复出现"X 现在也影响 R"。

## 8. 专项指引

### 8.1 多张 R 修饰牌叠加时的规则冲突解决（[设计推断]，基于 PoE/Hades/RoR2 证据；需原型验证）
| 冲突类型 | 建议规则 | 外部依据 |
| --- | --- | --- |
| 同一参数轴的数值修饰 | 同轴相加（加法池），不同轴相乘；每轴硬上限 | RoR2 "无限流血"被改难度（A）；Noita 封顶（EB §8） |
| 改写主形态 | 同时只允许一个，**来源固定为关键石/核心选择**；其余卡牌不可改写主形态 | PoE 技能宝石定主形态（C）；HotS 单 Heroic（A） |
| 改写目标类型（例：自己→指定地面） | 同一载荷只能有一个；冲突时**禁用后来者并在构筑界面标红** | PoE "两个触发源即禁用"（C） |
| 追加子效果（例：落地爆炸） | 允许多个，但每个占一个"子效果槽"，总数封顶（建议 2~3，具体待原型） | PoE 辅助上限 5（B）；Hades Blood Line 等"附加效果 + 费用"（A） |
| 次数/连发 | 加法，设硬上限，**连发之间不重新评估触发**（避免 Momus/Helheim 式连发被其他连发放大） | Hades II 连发被多次调（A） |
| 优先级 | 按**宿主槽位的固定顺序**（本体→Q→W→E→D）确定，界面显示序号 | PoEDB 确定顺序（C） |
| 费用/冷却修饰 | 单独一个"费用轴"，下限为固定值，禁止降到 0 | Hades II Magick 费用是最大调参面（A） |

### 8.2 递归/Proc 守卫（建议落地顺序，[设计推断]）
1. 为每个事件打"起源标签"：玩家手动 / 命中被动 / R 载荷 / 其他派生。
2. R 充能与 R 载荷内伤害：**只接受"玩家手动"或"命中被动"的起源**；R 载荷产生的事件对 R 充能系数为 0（对应 RoR2 DoT=0、PoE2 能量规则）。
3. 每个载荷每帧最多结算一次；同一来源在 N 帧内最多触发一次（对应 PoE CoC "每帧一个"+冷却）。
4. 载荷不得调用 R（硬编码在数据校验层，不在运行时判断）。
5. 把"来源级冷却"写进数据，而不是代码；构筑界面给出违规告警。
6. 每次新增卡牌/分支，必须过一遍"自环 / 双环"自动测试（对应 RoR2 Forgive Me Please 两次热修、Singularity Bands 2022）。

### 8.3 FFA 中的增强状态可读性（[设计推断]，证据为单人/团队游戏，FFA 无直接外部对照）
- 外部可借鉴的**做法**：开发者把过强的视觉噪声压下去（Hades Zeus' Aid 2020-01-21）、给"可用"状态加指示（RoR2 Warped Echo）、给"即将失效"加音频（Chronic Expansion）、让所有人看到彼此构筑（HotS Tab 屏幕）。
- **关键缺口**：这些来源面对的是单人或团队，玩家主要读自己的状态；Lockdown 是 8 人 FFA，**别人必须读你**。
- 建议：
  1. 一套统一视觉语法：**"R 已武装"一个全局标记 + 主形态一个形状/颜色码**（由关键石决定），卡牌子效果不改变这一层，只在效果发生时出现。
  2. 增强状态的**窗口要比普通技能更可预告**（载荷真正生效前有短暂可见预告）；否则对手无法反制。
  3. 同时显示的"不同增强视觉"数量封顶，避免多人同时开时屏幕噪声过载。
  4. 记分板/对手构筑可见（HotS 的做法）。
  5. 避免"无声"状态；每个主形态需要独立的音频辨识。
- 失败模式：R 状态对旁观者不可读 → 玩家觉得"被不明机制击杀"，违背"Simple Controls, Deep Decisions"。

## 9. 缺口与不确定

- Hades II Ω 与 Magick 的一手设计意图未找到；已读补丁说明只能证明开发行为。
- PoE 3.15/3.16 触发改动无法证实；GGG 一手论述集中在 PoE2 0.1.0d。0.3.0 的 Cast on Elemental Ailment 与 Ritual Cadence 只有搜索摘要级证据。
- RoR2 装备槽的设计原话未找到。
- Hades 1 的 God Gauge 结构（4 格、满格 = Greater Call）来自 Wiki，不是 Supergiant 原话。
- Deadlock 一手论述未取得；Vampire Survivors 进化与 Noita 新增内容未研究（Noita 已在 EB §8，本文不重复）。
- FFA 竞技环境里"增强状态可读性"没有外部一手对照，§8.3 属推断。
- PoE 的"increased vs more"乘算分层是已知机制，但本轮未取一手来源，未据此下结论。

## 10. 来源索引

| 编号 | 标题 | 发布者 / 日期 | URL | 级别 | 状态 |
| --- | --- | --- | --- | --- | --- |
| H1 | Hades 补丁说明（Long Winter / Nighty Night / Blood Price / 1.0 等） | Supergiant，2020-01-21、2020-01-28、2020-03-10、2020-06-23、2020-09-17 | `https://api.steampowered.com/ISteamNews/GetNewsForApp/v2/?appid=1145360&count=300&maxlength=0&feeds=steam_community_announcements`（Long Winter：https://steamstore-a.akamaihd.net/news/externalpost/steam_community_announcements/3690071589859860255；EA039：…/2597949245066272359；Blood Price：…/3384966595888900267） | A | 过滤读取相关行 |
| H2 | Hades:Call / Hades:Combat | Hades Wiki，2026-11-06 / 2026-11-26 | https://hades.wiki.gg/wiki/Hades:Call ；https://hades.wiki.gg/wiki/Hades:Combat | C | 搜索摘要 |
| H3 | Hades II 补丁说明（EA2、EA3、Olympic、Warsong、Unseen、EA10、EA11、1.0、PLP1、PLP2、Hotfix 3/5） | Supergiant，2024-06-04 至 2026-07-28 | API：`...GetNewsForApp/v2/?appid=1145350&count=200&maxlength=0&feeds=steam_community_announcements`；EA3：https://store.steampowered.com/news/app/1145350/view/6085006571972858968 ；EA11：https://steamdb.info/patchnotes/19326583/ ；1.0：https://store.steampowered.com/news/app/1145350/view/512967793587520047 | A | 过滤读取 `Omega|Magick|Hex` |
| H4 | Hades 2: Omega Variant Attacks Explained | GameRant，2024-05-06 | https://gamerant.com/hades-2-omega-moves-cast-special-attack-explained/ | B | 搜索摘要 |
| H5 | Complete Guide To Omega Moves In Hades 2 | TheGamer，2024-05-25 | https://www.thegamer.com/hades-2-omega-moves-cast-magic-explained/ | B | 搜索摘要 |
| H6 | Supergiant Games Interview With Greg Kasavin and Darren Korb | RPG Site | https://dev.rpgsite.net/interview/20348-supergiant-games-interview-2026-greg-kasavin-darren-korb-hades-2-ending-music-future-coffee | B | 仅摘要，未见 Ω 内容 |
| R1 | Hopoo Games Development Thoughts #1（2019-03-30）/ #3 / #16（2020-08-04）/ #17（2020-08-13）；Scorched Acres 更新（2019-06-25）；Anniversary Update（2021-03-25）；SotV 1.2.4（2022-05-26）；Devotion Update（2024-05-20） | Hopoo / Gearbox | API：`...GetNewsForApp/v2/?appid=632360&count=400&maxlength=0&feeds=steam_community_announcements`；Thoughts #16：https://steamstore-a.akamaihd.net/news/externalpost/steam_community_announcements/3664193578978242983 ；Thoughts #17：…/4828375451919323887 ；Thoughts #1：…/2465065785306277488 | A | 已读相关段落 |
| R2 | Dev Diary 7: Seekers of the Storm Roadmap Phase 1 - Items & Elites | Gearbox，2024-12-10 | Steam 公告（API 内） | A | 已读相关段落 |
| R3 | Proc Coefficient | Risk of Rain 2 Wiki | https://riskofrain2.fandom.com/wiki/Proc_Coefficient | C | 搜索摘要 |
| R4 | How Do Proc Coefficients Work In Risk Of Rain 2? | GameRant，2024-09-06 | https://gamerant.com/risk-of-rain-2-ror-2-proc-chain-coefficients-explained/ | B | 搜索摘要 |
| P1 | Trigger / Cast On Critical Strike Support / Spellslinger | PoE Wiki | https://www.poewiki.net/wiki/Trigger ；https://www.poewiki.net/wiki/Cast_On_Critical_Strike_Support ；https://pathofexile.fandom.com/wiki/Spellslinger | C | Trigger 页已读（部分）；其余摘要 |
| P2 | Energy | PoE2 Wiki | https://www.poe2wiki.net/wiki/Energy | C | 已读前 6000 字符 |
| P3 | 0.1.0d Early Access Patch Notes（GGG Kieren_GGG 原帖，Maxroll 转载） | GGG / Maxroll，2024-12-12 | https://maxroll.gg/poe2/news/0-1-0d-early-access-patch-notes ；GGG：https://www.pathofexile.com/forum/view-thread/3621299 | A（经 B 转载） | 转载页已读；GGG 原页未直接读到 |
| P4 | Early Access Patch Notes - Content Update 0.3.0 — The Third Edict | GGG | https://www.pathofexile.com/forum/view-thread/3826682 | A | 仅搜索摘要 |
| P5 | Path of Exile 2 Interview With Jonathan Rogers | Out of Games，2023-07-29；Wowhead，2024-06-18 | https://outof.games/news/6413-path-of-exile-2-interview-with-johnathan-rogers-skill-gem-upgrading-gold-flask-changes-more/ ；https://www.wowhead.com/news/path-of-exile-2-interview-with-jonathan-rogers-jessirocks-343146 | B | 搜索摘要 |
| P6 | PoE 2 Guide: Meta Gems Explained / How to Get and Use Meta Gems | Mobalytics，2025-09-11 / GameRant，2025-02-07 | https://mobalytics.gg/poe-2/guides/meta-gems ；https://gamerant.com/path-of-exile-2-meta-skill-gem-explained-poe2/ | B | 搜索摘要 |
| P7 | Version 3.16.0 | PoE Wiki（官方补丁转录），2021-10-22 | https://www.poewiki.net/wiki/Version_3.16.0 | C | 读到前 3000 字符；未见触发专项条目 |
| X1 | From the Bullpen: Designing the Talent System | Dustin Browder，Blizzard News，2014-07-02 | https://news.blizzard.com/en-us/article/14665104/from-the-bullpen-designing-the-talent-system | A | 已读 |
| X2 | Imbue | Deadlock Wiki，2026-09-07 | https://deadlock.wiki/Imbue | C | 已读 |
| X3 | Deadlock Items Explained: Tiers, Flex Slots, Imbue | SenseOS，2026-10-25 | https://senseos.gg/guides/deadlock/items-and-shop/ | B/C | 搜索摘要 |

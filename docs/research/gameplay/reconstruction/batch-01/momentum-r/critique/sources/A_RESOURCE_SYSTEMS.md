# Track A：资源与强化系统外部研究（劲 + R 键）

调研日期：2026-11-28。范围：LoL（Karma / Rengar / Renekton / Gnar / Udyr + Riot 资源哲学）、Battlerite、Overwatch、SF6 Drive、Guilty Gear、Brawl Stars、Dota 2。
Battlerite 的 Big Patch / Armory / Overload / 回合结构已在 `docs/research/gameplay/EXTERNAL_BENCHMARKS.md` §3 覆盖，此处不重复，只补 EX / 能量 / Royale。

## 0. 标签与总体结论

- [外部证据-A] 开发者/官方一手（官方博客、官方系统页、开发者博客问答）
- [外部证据-B] 可靠二手（媒体转述或翻译开发者访谈；已标出转述来源）
- [外部证据-C] 玩家维护的 wiki / 社区指南（只用于机制数值）
- [设计推断] 我对证据的推论；[原创假设] 无外部证据支持的新点子

结论先行：

1. 所有"强化下一次技能"的设计（Karma、Renekton、Rengar、Udyr、Battlerite EX、SF6 OD）都有同一个骨架：**一个有界资源 → 一个触发条件 → 一次对已有技能的修改**。决策来自"强化哪一个技能/何时"，不来自"要不要按"。
2. 一手证据中最直接对应 Lockdown 的是三条：Battlerite Royale（最多 20 人 FFA）**删除了 EX 与 R 的能量消耗**；Riot 说次级资源条与法力"抢 UI 且常覆盖同一价值，所以不需要两个就不加"；SF6 导演说格斗游戏"应尽量限制不同量表的数量"。三者都指向"不加第二条通用条"。
3. Renekton（Fury ≥50 时下一个基础技能被强化、强化技能不再产怒气、半血以下额外获取、战斗外衰减）是与 Lockdown 劲机制最像的现成设计，但其增长速度远慢于劲（见 §6 P3）。
4. 需要纠正任务简报中的一处：**Udyr 2022 重做没有"通过大招觉醒"**。他没有大招；觉醒是"进入姿态后 0.25–5 秒内再次施放同一姿态"，由一个周期冷却门控 [外部证据-C]。Riot 一手文章只给了设计目标 [外部证据-A]。
5. 未能找到的一手内容：Daisuke Ishiwatari 关于 Strive 保留/调整 Burst 与 Roman Cancel 的专门原因；Riot 对 Karma 机制的"为何不是死键"的原文。见 §8。

---

## 1. League of Legends

### 1.1 Riot 的资源哲学（一手）

来源：Ask Riot: Manaless Champions，RiotAether / Riot Jag / Riot Axes，2021-07-02，https://www.leagueoflegends.com/en-us/news/dev/ask-riot-manaless-champions/ [外部证据-A]

- 法力"自动提供节奏"：对线需要"张弛"，许多技能不加限制就应该被无脑连发。
- 法力让每个技能的**赌注**可校准：Blitzcrank 的钩子必须"放空要付出重要代价"；冷却"只能推到有限程度"，没有法力时钩子冷却要多长才行。
- 新英雄默认带法力；只有"有充分理由"才无法力。
- 次级资源条（Grit / Heat / Energy）"在 UI 上与法力竞争"，且"常覆盖与法力相同的价值，所以不需要两个就不同时包含"。Rumble 的 Heat 是正面案例：它限制连发并强制停顿或冲突，天然给前期定节奏。
- Viego：若用法力，变身/附身时法力条显示什么会产生"理解成本"，这对已很复杂的英雄是压力。
- 持续型无资源模式（如 Yasuo）"会给其余技能的取舍和平衡带来很大压力，常使单个技能平衡后显得不够强"。

对 Lockdown 的含义 [设计推断]：劲同时是 Q/W/E 的消耗（法力角色）和 R 的燃料（次级资源角色）。Riot 的话说明"法力"与"次级条"各自承担不同价值（节奏 vs. 形态/强化），合并成一条意味着两种价值互相挤占。

### 1.2 各英雄机制表

| 英雄 | 获得 | 消耗/触发 | 创造的决策 | 对手可读性 / 反制 | 开发者问题与调整 | 统一输入承载什么 |
|---|---|---|---|---|---|---|
| Karma（Mantra） | 旧版 Mantra 是大招，靠冷却；VU 后"普攻与技能降低 Mantra 冷却，可'买回'大招"，并"让 Mantra 以截然不同的方式强化每个技能，带来更有影响的决策" [C，引 Riot 开发说明，见 Karma/Development] | 施放 Mantra → 8 秒内下一个技能附加额外效果，**与原技能共享冷却** [C] | 选择强化哪个技能（伤害区域 / 治疗链接 / 群体护盾）；是否等 Mantra 冷却 | 靠技能图标与特效读取；反制是躲强化后的更大效果 | wiki 记录某个设计迭代为"施放技能得叠层、三层强化下一技能，使其不是'死键'"；**时间与原文出处未核实** [C] | 一个键，效果由"下一个选的技能"决定，天然随技能组合而变 |
| Rengar（Ferocity） | 施放基础技能/强化普攻得 1 层，上限 4；受伤/造成伤害后 10 秒内不衰减 [C] | 满层时下一个基础技能被强化：额外效果 + 额外一次施放 + 1.5 秒移速 [C] | 以"技能顺序"凑满层，何时释放强化技能；Bonetooth 叠层是长期成长 | 获得 Bonetooth 层与 Ferocity 有语音提示（wiki 补丁记录）[C]；强化技能无独立键 | 无一手开发者文本；wiki 记录"到 4 层瞬间进入强化，但灌木攻击获得的层有延迟" [C] | 无专门键：**强化是自动状态**，玩家只决定哪个技能吃它 |
| Renekton（Fury） | 普攻命中 +5；技能命中；半血以下 +50% 获取；Dominus 每秒 +5 [C] | ≥50 时下一个基础技能（不含 Slice）消耗 50 并强化；**强化技能不生成怒气**；战斗外 12 秒后每 0.25 秒 -1 [C] | 攒满并选择强化哪个技能（眩晕 / 回复 / 范围）；强化技能不再产怒 → 强化有机会成本 | 满怒时头顶条与特效 [设计推断，wiki 未明说]；反制：躲眩晕前摇，或在其未满怒时打 | 无一手文本；wiki 补丁记录多次调高/调低强化伤害与治疗上限 [C] | 与 Lockdown 现有劲最像：阈值 50、下一技能强化、战斗外衰减 |
| Gnar（Rage） | 受击/造成伤害 4/7/11 每 2 秒；普攻与回旋镖命中立即加；**13 秒不战斗衰减** [C] | 满怒：下一个技能变为 Mega Gnar 技能；**若 4 秒内不施放则自动变身**；Mega 持续 15 秒（怒气条倒计时）；返回后 15 秒"疲惫"不能获怒 [C] | 小形态 = 灵活清扫，Mega = 肉与控制但慢；以"选择在哪里弹出变身"为核心 | 形态本身是巨大视觉变化；Riot 的 Champion Insights（引自 wiki）写 Mega 是"巨像……但慢，对手几乎可以朝反方向走开" [C] | 自动变身与"疲惫"期都是对"囤着不用"与"永久强势"的限制 [设计推断] | 两种形态各自完整的 Q/W/E/R，输入不变，**形态替换整套技能含义** |
| Udyr（2022） | 无资源条；4 个基础技能各有 1.5 秒全局冷却，每个给予一种姿态；进入姿态后周期性可再次施放以"觉醒"[C] | 觉醒窗口：进入姿态后 0.25 秒起 5 秒内免费再施放；觉醒进入长冷却（游戏开局即满冷却）；"觉醒冷却"显示在血条下，**所有队伍可见** [C] | Riot：**"所有构筑都应有想觉醒每个姿态的时候"**；觉醒效果随**等级**而非姿态等级成长，使"一个姿态一点都不加"成为有意义的决策 [A] | 觉醒条对所有队伍可见 [C]；强化是明显的特效/形状语言（Riot VFX："颜色最先，其次形状语言"）[A] | Riot：Q 和 R 都是纯伤害时"谁更高效谁赢"，所以把 R 的力量转为 slow 与追击等功能，让选择"不只是一道复杂数学题" [A] | 一个统一操作（再次施放同键）+ 构筑决定哪个觉醒有价值 |

一手链接：
- Udyr VGU: Rebuilding the Spirit Walker，Riot，2022-08-23：https://www.leagueoflegends.com/en-gb/news/dev/udyr-vgu-rebuilding-the-spirit-walker/ [A]
- An Udyr for Every Occasion，Riot Stashu / RiotAether，2022-08-24：https://www.leagueoflegends.com/en-gb/news/dev/an-udyr-for-every-occasion/ [A]

二手/社区：
- Gnar：https://wiki.leagueoflegends.com/en-us/Gnar 、…/Gnar/Development [C]
- Karma/Development：https://wiki.leagueoflegends.com/en-us/Karma/Development [C]
- Rengar：https://wiki.leagueoflegends.com/en-us/Rengar [C]
- Renekton：https://wiki.leagueoflegends.com/en-us/Renekton 及 Template:Data_Renekton/Reign_of_Anger [C]
- Udyr：https://wiki.leagueoflegends.com/en-us/Udyr [C]

### 1.3 LoL 小结 [设计推断]

- Karma/Rengar/Renekton 的"强化下一技能"都**避免专用强化键**：玩家用同一个 Q/W/E 键，决定放在哪个技能。这降低输入负担，但也让强化状态的"开关"藏在技能选择里。Lockdown 新增 R 键本质是把"开关"独立出来（类似 Karma Mantra 和 Udyr 觉醒的再次施放），多了一个决策点，也多了一个按键。
- Gnar 与 Udyr 说明强化若是**时限窗口**，反制就变成"拖到窗口结束"或"拉开距离"，不需要反应力。
- Riot 的 Udyr 文章是唯一直接谈"一套统一的强化机制，如何在不同构筑下都有价值"的一手来源。

---

## 2. Battlerite（Stunlock）

### 2.1 EX 与能量

来源：官方 wiki（社区维护，机制性描述）[C]
- Abilities：https://battlerite.fandom.com/wiki/Abilities ；Energy：https://battlerite.fandom.com/wiki/Energy（直接抓取 403，内容来自搜索摘要）
- 能量通过**命中基础技能**小幅获得；EX 是某个基础技能的升级版，**消耗 25%–50% 能量**，每个英雄有 2 个；默认 Shift+技能键触发；**使用 EX 会同时消耗对应基础技能的冷却**；每个英雄有 100% 能量的大招（默认 F）；被打断的引导大招按剩余时间返还能量 [C]。

决策 [设计推断]：花能量强化哪一个技能（EX）还是存满放大招；EX 与基础技能共享冷却，所以"用 EX"不是纯增益，而是"这一次用强版还是留给下一次"。

可读性/反制：EX 是 Shift 变体，强化版的效果通常是显著不同的动画/效果 [未核实，社区描述]；反制仍主要靠 WASD 走位。

统一输入承载：Shift 修饰键 + 同一技能键 → 每个英雄两项（定制）EX，是"一个通用修饰键，英雄各自定义"的典型。

### 2.2 Battlerite Royale 删除 EX（一手）

来源：Battlerite Royale FAQ，Stunlock Studios，2018-09-05，https://blog.stunlock.com/ru/battlerite-royale-faq/ [外部证据-A]

关键事实（原文表述，转述）：
- Royale 是**最多 20 人的自由混战**，地图为竞技场的 30 倍，靠搜刮、金币与商店变强。
- "致死性需要提高，因为玩家必须按一定节奏被淘汰，这意味着必须平衡防御机制，尤其是治疗、护盾和控制。"
- "加入消耗品后，我们引入了额外热键，也带来了一层额外的策略与复杂度。"
- "在平衡防御机制时我们发现 **EX 技能在 Royale 中并没有真正起到作用**；许多 EX 是防御性的（控制、吸血、帮助你活得更久）。"
- "因此我们移除了 EX 技能以及 R 的能量消耗。**消耗品很好地填补了 EX 的空缺。**"

两点需要明确：
1. 一手原因是**"EX 多为防御性，与 Royale 需要的致死节奏冲突，并且消耗品已增加了热键与复杂度"**——不是"EX 太复杂"这类泛泛理由。
2. Royale 同时**删除了 R（大招）的能量成本**，R 改为纯冷却门控。这是一个 FFA 变体删除"能量 → 大招"耦合的直接先例。

[设计推断] 对 Lockdown：Arena 是小队对战，战斗节奏固定，能量门控可行；在 FFA 的 Royale 里，Stunlock 选择去掉 EX 与大招的能量门控，改由"拾取到的消耗品"提供一次性强化——这与 Lockdown "属性点兼商店货币"和"构筑卡"路线更接近。注意：Stunlock 的删除理由是"防御性 EX 妨碍致死节奏"，Lockdown 的 FFA 同样需要淘汰节奏，但这一对应是推断。

### 2.3 与 EXTERNAL_BENCHMARKS.md 的关系
Big Patch、Armory、Overload、回合重复问题已在 `EXTERNAL_BENCHMARKS.md` §3 [BR1–BR4]；本节仅补充 EX / 能量 / Royale。

---

## 3. Overwatch

### 3.1 终极技能充能经济

- 充能来源：造成伤害、治疗、承受伤害、被动时间；治疗/承伤的充能占比在 Overwatch 2 中调低过（补丁摘要）[外部证据-C，来源 Esports Tales 汇总，https://www.esportstales.com/overwatch/list-all-hero-updates-and-balance-changes ，抓取时间 2026，条目日期未逐条核实]。
- 经济层面（社区指南，非开发者）：对局 12–20 秒的团战被"谁多一个大招"决定；持有大招不放是浪费；赢一场干净的团战会"拒绝敌人充能" [外部证据-C：Guild Order，https://guildorder.com/games/overwatch/wiki/ult-economy-and-fight-sequencing ；Boosteria，https://boosteria.org/guides/overwatch-2-ultimate-economy-track-trade-win-fights ，2026-02-24]。
- 开发者一手：
  - **Director's Take – Balance Preview for Season 8，Aaron Keller（Blizzard），2023-11-30**，https://news.blizzard.com/en-us/article/24034079/directors-take-balance-preview-for-season-8 [外部证据-A]：换英雄时保留的大招充能从 25% 降到 15%。原文："我们确实喜欢这个机制**减少了换英雄的摩擦**，并认为 15% 仍然如此。然而存在一种观感：死亡后反制换英雄几乎总是优势，所以降低这个值，看是否影响换人率。"
  - 同一次平衡的开发者评论（经 Charlie INTEL 转载，[外部证据-B]，2023-12-05）把该机制描述为"帮助对局不那么一边倒，起到**软性回追机制**"：https://www.charlieintel.com/overwatch-2/all-hero-buffs-nerfs-in-overwatch-2-season-8-balance-changes-explained-290238/
  - Dexerto 对 OW2 开发者 Dawson 的转述（2023-01-26，[外部证据-B]）：团队查看了"哪些英雄放大招过多/过少"，认为"有些英雄太快得到最强技能"；并想让坦克换人"没那么有压力"：https://www.dexerto.com/overwatch/overwatch-2-devs-reveal-plans-to-revamp-ultimate-charge-for-all-heroes-2044471/
  - Overwatch 官方补丁页（2024-04）有"换英雄保留的大招充能从 15% 提高到 49%"摘要，**本次只见到搜索摘要，未读全文，数值未核实**：http://overwatch.blizzard.com/en-us/news/patch-notes/live/2024/04/
  - 全局 ult 成本上调（"所有英雄 +25%"、"全局 +12%"等）出现在汇总页 [外部证据-C]，具体版本/日期未逐条核实。

### 3.2 可读性

GDC 2016：Overwatch 音频团队被 Jeff Kaplan 要求做到"可以靠声音玩"；英雄语音必须具描述性；终极语音每次一致，使玩家快速学会，"因为 ult 使用对赢得比赛至关重要，所以需要清晰传达"（后半句是博客作者的归纳）。
- PCGamesN，2016-03-18，https://www.pcgamesn.com/overwatch/overwatch-devs-on-creating-a-game-you-can-play-by-sound-and-announcing-dolby-atmos-support [外部证据-B]
- Awesome Game Audio，2017-02-06，https://awesomegameaudio.wordpress.com/2017/02/06/overwatch-play-by-sound/ [外部证据-B/C，博客归纳]

### 3.3 为什么形成团战节奏 [设计推断]
充能由"造成/承受影响"驱动（接触越多越快）、释放后清零、团战内释放、对手可读（语音+特效）→ 形成"积累-爆发-重置"的节奏循环。Blizzard 的调整（成本上调、换人保留值改动）都是在校准**节奏频率与回追强度**，而不是改变机制本身。

### 3.4 统一输入承载
单个 Ultimate 键（默认 Q）+ 每个英雄一个完全不同的大招——"通用的开关，英雄定义的内容"。因为大招的内容是英雄身份的一部分，选择英雄 = 选择 R 的内容，这与 Lockdown "R 由构筑驱动"同构。区别：OW 英雄固定、全局可读；Lockdown 构筑是对局内动态的，需要额外的可读手段。

---

## 4. 格斗游戏：SF6 Drive 与 Guilty Gear

### 4.1 SF6 Drive 系统

官方：Street Fighter 6 · Fighting Ground，Capcom，https://www.streetfighter.com/6/mode/fightingground （无发布日期）[外部证据-A]
- Drive 量表**回合开始就可用**，用于多种攻防动作（Drive Parry、Drive Rush、Drive Impact、Overdrive、Drive Reversal）；**用光进入 Burnout**，"有很大劣势"，但**自动回复**，"量表管理是关键"。
- Drive Parry 成功会**回复** Drive；Drive Rush 从 Parry 出手消耗 1 格，从可取消普攻消耗 3 格。

社区细节 [外部证据-C]：Burnout 期间 Drive 必须完全恢复后才可再次使用；Burnout 时被格挡不扣量表而是加量；进攻（命中）能缩短 Burnout；Burnout 可被角落压制 https://sf6combo.kagewebsite.com/page/drive-system/ 。

设计原因（导演 Takayuki Nakayama，Famitsu 采访，EventHub 译文 2024-06-06，https://www.eventhubs.com/news/2024/jun/06/sf6-director-drive-system-expression/）[外部证据-B，译文转述一手]：
- "在格斗游戏里，你想尽可能**限制不同量表的数量**。越多，要理解的越多，越难把握。我们想强调的是 Drive 量表是角色的**情感强度或专注度**。"
- SF5 的 stun 量表"填满后角色原地不动，对习惯者容易理解，但更像'游戏系统'而非自然现象"。因此用"一击就晕"的 Drive Impact 之类设计替代。
- 玩家使用方式各不相同：有人故意主动进入 Burnout 以施压，有人避免 Burnout 偏防守，有人根据对手的量表保守或激进；"这个系统带来的玩法多样性超出了我们的想象"。
- 2022-06-19 的另一篇报道（[外部证据-B]，https://www.eventhubs.com/news/2022/jun/19/sf6-developers-parry-focus-attacks/ ）："Drive 量表起源于想要有一个**简单方式**使用量表来释放强力的进攻或防守机制。"

对 Lockdown 的启示 [设计推断]：
- **一个量表同时承担进攻、防守、位移、强化**，决定性的约束是"用光有清晰且可读的惩罚（Burnout）"，且惩罚后能自动恢复（不是永久锁死）。
- 反制"拥有大量 Drive"的是对手的 Drive Impact / 压制，即**状态本身可读**（量表可见），反制来自选择而非反应。
- 一个量表承载多种用法，不依赖每个角色定制；统一输入承载的是"通用工具"，不是"构筑内容"，与 Lockdown R（构筑内容）不同。

### 4.2 Guilty Gear：Tension、Roman Cancel、Burst

一手：
- Guilty Gear -Strive- Battle System（官方站，无日期，https://www.guiltygear.com/ggst/en/battle/systems/）[外部证据-A]：
  - Tension 量表"通过**前进与攻击等主动行为**增长，每回合重置"，用于两项通用机制：Overdrive（消耗 50%）与 Roman Cancel。
  - Roman Cancel："结果根据发动时机变化"——攻击中：冲击波减慢对手；防守中：把对手推开。
  - Burst 量表用于 Psych Burst（100%，被打时强制吹飞）、Counter Blitz（50%）、Deflect Shield（50%）。
  - Wall Break → Positive Bonus（限时：攻击力上升、Tension 自动回复、Burst 增益）。
- Developer's Backyard Vol.1，Akira Katano / Ishiwatari，2020-06-01，https://www.arcsystemworks.jp/guiltygear/db/en/volume1.php [外部证据-A]：
  - 目标是"不必记很长的连段也能造成大伤害"；"现在更强调根据局面选择招式"；"需要学的机制更少，才能在比赛中享受游戏"。
  - UI：Burst 图标随血条移动以减少视线移动；**"我们有意让 R.I.S.C. 量表不那么显眼，以减少首次游玩者要关注的量表数量"**。
- Developer's Backyard Vol.2，2020-07-03，https://www.arcsystemworks.jp/guiltygear/db/en/volume2.php [外部证据-A]：
  - Roman Cancel 介绍为"取消招式、以量表换取多种好处"，强调"在任何情况下攻防都能取得主动"。
  - Q6：Closed Beta 里 Wall Break 的 Tension 奖励被玩家视为"不值得"，所以**玩家刻意不打穿墙**。Katano："我们不想完全否定'不打墙'的选择，这是自由与恶作剧的一部分"，同时认为"打穿墙的收益大于停在角落"，并继续调整。→ **奖励若与代价/风险不对称，玩家会绕开它。**
- Siliconera，2021-06-09（Famitsu 采访的翻译，[外部证据-B]）：Ishiwatari/Katano："……**我们也删去了常见技巧，使各层玩家站在同一起跑线**。"这是泛指，**未特别提到 Burst/RC**。

Negative Penalty（社区）：Dustloop GGXRD/Gauges（页面更新 2024-05-29，https://www.dustloop.com/wiki/index.php?title=GGXRD/Gauges）[外部证据-C]：
- 后撤、站立不动、后跳会累积 Negative Penalty；触发后 **Tension 获取降低 80%、持续 10 秒，并丧失已存 Tension**。
- 该页写作者认为它"是为了保持游戏有趣的功能；并非阻止龟缩，而是阻止长时间完全避免接触"。
- **这是 Xrd 的资料。Strive 的具体数值本次没有核实**（只看到 Dustloop 对 GGST 帧数据的 Tension 获取字段说明）。

[设计推断]：
- Tension 把资源增长绑定在"主动接触"，消费在"通用但与时机相关的效果"上；RC 是"一个输入、多个情境效果"的范式——与 Lockdown "R 键由构筑/情境决定内容"类似，但 RC 的内容由**情境**决定，Lockdown 是由**构筑**决定。
- 负面惩罚适用于 1v1：龟缩者可以让对手无法接触；在 8 人 FFA 中，被动者会被他人或环境拉入战斗，且避战本身是合理策略，**不应照搬**。

---

## 5. 其他提供不同原则的设计

### 5.1 Brawl Stars：Hypercharge（Supercell）
- Supercell Support，Hypercharges，https://support.supercell.com/brawl-stars/en/articles/hypercharge-5.html（无日期）[外部证据-A]：Hypercharge 是时限强化，使 Super 更强，**对所有 Brawler 的速度、伤害、护盾增益数值相同**；通过**攻击对手**充能，"像 Super 一样，但速度更慢"；触发后结束需重新充能。
- Pocket Gamer 对 Frank Keienburg 的采访，2023-09-20，https://www.pocketgamer.com/brawl-stars/frank-keineburg-interview-hypercharge-update/ [外部证据-B]：开发 6–9 个月；用它替代 Power 11 原先"功能性但无聊"的被动装备槽；"取决于玩家水平，一场比赛只能触发几次，在错误时机使用会浪费；强力但不保证获胜，即使对方没有 Hypercharge"。
- 价值：**同一个统一强化，对所有角色数值相同，让角色/构筑差异只体现在被修改的 Super 上**；且频率被明确限制为"每局几次"。
- 同构性 [设计推断]：Brawl Stars 同样是多人混战（含 Showdown 吃鸡）+ 手游简化输入 + 派对感，是最接近 Lockdown 受众形态的参考。

### 5.2 Dota 2：Aghanim's Shard / Scepter
- Liquipedia（社区，[外部证据-C]）：Shard "升级某个已有技能或授予一个英雄专属新技能"，每局每名英雄**只能获得一次**；可由击杀奖励给团队里净值最低的两名英雄之一；https://liquipedia.net/dota2/Aghanim%27s_Shard 。Dota 2 Wiki 7.30 补丁记录列出了大量按英雄定制的 Scepter/Shard 行（[外部证据-C]，https://dota2.fandom.com/wiki/Version_7.30）。
- 价值：**强化来自"构筑项"（物品）而非资源条**，并由落后方优先获得（comeback 机制）。对应 Lockdown "构筑卡 + 属性点商店货币"路线。
- 未找到：Valve 的一手设计说明（本次未检索到）。

### 5.3 未覆盖
Smash 终极技能量表（Sakurai 专栏）、Deadlock（Valve）本次没有找到足够可靠的一手来源，不收录。

---

## 6. 可迁移原则（对劲 + R 设计）

Lockdown 约束：8 人 FFA，俯视，点击移动，移速 240 cm/s（低），Q/W/E + D，普攻锁定，聚会受众，24 个主动技能带分支，约 94 张构筑卡，5 属性 + 18 点天赋，属性点同时是商店货币；劲 0–100，起始 50，战斗中 +5/s，普攻命中约 +7.5/s，技能命中 +5，元素反应 +10，脱战向 50 衰减，Q/W/E 消耗 25–35 且有冷却。

下面"Lockdown 是否满足前提"均为 [设计推断]。

### P1. 统一输入承载"统一形状 + 构筑决定的内容"
- 原则：R 键的交互形状必须一致（一键、一个时机窗口、一个可预期的"对已有技能的修改"）；差异化放在被修改的内容，而不是每把构筑发明一套交互。
- 证据：Overwatch 一个 Ultimate 键，内容由英雄定义 [B/C]；Brawl Stars Hypercharge 对所有 Brawler 数值相同，差异在被修改的 Super [A]；Udyr 觉醒统一为"再次施放" [A+C]；Karma Mantra "同一键、选择哪个技能" [C]；GG Roman Cancel"一个输入、结果依时机" [A]。
- 前提：能把 24 技能 × 分支的 R 效果归并为少量"修改族"（如：强化下一技能 / 施放后短时窗口 / 追加一次施放）。
- Lockdown：满足一半——构筑驱动是对的，但 94 张卡若各自带 R 变体，会不可读。
- 失败模式：R = 数百条定制规则；或反过来 R 变成统一数值 buff，使构筑与 R 脱钩（Udyr 文章指出"所有构筑都应有想觉醒每个姿态的时候"才是目标）。

### P2. 决策来自"强化哪个/何时"，而不是"要不要按"
- 证据：Karma 以"截然不同的方式强化每个技能，带来更有影响的决策" [C]；Udyr 的"所有构筑都有觉醒每个姿态的情境""一点都不加有意义" [A]；Renekton 强化消耗并不再产怒 [C]；Battlerite EX 与基础技能共享冷却 [C]；SF6 玩家在 Burnout 与保守之间风格分化 [B]。
- 前提：强化必须有机会成本（消耗、共享冷却、不产资源）。
- Lockdown：R 若"满阈值即用"且无机会成本，就没有取舍；劲已被 Q/W/E 消耗，天然有成本，但需要明确 R 与 Q/W/E 争用什么。
- 失败模式：死键/永远正确的轮换（Riot 在 Karma 迭代中明确避免"死键"，见 §1.2 注 [C，未核实原文]）。

### P3. 资源增长应由"主动接触/命中"驱动，且节奏要用时间尺度检验
- 证据：GG Tension 由前进和攻击增长 [A]；Renekton 普攻 +5、阈值 50、战斗外 12 秒才开始衰减 [C]；Overwatch 由伤害/治疗/承伤驱动，Blizzard 反复校准"充能过快" [B/C]；Brawl Stars Hypercharge"通过攻击对手充能，每局只有几次" [A/B]。
- 前提：增长速率与对局节奏匹配。
- Lockdown：粗算 [设计推断]：从 50 起，战斗中 +5/s 单独需 10 秒到 100；加上普攻命中 +7.5/s，则约 4 秒。Renekton 每次普攻 5 点、需 50，约 10 次普攻；Lockdown 的阈值 60 只要约 2–3 次技能命中或 ~1.5 秒普攻连续命中，远快于 Renekton。若 R 以劲阈值为门，会近乎常驻。**需实际数值核查**（无源数据，仅按简报）。
- 失败模式：R 变成"每个遭遇都用"，失去稀缺感；与 Overwatch/Brawl Stars 的"几次/局"设计相反。

### P4. 不加第二条通用条；一条资源只做一份工作
- 证据：Riot Ask Riot（次级条与法力抢 UI、覆盖同一价值） [A]；Nakayama"限制不同量表数量"，SF6 用一条 Drive 管理攻防位移强化 [B]；GG 刻意隐藏 R.I.S.C. 以减少首次玩家要关注的量表数 [A]；Battlerite Royale 删除 EX 与 R 的能量成本 [A]。
- 前提：这条资源的"用完"必须有可读的后果（SF6 Burnout；Rumble 过热 [A]）。
- Lockdown：劲同时是 Q/W/E 的法力与 R 的燃料，等同 SF6 的"一条量表多用"，**可行**，但 SF6 需要 Burnout 提供"用光的后果"；Lockdown 目前只有冷却+成本双重限制，没有"清空"状态。
- 失败模式：R 与 Q/W/E 抢同一资源，玩家为留劲给 R 而不用技能（滞留），或 R 把劲清空后无法战斗；与"冷却 + 消耗"两道门叠加，使劲变成第二个冷却（Riot："冷却只能推到有限程度"，资源应校准赌注而非重复冷却 [A]）。

### P5. 强化状态必须对所有对手可读，且不增加读条压力
- 证据：Overwatch 语音+特效使大招可读，每次一致以便快速学会 [B]；Rengar 获得层/满层有语音响应 [C]；Gnar 形态变化 + 怒气条倒计时 [C]；Udyr 觉醒冷却条对所有队伍可见 [C]；SF6 Drive/Burnout 量表可见 [A]；Riot VFX："颜色优先，其次形状语言"以便玩家与对手辨认 [A]。
- 前提：画面的视觉带宽足够；俯视小角色需要地面/体型/音频，而不只是头顶图标。
- Lockdown：8 人同屏，7 个对手；低移速（240 cm/s）给阅读时间，但 FFA 的噪声更大。
- 失败模式：不可读 → "无故被秒杀"；过于嘈杂（8 个玩家各自有强化光效）→ 全屏噪声，反而不可读。

### P6. 反制应针对"状态"，而不是反应检验
- 证据：Gnar 的 Mega 形态慢，"敌人往反方向走就能逃" [C，引 Riot Champion Insights]；Udyr 窗口 5 秒、觉醒长冷却 [C]；Karma 强化需在 8 秒内选定 [C]；SF6 Burnout 可被压制 [C]；Overwatch 用"对手知道你有大招，就跟着做相应反制/用大招换大招" [C]。
- 前提：强化有时限窗口或明确弱点；对手能规划而非即时反应。
- Lockdown：产品负责人已删除高反应输入；窗口型强化与慢移速相容。
- 失败模式：强化使你不可被打断、立刻秒杀，且无窗口 → 只能靠反应力或运气。

### P7. 防止囤积：自动到期或有机会成本
- 证据：Gnar 满怒 4 秒内不放则自动变身，Mega 15 秒后"疲惫" [C]；Overwatch 指南"持有大招不用是浪费" [C]；Brawl Stars Hypercharge 结束后需重新充能 [A]；Renekton 强化不产怒（使用即消耗一个循环） [C]。
- 前提：玩家会优化"何时释放"而不是"是否释放"。
- Lockdown：FFA 中囤积有风险（别人击杀你时浪费），所以玩家本就倾向尽早用；但对"安全发育"的玩家，可永远不用。
- 失败模式：R 要么被囤积到被击杀才用，要么变成冷却循环；需要明确的衰减或上限。

### P8. 被动惩罚不能照搬 1v1 的"消极惩罚"，但要保留"向接触流动"的趋势
- 证据：GG Negative Penalty（Xrd：获取 -80% 持续 10 秒，清空 Tension）[C，Strive 未核实]；Renekton 战斗外衰减 [C]；Gnar 13 秒脱战衰减 [C]；Siliconera：去掉通用技巧，使各层玩家站在同一起跑线 [B]。
- 前提：1v1 中龟缩可阻止对手获得接触；FFA 中被动者被他人拉入或被他人消耗。
- Lockdown：已有"脱战向 50 衰减"，足够；再加负面惩罚会与"第三方渔利"这种 FFA 合理策略冲突。
- 失败模式：把避战者惩罚成弱者，而 FFA 里避战常是最优解，于是惩罚被视为错误设计。

### P9. 回追用"风险比例获取"，而不是"打败后补偿"
- 证据：Renekton 半血以下 +50% 获取 [C]；Overwatch 换英雄保留充能被 Blizzard 的开发者评论描述为"软性回追机制"，并因"几乎总是优势"而下调 [B+A]；Dota Aghanim's Shard 优先给团队里净值最低者 [C]。
- 前提：有明确的落后/高风险指标，且回追被对手读到。
- Lockdown：8 人 FFA 里领先者与落后者差距大，且"被集火"是自然回追；劲已有"受击"来源吗（简报未列）——**未知，需确认**。
- 失败模式：奖励被刷（故意受伤）；或使领先者很难被追上。

### P10. 强化应改变技能"形状/角色"，不能只乘数值
- 证据：Karma 的 Mantra 以"截然不同的方式"强化 [C]；Udyr：把 R 的力量转为功能，让选择"不只是一道复杂数学题"；觉醒按**等级**而非姿态等级成长，一点投入即可有效 [A]；Brawl Stars Hypercharge 改变 Super 效果，同时固定基础增益 [A]。
- 前提：基础技能已具备足够的"可被修改的变化点"。
- Lockdown：分支 + 构筑卡给出大量变化点，满足。
- 失败模式：R = "+X% 伤害"，最优构筑退化成数学题。

### P11. 门槛与成本要有可校准的"赌注"，不只是冷却
- 证据：Riot："法力使我们能校准每个技能的赌注，而不需要大量额外规则；冷却只能推到有限程度" [A]；SF6 Burnout 为"用光量表"设置明确后果 [A]。
- 前提：资源条的消耗与失败成本被玩家理解。
- Lockdown：Q/W/E 同时有冷却与 25–35 劲消耗，两个门叠加；若 R 也消耗劲，则三个门叠加。
- 失败模式：叠加后玩家无法判断"现在为什么按不了"（可读性问题）。

### P12. 频率目标要写成"每局几次/每场战斗几次"，并随局长校准
- 证据：Brawl Stars："一场比赛只能触发几次" [B]；Overwatch 团战 12–20 秒，ult 是决定性节奏 [C]；Blizzard 多次调高成本，并检查"哪些英雄放大招过多或过少" [B]；Dawson 说"有些英雄太快得到最强技能" [B]。
- 前提：有目标节奏；FFA 局长与 8 人的战斗频率可预测。
- Lockdown：未知（简报没有目标 R 频率）。
- 失败模式：没有目标就只能事后调整成本，容易陷入 Overwatch 式反复调高成本的循环。

---

## 7. 对 PO 已删除内容的一句话旁证 [设计推断]

- 通用"按住 0.35 秒"与"通用完美闪避"属于高执行力输入。外部证据支持把强化放在**决策层而非执行层**：Strive 删除通用技巧使各层玩家站在同一起跑线 [B]；SF6 的 Drive Parry 完美弹反是**可选的高阶增益**，其核心是量表决策 [A]。这与产品负责人的方向一致，但不构成对此次删除的"外部证明"。

## 8. 缺口与未核实项

- 未找到 Ishiwatari 关于 Strive Burst / Roman Cancel 的专门设计原因的原文；只有泛指的"去掉常见技巧"（Famitsu 译文）与 Developer's Backyard 的 UI 说明。
- 未能读取 Riot 对 Karma、Rengar、Renekton 的一手设计文章；机制均出自 LoL wiki [C]。Karma 的"死键"措辞与具体时间未核实。
- League wiki 的 Karma/Development、Gnar/Development 页面直接抓取受限（含大量导航内容或被拦截），内容来自搜索摘要。
- Battlerite wiki 直接抓取 403；EX/能量数值为搜索摘要。未找到 Stunlock 对 Arena 中引入 EX 的一手设计说明。
- Overwatch：一手来源只有 Blizzard 的 Director's Take（Season 8）；ult 成本上调的具体日期、被动充能数值、官方"团战节奏"评论未核实；2024-04 补丁摘要中的"15% 提高到 49%"数值未读全文。
- Negative Penalty 的 Strive 数值未核实；Dustloop 是 Xrd 页面。
- Smash 与 Deadlock 未收录（无可靠来源）。
- 未做实机验证；P3 的数值估算只依据简报的数值，没有读取 Lockdown 数据表。

## 9. 来源列表

| 编号 | 来源 | 日期 | URL | 等级 |
|---|---|---|---|---|
| S1 | Ask Riot: Manaless Champions | 2021-07-02 | https://www.leagueoflegends.com/en-us/news/dev/ask-riot-manaless-champions/ | A |
| S2 | Udyr VGU: Rebuilding the Spirit Walker | 2022-08-23 | https://www.leagueoflegends.com/en-gb/news/dev/udyr-vgu-rebuilding-the-spirit-walker/ | A |
| S3 | An Udyr for Every Occasion | 2022-08-24 | https://www.leagueoflegends.com/en-gb/news/dev/an-udyr-for-every-occasion/ | A |
| S4 | LoL Wiki：Gnar / Karma / Rengar / Renekton / Udyr | 持续更新 | https://wiki.leagueoflegends.com/en-us/ | C |
| S5 | Battlerite Royale FAQ | 2018-09-05 | https://blog.stunlock.com/ru/battlerite-royale-faq/ | A |
| S6 | Battlerite wiki：Abilities / Energy | 无 | https://battlerite.fandom.com/wiki/Abilities | C |
| S7 | Director's Take – Balance Preview for Season 8 | 2023-11-30 | https://news.blizzard.com/en-us/article/24034079/directors-take-balance-preview-for-season-8 | A |
| S8 | Charlie INTEL：OW2 Season 8 改动（含开发者评论） | 2023-12-05 | https://www.charlieintel.com/overwatch-2/all-hero-buffs-nerfs-in-overwatch-2-season-8-balance-changes-explained-290238/ | B |
| S9 | Dexerto：OW2 ult 充能重做计划 | 2023-01-26 | https://www.dexerto.com/overwatch/overwatch-2-devs-reveal-plans-to-revamp-ultimate-charge-for-all-heroes-2044471/ | B |
| S10 | PCGamesN：Overwatch GDC 2016 "play by sound" | 2016-03-18 | https://www.pcgamesn.com/overwatch/overwatch-devs-on-creating-a-game-you-can-play-by-sound-and-announcing-dolby-atmos-support | B |
| S11 | Streetfighter.com：Fighting Ground | 无 | https://www.streetfighter.com/6/mode/fightingground | A |
| S12 | EventHub：Nakayama / Famitsu | 2024-06-06 | https://www.eventhubs.com/news/2024/jun/06/sf6-director-drive-system-expression/ | B |
| S13 | EventHub：SF6 Parry / Drive 起源 | 2022-06-19 | https://www.eventhubs.com/news/2022/jun/19/sf6-developers-parry-focus-attacks/ | B |
| S14 | GGST Battle System | 无 | https://www.guiltygear.com/ggst/en/battle/systems/ | A |
| S15 | Developer's Backyard Vol.1 | 2020-06-01 | https://www.arcsystemworks.jp/guiltygear/db/en/volume1.php | A |
| S16 | Developer's Backyard Vol.2 | 2020-07-03 | https://www.arcsystemworks.jp/guiltygear/db/en/volume2.php | A |
| S17 | Siliconera：Famitsu 对 Ishiwatari / Katano 访谈 | 2021-06-09 | https://www.siliconera.com/arc-system-works-explains-changes-made-to-guilty-gear-strive/ | B |
| S18 | Dustloop：GGXRD/Gauges | 更新 2024-05-29 | https://www.dustloop.com/wiki/index.php?title=GGXRD/Gauges | C |
| S19 | Supercell Support：Hypercharges | 无 | https://support.supercell.com/brawl-stars/en/articles/hypercharge-5.html | A |
| S20 | Pocket Gamer：Frank Keienburg 访谈 | 2023-09-20 | https://www.pocketgamer.com/brawl-stars/frank-keineburg-interview-hypercharge-update/ | B |
| S21 | Liquipedia：Aghanim's Shard；Dota 2 wiki 7.30 | 持续更新 | https://liquipedia.net/dota2/Aghanim%27s_Shard | C |
| S22 | Guild Order / Boosteria：OW2 ult 经济指南 | 2026-02-24（Boosteria） | https://guildorder.com/games/overwatch/wiki/ult-economy-and-fight-sequencing | C |

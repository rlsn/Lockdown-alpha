# 外部玩法基准研究：十款对标游戏与 Lockdown 的结构对照

范围：Battlerite、League of Legends: Arena、Eternal Return、Supervive、Hades、Noita、Backpack Battles、Brotato、Slay the Spire、Dota 2。
对照对象：[App Spec](../../APP_SPEC.md) 与 [内容与数值](../../demo/CONTENT_AND_BALANCE.md) 所写的 v0.14.2 规则，以及 `SourceData/Demo/content.json` 的卡牌统计。
本文只做外部证据与结构对照，**不提出、不批准任何具体数值**；所有"可迁移原则"都只写方向与可检验的边界，数值要走"先提方案、经批准、单点维护"流程。

## 0. 阅读方式

### 0.1 证据分级

| 级别 | 含义 | 用法 |
| --- | --- | --- |
| A | 开发者一手：官方博客、补丁说明、GDC 页面或幻灯片、开发者署名访谈 | 可作结论依据 |
| B | 权威媒体对开发者的访谈整理 | 可作结论依据，需注明出处 |
| C | 社区 Wiki、第三方分析、媒体转述 | 只作交叉印证，结论必须另有 A/B 支撑，否则明确标"未证实" |

正文里的 `[BR1]` 等编号对应 §13 来源索引（标题、发布者、日期、URL、抓取状态）。
"推断"指本文根据证据与 Lockdown 规格做出的判断，不是来源原话。

### 0.2 任务前提核查（有几处与来源不符，先说清）

| 任务里的说法 | 核查结果 |
| --- | --- |
| LoL Arena 因"趋同"移除了 Trait/set 系统 | **来源是 ARAM: Mayhem，不是 Arena。** Riot 官方文章标题为 `/dev: Augmentmaxxing ARAM: Mayhem`（Riot Phlox，2026-05-27）[LA4]。Inven 的报道把它写成 Arena 更新 [LA5]，属于二手误归。本文只把 [LA4] 当作"Riot 如何描述 augment 趋同与 build 平衡三原则"的证据，不当作 Arena 的改动史 |
| Riot 因"过于侵入"移除/改造 Arena 的 cameo | **只有一半有一手证据。** [LA2]（官方，2024-04）明说 cameo 作为"涌现玩法"是"hit or miss"，玩家"无能动性"时尤其令人沮丧。2025 年 Arena 回归时 cameo 被 Guests of Honor 取代，这一事实目前只有 C 级来源 [LA6][LA7]，官方 `/dev` 原文没抓到（`dev-arena-returns` 与 `dev-arena-act-2` 返回 404，MobaFire 返回 403）。2026 年 [LA3] 只说"为让新 guest 都能出场，暂时停用旧 guest"，**不是**因为侵入 |
| Hades 的"神池锁定到 4 位神" | **一手来源与 Wiki 都没找到这条规则。** 可证实的结构约束是：三选一、Duo/Legendary 前置条件、重抽权限被 Mirror 管理 [HD2][HD3][HD4][HD5]。本文不引用"锁定 4 神" |
| Supervive 的"动态终局圈目标" | 只证实了圈大小调整、"最后存活队伍"、Storm Shift 与 Final circle 搜索半径等 [SV5][SV7]，**未找到**"动态圈目标"的一手设计说明 |
| Dota 2 "everything is OP in a distinct dimension" | **没找到 Valve/IceFrog 的一手原话。** 搜索到的只有第三方概括 [DT4]。本文把它当作社区启发式，只用补丁说明证明 Valve 的实际调整行为 [DT1][DT2] |
| Brotato 的"tag 偏置商店" | **只对武器证实**：拥有同名武器、拥有同类（class）武器的出现概率有加成且有上限 [BT1]。物品层面的 tag 偏置未找到一手说明 |
| Brotato "负属性机会成本" | 没找到设计者专门陈述，只有补丁里把负属性当调参杆的例子（如 Excalibur 负护甲 -3→-2）[BT5] |
| Battlerite "官方 postmortem" | **没有找到正式 postmortem。** 可用的是停止开发公告 [BR1]、Big Patch 与 2.0.2 说明 [BR2][BR3]、总监访谈 [BR4] |
| Backpack Battles "转售流动性" | 没找到设计者对转售率的陈述；只有补丁级证据（回收类道具、ALT 显示门控物、商店稀有度可见）[BB1][BB3][BB4] |
| Noita GDC 演讲内容 | 只读到 GDC Vault 摘要页 [NO1]，**没读到演讲正文或字幕**；关于涌现与设计困难的引文来自署名访谈 [NO2] |

## 1. Lockdown 基线：与外部问题对应的八个结构点

下面八点是后文反复引用的"Lockdown 现状"，每点都链接到当前规则的出处。

| 编号 | 结构点 | 出处与事实 |
| --- | --- | --- |
| S1 | **发育路线可解** | 34 处营地固定档位，同档整营 XP 相同，三种野怪只改行为不改价值；清空后本段为空；野怪经验全归最后一击（[内容 §6.1、§6.2](../../demo/CONTENT_AND_BALANCE.md#61-经验与击杀)）。局部打散：分散入场、标记营地（T=11/171/331）、空投（[Spec §2.1、§5](../../APP_SPEC.md#21-阶段节奏今日热点与野外事件)） |
| S2 | **终结者收益** | 击杀者拿全额折后 XP，每段前 2 次击杀各得牌三选一；每名助攻者另得击杀 XP 的一半与 1 属性点，**不稀释击杀者**（[内容 §6.1](../../demo/CONTENT_AND_BALANCE.md#61-经验与击杀)）。没有"首攻方保护"，没有按伤害占比分账 |
| S3 | **属性点双用途** | 未分配属性点既是五维成长，也是商店货币；单维 18 点解锁关键石（[Spec §4、§4.5](../../APP_SPEC.md#4-经验与成长)） |
| S4 | **隐藏词汇加权** | 牌抽取对"共享词汇"加权：基础规则 ×1.2、共享 ≥2 词封顶 ×1.8；商店 4 个牌位中 2 个按词汇加权；"界面不显示词汇"（[内容 §4.4、商店表](../../demo/CONTENT_AND_BALANCE.md#44-构筑牌与参数)） |
| S5 | **高上限靠出现率与反制，无自限机制** | 同卡多份先相加、异卡相乘，乘积只有技术上限 ×1000；吸血不设上限；规格明写"靠低出现率与重伤/爆发反制平衡"（[内容 §4.5](../../demo/CONTENT_AND_BALANCE.md#45-关键词状态与共用战斗规则)、[Spec §9](../../APP_SPEC.md#9-内容与数值设计边界)）。规格与内容手册**没有记录**对施放者本人的自伤或空间自限（未核对 SourceData，需确认） |
| S6 | **领先者双向反馈** | "上热搜"（L≥3 等级唯一最高者）存活每满 60 秒得 1 属性点；击杀他得 150 XP、3 属性点、稀有/传说三选一（[内容 §4.6、§6.1](../../demo/CONTENT_AND_BALANCE.md#46-全员规则与地图事件)） |
| S7 | **公开事件汇聚第三方** | 空投提前 15 秒公布落点；标记营地公开；热搜圈公开（模糊圆）；迷雾外玩家不上小地图（[Spec §2.1、§5、§7](../../APP_SPEC.md#5-地图野怪与视野)） |
| S8 | **单次终局** | 150 秒封街终局围绕一根旗杆，8 项终局改装只改胜负规则，不改构筑数值（[Spec §2.2](../../APP_SPEC.md#22-喷漆守护与终局胜负裁决)、[内容 §4.7](../../demo/CONTENT_AND_BALANCE.md#47-终局改装)） |

另有三项统计事实来自 `content.json`：`cards` 共 94 项（89 张构筑牌 + 5 枚元素勋章）；按宿主形态为本体 56、技能 20、冲刺 13、勋章 5；按类型为 trigger 27、modify 21、convert 20、grow 13、element 13；30 个词汇里 `basic_hit` 出现在 18 张牌上，居首。
内容手册 §4.7 的"94 项 cards"与 Spec §4.4 的"89 张"是同一份数据的两种口径（89 + 5 = 94），不是矛盾。

## 2. 速查矩阵

| 游戏 | 它真正解决/没解决的问题 | Lockdown 同构程度 | 最该迁移 | 最不能抄 |
| --- | --- | --- | --- | --- |
| Battlerite | 技能表达可读；回合间重复与残局拖延；最终因留存不足停更 | 部分同构（S8、S2） | 反制来自走位与可读前摇；一次性、随阶段增长的配装 | 靠"队友死亡"解锁的 Overload；降血加速的全局重调 |
| LoL Arena | 防滚雪球、造"可期待的破格 build"、防最优解 | 部分同构（S4、S5） | 破格物品"只能被给予"；允许换/升 build 以便转型 | 随机单点伤害 cameo；等价装备（取消发育）；凑套装才生效的 Trait |
| Eternal Return | 路线固化成新手隐形门槛；ABC 第三方；合成树过重 | **强同构**（S1、S2、S7） | 缩小路线效率差而非消灭路线；首攻方保护的思想；信息收紧 | 队伍制 Enemy Kill Effects；倒地/AED |
| Supervive | 新手"不知道该干嘛"；等级上限与永久农；击倒奖励；TTK | **强同构**（S1、S2、S6） | 软上限；奖励在"成功瞬间"发放；逐受害者递减；可读且有界的战斗重置 | 终结者治疗重置（奖励第三方）；局外 Armory；队伍复活规则 |
| Hades | 每个 build 可见地改写操作；前置条件与重抽权限 | 部分同构（S4） | 宿主槽身份；Duo/Legendary 靠前置而非概率；重抽有权限而非无限 | 局外永久加强；单人尺度的 Legendary 数值 |
| Noita | 系统涌现；顺序/乘法产生故事；自限抑制破格 | 部分同构（S5，**自限缺失**） | 把"高上限"绑定可感知的自我风险或空间约束 | 无对手视角的"全随意组合" |
| Backpack Battles | 协同可读；早期 RNG 失手后可转型；商店流动性 | 部分同构（S3、S4） | 显示关系而非隐藏加权；单一回收阀；撤销操作 | 自动战斗；全额转售 |
| Brotato | 单屏取舍；负属性成本；软偏置商店；经济 vs 战斗 | **强同构**（S3、S4） | 有上限且可见的偏置；负向选择（ban）；经济作为 build 轴 | 波次通胀；被动收入式"龟缩" |
| Slay the Spire | 跳过/精简；精英风险回报；遗物与牌的催化 | 部分同构（S3、S4） | 风险在承诺前可读；跳过要有真实价值；催化型设计 | "单人游戏所以破格无所谓"的前提 |
| Dota 2 | 非线性强度；反制生态；追赶不惩罚早期进攻 | 部分同构（S2、S6） | 追赶按受害者自身价值计；奖励路由给落后者 | 金币净值体系；连杀大额赏金 |

## 3. Battlerite

主要来源：[BR1] The Future of Battlerite（Stunlock Studios，2019-07-03）；[BR2] Dev Update: The Big Patch™（Stunlock，2019-02-06）；[BR3] Battlerite Arena Patch 2.0.2（Stunlock，2019-03）；[BR4] Game Design Deep Dive（Peter Ilves，Game Developer，2016-10-27）。

### 3.1 它解决了什么，没解决什么

- **解决：战斗里的技能表达。** 总监说明 Battlerite 用 WASD 控制，每个攻击单独设置移动减速、加减速计时与收招时间，让战斗"流畅"而不是 Bloodline Champions 的"断续"[BR4]。2019 年 Big Patch 进一步提高移速、降低全体生命，理由是"冷却依赖、难以走位躲避、移动慢"，并明说这会让更多生存来自"躲得开"[BR2]。（A）
- **没解决：回合间的陈旧与重复。** Big Patch 直言要解决"staleness, repetition, and the lack of meaningful strategic choices, especially from round to round"。旧系统每轮选一个 Battlerite，协同要"提前数轮规划"，强力项被延后开放，结果"选项比看起来少得多"，于是改为 5 个 rite 的预选加每轮可重配的 Armory（带入物品数随轮次 +1，上限 3，且不持久）[BR2]。（A）
- **残局拖延：** 回合常以 1v2、1v3 靠伤害优势慢慢磨死结束；Death Vortex 终局 1v1 最有回报但很少发生。Overload 用"队友死亡后解锁、8 秒内加速、加伤、加治疗，同时承受更多伤害"制造波动 [BR2]。2.0.2 又因"过度强调死亡后的局面、削弱死亡之前的重要性"整体下调 Overload 的伤害与急速 [BR3]。（A）
- **最终结局：** 停更公告称"留存一直是最大挑战"，对近期玩法改动与营销"收益递减"；Battle Season 3 起 Armory 与 Overload 从 Casual 和 League 移除（保留在私人对局）[BR1]。（A）**这不是设计 postmortem，不能当作"变异性导致留存差"的因果证明。**

### 3.2 Lockdown 是否有同样的结构问题

**部分有。**

- 技能表达层：Lockdown 有快速施法、向量施法、预输入、0.12 秒精准闪避和硬控递减（[Spec §3](../../APP_SPEC.md#3-操作与战斗)），反制主要来自操作而非冷却，方向与 Battlerite 一致。但"PvP 前摇是否可读"没有证据，需实机检查（无法判定）。
- 回合间重复：Lockdown 是三段野外加一次终局，不存在"每轮重复"，但 S8 说明决胜只发生在同一地点、同一目标上；变化只来自 8 项改装和构筑。**终局的重复性风险比回合制更集中**（推断）。
- 残局：Lockdown 终局已有缩圈、复活入队上限、棘轮下限（[Spec §2.2](../../APP_SPEC.md#22-喷漆守护与终局胜负裁决)），对应 Vortex 与抗拖延。但**终局击杀反而是滚雪球**（回血 35%、劲 +50、守护 +4 秒），与 Overload 的"给落后者翻盘窗口"方向相反（推断）。

### 3.3 可干净迁移的原则

1. **反制来自可见前摇与走位，而不是冷却与随机。** 任何新增 PvP 效果须有玩家可读的触发信号和可走位躲开的窗口（原则，非数值）。
2. **可重配、不持久、随阶段增加的"一次性装备"。** Armory 的形态适合休息室：Lockdown 的休息配装与终局改装已经接近，缺的是"针对已公开信息调整"的理由。
3. **局终必须有决定性收口。** Vortex 的价值不在伤害，而在强制对决。Lockdown 的缩圈与旗杆已承担，评估重点是"棘轮是否真的消除了 20 秒守护拖延"。

### 3.4 不能抄什么

- **"队友死亡解锁"的 Overload。** FFA 没有队友，类比成"落后者/被淘汰者暴走"会奖励自杀和送头；规格已把"送人头"列为自由混战的行为风险（[内容 §6.1](../../demo/CONTENT_AND_BALANCE.md#61-经验与击杀)）。Stunlock 自己也承认它让"死亡之前"变得不重要 [BR3]，并最终撤出主要队列 [BR1]。
- **全局降血加速。** 它会同时改变 TTK、近远程平衡与命中率 [BR2]；Lockdown 是 8 人混战加野怪，这种重调会放大第三方击杀窗口（见 §6 Supervive 的 TTK 教训）。
- **纯 PvP、无成长的设计前提。** Lockdown 的核心是发育与构筑，不能用 Battlerite 的"全员等强"来衡量。

### 3.5 二阶副作用与边界

| 副作用 | 边界 |
| --- | --- |
| 反制窗口变多 → 单场战斗更长 → 第三方窗口更长 | 任何"拉长战斗"的改动必须同时有"首攻方保护/信息收紧"（见 §5 Eternal Return） |
| 休息室再增加"配装税" → 属性点既是成长又是货币，配装消耗直接压缩关键石进度（S3） | 配装不得额外消耗未分配点；洗点与换装的代价只能是时间，且只在休息阶段 |
| 终局收口越强 → 单一终局越"成败一瞬" | 给终局改装以外的、可读的"形势信号"（如谁在守护），不增加随机伤害 |

## 4. League of Legends: Arena

主要来源：[LA1] /dev: Arena（Riot Maxw3ll，2023-06-27）；[LA2] /dev: Arena Round Three（Riot Cadmus，2024-04-15）；[LA3] /dev: Leveling Up Arena（Riot，2026-04-14）；[LA4] /dev: Augmentmaxxing ARAM: Mayhem（Riot Phlox，2026-05-27，**注意是 Mayhem**）；二手：[LA5][LA6][LA7]。

### 4.1 它解决了什么，没解决什么

- **防滚雪球：** 所有装备被调成同价 3000 金币，"没有零件、没有三把长剑回合，只有大件"。理由是 League 的"早期支配"在 Arena 里没有"别处"可去，"从大劣势里打架并不总是好玩"[LA1]。（A）
- **防最优解：** Runes 被整体砍掉，因为"所有人都点 Conqueror 并立刻叠满"[LA1]。Prismatic 装备**不可直接购买**，只能在特定回合像 augment 一样被"呈现"，原因是承认"显然总有数学上最优 build"，所以让"你和对手不能稳定地做同一个 build"[LA2]。（A）
- **鼓励破格上限：** Augment 设计成"远超传统 League 的增幅"，冷却"非常低"、技能"会暴击"，设计者描述测试里一个自动 augment 冷却降到 4 秒，形成随机闪现隐身的"忍者幻想"，是"很久以来笑得最厉害的一次测试"[LA1]。（A）
- **没解决：** [LA2] 自陈问题：高段位 build 与阵容变陈旧；某些策略"过于可靠且缺乏互动"；cameo 作为涌现玩法"hit or miss"，玩家无能动性时最令人沮丧；同一局里反复遇到同一队伍很无聊。Arena 因此被视为"early access"迭代。（A）
- **转型能力（2026）：** [LA3] 新增 augment 升级、满槽后"升级或替换"一个 augment、第 8 回合"合成回合"（加一个槽或移除一个 augment 以升级另一个），目的明说是"让玩家从不再有用的 augment 上转型，或随局势调整"。（A）
- **追赶机制：** 圆形场地中心的 Bulwark Blossom 在"被围攻时"给更多治疗与护盾，用来"减轻落后的打击"[LA3]。（A）
- **反拖延与场地：** 原版的收缩火圈与"不遮挡视野的深水"，目的是让玩家"感觉在和对方战斗，而不是和地图战斗"[LA1]。（A）
- **同一框架下的平衡三原则**（Mayhem 文章）：每个英雄都应有可追求的强力 build；没有英雄总能达成强力 build；强力 build 不应是胜负的唯一决定因素。Riot 同时说明"避免每次都削最强的"[LA4]。（A，但对象是 Mayhem）
- **Trait 的四个副作用**（Mayhem）：build 趋同、整局体验趋同、Trait 压过英雄身份、单个 augment 被稀释；"凑不齐第二件而没有 Trait 加成"令人沮丧 [LA4]。（A）

### 4.2 Lockdown 是否有同样的结构问题

**有，且比 Arena 更难压。**

- **S4 就是 Trait 的软版本。** 词汇加权让"已有主题的牌更容易再出现"，天然引向趋同；"界面不显示词汇"又让玩家看不到为什么出现这些牌。同卡份数叠加、异卡相乘（S5）则是 Mayhem 描述的"破格上限"。
- **Arena 的防滚雪球机制 Lockdown 不能用。** Arena 每回合重置血量与金币，Lockdown 的发育与击杀是持久的，且规格明写要"滚雪球组装破格流派"（[Spec §1](../../APP_SPEC.md#1-产品目标与范围)）。一个破格 build 在 FFA 中可以连续击杀 7 个对手而不被回合重置打断（推断）。
- **"不可直接购买的破格物"已被部分破坏：** 商店可用 4 点直接买传说牌，且 4 个牌位里 2 个按词汇加权，刷新首刷免费、之后逐次 +1 点（[Spec §4.5](../../APP_SPEC.md#45-休息商店)）。这等于把"稀有度权重 6"通过点数与刷新换成了"可买的可靠性"，与 [LA2] 的 Prismatic 设计原则相反（推断）。

### 4.3 可干净迁移的原则

1. **把"破格上限"与"可预期性"拆开：** 让破格靠被呈现而不是被购买，至少对最高稀有度如此。
2. **转型权限：** [LA3] 的"替换/升级/合成回合"说明 Riot 把"不让玩家被 RNG 锁死"当作与"保留上限"同等重要。Lockdown 已有换技能退点与休息洗点，应保持转型代价为时间而不是隐性惩罚。
3. **追赶按局势触发而不是按排名：** Bulwark Blossom 以"被围攻"为触发。FFA 里可映射为"最近窗口内承受多源伤害者"，但要先解决被刷（见 4.5）。
4. **Mayhem 三原则可直接当验收口径：** 每个构筑都有目标、没有构筑稳定拿到、强构筑不独自决定胜负。

### 4.4 不能抄什么

- **随机单点伤害类 cameo。** Lux 扫场、Sett 受伤后出拳等对"当时在被命中的人"无能动性 [LA1][LA2]。在持久血量、击杀给 XP 与牌的 Lockdown 中，随机击杀会变成无理由的奖励分配。
- **等价装备（3000 金币一件）。** 它直接取消"发育差异"，与 Lockdown 的 34 营地与 XP 阶梯相冲突。
- **凑套装才生效的 Trait。** [LA4] 自己承认这既趋同又令人沮丧；Lockdown 若把词汇加权升级成"凑齐 N 个词汇给额外效果"会重蹈覆辙。
- **早退出与固定 2v2 回合。** 这些依赖队列与回合重置，不适用于 160 秒野外段。

### 4.5 二阶副作用与边界

| 副作用 | 边界 |
| --- | --- |
| 让传说牌"只能被给予"→ 商店的稀有货架失去目的；玩家对"点数买到想要的牌"的预期受挫 | 先用 pick/win 数据确认传说牌是否被高频购买；若是，才考虑把传说牌移出可购买池。这是产品决策，需用户拍板 |
| 追赶按"被围攻"触发 → 可被两名玩家串通刷（互相拉仇、互相喂血） | 触发需要"多个独立来源"且按个人冷却，奖励不可转化为属性点，仅限生存类 |
| 转型更自由 → 构筑"承诺"的意义下降，最终全员"全能" | 保持牌容量格上限、同名叠加的鼓励；转型仍然要付休息室时间 |
| 把词汇显示出来可缓解"黑箱"，但也会放大趋同 | 只显示"适配关系"，不显示排名分，且保留至少一半牌位不受加权（见 §10 Brotato） |

## 5. Eternal Return

主要来源：[ER1] Season 3 Dev Stream Overview（Eternal Return 官方，2024-02-07，卡片新闻）；[ER2] 1.0.0 Gameplay（官方补丁说明，页面图片路径为 2023-07-19，页面标注"3 years ago"）；[ER3] PATCH NOTES 13.1（官方，2026-11-19）。

### 5.1 它解决了什么，没解决什么

- **路线是"隐形的入门门槛"：** 官方原文：对新手，"记住 routine 会形成隐性入门门槛"；对老玩家，偏离 routine 会导致"遥远的胜利方案"。开发者曾刻意把熊和狼放在地图外沿以分隔"目标"与"野怪路线"，结果被玩家优化到"为了抢目标就必须沿着刷怪路线走，反之亦然"。（A）[ER1]
- **官方的处理方式不是消灭路线，而是缩小效率差：** 重新摆放熊、狼，增加 8 种野怪群，降低单只野怪的个人 XP，使"最优路线与非最优路线的差距明显变小，路线熟练者的优势会减弱"；原话是"不打算完全消灭 routine"。（A）[ER1]
- **简化合成与掌握：** 起始物品改动、胸甲/腿甲到史诗级都变为基础物品的升级、删除大量普通/高级材料；材料数量"相比 Early Access 几乎减半"；材料集中使"Saved Plans"更短。（A）[ER1]。1.0 去掉 HP/陷阱掌握，改变制作与搜索掌握的效果，武器技能自动升级，并对与最高掌握者差距过大的玩家给经验加成（差 1–5 为 1 倍，6–10 为 1.2 倍，11–20 为 1.5 倍）。（A）[ER2]。13.1 把每个区域的重复任务合并为"通用 3 个"，并下调重复任务的 Credits 奖励。（A）[ER3]
- **ABC 第三方（反第三方）：** Enemy Kill Effects（击杀者回复 HP/SP 并减少技能冷却）**不在倒地时触发，而在完全淘汰时触发，且归属于"先交战的队伍"**，官方原话目的是"保护 ABC 局面中先交战的一方，并为连续战斗创造条件"。（A）[ER1]
- **信息收紧：** 卫星雷达启用时让 Hyperloop（高速传送设施）失效 10 秒，因为雷达让玩家直接去抢"免费目标"，官方希望玩家"在目标出现前做决定，而不是看雷达被动反应"[ER2]；战斗 ping 几乎移除，只留猎杀 ping [ER1]。（A）
- **终局圈：** 限制区增多且不再自动重建，更易引发 ABC，于是放慢限制区指定速度、给倒地者 AED 等自救技能、降低后期复活 Credits，整局"增加半天"。（A）[ER1]
- **安全网：** Shards 被官方定位为"变形而非升级"，同一武器的三个版本"价值相等"，是给不适配技能的角色的安全网。（A）[ER1]

### 5.2 Lockdown 是否有同样的结构问题

**有，而且是最强同构。**

- **路线（S1）：** 营地位置、档位、整营 XP 都固定且公开，"三种野怪"只改行为不改价值（[内容 §6.2](../../demo/CONTENT_AND_BALANCE.md#62-四档营地与种类池)）。唯一打散路线的变量是：随机入场点、标记营地的随机抽取、空投落点。**路线效率是否收敛，需要真实对局数据，目前无法判定，但结构上具备收敛条件**（推断）。
- **第三方（S2）：** FFA 里每场战斗都是 ABC。击杀奖励（全额 XP、前 2 次击杀的牌、助攻另加）全部归最后一击，**与 ER 在加入 Enemy Kill Effects 前的结构相同**。同时助攻是"额外发放"，意味着围攻者越多，总 XP 越大（推断，见 5.5）。
- **信息（S7）：** 空投落点提前 15 秒公布、标记营地公开，都是 ER 所说"免费目标被被动反应抢走"的触发器；好在迷雾权威、小地图不显示迷雾内玩家。

### 5.3 可干净迁移的原则

1. **缩小而非消灭路线效率差。** 变量必须放在"价值"或"位置/时机"上，只改变行为的变种（S1 的现状）不会改变路线选择。
2. **奖励连续性属于先交战的一方。** ER 选择把 Enemy Kill Effects 归给"先交战的队伍"。Lockdown 对应的是"按伤害贡献与先后分账，而非最后一击全拿"，而不是增加更多终结奖励。
3. **信息收紧。** 公开信息应当是"粗粒度"的（Lockdown 的热搜圈已是模糊圆），精确且倒计时的公开更适合只给"到点就发生"的事件。
4. **一个货币、少一层树。** Lockdown 已只有属性点一种货币，无需再简化；但 ER 把任务奖励合并说明"同一目标的多重来源"会成为认知负担。

### 5.4 不能抄什么

- **队伍制 Enemy Kill Effects。** 它依赖"先交战的队伍"与"整队淘汰"两个概念，在 FFA 里"先交战的一方"会被第三个人打断，定义失效；并且"击杀回血 + 冷却缩减"本身是滚雪球。
- **倒地状态与 AED 自救。** 倒地让被击杀者继续贡献，但在 Lockdown 里会让终结变成"必须补刀两次"，延长战斗并增加第三方窗口。Lockdown 野外本来就无限复活，不需要这层。
- **"放慢限制区"。** Lockdown 终局只有 150 秒，放慢会直接拖长胜负。

### 5.5 二阶副作用与边界

| 副作用 | 边界 |
| --- | --- |
| 按伤害占比分账 → 鼓励"点一下就跑"（poke 刷占比） | 分账须要求持续接触窗口与最低占比，且总奖励池不随参与人数增加 |
| 助攻为"额外发放" → 围攻者越多总奖励越高，鼓励围猎最近的人 | 助攻改为"在固定奖励池内分账"，或设单个受害者的总奖励上限；规格明确要求任何递减或扣减必须有提示并经产品授权（[内容 §6.1](../../demo/CONTENT_AND_BALANCE.md#61-经验与击杀)），所以必须先提方案 |
| 缩小路线效率差 → 全员收益变平，发育"没有可优化性" | 只缩小"最优路线与普通路线"的差，不缩小"打 Elite 与打小野"的差；保留技能型玩家的可学习空间 |
| 信息收紧 → 单机/弱玩家更难读局 | 不得减少 HUD 零滚动约束下的必要信息；收紧的是"别人的位置"，不是"自己的状态" |

## 6. Supervive

主要来源（Steam 官方公告，Theorycraft Games）：[SV1] JAN 22 - Leveling Rework + Big Balance Patch Notes（2025-01-22）；[SV2] [MUST READ] FEB 5 PATCH - Gold MATTERS（2025-02-05）；[SV3] FEB 19 PATCH（2025-02-19）；[SV4] MARCH 5 PATCH（2025-03-05）；[SV5] THE REINVENTED SUPERVIVE 1.0 FEATURE RUNDOWN（2025-07-23）；[SV6] 1.02 PATCH NOTES（2025-08-20）；[SV7] 2.01 PATCH NOTES - Tournament Updates, TTK Changes（2025-11-05）；[SV8] Sunsetting SUPERVIVE - End of Service Announcement（2025-12-17）；二手：[SV9][SV10]。

### 6.1 它解决了什么，没解决什么

- **新手不知道该干什么：** [SV2] 官方原话：开放世界"早期非常自由，难以判断我做得对不对"，所以让金币重要，作为"几乎所有玩家都懂的进度基准"，并**重新校正**：上一版"向 PvE 农场摆得太狠"，于是增加击倒奖励（击倒奖励的经验提高，3 级以下击倒至少给一整级）。（A）
- **等级上限的来回：** [SV1] 去掉"每日等级上限"，最高等级 9→15，目的是"去掉规则感强的系统，建立更清晰的胜利路径"（"有疑问就去农场"）。[SV4] 仅六周后："我们把队伍之间的实力差推得太远了"，"在'农场感觉好'与'永久农场成为必须'之间要有平衡"，于是**软上限回归**：达到当日等级断点后，从野怪获得 20% 经验且不少于 1；此时**不再算作"分摊经验"对象**，低等级队友仍得全额；同时每级技能强度由 +10 降回 +5，装备升级收益回调。（A）
- **击倒经验与反血袋：** [SV1] 经验在"击倒"而不是"击杀"时发放；击倒高于自己等级的目标给大量经验，低于自己的给很少；对同一受害者的重复击倒每次递减 50%（"no blood bags pls"）。（A）
- **战斗重置（奖励主动进攻）：** [SV3] 击倒后给予治疗与冲刺冷却重置，治疗量按英雄的"决斗潜力"区分，目标是让玩家"能梦想以完美操作打 1vN"；**官方说几周前测试过"击杀完全重置"，"很多人不喜欢"**，并强调要保持"战斗可读"。[SV4] 又回收：多冲刺英雄只返还 1 层，个别英雄不再返还。（A）
- **致死性：** [SV6] 把"下砸"致死线放在约 70% 生命。[SV7] 官方原话："当致死性过高，战斗会变得平淡重复"，目标是"让战斗稍长，创造更多来回窗口"，并同时缩小起始圈约 10%，"让更多队伍有机会加入"。（A）
- **复活与反伏击：** [SV1] 第 1 天复活计时按"队伍使用次数"递增，以减少"无尽的送死涌入"；[SV5] 1.0 信标复活 6 秒，复活后 12 秒"复活病"（输出与受治疗 -25%）；传送到队友有 3 秒减速加沉默以防"传送伏击"。（A）
- **结局：** 2025-12 官方宣布 2026-02 结束运营 [SV8]。[SV9][SV10] 转述官方说法：改动"没有足够有效"，大量玩家下载后很快离开。（B/C）[SV5] 自己承认：开放测试时 90% 好评仍"不足以长期留住人"，并把 build 表现力不稳归因于"BR 战利品随机、死亡掉落、要现场合成"，由此做出局外 Armory。（A）

### 6.2 Lockdown 是否有同样的结构问题

**多处同构。**

- **等级与发育（S1）：** Lockdown 没有每日/每段等级上限，且规格明确"不保留总 XP 上限"（[内容 §6.1](../../demo/CONTENT_AND_BALANCE.md#61-经验与击杀)）。因此"永久农场"的约束来自**供给**：营地清空后本段保持为空，8 人共争（推断）。这与 Supervive 的"规则型软上限"是两种机制，必须分开评估。
- **终结者收益（S2）：** Lockdown 的击杀奖励给最后一击，同样可被第三方拿走。**若直接复制"击倒后治疗与冷却重置"，受益最大的是补刀者**（他本来血量最满）。这一点在 Supervive 里靠队伍、圈与复活规则缓冲，在 FFA 中没有对应缓冲（推断）。
- **领先者双向反馈（S6）：** 上热搜者存活期间持续得属性点，被击杀时击杀者得 150 XP、3 点和稀有三选一。Supervive 的对应物是"按等级差缩放的击倒经验"，且**逐受害者递减**；Lockdown 有等级差系数（40%–160%）但没有逐受害者递减，且规格限制"不能未经产品授权添加死亡惩罚或无提示的递减奖励"。
- **新手读局：** Lockdown 的公开定时事件相当于给"该去哪"提供提示，但这是否足够，目前没有新手数据，无法判定。

### 6.3 可干净迁移的原则

1. **软上限而不是硬上限，且"永不为零"。** [SV4] 的设计是"保留收益但降低，并且不让满级者拖累同伴的分摊"。若 Lockdown 需要限制"永久农场"，应在供给侧而不是新增规则。
2. **奖励在"成功的瞬间"发放，并随目标相对价值缩放。** 击倒即给 XP（而非等待击杀）与高等级目标更多，是"让落后者的第一次成功立刻有回报"。Lockdown 的等级差系数已有同向设计。
3. **逐受害者递减。** 防"血袋"。但这是产品级决策，必须有提示并获批准。
4. **战斗重置要小、可读、按角色调。** [SV3] 的"测试过完全重置、很多人不喜欢"是强证据：重置要保持小量、可读，并且 [SV4] 说明它仍需要按英雄逐个回收。
5. **TTK 是调节旋钮，不是目标。** [SV7] 的结论是"过高致死性使战斗平淡"，应以"来回窗口数"作为验收指标，而不是单纯拉长。

### 6.4 不能抄什么

- **击杀后治疗/冷却重置（直接复制）。** 在 FFA 里它主要奖励"在别人互殴后补刀的人"，而不是"主动发起者"。若要做，必须与"首攻方保护"一起设计（见 §5）。
- **局外 Armory。** [SV5] 的 Armory 是赛季性的局外收藏，而 Lockdown 明确"无局外养成/账号货币"（[Spec §1](../../APP_SPEC.md#1-产品目标与范围)）。其教训只取"build 表现力要可靠、不能靠随机掉落"，不取形式。
- **队伍制复活与信标。** 第 1 天按队伍计数的复活计时、Beacon 6 秒复活、复活病都是针对队伍与死亡掉落设计的。
- **每日硬等级上限。** [SV1] 与 [SV4] 的来回已经证明硬上限会引发"规则感"与"必须永久农场"两种极端。

### 6.5 二阶副作用与边界

| 副作用 | 边界 |
| --- | --- |
| 击倒即发 XP → 被击倒者之后被"补刀确认"的价值下降，可能鼓励"打到半死就走" | 保留补刀的小额奖励，但总额不能超过单人击杀奖励；用"同一受害者窗口内只结算一次"防重复 |
| 逐受害者递减 → 被围攻者变成"低价值目标"，反而更安全（"被打到没价值"） | 递减只作用于 XP，不作用于属性点与牌；并用 HUD 明示（零滚动约束下用图标） |
| 战斗重置 → 落败方也能借"击杀后的补满"反杀 | 重置只对发起者归因的击杀生效；任何冷却返还要以"该英雄的受益与风险"逐个检查，而不是统一系数 |
| 把 TTK 拉长 → 第三方窗口更长 | 同步要求"首攻方保护"与"信息收紧"才允许拉长；否则以"来回窗口数"为准而非时长 |

## 7. Hades

主要来源：[HD1] Supergiant's fourth outing Hades introduces a more mature, organized dev process（Greg Kasavin，Game Developer / GDC Twitch，2019-01-17）；[HD2] The Blood Price Update - Patch Notes（2020-06-23）；[HD3] Early Access Patch 042 -- v0.27008（2020-03-17）；[HD4] v1.0 Launch Update - Patch Notes（2020-09-17）。C 级：[HD5] Hades Wiki（Boons）、[HD6] Game Design Lab（Hades systems）。

### 7.1 它解决了什么

- **设计意图（Kasavin，A/B）：** 偏好"让你实验、给你试错余地"的设计，"当你找到一套看似完美的组合时，也许会被推到一套更好或更有趣的新工具组合里"[HD1]。
- **槽位身份（C 为主）：** 对 Zagreus 有 Attack、Special、Cast、Dash、Call 五个专属槽，一个槽位一次只能有一个"槽位 boon"，同神再次提供同槽位时只升稀有度 [HD6]；Duo 需要两位神各至少一个前置 boon，Legendary 需要特定前置 [HD5]。这两点的结构说法来自 C 级来源，**不是 Supergiant 原话**，但与补丁说明（前置条件被反复调整）的行为一致。
- **前置条件是一手可证的调节手段：** EA 042 把 Sunken Treasure、Fully Loaded、Winter's Harvest 的前置条件加重，理由是"不该在局内这么早被提供"[HD3]。Blood Price 为所有神对增加 Duo，同时"略微降低了奥林匹斯诸神提供 Duo 的基础概率（因为现在多了很多）"[HD2]。
- **重抽权限被 Mirror 管理：** v1.0 把 Fated Authority 的最大等级下调、成本"显著降低"，Fated Persuasion 成本"显著降低"，EA 042 保证使用 Fated Persuasion 后"至少有一个新选项"[HD3][HD4]。（A，但补丁没有解释每个能力的具体含义，本文不展开）

### 7.2 Lockdown 是否有同样的结构

**部分有，且 Lockdown 已有 Hades 式宿主槽。** 构筑环按 `Q/W/E/D` 与本体五个宿主排列并有固定牌槽（[Spec §7](../../APP_SPEC.md#7-游戏界面与反馈)），24 个主动、各带三级分支。`content.json` 中的牌形态分布是本体 56、技能 20、冲刺 13、勋章 5，意味着**大部分构筑牌作用在本体（普攻）上**。抽样看 `modify`/`convert` 类牌：`card_long_arm`（普攻变远程弹体）、`card_wide_swing`（三段扫击）确实是"可见改写"，`card_muscle_memory`（每 4 次命中 +2 AD、+1% 攻速）接近"看不见的被动百分比"。因此**一部分牌满足 Hades 的"改变基础操作"标准，另一部分不满足**，比例需要逐张核查（无法一次判定）。

- **前置条件 vs 概率权重：** Hades 用"前置 boon"把强牌门控在"你已经选了什么"，Lockdown 用"drop_weight × 词汇权重"的概率门控（S4）。两者都能做出"组合才出现"，但 Hades 的做法让玩家**能计划**，Lockdown 的做法让玩家**只能碰运气**（推断）。
- **重抽：** Lockdown 有开局 3 次、每次休息 1 次的免费牌重抽，商店刷新首刷免费、之后逐次 +1 点，跳过牌组 +1 点。这是"有限权限"的形态，与 Hades 一致。

### 7.3 可干净迁移的原则

1. **用"前置"而不是"更高概率"表达组合。** 前置条件对玩家可见、可规划（推断）。Lockdown 若要降低"碰运气"，应优先考虑"已装某牌后才可能出现"这种门控，而不是加大词汇权重。
2. **宿主槽身份。** 每次抽牌都应该有"会改变我哪个操作"的回答；纯数值卡应并入已有卡或降级为商店补位。
3. **重抽是权限，而不是货币的无限转换。** 免费次数 + 递增成本是对的，需要防止"刷到传说"成为主要玩法。
4. **新增内容时同步调低基础出现概率。** Blood Price 是直接证据：新增 28 个 Duo 的同时下调总体出现率，避免"Duo 泛滥"稀释价值。

### 7.4 不能抄什么

- **局外永久加强（Mirror of Night）。** Lockdown 无局外养成（Spec §1）。Hades 是单人 PvE，永久加强只影响难度，不影响对手之间的公平；换成 FFA 就会变成先天优势。
- **单人尺度的 Legendary 数值。** Hades 的 11 个 Legendary 面对的是 AI；在 PvP 里同等强度会成为"拿到就赢"。
- **"锁定 4 神"。** 未证实，不引用。

### 7.5 二阶副作用与边界

| 副作用 | 边界 |
| --- | --- |
| 用前置条件门控 → 最优路径可被攻略，人人走同一路径（Hades 靠 Duo 数量与 Heroic 稀释） | 门控必须是"多入口"：同一强牌至少有两条以上不同来源的前置；并检查 pick 集中度 |
| 重抽有限 → 开局运气影响整局 | 保证首轮三选一内至少有一个"可用"（现有"不允许无解开局"规则已覆盖营地，须对牌也核查） |
| 要求"每张牌改写操作" → 删除大量小数值牌 → 低稀有度牌失去存在感 | 数值牌仍可存在，但只能作为"补强已选方向"，且不占用容量格最高优先位 |

## 8. Noita

主要来源：[NO1] Exploring the Tech and Design of 'Noita'（Petri Purho，GDC 2019 Independent Games Summit，仅摘要页）；[NO2] Road to the IGF: Nolla Games' Noita（Petri Purho，Game Developer，2019-03-13）；[NO4] Noita Sauna Update - Hotfixes（2020-12-01）；C 级 [NO5] Blueprinted Gaming 的 Noita 魔杖文档。

### 8.1 它解决了什么

- **涌现作为机器：** Purho 称模拟世界是"不断产生有趣游玩情境的涌现生成机器"，并举例：杀死头顶的敌人，尸体砸碎油灯，油洒到自己身上起火，是他自己"从未遇到过的情形"[NO2]。（A）
- **但"模拟不会自动变成好玩"：** 同一篇访谈明确说"模拟甚至可能妨碍更有趣的玩法"，难点在"这好玩吗？玩家能看懂吗？会太难或太令人沮丧吗？"[NO2]。（A）
- **开发者对"无上限涌现"的约束手段：** 后续补丁里出现"给 Spells to Power 加爆炸半径上限"[NO4]，说明即便是 Noita，也需要对失控的乘法效果做硬封顶。（A）
- **魔杖的顺序与乘法（C）：** 法术按顺序抽取，修饰符作用于其后的投射物，多重施放与触发/定时机制组合成嵌套结构；无限寿命等极端构造"对施放者本人也很危险"[NO5]。这是社区整理的机制说明，不是设计者对"自限作为平衡"的陈述。（C）

### 8.2 Lockdown 是否有同样的结构

**有"乘法与链式"，缺"自限"。**

- 乘法：同卡份数先相加、异卡相乘，技术上限 ×1000；链式深度上限 16、每权威步最多 256 个后续效果（[内容 §4.4](../../demo/CONTENT_AND_BALANCE.md#44-构筑牌与参数)）。这与 Noita"给失控效果加硬封顶"是同一类工程护栏。
- **自限：** 规格没有记录对施放者本人的伤害、元素地面对施放者的影响或空间限制（S5，未核对 SourceData）。Noita 的单人环境里，"对手"是敌人与地形，**自伤是主要的天然平衡器**；Lockdown 把这个平衡器完全交给"7 个对手的反制"（推断）。

### 8.3 可干净迁移的原则

1. **高上限必须绑定可感知的自我风险或空间约束。** 例如：高收益需要站位、需要靠近、会让自己更容易被控；而不是只靠"低出现率"。（原则，非数值）
2. **只对"失控"加硬封顶，不对"强"加封顶。** Noita 的爆炸半径上限与 Lockdown 的技术上限 ×1000 都属此类，数值调整只处理"强"。
3. **"看得懂"是前提。** [NO2] 的结论是复杂模拟若不让玩家理解，反而会妨碍好玩，Lockdown 的 10 种元素反应与 5 种地面状态需要保持"只因看见才会用"。

### 8.4 不能抄什么

- **无约束的自由组合。** 单人游戏里"自己把自己炸死"是故事；FFA 里是"别人看到的送头"。
- **以世界模拟为唯一平衡手段。** Lockdown 的世界不是全模拟，无法依赖地形与液体做平衡。

### 8.5 二阶副作用与边界

| 副作用 | 边界 |
| --- | --- |
| 引入自限 → 低稀有度构筑被"自限"拖垮，高稀有度构筑也被拖垮，上限整体下降 | 自限必须是**可选择的代价**（拿牌时可见），不是隐藏惩罚；且不应对已装牌的玩家追溯生效 |
| 自限会让"送人头"得到新的理由 | 自限伤害不得计入对手的击杀奖励；自杀不产生奖励（现有"同一生命重复致死不能产生第二份"已覆盖一部分） |
| 乘法上限被触顶 → 个别构筑"到头" | 触顶须有日志（现有 `LD_DAMAGE_FACTOR_CAPPED`），并以触顶率作为是否过强的客观指标，而不是靠体感 |

## 9. Backpack Battles

主要来源（Steam 官方公告，Playwright Games / PlayWithFurcifer 团队）：[BB1] Backpack Battles 0.9.16 - LOADOUTS & NEW ITEMS!（2024-08-11）；[BB2] The SKILL UPDATE is Here! - Patch 0.9.20（2024-09-20）；[BB3] Patch 1.0.7（2025-08-22）；[BB4] Patch 1.0.8（2025-09-26）；[BB5] Engineer Patch 1.1（2026-03-27）。

### 9.1 它解决了什么

- **可读性：** 1.0.7 把"商店各稀有度的出现概率"直接显示在商店招牌上 [BB3]；1.0.8 把"依赖子职业物品才出现的物品"也像配方一样在 ALT 时显示 [BB4]。（A）
- **早期失手的转型通道：** 替代起始背包（每职业一个选项）[BB1]；第 4、10 回合各三选一"技能"，官方原话是"打乱玩法并开启新 build"，另有"选子职业"回合 [BB2]；"Unstable Recombobulator"被官方描述为"帮助你回收不再需要的物品"的史诗商店物品 [BB1]；1.1 增加撤销/重做工具栏 [BB5]。（A）
- **转售流动性：** 没有设计者对"全额/部分转售"的陈述，只能从 Recombobulator 的存在推断它是一个**专门的回收阀**，而不是通用转售（推断）。

### 9.2 Lockdown 是否有同样的结构

- **读不懂关系（S4）：** 词汇不在界面显示，玩家无法知道"为什么这张牌更常出现"。Backpack Battles 的做法是"让关系和概率显性化"。
- **转型：** Lockdown 允许随时装卸牌与换技能（换主动退还已投技能点；不兼容牌退回牌库），洗点只在休息室；开局 3 次牌重抽与首刷免费让"早期失手"有出口。**文档没有写"牌能否转售成点数"**（[Spec §4.5](../../APP_SPEC.md#45-休息商店) 只写卖图纸得 1 点），无法判定流动性（需核对代码）。
- **锁定：** 真正的锁定在属性点：单维 18 点解锁关键石，洗点只能在休息室。

### 9.3 可干净迁移的原则

1. **显示关系而非隐藏加权。** 当一个系统按"标签"偏置抽取时，要么给玩家看见标签，要么明确"此牌适配你的构筑"的指示，否则它在玩家眼里就是不可解释的随机。
2. **显示概率。** 招牌显示稀有度概率，和 Lockdown 的 `drop_weight` 100/22/6 正好对应。
3. **回收阀而不是转售。** 一件专门的"回收"物，比"随时全额卖回"更易控制套利。
4. **撤销用于"手滑"，不用于"决策"。** 工具栏的撤销是误操作恢复，而不是改变已提交的选择。

### 9.4 不能抄什么

- **自动战斗与异步幽灵对战。** Lockdown 是实时操作与 FFA。
- **随时全额转售。** 在"点数既是货币也是属性"（S3）的体系里，全额转售会把点数变成无摩擦的储值，可在终局前零成本转型并绕过 18 点关键石门槛（推断）。

### 9.5 二阶副作用与边界

| 副作用 | 边界 |
| --- | --- |
| 显示词汇 → 玩家更容易走向"最优词汇组合" → 趋同（见 §4 的 Mayhem 教训） | 只显示"相关"，不显示分数；保留至少一半牌位不受加权 |
| 增加回收阀 → 变成"刷牌→回收→刷牌"的套利链 | 回收所得必须低于购入价，且仅限休息室，并且回收所得不进入"关键石"可用点 |
| 显示概率 → 玩家算出"必须刷到传说"的刷新次数 | 刷新成本递增（现有）+ 每次休息刷新上限；并监控"传说牌占购买"的比例 |

## 10. Brotato

主要来源（Steam 官方公告，Evil Empire / Blobfish）：[BT1] Update 1.2（2026-11-02）；[BT2] New Dawn Update is Available Now!（2025-10-27）；[BT3] Balancing Patch（2026-11-12）；[BT4] Brotato Demo - Update 0.1.0 (Steam Next Fest)；[BT5] The All Pain No Gain update is available now!（2026-05-12）。

### 10.1 它解决了什么

- **软偏置商店（A，仅武器）：** 1.2 的商店调整：被提供"你已拥有武器"的概率 20%→23%（早期波次额外 5%→7%），**每拥有一种不同武器再 +2%，最多计到 6 种**；被提供"你的某一类（class）武器"的概率则**每拥有一种不同武器 +1%，最多计到 6 种** [BT1]。这是有上限、小幅、且可被玩家看到的偏置，而不是硬锁。
- **负向选择权：** 引入 ban 系统，最多 8 个 ban，官方称"8 个是我们要找的甜点"[BT2]；后续平衡补丁又加入"每 4 波获得 1 个 ban 令牌，最多到第 20 波"[BT3]。（A）
- **经济作为 build 轴：** Harvesting 属性"在每波结束给材料与 XP"，在 Demo 0.1.0 就已存在 [BT4]；"All Pain No Gain"的新角色"Wounded"给更多 Harvesting 与速度但一击即死 [BT5]。（A）
- **负属性成本：** 官方补丁把"负护甲 -3→-2"这类调整当作平衡杆 [BT5]。（A，仅例子）

### 10.2 Lockdown 是否有同样的结构

**同构度高，但"可见性"不同。**

- 属性点既是成长也是货币（S3）：这正是 Brotato "买与不买的机会成本"，但 Lockdown 的取舍分散在"构筑环"和"休息商店"两个界面，且**没有负属性**。
- 偏置（S4）：Lockdown 的偏置是 ×1.2/×1.8 的**权重倍率**，而 Brotato 是"每种不同武器 +1%/+2%、最多计到 6"，量级小很多；更关键的是 Brotato 的类别在界面上清楚可见，Lockdown 不显示词汇。
- 经济 vs 战斗：Lockdown 的"点数来源"是升级、击杀、热搜存活、事件奖励，没有"被动收入"属性；但"上热搜存活每 60 秒 +1 点"是对**领先者**的被动收入（S6）。

### 10.3 可干净迁移的原则

1. **偏置小、有上限、可见。** 偏置要有上限（Brotato 到 6 种封顶），且**至少一部分货架不受影响**（Lockdown 商店 4 位里已有 2 位均匀，应保持）。
2. **负向选择优先于"刷到为止"。** ban 令牌让玩家剔除"我讨厌的"，比增加刷新次数更少制造"刷到传说"的路径；但它也提高可靠性，需要和 §4 的"可预期性"一起评估。
3. **经济属性当作 build 的一个轴。** 让"多拿点数"与"多拿战力"在构筑里互斥，而不是单一数轴。
4. **负属性给强物品配"明确可见的代价"。** 与 §8 的自限一致。

### 10.4 不能抄什么

- **波次价格通胀。** 在 FFA 里，难度来自其他玩家，而不是"随时间自动变贵"；价格随时间上涨会惩罚后期而不是惩罚领先者。
- **被动收入式的"龟缩"。** Harvesting 在单人生存里没问题；在 FFA 里"不战斗也涨"就是躲起来拿点。
- **角色专属物品池锁定。** Lockdown 全员同一基础角色，不应出现按身份锁池。

### 10.5 二阶副作用与边界

| 副作用 | 边界 |
| --- | --- |
| 点数既是货币也是属性 → 每增加一个点数"消耗口"（ban、刷新、重抽）就压缩关键石进度 | 总消耗口与点数收入的关系必须可测；任何新增消耗口都要以"关键石是否仍在合理等级内可达"为验收 |
| 引入负属性 → 玩家觉得"被惩罚" | 负属性只出现在高稀有度、拿到时就在卡面上明示；不允许隐藏代价 |
| 加 ban → 玩家 ban 掉"反制牌" | ban 只作用于自己的货架，不改变全局池；总令牌数有限 |

## 11. Slay the Spire

主要来源：[ST1] GDC Vault 会议页 'Slay the Spire': Metrics Driven Design and Balance（Anthony Giovannetti，GDC 2019）；[ST2] 同一演讲的幻灯片 PDF（共 22 页，信息密度低）；[ST3] Road to the IGF: Mega Crit Games' Slay the Spire（Anthony Giovannetti，Game Developer）；C 级 [ST4]。

### 11.1 它解决了什么

- **平衡目标：** 幻灯片首要原则是"每张牌都应该有位置（同时避免任何过度扭曲的东西）"，并提醒"不要过度依赖指标：数据是证据，不是结论"，另有 Ascension 作为"玩家技能分层"[ST2]。（A）
- **单人游戏前提：** Giovannetti 明说：他们"允许组合与强协同，是设计的标志"，并且"因为是单人卡牌游戏，不必担心强组合常见的缺点——对手因为过强的策略而感觉很差"[ST3]。（A）
- **地图风险回报：** 路径受 FTL 启发，"为策略游戏增加分层决策"，"地图导航给游戏增加风险/回报与规划，而不增加太多复杂度"[ST3]。（A）
- **敌人设计：** 需要"一大批以不同方式挑战不同策略的敌人"，不让存在"单一的常胜策略"[ST3]。（A）
- **"精英冒险/跳过精简/遗物催化"（C）：** 这些说法来自第三方整理 [ST4]，**不是 Mega Crit 原话**。精英给遗物、商店删牌花钱与卡位、跳过牌组的价值来自精简，这些是玩家共识，本文只作为问题陈述，不作为一手结论。

### 11.2 Lockdown 是否有同样的结构

**部分有，但机制替换了。**

- **精简/跳过：** Lockdown 没有"牌组稀释"——牌是常驻被动，**硬容量**（6 + ⌊(L−1)/3⌋，L1=6 格、L16=11 格）取代了"牌组变厚"（[内容 §4.4](../../demo/CONTENT_AND_BALANCE.md#44-构筑牌与参数)）。跳过一组三选一给 1 点属性点（`CARDS.SkipPoints`）。这是**经济上的跳过价值**，不是"提升一致性"的跳过价值。
- **精英风险回报：** 大野 1350 HP/126 攻击，"1 级只有普攻的角色单挑打不过"，标记营地首清给牌三选一，这是"风险换奖励"，**但没有 StS 式的"承诺前可见的路径选择"**：风险包含迷雾内的 PvP 暴露（推断）。
- **单人游戏前提不成立：** Lockdown 的"胡牌"面对 7 个真人/AI，必须由对手实时反制（S5）。

### 11.3 可干净迁移的原则

1. **风险在承诺前可读。** 精英的位置、档位、预期收益在标记营地时已公开，对 PvE 可读；对 PvP 风险不可读。应让"现在去哪最危险"至少对自身可见（例如附近最近的已公开敌对事件）。
2. **跳过要有真实价值，且不是绝对最优。** 当前 +1 点相对商店普通牌价（2 点）的价值需要被测量。
3. **催化型设计。** 现有牌中 convert 20、trigger 27、modify 21，说明已有"让其他牌更强"的类型。StS 的教训是遗物与牌不应只是加数值，而要改变"什么才是好牌"。
4. **指标驱动但不以指标为结论。** 把"每张牌都有位置"转成可测：每张牌的被选/被装/被保留率与"出现却被跳过"率，并保留 AI 的 `CardSkipChance` 作为基线。

### 11.4 不能抄什么

- **"单人所以破格无所谓"。** 这一前提在 FFA 不成立（S5）。
- **"跳过即精简"的逻辑。** Lockdown 没有牌组，跳过的真正回报是"点数"，而点数可堆向关键石（18 点）；若"全部跳过"变成最优，就变成"属性堆叠冲关键石"。
- **随机地图节点与单次失败重来。** Lockdown 是持续野外与定时休息。

### 11.5 二阶副作用与边界

| 副作用 | 边界 |
| --- | --- |
| 提高跳过价值 → "全跳过、堆关键石"成为主流 | 以"跳过率"与"关键石达成时间"监控；不得让跳过价值超过任意一张可用牌 |
| 让风险可读 → 玩家都能选择"绕开" | 风险回报比必须由"回报稀缺"保证（如首清奖励），不能靠隐藏风险 |
| 强催化 → 破格 → 7 个对手无法反制 | 每个催化型牌必须有对应的"可见失败状态"（自己受伤/被控/破盾后失效） |

## 12. Dota 2

主要来源：[DT1] Dota 2 Patch 7.11 的官方补丁数据（`dota2.com/datafeed/patchnotes?version=7.11`，时间戳对应 2018-03-15）；[DT2] Dota 2 Patch 7.33（2023-04-20）；C 级 [DT3] Liquipedia Gold/Changelogs（补丁汇总）。

### 12.1 它解决了什么

- **追赶不应按"队伍差"，而应按"受害者自身价值"：** 7.11 说明：失败方的 AoE 金币"不再随整体队伍净值差缩放，而只随死亡英雄的个人净值"，因为之前"队里一个核心打得好，一个辅助死亡就给对方越来越多金币"；追赶项改为 `(DyingHeroNetWorth × 0.026 + 70) / 击杀者数`，并把按净值排名的乘数由 1.2/1.1/1.0/0.9/0.8 改为 1.2/1.05/0.9/0.75/0.6 [DT1]。（A）
- **奖励路由给落后者：** 7.33 的 Tormentor 击破后，将 Aghanim's Shard buff 给"两名净值最低且还没有 Shard 的己方英雄"之一；全员已有 Shard 时每人获得金币与经验 [DT2]。（A）
- **不惩罚早期进攻（证据较弱）：** 补丁历史显示击杀赏金含"受害者等级项"与"连杀项"，并多次重标度连杀奖励（Liquipedia 汇总）[DT3]。（C）这些补丁说明 Valve 在**持续调整**，不能证明"已经解决"。
- **"一切都 OP，只是维度不同"：** **未找到一手来源**，本文不使用。

### 12.2 Lockdown 是否有同样的结构

- **非线性强度点（有）：** 2/5/9 级主动、3/6/9/12/15 级技能点、每 3 级牌与容量、单维 18 点关键石、5 档元素勋章三阶融合。强度确实是"跳变式"。
- **追赶（部分有）：** 击杀 XP 的等级差系数 40%–160%；上热搜的 150 XP 悬赏。**与 Dota 7.11 的差别：** Lockdown 的悬赏是**固定值**，不随被击杀者积累的价值变化；而且"上热搜"是二元状态（等级唯一最高才算，并列则无人），在 7.11 的语境里属于"按排名而不是按价值"。
- **反制生态：** Lockdown 有硬控递减、破盾、重伤、闪避，但**对抗对手构筑的"反制选择"依赖于"看到对手构筑"**。Spec 只明确了 Tab 战术计分板与赛后复盘；局内是否能看到对手构筑，**无法判定**（需核对）。没有这个信息，"针对对手出反制"就无法发生，反制生态会退化为"普适成长"。

### 12.3 可干净迁移的原则

1. **追赶按受害者自身积累而不是排名。** 把"上热搜"的二元悬赏与受害者自身积累价值（例如其已用点数或等级）连续挂钩，避免并列时无人被悬赏。
2. **目标奖励向落后者路由，且"相对中位数"而不是"相对最低"。** 7.33 在团队里把 Shard 给最低者，可行是因为同队没人想当最低；FFA 中会被"故意落后"利用。
3. **反制需要信息来源。** 若要有反制生态，就要公开足够的构筑信息，而不是只靠隐藏。
4. **只保留一条追赶通道。** 7.11 的修正本质是去掉"双重计价"（队伍差 + 个人净值），避免叠加。

### 12.4 不能抄什么

- **金币净值体系与买活。** Lockdown 没有金币，且 160 秒野外段不支持 40 分钟尺度的经济。
- **大额连杀赏金。** 在 FFA 里它会变成"送给一个人 7 次"的大额福利。
- **Roshan 式全局奖励。** Lockdown 有画饼老板与擂台，已承担这一职能。

### 12.5 二阶副作用与边界

| 副作用 | 边界 |
| --- | --- |
| 悬赏连续化 → 领先者被所有人盯上，且"上热搜"本来的被动点收入失去平衡 | 领先者的被动收入与被击杀悬赏要成对调；监控"领先者被围攻率" |
| 向落后者路由奖励 → 故意压等级（sandbagging） | 路由对象取"落后于中位数的一部分"，并设时间窗滞后，避免瞬时刻意跌落 |
| 公开构筑信息 → 玩家针对性出反制，也让"被针对"成为新的挫败 | 只公开"类别"（如控制/吸血/爆发倾向），不公开具体牌；且只在休息室公开 |

## 13. 来源索引

级别含义见 §0.1。URL 取自抓取记录；Steam 公告 URL 由 Steam News API 的公告 ID 构造（`steamcommunity.com/games/<appid>/announcements/detail/<gid>`），其中 [SV5] 的 ID 来自搜索结果页本身。

### Battlerite

| 编号 | 标题 | 发布者 / 日期 | URL | 级别 | 状态 |
| --- | --- | --- | --- | --- | --- |
| BR1 | The Future of Battlerite | Stunlock Studios，2019-07-03 | https://blog.stunlock.com/br/the-future-of-battlerite/ | A | 已读全文 |
| BR2 | Dev Update: The Big Patch™ | Stunlock Studios，2019-02-06 | https://blog.stunlock.com/br/the-big-patch/ | A | 已读 Arena 与 Overload 部分 |
| BR3 | Battlerite Arena Patch 2.0.2 | Stunlock Studios，2019-03 | https://blog.stunlock.com/battlerite-arena-patch-2-0-2/ | A | 仅读搜索摘要 |
| BR4 | Game Design Deep Dive: Turning Bloodline Champions into Battlerite | Peter Ilves，Game Developer，2016-10-27 | https://www.gamedeveloper.com/business/game-design-deep-dive-turning-i-bloodline-champions-i-into-i-battlerite-i- | B | 已读前 12000 字符 |

### League of Legends: Arena / Mayhem

| 编号 | 标题 | 发布者 / 日期 | URL | 级别 | 状态 |
| --- | --- | --- | --- | --- | --- |
| LA1 | /dev: Arena | Daniel "Riot Maxw3ll" Emmons，2023-06-27 | https://www.leagueoflegends.com/en-us/news/dev/dev-arena/ | A | 已读全文 |
| LA2 | /dev: Arena Round Three | Eduardo "Riot Cadmus" Cortejoso，2024-04-15 | https://www.leagueoflegends.com/en-gb/news/dev/dev-arena-round-three/ | A | 已读全文 |
| LA3 | /dev: Leveling Up Arena | Riot，2026-04-14 | https://www.leagueoflegends.com/en-us/news/dev/dev-leveling-up-arena/ | A | 已读全文 |
| LA4 | /dev: Augmentmaxxing ARAM: Mayhem | Riot Phlox，2026-05-27 | https://www.leagueoflegends.com/en-us/news/dev/dev-augmentmaxxing-aram-mayhem/ | A | 已读全文（对象是 Mayhem） |
| LA5 | LoL: 'Arena' Set Augments to be Removed | Inven Global，2026-05-28 | https://www.invenglobal.com/articles/22318/lol-arena-set-augments-to-be-removed | C | 已读；与 LA4 对象不符 |
| LA6 | All Guests of Honor for Arena | Strafe，2025-01 | https://www.strafe.com/news/read/all-guests-of-honor-for-arena-in-league-of-legends/ | C | 仅搜索摘要 |
| LA7 | New /dev Update: Arena Returns | MobaFire | https://www.mobafire.com/league-of-legends/news/arena-act-2 | C | 搜索摘要，原页 403 |

### Eternal Return

| 编号 | 标题 | 发布者 / 日期 | URL | 级别 | 状态 |
| --- | --- | --- | --- | --- | --- |
| ER1 | Season 3 Dev Stream Overview | Eternal Return 官方（Nimble Neuron），2024-02-07 | https://playeternalreturn.com/posts/news/1717 | A | 已读至"晚期成长"章节 |
| ER2 | 1.0.0 Gameplay | Eternal Return 官方 | https://playeternalreturn.com/posts/news/1281 | A | 已读前 5000 字符 |
| ER3 | PATCH NOTES 13.1 - NOVEMBER 19TH, 2026 | Eternal Return 官方，2026-11-19 | https://playeternalreturn.com/posts/news/3969 | A | 仅搜索摘要 |

### Supervive

| 编号 | 标题 | 日期 | URL | 级别 | 状态 |
| --- | --- | --- | --- | --- | --- |
| SV1 | JAN 22 - Leveling Rework + Big Balance Patch Notes | 2025-01-22 | https://steamcommunity.com/games/1283700/announcements/detail/1789580505277155 | A | 已读 |
| SV2 | [MUST READ] FEB 5 PATCH - Gold MATTERS + Mobility Adjustments + Discord Social | 2025-02-05 | https://steamcommunity.com/games/1283700/announcements/detail/1790848102589412 | A | 已读相关节 |
| SV3 | FEB 19 PATCH - More strategic tuning, balance, and mobility adjustments | 2025-02-19 | https://steamcommunity.com/games/1283700/announcements/detail/1792116353164260 | A | 已读相关节 |
| SV4 | MARCH 5 PATCH - Saros arrives, leveling adjustments, and Hunter balance | 2025-03-05 | https://steamcommunity.com/games/1283700/announcements/detail/1793384379343843 | A | 已读 |
| SV5 | THE REINVENTED SUPERVIVE 1.0 FEATURE RUNDOWN | 2025-07-23 | https://steamcommunity.com/games/1283700/announcements/detail/516342322187730946 | A | 内容经 Steam News API（gid 1806064758588867）读取 |
| SV6 | 1.02 PATCH NOTES - Dunking changes, Buffs & Nerfs and a New Hunter! | 2025-08-20 | https://steamcommunity.com/games/1283700/announcements/detail/1808601382394244 | A | 已读相关节 |
| SV7 | 2.01 PATCH NOTES - Tournament Updates, TTK Changes, and Hunter Balance! | 2025-11-05 | https://steamcommunity.com/games/1283700/announcements/detail/1815580768212543 | A | 已读相关节 |
| SV8 | Sunsetting SUPERVIVE - End of Service Announcement | 2025-12-17 | https://steamcommunity.com/games/1283700/announcements/detail/1819386365085433 | A | 已读（正文很短） |
| SV9 | Supervive is shutting down in February 2026 | Dot Esports | https://dotesports.com/supervive/news/supervive-shutting-down-february-2026 | C | 仅搜索摘要 |
| SV10 | SUPERVIVE Live Service Ending in 2026 | GAMES.GG，2025-12-18 | https://games.gg/news/supervive-live-service-ending-in-2026/ | C | 仅搜索摘要 |

### Hades

| 编号 | 标题 | 发布者 / 日期 | URL | 级别 | 状态 |
| --- | --- | --- | --- | --- | --- |
| HD1 | Supergiant's fourth outing Hades introduces a more mature, organized dev process | Greg Kasavin，Game Developer，2019-01-17 | https://www.gamedeveloper.com/design/supergiant-s-fourth-outing-i-hades-i-introduces-a-more-mature-organized-dev-process | B | 引文来自搜索摘要，全文只读前 9000 字符 |
| HD2 | The Blood Price Update - Patch Notes | Supergiant，2020-06-23 | https://steamcommunity.com/games/1145360/announcements/detail/3384966595888900267 | A | 已读相关行 |
| HD3 | Early Access Patch 042 -- v0.27008 | Supergiant，2020-03-17 | https://steamcommunity.com/games/1145360/announcements/detail/3893864668628715652 | A | 已读相关行 |
| HD4 | v1.0 Launch Update - Patch Notes | Supergiant，2020-09-17 | https://steamcommunity.com/games/1145360/announcements/detail/3819572206267800157 | A | 已读相关行 |
| HD5 | Hades:Boons - Hades Wiki | 社区 | https://hades.wiki.gg/wiki/Hades:Boons | C | 搜索摘要 |
| HD6 | Systems Architecture: Hades systems | Game Design Lab（teemo.dev） | https://teemo.dev/game-design/hades/systems/rebuild-blueprint/ | C | 搜索摘要 |

### Noita

| 编号 | 标题 | 发布者 / 日期 | URL | 级别 | 状态 |
| --- | --- | --- | --- | --- | --- |
| NO1 | Exploring the Tech and Design of 'Noita' | Petri Purho，GDC 2019 | https://www.gdcvault.com/play/1025695/Exploring-the-Tech-and-Design/ | A | 仅摘要页，未读演讲 |
| NO2 | Road to the IGF: Nolla Games' Noita | Petri Purho，Game Developer，2019-03-13 | https://www.gamedeveloper.com/game-platforms/road-to-the-igf-nolla-games-i-noita-i- | B | 已读前 9000 字符 |
| NO4 | Noita Sauna Update - Hotfixes | Nolla Games，2020-12-01 | https://steamcommunity.com/games/881100/announcements/detail/3856733252024610280 | A | 已读相关行 |
| NO5 | Noita Advanced Wand Mechanics / Wand Crafting | Blueprinted Gaming | https://blueprintedgaming.com/docs/noita/advanced-wand-mechanics | C | 搜索摘要 |

### Backpack Battles

| 编号 | 标题 | 日期 | URL | 级别 | 状态 |
| --- | --- | --- | --- | --- | --- |
| BB1 | Backpack Battles 0.9.16 - LOADOUTS & NEW ITEMS! | 2024-08-11 | https://steamcommunity.com/games/2427700/announcements/detail/5963414363400700202 | A | 已读 |
| BB2 | The SKILL UPDATE is Here! - Backpack Battles Patch 0.9.20 | 2024-09-20 | https://steamcommunity.com/games/2427700/announcements/detail/6341720536505682602 | A | 已读 |
| BB3 | Backpack Battles Patch 1.0.7 | 2025-08-22 | https://steamcommunity.com/games/2427700/announcements/detail/1808601382471962 | A | 已读相关行 |
| BB4 | Backpack Battles Patch 1.0.8 | 2025-09-26 | https://steamcommunity.com/games/2427700/announcements/detail/1811772772271581 | A | 已读相关行 |
| BB5 | ENGINEER IS HERE!! Patch 1.1 - FREE UPDATE OUT NOW!! | 2026-03-27 | https://steamcommunity.com/games/2427700/announcements/detail/1828441623104001 | A | 已读相关行 |

### Brotato

| 编号 | 标题 | 日期 | URL | 级别 | 状态 |
| --- | --- | --- | --- | --- | --- |
| BT1 | Update 1.2 | 2026-11-02 | https://steamcommunity.com/games/1942280/announcements/detail/1847920302249169 | A | 已读 |
| BT2 | New Dawn Update is Available Now! | 2025-10-27 | https://steamcommunity.com/games/1942280/announcements/detail/1814309641572669 | A | 已读相关节 |
| BT3 | Balancing Patch | 2026-11-12 | https://steamcommunity.com/games/1942280/announcements/detail/1849192144183252 | A | 已读开头 |
| BT4 | Brotato Demo - Update 0.1.0 (Steam Next Fest) | Evil Empire / Blobfish | https://steamcommunity.com/games/1942280/announcements/detail/4480532686362622844 | A | 已读相关行，日期未取 |
| BT5 | The All Pain No Gain update is available now! | 2026-05-12 | https://steamcommunity.com/games/1942280/announcements/detail/1832065502824117 | A | 已读相关行 |

### Slay the Spire

| 编号 | 标题 | 发布者 / 日期 | URL | 级别 | 状态 |
| --- | --- | --- | --- | --- | --- |
| ST1 | 'Slay the Spire': Metrics Driven Design and Balance | Anthony Giovannetti，GDC 2019 | https://www.gdcvault.com/play/1026309 | A | 会议页摘要 |
| ST2 | 同上幻灯片 PDF | Anthony Giovannetti | https://media.gdcvault.com/gdc2019/presentations/Giovannetti_Anthony_SlayTheSpire.pdf | A | 提取全部 22 页文字，信息密度低 |
| ST3 | Road to the IGF: Mega Crit Games' Slay the Spire | Anthony Giovannetti，Game Developer | https://www.gamedeveloper.com/game-platforms/road-to-the-igf-mega-crit-games-i-slay-the-spire-i- | B | 已读 |
| ST4 | Slay the Spire: Design tensions & philosophy | saschb2b | https://saschb2b.github.io/game-mechanics/games/slay-the-spire/design-tensions | C | 仅搜索摘要 |

### Dota 2

| 编号 | 标题 | 发布者 / 日期 | URL | 级别 | 状态 |
| --- | --- | --- | --- | --- | --- |
| DT1 | Dota 2 Patch 7.11 | Valve，补丁时间戳对应 2018-03-15 | https://www.dota2.com/datafeed/patchnotes?version=7.11&language=english | A | 已读 JSON 相关条目 |
| DT2 | Dota 2 Patch 7.33 | Valve，2023-04-20 | https://www.dota2.com/datafeed/patchnotes?version=7.33&language=english | A | 已读 JSON 相关条目 |
| DT3 | Gold/Changelogs | Liquipedia Dota 2 | https://liquipedia.net/dota2/Gold/Changelogs | C | 搜索摘要 |
| DT4 | Dota Author & Creative Team Guide（含"everything is overpowered"概括） | Shapes（第三方） | https://shapes.inc/fandom/dota/author | C | 仅搜索摘要，未证实 |

## 14. 跨游戏综合

### 14.1 十条最强的外部结论（按证据强度）

1. **路线固化是真实的结构风险，不是臆想**（ER）。ER 官方明确承认路线成为入门门槛，并且选择"缩小效率差、不消灭路线"。
2. **最后一击全拿会奖励第三方**，不同游戏用不同方式缓冲（ER 用"先交战方"，Supervive 用击倒即发与队伍）。FFA 没有这些缓冲，Lockdown 直接暴露在这个结构下。
3. **硬上限与软上限都会出问题**（Supervive 六周内来回）。应优先从供给侧约束，而不是增加规则。
4. **Trait/凑套装式加权会趋同**（Mayhem），并且"差一件没有效果"令人沮丧。Lockdown 的词汇加权是它的软版本。
5. **破格上限的可预期性要被刻意压低**（Arena Prismatic 不可购买）。Lockdown 允许点数购买传说牌是一个对立设计，需要数据判断。
6. **随机无能动性事件被 Riot 自己承认为问题**（Arena cameo）。Lockdown 的随机事件应保持"可预知、可反应"。
7. **单人游戏可以容忍"破格无所谓"**（StS），FFA 不行。Lockdown 的自限缺失（S5）是最大的结构空白。
8. **击倒/击杀重置必须小而可读**（Supervive 团队否决"完全重置"）。
9. **追赶应按受害者自身价值，且只保留一条通道**（Dota 7.11）。
10. **转型通道要存在**（Arena 的替换/升级、Backpack Battles 的起始背包与回收阀、Hades 的重抽权限），但不得变成无摩擦转型。

### 14.2 优先级（仅方向，数值均需另行提案并经批准）

| 优先级 | 方向 | 依据 | 先要回答的数据问题 |
| --- | --- | --- | --- |
| P0 | 评估"最后一击全拿 + 助攻额外发放"对围猎与补刀的诱因 | ER、Supervive、Dota 7.11 | 击杀中"补刀者伤害占比"分布；单个受害者的总 XP 与参与人数的关系 |
| P0 | 评估自限缺失 | Noita、StS | 触顶（`LD_DAMAGE_FACTOR_CAPPED`）次数、胜者构筑的乘区数、胜者的吸血来源数 |
| P1 | 词汇加权的可见性与趋同 | Mayhem、Backpack Battles、Brotato | 每局胜者构筑的词汇集中度；商店 2 个加权位的"命中传说"率 |
| P1 | 路线收敛 | ER | 不同玩家在同一段的营地清理顺序重合度；首清标记营地的到达时间分布 |
| P1 | 传说牌的可购买性 | Arena Prismatic | 传说牌来源占比（掉落 vs 商店购买）；购买后留存时长 |
| P2 | 悬赏连续化 | Dota 7.11 | "上热搜"并列无人时段占比；被悬赏者的被击杀率 |
| P2 | 跳过价值 | StS | 三选一被跳过率；跳过后 20 秒内的消费去向 |

### 14.3 二阶副作用汇总：哪些改动会互相放大

| 组合 | 放大效应 | 必须同时有的约束 |
| --- | --- | --- |
| 拉长战斗 × 第三方窗口 | 战斗越长、围观者越多 | 首攻方保护或奖励池上限 |
| 击杀后治疗/重置 × 最后一击 | 补刀者最强 | 重置只对"发起者归因"生效 |
| 显示词汇 × 词汇加权 | 趋同加速 | 保留不受加权的牌位；只显示相关性 |
| 增加点数消耗口 × 关键石 | 关键石达成延后 | 总消耗口与点数收入平衡 |
| 追赶奖励 × 故意落后 | 刷奖励 | 相对中位数计算，时间滞后 |
| 悬赏连续化 × 上热搜被动点 | 领先者既有收入又有赏金 | 两者成对调整，只保留一条通道 |

### 14.4 红线（明确不抄）

- 队伍制机制：Enemy Kill Effects、信标复活、复活病、队友死亡解锁的 Overload。
- 随机无能动性的伤害事件（cameo 类）。
- 等价装备、取消发育。
- 全额随时转售。
- 局外永久加强与账号货币（Spec §1 已排除）。
- 自动战斗与异步幽灵对战。

## 15. 缺口与未证实项

- **LoL Arena 的 cameo 被 Guests of Honor 替代**：只有 C 级来源，官方 `/dev` 原文未抓到（404/403）。若需引用，需再找官方原页或开发者视频。
- **Hades "锁定到 4 位神"**：未证实。
- **Supervive "动态终局圈目标"**：未证实；只证实圈缩小、Storm Shift 与最终圈搜索半径调整。
- **Dota 2 "一切都 OP"**：无一手来源。
- **Brotato 物品（非武器）tag 偏置与负属性设计意图**：无一手说明。
- **Backpack Battles 转售率**：无设计者陈述。
- **Noita 演讲正文**：未读。
- **Slay the Spire 精英/精简的设计者陈述**：只有 C 级整理。
- **Lockdown 侧：** 对施放者本人的伤害/自限、局内能否看到对手构筑、牌是否可转售成点数、各牌是否"可见地改写操作"，均未在规格中找到明确口径，需核对 SourceData 与代码；本文没有对这些点做断言。
- **所有结论都未经 Lockdown 实机数据验证**；规格自己也声明试玩数值"不宣称已完成最终平衡验收"（[Spec §9](../../APP_SPEC.md#9-内容与数值设计边界)）。

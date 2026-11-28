# Lockdown 优先级实施待办清单 (Prioritized Implementation Backlog: P0–P3)

本清单严格按 **游戏性信息价值、运行态风险严重度、开发成本与可逆性** 将全部研究结论转化为可执行、可验证、可回滚的工程与设计任务。未经用户拍板的玩法数值与规则变更均明确标注前置决策点。

---

## P0：阻断性运行态缺陷与生态失真修复（立即可执行 · 零设计争议）

> **目标**：在不改变产品核心方向的前提下，消灭会导致 **服务器挂死、核心事件 100% 损坏、出生点死锁、卡牌正负号写反、关卡数据脑裂与 AI 瘫痪** 的实锤缺陷。如果不先修完 P0，任何后续平衡调参与真人试玩数据都会被严重污染。

### P0-1：修复 `A12 越打越上头 × A09 手快有手慢无` 休息室假人同帧指数爆炸死机
- **问题**：`A12` 在每次 `A09` 额外追击上重复叠层并调用全属性重投影，而 `A09` 读取未封顶攻速生成额外追击，打不死假人 30 秒内单拳命中飙至 `111,846+` 次挂死服务器。
- **证据**：`LDCombatCards.cpp:695-720, 1460-1469`；实机遥测 `Artifacts\GameplayResearch\telemetry_7ai_excerpt.log`（`t=739650` 单击 `111,846` 次命中，写爆 1.2GB 日志）。
- **设计假设**：`A12` 只应随真实的每一次“出拳动作”线性升温，不应被单拳分裂出的残影自我指数放大。
- **修改范围**：`Source\Lockdown\Private\Combat\LDCombatCards.cpp`、`SourceData\Demo\content.json`（可选层数上限说明）。
- **预期影响**：彻底根除服务器卡死，保留高攻速追击流正常强度（每秒最多叠 5 层，单拳最多追加 4 段残影、溢出转追击增伤）。
- **成本 / 依赖 / 风险**：**极低（0.5 天）**；无前置依赖；风险极低。
- **验收标准**：新增规则测试 `Lockdown.Rules.Combat.OvercapFocusDummyBounded`（装 `2×A12 + 2×A09` 连续攻击训练假人 65 秒，单步命中 `<= 6`、帧耗时无增长）；重跑 `RunDemoOpponentCheck.ps1 -BotMatch` 全程 0 超时通关。
- **回滚条件**：若规则测试破坏既有合法连击计数回执，回退该提交重新核对 `bExtraStrike` 标记位。

### P0-2：修复 F2 中心擂台与 F3 小迷你旗每帧重置清零 Bug
- **问题**：`FLDObjectiveState::Advance` 每帧从 `Segment = 0` 循环判断 `View.Serial != Segment + 1`，在 `T >= 250s` 后每帧清空擂台伤害与迷你旗读条，导致两大中期核心争夺目标 100% 无法获胜并每局刷屏 2.2 万行日志。
- **证据**：`Source\Lockdown\Private\World\LDFieldPacing.cpp:88-100`、`LDGameModePacing.cpp:150-176`；Match 1 `Game.log`（`22,335` 行重复日志，`winner=-`）。
- **设计假设**：恢复分段中心事件后，中期玩家将自然向中心汇集交战，显著提升 F2/F3 PvP 冲突密度与逆风拿五选一翻盘机会。
- **修改范围**：`Source\Lockdown\Private\World\LDFieldPacing.cpp`、`Source\LockdownTests\Private\*`。
- **预期影响**：F2 擂台与 F3 迷你旗恢复正常结算，日志体积缩减 `~55%`，每帧无谓网络脏复制归零。
- **成本 / 依赖 / 风险**：**极低（2 小时）**；无依赖；零回归风险。
- **验收标准**：新增 `Lockdown.Rules.FieldEcology.ObjectiveAdvanceMonotonic` 覆盖 `T = 0..480s` 逐帧推进，断言 `View.Serial` 单调递增且擂台伤害跨帧累计正确。
- **回滚条件**：无（纯缺陷修复）。

### P0-3：对齐 `L_Demo.umap` 世界快照、修复 `#353` 11 处不可达营地与 `c001` 出生点寻路死锁
- **问题**：`L_Demo.umap` 实刷 64 营地/8 躺平角（与规格 34 营地/2 躺平角分裂），`#353` 11 个野怪落点在悬崖/地下不可达，且 `BuildEvaluation` Seed `c001` 实测捕获 `(-4772,-3546,502)` 分散出生点寻路死锁 90 秒。
- **证据**：`LDCampLifecycle.cpp:41-60`、`LDMapRegistry.cpp:419-515`、`LDMapTraversalAuditTests.cpp:52-60`、Issue `#353`、`20261128-131440-p3-breakout-samples` `c001a-bare.log`。
- **设计假设**：任何生成的野怪与玩家入场点都必须与中心旗杆双向连通可达，地图不应存在单向白嫖盲区或开局罚站陷阱。
- **修改范围**：`L_Demo.umap`（需协调关卡负责人）、`LDMapRegistry.cpp`（移除孤立面准入漏洞，强化分散入场点双向导航实测）、`LDMapTraversalAuditTests.cpp`（`!field_path || !centre_path` 升级为 `AddError`）、运行 `SyncDemoMap.ps1`。
- **预期影响**：消除 50% 构筑评估种子超时污染与野怪卡死挫败，统一文档、数据与运行关卡真相。
- **成本 / 依赖 / 风险**：**中（1–1.5 天）**；涉及 `.umap` LFS 二进制资产，需单人独占编辑并跑 `BootstrapAssets.ps1 -Mode Validate`。
- **验收标准**：`Lockdown.Rules.StaticScene.MapTraversal` 全绿且 `Placements.json` 100% `field_path == true`；重跑 `p3-breakout-samples.json` `c001` 种子 `0` 寻路超时。
- **回滚条件**：`BootstrapAssets -Mode Validate` 失败时用 Git LFS 还原 `.umap` 重新调整点位。

### P0-4：修复已核实的卡牌、元素、终局结算与 UI 换招 9 项核心逻辑 Bug
- **问题与证据汇总**：
  1. `Y11 气短` 爆劲倍率写反为 `×0.875` 削弱（`LDCombatCards.cpp:2084`）；
  2. `S09.3A2 / S10.3A1` 双元素分支被提前 `return` 吞没（`LDCombatElementsDirector.cpp:173`）；
  3. Ⅲ阶勋章副元素用 `HitId.A % 4u` 随机而非确定性表（`LDCombatElementsDirector.cpp:113`）；
  4. `N6 警戒封条` 宽度漏乘 `0.5` 放大 `3.14×`（`LDCombatSkillDirector.cpp:259`）；
  5. `U05 鹰眼架枪` 释放射程未走 `CastRangeCm` 导致空放（`LDCombatActions.cpp:1595, 2376`）；
  6. `D10 一路结冰` 先于 `D11 挪地盘` 执行导致永远自挪冰面（`LDCombatCards.cpp:904-922`）；
  7. `Y29 烧着扛揍` 缺失 `30% MaxHP` 护盾上限（`LDCombatCards.cpp:2002`）；
  8. 终局 `GuardRatchetSec` 无效及 `150s` 超时漏算 `BankedGuard / CompletedSprays`（`LDCaptureReducer.cpp:583`、`LDGameModeShowdown.cpp:181`）；
  9. 野外换招客户端 `LDChoicesPanel.cpp:121` 误清确认框、弹道闪避按飞行终点而非擦身点判 `300ms`（`LDCombatBranchDirector.cpp:708`）、技能点超 `6` 点死锁（`LDProgressionRules.cpp:1049`）及 `SummarizePlaytestLog.py:457` 裸基准虚高 `2.29×`。
- **成本 / 依赖 / 风险**：**中（1.5 天）**；纯 C++ 与脚本局部修正，攒成一批统一编译与 `Verify.ps1` 验证。
- **验收标准**：逐项补充最小 `Lockdown.Rules` 单元测试；重跑 `TestSummarizePlaytestLog.py`。
- **回滚条件**：单项独立函数提交，任一回归独立回退。

### P0-5：修复对手 AI 七大策略失明（让 AI 成为合格的构筑陪练与压测基线）
- **问题**：AI 只按平 A 估算打野战损拒打大野/远古、不捡恢复球、野外秒光点数导致 0 点逛商店、不会放 `电风扇/警戒封条/嘲讽/刺猬/冲刺鬼影`、满格永不换废牌、无脑报 PK 送头名 `+14` 点、终局无视缩圈烧死。
- **证据**：`Source\Lockdown\Private\AI\LDOpponentPolicy.cpp`；Match 1 `Game.log`（AI L7–L10 vs 真人 L16）；`EXP-02`（`hedgehog` & `dash_ghost` `0.0` 次施法）。
- **修改范围**：`LDOpponentPolicy.cpp`、`LDOpponentFrame.cpp`、`LDCombatObservation.cpp`（补齐技能 `WarningRemainingMs`）、`LDBreakoutPresets.cpp`（补齐 8 个零属性预设的 48 点分配并修正 `attack_speed` 误配的 `K01`）。
- **预期影响**：AI 终局等级从 L7–L10 升至 L12–L14，商店使用率 >70%，全部 24 主动与 10 大样板真实施法生效。
- **成本 / 验收**：**中（1.5 天）**；重跑 `p3-breakout-samples.json` 确认 `hedgehog` 与 `dash_ghost` `subject_casts > 3` 且 `RunDemoOpponentCheck.ps1 -BotMatch` 全绿。

---

## P1：最高杠杆核心游戏性重构实验（需用户确认方案倾向后实施）

### P1-1：解开“商店 vs 关键石”死结 + 选牌词汇与宿主卫生改造（对应议题二 方案 B + D）
- **问题与证据**：Match 1 全场主窗口 `0` 购物；蒙特卡洛证实当前质变率仅 `24.1%`（仅比纯随机高 `4.8%`）。
- **设计假设**：每轮休息发放 `2` 点当轮专属 `RestShopCredit` + 温和阶段稀有度（`F1 100/30/8 → F3 100/50/18`）+ 属性点亮词汇 + 满格一键换牌，可在不引入复杂双货币 HUD 的前提下将流派质变率翻倍至 `~50%`，同时用 `2.4×` 封顶防止同质化锁死。
- **修改范围**：`SourceData\Demo\content.json`、`DemoContent.py`、`LDProgressionRules.cpp`、`LDWorkshop.cpp`。
- **成本 / 验收**：**中（2 天）**；通过 `GameplayResearchLab` 与 5 局加速遥测验证：人均每局商店购买 `>= 2.5` 件、质变成型率 `>= 45%`、连续对局同套路完全重牌率 `< 35%`、0 废宿主卡。
- **回滚条件**：若出现特定 2 卡组合（如 `D01+D06`）单局出现率 `> 40%` 统治全场，可单改 `content.json` 回调 `RestAllowance` 或下调该卡权重。

### P1-2：终局反死锁实验——“人数自适应双杆占点 (`V2`)” vs “扩圈单杆拔河 (`V5`)” A/B 试玩
- **问题与证据**：8 人局 `600 cm` 终圈内 `450 cm` 独处圈超时率高达 `54.0%`，纯肉胜率 `24.0%`；经 Agent 6 反证证明单圈微调无法在 8p 下把超时率压进 `<10%`。
- **设计假设**：通过配置开关同时提供 `950 cm` 扩圈修调（保底）与“`>=4` 人存活双杆占点 / `<=3` 人收束单杆”实验模式，并重做 `私人旗杆/篡位` 等逃课改装，能让全流派（风筝/刺客/阵地/战士）都在终局拥有公平舞台。
- **修改范围**：`LDGameModeShowdown.cpp`、`LDCaptureReducer.cpp`、`content.json`。
- **成本 / 验收**：**中（2 天）**；8p 模拟与加速遥测超时率 `< 10%`，`Turtle` 胜率 `<= 16%`，真人盲测终局满意度 `>= 4.0/5`。
- **回滚条件**：通过 `-LDShowdownRules=Classic` 参数保留一键回退单中心旗杆能力。

### P1-3：机动、冲刺停步与“零蓄力预警爆劲”手感升级（对应议题四 方案 B）
- **问题与证据**：`240 cm/s` 走不出 `0.8s` 预警圈；`0.35s` 按住爆劲吞走位、劣化瞬发防反且在高频技能流倒扣 `31.7%` DPS；完美闪避场均仅 `6–8` 次。
- **设计假设**：移速温和提至 `270 cm/s`、`D` 冷却 `7.5s`（`0.18s` 全程闪避窗 + `585cm→135cm` 贴脸停步修复），并将爆劲改为 **`Alt + Q/W/E` 零蓄力瞬发（带 `>=0.18s` 金白爆气前摇保对手反制）**，将全面盘活走位、闪避与连招流畅度。
- **修改范围**：`LDPlayerOrders.cpp`、`LDPlayerInput.cpp`、`LDCombatPreInput.cpp`、`LDCombatMomentumDirector.cpp`、`content.json`。
- **成本 / 验收**：**中（1.5–2 天）**；需配合 **1 次渲染实机试玩 + 8 人真人盲测** 检验技能命中率与近战追击手感。
- **回滚条件**：若远程 `A05` 风筝胜率超标（`> 60%`），通过热重载回调移速或下调 `A05` 额外射程（`330 → 240 cm`）。

---

## P2：地图生态、节奏收紧与元素地面盘活（中期深化迭代）

### P2-1：人数自适应小/中野段内刷新 + 单体孤立增伤 + 大野贡献分账（议题三 方案 B）
- **问题与证据**：8 人局 34 营地人均上限仅 L8（后 90s 空图），2 人局人均 L16 溢出；大野经验效率倒挂且 100% 尾刀独吞。
- **范围与成本**：`LDCampLifecycle.cpp`、`LDCombatMonsters.cpp`、`LDGrowthDirector.cpp`、`content.json`（**2 天**）。
- **验收与护栏**：8p 终局均等升至 `L11–L13`，单挑流与 AoE 流刷野经验差 `< 18%`，拉脱野怪不再可被无伤定身白打。

### P2-2：对局节奏收紧、平权 PK / 实景擂台整合与冗余微事件裁撤（议题三 方案 B/D）
- **问题与证据**：休息+PK 占走近 40% 时长（0 观众仍等 10s 押注），9 种微事件分散冲突，PK 无平权送头名 `+14` 点。
- **范围与成本**：`LDMatchTimeline.cpp`、`LDPKDirector.cpp`、`content.json`（**1.5 天**）。0 观众秒跳押注、休息窗收紧至 `25–35s`、PK 启用临时平权等级（并在 Lobby 提供开关测试取消休息 PK 改走 F2 实景擂台）、删除 `外卖柜/二手贩子`、合并 `2.5s` 占圈空投。
- **验收**：前三段野外占比稳定 **`>= 70%`**，整局时长控制在 **`10.5–12.5` 分钟**。

### P2-3：地面状态供给打通与Ⅲ阶勋章“元素共鸣”重塑（议题四 方案 B）
- **问题与证据**：5 种地面状态中 3 种近乎绝迹；Ⅲ阶勋章单键随机自反应抹平双技能配合。
- **范围与成本**：`LDCombatElementsDirector.cpp`、`LDCombatGroundDirector.cpp`、`content.json`（**2 天**）。让 `S08/S09.2B/S10.2B` 产出真实地面状态、支持区域施法直接转化地面，将Ⅲ阶勋章改为 `180cm` 溅射挂元素 + `4s` CD 脚下造地，反应公式补入额外 AD 自适应加成。
- **验收**：地面系卡牌有效触发率提升 `3×`，物理流派反应伤害追平法系。

---

## P3：认知减负、复盘进化与长期架构探索（波次打磨与新模式原型）

### P3-1：可视流派徽记（替代隐藏 30 词条）、首屏教学修正与纯 PvP 赛后复盘
- **问题与证据**：`怎么玩` 第 2 卡写错快速施法按键（`LDTerms.h:269`）；30 个隐藏词汇不可见；赛后 Top-3 贡献被 10 万打野伤害污染。
- **范围与成本**：`LDTerms.h`、`ULDResultReport`、牌面徽记 UI（符合零滚动铁律）（**1.5–2 天**）。赛后默认展示 **对参赛者有效战斗贡献 + 致死败因摘要 + 一键收藏冠军构筑蓝图为下局星标指引**。

### P3-2：激进原型探索——“10 分钟无缝封街昼夜循环”或“3 星同卡原位融合工坊”
- **定位**：仅在 P1/P2 真人试玩仍反映“切场回休息室打断兴致”或“11 格工坊图标太碎看不清”时，在独立分支以原型方式小规模验证，不提前投入正式主线成本。

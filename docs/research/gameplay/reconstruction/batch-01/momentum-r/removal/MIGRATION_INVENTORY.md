# 删除通用长按爆劲与精准闪避：迁移清单（S1）

分支：`sinmentis/b01-mr-removal`（基于 `sinmentis/b01-s1-runtime-safety-correctness` 头）。PO 决定 M-11：删除完成后**并入可玩基线**；劲研究暂停（M-09），劲保留现状、普通技能继续耗劲，删除爆劲后劲只用于技能消耗。
行号基于该分支起点，删除后会变化；以符号名为准。

标记：
- **删** — 纯技术，可直接删除（已批准的 §3.1 / §4.1 范围）。
- **保** — 保持不变。
- **PO** — 需 PO 批准的迁移；列出 §4.3 候选方向。已有 PO 决定的写在“分支状态”（M-10、M-11）。

---

## A. 通用长按爆劲（Hold-to-Overdrive）

### A1 输入与请求（删）

| 位置 | 内容 |
| --- | --- |
| `Framework/LDPlayerOrders.cpp`（`PressSkill`/`ReleaseSkill`/`TickOverdriveHold`/`EndOverdriveHold`/`CancelArmedCombat`/`OverdriveHoldProgress`/`SyncArmedEntryFromQuickCastHolds` 中的爆劲分支） | 满劲按住 0.35 s 蓄爆劲、Alt 一键锁存、蓄力中移动/取消、单位目标失败清理 |
| `Framework/LDPlayerInput.cpp`（Alt 锁存绑定、`Navigate` 取消蓄力） | 输入绑定 |
| `Public/Framework/LDPlayerController.h`（`OverdriveEntry`、`bOverdriveHolding`、`bOverdriveArmed`、`bOverdriveLatched`、`OverdriveHoldStartSec`、`HeldOverdriveEntry`） | 客户端状态 |
| `FLDCombatActionRequest::bOverdrive`、`FLDCombatAction::bOverdrive`、`FLDCombatPreInput`（`Combat/LDCombatPreInput.h`）的 `bOverdrive` | 请求/预输入字段 |
| `ALDCombatDirector::SubmitCharge` + `FLDCombatChargeRequest` RPC + `FLDBodyCombatState::OverdriveCharge`（`FLDCombatOverdriveCharge`） | 服务器侧“正在蓄力”展示状态 |
| `LDCombatActions.cpp`：`ChargeHeld`（半程承认）、释放时 `Action.bOverdrive` 判定、按门槛扣劲 | 服务器判定 |
| 本批 T02-17 提交 `06174c70`（取消蓄力时的提示） | 随机制一起删除 |

### A2 服务器爆劲效果（删 —— 但见 A4 依赖）

| 位置 | 内容 |
| --- | --- |
| `ALDCombatDirector::ApplyOverdrive`（`LDCombatMomentumDirector.cpp`） | 数值 ×`OVERDRIVE.Scale`、五元素爆劲强化（火 +3 灼烧、水减速 45%/1.5 s、电 +0.5R、冻定身 0.6 s、馊持续 ×2），含无元素技能读取技能槽勋章元素 |
| `OverdriveThresholdOf` / `OverdriveScaleOf` / `EndOverdriveCharge` | 门槛与倍率 |
| `FLDCombatActiveTerms::OverdriveBurnStacks` / `OverdriveZoneBurnStacks`、`FLDCombatHit::OverdriveBurnStacks`、`FLDCombatCastLedger::bOverdrive`、`bReleasedOverdrive` | 爆劲携带到命中、元素与地面 |
| `LDCombatElementsDirector.cpp`（卡牌火 + 爆劲额外层数）、`LDCombatEffects.cpp`（Overdriven 命中标志、致死记录 `bOverdrive`）、`LDCombatMechanicsDirector.cpp`（区域脉冲爆劲层） | 结算接线 |
| `LDMomentum::FLedger::Overdrives`，`LD_MOMENTUM ... overdrive=` 字段 | 每命账本 |

### A3 内容与数据（删，需重生成 `DA_DemoContent.uasset`）

| 位置 | 内容 |
| --- | --- |
| `SourceData/Demo/content.json` 参数 `OVERDRIVE.Threshold/HoldSec/Scale/FireBurnStacks/WaterSlow/WaterSlowSec/ShockReactionRatio/RotDurationScale/ChillRootSec` | 爆劲参数 |
| `content.schema.json`、`LDDemoContentImport.cpp`（`OVERDRIVE.*` 读取）、`LDDemoContentValidation.cpp`（技能等级 × 爆劲倍率的数值域检查）、`FLDDemoOverdriveParameters`、`Scripts/DemoContent.py`（`_validate_momentum` 中的爆劲校验）、`ExportDemoNativeFixtures.py`、`DemoContentTestFixtures.py` | 管线 |
| 词汇 `overdrive`（`ELDCardVocab` 位）、效果操作 `overdrive_short`（native id 254） | 词汇/操作编号（删除后按 `RETIRED_RANGES` 保留号段） |

### A4 依赖爆劲的玩法内容（PO）

| 机制 | 现状 | §4.3 候选方向（不选定） | 分支状态 |
| --- | --- | --- | --- |
| **Y11 `card_short_breath` 气短**（`overdrive_short`，门槛 35、倍率 140%） | 完全依附通用爆劲 | 并入新 R（降低 R 门槛/提高 R 强度）；改为劲经济牌（降低技能劲耗/提高劲上限）；删除并替换 | **PO：删除**（M-10）；已删，号段 266 退役 |
| **元素爆劲强化**（五元素各一种强化） | 是“技能 × 元素”的强化层 | 作为默认 R 的元素分支；作为勋章 Ⅱ/Ⅲ 阶额外效果；删除 | 随爆劲删除 |
| **无元素技能读取技能槽勋章元素打爆劲**（APP_SPEC §4.4 勋章段） | 勋章的次要用途 | 交给 R；删除该句 | **PO：删除该句**（M-11）；已删，勋章主效果不变 |
| **A08 `card_fist_skill`** 文本“不触发爆劲” | 纯说明 | — | 删除该子句（**删**） |
| **K06 / A08 自动重放** `bAutoRecast` 屏蔽爆劲 | 屏蔽逻辑 | — | 随爆劲删除（**删**） |
| **劲的消耗端** | 爆劲是唯一的大额劲消耗 | 由 S2 劲资源研究决定（M1–M4） | **PO：劲保留现状**（M-09/M-11），只用于技能消耗 |
| **劲的消耗侧 AI** | `LDOpponentPolicy.cpp`“满劲且目标血量 > 40% 时爆劲” | 随 R 设计重写 | 已删 `bOverdrive` 字段与模拟蓄力段（S0 同意的最小删除）；`DodgeIntent` 保留 |

### A5 表现（删，含二进制资产）

| 位置 | 内容 |
| --- | --- |
| `UI/LDHUD.cpp`、`UI/LDHUDModel.cpp`（劲条“爆劲刻度”、槽位蓄力环、`OverdriveScaleBp` 技能说明弹层的元素爆劲行） | HUD |
| `UI/LDWorldLabels.cpp`（他人蓄力环） | 世界标签 |
| `Character/LDHumanoidVisualComponent.*`、`LDCharacter.cpp`、`LDBodyMotion`（`bCharging` 姿态） | 动作 |
| `Character/LDHitVfx.*`、`LDImpactFx.*`、`LDHitFeel.cpp`（`bOverdriveCast`、Overdriven 命中） | VFX 与打击感 |
| `LockdownEditor/Art/LDDemoVfxNiagara.cpp` 生成器 + 资产 `NS_LDOverdriveCharge`、`NS_LDOverdriveBurst`、`MI_LDOverdrive_{Floor,Mote,Ring,Snap,Streak}` | Niagara（二进制，单独提交删除） |
| 观察/视图 `LDCombatTypes.h`、`LDViewTypes.*`、`LDWorldViewRelay.*`、`LDCombatObservation.cpp`、`LDCombatCues.*`（蓄力/爆劲释放标志） | 复制字段 |
| `Probes/LDDemoArtCaptureProbe.cpp`、`LDDemoCoreProbe.cpp` | 渲染/场景探针中的爆劲画面 |

### A6 遥测与脚本

| 位置 | 分类 |
| --- | --- |
| `LD_MOMENTUM overdrive=` | 删字段（**删**） |
| `Scripts/SummarizePlaytestLog.py`、`TestSummarizePlaytestLog.py` 的爆劲次数列 | **S5 所有**，需协调；脚本须容忍字段缺失 |
| `Scripts/BuildEvaluationManifests/p3-ttk-baseline-l{1,6,10}.json` 中的爆劲用语 | **S2 所有**，列出待协调 |

### A7 测试

| 测试 | 处理 |
| --- | --- |
| `Lockdown.Rules.Combat.Overdrive.Threshold/Release/Cost/ElementBonus`、`Combat.Cards.Y11` | 删（专属） |
| `Combat.AdaptiveAndKeystones` 中 Y11 段 | 删该段 |
| `DemoControlFeel.ElevationAimAndClip` 的 g/h/i 段 | 删该段 |
| `LDBodyMotionTests`、`LDHUDRulesTests` 的蓄力姿态/刻度断言 | 删对应断言 |
| `LDMomentumRulesTests` 中的爆劲断言 | 删对应断言 |

### A8 文档

`APP_SPEC.md` §0 术语表、§3.2、§4.4 勋章段、§7 HUD、§9 验收；`CONTENT_AND_BALANCE.md` §3.2.1 爆劲参数、§3.3 成长段“爆劲再 ×1.35”、A08 / Y11 行、勋章 Ⅰ 阶、`OVERDRIVE.*` 表；`COMBAT_AND_GROWTH.md`、`MOVEMENT_INPUT_AND_CAMERA.md`、`OPPONENT_AI.md`、`VISION_AND_UI.md`、`VFX.md` §7.7、`READINESS.md`、`DEVELOPMENT.md`（劲收入报告列）。删除分支上同步改为当前事实。

---

## B. 通用精准闪避（Perfect Dodge）

### B1 判定与奖励（删）

| 位置 | 内容 |
| --- | --- |
| `ALDCombatDirector::ResolvePerfectDodges` 及调用点：`LDCombatActions.cpp`（直线、预警自身范围）、`LDCombatBranchDirector.cpp`（弹道走廊，含本批 T02-12）、`LDCombatSkillDirector.cpp`（扇形通道、地面落点）、`LDCombatMonsters.cpp`（野怪预警） | 预警精准闪避 |
| `LDCombatEffects.cpp`：冲刺起手 0.12 s 内擦过直接攻击/弹道 → 本次减伤 50%、返还冲刺 30% 剩余冷却 | 擦身（原生）精准闪避 |
| `MOMENTUM.PerfectDodge`（20 劲）、`PerfectDodgeWindowSec`、`DashDodgeWindowSec`、`DashDodgeCutRatio`、`DashDodgeCooldownRefundRatio` | 参数（content + schema + importer + `DemoContent.py` + fixtures） |
| `LDMomentum::IsPerfectDodge`、`ELDMomentumBook::Dodge` 与 `FLedger::DodgeP2`、`LD_MOMENTUM dodge=` | 劲收入来源 |
| `FLDBodyCombatState::PerfectDodges/LastPerfectDodgeAtMs/DashStartFootCm`、`PerfectDodgeMarks`、`LD_PERFECT_DODGE` 日志 | 计数与日志 |
| `ELDDemoEffectTrigger::PerfectDodge`、`FLDCardGrowth::PerfectDodges` | 卡牌触发器 |
| `OnPerfectDodgeCards` | 卡牌入口 |
| 本批 T02-12 提交 `54727752` 与测试 `Combat.Events.FlightDodgeTiming` | 随机制一起删除 |

### B2 依赖精准闪避的玩法内容（PO）

| 机制 | 现状 | §4.3 候选方向（不选定） | 分支状态 |
| --- | --- | --- | --- |
| **D04 `card_dodge_strike` 闪完就打**（`dodge_free_cast`：完美闪避后下个技能不耗劲） | 依附闪避事件 | 迁移触发条件（如“冲刺穿过敌方弹道/区域后”）；转为冲刺后的普通奖励（需限频）；并入其他冲刺牌；删除并替换 | **PO：删除**（M-10）；已删，号段 247、操作 252 退役 |
| **D09 `card_slippery` 越躲越快**（每次完美闪避本局移速 +1.5%，无上限） | 成长牌，计数来源消失 | 换成其他计数事件（承受控制、冲刺接触、躲开预警）；并入 A13 式成长；删除 | **PO：删除**（M-10）；已删，号段 252 退役；`hit_growth` 只剩 `BasicHit` 触发 |
| **U07 `card_phantom_molt` 幻影脱壳**（完美闪避时留下嘲讽野怪、1 s 后自爆的残影） | 依附闪避事件；残影系统（`Decoys`）只被它使用 | 改为“冲刺起点留下残影”（需冷却/限频）；改为受到致命一击或被控时触发；删除 | **PO：删除**（M-10），含残影系统；已删，号段 296、操作 269 退役，`PhantomDecoy` 外观号 23 留空 |
| **S03 `active_crotch_grab` 2B 掏完就溜**（命中后翻滚 250 cm，“享 0.12 s 精准闪避”） | 翻滚本身仍有效，丢失闪避窗口这部分价值 | 维持翻滚、去掉闪避说法；以其他收益补偿（如翻滚期间短暂减伤）；替换分支 | **PO：只删“精准闪避窗口”字样，不补偿**（M-11）；已改 |
| **劲收入：精准闪避 +20 劲** | 劲收入来源之一 | 由 S2 劲资源研究重新分配 | 删除 |
| **冲刺冷却返还 30%**（闪避时） | 冲刺节奏 | 不补偿 / 脱战冲刺恢复加快（PO §5 列为轻量研究项） | 删除 |
| **擦身减伤 50%** | 防御收益 | 不补偿；由 R 或特定技能（格挡/招架）承担 | 删除 |

### B3 保持不变（保）

| 位置 | 理由 |
| --- | --- |
| 普通冲刺 `D`、冲刺接触、`as_dash` 翻滚分支的位移本身 | §4.2 必须保留 |
| `AI.DodgeChance` 与 `LDOpponentPolicy::DodgeIntent`（看见预警后用 D 横向躲出范围） | 这是空间躲避，不依赖精准闪避判定；删除后仍有价值（§4.2“通过空间位移避开技能范围”） |
| 招架 `parry_stance`（S16 等）与 `SelfParry` 音效 | 独立能力，§4.2 保留设计空间 |
| 预警（`Warnings`/`WarningRemainingMs`）的显示与 AI 读取 | 预警本身服务可读性，不依赖闪避 |

### B4 表现（删，含二进制资产）

| 位置 | 内容 |
| --- | --- |
| `LDObservation::Dodged` 命中标志与复制 | 观察 |
| `UI/LDWorldFeedback.*`（闪避飘字/金色残影）、`UI/LDTerms.h`（“闪避”术语） | UI |
| `LDHitVfx`/`LDImpactFx`/`LDHitFeel`/`LDHumanoidVisualComponent` 中的闪避金边与残影 | VFX/动作 |
| 音效 `SelfPerfectDodge`：`SourceData/Audio/audio_cues.json`、`synth_demo_audio.py`、`ELDAudioCue`、`LDAudioCatalog`、`LDAudioCueTracker`、`LDAudioFeel`、`Content/Lockdown/Audio/Cues/SW_SelfPerfectDodge.uasset`、`DA_DemoAudio` | 音效（删除后 `ELDAudioCue` 号段按目录规则留空） |
| `NS_LDDecoyPop`、`MI_LDDecoyPop_{Flash,Smoke,Wisp}` | 残影爆炸（随 U07 删除，已删） |
| `T_icon_card_dodge_strike` 等卡牌图标（`SourceData/UI/ui_icons.json`） | 随卡牌删除（已删） |

### B5 遥测、测试、文档

- 遥测：`LD_PERFECT_DODGE`、`LD_MOMENTUM dodge=`；`SummarizePlaytestLog.py` 的完美闪避计数（**S5 所有**，协调）。
- 测试（删专属）：`Combat.Cards.D04/D09/U07`、`LDMomentumRulesTests` 的闪避窗口、`LDBuildPrimitivesTests` 的“0.12 s 擦身 + 残影”段、`LDWorldFeedbackTests` 的闪避飘字、`Combat.Events.FlightDodgeTiming`、`TestDemoApprovedImport.py` 中这些卡的期望。
- 文档：`APP_SPEC.md` §3.2 / §9 验收；`CONTENT_AND_BALANCE.md` §3.2.1 闪避参数、S03 分支表、D04/D09/U07 行；`COMBAT_AND_GROWTH.md`；`AUDIO_AND_FEEL.md`；`VFX.md` §7.7；`VISION_AND_UI.md` 音效段；`READINESS.md`；`DEVELOPMENT.md` 劲收入报告列。

---

## C. 删除分支上的“下架”处理方式

“下架”指在删除分支上让该牌**无法获得、无法装备**，避免“看似可用实则失效”，同时不替该牌选择新规则。可选实现，二选一，见下方进展记录：

1. 从 `content.json` 删除卡牌与专属效果操作，`native_ids.json` 把号段加入 `RETIRED_RANGES`（仓库现有的退役做法）。PO 若决定迁移，新牌使用新号段。
2. 保留定义，新增“不进入任何牌池/商店/开发工具”的标记。需要新的 schema 字段，迁移后再删除标记。

---

## D. 跨所有者协调项

| 项 | 所有者 | 状态 |
| --- | --- | --- |
| AI `bOverdrive` 意图字段与爆劲决策（`LDOpponentPolicy.cpp`、`LDOpponentController.cpp`） | B01 S2 | 改动前协调 S0 |
| `SummarizePlaytestLog.py` 爆劲/闪避列 | S5 | 协调 S0 |
| `BuildEvaluationManifests/p3-ttk-baseline-*.json` | S2 | 协调 S0 |
| Niagara/音效二进制资产 | S1 独占本分支 | 单独提交 |

---

## 进展记录

- 初稿：完成范围盘点。
- 删除完成（分支 `sinmentis/b01-mr-removal`，三个提交）：
  - `bef3d73f` 删除通用长按爆劲：输入、服务器、`OVERDRIVE.*` 参数、AI 模拟蓄力与 `bOverdrive`、HUD/世界标签/VFX/音效表现、`NS_LDOverdrive*`/`MI_LDOverdrive_*` 资产、专属测试；观察 schema 24→25。S10 3B1 放电改读 `COMBAT.ShockRatio`（同值 0.5）；技能说明弹层删去勋章元素爆劲行；`LDTerms` 上手说明改为“技能键加左键确认。打人、连击、躲招都回劲。”。
  - `ba550e04` 正式删除 Y11、D04、D09、U07（PO M-10）：卡牌、显示行、图标、效果操作 `dodge_free_cast`/`overdrive_short`/`phantom_molt`、词汇 `overdrive`/`perfect_dodge`、`hit_growth` 的 `PerfectDodge` 触发、D04 免费施放、残影系统与 `PhantomDecoy` 外观（号 23 留空）、`StandIn` 身形、`NS_LDDecoyPop`；native id 247/252/266/296 与操作 252/254/269 退役。
  - `f92890d0` 删除通用精准闪避：预警与 0.12 s 擦身判定、减伤、劲收入、冲刺冷却返还、计数、`LD_PERFECT_DODGE`、`LD_MOMENTUM dodge=`、`Dodged` 命中标志（schema 26，32 留空）、金色残影、飘字、术语、`SelfPerfectDodge` 音效（号 36 留空）、`MOMENTUM.PerfectDodge*/DashDodge*` 参数；本批 T02-12 测试 `Combat.Events.FlightDodgeTiming` 随机制删除。S03 2B 只删“完美闪避窗口”字样。`WorldFeedback.DodgeAndChain` 改为 `WorldFeedback.ChainEnd`。
- 文档已改为当前事实。未改：`DEVELOPMENT.md` 劲收入报告列描述的是 S5 的 `SummarizePlaytestLog.py`，待 S5 改脚本后同步。
- 爆劲门槛刻度 `LDBroadcastPaint::FBar::Notch` 已无调用方，单独提交删除。

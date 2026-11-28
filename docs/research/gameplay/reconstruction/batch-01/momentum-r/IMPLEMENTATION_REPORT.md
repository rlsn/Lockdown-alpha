# 自动强化（M-12）实施报告 · MR-Impl

分支 `sinmentis-b01-mr-auto-empower`（以 merge 同步 `sinmentis/b01-mr-removal` 与 `sinmentis/b01-integration@8b68cf1e`）。
输入：PO 第四道指令；S0 裁定（参数取 `momentum/AUTO_EMPOWER_PARAMS.md` v1，规格取 `build-integration/AUTO_EMPOWER_SPEC.md`，`critique/AUTO_EMPOWER_CRITIQUE.md` #2/#6/#7 必做）。

## 1. 开关

| 方式 | 说明 |
| --- | --- |
| `content.json` `EMPOWER.Enabled=1` | 数据开关（默认 0）；`EMPOWER.*` 共 27 个参数，DEV `-LDDevContentReload` + `python -B Scripts\SyncDemoContentData.py` 热重载 |
| 主机命令行 `-LDEmpower` | 开发期开关，不改数据；`RunDemoOpponentCheck.ps1 -ExtraArguments '-LDEmpower'` 可直接跑 |
| 主机命令行 `-LDEmpowerHost=Basic\|Q\|W\|E\|D` | 未自选宿主的参赛者统一用该宿主（A/B 用） |

关闭时：不改技能劲耗、不接受宿主命令（回执“强化实验未开启”）、观察帧强化标记恒 0、HUD 与删除分支一致。另有保险：`EMPOWER.Cost > EMPOWER.Threshold` 时视为关闭。

## 2. 实际实现的机制

- **劲**：开局 50；交战被动 2.5/s（PvE 与 PvP 同算）；脱战 5 s 后以 10/s 回到 50；普攻回劲不变；技能命中回劲每次释放至多 3 个目标；玩家起源反应 10、派生反应 0；受击回劲 `EMPOWER.DamagedPerPercent`（默认 0，A/B 开关）。
- **技能不耗劲**：开关下 Q/W/E 学得劲耗为 0，只受冷却限制（`ActiveOf`）。
- **宿主印记**：每名参赛者一个宿主（普攻/Q/W/E/D）。玩家默认普攻，在工坊空手点击技能/普攻/冲刺栏即设为宿主（金色“劲”签）；权威命令 `ServerSetEmpowerHost`，按序号去重，拒绝回执中文原因。AI 按“宿主上的卡牌阶数 + 技能分支阶 −1”自动取最高（并列 Q→W→E→普攻→D）。
- **自动强化**：劲 ≥ 门槛 60 时，宿主的下一次合法动作在入列时被标记为强化，释放提交时扣 50（成功释放即扣，打空照扣；释放时若劲已不足则记 `outcome=short` 按普通动作执行）。非宿主动作正常执行，不消耗机会。自动重放（`bAutoRecast`）永不强化。
  - 普攻与 D：仅当本次动作针对/命中参赛者时触发；视野内无敌方参赛者时野怪也算（反证 #2）。
  - Q/W/E：成功释放即触发。
- **五类处理器**（由技能 effect op 推导）：
  - 拳（普攻、empower_basic/frenzy 技能）：一记劲拳 = 第 3 段连击形态 + 推开 + 前方扇形追加伤害（`FistConeRatio`）。
  - 打（多数伤害技能）：形状 ×`ShapeScale`（射程/半径/长宽）、追加前摇、次级效果（减速/定身等时长）×`SecondaryScale`，本次释放的原生命中上限解除（仍受 E3 每目标一次）。
  - 守（护盾/格挡/架势/嘲讽）：零追加前摇；护盾到期或被打破时以 `GuardBurstRadiusCm` 爆开。
  - 移（冲刺/冲刺类技能）：不在敌前止步，穿过至多 `MoveContacts` 人，各结算一次接触；落地震地减速。
  - 阵（区域/陷阱/扇面/书包/封条）：形状与生命 ×`WardScale`。
  - 元素层：沿用旧爆劲元素表（不再带倍率）：火灼烧、水湿、电导电、冰减速。
- **防递归（E1–E4）**：强化动作及其派生（同链、同释放账本、`EmpowerSerial` 命中）不产劲（记入 `blocked`）、不再触发强化；派生反应回劲为 0；接触上限按目标去重。
- **卡牌交互（反证 #7）**：脑力关键石“零耗 +15 劲”在开关下不触发；K06 重放清强化标记；人头刷新只刷冷却，不退劲；劲耗乘数/返还/换算不作用于强化消耗。
- **HUD**：劲条在门槛处画刻痕，达门槛变金色；宿主技能槽金边，就绪时呼吸；劲条标签“劲·Q”；悬停说明宿主、门槛、消耗；工坊宿主金签。对手头顶劲条在“达门槛且宿主可用”时变金，目标卡显示“强化就绪（类别）”（观察帧 Schema 29 新增 `EmpowerReady`；27 为 S1 的“你被锁定”，28 为 S1 的 `CueFlags`，经视野授权）。
- **遥测**：`LD_EMPOWER`（host/processor/source/serial/paid/before/after/outcome）、`LD_HIT empower=<serial>`、`LD_MOMENTUM empower= empower_spent= blocked= armed_ms=`。

## 3. 玩家操作体验

不新增按键。玩家在工坊点一次选宿主（不选则是普攻）。战斗中劲条过刻痕变金、宿主槽亮起，下一次按宿主键（或普攻命中英雄）自动打出强化版本；扣 50 后通常剩 10–20，需约 15–20 s 交战再次就绪。非宿主技能随便按，不会“误用”强化。

## 4. 代表构筑（SPEC §6）

| # | 构筑 | 本实现的强化效果 | 状态 |
| --- | --- | --- | --- |
| B1 | 普攻攻速 | 劲拳：第 3 段 + 推开 + 扇形追加；A01 在劲拳链内不回劲 | 已实现，规则测试 Punch/ChainNoMomentum |
| B2 | 慢速重击 | 同 B1，暴击按原规则；推开可与 A04/Y08 叠加 | 已实现；“劲拳必暴”未做 |
| B3 | E = 板砖循环 | 打类：射程/范围放大；技能全程不耗劲 | 已实现；“连环砖 + 再落三砖”专属补丁未做 |
| B4 | Q = 滋你一脸 + 电 | 打类：长宽 ×1.4、元素层湿减速；直线终点留水洼 3 s（随后电导电）；强化链内 Y04 回劲被拦 | 已实现（第二轮补水洼，规则测试 Ground） |
| B5 | Q = 锅盖挡脸 | 守类：零追加前摇 + 护盾到期/打破爆开；Y06 每层再爆一次，两次爆炸都带强化序号、不产劲 | 已实现，规则测试 Shapes / ShieldBurstSerial |
| B6 | D 冲刺 | 移类：穿 ≤3 人各一次接触；D01 刷新不退劲 | 已实现；“每人一次普攻”依赖 D03 原逻辑 |
| B7 | W = 蛮牛顶 | 移类：穿人 + 落地震地减速 | 已实现；穿人定身 0.7 s 未做 |
| B8 | Q = 占座书包 | 阵类：生命/范围 ×WardScale | 已实现；爆炸 +40% 未做 |
| B9 | E = 警戒封条 | 阵类：长度 ×1.4；首次触碰即眩晕 0.6 s | 已实现（第二轮，规则测试 CordonTouch） |
| B10 | 8 AI 整局 | 宿主普攻 117 / Q 40，处理器拳/打/守；157 次全部 applied，无 `blocked` 以外异常 | BotMatch 7 AI 已跑（§5.2） |

### 4.1 第二轮：机制与形态补丁（S0 第二轮指令 ①–⑦）

每条按规格 §2.3 那一行所复用的现有机制实现，规则挂在机制上（`LDAutoEmpower::ApplyForms`），所以以后有同样机制的新技能自动继承。数值都在 `EMPOWER.*`（新增 `GroundSec` 3 s、`WallStunSec` 0.6 s、`CounterStunSec` 0.5 s、`TouchStunSec` 0.6 s、`ConeAngleScale` 2）。

| 项 | 技能 | 强化后 | 复用机制 | 规则测试 |
| --- | --- | --- | --- | --- |
| ① 地面残留 | S07 烫你屁股 | 脚下留火场 3 s，范围同本次（已放大的）区域 | `SpawnGround` 火场 | Forms、Ground |
| ① | S09 滋你一脸 | 直线（已加长）终点留水洼 3 s | `SpawnGround` 水洼 | Forms、Ground |
| ① | N1 冰可乐 | 扇形上留冰面 ≥3 s（已有 2A 时取较长） | N1 2A 的扇形冰面 | Forms |
| ② 撞墙眩晕 | S01 滚远点 | 击退 ×1.4，撞墙眩晕 ≥0.6 s | S01 3B1 `wall_stun` | Forms |
| ② 反击眩晕 | S05 拧你手腕 | 招架窗口 ×1.4，反震眩晕 ≥0.5 s | S05 3B1 `melee_stun` | Forms |
| ③ 穿透 | S06 甩你一鞋、N3 过期外卖 | 弹道穿过路上所有人，每人全额 | M14 弹道穿透计数 | Forms、Pierce |
| ③ | A05 飞行普攻（普攻宿主） | 劲拳不打扇形，改为弹体穿透到射程尽头，后续每人吃 50% 追加伤害（无命中特效、无连击段） | 同上 | Pierce |
| ④ 首触眩晕 | N6 警戒封条 | 每名敌人首次触碰即眩晕 0.6 s | 封条触碰（3A1 的二次触碰眩晕照旧） | Forms、CordonTouch |
| ⑤ 全抓 | S02 扒裤子 | 抓住直线上所有人（不止最近一个） | grab_line 目标集合 | Forms、GrabAll |
| ⑥ 角度 ×2 | N1 冰可乐 | 扇形角度 ×2，射程不变（第一轮是角度和射程都 ×1.4） | 目标扇形 | Forms |
| ⑦ 护盾爆炸序号 | S14 + Y06 | 强化护盾层的全部爆炸（自身爆开、S13 A1 式破盾爆炸、Y06 每层一次）都带本次强化序号，不产劲 | 护盾分支行记序号 | ShieldBurstSerial |

### 4.2 第二轮：探针与评估（S0 转 S5 清单 a–c）

- **a) 拥有连接探针**（`RunDemoGrowthLinkCheck.ps1 -ExtraArguments '-LDEmpower'`，`LDDemoGrowthLinkProbe.cpp`）。客户端：
  - 本人视图起始宿主为普攻，并带门槛与价格；
  - 宿主请求的回执是视图在其序号下写出该宿主；
  - 重放旧序号（换成别的宿主）不改变任何东西；非法宿主被拒并带原因；
  - 击杀用普攻宿主：本人视图见过劲达门槛，并计到强化次数。

  Host：
  - 对手 `EmpowerReady` 只随视野授权的劲组出现，且确实看到了；
  - 权威侧每次强化恰好扣一次 `EMPOWER.Cost`（重发不重复扣费），且与客户端视图计数一致。

  不带开关时：一次宿主请求被拒（“强化实验未开启”）、视图不变。Combat 探针未改：GrowthLink 已覆盖以上断言，且场景更短。
- **b) BuildEvaluation 宿主计划**：计划里某一侧可写 `empower_host: Basic|Q|W|E|D`（按 arm，即按一组 Case、按一侧），需要场景 `empower: true`。原生驱动在比赛开始后经 `SubmitEmpowerHost` 提交；被拒则该 Case 判 `Invalid`。Adapter（合入集成分支后统一为 v19：含 S2 的 clear_camps/strafe 与本分支的按侧宿主）判读：`LD_EMPOWER_HOST` 恰好一次、该侧只以该宿主强化，否则 `EmpowerHostNotApplied`。示例清单 `Scripts/BuildEvaluationManifests/b01-empower-host-smoke.json`。
- **c) `LD_EVAL_PROFILE empower=off|auto|<宿主>`**：Adapter 要求该值与运行开关和计划一致。

## 5. 实测结果

**通过**
- `BuildEditor`（合并删除分支与集成分支后）；`SyncDemoContentData.py`、`--check`；`BootstrapAssets -Mode RefreshDefinitions`。
- Python `TestDemoApprovedImport.py`（51 项，含 EMPOWER 键全集）。
- Verify（`-Filter`）：新增 6 项 `Lockdown.Rules.Combat.Empower.{Processors,ThresholdAndCost,HostCommand,ChainNoMomentum,Shapes,Punch}`；回归 Combat.Momentum*、DemoContent*（含导入组）、HUDActionView、HUDCombatOwnerView、ViewPrivacySchema、WorldLabels* —— 结果见 §5.1。
- BotMatch（7 AI、加速、`-ExtraArguments '-LDEmpower'`）：见 §5.2。

**未运行**
- 拥有连接的多人场景、渲染场景（S0 统一安排）；完整 `Test*Import.py` 套件；FullUnity。

### 5.1 Verify

`Verify.ps1 -Base sinmentis/b01-integration -Filter …`：编辑器构建通过；所选 29 项规则测试中 28 项通过，其中新增 6 项 `Combat.Empower.*` 全部通过，另含 Combat.Momentum.{Idle,Income,Spend}、DemoContent 8 项、WorldLabels 8 项、HUDActionView、HUDCombatOwnerView、ViewPrivacySchema。
**失败 1 项（非本分支引入）**：`Lockdown.Rules.DemoContent.CardCatalog` 期望 94 张，实际 90 张。原因是 S1 删除 Y11/D04/D09/U07 后，`LDDemoContentCatalogTests.cpp:1408` 未同步，集成分支 `8b68cf1e` 上同样存在。Python 检查（ApprovedImport、NativeFixtureImport、LocalVerification、RuleReport 等）均通过。

### 5.1b 第二轮

- **Verify**（`-Base sinmentis/b01-integration`）：编辑器构建通过，所选 34 项中 33 项通过。
  - 第二轮新增 6 项全部通过：`Lockdown.Rules.Combat.Empower.{Forms,GrabAll,Pierce,Ground,CordonTouch,ShieldBurstSerial}`（已登记 REQUIRED_TESTS）。
  - 其余通过的包括第一轮 6 项、Combat.Momentum 3 项、Combat.Skills 6 项、Cards.A05/Y06、DemoContent、HUDCombatOwnerView、ViewPrivacySchema。
  - 唯一失败仍是继承的 `DemoContent.CardCatalog`（94→90）。
- **Python**：`TestBuildEvaluationLockdown.py` 65 项 OK，含新增宿主计划用例。
- **GrowthLink 拥有连接场景**：
  - `-LDEmpower`：通过，§4.2 全部断言通过。客户端 3 次强化，权威侧扣 150，两边计数一致；Host 在 22 帧看到客户端的公开就绪标记，无未授权标记。
  - 关闭：通过，宿主请求被拒（“强化实验未开启”）。
- **修复：对手看不到普攻宿主的就绪标记**。第一次 GrowthLink 时 Host 看到的就绪帧为 0，原因是公开就绪要求普攻计时已到；持续出拳的普攻宿主几乎从不满足这一条。已改为普攻宿主只要没被缴械或眩晕即算“宿主可用”；技能宿主仍看冷却。修复后 Host 看到 22 帧。
- **BuildEvaluation 冒烟**（`b01-empower-host-smoke.json`，4 Case，开关开）：4/4 Completed。
  - 每个 Case 的 `LD_EMPOWER_HOST` 恰好一次，宿主与计划一致；`LD_EVAL_PROFILE empower=` 为 Basic、Q 或 auto，与计划一致。
  - 普攻宿主的主角每场强化 2 次。
  - Q 宿主的主角 0 次：AI 在约 12 s 的对决里只放了 1 次 Q（开战时，劲未到门槛）。这是 AI 施放节奏问题，交 S2。
  - “auto”对手实际选了普攻：AI 自动宿主打分里，本体的牌与勋章阶让本体胜过未投入的 Q，与规则一致。
  - 同一计划在 45 s 时限下关开两态都是 `NoEngagement`（接近距离 1262 cm），说明与强化无关，属既有 capture 场地的接近时长问题，已改用 90 s。

### 5.2 BotMatch

第一轮（修复前）：判定通过，873 s 对局，`LD_EMPOWER` 359 次全部 `applied`，但全部宿主为普攻。原因是 AI 判定误用“有无控制器”（AI 也持有服务器端 PlayerController），已改为按参赛者类型 `OpponentAI` 判定。

第二轮（修复后，合并集成分支）：判定通过，825 s，7 AI。
- `LD_EMPOWER` 共 157 次，全部 `applied`，无 `short`。宿主分布：普攻 117、Q 40；处理器分布：拳 122、打 30、守 5。
- 平均每名 AI 约 37 s 强化一次；就绪后平均等待（`armed_ms`）约 5.3 s/命。
- 强化链内被拦的劲共 1109（E1 生效）。带强化序号的命中（`LD_HIT empower>0`）181 条。
- 无错误，无权威故障。

结论：机制在整局中稳定触发、结算且不产生异常回劲。普攻宿主占多数，原因是 AI 身上本体卡普遍多于技能卡。

### 4.3 与 S1 战斗批次的叠加口径

- 强化追加前摇与承诺起手（C-09 B1）**相加**：承诺分支的起手已写在技能前摇里，攻类强化再加 `EMPOWER.StrikeExtraWindupSec`。落点预警（C-12 A1）与“你被锁定”（A2）都按合计后的释放时刻显示。
- 打断部分冷却（C-11 B2）照常作用于强化动作。强化只在释放提交时扣费，所以起手被打断不扣劲，只按 B2 进部分冷却。
- 公开就绪 `EmpowerReady` 仍是独立字段，没有并入 `CueFlags`：它是处理器类别（0–5）而不是一位开关，并且归劲组授权，不归动作提示组。

## 6. 与规格的偏差（实现选择，可在 B02 补）

- 拳类为单记劲拳（SPEC），不是 PARAMS 的三连拳。
- 第二轮已补 §4.1 全部七项。仍未做的 §2.3 行（多为“数值/形态加成”而非新机制，或与现有分支逻辑耦合）：
  - S06“拖鞋落在终点”：拖鞋仍在第一次命中处掉落。掉落逻辑按命中逐次读分支值，挪到飞行终点要复制一条掉落路径，收益小，留 B02。
  - S10 弹射 +2（已在第一轮，`ExtraCount`）；S18“0.6 s 后同点再落 60%”、S22“2 s 余音光环”、S19 引信与爆炸半径之外的“爆炸 +40%”、N4“爆炸 +40%”、N2“摇头”、B2“劲拳必暴”、B7“穿人定身 0.7 s”：都是在现有分支机制上再叠一层计时器或伤害加成，开关下的代表构筑已有可辨识的形态差异，按 PO“机制优先、不单纯加伤害”的口径留 B02 随卡牌迁移一并定。
  - 地面残留只按技能**自身**元素（火/水/冰）；分支或勋章给的元素不留地面，避免在审批外扩大到所有带元素勋章的技能。
- 人头刷新只防退劲（E1），不额外限制冷却刷新。
- 打类追加前摇只在 `ProcessDirect` 入列路径生效。
- `LD_EVAL_PROFILE` 已加 `empower=off|auto|<宿主>`（S0 第二轮指令 c，`AI/LDOpponentController.cpp` 仅改这一行日志）。BreakoutPresets 宿主列、“为英雄保留宿主技能”的 AI 决策仍归 S2。

## 7. 尚未解决的平衡问题

- 技能零耗后急速与 K01/A02/D02 价值上升（PO 已列 B02）。
- 门槛 60 / 消耗 50 下，BotMatch 平均每名 AI 约 37 s 强化一次。普攻宿主因普攻回劲更快，频率高于技能宿主，需按宿主比较 `armed_ms` 与频率后再调。另外 AI 宿主 75% 落在普攻：自动选宿主的打分是否应偏向技能，交 S2 与平衡评审决定。
- K08（+50% 劲耗代价消失，变为净增益）、K12（劲耗折生命变 0）、D08/D01（与强化共享劲）、脑力关键石（已关）语义变化。
- 劲收入牌（A01、K02、Y04、Y16、U16 及 13 个分支回劲）在“劲只用于强化”后价值需重估。

## 8. Batch 02 迁移依赖

- SPEC §7 全表（K08、K12、D08+D01、Y11 迁为改门槛/消耗的牌、D04/D09/U07、劲收入牌与分支回劲、急速、A01/A02、阵地内容、改变强化的卡牌、文档）。
- 逐技能补丁表（§6 偏差）。
- AI：宿主选择策略与强化感知（S2）；BuildEvaluation 按 Case 设开关与宿主（S5）。
- 开关转为默认前：APP_SPEC §3.2/§4.4/§7、CONTENT_AND_BALANCE §3.2.1/§4.2 劲耗列一次性改写；卡牌说明在开关下仍写“耗劲”的需改。

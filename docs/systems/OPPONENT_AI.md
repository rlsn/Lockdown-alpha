# 对手 AI

本页记录本局填充至 6 席的权威对手 AI 决策、构筑策略、战斗施法与公平性边界（`LDOpponentDirector`、`LDOpponentPolicy`、`LDOpponentGateway`、`LDCombatMonsters`）。

## 公平信息边界与网关约束

- **零全知特权**：每个 AI 席位只通过 `LDOpponentGateway` 读取两类合法输入——本人授权投影（自身 HP/劲/位置、冷却轨道、成长账本、待选候选）与该席位独立的 `FLDWorldView` 观察帧加 `PublicMatch` 公开信息。可见对手的施法预警按真人能看到的公开施法动作读取（`ReadCastWarnings`：直线/扇形技能前摇的施法者位置、朝向与剩余时间，轮廓取该技能 1 级定义）；视野内的清营回复球作为临时地点读入（`ReadRecoveryOrbs`）；终局读取当前街道缩圈半径与开始时刻（公开的 `LDShowdown::StreetRadiusCm`）。
- **严禁越权读取**：AI 不得读取隐藏敌人的坐标/血量/构筑、未揭示空投落点、隐藏营地的实时剩余血量或刷新倒计时（未目击营地仅能按目录基准值与当前公开野外进度阶数估算满血代价，并用 `AI.ExploreRevisitIntervalSec = 20 s` 记录自身探访冷却）。
- **同一执行入口**：AI 发出的移动、追击、攻击移动、技能释放、冲刺、成长构筑、休息室服务、PK 报名/押注/投掷、复活选点命令，全部经过与真人完全一致的导航、战斗、成长与阶段准入校验，不享受额外数值、减伤、免控或跳过前摇特权。

## 决策节拍与行为状态机（`LDOpponentPolicy`）

常规思考节拍为 `AI.DecisionIntervalSec = 0.25 s`（死亡、硬控禁动或目标失视野在事件当拍立即终止非法追击，不等下一拍）；可重试路径/服务拒绝采用指数退避（最多 `3` 次，`0.25 s -> 0.50 s`），单目标最长持续投入 `AI.EngagementBudgetSec = 40 s`。

| 阶段 / 情境 | 优先级与行动选择 |
| --- | --- |
| **死亡等待** | 立即处理未决主动/构筑牌/技能升级与加点；在复活候选窗内从 `3` 个候选出生点中选择落点 |
| **休息室（R0–R3）** | 先清空待选主动、构筑牌与 R3 终局改装，执行技能分支升级、图纸替换/学习、休息商店采购（技能点/契合词汇牌/缺槽技能）与属性加点；若当前已分配了被判定为非契合的五维点则先 `Respec` 重配；R1/R2 报名窗按血量与构筑完整度参与 PK 报名、押注与观众投掷 |
| **终局决战（FinalField）** | 街道缩圈开始后，身处当前圈外（或距圈边 `StreetRingMarginCm` 以内）时先走回圈内；其余围绕解锁后的主旗杆（或自身 `final_private_pole`）、安全圈收缩边界、阻断敌方喷漆/守点及收割残血目标决策；低血时优先寻找圈内掩体拉扯而非跑野外躺平角 |
| **定时野外（F1–F3）** | 1. **保命撤退**：`HP < AI.RetreatHPRatio`（25%）时脱离交战，视野内有清营回复球先去拾取，否则前往空投或最近躺平角，回至 `45%` 以上再返场。<br>2. **主旗杆/野外目标拦截**：根据公开喷漆/守护进度与路程预算（含 `AI.CaptureReactionBudgetSec = 2 s`）赶往打断；缺血或缺劲且未在打营时先拾取视野内 `ObjectiveReach` 以内的回复球；`HP >= 60%` 且距离 `<= 2500 cm` 时争夺开放或预告中的空投、小迷你旗、中心擂台、外卖柜与标记营地。<br>3. **遭遇战与热搜狩猎**：视野内出现可战胜敌方参赛者或等级差 `<= 2` 的热搜目标时发起连招追击。<br>4. **打野发育**：按自身当前属性与保守站桩模型评估小野（余血 `>= 15%`）与中野/大野/远古（入场 `>= 80%`、预计余血 `>= 35%`）路线清营。 |

## 行走与绕行

野外地面行走在提交时按真实注册路径检查：穿过可见敌方地面状态时比较绕行代价；路径前 `BodyLookaheadCm`（1200 cm）内撞上可见营地成员（目录碰撞半径加自身胶囊半径）时改走该成员两侧的绕行点。保持中的行走在附近有营地成员时每 `BodyRerouteIntervalMs` 重查一次。找不到绕行（营地与地形把通道封死）时，控制器把挡路成员写进决策帧，策略（含构筑评估的接近阶段）先打穿它。`LD_AI_BODY_DETOUR` / `LD_AI_BODY_BLOCKED` 记录绕行与受阻原因。

## 躲避预警

视野内的敌方预警（地面延时落点、陷阱、营地前摇，以及公开施法动作的直线/扇形前摇）覆盖自身脚下、且离落地不超过 `DodgeLeadMs`（450 ms）时，AI 用就绪的 D 冲出轮廓（直线与扇形向侧面，圆形向外）。生产 AI 对每个预警按 `AI.DodgeChance` 做一次稳定判定；构筑评估的 `warning_react_ms` 去掉判定，改为看见后等满反应时间。动作进行中（前摇、引导）不打断。`LD_AI_DODGE` 为每个覆盖过自身的预警记一行：`result=dash` 时 `remaining_ms` 是发出冲刺时的剩余时间；`result=none` 时 `reason` 是拦下冲刺的原因：`acting`（动作进行中）、`dash_unready`（D 未就绪）、`reaction_wait`（评估反应时间未到）、`off`（评估未开躲避）、`roll`（生产判定未中）、`retry_blocked`（失败记忆暂停）、`early`（没有决策落在窗口内）或 `left`（落地前已离开轮廓）。

## 构筑、分支与施法覆盖

- **26 主动与 7 种目标模式全覆盖**：`LDOpponentPolicy` 按 `ELDDemoTargetMode`（`Self`、`Unit`、`Direction`、`GroundPoint`、`Cone`、`Line` 及通用 `Dash`）与技能角色（控制、护盾/解控、突进、爆发、持续区、召唤/墙体）生成瞄准点、施法时机与满 60 劲的爆劲（Overdrive）释放决策。技能形状按已学分支的变体读取（分支可改目标、元素与效果）；自身嘲讽在半径内对敌方参赛者作为控制施放（不只用于打断喷漆）；自身护盾/格挡在低血保命之外，也在敌方参赛者进入普攻射程且无更优技能时施放。
- **质变目标（`DrawBreakoutGoal` / `BreakoutProgress`）**：每个 AI 在第一个构筑步前按 `AI.BreakoutChance` 抽一套 AI 可用的质变样板（`LDBreakoutPresets`，不含 DEV 与评估专用的地雷阵和抽裤腰带）作为本局目标；对局代数轮换样板顺序，房间名单里的每个座位取下一套，所以同房 AI 目标互不重复。目标技能在主动三选一、图纸学习与休息室替换中优先于其他偏好；技能点先给未到目标分支的目标技能并沿目标分支走；样板指定的属性（攒个大的：力道，刺猬肉山：脸皮）成为第一关键石且不会被洗点；目标里的牌按 `AI.BreakoutCardScore` 加分。`LD_AI_BREAKOUT_GOAL` 记录抽到的目标，`LD_AI_SUMMARY` 的 `goal_skills`、`goal_cards` 记录局末达成的目标技能（到目标分支）与已装目标牌数。
- **构筑牌、元素勋章与终局改装**：优先选择与已学技能宿主形态/词汇匹配的构筑牌并装入容量格（库存中最契合的先装，勋章优先且不占容量）；容量满时若库存牌比已装最不契合的牌更契合（与已学技能共享词汇、目标样板牌加分），先卸下后者再装入；拾得元素勋章后装入空元素槽；R3 从候选终局改装中选定一项。质变样板（`LDBreakoutPresets`）自带满级全部合法属性点分配，DEV 与构筑评估不再依赖之后由谁加点。
- **五维加点与双关键石聚焦（`RelevantStats` / `AllocatePoints` / `WantsRespec`）**：
  - 综合已学主动的当前 1–3 级分支效果（`ADRatioBp`、`APRatioBp`、`AdaptiveRatioBp`、`MaxHPRatioBp`、`ArmorRatioBp` 及强化普攻效果）与已装构筑牌词汇（普攻/三段/攻速/暴击、冲刺机动、施法元素、生命/护甲）评估体格、脸皮、力道、脑力、身手五维权重。
  - `stat_agility`（身手）全程开放且不会被 `WantsRespec` 误洗点；普攻/攻速/暴击向构筑优先聚焦身手与力道，先点满第 1 个 18 点关键石后立即聚焦第 2 核心属性冲第 2 个 18 点关键石，双关键石成型后再均衡剩余相关属性。

## 检查入口

- 纯规则：`Lockdown.Rules.OpponentPolicy*`、`Lockdown.Rules.DemoOpponentPolicy.*`（含 `BodyDetours`、`CastWarnings`、`StreetRing`、`RecoveryOrb`）、`Lockdown.Rules.CampDecision*`。
- 拥有连接场景：`Scripts/RunDemoOpponentCheck.ps1`（含满房 AI 自主任命、成长、清营、战斗与喷漆守护）。
- 机器人对局：`Scripts/RunDemoOpponentCheck.ps1 -BotMatch`（满房 AI 整局跑完、无报错、每个 AI 都在成长，只判不变量；见[验证手册](VERIFICATION.md#机器人对局)）。

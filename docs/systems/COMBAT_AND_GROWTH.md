# 战斗、效果与成长

本文说明可复用的原生结算链。Demo 的属性与五维点数、主动机会、Q/W/E 三个主动槽、构筑安全窗和死亡保留规则来自
[App Spec](../APP_SPEC.md)，不能靠修改文档或目录标签宣称已经实现。

## 当前组件

| 入口 | 责任 |
| --- | --- |
| `Combat\LDCombatDirector` | 唯一 Bodies/P2 资源记录与按实体保留的 Combat 记录（构筑投影、来源冷却轨道、普攻节拍、Direct 请求窗口）；真人、对手 AI 与营地成员共用的动作入口；对外只读身体状态 |
| `Combat\LDCombatDriver.cpp` | 唯一权威 driver：按真实到期点切步，按契约顺序执行各阶段，步后发布视野与本人 `CombatView` |
| `Combat\LDCombatActions/Effects/Sources/Life.cpp` | 准入/释放/回执；命中、投射物、状态到期、回复与致死锁存；构筑安装、来源绑定与可用性读模型；身体准备/安装/退役 |
| `Combat\LDTacticalRules` | `EvaluateDamage` / `EvaluateBasicTiming` / `CooldownDurationMs`，以及 Root/屏障/净化/定点回复纯规则；不提交资源或产生效果 |
| `Combat\LDControlRules`、`Combat\LDCombatControl.cpp` | 嘲讽（`taunt` 效果）的纯规则与在 Combat 身体记录上的应用；见下文“嘲讽” |
| `Combat\LDAttributeProjection` | 已批准构筑输入的属性与贡献投影、一次最大值变化的精确资源比例 |
| `Framework\LDGameModeLifecycle` | 唯一生命/身体绑定、借用许可、当前时间、原生运动前缀与只读发布 |
| `Framework\LDGameModeLife.cpp` | 阶段顺序与许可分派、生命结束/十点复活/保护、出生落地与营地整组生命提交 |
| `World\LDCampDecision` | 仅参数化营地决策；不读 Actor、不提交移动/攻击/奖励 |
| `World\LDCampLifecycle`、`World\LDCampDirector` | 营地名册与每局记账；服务器营地 Director 经 Combat 驱动阶段入口生成野怪、推进运动、提交普攻、消费受击/死亡事实并做回营重置与整组刷新，奖励只由成长 sink 支付 |
| `Development\LDDevelopmentDirector`、`LDProgressionRules` | 经验/等级、派生点数、持久主动候选组、待学图纸与 Q/W/E 的权威成长记录；构筑变更经 Combat 准备与安装 |
| `Development\LDGrowthDirector` | 死亡与地图事件奖励的复合提交、Transactions 分派与本人发布，见 [成长事务](GROWTH_TRANSACTIONS.md) |

这些入口位于 `Source\Lockdown\Public`/`Private`。Demo 运行输入沿
`SourceData\Demo`、`SyncDemoWorldData` / `SyncDemoContentData` 和原生不可变目录生成链读取。
属性、技能与物品只来自 Demo 目录。

## 从意图到结果

UI 只能准备或发送动作，不直接扣蓝、改 HP、增加效果或确认命中。
服务器验证 match/life、序列、真实动作来源、当前绑定、资源/冷却、状态与目标几何。
单位目标必须重新解析观察句柄，不能依赖预览时的旧权限或隐藏 Actor 指针。

预览、接受、释放、命中和结束是不同阶段。正式资源消耗与实际释放必须遵循同一提交边界；
失败要明确，不让客户端通过同时点击技能图标和快捷键产生两次效果。
已释放的效果是否继续存在由其生命周期决定，不通过销毁 UI 或更换槽位退款。

普攻、Q/W/E 与 D 冲刺经同一 `SubmitAction` 入口（真人 RPC、对手 AI 与营地成员各自的服务器 Caller）：
本人/形状/请求窗口先验，进攻意图随即经生命域解除保护，再验来源、冷却、蓝量、控制、目标与几何；
未准入不付费，已解除的保护不恢复。技能释放时重验来源与目标（普攻释放见下文 `BasicReleaseGate`），成功才扣蓝并按 `FORMULA.CooldownMs`
写入该来源的持久冷却轨道（每个来源独立，失源、换槽、死亡都不刷新）。
冷却、蓝量与定身对位移的禁止由纯函数 `LDCombatSourceRules::ActiveGate` 判定，准入、释放和 owner view 共用同一顺序。
D 冲刺（`ELDCombatEntry::Dash`）是参赛者的 `Innate` 来源：`ResolveEntry` 给出 `LDCombatSourceRules::InnateDashId`，
`ActiveOf` 取导入时投影成单级 DirectionDash 主动的 `Character.Dash`。普通D不是进攻意图，不解除保护；
构筑带 `dash_contact_damage` 触发项时（#198 第一部分的目录没有来源，留给后续基础构件），D会沿真实移动轨迹产生撞伤，准入前按进攻意图解除保护。
每名参赛者在 Combat 记录创建时签发唯一一条 `Innate` 轨道（开局就绪），构筑变更既不规划也不解绑它。
释放时 `LDCombatSourceRules::InnateDashTerms` 只读目录的 `Character.Dash` 距离与冷却（#198 删除了 `dash_boost`），
冷却不吃技能急速；结束时刻写成绝对 ReadyAt，之后换装或学习不改写。
已接受但未释放的 Direct 动作因死亡、取消、打断或营地回位而结束时，回执落为 Rejected 并带原因。
Root/屏障/Root 净化/定点回复的规则是 `LDTacticalRules` 中的纯函数：
Root 按效果实例保存、同实例不续期、禁止 `POLICY.RootBlocks` 所列动作；屏障为 ReplaceIfNotWeaker，
不继承旧上限或严格大于判定；回复按 CarrySubP2Remainder 积分。
嘲讽：`taunt` 效果在释放时让施放者周围半径内（脚点平面距离，含边界）存活、无复活保护、
且此刻能观察到施放者的其他参赛者进入被嘲讽（野怪不受嘲讽）；同一身体只保留最新一次嘲讽；嘲讽落地是一次控制（`LDControl::TauntVictim`）。被嘲讽期间
Q/W/E 与 D 冲刺在准入、释放与 HUD 可用性中都经 `ActiveGate`（`bTaunted`）以 `ControlBlocked` 拒绝，普攻只能以嘲讽者的那条生命为目标；
已在前摇、被嘲讽所禁止的动作当场放弃。每个 Actions 阶段开头 `RefreshTaunts` 结束过期、嘲讽者死亡/离局、
或被嘲讽者已观察不到嘲讽者（视线或距离）的嘲讽；离局另由 `CancelEntityEffects` 清除。
订单方面由 Navigation 强制（见 [移动、输入与镜头](MOVEMENT_INPUT_AND_CAMERA.md)）。
嘲讽只出现在本人 `CombatView` 的状态里（`Taunt` 及到期），不进入他人可见的公开状态。
同一身体正在普攻前摇时，准入的技能会放弃这次普攻并开始施法；另一技能前摇未出手时再提交技能被拒绝（`Busy`）。
普攻释放（`ReleaseBasic`）不重查距离，只经纯函数 `LDCombatSourceRules::BasicReleaseGate`：攻击者自身
被缴械/眩晕/击退或被嘲讽转向他人时放弃（`ControlBlocked`），目标那条生命已死亡或濒死、离局、换命或换身体时放弃
（`TargetUnavailable`），参赛者攻击者重新观察不到该生命时放弃（`TargetUnavailable`），视线被挡时放弃（`Blocked`），
否则命中起手时提交的目标生命（2026-11-07 用户决定：“只要起手就判定一定会打中，即使跑远了”；可见与视线检查按
2026-11-07 后续用户决定 "let's add back the visibility and line of sight check" 恢复）。
导航对同一观察目标重新下达的 AttackUnit 经 `ALDCombatDirector::AdoptNavAttack` 把未出手的普攻改挂到新订单，
不因替换订单作废已提交的节拍；其他替换、S 与订单结束仍经 `CancelNavAttack` 放弃它。
`FLDCombatBodyRead::ActionReleaseAtMs` 给出前摇中动作的释放时刻，供导航判断本 Actions 阶段即将出手的普攻已不再占住身体。
拥有客户端的预输入（`LDCombatPreInput`）只在本地暂存前摇期间的一次按键并在前摇结束后作为普通 Direct 请求发出，
服务器规则不因此改变；快速施法同样只是本地确认方式，见 [移动、输入与镜头](MOVEMENT_INPUT_AND_CAMERA.md)。

## 权威时序

`AdvanceAuthority(const FLDMatchContext&, int64 ThroughMs, FString&)` 在 `Combat\LDCombatDriver.cpp`
中逐步推进：步终点取可用时间、原生最大步长和最早的真实到期点（释放、状态到期、冷却、
普攻节拍、脱战回复起点、保护到期、复活时刻、营地整组刷新、旗杆开放、喷上涂鸦与守护完成时刻）。每步依次执行 Motion → Session
（保护半开到期）→ Actions（先结算到本边界的回复）→ Effects → DeathsAndRewards → RespawnsAndWorld →
Transactions → Capture → Close 和完成边界，仅推进原生游戏时间已经提供的预算；
Close 锁定结果后仍完成该边界，随后无 permit 的发布依次调用成长 `PublishGrowth` 与 `ExecuteCapturePublish`，然后停止推进。
DeathsAndRewards 先结束本集合全部生命，再逐个调用成长 `CommitDeathReward`：可重试的拒绝只在下一边界再试一次，
其余未支付结果明确报错并故障。待重试奖励存在时结果不能锁定：同一边界的 Capture 若恰好完成，Close 保留提案，
driver 立即以同刻结算边界（`BeginSettlementStep`：新 BatchSerial、不推进时间，只有 DeathsAndRewards 重试与 Close）
先结清奖励再锁定原完成时刻的胜者；只有故障关闭写闸时奖励才记录为未支付（`LD_REWARD_UNPAID_AT_FAULT`）。
Transactions 每步调用一次 `ProcessTransactions`。
生命变化（Joined/Respawned/ProtectionEnded/Died）在各自阶段向喷漆与守护归约器提交资格事实并通知提供者。
控制在提交时通知喷漆区：唯一控制入口 `CommitControl` 每实际提交一次定身、眩晕、沉默、嘲讽或击退，
都在同一 Actions/Effects 许可内调用 `InterruptSpray(Target)`，再经 `ALDGameMode::PublishCaptureInterrupt` 清零该参赛者的喷漆进度
（事实时刻为该边界 AtMs；营地身体或没有活动归约器的世界不受影响）；缴械、减速和伤害从不调用它。
被击退出喷漆区由随后的原生运动作为普通离区事实进入归约器。端到端规则见 `Lockdown.Rules.DemoCapture.CombatHitInterrupt`。
原生 CMC 每身体每步只执行一次；Owner 显示时钟只复制完成步，视野在本次 driver 推进完成后采当前姿态并发布。
`PublishAuthority` 失败返回同一 driver，由 Core 进入 Fault，而不是保留一局失去视野服务的正常比赛。
营地成员在同一分派中接入：Motion 调 `AdvanceMemberMotion`；Actions 结算回复后先调 `ProcessAuthorityIntents`，
本步野怪请求随即与 Direct 请求一同准入；Effects 把营地成员受到的已提交敌对命中（伤害或控制）作为 `OnCombatHostileContact` 交给营地；
DeathsAndRewards 依次 `EndCampMemberLife` → `ConsumeMemberDeath` → `CommitDeathReward`；RespawnsAndWorld
在参赛者复活后调 `AdvanceCamps`。初始出生在同一开局许可内、参赛者 Joined 之后调用 `SpawnInitialCamps`。
DEV 测试会话的 `DevRefreshCleared`（Transactions）只把等待中的刷新截止提前，实际重生仍由下一个 `AdvanceCamps` 完成。
任一营地入口返回 false 即为权威故障。

修改这个顺序会影响同时死亡、资源释放、命中与喷漆/守护完成的同刻竞争，必须连同对应场景验证。
喷漆、守护的归约与结果归 GameMode 核心的喷漆与守护归约器（`Framework\LDCaptureReducer`）。

## 状态、伤害和效果归属

HP/MP/盾只在 `Combat.Bodies.Resources` 中以 P2 整数保存，Owner 的点数用于显示。
伤害求值先完整验证定义、来源值、抗性及算术，再处理保护、盾吸收、实际 HP 损失和过量伤害；
只有 `HPLostP2` 是实际生命损失，不能把盾吸收也当作吸血依据。求值不读 Actor、时钟或随机数，
也不代替拥有 permit 的命中/死亡提交。
`ReadObservedPublicFacts` 只读当前已绑定身体上明确提交的公开构筑/状态投影。
复活保护仅由 Alive/current Body 且 Core NowMs 小于 Lives.ProtectedUntilMs 派生
`RespawnProtection` 的单层状态及精确期限，不存成可驱散的 Combat buff，不在读取时补默认等级；
命中点以同一生命状态做统一保护闸（免伤免控，群攻副目标排除保护者）。
公开 Root/Shield 提示同样在读取时由当前 Combat 状态派生，不另存一份可漂移的公开状态。
动作提示由 `LDCombatCues::Select` 按 Cast > Hit > Attack 选出（普攻中被打的身体仍显示受击），
`AppendObservedAction` 给出提示及其开始时刻，本人身体按同一规则与公开时钟取提示与年龄。
提示为 Cast 时，`ObservedCastSource` 由前摇中的动作或最近释放的动作（`ReleasedSource`）给出施法技能的原生 id，
写入公开事实的 `ActionAbility`（Q/W/E），其他提示为 0；
D 冲刺是人人都有的移动，`ActionAbility` 为 0（只显示施法动作，不飘名称）；
来源没有批准定义时 `ReadObservedPublicFacts` 失败，不发布 0。观察者据此只显示施法名，不显示等级、冷却或技能列表。
身体表现只按 cue 字节、动作年龄和状态位绘制（`ULDHumanoidVisualComponent`，时间轴为纯函数 `LDBodyMotion`）。
身体是美术目录（`ULDArtSubsystem`，`DA_DemoArt`）给出的骨骼角色：参赛者按座位色给目录 TintSlots 的 `Tint` 着色，营地两档各用自己的模型；
动画在 C++ 中单节点播放（无 AnimBP）：站立/跑动（速率 = 速度 / 目录参考跑速，低于 15 cm/s 站立），普攻与施法片段的出手帧（`ImpactSec`）
对齐到 0.2 s 的出手时刻，受击按目录速率，一次性片段每帧按呈现时钟定位；目录缺少身体时记录 `LD_BODY_ART_UNAVAILABLE` 且不画替身。
受击闪白转红与出现辉光走材质 `Flash` 自发光；
普攻整体前冲并在出手的手部闪光；施法时手部聚能球；受击整体后退、胸前闪光，本人镜头轻微震动；
状态标记对所有单位使用固定状态色，与六个座位色、草坪和红色跑道都拉开：定身为深色描边上的脉动品红圆盘加品红脚镣，
护盾为旋转的亮青色栅栏罩加光环，复活保护为白金光柱加环绕光球；护盾与复活保护的每个标记背后沿本机视线放一个稍大的
近黑副本作为描边，在同色系座位（青色、黄色）身上也能分开；
带复活保护首次出现时由小变大并有光柱；死亡由不复制、无碰撞的 `ALDBodyPuppet` 在原地播放死亡片段倒地，再下沉、缩没
（本人身体须等自身投影确认该命已死，终局或离局移除身体不播放）；甩你一鞋（`active_snark_bolt`，唯一的弹道技能）的飞行由 `ULDFlightTrailComponent` 拖出粉尘。
驱散不等于回滚伤害，也不等于永久禁止持续来源重新施加效果。
**助攻贡献**（#112 §8）只有一个记录入口 `ALDCombatDirector::RecordAssistContribution(Victim, CreditOwner, NowMs)`：
受害者是参赛者、CreditOwner 是另一名有 Combat 记录的参赛者时，按纯规则 `LDAssist::RecordContribution` 在受害者这条生命的
`FLDBodyCombatState::AssistContributions` 里刷新其最近贡献时刻（锁定死亡后不再记录，换命随身体消失）。
`ApplyHit` 在提交后、与受击提示同一条件（正伤害含被护盾吸收的部分、定身/缴械/眩晕、减速）调用一次，
`ApplyTauntEffect` 对每个被嘲讽者调用一次；新增的控制与减益在其施加成功处调用同一入口，不另建记录。
`LockDeath` 只在击杀者合格时以 `LDAssist::SelectAssisters`（`REWARD.AssistWindowSec` 窗口含边界，排除击杀者、受害者与已离局者）
把排序后的助攻者冻结进 `FLDCommittedDeath::Assisters`，之后不随离局或升级改写。
护盾、普通抗性和条件修正应走统一伤害路径，不能由某个被动在末尾再加一层未经约束的减伤。

临时属性只有一条投影：身体保存构筑投影（`BuildProjection`）与当前生效投影（`AttributeProjection`），
`LDAttributeProjection::ApplyTemporary` 用同一公式在构筑项上叠加限时增益、低血条件增益（`low_hp_stat_bonus` 触发项，当前目录没有来源）与最强一个减速
（负比例），点数类向下取整并夹紧到目录上下限；生命上限/法力上限不接受临时项。`RefreshAllTemporary` 在 Actions
（回复结算后）与 Effects（命中后）各执行一次，到期、血线越界与施加都在下一次运动前生效；`NextDueMs` 包含这些截止时刻。
减速与限时增益的纯规则在 `LDTacticalRules`（`ApplySlow`、`StrongestSlowBp`、`DispelSlows`、`ApplyTimedBonus`、`TemporaryTerms`）；
减速只减移速，按效果实例保存，只取最强一个。破防与灼烧见下文“构筑 v7 战斗原语”。
伤害系数为基础值、`ad_ratio`、`ap_ratio` 与 `max_hp_ratio`（施放者生命上限，`FLDDamageDefinitionValues::MaxHPRatioBp`），同在抗性前相加；普攻不带生命上限系数。技能命中载荷在释放时快照（含施放者 AD/AP/生命上限，以及按施放者当前等级成长后的数值，见“技能等级成长”）：指向单位、弹体或 `self_area`（释放瞬间脚点为圆心、与横扫共用 `CollectAreaTargets`
的逐目标合法性与视线检查）各得一份；控制与减速随命中经 `CommitControl` 施加。
施放者离局时 `CancelEntityEffects` 一并移除其减速、灼烧层、区域与路径扫掠，营地回位清空全部临时项。

### 控制、强化普攻与反伤

纯规则在 `Combat\LDCombatControlRules`（`LDCombatControl`），Combat 的准入、释放、命中与读模型都只经它判断：

- **动作禁止矩阵**（`Blocks`）：定身按 `POLICY.RootBlocks` 禁移动与位移（冲刺）；缴械禁普攻；沉默禁 Q/W/E；眩晕禁移动、位移、普攻与
  技能。缴械/眩晕实例与定身同形（`FLDCombatRoot`：效果、来源、半开到期），存于身体的 `Disarms`/`Stuns`，
  同一效果不续期，换命即消失，营地回位与离局清除。命中时眩晕放弃任何未出手的动作、缴械放弃未出手的普攻（回执 `ControlBlocked`，
  不付费也不退节拍）；受控期间的 Direct 请求（真人、AI、营地同一入口）在准入时被拒，`ReadCombatBody` 的
  `bAttackAllowed`/`bMovementAllowed` 同步关闭，导航攻击指令因此原地等待而不重复提交；出手时再判一次。
  本人视图给出 Disarm/Stun/Empowered 状态及其 `ControlBlocks`，`bRooted` 表示“移动被禁”（定身或眩晕）；
  观察者看到公开的缴械、眩晕。
- **强化普攻**（`empower_basic`）：释放时武装一次（同来源替换不叠加），由接下来已提交命中的普攻逐次消费（`ConsumeEmpower`，
  `Charges` 次），额外伤害在暴击采样前并入该次 `BasicPrimary`（吃暴击、吸血、横扫与命中特效）；到期在 Effects 阶段移除。
- **反伤**（`thorns`）：构筑触发项，经 `CollectPassiveEffects` 解析（#198 第一部分没有来源，处理器留给后续基础构件）。
  命中后持有者仍存活、伤害为正、命中是主普攻、距离不超过半径，且该来源对同一攻击者已过 `IntervalMs` 时（`ThornsTriggers`），
  生成一个 `bReflect` 的技能类命中，伤害=基础+护甲×比例+生命上限×比例
  （`LDCombatControl::ThornsBaseP2`，被暴击时乘 `CriticalMultiplierBp`；基础值按拥有者等级成长），可附只减移速的减速（`slow`/`slow_duration`），
  走同一伤害、归属、死亡锁定与奖励路径；反伤命中不再触发反伤，也不暴击、不吸血、不触发命中特效。

### 共用战斗原语（#112）

- **唯一控制路径** `ALDCombatDirector::CommitControl(Victim, FLDControlApplication, NowMs, bApplied, Error)`：
  `ELDControlKind { Root, Stun, Disarm, Taunt, Knockback, Slow, Silence }`。硬控（定身、眩晕、缴械、嘲讽、沉默）先经
  `LDCombatControl::DiminishHardControl`（`FLDHardControlChain`：100%/60%/30%，最短 min(200ms, 原持续)，
  窗口为最晚硬控结束后 2 秒）；只有实际落地的控制才推进计数与修订。嘲讽只作用于参赛者。击退（不递减）在本函数内提交其禁动窗口
  （`Knockbacks`），放弃未出手的动作并递增 `KnockbackSerial`；位移本身由 `ApplyKnockback` 在提交成功后启动（见下节）。
  每次实际提交的定身、眩晕、沉默、嘲讽、击退在此调用喷漆打断，每次实际提交的控制与减速在此记录助攻贡献。
  `IsImpaired(Body, Now)` 判断任何控制（含击退、沉默）或减速（`control_damage_amp` 的“被减速或被控制”）。
- **命中特效**：`FLDCombatHit::OnHitScaleBp` 是该命中的特效系数（普攻主击 10000，技能按定义，附加伤害/反伤/副击为 0）；
  `SnapshotOnHit(Body, ScaleBp, Hit, Error)` 在释放时快照吸血、横扫等来源，`ApplyHit` 内 `ApplyOnHitEffects` 按系数提交。
  `bAddedDamage` 的命中不暴击、不触发命中特效；`AddedHit` 是附加伤害命中（折叠椅第 3 击、路径减速）的唯一构造。
- **伤害求值顺序**（`ApplyHit`）：加成池 `FLDDamageEvaluationInput::DamageBonusBp`（#147 D21：`ControlAmpBp`，被减速或被控制，
  `ControlAmpBelowBp` 时低血目标也算）乘一次，普攻暴击 ×`STAT.CritMultiplier` 在池前；技能不暴击（#198 删除了技能暴击、斩杀、
  暴击减伤与灼烧增幅）→ 破防后的抗性（`LDCombatKeywords::BreachedResistanceP2`：共享破防层的固定值）→ 护盾 → 生命；
  吸血（命中特效吸血×系数及其低血加成、低血条件项的普攻吸血）相加后经 `LDCombatKeywords::CapLifestealBp` 夹到
  `COMBAT.LifestealCap`，按 `HPLostP2` 经 `HealBody` 回复；灼烧不吸血。`LD_HIT` 的 `amp_bp` 是池与倍率合起来的系数，末尾 `pool_bp` 是加成池。
- **剩余冷却缩减**：`ALDCombatDirector::ReduceCooldown(Entity, ELDCombatEntry, AmountMs)` 与按定义查找槽位的
  `ReduceLearnedCooldown`，都经纯函数 `LDCombatSourceRules::ReduceReadyAt`（不早于当前时刻；`MAX_int64` 表示立即就绪；
  已就绪轨道不变）。
- **位移判定**：`LDCombatSourceRules::DisplacesCaster` 按条目的数值判定它是否位移施放者（冲刺类主动）；
  `CastRangeCm` 统一给出技能施法距离：目标定义的射程，加上目标定义 `AttackRangeRatioBp` 允许的额外普攻距离份额（有上限；当前目录没有条目使用）。
- **触发效果查找**：`CollectPassiveEffects(Projection, Op, Out, Error)` 是构筑触发效果（反伤、吸血、`mana_on_hit`、`cooldown_refund`、
  `nth_basic_bonus`、破防/灼烧触发、`triggered_shield`、`shield_break_retort`、`primary_guard`、`low_hp_stat_bonus`、冲刺撞伤）的唯一读取口。
  #198 第一部分删除了被动与属性卡，它目前不返回任何来源；这些处理器保留给后续的基础构件，在已批准内容使用它们之前不会出现在对局里。
  #198 删除的效果（`cleave`、`dash_boost`、`ability_crit`、`crit_damage_reduction`、`path_slow`、`zone_root`、`cast_frenzy`、`affliction_amp`、
  `hit_haste`、`execute`、`pierce`、`echo`、`taunt_shield`、`dispel_disarm`、`cower`、`argument_chain`、`spell_sustain`、
  `dash_contact_guard`）连同处理器与状态一起删除，编号不复用。


### #112 新机制（`LDCombatMechanics`、`LDCombatMechanicsDirector.cpp`）

纯规则在 `Combat\LDCombatMechanics`（护盾打破判定、击退方向、区域脉冲时刻），
Combat 身体记录上的应用在 `LDCombatMechanicsDirector.cpp`；使用它们的条目与数值见[内容与数值 §4–§5](../demo/CONTENT_AND_BALANCE.md#4-技能状态和属性目录)。

- **击退**：`ApplyKnockback(Target, Body, Hit, Now, bApplied, Error)` 先经 `CommitControl(Knockback)` 提交窗口，再用与冲刺相同的
  `ULDCharacterMovementComponent::StartDemoDash` 沿地面推开（遇阻停在最远合法点）；方向由 `KnockbackDirection` 从施放点（命中的
  `KnockbackAnchorCm` 或来源脚点）指向目标脚点。窗口内 `IsActionBlocked` 拒绝移动、位移、普攻与技能；Navigation 看到
  `ReadKnockbackSerial` 变化即放弃当前订单（`ELDOrderFailure::KnockedBack`，AI 视为 Dropped）。公开状态 `knockback`。
- **护盾入口**：所有护盾（技能、受伤触发护盾、按嘲讽人数的护盾）只经 `GrantShieldTo` 以 `ReplaceIfNotWeaker` 安装；
  `ShieldGrantedP2` 记录当前记录的获得量。被伤害打空（`LDCombatMechanics::ShieldBroken`，到期或被消耗不算）时触发反击
  `OnShieldBroken`（`shield_break_retort`，内置冷却 `RetortReadyAtMs`）与受伤护盾的爆炸 `BurstBrokenShield`（当前目录都没有来源）；
  这些命中带 `bFromShieldTrigger`，不算 Q/W/E 命中。
- **治疗入口**：一次性治疗（吸血）经 `HealBody`；自然与脱战回复不经过它。没有治疗削减。
- **区域**（`zone`，脚底开锅）：目标 `GroundPoint` 的请求带服务器验证的地面点（`ELDCombatTargetKind::Point`），释放时夹到目标射程，
  `ReleaseMechanics` 在该点登记 `FLDCombatZone`（载荷在释放时快照）。`zone_damage` 按 `NextPulseAtMs` 的定次脉冲结算伤害；
  `zone_slow` 每 `IntervalMs` 判定一次，给区内敌人施加持续 `DurationMs` 的减速（不再有 linger）；`zone_burn` 每 `interval` 给区内敌人加灼烧层。
  `zone` 的 `count` 是脉冲次数，从释放时刻起每 `interval` 一次（含 +0）：脚底开锅（3 次、间隔 1 s、时长 3 s）在 +0/+1/+2 各脉冲一次，
  每次的 `zone_slow` 持续 0.5 s（`Lockdown.Rules.DemoBuildPrimitives.ZonePulses`）。
  区内目标与横扫一样经 `CollectAreaTargets`（含边界、要求视线、排除保护者）。
- **夹取**：所有属性最终统一夹到目录上下限（不再拒绝构筑；只有带 DEV 测试项的属性超过上限时拒绝，DEV 项不靠夹取），内容验证 `BuildEnvelope` 只检查极值下伤害/护盾不溢出。
- 离局 `CancelEntityEffects` 一并移除其区域、击退与灼烧层；营地回位清空全部。

周期伤害（区域脉冲、灼烧 tick）按原计划的到期点和受害者 life 结算；晚进入区域不重新领取整段总伤害。
释放者、资源预算所有者、当前伤害归属和受害者生命是不同概念，
反射、死亡或替换来源不能重建免费预算或让旧投射物击中新 life。

付费施法必须先保证分配与来源合法，提交时一致消耗；
不得出现已扣资源但静默没有效果，或多个来源对同一额度重复返还。
表现层不得成为第二个伤害、碰撞或效果计时权威。

### 构筑 v7 战斗原语（#147，`LDCombatKeywords`、`LDCombatKeywordsDirector.cpp`）

纯规则在 `Combat\LDCombatKeywords`（共享破防与灼烧、吸血上限），
Combat 身体记录上的应用在 `LDCombatKeywordsDirector.cpp`。关键词参数是内容参数 `COMBAT.Breach*`、`COMBAT.Burn*`、`COMBAT.LifestealCap`
与 `SKILL.GrowthPerLevel`、`SKILL.GrowthPerLevelAfter10`（`FLDKeywordTerms::From`）。#198 目录有 22 个单级主动、没有被动，字段用法见
`FLDDemoEffectDefinition` 的注释。效果触发 `ELDDemoEffectTrigger` 只有 `None`、`BasicHit`、`AbilityHit`、`Damaged` 四种
（反伤的 `trigger` 是它自己的字段，只接受 `Basic`）。

- **沉默**（`silence`，`ELDControlKind::Silence`，状态 `silence`）：实例形同缴械/眩晕，存于 `Silences`；矩阵动作 `ELDCombatControlAction::Skill`
  只被沉默与眩晕禁止。Q/W/E 的准入、释放与 `EntryView` 都经 `IsCastBlocked(Body, SourceKind)`（Cast 对所有入口，Skill 只对 Learned），
  D 冲刺不受影响。`CommitControl` 中它是硬控（递减），落地时放弃前摇中的 Learned 技能（不付费），并清零喷漆（D10）。
  本人视图状态带 `SilenceBlockBits()`（Cast），观察者看到公开状态；对手 AI 在沉默期间把 Learned 来源视为未就绪，并把带沉默的技能当作可打断喷漆的控制。
- **共享破防**（`breach`）：`FLDBodyCombatState::SharedBreach`，所有来源同一组，每层固定 `COMBAT.BreachPerStack`，最多
  `COMBAT.BreachMaxStacks`，新层刷新整组（满层只刷新）；作为额外固定值进入 `BreachedResistanceP2`；来源离局层保留；营地回位清空。
  显示为 `exposed`（破防）带层数。
- **灼烧**（`burn`，#198 起为火元素）：`FLDBodyCombatState::Burn`，**每层独立计时**：施加时起 `COMBAT.BurnSec`、每 `COMBAT.BurnTickSec`
  一跳（最后一跳落在本层截止时刻），每层记住施加者并快照其**等级与法强**（`FLDCombatBurnLayer::Level/SourceAPP2`）；
  `AdvanceBurns` 用 `LDCombatKeywords::PopBurnTick` 取最早到期的一跳，同一时刻、同一施加者与来源的层合并为一个魔法 tick 命中（`bBurnTick`、`bPeriodic`：
  不暴击、不触发命中特效、不吸血，带施加者存活时的受控增伤）；满层时新层替换最早结束的一层；施加者离局移除其层，营地回位清空。每层每秒
  `LDCombatKeywords::BurnLayerPerSecondP2(Level, AP)` = G(L施加者)·`COMBAT.BurnPerSec` + `COMBAT.BurnAPRatioPerSec`×法强，
  G(L) 取 `LDSkillGrowth::FactorBp`（施加者等级由 `GrowthLevelOf` 给出，营地身体按 1 级）。被反应消费时 `BurnRemainingShares` 给出各层尚欠的跳数之和。
- **施加途径**：破防的 `stacks` 随命中载荷，触发项形式（`basic_stacks` 普攻主击与 `stacks` Learned Q/W/E 命中，`Token≠0`）在
  `ApplyKeywordStacks` 中按存活施加者结算；火（命中载荷的 `BurnStacks` 与触发项形式的 `burn`）属于元素批次，见下节；
  周期、结算、反伤、附加伤害与护盾触发的命中不算触发项；`zone_burn` 让区域每 `interval` 给区内敌人加层。
- **条件增伤**：`control_damage_amp` 的 `hp_below` 让低血目标也算；
- **吸血**：`lifesteal` 的 `hp_below` + `low_hp_multiplier` 对低血目标加倍（计入上限）；`low_hp_stat_bonus` 的 `lifesteal` 是低血期间的普攻吸血。
- **多次强化普攻**：`empower_basic` 的 `charges`（`FLDCombatEmpower::Charges`，`ConsumeEmpower` 每次取一层）与 `stats`（期间的限时属性，
  随最后一层或窗口结束）。
- **受伤触发护盾**：`triggered_shield` 的 `Damaged` 触发（`OnDamaged`）：承受他人正伤害后给护盾（按等级成长的 base + 生命上限比例），
  只有真正替换成功才开始 `interval` 冷却；`burst_ratio`/`burst_radius` 记在 `ShieldBurst`，该护盾被打破时 `BurstBrokenShield` 以获得量的比例对周围敌人造成魔法伤害。
- **反伤**：`thorns` 基础值随拥有者等级成长；`slow`/`slow_duration` 是只减移速的减速。
- **定身**：`root` 对减速中的目标仍是定身（#198 删除了 `slowed_stun`，`DemoBuildPrimitives.RootOnSlowed`）。
- **解除**：解除定身与解除减速在释放前经 `ApplyCleanses` 结算，缴械不被解除。
- **普攻命中缩短三个主动冷却**：触发项 `cooldown_refund` 的 `BasicHit` 触发（无窗口）在每次普攻主击时缩短 Q/W/E 三个技能的剩余冷却。
- 属性 `MPRegenFlat`（法力回复固定值）与 `HPRegenFlat` 同形。

### 水火电与反应（#198，`LDCombatElements`、`LDCombatElementsDirector.cpp`）

纯规则在 `Combat\LDCombatElements`（反应表、R、S16 记录与反震量、S21 削减、命中类别），身体记录上的应用在 `LDCombatElementsDirector.cpp`；
参数是 `COMBAT.Wet*`、`COMBAT.Electrified*`、`COMBAT.Discharge*`、`COMBAT.Reaction*`、`COMBAT.Fog*`、`COMBAT.Conduct*`、`COMBAT.Overload*`
（`FLDElementTerms::From`，不可用即明确失败）。数值单点见[内容与数值](../demo/CONTENT_AND_BALANCE.md) §4.4。

- **元素槽**：每个目标生命一个元素。火 = 存在活的灼烧层；水（潮湿）与电（感电）是 `FLDBodyCombatState::ElementMark`（施加者与其等级、法强快照、
  到期时刻）；潮湿另带一个 `COMBAT.WetSlow` 的减速实例（`SlowEffectId`，与标记同生同灭，最强减速生效）。同元素刷新（施加者换成最新者）。
- **批次**：`ApplyHit` 在伤害、控制与 `ApplyKeywordStacks` 之后、`bControllable` 时调用 `ApplyElements`：先火（命中的 `BurnStacks` 与存活来源的
  触发项 `burn`），再水（`bApplyWet`），再电（`bApplyElectric`）；每次 `ApplyElement` 安装、刷新或与已有的不同元素反应，反应的即时结果可接后面的元素。
  命中的施加者快照是 `ElementLevel/ElementAPP2`，由 `StampElementSnapshot` 填写；构件只需在命中上置位并填快照，不直接调 `ApplyElement`。
- **反应**（`ELDCombatReaction`，触发者 = 新元素施加者，两种元素都被消费）：R = `COMBAT.ReactionBase` + `COMBAT.ReactionPerLevel`×(L−1) +
  `COMBAT.ReactionAPRatio`×法强（魔法，不乘 G）。火的部分立即结算：已有火按 `BurnRemainingShares` 各归原施加者与来源，新火按其完整预算归触发者；
  这些结算命中带 `bBurnTick`。起雾（火＋水）：主目标受 R，在主目标脚下生成固定雾区（`FLDCombatFog`，每拥有者一个，新雾替换旧雾），
  生成时与之后每 `COMBAT.FogIntervalSec` 给区内**无元素**敌人潮湿；导电（水＋电）：主目标与其 `COMBAT.ConductRadiusCm` 内此刻潮湿的敌人各受 R
  并减速，被波及者的潮湿被消费；过载（火＋电）：主目标 `COMBAT.OverloadRadiusCm` 内的敌人（含主目标）各受 R 与 `COMBAT.OverloadBreachStacks` 层破防。
  完成的反应经 `OnReactionCompleted` 记入本次 Effects 的 `ReactionFacts`（触发者、触发来源、主目标、命中），并写 `LD_REACTION` 日志。
- **放电**：感电目标受到正伤害后（在本次命中的元素之前判定，所以新施的电不读父伤害），若已过 `DischargeReadyAtMs`，向
  `COMBAT.DischargeRadiusCm` 内离它最近的另一合法敌人放 R（施加者快照，不施电），目标的放电间隔 `COMBAT.DischargeIntervalSec` 属于目标；找不到对象不占间隔。
- **结算伤害**：反应、放电与提前结算的命中 `bStatusSettle`（或 `bBurnTick`），没有元素载荷，不吸血，不是技能命中；
  `LDCombatElements::CategoryOf` 给出 AI 规范 §8 的类别（`BasicPrimary`、`SkillDirect`、`SkillHit`、`StatusSettle`）；区域原生脉冲与 S16 反震是 `SkillDirect`。
- **来源离局**：其雾区、S10 弹射与它施加的潮湿/感电（含潮湿减速）一起结束。
- **S09 直线**（`ELDDemoTargetMode::DirectionLine`）：释放时 `CollectLineTargets` 取施法者脚下沿方向 `RangeCm`、半宽 `LineHalfWidthCm` 带内、
  视线可达的合法身体，击退沿施放方向（锚点设在目标脚后）。**S10 弹射**（`chain_bounce`）：首目标照常命中，`FLDCombatChain` 之后每 `interval`
  从上一个被击中者（死亡则用其最后脚点）找 `radius` 内最近、未被这次弹射击中的合法敌人，伤害按 `DeliveryScaleBp` 逐跳乘 `ratio`；找不到就结束。
  **S13**：`dash_contact` 的 `targets: Every` 让每个实际接触者各受一次原生命中（`bEveryContact`），出发时授盾。
- **S16 顶着揍**（`brace`，`FLDCombatBrace`）：释放时固定减伤比例、记录上限（`limit`，随 G）与反震基础（`base`，随 G）；期间来源不是自己的伤害在
  `EvaluateDamage` 中于抗性、加成池与投送比例之后、主击平减之前被削 `BraceCutBp`，实际削掉的 `BraceCutP2` 计入记录（最多上限，受保护目标为 0）。
  不因攻击、移动或施法取消；自然结束时存活身体在当时脚下对 `radius` 内敌人造成 `base + record_ratio×记录` 的魔法直接伤害（`LD_BRACE_ANSWER`）；
  死亡（`bPendingDeath`）、离局或新释放直接丢弃，不补爆。状态 `braced` 显示剩余时间。
- **S20**：`zone_wet` 让每次区域脉冲同时施潮湿，`zone_haste` 在拥有者脚站在区内时给它一份以区域效果 id 为键、到区域结束的移速限时增益，离开即撤；
  本场队友尚无队伍概念，接入 PK 队伍时再扩到队友。
- **S21 馊水糊脸**（`grievous`，`FLDCombatGrievous`）：命中给目标较强比例与较晚到期；`HealBody` 与 `GrantShieldTo` 对新治疗、新护盾先按比例削减
  （护盾削减后再做替换判断），已有护盾不变；状态 `soured`。
- 观察：`wet`、`electrified`、`braced`、`soured` 是公开状态（30–33）；观察唯一性检查是 64 位掩码，`LDObservation::MaxStatuses` 为 18。

## 构筑与来源

成长构筑由 `ALDDevelopmentDirector` 持有（`FLDGrowthBuild`：等级、按目录顺序每个五维一格的已分配点数、至多三个 `{Id, Slot}`
主动、按 id 排序的待学图纸与 Revision）。#198 第一部分删除了三选一、被动、属性卡与品质、开局方向卡与重抽，以及技能 rank；
点数、主动机会与命令合法性见 [成长事务](GROWTH_TRANSACTIONS.md)。
`MakeProjectionInput` 把构筑交给 `FLDBuildProjectionInput`（等级、持有点数的五维 `StatPoints`、Q/W/E 主动、构筑修订）；
`LDAttributeProjection` 按每个五维的 `PerPoint`（`stat_physique` 生命上限与生命回复、`stat_thick_skin` 护甲与魔抗、`stat_strength`
攻击力与暴击率、`stat_brains` 法强、技能急速与法力回复、`stat_agility` 攻速与移速比例）把点数作为本人属性视图的 `Choices` 行
（比例项进入 `PercentBonus`）加到等级成长上，最终统一夹到目录上下限。

AI 的开局时刻/候选位由 `ALDGameMode::DrawOpponentOpeningPlan` 在绑定时从现有服务器 `SpawnRandom` 流抽取，
不是每个 AI 新建 GUID 种子。`LD_AI_OPENING_PLAN` 记录抽取前的流状态 `seed`，可用同状态重放这两次抽取；
该状态只在服务器日志中，纯策略只收到计划的时刻和候选位。
构筑命令或升级都先组成变更后的完整 `FLDBuildProjectionInput`，
携带 Combat 上次安装的 `FLDExpectedBuildVersions` 交给纯函数 `PrepareBuildChange`；
同一许可内整笔成长提交全部可行后才 `ApplyBuildChange`，失败时经验、点数与构筑都不变。只改待学图纸的命令推进构筑修订但不重投影 Combat。
DEV 测试会话的测试属性层（`FLDBuildProjectionInput::DevBonuses`）也只由 Combat 持有：成长域输入不带它，
Combat 把已安装的层带进每次构筑变更，投影把它作为独立的 `Dev` 来源行；见[成长事务](GROWTH_TRANSACTIONS.md)。
来源冷却轨道与回绑只由 Combat 持有：遗忘（目前只有 DEV `LearnActive` 指定槽）不清洗冷却，重学回绑原结束时间。
满槽时普通命令不替换已学主动（`SlotOccupied`/存为图纸），休息替换属于 #198 后续部分。
等级、技能学习和 cooldown 是参赛状态，不应随 UI 或 Pawn 重建而免费重置。

构筑安全窗（`GROWTH.BuildSafeWindowSec`）由 Combat 记在实体记录的 `LastBuildUnsafeAtMs` 上（跨生命保留，只前进）：
`StartNavAttack` 受理的普攻起手、`LDCombatSourceRules::IsOffensive` 的主动释放、`ApplyHit` 中来源不是自己的正伤害（标记受害者并记入
拥有者，灼烧 tick 同样经此路径）、`CommitControl` 实际落地的硬控（定身、眩晕、缴械、嘲讽、沉默）或击退都刷新它；
减速、护盾、回复与非进攻主动（贴脸补一拳、锅盖挡脸、左右开抽与天生冲刺）不刷新。`LDCombatSourceRules::BuildSafeAtMs` 给出
`LastBuildUnsafeAtMs + 窗口`（没有记录为 0），`ReadBuildSafeAtMs` 供成长域提交时读取，本人 `CombatView.BuildSafeAtMs` 发布同一值；
DEV 重置把它清零。

### 技能等级成长（#147）

技能的固定数值随拥有者**当前等级** L 分段成长：1–10 级每级使用 `SKILL.GrowthPerLevel`（15%），
11–16 级每级使用 `SKILL.GrowthPerLevelAfter10`（7.5%），所以 G(10)=2.35、G(16)=2.8。
#198 起每个主动只有一组作者数值（`FLDDemoAbilityValues`：法力、冷却、效果），代码不再有 rank 或高级倍率。

- **成长范围**（`LDSkillGrowth::GrowEffect`）：效果的基础量（伤害、护盾、反伤、强化普攻与周期伤害；法力授予除外）、S16 的记录上限与限时属性
  的点数（攻击力、法强、护甲、魔抗）。法力、比例、持续时间、冷却、射程与层数不成长；属性系数在两档间不变。
- **预计算**：`FLDDemoContentSnapshot` 构造时对每个主动、每个等级 1..`MaxLevel` 用 `LDSkillGrowth::GrowValues` 生成一行；
  `FindValues(Ability, Level)` 返回该行，等级越界或某行算术溢出（内容验证已证明不会发生）时返回空并由调用方明确失败。
- **读取**：主动经 `ActiveOf` 在准入与释放时取成长后的数值，命中载荷在释放时快照；升级重新投影构筑；
  护盾与灼烧在施加时固定（灼烧层快照施加者等级与法强）。卡面用同一 `LDDemoCardText` 与 `FindValues` 渲染当前等级的数字（见[视野与 UI](VISION_AND_UI.md)）。

## 检查入口

`Lockdown.Rules.DemoCombatEvaluation.*`、`DemoCombatStatus.*`、`DemoCombatSources.*`（含 `InnateDash`）、`DemoOrders.*`、
`DemoAttributeProjection.*` 与 `DemoCore.*` 分别覆盖纯数值、Root/屏障/回复/冷却规则、来源轨道回绑、
攻击移动选敌与订单簿换局/换命隔离、属性及核心身份/许可/时间/投影边界；`DemoBuildExpansion.Slow/TimedBonus/ConditionalTriggers` 覆盖减速、限时增益
与低血条件项的纯规则，`DemoCombatPrimitives.*` 覆盖硬控递减、`CommitControl`、剩余冷却缩减与解除减速，
`DemoAttributeProjection.StatPoints/TemporaryTerms/LowHPTerm` 覆盖五维点数投影、临时项投影与低血条件项，
`DemoCombatEvaluation.ControlAmplify` 覆盖增伤结算，`DemoMechanics.ShieldEvents/Knockback/Periodic` 覆盖护盾打破判定、
击退方向与区域脉冲时刻；`DemoControl.ActionMatrix/EmpowerOnce/ThornsRules/ExpansionContent` 覆盖动作禁止矩阵、强化普攻消费、反伤触发/去重/数值与条目的类型化导入与显示；
`DemoGrowthSkillGrowth` 覆盖 G(L)、成长取整与按等级成长的单级数值；`DemoCombatCues.*` 覆盖动作提示的选取与优先级，
`DemoBodyMotion.*` 覆盖身体表现时间轴（前冲、聚能球、火花、闪色、片段定位、出现、倒地与同一动作的重启判定），
`DemoBuildPrimitives.*`（#147、#198）覆盖关键词纯规则、伤害加成池（技能暴击被拒）、沉默与解除、共享破防与灼烧、命中中的加成池、吸血上限、减速中目标仍被定身、构筑安全窗的触发与不触发、区域脉冲次数、多次强化普攻与完整减益观察，
`DemoElements.Rules/FogAndSlot/BraceAndGrievous`（#198）覆盖反应表与 R、元素槽与潮湿刷新、起雾的火结算与雾区归属、S16 削减记录与死亡不反震、S21 削减新护盾与治疗，
`DemoAssist.*` 覆盖助攻贡献记录与窗口、经验平分、击杀通知/结算行，以及击杀者与助攻者的死亡奖励一次结清，
`DemoControlFeel.*` 覆盖按住右键的重发节流与预输入判定；`DemoControl.*`（`LDControlRulesTests`）覆盖嘲讽的选取、
强制目标与恢复；成长的纯规则是
`DemoGrowth*`，见 [成长事务](GROWTH_TRANSACTIONS.md)。
`RunDemoCoreCheck.ps1` 用真实 Host 和两个 owning clients 运行明确标记的 DEV 初生/运动接缝，
`RunDemoCombatCheck.ps1` 经普通大厅 Start 用参与的 Listen Host 与一个 owning client 运行 DEV 战斗与订单场景：
Host 站点附近按固定方位确定的收缩边外合法脚点作为点选地面仍被拒、作为被追击身体的终点可完整规划，
已接受行走上打开设置并超过订单租约（订单与心跳继续、设置下的新订单与 S 被本地拒绝、关闭后不重放）、
远程攻击移动接敌与 S、右键击杀的击杀播报与复活延迟、死亡期间小地图平移驱动的静态镜头、
十点随机复活的精确保护和公共状态，以及空 Q 经同一 Direct 入口被拒但解除保护且不恢复；
随后 Host 的 DEV 成长夹具（只有 Host 带 `-LDDevGrowthFixture`；候选在 Lobby 按 Start 前暂存，第一组即开局候选）为双方预置候选，
并给 Client1 发放经验到首个主动机会等级；双方经普通 `SubmitBuild` 主动选择学会：Host 从开局候选学扒裤子，Client1 从开局候选学锅盖挡脸、
从 3 级候选学甩你一鞋（选择时构筑安全窗未过则先存为图纸，窗口过后学到空槽），
再经快捷入口的准备与确认施放：扒裤子定身 Client1 且 Host 的观察帧看到 Root、锅盖挡脸在定身到期前加护盾并解除定身、
Client1 的甩你一鞋作为 Chalk 投射物事实飞向 Host、命中并减速，Host 的右键追击从基础射程之外接近并在投影的基础射程内落下普攻。
双方还检查施法后攻击：Listen Host 的单位技能跟进及同步拒绝后的目标保留，真实拥有客户端在排队失效目标、
被拒施法后以方向 `CastPoint` 恢复已激活的攻击目标。
Host 以 `-RenderOffScreen` 渲染主视图，在到达对决点（首个读回可能卡住游戏线程数秒，先在这里吸收）、
在对决点旁的地面右键（DEV 指针代替鼠标，走普通 `ClickMove`）后的落点标记、本人被远程普攻击中、
右键击杀前指针悬停在 Client1 身上的悬停环与十字光标、右键后的攻击标记、
自己的观察帧显示定身、护盾、甩你一鞋飞行中段（实际绘制的投射物代理离 Client1 脚点至少 120 cm），
以及本人普攻挥击时各读回一帧
（`HostSees-DuelSite/MoveMarker/OwnHit/Hover/AttackMarker/Root/Shield/ChalkFlight/OwnStrike.png`，含 UI），
只在读回的那一帧仍显示该时刻时保存，供人工审阅画面可读性，不做像素比对；
双方报告还记录操作手感实测（只报告不设门槛）：首次右键行走从下单到身体起动、到达全速的时间，
该行走等本进程开局头几帧的一次性卡顿过去（连续 0.5 s 没有一帧超过 CharacterMovement 的
`MaxSimulationTimeStep`，最多等一个步骤时限）才下单，并同时报告下单到起动之间的帧数与最长一帧，
Client1 的下单到 Activated 回执时间，Host 全速行走中按 S 到停住的时间与滑行距离及随后再次右键的起动时间，
Client1 的 S 到服务器 Stopped 回执时间，以及当时的 CharacterMovement 参数；
营地/AI 战斗不在其内。夹具不代表正常升级或完整对局。
这些规则测试不代替新成长或真实拥有连接的运行验证。

数据编辑先运行对应 Sync `--check` 与输入测试。测试用的授予、传送或改时钟要明确是夹具，
不能当作正常升级、打野收益或完整对局已经成立。

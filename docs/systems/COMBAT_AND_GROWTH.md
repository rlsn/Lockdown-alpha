# 战斗、效果与成长

本文说明可复用的原生结算链。Demo 的属性、三选一、两个主动槽和死亡保留规则来自
[App Spec](../APP_SPEC.md)，不能靠修改文档或目录标签宣称已经实现。

## 当前组件

| 入口 | 责任 |
| --- | --- |
| `Combat\LDCombatDirector` | 唯一 Bodies/P2 资源记录与按实体保留的 Combat 记录（构筑投影、来源冷却轨道、普攻节拍、Direct 请求窗口）；真人、对手 AI 与营地成员共用的动作入口；对外只读身体状态 |
| `Combat\LDCombatDriver.cpp` | 唯一权威 driver：按真实到期点切步，按契约顺序执行各阶段，步后发布视野与本人 `CombatView` |
| `Combat\LDCombatActions/Effects/Sources/Life.cpp` | 准入/释放/回执；命中、投射物、状态到期、回复与致死锁存；构筑安装、来源绑定与可用性读模型；身体准备/安装/退役 |
| `Combat\LDTacticalRules` | `EvaluateDamage` / `EvaluateBasicTiming` / `CooldownDurationMs`，以及 Root/屏障/净化/定点回复纯规则；不提交资源或产生效果 |
| `Combat\LDControlRules`、`Combat\LDCombatControl.cpp` | 嘲讽与隐身（`taunt`/`stealth` 效果）的纯规则与在 Combat 身体记录上的应用；见下文“嘲讽与隐身” |
| `Combat\LDAttributeProjection` | 已批准构筑输入的属性与贡献投影、一次最大值变化的精确资源比例 |
| `Framework\LDGameModeLifecycle` | 唯一生命/身体绑定、借用许可、当前时间、原生运动前缀与只读发布 |
| `Framework\LDGameModeLife.cpp` | 阶段顺序与许可分派、生命结束/十点复活/保护、出生落地与营地整组生命提交 |
| `World\LDCampDecision` | 仅参数化营地决策；不读 Actor、不提交移动/攻击/奖励 |
| `World\LDCampLifecycle`、`World\LDCampDirector` | 营地名册与每局记账；服务器营地 Director 经 Combat 驱动阶段入口生成野怪、推进运动、提交普攻、消费受击/死亡事实并做回营重置与整组刷新，奖励只由成长 sink 支付 |
| `Development\LDDevelopmentDirector`、`LDProgressionRules` | 经验/等级、持久三选一队列与 Q/W 替换的权威成长记录；构筑变更经 Combat 准备与安装 |
| `Economy\LDEconomyDirector`、`Inventory\LDVendorDirector` | 死亡奖励复合提交、Transactions 分派、本人发布与两槽装备买卖，见 [库存与事务](INVENTORY_AND_TRANSACTIONS.md) |

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

普攻、Q/W、装备 1/2 与 D 冲刺经同一 `SubmitAction` 入口（真人 RPC、对手 AI 与营地成员各自的服务器 Caller）：
本人/形状/请求窗口先验，进攻意图随即经生命域解除保护，再验来源、冷却、蓝量、控制、目标与几何；
未准入不付费，已解除的保护不恢复。释放时重验来源与目标，成功才扣蓝并按 `FORMULA.CooldownMs`
写入该来源的持久冷却轨道（每个来源独立，失源、换槽、死亡都不刷新）。
冷却、蓝量与定身对位移的禁止由纯函数 `LDCombatSourceRules::ActiveGate` 判定，准入、释放和 owner view 共用同一顺序。
D 冲刺（`ELDCombatEntry::Dash`）是参赛者的 `Innate` 来源：`ResolveEntry` 给出 `LDCombatSourceRules::InnateDashId`，
`ActiveOf` 取导入时投影成单级 DirectionDash 主动的 `Character.Dash`；它不是进攻意图，不解除保护。
每名参赛者在 Combat 记录创建时签发唯一一条 `Innate` 轨道（开局就绪），构筑变更既不规划也不解绑它。
释放时 `LDCombatSourceRules::InnateDashTerms` 按身体当前投影的装备被动族（同名一次）计算距离与冷却：
`dash_boost` 乘冷却比例、加距离比例，时长不变，冷却不吃整活频率；结束时刻写成绝对 ReadyAt，之后换装不改写。
已接受但未释放的 Direct 动作因死亡、取消、打断或营地回位而结束时，回执落为 Rejected 并带原因。
Root/屏障/Root 净化/定点回复的规则是 `LDTacticalRules` 中的纯函数：
Root 按效果实例保存、同实例不续期、禁止 `POLICY.RootBlocks` 所列动作；屏障为 ReplaceIfNotWeaker，
不继承旧上限或严格大于判定；回复按 CarrySubP2Remainder 积分。
嘲讽与隐身：`taunt` 效果在释放时让施放者周围半径内（脚点平面距离，含边界）存活、无复活保护、
且此刻能观察到施放者的其他参赛者进入被嘲讽（野怪不受嘲讽）；同一身体只保留最新一次嘲讽；嘲讽落地是一次控制，隐身的受控者立即现身（`LDControl::TauntVictim`）。被嘲讽期间
Q/W/装备 1/2 与 D 冲刺在准入、释放与 HUD 可用性中都经 `ActiveGate`（`bTaunted`）以 `ControlBlocked` 拒绝，普攻只能以嘲讽者的那条生命为目标；
已在前摇、被嘲讽所禁止的动作当场放弃。每个 Actions 阶段开头 `RefreshTaunts` 结束过期、嘲讽者死亡/离局、
或被嘲讽者已观察不到嘲讽者（视线、距离或隐身）的嘲讽；离局另由 `CancelEntityEffects` 清除。
订单方面由 Navigation 强制（见 [移动、输入与镜头](MOVEMENT_INPUT_AND_CAMERA.md)）。
`stealth` 效果给施放者一段隐身：敌方（参赛者、对手 AI、野怪）只能在 `reveal_radius` 内观察到它，规则走
Vision 的同一观察链；隐身期间润速按 `FORMULA.MoveSpeed` 多一项负的加成比例（`move_speed_penalty`）。
普攻开始、任何 Q/W/装备主动被准入、或受到正伤害（含被心理防线全部吸收）/控制时立即现身；重复释放重新计时。
嘲讽与隐身只出现在本人 `CombatView` 的状态里（`Taunt`/`Stealth` 及到期），不进入他人可见的公开状态。
同一身体正在普攻前摇时，准入的技能会放弃这次普攻并开始施法；另一技能前摇未出手时再提交技能被拒绝（`Busy`）。
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
控制在命中提交时通知喷漆区：`ApplyHit` 每提交一次定身或眩晕、`ApplyTauntEffect` 每嘲讽一名参赛者，
都在同一 Actions/Effects 许可内调用 `InterruptSpray(Target)`，再经 `ALDGameMode::PublishCaptureInterrupt` 清零该参赛者的喷漆进度
（事实时刻为该边界 AtMs；营地身体或没有活动归约器的世界不受影响）。击退加入后走同一入口；缴械、减速和伤害从不调用它。
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
提示为 Cast 时，`ObservedCastSource` 由前摇中的动作或最近释放的动作（`ReleasedSource`）给出施法来源的原生 id，
分别写入公开事实的 `ActionAbility`（Q/W）或 `ActionEquipment`（装备主动），两者至多一个非 0，其他提示均为 0；
D 冲刺是人人都有的移动，两者都为 0（只显示施法动作，不飘名称）；
来源没有批准定义时 `ReadObservedPublicFacts` 失败，不发布 0。观察者据此只显示施法名，不显示等级、冷却或技能列表。
身体表现只按 cue 字节、动作年龄和状态位绘制（`ULDHumanoidVisualComponent`，时间轴为纯函数 `LDBodyMotion`）。
身体是美术目录（`ULDArtSubsystem`，`DA_DemoArt`）给出的骨骼角色：参赛者按座位色给目录 TintSlots 的 `Tint` 着色，营地两档各用自己的模型；
动画在 C++ 中单节点播放（无 AnimBP）：站立/跑动（速率 = 速度 / 目录参考跑速，低于 15 cm/s 站立），普攻与施法片段的出手帧（`ImpactSec`）
对齐到 0.2 s 的出手时刻，受击按目录速率，一次性片段每帧按呈现时钟定位；目录缺少身体时记录 `LD_BODY_ART_UNAVAILABLE` 且不画替身。
受击闪白转红与出现辉光走材质 `Flash` 自发光；
普攻整体前冲并在出手的手部闪光；施法时手部聚能球；受击整体后退、胸前闪光，本人镜头轻微震动；
状态标记对所有单位使用固定状态色，与六个座位色、草坪和红色跑道都拉开：定身为深色描边上的脉动品红圆盘加品红脚镣，
心理防线为旋转的亮青色栅栏罩加光环，复活保护为白金光柱加环绕光球；心理防线与复活保护的每个标记背后沿本机视线放一个稍大的
近黑副本作为描边，在同色系座位（青色、黄色）身上也能分开；
带复活保护首次出现时由小变大并有光柱；死亡由不复制、无碰撞的 `ALDBodyPuppet` 在原地播放死亡片段倒地，再下沉、缩没
（本人身体须等自身投影确认该命已死，终局或离局移除身体不播放）；天外飞瓶飞行由 `ULDFlightTrailComponent` 拖出粉尘。
驱散不等于回滚伤害，也不等于永久禁止持续来源重新施加效果。
**助攻贡献**（#112 §8）只有一个记录入口 `ALDCombatDirector::RecordAssistContribution(Victim, CreditOwner, NowMs)`：
受害者是参赛者、CreditOwner 是另一名有 Combat 记录的参赛者时，按纯规则 `LDAssist::RecordContribution` 在受害者这条生命的
`FLDBodyCombatState::AssistContributions` 里刷新其最近贡献时刻（锁定死亡后不再记录，换命随身体消失）。
`ApplyHit` 在提交后、与受击提示同一条件（正伤害含被心理防线吸收的部分、定身/缴械/眩晕、减速）调用一次，
`ApplyTauntEffect` 对每个被嘲讽者调用一次；新增的控制与减益在其施加成功处调用同一入口，不另建记录。
`LockDeath` 只在击杀者合格时以 `LDAssist::SelectAssisters`（`REWARD.AssistWindowSec` 窗口含边界，排除击杀者、受害者与已离局者）
把排序后的助攻者冻结进 `FLDCommittedDeath::Assisters`，之后不随离局或升级改写。
护盾、普通抗性和条件修正应走统一伤害路径，不能由某个装备在末尾再加一层未经约束的减伤。

临时属性只有一条投影：身体保存构筑投影（`BuildProjection`）与当前生效投影（`AttributeProjection`），
`LDAttributeProjection::ApplyTemporary` 用同一公式在构筑项上叠加限时增益、他急了一类的条件增益与最强一个减速
（负比例），点数类向下取整并夹紧到目录上下限；精神状态上限/电量上限不接受临时项。`RefreshAllTemporary` 在 Actions
（回复结算后）与 Effects（命中后）各执行一次，到期、血线越界与施加都在下一次运动前生效；`NextDueMs` 包含这些截止时刻。
减速、限时增益、破防与吸血标记的纯规则在 `LDTacticalRules`（`ApplySlow`、`DispelSlows`、`ApplyTimedBonus`、`ApplyBreach`/`HasBreach`/`BreachedResistanceP2`、
`ApplyVampMark`/`VampMarkBp`、`TemporaryTerms`）；减速可带 `AttackRatioBp`（手速减成，取最强一个）。
伤害系数为基础值、`ad_ratio`、`ap_ratio` 与 `max_hp_ratio`（施放者精神状态上限，`FLDDamageDefinitionValues::MaxHPRatioBp`），同在抗性前相加；普攻不带精神状态上限系数。技能命中载荷在释放时快照（含施放者 AD/AP/精神状态上限）：指向单位、弹体或 `self_area`（释放瞬间脚点为圆心、与横扫共用 `CollectAreaTargets`
的逐目标合法性与视线检查）各得一份；控制与减速随命中经 `CommitControl` 施加。潜水党的附加伤害是独立的技能伤害实例：
普攻在开始挥击时、伤害技能在释放时判定脱战窗口；冲刺或跳跃按 `DashCreditMs` 提前就绪（只对当前这段脱战有效，`AmbushReadyNow`）。
施放者离局时 `CancelEntityEffects` 一并移除其减速、破防、吸血标记与路径扫掠，营地回工位清空全部临时项。

### 控制、背后判定、强化下一击、反伤与跳跃

纯规则在 `Combat\LDCombatControlRules`（`LDCombatControl`），Combat 的准入、释放、命中与读模型都只经它判断：

- **动作禁止矩阵**（`Blocks`）：定身按 `POLICY.RootBlocks` 禁移动与位移（冲刺、跳跃）；缴械禁普攻；眩晕禁移动、位移、普攻、
  技能与装备主动。缴械/眩晕实例与定身同形（`FLDCombatRoot`：效果、来源、半开到期），存于身体的 `Disarms`/`Stuns`，
  同一效果不续期，换命即消失，营地回位与离局清除。命中时眩晕放弃任何未出手的动作、缴械放弃未出手的普攻（回执 `ControlBlocked`，
  不付费也不退节拍）；受控期间的 Direct 请求（真人、AI、营地同一入口）在准入时被拒，`ReadCombatBody` 的
  `bAttackAllowed`/`bMovementAllowed` 同步关闭，导航攻击指令因此原地等待而不重复提交；出手时再判一次。
  本人视图给出 Disarm/Stun/Empowered 状态及其 `ControlBlocks`，`bRooted` 表示“移动被禁”（定身或眩晕）；
  观察者看到公开的缴械、眩晕。
- **背后判定**（`IsBehind`）：以命中时刻目标的服务器朝向与双方脚点为准，攻击者落在 `COMBAT.RearArcDeg` 的后方扇形内即为背后。
  身体的服务器朝向是最近的移动方向，普攻或技能被接受时转向其目标/方向/落点。`backstab` 让同一条目的伤害在背后命中时
  按 `RatioBp` 放大（`FLDDamageEvaluationInput::AmplifyBp`，在抗性之前）并附加眩晕。
- **强化下一击**（`empower_basic`）：释放时武装一次（替换不叠加），由下一次已提交命中的普攻消费（`ConsumeEmpower`），
  额外伤害在暴击采样前并入该次 `BasicPrimary`（吃暴击、吸血、横扫与命中特效）；`RefundMs` 让消费后返还武装来源的剩余冷却；
  到期在 Effects 阶段移除。秦王绕柱 3 级的 `Momentum` 以同一方式并入。
- **反伤**（`thorns`）：学会的被动由属性投影列为 `LearnedTriggers`（只记录等级，不产生属性项），装备被动按同名家族一次。
  命中后持有者仍存活、伤害为正、命中类别符合（主普攻，或允许时的技能命中）、距离不超过半径、非免疫的背后来袭，
  且该来源对同一攻击者已过 `IntervalMs` 时，生成一个 `bReflect` 的技能类命中，伤害=基础+嘴硬×比例+精神状态上限×比例
  （`LDCombatControl::ThornsBaseP2`，被暴击时乘 `CriticalMultiplierBp`），可附手速/润速减成（`cripple_*`，按减速实例施加），
  走同一伤害、归属、死亡锁定与奖励路径；反伤命中不再触发反伤，也不暴击、不吸血、不触发命中特效。
- **跳到指定点**（目标 `GroundPoint`、效果 `jump`）：请求带服务器验证的地面点（`ELDCombatTargetKind::Point`），
  距离夹到目标射程，用与冲刺相同的原生根运动沿地面移动（遇阻停在合法前段，接地与喷漆区进出照常由运动链计算）；
  同一条目里其余效果在 `LandAtMs` 由 `ResolveLandings` 以落地身体为中心生效：自身效果直接施加，伤害/定身/减速/缴械/眩晕按起跳时快照的命中载荷（`HitTemplate`）交给落地脚点 `self_area` 半径内经 `CollectAreaTargets` 点名的每个目标，潜水党就绪时各附一次附加伤害（体重即正义），
  死亡或换命丢弃落地。

### 共用战斗原语（#112）

- **唯一控制路径** `ALDCombatDirector::CommitControl(Victim, FLDControlApplication, NowMs, bApplied, Error)`：
  `ELDControlKind { Root, Stun, Disarm, Taunt, Knockback, Slow }`。硬控（定身、眩晕、缴械、嘲讽）先经
  `LDCombatControl::DiminishHardControl`（`FLDHardControlChain`：100%/60%/30%，最短 min(200ms, 原持续)，
  窗口为最晚硬控结束后 2 秒）；只有实际落地的控制才推进计数与修订。嘲讽只作用于参赛者。击退由其所有者在本函数内提交。
  每次实际提交的定身、眩晕、嘲讽、击退在此调用喷漆打断，每次实际提交的控制与减速在此记录助攻贡献。
  `IsImpaired(Body, Now)` 判断任何控制或减速（已截图、掏蛋子 3 级使用）。
- **命中特效**：`FLDCombatHit::OnHitScaleBp` 是该命中的特效系数（普攻主击 10000，技能按定义，附加伤害/反伤/副击为 0）；
  `SnapshotOnHit(Body, ScaleBp, Hit, Error)` 在释放时快照吸血、横扫等来源，`ApplyHit` 内 `ApplyOnHitEffects` 按系数提交。
  `bAddedDamage` 的命中不暴击、不触发命中特效；`MakeAmbushHit` 是附加伤害命中的唯一构造。
- **伤害求值顺序**（`ApplyHit`）：技能暴击（`bCritIfImpaired`、`AbilityCritChanceBp`，倍率 `STAT.AbilityCritMultiplier`，
  每命中一次）→ 破防后的抗性（`BreachedResistanceP2`：先减固定值再乘比例）→“造成伤害 +%”相加为一个系数（`ControlAmpBp`、
  `DistanceAmpPer100Bp`）→ 背后倍率 → 目标暴击减伤（`crit_damage_reduction`）；吸血（命中特效吸血×系数、全能吸血、吸血标记）
  相加后按 `HPLostP2` 回复。
- **剩余冷却缩减**：`ALDCombatDirector::ReduceCooldown(Entity, ELDCombatEntry, AmountMs)` 与按定义查找槽位的
  `ReduceLearnedCooldown`，都经纯函数 `LDCombatSourceRules::ReduceReadyAt`（不早于当前时刻；`MAX_int64` 表示立即就绪；
  已就绪轨道不变）。
- **位移强化**：`LDCombatSourceRules::SkillDisplacementBoost` 在释放时给出带冲刺/跳跃条目的基础冷却比例（整活频率之前）与距离加成；
  `CastRangeCm` 统一给出技能施法距离（天外飞瓶的普攻距离加成、网线够长 3 级的 +80cm）。
- **被动效果查找**：`CollectPassiveEffects(Projection, Op, Out, Error)` 是学会被动与装备被动（同名家族一次）的唯一读取口；
  装备效果后续经 `LDDemoEquipment::Effective*` 取当前生效定义。
- 其余 3 级状态：`Momentum`（秦王绕柱）、`Retaliation`（你瞅啥）、`DashSweeps`（一个滑铲路径减速）、`BasicHitRefund`（无影大比兜）、
  `RegenSurgeUntilMs`（发疯文学）、`LowHPShieldReadyAtMs`（他急了），均在 `NextDueMs` 中登记截止时刻。

持续伤害按原计划的 tick 和受害者 life 去重；晚进入区域不重新领取整段总伤害。
释放者、资源预算所有者、当前伤害归属和受害者生命是不同概念，
反射、死亡或替换来源不能重建免费预算或让旧投射物击中新 life。

付费施法必须先保证分配与来源合法，提交时一致消耗；
不得出现已扣资源但静默没有效果，或多个来源对同一额度重复返还。
表现层不得成为第二个伤害、碰撞或效果计时权威。

## 构筑与来源

成长构筑由 `ALDDevelopmentDirector` 持有（`FLDGrowthBuild`：等级、技能 Id/Rank/主动槽、属性卡与 Revision）。
构筑可带的选择数为 min(等级, 最大选择次数)：开局即有一次三选一，1 级构筑可带一张卡（D10）。
选择、升级或买卖都先组成变更后的完整 `FLDBuildProjectionInput`（等级、被动、属性卡、Q/W 主动、
两槽装备实例与修订），携带 Combat 上次安装的 `FLDExpectedBuildVersions` 交给纯函数 `PrepareBuildChange`；
同一许可内整笔成长提交全部可行后才 `ApplyBuildChange`，失败时钱、物、经验与构筑都不变。
DEV 测试会话的测试属性层（`FLDBuildProjectionInput::DevBonuses`）也只由 Combat 持有：成长域输入不带它，
Combat 把已安装的层带进每次构筑变更，投影把它作为独立的 `Dev` 来源行；见[库存与事务](INVENTORY_AND_TRANSACTIONS.md)。
已进阶装备（`FLDEquippedItemValue.bUpgraded`）的固定属性、被动效果与主动等级只经 `LDDemoEquipment::EffectiveStats/EffectivePassiveEffects/EffectiveActiveRank`
取得，属性投影、来源与效果处理器不另写进阶分支；同款双持时同名被动取有进阶的一件。
来源冷却轨道与债优先回绑只由 Combat 持有：替换遗忘 rank 但不清洗冷却，进阶不刷新冷却，售出重购按回绑规则继承；
商店报价用只读 `PreviewGearBinding` 说明继承冷却，以购买回执为准。

等级、技能学习和 cooldown 是参赛状态，不应随 UI 或 Pawn 重建而免费重置。

## 检查入口

`Lockdown.Rules.DemoCombatEvaluation.*`、`DemoCombatStatus.*`、`DemoCombatSources.*`（含 `InnateDash`）、`DemoOrders.*`、
`DemoAttributeProjection.*` 与 `DemoCore.*` 分别覆盖纯数值（含 `AbilityCrit`）、Root/屏障/回复/冷却规则、来源轨道回绑、
攻击移动选敌与订单簿换局/换命隔离、属性及核心身份/许可/时间/投影边界；`DemoBuildExpansion.*` 覆盖减速、限时增益、
低血线/潜水党窗口与破防/吸血标记的纯规则，`DemoCombatPrimitives.*` 覆盖硬控递减、`CommitControl`、剩余冷却缩减、破防叠加、
手速减速与解除减速、回复翻倍窗口，`DemoAttributeProjection.TemporaryTerms/ExpansionPassives` 覆盖临时项投影与条件被动，
`DemoCombatEvaluation.ControlAmplify` 覆盖增伤结算；`DemoControl.ActionMatrix/RearArc/EmpowerOnce/ThornsRules/JumpPlan/ExpansionContent` 覆盖动作禁止矩阵、背后扇形、强化下一击单次消费、反伤触发/去重/数值、跳跃落点规划与扩展条目的类型化导入与显示；`DemoCombatCues.*` 覆盖动作提示的选取与优先级，
`DemoBodyMotion.*` 覆盖身体表现时间轴（前冲、聚能球、火花、闪色、片段定位、出现、倒地与同一动作的重启判定），
`DemoAssist.*` 覆盖助攻贡献记录与窗口、奖励分配、多账户链式账本、击杀通知/结算行，以及击杀者与助攻者的死亡奖励一次结清，
`DemoControlFeel.*` 覆盖按住右键的重发节流与预输入判定；`DemoControl.*`（`LDControlRulesTests`）覆盖嘲讽的选取、
强制目标与恢复，以及隐身的观察授权与撤销、近距离发现、现身与润速；成长与商店的纯规则是
`DemoGrowth*`，见 [库存与事务](INVENTORY_AND_TRANSACTIONS.md)。
`RunDemoCoreCheck.ps1` 用真实 Host 和两个 owning clients 运行明确标记的 DEV 初生/运动接缝，
`RunDemoCombatCheck.ps1` 经普通大厅 Start 用参与的 Listen Host 与一个 owning client 运行 DEV 战斗与订单场景：
Host 站点附近按固定方位确定的收缩边外合法脚点作为点选地面仍被拒、作为被追击身体的终点可完整规划，
已接受行走上打开设置并超过订单租约（订单与心跳继续、设置下的新订单与 S 被本地拒绝、关闭后不重放）、
远程攻击移动接敌与 S、右键击杀的击杀播报与复活延迟、死亡期间小地图平移驱动的静态镜头、
十点随机复活的精确保护和公共状态，以及空 Q 经同一 Direct 入口被拒但解除保护且不恢复；
随后 Host 的 DEV 成长夹具（只有 Host 带 `-LDDevGrowthFixture`；选项在 Lobby 按 Start 前暂存，第一组即开局 Offer）为双方预置开局与下一组选项并发放经验（升级比所学卡多一级，最后一次机会不选），
Host 经普通 `SubmitChoice` 学会 attr_reach 与扒裤子，Client1 学会我没事我很好与天外飞瓶，再经快捷入口的准备与确认施放：
扒裤子定身 Client1 且 Host 的观察帧看到 Root、我没事我很好在定身到期前加护盾并解除定身、
Client1 从较远的对决点扔出的天外飞瓶作为 Chalk 投射物事实飞向 Host 并命中、增程卡让右键追击在基础射程之外落下普攻。
Host 以 `-RenderOffScreen` 渲染主视图，在到达对决点（首个读回可能卡住游戏线程数秒，先在这里吸收）、
在对决点旁的地面右键（DEV 指针代替鼠标，走普通 `ClickMove`）后的落点标记、本人被远程普攻击中、
右键击杀前指针悬停在 Client1 身上的悬停环与十字光标、右键后的攻击标记、
自己的观察帧显示定身、护盾、天外飞瓶飞行中段（实际绘制的投射物代理离 Client1 脚点至少 120 cm），
以及本人普攻挥击时各读回一帧
（`HostSees-DuelSite/MoveMarker/OwnHit/Hover/AttackMarker/Root/Shield/ChalkFlight/OwnStrike.png`，含 UI），
只在读回的那一帧仍显示该时刻时保存，供人工审阅画面可读性，不做像素比对；
双方报告还记录操作手感实测（只报告不设门槛）：首次右键行走从下单到身体起动、到达全速的时间，
该行走等本进程开局头几帧的一次性卡顿过去（连续 0.5 s 没有一帧超过 CharacterMovement 的
`MaxSimulationTimeStep`，最多等一个步骤时限）才下单，并同时报告下单到起动之间的帧数与最长一帧，
Client1 的下单到 Activated 回执时间，Host 全速行走中按 S 到停住的时间与滑行距离及随后再次右键的起动时间，
Client1 的 S 到服务器 Stopped 回执时间，以及当时的 CharacterMovement 参数；
装备主动与营地/AI 战斗不在其内。夹具不代表正常升级或完整对局。
这些规则测试不代替新成长、商店或真实拥有连接的运行验证。

数据编辑先运行对应 Sync `--check` 与输入测试。测试用的授予、传送或改时钟要明确是夹具，
不能当作正常升级、打野收益或完整对局已经成立。

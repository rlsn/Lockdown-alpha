# 战斗、效果与成长

按任务跳转：[组件与文件](#当前组件) · [意图到结果](#从意图到结果) · [纯值接口](#纯值接口) ·
[属性与资源](#属性与资源) · [来源与冷却](#来源与冷却) · [权威时序](#权威时序) ·
[状态与效果](#状态伤害和效果归属) · [构筑](#构筑与来源) · [死亡与营地](#死亡奖励与营地重置) ·
[生命周期](#生命周期矩阵) · [检查](#检查入口) · [野怪攻击](#野怪攻击模式)。
先查下表的不变量，再读对应章节和实现；成长提交与回执另见[成长事务](GROWTH_TRANSACTIONS.md)。

| 项 | 内容 |
| --- | --- |
| 规则 | [App Spec](../APP_SPEC.md) §4–§6；公式、目录与数值只在[内容与数值](../demo/CONTENT_AND_BALANCE.md)，本文只引用键名与操作语义；跨域语义见[共享契约](../demo/SHARED_CONTRACTS.md) SC-03/05/06 |
| 文件 | `Combat\LDCombatDirector`（分部 `LDCombatActions/Effects/Sources/Life/Driver/Observation/Provider.cpp`）、`LDCombatTypes.h`、`LDTacticalRules`、`LDCombatEvaluationTypes.h`、`LDCombatSourceRules.h`、`LDAttributeProjection`、`LDControlRules`、`LDCombatCues`、`LDCombatPreInput`；`Framework\LDMatchLifecycleTypes.h`、`LDGameModeLifecycle.cpp`、`LDGameModeLife.cpp`；`Development\LDDevelopmentDirector`、`LDGrowthDirector`；`World\LDCampDirector`、`LDCampDecision` |
| 入口函数 | `AdvanceAuthority`（唯一权威批次）、`SubmitAction` / `CancelAction` / `QueryActionReceipt` / `QueryAvailability` / `BuildOwnerView`、`StartNavAttack` / `AdoptNavAttack` / `CancelNavAttack`、`PrepareBuildChange` / `ApplyBuildChange`、`PrepareCampReset` / `ApplyCampReset`、`ApplyHit`、`CommitControl`、`CommitReward` → `ALDGrowthDirector::CommitDeathReward` |
| 不变量 | `ALDCombatDirector` 是唯一的战斗、属性、冷却、奖励交接与身体战斗状态所有者；HP/劲/盾只以 P2 存于 `Combat.Bodies.Resources`；进攻意图先解除保护再验动作；未释放不付费，释放后不退款；冷却按来源持久计时，换槽/遗忘/死亡不清洗；一条生命只有一个 `FLDDeathKey`；先锁完本批全部死亡事实再支付奖励 |
| 检查 | `Lockdown.Rules.DemoCombat*`、`DemoAttributeProjection.*`、`DemoOrders.*`、`DemoCore.*`、`DemoControl.*`、`DemoBuildPrimitives.*`、`DemoElements.*`、`Combat.Components.*`、`Combat.Events.*`；场景 `RunDemoCombatCheck.ps1`、`RunDemoCoreCheck.ps1`（见[检查入口](#检查入口)） |

## 当前组件

| 入口 | 责任 |
| --- | --- |
| `Combat\LDCombatDirector` | 唯一 Bodies/P2 资源记录与按实体保留的 Combat 记录（构筑投影、来源冷却轨道、普攻节拍、Direct 请求窗口）；真人、对手 AI 与营地成员共用的动作入口；对外只读身体状态 |
| `Combat\LDCombatDriver.cpp` | 唯一权威 driver：按真实到期点切步，按契约顺序执行各阶段（`RunDeaths`、`CommitReward`），步后发布视野与本人 `CombatView` |
| `Combat\LDCombatActions/Effects/Sources/Life/Provider.cpp` | 准入/释放/回执/取消/打断；命中、投射物、状态到期、回复与致死锁存；构筑安装、来源绑定与可用性读模型；身体准备/安装/退役与离局取消效果；关局与重置钩子 |
| `Combat\LDCombatObservation.cpp` | 观察投影：`AppendObservedStatuses`、`AppendObservedAction`、`ReadObservedProjectiles`、`ReadObservedPublicFacts` |
| `Combat\LDTacticalRules` | `EvaluateDamage` / `EvaluateBasicTiming` / `CooldownDurationMs` / `SettleRegen`，以及 Root/屏障/净化/减速/限时增益纯规则；不提交资源或产生效果 |
| `Combat\LDControlRules`、`LDCombatControl.cpp` | 嘲讽的纯规则与应用 |
| `Combat\LDAttributeProjection` | 构筑输入的属性与贡献投影、一次最大值变化的精确资源比例、临时项 |
| `Framework\LDGameModeLifecycle` | 唯一生命/身体绑定、借用许可、当前时间、原生运动前缀、故障报告与只读发布 |
| `Framework\LDGameModeLife.cpp` | 阶段顺序与许可分派、生命结束/十二点复活/保护、出生落地与营地整组生命提交 |
| `World\LDCampDecision`、`LDCampLifecycle`、`LDCampDirector` | 仅参数化营地决策；营地名册与每局记账；服务器营地 Director 经 Combat 阶段入口生成野怪、推进运动、提交普攻、消费受击/死亡事实、回营重置与整组刷新 |
| `Development\LDDevelopmentDirector`、`LDProgressionRules` | 经验/等级、派生点数、持久主动候选组、待学图纸与 Q/W/E 的权威成长记录 |
| `Development\LDGrowthDirector` | 死亡与地图事件奖励的复合提交、Transactions 分派与本人发布，见[成长事务](GROWTH_TRANSACTIONS.md) |
| `Framework\LDPlayerOrders.cpp`、`LDPlayerInput.cpp` | 拥有连接入口；客户端准备、预输入与快速施法见[移动、输入与镜头](MOVEMENT_INPUT_AND_CAMERA.md) |

入口位于 `Source\Lockdown\Public`/`Private`。属性、技能与物品只来自唯一 Demo 目录 `ULDDemoContentCatalog`。
已退出的旧链（R09 `LDCharacterBuild`、`LDV08Loadout`、`LDGearRules`、`LDTacticalCatalog`、`LDEquipmentCatalog`、`LDTacticalRequest`、`LDContentActions/*`、
`LDTacticalExecution`、`LDWorldClock` 回拨补算、最后接触者推奖、并行怪物攻击计时）不作为现状、兼容入口或数值来源。

## 身份与接口

共享核心类型在 `Framework\LDMatchLifecycleTypes.h`（由[架构与网络](ARCHITECTURE_AND_NETWORKING.md)维护），战斗 DTO 在 `Combat\LDCombatTypes.h`；本模块不另建 Match、Life、时钟或写闸。

| 类型 | 字段 | 不变量 |
| --- | --- | --- |
| `FLDMatchContext` | `uint32 Generation; FGuid LedgerMatch` | 网络只声明 Generation，服务器解析账本 GUID |
| `FLDVictimKey` | `Kind; FGuid EntityId; uint32 Life` | EntityId 为稳定逻辑主体（参赛者 Contender，野怪 CampId/MemberId），不是 Actor 或观察句柄 |
| `FLDCombatBodyRef` | `Victim; FGuid BodyIdentity` | 一条逻辑生命只有一条当前身体绑定；重复 Register/换 Pawn 不发新血量或第二死亡权 |
| `FLDCombatSourceRef` | `Kind`（None/Basic/Learned/Innate）、`DefinitionId` | 所有者、等级与绑定由服务器解析；Q/W/E/D 只是入口 |
| `FLDCombatRequestKey` | Direct：Generation/Entity/Life/DirectSequence；Nav：Generation/Entity/Life/MoveIdentity/AttackCycle | 两种固定判别形状（`Origin`），不按裸整数混比；Nav 仅由服务器订单产生 |
| `CombatToken` | 完整准入时发行的 `uint64` 动作序号 | 同 Caller 本局单调、跨身体保留；重复请求返回同 Token |
| `FLDCombatHit` | `HitId`、`EffectId`、`Token`、目标 BodyRef、源身体/所有者 | Hit 去重不是死亡去重 |
| `FLDDeathKey` | `LedgerMatch; FLDVictimKey Victim` | 不含 Killer、EffectId 或时间；改这些不能再奖励同一生命 |
| `FLDCommittedDeath` | Key、VictimBody、VictimContender、FatalEffectId、CreditOwner（空即无主）、ReleasedSource、SourceBodyAtRelease、RewardBasis、Cause、Assisters、Recap、OccurredAtSeconds/Ms | 首次 PendingDeath 原子锁定；Life/成长/营地消费同一值 |
| `FLDRewardBasis` | RewardDefinitionId；VictimLevelAtDeath、VictimBuildRevisionAtDeath；`bCreditOwnerEligibleAtDeath`、`NoRewardReason`（NoOwner/SelfInflicted/OwnerLeft） | 按致死真实时刻冻结；内部支付失败不是 NoReward |

宽度：Generation/Life 为 `uint32`；请求序列、CombatToken、AttackCycle、事件序号与各类 revision 为 `uint64`；身体、参赛者、实例、`IssuedCooldownId` 为 `FGuid`；
期限 `int64` 毫秒，原始模拟时刻 `double` 秒；资源为 `int64` P2。改 wire 宽度时同批改序列化、生产者、消费者及拒绝测试；计数耗尽明确失败，不回绕。

```cpp
bool AdvanceAuthority(const FLDMatchContext& Match, int64 ThroughMs, FString& Error);
bool ResolveControllerCaller(const AController& Controller, FLDCombatCaller& Out, FString& Error) const;
FLDCombatActionReceipt SubmitAction(const FLDCombatCaller&, const FLDCombatActionRequest&);
FLDCombatActionReceipt CancelAction(const FLDCombatCaller&, const FLDCombatCancelRequest&);
FLDCombatActionReceipt QueryActionReceipt(const FLDCombatCaller&, uint64 DirectSequence) const;
FLDCombatEntryView QueryAvailability(const FLDCombatCaller&, ELDCombatEntry Entry) const;
bool BuildOwnerView(const FLDCombatCaller&, FLDCombatOwnerView& Out) const;
FLDCombatActionReceipt StartNavAttack(const FLDCombatCaller&, const FLDCombatRequestKey&, const FLDCombatBodyRef& Target,
    const FGuid& Observed, uint32 ObservationEpoch, const FLDGameplayWritePermit&);
void CancelNavAttack(const FGuid& Entity, const FLDMoveIdentity& Move);
FLDCombatPrepareResult PrepareBuildChange(const FLDCombatCaller&, const FLDBuildProjectionInput&, const FLDExpectedBuildVersions&) const;
void ApplyBuildChange(FLDPreparedCombatProjection&&, const FLDGameplayWritePermit&);
FLDCampResetPrepareResult PrepareCampReset(const FLDCampResetInput&, const FLDGameplayWritePermit&) const;
void ApplyCampReset(FLDPreparedCampReset&&, const FLDGameplayWritePermit&);
```

- `AdvanceAuthority` 由 Director Tick 以 GameMode 可用权威时间调用，失败交 `ALDGameMode::ReportAuthorityFault`。动作终态不经 delegate：Direct 由 `FinishDirectReceipt`/`AbandonAction` 写终态回执，Nav 普攻由 Navigation 驱动。
- `ApplyBuildChange` 只供已验证复合提交（`LDDevelopmentDirector`、`LDGrowthDirector`）内部不失败安装，不开放为 UI/AI 写入口。
- `FLDCombatCaller` 只在服务器，由拥有 Controller/服务器 AI/注册野怪解析，不进 wire；无当前 Pawn 的死亡选择与既成死亡结算用持久参赛上下文，不伪造 Alive Body。
- 网络桥：`ServerSubmitCombatAction`、`ServerCancelCombatAction`（Server Reliable）、`ClientCombatReceipt`（Client Reliable）。`FLDCombatActionRequest` 只带 Generation/Life/DirectSequence、
  `ELDCombatEntry`、`FLDCombatTarget` 与 `ExpectedSourceRevision`，不传 AD/Rank/HP/ReadyAt、目标列表、客户端时钟或所有者。代次、序号、枚举与有限数值属于前闸；
  入口是否持有来源、TargetMode 是否匹配属于失保后的语义校验；空入口照常发送，由服务器以 `SourceUnavailable` 失败，不伪装 Malformed。
- 不分列 Build/SourceBinding 修订：Direct 以本人视图该入口的 `ExpectedSourceRevision` 前验（不符 `StaleSource`）。不逐请求上传目录摘要：连接在 `ALDGameMode::PreLogin` 以 `LDCatalog`
  选项钉住 `FLDDemoContentDigest`（`manifest.json` 规范字节的 SHA-256，由 `Scripts\SyncDemoContentData.py` 生成并写入 `LDDemoContentContract.h`），本局内容快照不可变。
- AI 经服务器绑定上下文调用同一服务，不调用下层或假造 RPC；参赛 AI 用同一持久构筑。普通怪物复用动作/伤害原语，营地只决定意图、回位与刷新。CampReset 是世界域提供到家证据后的服务器内部操作。

## 从意图到结果

UI 只能准备或发送动作，不直接扣蓝、改 HP、加效果或确认命中。预览、接受、释放、命中和结束是不同阶段；失败要明确，同时点击图标和快捷键不能产生两次效果；
已释放效果的存续由其生命周期决定，不通过销毁 UI 或换槽退款。

**收件与回执**：DirectSequence 由同一输入源供全部 Direct 入口共用（AI 走服务器绑定的 Direct 域）；Nav 用当前 MoveIdentity+AttackCycle。完整准入才分配 CombatToken。
`FLDCombatActionReceipt` 带 Generation/Life/Origin/DirectSequence/Entry、CombatToken、ReceiptRevision、Phase、Reject、`bProtectionRemoved`、LifeRevision。
阶段为 Queued、ActionAccepted、ReleaseCommitted、Finished、Rejected：收件不是接受，接受不是支付，支付不是命中；终态不可改，Query/重复请求只返回已知进展。
Direct 拒旧水位、Nav 订单/周期水位、取消水位和回执游标分开：同 key 同条款只重放，窗内异条款 `RequestConflict`，过窗 `ReceiptExpired/StaleRequest`。
较新无效请求不撤销已接受旧订单，更老的 Pending 不能迟到激活。取消可指 RequestKey、`ThroughDirectSequence` 或已知 CombatToken；不能把 Through 与 CombatToken 数值比较；
S/新订单只停可中断工作，不回收已释放效果或退款。已接受未释放的 Direct 动作因死亡、取消、打断或营地回位而结束时回执为 Rejected 并带原因。

**解除保护**：唯一入口 `ALDGameMode::EndProtectionForAggressiveIntent(Caller, Match, Body, Intent, Permit) -> FLDProtectionReceipt`。Intent.Kind 为 AttackUnit/AttackMove/BasicAttack/ActivateCharacter；
`FLDAggressionRequestKey` 含 Origin、Sequence、CancelEpoch（Navigation 用原 Order/Epoch，Combat 用 DirectSequence 且 CancelEpoch 为零）。域先做 shape/freshness/冲突检查，核心再验 Caller、
当前 Match/Body/Life/Alive 与 permit，由 Life 按当前 Actions 真实时间发布一次资格变化。前闸通过后先失保，再查来源、目标、距离、劲/冷却、控制与路径；
Rejected 也可能 `ProtectionRemoved=true`。所有角色主动（含自身守护/回复）适用；普通移动、Stop、本地准备与普通 D 不适用。旧局/旧生命、重放、畸形包在前闸拒绝，不改新生命。
在喷漆区内失败的进攻仍可能因失保改变喷漆资格。

**准入与释放**：普攻、Q/W/E 与 D 冲刺经同一 `SubmitAction`（真人 RPC、AI、营地成员各自的服务器 Caller）。冷却、劲与定身对位移的禁止由
`LDCombatSourceRules::ActiveGate` 判定，准入、释放和 owner view 共用同一顺序。一个身体同时只有一个未释放动作：普攻前摇中被接受的技能放弃该普攻（`AbandonAction`，节拍不退回），
能力前摇中再提交技能返回 `Busy`。

- **普攻**：Navigation 在 Actions 许可内以订单键调 `StartNavAttack`。合法 AttackStart 固定本次前摇与最早 `NextAttackAt`；取消、移动、改攻速不提前该期限，攻速只影响下一次。
  参赛者节拍是局内持久状态，跨死亡按时间继续；怪物新生命按营地初始化。释放（`ReleaseBasic`）不查距离，只经 `LDCombatSourceRules::BasicReleaseGate`：攻击者被缴械/眩晕/击退
  或被嘲讽转向他人放弃（`ControlBlocked`）；目标那条生命死亡或濒死、离局、换命或换身体放弃（`TargetUnavailable`）；参赛者攻击者重新观察不到该生命放弃（营地感知由营地模块负责）；
  视线被挡放弃（`Blocked`）；否则命中起手时的目标生命。攻击者死亡后 `ReleaseDue` 不再释放其动作。`AdoptNavAttack` 让同一观察目标的新订单接管未出手普攻；
  `FLDCombatBodyRead::ActionReleaseAtMs` 供导航判断本阶段即将出手的普攻不再占住身体。
- **技能**：释放时重验来源/生命、取消、条款、目标、控制、劲/冷却与几何；效果/状态容量预分配且旧状态未被破坏后验 permit，一次提交扣劲、本条 ReadyAt（`FORMULA.CooldownMs`）、Effect 与回执。
  不先扣费再生成，不在 staging 时销毁旧盾/场/状态。释放后的 Miss、阻挡或目标死亡不退款；已接受动作释放失败不恢复历史订单。
- **D 冲刺**（`ELDCombatEntry::Dash`）是参赛者的 `Innate` 来源：`ResolveEntry` 给出 `LDCombatSourceRules::InnateDashId`，`ActiveOf` 取投影成单级 DirectionDash 主动的 `Character.Dash`。
  每名参赛者在 Combat 记录创建时签发唯一一条开局就绪的 Innate 轨道，构筑变更不规划也不解绑它。释放时 `InnateDashTerms` 只读目录距离与冷却，冷却不吃技能急速，结束写成绝对 ReadyAt。
  普通 D 不是进攻意图；构筑带 `dash_contact_damage` 时 D 沿真实轨迹撞伤，准入前按进攻意图解除保护。
- 动作结束只结束当次执行：Direct 写一次终态回执，Nav 普攻经 `CancelNavAttack`；下一普攻由下一个 Actions 阶段重新验证，没有第二条回调驱动战斗循环。

**控制矩阵**（全部控制与减速只经 `CommitControl` 施加；定身、眩晕、沉默、缴械、嘲讽共用硬控递减，见[内容与数值 §4.5](../demo/CONTENT_AND_BALANCE.md#45-关键词状态与共用战斗规则)）：

| 状态 | 移动/追击 | 原地普攻 | 非位移 Q/W/E | 位移主动 |
| --- | --- | --- | --- | --- |
| Root | 禁止 | 允许 | 允许（含自身盾 + 净化 Root） | 禁止（准入/释放拒绝，不付费） |
| PendingDeath/Dead | 禁止新动作 | 禁止 | 禁止 | 禁止 |
| 复活保护 | 允许，不计入喷漆区人数 | 先失保再验动作 | 同左 | 同左，仍查 Root |
| Left/Closed | 禁止 | 禁止 | 禁止 | 禁止 |

- Root 按 effect/cast + 完整目标生命保存独立实例与原期限，任一未到期即生效，显示最大剩余；同实例重发不续期，较早到期不清掉另一来源。禁止项为 `POLICY.RootBlocks`。
  “我没急”（`active_not_mad`）给护盾并解除当时全部减速与 Root，不带后续免疫。
- `EFFECT.ShieldPolicy=ReplaceIfNotWeaker` 比较旧盾**当前剩余量**：新量不少于剩余即以新量、新来源、新截止完整替换；较弱保留旧盾，且只是子效果结果，组合净化照常、劲与冷却照付。
  之后 Max/AP 变化不重算已快照盾。
- **嘲讽**：`taunt` 在释放时让施放者半径内（脚点平面距离含边界）存活、无保护且此刻能观察到施放者的其他参赛者被嘲讽（野怪不受）；同一身体只保留最新一次；落地是一次控制（`LDControl::TauntVictim`）。
  期间 Q/W/E 与 D 经 `ActiveGate`（`bTaunted`）以 `ControlBlocked` 拒绝，普攻只能打嘲讽者那条生命，前摇中被禁止的动作当场放弃。每个 Actions 开头 `RefreshTaunts` 结束过期、嘲讽者死亡/离局
  或观察不到嘲讽者的嘲讽；离局由 `CancelEntityEffects` 清除。订单强制见[移动、输入与镜头](MOVEMENT_INPUT_AND_CAMERA.md)。嘲讽只出现在本人 `CombatView`。
- 目录不用的状态与效果（免死、背后判定、灼烧以外的持续伤害、减疗、易伤）连同处理器已删除，不保留禁用生产者或空 handler。

`ELDCombatReject`：InvalidRequest/NotOwner、StaleMatch/StaleLife/StaleRequest、RequestConflict/ReceiptExpired、Cancelled、StaleSource、SourceUnavailable、
Busy/Cooldown/AttackNotReady/MomentumShort/ControlBlocked、TargetUnavailable、InvalidDirection/OutOfRange/Blocked、Dead/MatchClosed、AuthorityDataUnavailable/CapacityUnavailable。
构筑变更的数值失败（NumericUnderflow/NumericDomain）属于 `ELDCombatPrepareError`。隐藏、撤销、过期目标统一 TargetUnavailable；分配/算术/缺边界证据不是 Miss，不能用零伤害或默认坐标吞掉。

**目标与接触**：单位新锁定、每次追击、AttackStart 与需要单位许可的 Release 都经 `ResolveObservedUnit`/`TraceSight` 重新解析，失视野取消锁定并停追击。
目标种类 Missing/Self/Unit/Direction/Point；Point 只用于 GroundPoint 区域技能，释放时夹到射程内。区域与方向效果在权威内部读同一 `Bodies` 求接触并逐目标做世界 LOS（`LDCombatEffects.cpp`），
可以接触隐藏身体，但不授予观察句柄，不交 UI/AI/Nav；方向效果在首次真实接触时取得目标生命。缺世界注册或非法半径明确失败，不截断为“最近若干目标”。

## 纯值接口

`LDTacticalRules`（值类型在 `LDCombatEvaluationTypes.h`）借用已验证的 `FLDDemoContentSnapshot`，真实执行（`ApplyHit`、身体普攻时长）与对手 AI 估计（`LDOpponentPolicy.cpp`）调用同一组纯函数：
`EvaluateDamage(FLDDamageEvaluationInput, Snapshot)`、`EvaluateBasicTiming(FLDBasicTimingInput, Snapshot)`。纯函数不查世界、不读时钟、不采随机、不分配 Token 或改状态。

| 值类型 | 边界 |
| --- | --- |
| `FLDDamageEvaluationInput` | 显式 `Definition(BaseP2, ADRatioBp, APRatioBp, MaxHPRatioBp, DamageType)`、HitClass、必需解析的源属性/本次暴击/目标抗性/HP/盾/保护；仅零系数可省对应源属性，缺字段不默认为零或未保护 |
| `FLDDamageEvaluationResult` | `TOptional` Value 与 Code/Field/Message 的 Error；成功给 DamageP2、ShieldAbsorbedP2、HPLostP2、OverkillP2、RemainingHPP2/RemainingShieldP2。只有 `HPLostP2` 是实际扣血/吸血基数 |
| `FLDBasicTimingInput` / `Result` | 基础与当前攻速有理数及 BaseWindupMs（玩家 `STAT.BaseAttackSpeed/UNIT.BasicAttackWindupSec`，怪物 `CAMP.*.AttackSpeed/AttackWindupSec`）；输出 IntervalMs/WindupMs 且前摇小于间隔，不生成绝对时刻 |

- HitClass 为 BasicPrimary、BasicSecondary、Ability：只有主普攻允许本次暴击（×`STAT.CritMultiplier`，在乘区之前）；带暴击的技能输入以 Domain 拒绝；反伤与附加伤害不暴击；Basic 拒绝 AP 系数与魔法伤害。
- 完整字段、数值域与精确算术校验先于保护零伤；中间有理数保留到最终一次 floor/ceil；精度与取整只取内容合同（`FORMULA.Crit/Resistance/Damage`、`FORMULA.AttackIntervalMs/AttackWindupMs`）。
  原生公式夹具使用 producer 生成的 `approved_base`，`base` 是独立合成输入，不在夹具缺失时回退生产资产。
- AI 只以自身属性、合法可见成员与公共模板构造保守假设（显式不暴击），缺输入返回 EstimateUnavailable，不当成零风险。

## 属性与资源

`LDAttributeProjection::Project(FLDAttributeBuildValues, Snapshot)`：输入为等级、五维点数、已装的牌（按序号排序，同名多份各一行）与 DEV 测试层；空数组表示没有该来源。
结果含全部最终字段与按属性顺序的基础/等级/点数/牌/DEV 贡献；Y15 成为 `MissingHPAttackSpeedBp`，全部已装的牌另列出供触发处理（`InstalledCards`），条件脱战回复单独返回。
初始身体用它初始化 `Bodies.Resources` 与普攻时长；构筑变更经成长事务调用 `PrepareBuildChange`/`ApplyBuildChange`。

| 属性 | 内容键 | 消费者/单位 |
| --- | --- | --- |
| AD/AP | `STAT.AD/AP.*`；`FORMULA.StatProjection/Damage` | AP 只进明确的法术加成，不改普攻类型 |
| Armor/MR | `STAT.Armor/MagicResist.*`；`FORMULA.Resistance` | 每次 Hit 取目标当前值 |
| 攻速/间隔/前摇 | `STAT.BaseAttackSpeed/AttackSpeedPerLevelRatio`；`FORMULA.AttackSpeed/AttackIntervalMs/AttackWindupMs` | 在合法 AttackStart 快照，不缩短能力前摇 |
| 移速 | `UNIT.MoveSpeedCmPerSec`；`FORMULA.MoveSpeed → STAT.MoveSpeedCmPerSec` | cm/s，接 CharacterMovement |
| 攻击距离 | `UNIT.BasicAttackRangeCm` + 来源 `AttackRangeCmFlat`；`FORMULA.AttackRange → STAT.AttackRangeCm` | 脚点 XY 中心距；只限制普攻开始；不等于能力 Range 或群攻 Radius；扩大不创建追踪弹体 |
| 最大/当前 HP | `STAT.MaxHP.*`；`FORMULA.Damage/Regen/ResourceMaxChange` | P2；Combat 唯一真值 |
| 劲 | `MOMENTUM.*`、脑力 `MomentumRegenFlat`（见下方“劲、连击、爆劲与完美闪避”） | P2，上限固定 100；Combat 唯一真值 |
| 回复 | `STAT.HPRegen.*`；`FORMULA.Regen` | 点/秒，按有效速率区间累计 |
| 暴击 | `STAT.CritChance.Base/CritMultiplier`；`FORMULA.Crit`、`POLICY.CritSampling` | 一次 AttackId 在成功 Release 采样一次，重放不重抽 |
| 冷却 | `STAT.CooldownHaste.Base`；`FORMULA.CooldownMs` | 只影响未来 Release |

- 每项最终属性夹到 `STAT.<属性>.Min/Max`，超界部分不生效、不拒绝构筑；带 DEV 项的属性超上限时拒绝该次投影。当前资源、已有 ReadyAt 与 UI 不反向参与投影；循环依赖在导入或 Prepare 拒绝。
  普攻没有副击：`POLICY.BasicSecondaryTriggers=Disabled`。
- **最大资源变化**：`PrepareBuildChange` 在局部候选上一次验证完整变化、来源、数值、容量与绑定，生成最终属性、资源变换与来源计划；不调 Changed/Sync，不取消动作、不扣机会、不改资源/CD。
  `POLICY.ResourceMaxChange=PreserveRatio` 以旧当前值、旧上限、最终新上限调用一次 `LDAttributeProjection::ResourceMaxChange`，不逐个拆装中间态，不从 HUD 反算，不触发吸血或喷漆清零。
  活体正 HP 降上限后量化为零返回 `NumericUnderflow`，整笔事务不改；非初始化零分母返回 `NumericDomain`（`UndefinedResourceRatio`）。Prepare 还捕获 BodyRef 与属性/资源/状态修订，
  其间受伤、耗蓝、回复或状态到期使旧计划失效，必须重 Prepare。死亡等待选择不对死身体做比例回血，正常复活由生命域满最新上限。
- **回复**：`POLICY.RegenIntegration=CarrySubP2Remainder`，同一资源记录保存有界亚 P2 余数与结算水位；速率/来源变化前先以旧 rate/旧 max 结清到实际边界；满资源丢弃超额与余数；Prepare 不偷偷 Tick；死亡/新生命清余数。
  `LastCombatAt` 只在成功 AttackStart、有伤害效果的成功 Release、承受正伤害（含盾全吸收）时更新；失败尝试、位移、自保盾与选卡不刷新。复活以复活时刻初始化脱战起点。
  参赛者满 `REGEN.OutOfCombatDelaySec` 后另加按当前 Max 比例的脱战回复，与宿舍回复各自起算并与基础回复叠加，野怪不享有；纯规则 `LDTactical::SettleRegen`（`Regenerate` 逐身体调用），起点也是 `NextDueMs` 的边界。

### 劲、连击、爆劲与完美闪避（`LDMomentum`、`LDCombatMomentumDirector.cpp`）

纯值规则在 `LDMomentum`：`InCombat`（距最近一次真实战斗事件不到 `MOMENTUM.IdleDelaySec`）、`Settle`（交战每秒 `CombatPerSec` + 脑力
`MomentumRegen`，脱战每秒 `IdleReturnPerSec` 向 `Start` 靠拢并停在 `Start`，与 `POLICY.RegenIntegration` 同样保留亚 P2 余数）、
`BasicGainP2(IntervalMs)`（10 × 间隔 ÷ 1.334s，向下取 P2）、`Apply`（夹到 0..Max 并返回实际变化）、`NextComboStep`/`ComboRatioBp`、
`OverdriveReady` 与 `IsPerfectDodge`。数值来自 `FLDDemoContentParameters` 的 `Momentum`/`Combo`/`Overdrive`。

- **账本**：唯一的劲入口是 `GrantMomentum(Body, AmountP2, ELDMomentumBook)`，按 `ELDMomentumBook`（Basic/Combat/Skill/Reaction/Dodge/Other/Idle/Spent）
  记入本条生命的账本；死亡、离局与新局前 `LogMomentumLedger` 写一行 `LD_MOMENTUM`。`SettleMomentum` 与生命回复在同一 `RegenerateBodies` 里按真实时间结算交战回复与脱战回落（交战起点 `MomentumEngagedAtMs`）。
- **收入**：普攻主击命中（`OnMomentumHit`）、一次释放对每个目标的第一次原生直接命中（释放预算 `Struck` 去重）、反应触发者、完美闪避、外卖、
  A01/K02/Y04/Y16 等牌、脑力关键石与分支的 `momentum_gain`/`momentum_steal`/`zone_momentum`。野怪没有劲（`CAMP.MomentumPolicy=NotApplicable`）。
- **连击**：`AttackStart` 成功后按 `NextComboStep` 推进并把 `ComboRatioBp` 乘进普攻原始量（暴击前），第 3 段的命中表现用暴击档；
  前摇被取消的普攻不推进；死亡、新生命与休息清零。公开观察把本次普攻段数放进 `ActionComboStep`。
- **爆劲**：客户端劲满时按住技能键 `OVERDRIVE.HoldSec` 后经 `SubmitCharge` 报告蓄力开始（公开 `bCharging`），松开时请求带 `bOverdrive`；
  服务器只在蓄力至少一半时长、释放瞬间 `OverdriveReady` 才由 `ApplyOverdrive` 把伤害、护盾、治疗的基础量与系数乘 `OVERDRIVE.Scale` 并按技能元素加料
  （烫加灼烧层；湿换减速；麻在命中后排一次 `OVERDRIVE.ShockReactionRatio` 倍反应伤害；冻定身 `OVERDRIVE.ChillRootSec`；馊乘馊的时长；吹推开
  `OVERDRIVE.GustPushCm` 并在落点留目标所挂元素的地面状态），然后劲清零。
  不满足时按普通施放结算，不拒绝。眩晕、沉默、击退、劲跌破门槛或死亡经 `EndOverdriveCharge` 取消蓄力，不扣劲；本人视图的 `ChargingEntry` 发布当前蓄力的技能。
- **完美闪避**：D 冲刺开始时在身体上记 `DashStartedAtMs` 与当时脚点；带预警的效果（前摇 ≥ 0.15s 的弹道、直线、扇形、自身范围、地面落点、
  陷阱爆炸、野怪预警技能）结算时调用 `ResolvePerfectDodges(EffectId, Source, InArea)`：开始时在判定内、结算时不在、
  且冲刺开始距结算不超过 `MOMENTUM.PerfectDodgeWindowSec` 的敌方参赛者各得一次（`PerfectDodgeMarks` 按效果实例与人去重），
  记 `PerfectDodges`/`LastPerfectDodgeAtMs` 进本人视图并写 `LD_PERFECT_DODGE`。普攻不检查。

## 来源与冷却

readiness 由 Combat 写在参赛记录中，键为 `LedgerMatch + Contender + SourceKind + 能力定义 + IssuedCooldownId`（`FLDCombatTrack`：Kind/DefinitionId/ReadyAtMs），槽位不入键。
同一可学习主动只发行一条，D 冲刺一条 `Innate`。获得来源的 Prepare：已有记录则回绑原记录与结束时间；本局首次合法学习准备一条无债的就绪记录。
选择、绑定、SourceBindingRevision、机会与投影一并提交，失败全不安装。

- 换槽、升级、遗忘重学、死亡、窗口重开保持 CD；来源丢失继续计时，不存冻结的 Remaining；重学从一级起但回绑原结束时间。Prepare 后先 Release 时重新核验修订，不以旧 ReadyAt 覆盖新债。
- 新 CD/攻击截止用原始开始/释放时刻向上到毫秒加内容向上时长；ready 判断用实际 now 或安全向下的毫秒；已有整数期限不经 double 往返，加法与转换检查溢出。
- 未 Release 的 Action 必须有当前生命、实际来源与能力条款；死亡、遗忘或来源失效即取消。已 Release 的独立 Effect 保存源所有者、SourceBodyAtRelease、SourceRef、等级、目录摘要、实际 AD/AP/暴击、
  空间数据与结束条件；释放者死亡、重生、遗忘不改快照、期限或预算。持续属性与需维持条件的作用在条件失效时停止。目标生命始终严格：self 盾/控制不转到复活身体，旧目标 Hit 不改绑新 Life。
- 离局按 OwnerContender 取消未结 Effect/目标状态/维持贡献并立即移除身体；此前的 HP 损失、盾消耗、控制时段与锁定死亡不回滚。

## 权威时序

只有 `AdvanceAuthority(Match, ThroughMs, Error)`（`LDCombatDriver.cpp`）推进权威批次；GameMode 只拥有同一模拟时钟、`BeginAuthorityStep/ValidateMotionStep`、phase permit、生命与结果。
`FLDAuthorityStep`（Match、StepSerial、Start/EndServerSeconds）、`FLDAuthorityBoundary`（Match、BatchSerial、AtMs）、`FLDGameplayWritePermit`（Boundary、Phase、GateRevision，只由 GameMode 构造，写入前 `ValidateWritePermit`）。

步终点取可用时间、原生最大步长和最早的真实到期点（释放、状态到期、冷却、普攻节拍、脱战回复起点、保护到期、复活、营地整组刷新、旗杆开放、喷上涂鸦与守护完成）。

| 阶段 | 工作 |
| --- | --- |
| Motion | 原生运动分段与存在事件（`PublishCaptureMotion`）；到期 Root 先提交再推进其后分段 |
| Session | 离开/HostEnd、保护半开到期（t 到期先于 t 命中）、旗杆开放条件 |
| Actions | 先结算到本边界的回复，营地 `ProcessAuthorityIntents`，身份前闸、进攻失保、订单/动作准入、AttackStart 与到期 Release |
| Effects | 有序接触/命中/状态/恢复；统一保护闸；首次致零立即锁存；营地成员受到的敌对命中交 `OnCombatHostileContact` |
| DeathsAndRewards | 先结束本集合全部生命（营地 `EndCampMemberLife` → `ConsumeMemberDeath`），再逐个 `CommitDeathReward` |
| RespawnsAndWorld | 复活与保护；营地 `AdvanceCamps`、`SpawnPendingInitialCamps` |
| Transactions | 每步一次 `ProcessTransactions`；同刻 Combat 因果在 Capture 前排空 Effects → DeathsAndRewards 子批，不重开 Actions |
| Capture | 只消费 fence 齐备的进出/资格/打断事件 |
| Close → Publish | `TryCloseMatch` 原子锁结果与闸，`CloseCombatMatch`/`EndMatchActions` 清未结工作，再无 permit 发布（`PublishGrowth`、`ExecuteCapturePublish`） |

- 死亡事实带 `OccurredAtSeconds` 与 checked ceil 的 `OccurredAtMs`；先按真实时刻排因果，真正同刻才按 phase 与稳定次序；同毫秒桶不让较晚的 Leave 排到较早 Hit 前。DueAt 与 RecordedAt 不冒充发生时间；缺当时姿态/接触证据必须阻断。
- 奖励可重试拒绝（`RetryableNotCommitted`）保留原事实（`RetryRewards`），只在下一边界 DeathsAndRewards 开头重试一次；部分提交的结果不进重试；首次非可重试失败或重试仍失败即权威故障，不造 NoReward 或普通胜利，也不覆盖已锁胜者。
  待重试奖励存在时结果不能锁定：同边界 Capture 恰好完成时 Close 保留提案，driver 以同刻结算边界（`BeginSettlementStep`：新 BatchSerial、不推进时间，只有 DeathsAndRewards 重试与 Close）先结清奖励；
  只有故障关闭写闸时奖励记为未支付（`LD_REWARD_UNPAID_AT_FAULT`）。
- 生命变化（Joined/Respawned/ProtectionEnded/Died）在各自阶段向喷漆与守护归约器（`Framework\LDCaptureReducer`）提交资格事实。`CommitControl` 每实际提交一次定身、眩晕、沉默、嘲讽或击退，
  都在同一许可内 `InterruptSpray(Target)` 并经 `ALDGameMode::PublishCaptureInterrupt` 清零该参赛者喷漆进度（事实时刻为边界 AtMs）；缴械、减速和伤害不清零；击退出区由随后的原生运动作为普通离区。
  `PublishCaptureMotion/Eligibility/Interrupt` 是喷漆区唯一窄接口，不新增通用 eventbus。
- 原生 CMC 每身体每步只执行一次；Owner 显示时钟只复制完成步；视野在 driver 推进完成后采姿态发布；`PublishAuthority` 失败使 Core 进入 Fault。UI 同步时钟只作倒计时。
- 营地接入：Motion 调 `AdvanceMemberMotion`；初始出生在开局许可内、参赛者 Joined 之后 `SpawnInitialCamps` 只按名册顺序生成前 `InitialGroupsPerFrame`（3）个，其余由 `SpawnPendingInitialCamps` 每帧至多一批；
  R0 休息期间出现的营地立即冻结，`ThawAfterRest` 先补齐再解冻。DEV `DevRefreshCleared` 只把等待中的刷新截止提前。任一营地入口返回 false 即权威故障。
- 修改阶段顺序会影响同时死亡、资源释放、命中与喷漆/守护完成的同刻竞争，必须连同对应场景验证。

## 状态、伤害和效果归属

HP/劲/盾只在 `Combat.Bodies.Resources` 中以 P2 整数保存，Owner 的点数用于显示。`ApplyHit` 是唯一资源入口：验证 Match/permit、Effect/接触键、目标生命、存活/关系、当前保护与几何，
再按 `FORMULA.Damage` 计算全部后果；源输入用 Release 快照，目标抗性/保护/屏障/HP 用当次已提交真值；每个 Hit 的容量与数值失败先于写入。第一次 HP 到零立即 PendingDeath，
不留同批吸血/回复/盾把它治活的窗口，其后 Hit 不夺取致死来源。吸血只按 `HPLostP2`，普攻命中特效系数 1.0、技能按定义，反伤与附加伤害不触发，所有比例相加，没有上限；
治疗指向原释放身体生命，不治疗复活身体或 PendingDeath。
`ReadObservedPublicFacts` 只读当前身体上明确提交的公开构筑/状态投影。
复活保护仅由 Alive/current Body 且 Core NowMs 小于 Lives.ProtectedUntilMs 派生
`RespawnProtection` 的单层状态及精确期限，不存成可驱散的 Combat buff，不在读取时补默认等级；
命中点以同一生命状态做统一保护闸（免伤免控，群攻副目标排除保护者）。
公开 Root/Shield 提示同样在读取时由当前 Combat 状态派生，不另存一份可漂移的公开状态。
动作提示由 `LDCombatCues::Select` 按 Cast > Attack 选出；被命中从不成为提示（身体保持原姿势，只叠加闪光与火花），
`AppendObservedAction` 给出提示及其开始时刻，本人身体按同一规则与公开时钟取提示与年龄。
提示为 Cast 时，`ObservedCastSource` 由前摇中的动作或最近释放的动作（`ReleasedSource`）给出施法技能的原生 id，
写入公开事实的 `ActionAbility`（Q/W/E），其他提示为 0；
D 冲刺是人人都有的移动，`ActionAbility` 为 0（只显示施法动作，不飘名称）；
来源没有批准定义时 `ReadObservedPublicFacts` 失败，不发布 0。观察者据此只显示施法名，不显示等级、冷却或技能列表。
身体表现只按 cue 字节、动作年龄和状态位绘制（`ULDHumanoidVisualComponent`，时间轴为纯函数 `LDBodyMotion`）。
身体是美术目录（`ULDArtSubsystem`，`DA_DemoArt`）给出的骨骼角色：参赛者按座位色给目录 TintSlots 的 `Tint` 着色，营地按档位使用各自的模型或配色；
动画在 C++ 中单节点播放（无 AnimBP）：站立/跑动（速率 = 速度 / 目录参考跑速，低于 15 cm/s 站立），普攻与施法片段的出手帧（`ImpactSec`）
对齐到当前权威动作的实际起手/释放时间，普攻周期与前摇由当前攻速共同缩放，重复输入与取消不重置已提交的下一次普攻时间。一次性片段每帧按呈现时钟定位；目录缺少身体时记录 `LD_BODY_ART_UNAVAILABLE` 且不画替身。
视觉朝向以 540 度/秒独立追随身体朝向，启动、急停、转向按真实速度/加速度平滑倾斜；不延迟点击意图，不修改权威碰撞、导航或移动速度。本人打中目标时的出手停顿只影响本机姿势，不停止服务器推进。
被命中按公开命中外观使用魔法、火、水、电或普通色闪光并走材质 `Flash` 自发光，命中分类仅读取服务器已提交 payload；
普攻整体前冲并在出手的手部闪光，本人命中确认时这团火花放大（见视野与界面手册的打击确认）；施法时手部显示聚能核心、光环和火星；被命中只有胸前闪光与命中粒子，身体不后退；
公开击退状态期间使用目录 Hit 片段，按可见的实际移动方向倾斜身体，状态结束后恢复正常动画；这只改变本地表现，不改变移动或碰撞；
状态标记与特效部件不进 Lumen 表面缓存和距离场场景（它们频繁显隐，每次显隐都会重建渲染代理），
固定颜色的材质实例由 `ULDArtSubsystem::SharedTint` 在同一游戏实例内共享，只有随每次命中改色的命中火花各自持有；
状态标记对所有单位使用固定状态色，与六个座位色、草坪和红色跑道都拉开：定身为深色描边上的脉动品红圆盘加品红脚镣，
护盾为旋转的亮青色栅栏罩加光环，复活保护为白金光柱加环绕光球；护盾与复活保护的每个标记背后沿本机视线放一个稍大的
近黑副本作为描边，在同色系座位（青色、黄色）身上也能分开；
带复活保护首次出现时由小变大并有光柱；死亡由不复制、无碰撞的 `ALDBodyPuppet` 在原地播放死亡片段倒地，再下沉、缩没
（本人身体须等自身投影确认该命已死，终局或离局移除身体不播放）；甩你一鞋（`active_snark_bolt`）和馊水糊脸（`active_sock_shot`）的飞行由 `ULDFlightTrailComponent` 拖出粉尘，投射物返程时尾迹变为紫色。
驱散不等于回滚伤害，也不等于永久禁止持续来源重新施加效果。
**助攻贡献**只有一个记录入口 `ALDCombatDirector::RecordAssistContribution(Victim, CreditOwner, NowMs)`：
受害者是参赛者、CreditOwner 是另一名有 Combat 记录的参赛者时，按纯规则 `LDAssist::RecordContribution` 在受害者这条生命的
`FLDBodyCombatState::AssistContributions` 里刷新其最近贡献时刻（锁定死亡后不再记录，换命随身体消失）。
`ApplyHit` 在提交后、与受击提示同一条件（正伤害含被护盾吸收的部分、定身/缴械/眩晕、减速）调用一次，
`ApplyTauntEffect` 对每个被嘲讽者调用一次；新增的控制与减益在其施加成功处调用同一入口，不另建记录。
`LockDeath` 只在击杀者合格时以 `LDAssist::SelectAssisters`（`REWARD.AssistWindowSec` 窗口含边界，排除击杀者、受害者与已离局者）
把排序后的助攻者冻结进 `FLDCommittedDeath::Assisters`，之后不随离局或升级改写。

**伤害统计**只有一个记录入口 `ALDCombatDirector::RecapHitCommitted(Hit, Victim, Damage, NowMs)`，`ApplyHit` 紧跟
`LedgerHitCommitted` 调用一次，口径与构筑蓝图的伤害事实相同：对另一个身体实际提交的正伤害（含被护盾吸收的部分），
自伤与私人训练不计。纯规则在 `LDDamageRecap`（`Combat/LDDamageRecap.h`）：
- 承伤：受害者是参赛者时，把这一击（攻击者为记功的参赛者，否则为来源身体的野怪定义，再否则无主；来源 `FLDCombatSourceRef`；
  `DamageP2`）追加进这条生命的 `FLDBodyCombatState::DamageLog`，并丢弃早于 `REWARD.AssistWindowSec` 的样本。反应伤害另带规则链：`FLDCombatHit::ReactionKind`、`ReactionConsumed`、`ReactionSetter`
  与 `ReactionAssistSetters`。`ApplyElement` 按每个被打身体自己的状态记录：主目标记被消费元素的施加者（火取结算的各灼烧份额的
  不同记功者，其他取元素标记的记功者）；导电旁目标记它自己的潮湿与施加者；过载溅射目标没有被消费状态，也没有施加者。
  施加者恰好一名时写 `ReactionSetter`，多名时不具名。规则链写进 `FLDDamageLine::Reaction`、`Consumed` 与 `Setter`
  （施加者不是参赛者时不具名；溅射行只有 `Reaction`）。`LockDeath` 先以 `LDDamageRecap::HideUnreadableSetters` 去掉受害者
  不能读到身份的施加者：只保留受害者本人、窗口内对它造成过伤害的参赛者与冻结的助攻者，其余不具名并合并。
  然后以 `LDDamageRecap::Summarize`（窗口含边界，含致死一击）按攻击者、来源与规则链求和，
  冻结进 `FLDCommittedDeath::Recap` 与 `RecapWindowMs`（助攻窗口）；致死者是野怪时记 `KillerMonsterId`。
  攻击者按总量降序，同一攻击者的行按伤害降序，并列按固定顺序。`ALDGameMode::SendDeathRecap` 另附击杀者与冻结的助攻者。
- 状态引爆提示：反应消费了另一名参赛者施加的元素且触发者也是参赛者时，`ApplyHit` 在这一击完整提交（含 `LockDeath`）且
  造成正伤害或施加了控制后，才调用 `ALDCombatDirector::NotifyReactionAssists`；失败或零伤害无控制的命中不发提示。
  每次反应每个不同施加者（不是触发者、也不是被打身体）只发一次，记 `LD_REACTION_ASSIST`。`ALDGameMode::SendReactionAssist`
  只发给施加者的拥有连接，并要求施加者处于当前比赛的存活生命、最近视野样本观察到被消费状态的身体，
  且命中时仍符合视野域的展示权限（含隔离、PK 队友共享视野与曝光）；提示带施加者的 `Life`。
  触发者的身体须属于其本人并通过同一检查才具名，否则不具名。视野权威故障按命中权威故障处理。
  `ALDPlayerController::ClientReactionAssist` 用
  `LDDamageRecap::KeepReactionAssist`：形状非法的拒绝；早于已知本人 `OwnerState`（比赛、生命）或早于已保留提示的视为过期，
  不改动已保留的提示；其余替换更早（比赛、生命）的提示，取下一个本地序号并保留最近 6 条。可靠提示可能先于复制的比赛与
  本人状态到达，事件播报（`FEventFeed`）对尚未知道的比赛或生命的提示连同其后的提示一起等待，到齐后再按序播报；
  只播报当前比赛与本人当前生命、未过期（`StaleKillMs`）的提示，每条至多一次；首次观察前已保留的提示同样播报，
  核心时钟未知时等待而不丢弃。提示只报告引爆，不改变助攻结算：施加者在施加元素时已按 `RecordAssistContribution` 记作贡献，在助攻窗口内死亡即计为助攻；
  反应消费本身不另记贡献。
- 造成：记功者是参赛者时，累加进 `DamageTotals[CreditOwner]` 的对应来源行；`DamageTotalsOf` 按伤害降序读出。
灼烧、区域、反应与弹射沿用各自命中的 `Source` 与 `CreditOwner`，不另外归类。投送与界面见[视野与界面](VISION_AND_UI.md)。
护盾、普通抗性和条件修正应走统一伤害路径，不能由某个被动在末尾再加一层未经约束的减伤。

临时属性只有一条投影：身体保存构筑投影（`BuildProjection`）与当前生效投影（`AttributeProjection`），
`LDAttributeProjection::ApplyTemporary` 用同一公式在构筑项上叠加限时增益、Y15 缺血攻速、A12 连打攻速与最强一个减速
（负比例），点数类向下取整并夹紧到目录上下限；生命上限不接受临时项。`RefreshAllTemporary` 在 Actions
（回复结算后）与 Effects（命中后）各执行一次，到期、血线越界与施加都在下一次运动前生效；`NextDueMs` 包含这些截止时刻。
减速与限时增益的纯规则在 `LDTacticalRules`（`ApplySlow`、`StrongestSlowBp`、`DispelSlows`、`ApplyTimedBonus`、`TemporaryTerms`）；
减速只减移速，按效果实例保存，只取最强一个。破防与灼烧见下文“构筑 v7 战斗原语”。
伤害系数为基础值、`ad_ratio`、`ap_ratio` 与 `max_hp_ratio`（施放者生命上限，`FLDDamageDefinitionValues::MaxHPRatioBp`），同在抗性前相加；普攻不带生命上限系数。技能命中载荷在释放时快照（含施放者 AD/AP/生命上限，以及按施放者当前等级成长后的数值，见“技能等级成长”）：指向单位、弹体或 `self_area`（释放瞬间脚点为圆心、与横扫共用 `CollectAreaTargets`
的逐目标合法性与视线检查）各得一份；控制与减速随命中经 `CommitControl` 施加。
施放者离局时 `CancelEntityEffects` 一并移除其减速、灼烧层、区域与路径扫掠，营地回位清空全部临时项。


**本人视图**：`FLDCombatOwnerView` 由 `PublishOwnerViews` 写入拥有 Controller 的 `CombatView`，随 OwnerState 单一发布通路同步。信封为 Generation、Life、Revision、AsOfMs、bHasBody、SourceRevision，
消费者先拒旧再替换完整快照。内容：资源 `HPP2/MaxHPP2/MomentumP2/MaxMomentumP2/ShieldP2`、连击 `ComboStep/ComboStartMs`、完美闪避 `PerfectDodges/LastPerfectDodgeAtMs`；`FLDCombatAttributeRowView`（Attribute、Base、Level、Choices、Passives、PercentBonus、Final、bClamped）及 MoveSpeedCmPerSec、
AttackRangeCm、bOutOfCombatRegenActive；Q/W/E/Dash 各一行 `FLDCombatEntryView`（SourceKind、DefinitionId、Rank、TargetMode、RangeCm、MomentumCostP2、Branch、IssuedCooldownId、ReadyAtMs、CooldownMs、仅显示的 Unavailable）；
`FLDCombatStatusView`（StatusId、ExpiresAtMs、AmountP2、ControlBlocks、bIsPassive）、bRooted、NextAttackAtMs、ActionEntry/Phase/Token/ReleaseAtMs、本人提示 `Cues`、`BuildSafeAtMs`。
Alive/ProtectedUntil/LifeRevision 来自生命域 OwnerState。自己受到的状态可解释类型与时限，不借来源字段泄露隐藏施加者。

**对手投影**只由 Vision 的 `FLDObservedUnitView` 维护：合法可见时的名称、等级、HP、劲比例、蓄力、状态与 Q/W/E 原生 ID（`FLDObservedActivesView`，由 `PublicActivesOf` 在构筑安装与身体准备时写入），
没有 IssuedCooldownId、来源修订、CD 或完整属性；状态只取 `AppendObservedStatuses` 的 Root/Shield 与生命域保护。失权立即清面板、标记与附着表现。
弹体由 `ReadObservedProjectiles` 按当前位置与朝向交给观察者，唯一外观为粉笔（`ELDObservedProjectileLook::Chalk`，`active_snark_bolt`），其他来源没有外观则整体失败。
Normal Listen Host 持有权威 Actor 也必须经同一呈现授权（Mesh/阴影/名字、盾/Root/弹体/命中特效、灯光与声音），保留服务器碰撞，原生与观测代理不双重输出。
合法隐藏次击结果不整包发给攻击者，不自动成为 AI/Nav 目标。公共圈与 PvP 击杀通知只带批准身份/原因。

### 控制、强化普攻与反伤

纯规则在 `Combat\LDCombatControlRules`（`LDCombatControl`），Combat 的准入、释放、命中与读模型都只经它判断：

- **动作禁止矩阵**（`Blocks`）：定身按 `POLICY.RootBlocks` 禁移动与位移（冲刺）；缴械禁普攻；沉默禁 Q/W/E；眩晕禁移动、位移、普攻与
  技能。缴械/眩晕实例与定身同形（`FLDCombatRoot`：效果、来源、半开到期），存于身体的 `Disarms`/`Stuns`，
  同一效果不续期，换命即消失，营地回位与离局清除。命中时眩晕放弃任何未出手的动作、缴械放弃未出手的普攻（回执 `ControlBlocked`，
  不付费也不退节拍）；受控期间的 Direct 请求（真人、AI、营地同一入口）在准入时被拒，`ReadCombatBody` 的
  `bAttackAllowed`/`bMovementAllowed` 同步关闭，导航攻击指令因此原地等待而不重复提交；出手时再判一次。
  本人视图给出 Disarm/Stun/Empowered 状态及其 `ControlBlocks`，`bRooted` 表示“移动被禁”（定身或眩晕）；
  观察者看到公开的缴械、眩晕。
- **强化普攻**（`empower_basic`）：每个来源（S11、S17 释放时，D03 冲刺完成时）至多一份待用包（`FLDCombatEmpower`：
  来源、按当时属性快照的物理量 `AmountP2`、期限与 `Charges`），同来源重新武装只替换其未用值与期限（`ArmEmpower`），不同来源并存。
  已提交命中的普攻经 `ConsumeEmpowers` 对每个仍有效的包各取一次，相加后在暴击采样前并入这一次 `BasicPrimary`
  （吃暴击与吸血，连击计数只算一次主击）；到期在 Effects 阶段移除（`ExpireEmpowers`）。本人视图显示一个 Empowered，期限取最晚一份。
- **反伤**（`thorns`，Y01）：构筑触发项，经 `CollectPassiveEffects` 解析。命中后持有者仍存活、伤害为正（含盾吸收）、命中是敌方主普攻、
  距离不超过半径（`ThornsTriggers`；`IntervalMs` 为 0 时没有间隔），且持有者以 `TraceObservation` 观察到攻击者（可见、有视线）时，
  生成一个 `bReflect` 的技能类命中，伤害=基础+护甲×比例+生命上限×比例
  （`LDCombatControl::ThornsBaseP2`，被暴击时乘 `CriticalMultiplierBp`；基础值按拥有者等级成长），
  走同一伤害、归属、死亡锁定与奖励路径；反伤不是普攻，不再触发反伤，也不暴击、不吸血。

### 共用战斗原语

- **唯一控制路径** `ALDCombatDirector::CommitControl(Victim, FLDControlApplication, NowMs, bApplied, Error)`：
  `ELDControlKind { Root, Stun, Disarm, Taunt, Knockback, Slow, Silence }`。硬控（定身、眩晕、缴械、嘲讽、沉默）先经
  `LDCombatControl::DiminishHardControl`（`FLDHardControlChain`：100%/60%/30%，最短 min(200ms, 原持续)，
  窗口为最晚硬控结束后 2 秒）；只有实际落地的控制才推进计数与修订。嘲讽只作用于参赛者。击退（不递减）在本函数内提交其禁动窗口
  （`Knockbacks`），放弃未出手的动作并递增 `KnockbackSerial`；位移本身由 `ApplyKnockback` 在提交成功后启动（见下节）。
  每次实际提交的定身、眩晕、沉默、嘲讽、击退在此调用喷漆打断，每次实际提交的控制与减速在此记录助攻贡献。
  `IsImpaired(Body, Now)` 判断任何控制（含击退、沉默）或减速（`control_damage_amp` 的“被减速或被控制”）。
- **命中特效**：`FLDCombatHit::OnHitScaleBp` 是该命中的特效系数（普攻主击 10000，技能、附加伤害与反伤为 0）；
  `SnapshotOnHit(Body, ScaleBp, Hit, Error)` 在出手时快照吸血（Y17），`ApplyHit` 按系数提交；`FLDCombatHit::LifestealShareBp`
  是主击自身伤害包在原始伤害中的份额（`LandBasic` 按 自身 /（自身 + 并入强化包）计算，默认 10000），吸血只按该份额的 HP 损失回复。
- **原生直接命中**：`IsNativeDirectHit(Hit)`——技能类命中、`bDirectAbilityDamage`、来源为已学主动，且不是反伤、附加伤害、灼烧、
  状态结算、骑乘（`bRider`）或护盾触发的命中；普攻不是。之后的派生命中只要是宿主主动来源的技能直接伤害、沿用释放 EffectId 与 `CastLedger`，就自动算作原生直接命中。
  `bAddedDamage` 的命中不暴击、不触发命中特效；`AddedHit` 是附加伤害命中的构造。
- **伤害求值顺序**（`ApplyHit`）：普攻暴击 ×`STAT.CritMultiplier`（技能只在 Y09 下暴击）→ 乘区之积 `FLDDamageEvaluationInput::DamageFactorBp`
  （`LDTactical::DamageFactorProduct`，见“构筑牌”）→ 破防后的抗性（`LDCombatKeywords::BreachedResistanceP2`：共享破防层的固定值）→
  护盾 → 生命；吸血不设上限，按 `HPLostP2 × LifestealShareBp` 经 `HealBody` 回复；灼烧与反伤不吸血。
  `LD_HIT` 的 `factor_bp` 是乘区之积，`factors` 列出各键的 Bp。
- **剩余冷却缩减**：`ALDCombatDirector::ReduceCooldown(Entity, ELDCombatEntry, AmountMs)` 与按定义查找槽位的
  `ReduceLearnedCooldown`，都经纯函数 `LDCombatSourceRules::ReduceReadyAt`（不早于当前时刻；`MAX_int64` 表示立即就绪；
  已就绪轨道不变）。
- **位移判定**：`LDCombatSourceRules::DisplacesCaster` 按条目的数值判定它是否位移施放者（冲刺类主动）；
  `CastRangeCm` 统一给出技能施法距离：目标定义的射程，加上目标定义 `AttackRangeRatioBp` 允许的额外普攻距离份额（有上限；当前目录没有条目使用）。
- **触发效果查找**：`CollectPassiveEffects(Projection, Op, Out, Error)` 是牌效果的唯一读取口：遍历投影的
  `InstalledCards`，从目录取每份牌的 Op 效果，固定量按持有者等级以 `LDSkillGrowth::GrowEffect` 成长后按值返回
  （`FLDPassiveEffectRef`：Key=牌 id、序号、`Source.Kind=Card`、位置、技能牌的 `Active`、Y25 的 `Element`）；目录缺牌或成长溢出是明确失败。
  `FindTrigger` 返回第一项并传出失败。已退役的效果操作编号不复用（`native_ids.json` 的 `retired_ranges`）。

### 构筑牌（`LDCombatCards.cpp`、`LDAttributeProjection`）

已安装的牌（`FLDBuildProjectionInput::Cards`，`FLDInstalledCardValue{CardId, Serial, TargetKind, Active, Element}`，按序号排序，
同名多份各占一行）先经 `LDAttributeProjection::Project`：未知牌、重复序号或形式与位置不符都明确拒绝；Y13 护甲转生命与本局成长
（`CardGrowth`：Y19/Y20/Y21 的累计值）进入 `Cards` 属性项，Y15 成为 `MissingHPAttackSpeedBp`，每张牌列入 `InstalledCards`。
其余效果由 Combat 经 `CollectPassiveEffects`（同 `LDCombatSourceRules::CollectCardEffects`）读取：每份一条 `FLDPassiveEffectRef`，
`Key` 是牌 id（同名多份的乘区分组键），带序号、位置与 Y25 的元素。

- **乘法伤害**（spec §3.7）：一次命中的加成来源是 `FLDDamageFactor{Key, Bp}` 列表；`LDTactical::DamageFactorProduct` 先把同一 `Key`
  的 Bp 相加，再把各组的 (1 + ΣBp) 相乘，结果以 `DAMAGE.FactorTechCap` 为技术上限（触顶 `LD_DAMAGE_FACTOR_CAPPED`），负值、无名键或
  int64 溢出明确失败。`EvaluateDamage` 读 `DamageFactorBp`（10000 = ×1），在暴击之后、抗性之前乘一次。释放时锁定的乘区
  （K09/K12 `skill_damage_factor`、K16 闲置、K17 成长）挂在 `FLDCombatCastLedger::Factors` 上，随本次释放的每个原生直接命中生效；
  K04 记号、Y22 蓄力、Y27 反应倍率与力道关键石在命中时加入。`LD_HIT` 写 `factor_bp` 与 `factors=键:Bp,...`。
- **连锁保护**（spec §3.8）：`FLDCombatHit::ChainDepth` 是派生深度；`ApplyPendingHits` 给每个派生命中记上一层，超过 `CHAIN.MaxDepth`
  的不再派生（`ChainAllows` 在卡牌派生处同样判断），一个权威步最多处理 `CHAIN.MaxEffectsPerStep` 个派生，其余留给下一步，
  都记 `LD_CHAIN_GUARD`。
- **连锁记录**（spec §6.5，`LDCombatChains.cpp`、纯规则 `Combat\LDChainRules`）：`FLDCombatHit` 与 `FLDCombatAction` 带 `ChainId`。
  技能、普攻与冲刺的释放各开一条连锁（`OpenChain`），`EffectChains` 把释放的 EffectId 映到它，派生命中在 `ApplyPendingHits` 继承；
  释放与命中结算期间 `CurrentChain` 指向当前连锁。步骤来自牌的触发（`LedgerCardTriggered`）、元素反应、地面状态生成与参赛者击杀
  （`LDChain::ReactionStep/GroundStep/KillStep`）。`AdvanceChains`（`LDCombatDriver` 每步调用）在 `CHAIN.EndSec` 没有新步时结束连锁；
  至少 `CHAIN.MinKeySteps` 种不同步骤的连锁得出键（`LDChain::Key`，步骤种类按首次出现排列），记 `LD_CHAIN`（每人只记最长的
  `LoggedPerOwner` = 5 条，`KeepLongest`），本人最近 `OwnerRecentEnds` = 4 条写进 `FLDCombatOwnerView::Chains`（只给主人，带递增 `Serial`）。
  `ReadResultHighlights` 给结算的每名参赛者最长连锁与对参赛者最高的一下（`FLDDamageSummaryView::Highlights`）。
- **普攻牌**：A01 第 3 段回劲；A02 普攻命中随机一个未就绪技能冷却减少；A03 第 3 段命中算一次技能命中（劲与技能命中类牌，
  不触发技能本身效果）；A04 第 3 段真实击退；A06 第 3 段变成 N+1 下，各 0.8；A09 超出攻速上限的部分换成多打一下的概率；
  A11 附加法强魔法伤害（`FLDCombatHit::bRider` 骑乘命中，与主击共享暴击与乘区，不再触发）；A12 同一目标连续命中叠攻速；
  A14/A15 主击加破防/灼烧层；A16/A17 主击挂湿/麻；A05 普攻变成飞行物（射程投影加长，命中即停）；A07 第 3 段扇形横扫；
  A08 第 3 段命中按 Q 的射程以缩放效果放 Q（`QueueComboCast`，不耗劲、不进冷却、不触发爆劲）；A10 低攻速换普攻乘区；
  A13 普攻命中参赛者计数成长攻击力。
- **技能牌**（宿主是 `Active` 那个已学技能）：K01/D02 命中或接触后其他技能冷却减少（`HasteOtherActives`）；K02 命中回劲；K04 记号；
  K06 释放后排程 N 次自动重放（`ScheduleAutoRecasts`，60% 效果、不耗劲，`RunAutoRecasts` 在动作阶段执行）；K08/K09 冷却与劲耗
  倍率在 `CompileCardActive` 编进 `FLDCombatActiveTerms`（同名相加）；K12 改用生命支付（`HPCostBpPerMomentum`，生命不足拒绝）；
  K03 本技能命中过的参赛者在助攻窗口内死亡且主人得到击杀或助攻时刷新本技能冷却；K05 造成控制后武装下一个技能乘区
  （`ControlAmp*`）；K07/K10 改造范围与射程（`host_hit_types` 限定宿主；K10 远距命中乘区）；K11 控制时长与冷却（`host_control`）；
  K14 命中时消耗护盾转为同类型伤害；K18 按最近施放的其他技能数加乘区（`RecentCasts`）；
  K13 附加攻击力物理伤害（骑乘命中）；K15 技能吸血；K16 就绪后闲置计时；K17 命中参赛者累计成长；K19–K21 命中挂元素；K25 触发反应时本技能冷却减少。
- **冲刺牌**：D01 接触敌人刷新冲刺冷却（每次冲刺一次）；D03 冲刺结束武装强化普攻；D05 冲刺分段（`SplitSegments`，同名多份多段，
  冷却不变）；D06 穿过敌人，对每个穿过的身体经 `MakeBasicStrike` 打一次普攻（算普攻命中，不推进连击）；D12 接触挂麻；
  D04 完美闪避后下一个技能免劲（`FreeCasts`，`OnPerfectDodgeCards`）；D07 冲刺加长并撞人（`DashRamHit`）；D08 冲刺耗劲换冷却
  （`DashMomentumCostP2`）；D09 完美闪避计数成长移速。
- **本体牌**：Y01 直接伤害（含护盾吸收）按 25%/份反弹魔法伤害，反弹不再反弹、可吸血；Y02 击杀或助攻刷新全部技能冷却并回满劲
  （`OnCardTakedown`）；Y03 低血 2 s 内每次伤害降为 1（每条命一次）；Y04/Y05 反应回劲/挂新元素；Y07 受伤护盾（冷却按持有者）；
  Y08 击退撞墙眩晕、撞人两人受伤；Y09 技能可暴击；Y10 吸血对技能生效；Y14 溢出治疗转护盾（`OverhealShield`，与现有护盾取强）；
  Y16 技能急速换技能命中回劲；Y17 普攻吸血；Y19/Y20/Y21 本局成长（计数乘以当前持有的成长牌，`CardGrowthTerms`；成长事件时 `RefreshCardGrowth` 重算，构筑变更的 `PrepareBuildChange` 按提议的牌重算，卸下即失去、加装即按已有计数生效）；A11/K13 的附带伤害不触发任何效果，包括受击方的技能分支效果；Y22 不造成伤害的时间转下一次伤害乘区；
  Y25 光环把选定元素挂给 200 cm 内对你造成直接伤害的人；Y26 你施加的元素持续时间 ×2；Y27 你触发的反应伤害乘区；Y28 灼烧上限；
  Y06 护盾被打破时周围魔法伤害（`OnShieldBrokenCards`）；Y11 每个身体自己的爆劲门槛与倍率（`OverdriveThresholdOf/ScaleOf`，
  多份取同一值；`FLDCombatOwnerView::OverdriveThresholdP2` 供 HUD 与输入）；Y18 移速高于基础换乘区；Y24 命中记账、满额兑现
  （`OnTallyHit`）；Y29 造成的灼烧伤害转护盾（`OnBurnDealtCards`）。
- 牌的状态（Y03 一条命一次、A12 目标与层数、K06 排队的重放、K16/Y22 计时、K17/Y19–Y21 累计）在 `LDCombatRest.cpp` 按生命或对局重置；
  只读预览（`LDWorkshopPreview`）只覆盖 K08/K09 的冷却劲耗与 Y13，其余写为部分。

### 战斗机制（`LDCombatMechanics`、`LDCombatMechanicsDirector.cpp`）

纯规则在 `Combat\LDCombatMechanics`（护盾打破判定、击退方向、区域脉冲时刻），
Combat 身体记录上的应用在 `LDCombatMechanicsDirector.cpp`；使用它们的条目与数值见[内容与数值 §4–§5](../demo/CONTENT_AND_BALANCE.md#4-技能状态和属性目录)。

- **击退**：`ApplyKnockback(Target, Body, Hit, Now, bApplied, Error)` 先经 `CommitControl(Knockback)` 提交窗口，再用与冲刺相同的
  `ULDCharacterMovementComponent::StartDemoDash` 沿地面推开（遇阻停在最远合法点）；方向由 `KnockbackDirection` 从施放点（命中的
  `KnockbackAnchorCm` 或来源脚点）指向目标脚点。窗口内 `IsActionBlocked` 拒绝移动、位移、普攻与技能；Navigation 看到
  `ReadKnockbackSerial` 变化即放弃当前订单（`ELDOrderFailure::KnockedBack`，AI 视为 Dropped）。公开状态 `knockback`。
- **护盾入口**：所有护盾（技能、受伤触发护盾、按嘲讽人数的护盾）只经 `GrantShieldTo` 以 `ReplaceIfNotWeaker` 安装；
  `ShieldGrantedP2` 记录当前记录的获得量。被伤害打空（`LDCombatMechanics::ShieldBroken`，到期或被消耗不算）时触发反击
  `OnShieldBroken`（`shield_break_retort`，内置冷却 `RetortReadyAtMs`；当前目录没有来源）；
  这些命中带 `bFromShieldTrigger`，不算 Q/W/E 命中。
- **治疗入口**：一次性治疗（吸血）经 `HealBody`；自然与脱战回复不经过它。没有治疗削减。
- **区域**（`zone`，脚底开锅）：目标 `GroundPoint` 的请求带服务器验证的地面点（`ELDCombatTargetKind::Point`），释放时夹到目标射程，
  `ReleaseMechanics` 在该点登记 `FLDCombatZone`（载荷在释放时快照）。`zone_damage` 按 `NextPulseAtMs` 的定次脉冲结算伤害；
  释放锁定的乘区随区域在释放时快照，随每一跳生效；
  `zone_slow` 每 `IntervalMs` 判定一次，给区内敌人施加持续 `DurationMs` 的减速（不再有 linger）；有伤害的区域的 `zone_burn` 必须与脉冲同拍
  （同间隔、无延迟、脉冲覆盖整个时长，否则释放时明确失败），每一跳的伤害命中就带 `PulseBurnStacks` 层火，与该命中的牌元素同一元素批次；
  没有伤害的区域的 `zone_burn` 才按自己的 `interval` 单独加层。
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

### 构筑 v7 战斗原语（`LDCombatKeywords`、`LDCombatKeywordsDirector.cpp`）

纯规则在 `Combat\LDCombatKeywords`（共享破防与灼烧），
Combat 身体记录上的应用在 `LDCombatKeywordsDirector.cpp`。关键词参数是内容参数 `COMBAT.Breach*`、`COMBAT.Burn*`
与 `SKILL.GrowthPerLevel`、`SKILL.GrowthPerLevelAfter10`（`FLDKeywordTerms::From`）。目录有 22 个单级主动，字段用法见
`FLDDemoEffectDefinition` 的注释。效果触发 `ELDDemoEffectTrigger` 有 `None`、`BasicHit`、`AbilityHit`、`Damaged` 与
`DashComplete` 五种（反伤的 `trigger` 是它自己的字段，只接受 `Basic`）。

- **沉默**（`silence`，`ELDControlKind::Silence`，状态 `silence`）：实例形同缴械/眩晕，存于 `Silences`；矩阵动作 `ELDCombatControlAction::Skill`
  只被沉默与眩晕禁止。Q/W/E 的准入、释放与 `EntryView` 都经 `IsCastBlocked(Body, SourceKind)`（Cast 对所有入口，Skill 只对 Learned），
  D 冲刺不受影响。`CommitControl` 中它是硬控（递减），落地时放弃前摇中的 Learned 技能（不付费），并清零喷漆。
  本人视图状态带 `SilenceBlockBits()`（Cast），观察者看到公开状态；对手 AI 在沉默期间把 Learned 来源视为未就绪，并把带沉默的技能当作可打断喷漆的控制。
- **共享破防**（`breach`）：`FLDBodyCombatState::SharedBreach`，所有来源同一组，每层固定 `COMBAT.BreachPerStack`，最多
  `COMBAT.BreachMaxStacks`，新层刷新整组（满层只刷新）；作为额外固定值进入 `BreachedResistanceP2`；来源离局层保留；营地回位清空。
  显示为 `exposed`（破防）带层数。
- **灼烧**（`burn`，为火元素）：`FLDBodyCombatState::Burn`，**每层独立计时**：施加时起 `COMBAT.BurnSec`、每 `COMBAT.BurnTickSec`
  一跳（最后一跳落在本层截止时刻），每层记住施加者并快照其**等级与法强**（`FLDCombatBurnLayer::Level/SourceAPP2`）；
  `AdvanceBurns` 用 `LDCombatKeywords::PopBurnTick` 取最早到期的一跳，同一时刻、同一施加者与来源的层合并为一个魔法 tick 命中（`bBurnTick`、`bPeriodic`：
  不暴击、不触发命中特效、不吸血，带施加者存活时的受控增伤）；满层时新层替换最早结束的一层；施加者离局移除其层，营地回位清空。每层每秒
  `LDCombatKeywords::BurnLayerPerSecondP2(Level, AP)` = G(L施加者)·`COMBAT.BurnPerSec` + `COMBAT.BurnAPRatioPerSec`×法强，
  G(L) 取 `LDSkillGrowth::FactorBp`（施加者等级由 `GrowthLevelOf` 给出，营地身体按 1 级）。被反应消费时 `BurnRemainingShares` 给出各层尚欠的跳数之和。
- **施加途径**：破防的 `stacks` 随命中载荷，普攻牌形式（A14 的 `basic_stacks`）在 `ApplyKeywordStacks` 中按存活施加者对每次普攻主击结算；
  火（命中载荷的 `BurnStacks`、A15 的 `basic_stacks` 与 K19 的火）属于元素批次（`ApplyElements`，见“水火电与反应”）；
  周期、结算、反伤、附加伤害与护盾触发的命中不算；区域的火见上文 `zone_burn`。
- **多次强化普攻**：`empower_basic` 的 `charges`（`FLDCombatEmpower::Charges`，`ConsumeEmpowers` 每次取一层）与 `stats`（期间的限时属性，
  随最后一层或窗口结束）。
- **受伤触发护盾**：`triggered_shield` 的 `Damaged` 触发（`OnDamaged`，Y07）：承受敌对正伤害且仍存活后给护盾（按等级成长的 base + 生命上限比例），
  只有真正替换成功才开始 `interval` 冷却；弱盾不授予、不刷新。
- **定身**：`root` 对减速中的目标仍是定身（`DemoBuildPrimitives.RootOnSlowed`）。
- **解除**：解除定身与解除减速在释放前经 `ApplyCleanses` 结算，缴械不被解除。
- **普攻命中缩短三个主动冷却**：触发项 `cooldown_refund` 的 `BasicHit` 触发（无窗口）在每次普攻主击时缩短 Q/W/E 三个技能的剩余冷却。
- 属性 `MomentumRegenFlat`（交战中的劲回复固定值，`EAttribute::MomentumRegen`）与 `HPRegenFlat` 同形。

### 元素、反应与地面状态（`LDCombatElements`、`LDCombatElementsDirector.cpp`、`LDCombatGroundDirector.cpp`）

纯规则在 `Combat\LDCombatElements`（6 × 6 反应表 `ReactionOf`、每种反应的表项 `RuleOf`、R、麻的计数、馊的跳伤与削减、地面状态的转换表
`GroundConversion`、所含判定 `GroundContains`、上限淘汰 `GroundEvictions`、S16 记录与反震量、命中类别），身体记录上的应用在
`LDCombatElementsDirector.cpp`，地面状态在 `LDCombatGroundDirector.cpp`。参数是 `COMBAT.*` 的元素与反应项和 `GROUND.*`
（`FLDElementTerms::From`、`FLDGroundTerms::From`，不可用即明确失败）。数值单点见[内容与数值](../demo/CONTENT_AND_BALANCE.md) §4.4。

- **元素槽**：`ELDCombatElement` 为 烫 1、湿 2、麻 3（原“电”的编号）、冻 4、馊 5、吹 6。每个目标生命一个元素：烫 = 存在活的灼烧层；湿、麻、冻、馊是
  `FLDBodyCombatState::ElementMark`（施加者与其等级、法强快照、到期时刻）。湿带一个移速减速实例，冻带一个移速减速与一个攻速减速实例
  （`SlowEffectId`、`AttackSlowEffectId`，与标记同生同灭）；麻记从挂上起受到的直接伤害次数 `ShockHits`；馊记削减比例 `HealCutBp` 与下次跳伤
  `NextTickMs`。同元素刷新时长并换成最新施加者（麻保留计数，馊保留节奏并取较强削减）。吹从不挂上。
- **批次**：`ApplyHit` 在伤害、控制与 `ApplyKeywordStacks` 之后、`bControllable` 时调用 `ApplyElements`：先烫（命中的 `BurnStacks`、存活来源的
  触发项 `burn` 与牌的火），再按 湿、麻、冻、馊、吹 依次施加命中的原生元素（`FLDCombatHit::NativeElements` 位）与牌的同种元素（`CardElements`）。
  原生元素读命中的施加者快照 `ElementLevel/ElementAPP2`（释放时由 `StampElementSnapshot` 填写）；牌的元素以该牌为来源、按存活持有者当时的
  等级与法强施加。命中可带馊的自有削减与时长（`RotCutBp`、`RotMs`，S21 3A1、N3 3B1）、吹的自有推开距离与方向（`GustPushCm`、`GustDirection`，
  风扇与末波）以及吹的爆劲落点地面状态（`bGustGround`）。
- **麻**：持有者受到直接伤害（不是 `bStatusSettle`、`bBurnTick`、`bPeriodic` 或麻自己的打击）时 `CountShock` 计数，第 `COMBAT.ShockEveryNthHit`
  次由 `StrikeShock` 对持有者与施加者在 `COMBAT.ShockSplashRadiusCm` 内的其他敌人各造成 `COMBAT.ShockRatio` × R（`bOverchargeDischarge`，不再计数，
  写 `LD_SHOCK`）。S10 3B1 过载期间由 `OnBranchHitTaken` 每次受击都放电，计数不动。
- **馊**：`AdvanceElements` 按 `COMBAT.RotTickSec` 从挂上起逐跳结算施加者快照的 `RotTickP2`（周期性状态伤害，含到期那一跳）；
  `HealBody` 与 `GrantShieldTo` 对新治疗、新护盾先按 `AfterRotP2` 削减（护盾削减后再做替换判断），已有护盾不变。
- **吹**：`PushGust` 排一次无伤害的状态命中，沿 `GustDirection`（没有时为施加者脚点指向目标）推开 `COMBAT.GustPushCm`；冰面上的击退距离 ×
  `GROUND.IceDisplacementScale`。吹的爆劲另在推开落点生成目标所挂元素对应的地面状态。
- **反应**（`ELDCombatReaction` 1–15，触发者 = 新元素施加者）：R = `COMBAT.ReactionBase` + `COMBAT.ReactionPerLevel`×(L−1) +
  `COMBAT.ReactionAPRatio`×法强（魔法，不乘 G）。旧元素被消耗（火旋保留灼烧、雷暴重新挂麻）；起雾立即结算灼烧（已有火按 `BurnRemainingShares`
  各归原施加者与来源，新火按其完整预算归触发者，命中带 `bBurnTick`），其他消耗烫的反应直接清掉灼烧。主目标的反应命中按表带伤害倍数与控制或状态
  （过载与腐蚀的破防、导电减速、冻住定身、熏缴械、冻疮攻速、泼洒与雷暴的击退），周围附带按表打击 `RuleOf` 的半径内其他敌人（导电只打此刻湿的、
  并消耗其湿）；发酵、沼气爆、雷暴、火旋在主目标上施加新的馊、灼烧、麻或加倍灼烧；碎冰在目标上记 `ShatterBp/ShatterUntilMs`，下一次技能命中取
  这一乘区并用掉。随后新元素对脚下地面状态做转换，再由反应在脚下（雷暴为击退落点）生成表中的地面状态。触发者获得 `MOMENTUM.Reaction`；完成的反应经
  `OnReactionCompleted` 记入本次 Effects 的 `ReactionFacts`（含被消耗的元素），并写 `LD_REACTION`（`kind`、`held`、`incoming`、`r`、`ground`）；
  目标身体的 `Cues.LastReaction/LastReactionAtMs` 供观察者头顶飘字。
- **地面状态**（`FLDGroundState`：公开序号、种类、主人快照、圆心、半径，D10 的带形半长、N1 的扇形角、到期、下次结算、版本、来源）：
  `SpawnGround` 生成；技能自己放下的（施加者来源为已学技能且带释放 id）同一技能只留 1 份（Y12 每份 +1），然后按 `GroundEvictions` 让主人超出
  `GROUND.MaxPerOwner`、全场超出 `GROUND.MaxTotal` 的最早者先消失（写 `LD_GROUND_EVICT`）。`AdvanceGround` 在每个 Effects 步：到期移除；
  逐个到点的结算对主人的敌人（脚在形状内）施加站立效果（水洼湿、火场灼烧、毒雾馊与 N3 3A1 攻速降低、电场 0.3R 与麻、风雪区减速与冻）；
  再读每个活身体脚下：踩在敌方冰面上记 `bOnEnemyIce`（移动参数），踩在自己的地面状态上记 Y23 的起点 `TurfSinceMs`。`ConvertGroundAt` 处理
  “再加元素”：转换后归新施加者、重置寿命；吹动的保留主人；起雾、沼气爆（区内每人 `COMBAT.MethaneRatio` × R）与电场放电（`GROUND.FieldDischargeRadiusCm`
  内每个敌人 R）都算新施加者，写 `LD_GROUND_CONVERT`。地面状态不改寻路、不挡视线、不碰撞。
- **冰面惯性**：权威移动参数。服务器 `ALDCombatDirector::ReadMoveAccelScale` 在 `bOnEnemyIce` 时给 `GROUND.IceAccelScale`，本人 `CombatView.MoveAccelScale`
  发布同一值；`ULDCharacterMovementComponent::GetMaxAcceleration`/`GetMaxBrakingDeceleration` 两侧都乘它（服务器读 Combat，拥有客户端读本人视图），
  预测与校正用同一参数。冲刺距离与击退距离在敌方冰面上 × `GROUND.IceDisplacementScale`。
- **结算伤害**：反应、麻、馊、地面状态与提前结算的命中 `bStatusSettle`（或 `bBurnTick`），没有元素载荷，不吸血，不是技能命中；
  `LDCombatElements::CategoryOf` 给出 AI 规范 §8 的类别（`BasicPrimary`、`SkillDirect`、`SkillHit`、`StatusSettle`）；区域原生脉冲与 S16 反震是 `SkillDirect`。
- **来源离局**：其雾区、风扇、地面状态、S10 弹射与它施加的元素（含减速）一起结束。
- **S09 直线**（`ELDDemoTargetMode::DirectionLine`）：释放时 `CollectLineTargets` 取施法者脚下沿方向 `RangeCm`、半宽 `LineHalfWidthCm` 带内、
  视线可达的合法身体，击退沿施放方向（锚点设在目标脚后）。**S10 弹射**（`chain_bounce`）：首目标照常命中，`FLDCombatChain` 之后每 `interval`
  从上一个被击中者（死亡则用其最后脚点）找 `radius` 内最近、未被这次弹射击中的合法敌人；没有就弹回第一个目标（规格 §9，单挑时连续弹回），
  伤害按 `DeliveryScaleBp` 逐跳乘 `ratio`；第一个目标已死则结束。**S13**：`dash_contact` 的 `targets: Every` 让每个实际接触者各受一次原生命中。
- **S22 / N1 扇形**（`channel_cone`）：吹只随最后一波（`bFinalGust`，沿施放方向），N1 3B1 的定身也只在最后一波（`final_root`）；N1 2A 在释放时以
  施法者脚下为顶点、沿施放方向生成扇形冰面（`ground_state`）。
- **N2 风扇**（`fan`，`FLDCombatFan`）：放在目标点（不超过射程），朝施放方向；`AdvanceFans` 每 `interval` 对前方 `length` × `width` 矩形内主人的敌人
  施加一次吹（推开 `distance`，带 3A1 撞墙眩晕；3A2 时带上主人最后施加给别人的元素 `LastAppliedElement`），吹到人时按 3B1 回劲，并对圆心在矩形内的
  地面状态做一次吹的转换（火场与毒雾被吹走、电场放电）；2B 的朝向按三角波左右摆。主人死亡或离局结束；同一技能只留 1 个（Y12 每份 +1）。
  观察者看到它当前吹的矩形（`Wind` 外观）。
- **N4 书包**（`bag`，`FLDCombatBag`，`LDCombatBagDirector.cpp`）：放在目标点（不超过射程），不是身体，不能被普攻或单体技能选中。生命为 `base + per_level×(L−1)`（2B 乘 `hp_ratio`）；服务器放一个只挡身体的阻挡体 `ALDCombatBagBlocker`（Pawn 通道阻挡，不挡视线、镜头与导航，不复制），观察者看到的书包与书包墙（`Bag`、`BagWall` 外观）在拥有客户端上有同样的阻挡，使预测与权威一致。敌方弹道在 `AdvanceProjectiles` 里先撞到书包就停在书包上（`FirstBagOnSegment`，本人的弹道穿过）；敌方弹道、地面爆炸、扇形与释放范围按命中的原始量（`RawBagDamageP2`：基础 + 攻击/法强系数，乘投送比例，不计暴击、乘区与抗性）扣它的生命，持续区域的脉冲不打它。`AdvanceBags` 在到期或生命归零时移除它并在 `BlastRadiusCm` 内对主人的敌人结算释放时快照的载荷（击退从书包中心推开；3A2 挂冻），爆炸也打其他敌方书包；3B2 在原地留主人的火场。同一技能只留 `count` 个（2A 为 2，Y12 每份 +1），最旧的直接消失；3A1 让新书包与同技能的上一个书包之间连一道墙，任一书包消失墙就消失。主人离局时书包一起移除，日志为 `LD_BAG`、`LD_BAG_HIT`、`LD_BAG_BLAST`。
- **N3 与 S21 3A2**：弹道去程终点（命中点或射程尽头）按 `ground_state` 生成毒雾（N3 2A 半径 ×1.5、3A1 攻速降低、3A2 7s）。N3 2B 的二次爆炸是
  `follow_up`（3B1 带 8s 馊，3B2 落下时回劲）。
- **S16 顶着揍**（`brace`，`FLDCombatBrace`）：释放时固定减伤比例、记录上限（`limit`，随 G）与反震基础（`base`，随 G）；期间来源不是自己的伤害在
  `EvaluateDamage` 中于抗性、乘区与投送比例之后被削 `BraceCutBp`，实际削掉的 `BraceCutP2` 计入记录（最多上限，受保护目标为 0）。
  不因攻击、移动或施法取消；自然结束时存活身体在当时脚下对 `radius` 内敌人造成 `base + record_ratio×记录` 的魔法直接伤害（`LD_BRACE_ANSWER`）；
  死亡（`bPendingDeath`）、离局或新释放直接丢弃，不补爆。状态 `braced` 显示剩余时间。
- **S20**：`zone_wet` 让每次区域脉冲同时施潮湿，`zone_haste`（`UpdateZoneHaste`）在拥有者及其本场队友（释放时的 PK 隔离组与同一非零 PK 方）
  脚站在区内时给一份以区域效果 id 为键、到区域结束的移速限时增益，离开、死亡或冻结即撤；敌人与区外队友没有。
- 牌：A18/A19 是普攻的 `basic_element`，K22–K24 是宿主的 `skill_element`；D10（`dash_ice`）与 D11（`ground_relocate`）在 D 完成时
  （`ResolveDashCompletions`）生成路径冰带或把主人最新的地面状态、区域或未触发陷阱挪到脚下（各一次，跟随的区域不算）；Y12（`double_stall`）
  在陷阱、风扇与技能放下的地面状态上多留一份；Y23（`ground_growth`）由 `GroundFactors` 按站上自己地面状态的整秒数给乘区，N1 3A1 的冰面易伤也在这里。
- 观察：`wet`、`electrified`（麻）、`braced`、`chilled`、`rotten` 是公开状态（30–32、36、37）；观察唯一性检查是 64 位掩码。

交战窗口（`GROWTH.BuildSafeWindowSec`）由 Combat 记在实体记录的 `LastBuildUnsafeAtMs` 上（跨生命保留，只前进）：它不限制任何成长命令，只供 HUD 在交战中收起野外摘要。
`StartNavAttack` 受理的普攻起手、`LDCombatSourceRules::IsOffensive` 的主动释放、`ApplyHit` 中来源不是自己的正伤害（标记受害者并记入
拥有者，灼烧 tick 同样经此路径）、`CommitControl` 实际落地的硬控（定身、眩晕、缴械、嘲讽、沉默）或击退都刷新它；
减速、护盾、回复与非进攻主动（贴脸补一拳、锅盖挡脸、左右开抽与天生冲刺）不刷新。`LDCombatSourceRules::BuildSafeAtMs` 给出
`LastBuildUnsafeAtMs + 窗口`（没有记录为 0），本人 `CombatView.BuildSafeAtMs` 发布这一值（`ReadBuildSafeAtMs` 保留为读取入口，成长提交与 AI 不再读）；
DEV 重置把它清零。

### 局末构筑蓝图

`ALDCombatDirector` 持有本局唯一的 `FLDBuildLedger`（`Development/LDBuildBlueprint.h`，挂点在 `LDCombatBlueprintLedger.cpp`），
它只读 Combat 已安装的 `FLDBuildProjectionInput`，不是第二份构筑真相。版本按**结构**划分：持有点数的五维、Q/W/E 主动与已装的牌
（每份一行：牌、位置 `basic`/`skill`/`dash`/`body`、技能牌的 `host`、Y25 的 `element`，按这四项排序）；只换安装、改位置或改元素也开新版本，重构前后的收益分段归属。只升级或只改 DEV 测试层的修订留在当前版本（更新等级区间与修订区间，
`dev_layer` 标记测试层），结构相同的修订不另造版本。每个版本带 `[from_ms,to_ms)`（权威毫秒，含开局倒计时）与修订区间；
第一个版本从 0 开始，即开局的空构筑。

事实只在 Combat 提交它的地方记一次：`ApplyBuildChange` 安装后（先补记被替换的开局构筑）、`LandBasic` 落地的普攻与
`ReleaseAbility` 的主动释放（记下 EffectId 所属版本；天生冲刺不计施法）、`ApplyHit` 的已提交命中、`HealBody` 实际回复的生命、`SettleRegen` 实际恢复的自然（含宿舍）生命、`GrantShieldTo` 实际授予的护盾。
命中的造成伤害（`DamageP2`，含被目标护盾吸收部分，每次命中一次）记给记功参赛者**释放该效果时**的版本；
没有登记释放的反应命中（反伤、爆盾）记给提交时安装的版本；承受伤害、自身护盾吸收与生命损失、
本人削减实际减掉的量（S16 姿态 `BraceCutP2`，记为减免）、一次性回复、自然恢复和获得护盾
记给受影响者提交时安装的版本。自伤与受保护的零结果不记。拒绝为本人战斗请求因玩法门槛被拒（忙碌、冷却、普攻间隔、劲、控制、目标、方向、射程、视线；协议、过期与阶段拒绝不计），
直接请求在回执首次转为拒绝时（`StoreReceipt`）计一次，寻路普攻在出手被拒时计。公共休息室中的训练（休息生命与共享真实训练目标，§16.3）
一律不记，不论攻击者是否属于目标对应槽位。早期版本写下的 `collected:false` 与 `null` 照原样读出，不当作 0。

触发为已装的牌一次产生真实结果（`LedgerCardTriggered`：伤害、治疗、护盾、劲、冷却、元素或位移确实发生；被拒、为零或没有目标的不计），
每次给持有者记 1，不按受击人数；常驻与编译期改写（K08/K09、Y13、Y15）不计。

结果发布时 `ALDGameMode::PublishBuildBlueprints`（`LDGameModeBlueprint.cpp`）用 `LDBuildBlueprint::Compose` 把台账与冻结结果
（公开 ResultId 作为 Match ID、原因、胜负、开始/结束时刻、本人等级与 K/D/A、产品版本、协议版本、目录根摘要与 schema/handler/projection
修订）组成每名在场真人的记录，经 `ULDBuildBlueprintComponent` 只发给其拥有连接；AI 不生成，任何人都收不到别人的构筑。
文件格式、保存、保留与分享见[视野与 UI](VISION_AND_UI.md#构筑蓝图文件与查看)。

### 技能等级成长与分支

技能的固定数值随拥有者**当前等级** L 与**技能等级** R（1..3）成长：G(L) = 1 + `SKILL.GrowthPerLevel`（4%）×（L − 1），
基础量再乘 `SKILL.RankBaseScale[R]`（1 / 1.5 / 2），攻击力、法强与生命上限系数乘 `SKILL.RankRatioScale[R]`（1 / 1.25 / 1.5）。
每个主动有一组 1 级作者数值（`FLDDemoAbilityValues`：劲耗、冷却、效果）和六个分支（`FLDDemoAbilityDefinition::Branches`，
`FLDDemoAbilityBranch{Path, Target, Windup, Element, HitType, Values}`），分支是编译器把 `ranks` 补丁沿 A→A1/A2、B→B1/B2 解析后的完整定义。

- **成长范围**（`LDSkillGrowth::GrowValues(Authored, Ranks, PerLevelBp, Level, Rank)`）：效果的基础量（伤害、护盾、反伤、强化普攻与周期伤害；
  劲的获得量除外）、S16 的记录上限与限时属性的点数（攻击力、法强、护甲、魔抗）。劲耗、比例、持续时间、冷却、射程与层数不成长，只随分支变化。
- **预计算**：`FLDDemoContentSnapshot` 构造时对每个主动、每个分支（含 None）、每个等级 1..`MaxLevel` 生成一行，技能等级由分支路径推出
  （`LDSkillBranch::RankOf`）；`FindValues(Ability, Level, Branch)` 返回该行，越界、缺分支或算术溢出（内容验证已证明不会发生）时返回空并由调用方明确失败。
- **读取**：主动经 `ActiveOf` 在准入与释放时按 `FLDLearnedActiveValue.Branch` 取成长后的数值、目标形式与前摇，命中载荷在释放时快照；
  升级与升技能重新投影构筑；护盾与灼烧在施加时固定。卡面用同一 `LDDemoCardText` 与 `FindValues` 渲染当前等级与分支的数字（见[视野与 UI](VISION_AND_UI.md)）。
- **技能点与升级**：`LDProgression::SkillPoints` = `SKILL.PointLevels` 中已达到的个数 − Σ（R − 1）。成长命令 `RankUpSkill`（`FLDBuildCommand.Branch`）
  经 `ApplyRankUpSkill` 只接受 `LDSkillBranch::IsNext(当前, 目标)` 的下一步，缺点为 `NoSkillPoint`、分支不合法为 `BranchUnavailable`；
  对局中随时可用（与学习同样的准入）。遗忘与替换清空分支，点数随之退回。回执带 `Branch` 与 `SkillPointsAfter`，日志 `LD_SKILL_RANK`。
- **分支效果**（`LDCombatBranchDirector.cpp`）：分支新增的效果操作（`native_ids.json` 170–195：易伤、下一次普攻增伤、回劲、偷劲、后续追击、
  二次施放、后撤、自身增益、灼烧引爆、周期脉冲、区域减伤/回劲、过载、跟随普攻、护盾破裂/反弹/结束回血/命中加成、顶着揍的眩晕/回血/叠层/推开/转盾、
  弹道落地区域）在释放时由 `ArmBranchRelease`/`ArmBranchHostile` 装上，命中附加项放在 `FLDCombatCastLedger.Riders` 并对本次释放的每个原生直接命中生效；
  状态在身体的 `FLDCombatBranchState`（易伤、二次施放窗口、免劲窗口、下一次普攻增伤等），追击与脉冲在 `FollowUps`/`Pulses` 里由
  `AdvanceBranches` 推进，死亡、新生命与休息经 `ClearBranchState` 清除。分支复用的改装件机制（拉近、回程、穿透、再落、区域跟随、末击击退、
  强化普攻射程）由 `CompileCardActive` 从分支数值中取出、作为没有牌 id 的 `FLDPassiveEffectRef` 注入，不算牌的触发。

## 死亡、奖励与营地重置

Combat 首次 PendingDeath 同次冻结 `FLDCommittedDeath`（真正致死 Effect/Source/Owner、受害生命、目录与 RewardBasis）。无主不回退 LastOwner；无奖也有终态事实；自杀按稳定 Owner/Entity 判断。
**每个 Effects 致死集合先锁完全部事实及 RewardBasis，再在 DeathsAndRewards 逐项支付**：互杀中第一笔 XP 使某受害者升级，另一笔仍读冻结的 VictimLevelAtDeath。

- Life 的 `EndContenderLife(Fact, Permit)` 消费同一 DeathKey 并发布一次资格变化，Combat 不另发 Died。
- 成长 sink 为 `ALDGrowthDirector::CommitDeathReward(const FLDCommittedDeath&, const FLDGameplayWritePermit&) -> FLDRewardReceipt`（Granted、NoReward 或失败）：Prepare 预计算 XP/等级/机会、候选、回执、
  Combat 投影及容量，同一 permit 下各收款人的成长安装之间不回调、发布或重入。见[成长事务](GROWTH_TRANSACTIONS.md)。
- 信用资格按 OccurredAtSeconds 冻结：死亡但仍在局的 Owner 合格；Hit 致死先于 Leave 则后来离局不改；Leave 在先则取消效果，不能产生新的致死。旧参赛记录只服务已锁事实结算，不恢复在局。
- 野怪由最后一击的参赛者所有者独占 XP；参赛者之间另按助攻分配（App Spec §4.1）；自杀、离局与回位不奖励。

**营地受击**：`ApplyHit` 对营地成员成功提交正伤害（含盾吸收）或 Root 后产生 `FLDCombatHostileContact`（`HitId`、`VictimBody`、`SourceBody`、`CreditOwner`、`OccurredAtSeconds`，不含隐藏坐标）；
Miss 与零接触不产生。同一 Effects 阶段内 `ALDCampDirector::OnCombatHostileContact` 按 HitId 幂等消费，只记录本营敌对来源身份，再由 `LDCampDecision` 产生同营反击意图；
同营广播来源身份而非坐标，各成员独立观察与重查来源生命，不把旧来源的重生身体自动改绑为报复目标。Returning 不产生无敌或不可选中，途中 Hit/Root/死亡照常，世界策略不恢复追击。

**CampReset**：`FLDCampResetInput` 由 World 构造（CampId/MemberId、当前 BodyRef、真实 HomeProof、`uint64 ReturnEpoch`、世界/成员/资源/状态修订），重置键为 `LedgerMatch + VictimKey + ReturnEpoch`，
World 持有返回代次与已重置水位。ReturnEpoch 只在进入新的 Returning 周期时递增；周期中受击只更新修订使旧 Prepared 失效。`PrepareCampReset` 在 RespawnsAndWorld、当期 Effects/DeathsAndRewards 之后，
检查真实到家、仍为该存活成员、HP 正且非 PendingDeath、修订与 permit；已提交只重放原回执。Prepare 在局部候选中先移除临时状态，再投影最终 MaxHP/MaxMP，按最终上限准备满资源。
`ApplyCampReset` 与世界成员安装在同一无重入提交中安装状态/上限/满 HP、清仇恨、技能就绪；不改 EntityId/BodyIdentity/Life，不发行新成员、不产生死亡或 XP、不清 Hit/请求/死亡拒重水位。
已清状态的旧到期回调按原 EffectId 与目标生命核对，不重新生效，也不误删后来的新实例；到家前未命中的独立效果之后仍以同一生命正常校验。

## 生命周期矩阵

| 事件 | 构筑/资源/持续贡献 | 未释放动作/订单 | 已释放独立效果 | 冷却/奖励 |
| --- | --- | --- | --- | --- |
| 前闸拒绝 | 不改保护和资源 | 不替换当前单 | 无 | 不写 CD/节拍 |
| 意图后游戏拒绝 | 保护已解除 | 未准入不替换旧单 | 无 | 不扣劲、不写 CD；失保不回滚 |
| S/新单/取消 | 不撤回已发生状态 | 取消可中断工作 | 不回收、不退款 | 保留已承诺节拍与成本 |
| 换槽/升级 | 一次最终投影 | 验实际来源条款 | 保持旧快照 | 旧 ReadyAt 不重算 |
| 替换/遗忘主动 | 遗忘旧等级与持续贡献 | 旧来源未释放的失效 | 继续原期限 | 原发行记录保留，重学沿原结束 |
| 来源死亡 | 构筑/候选保留，活体持续作用停止，清旧控制 | 取消 | 继续 | CD/节拍继续；在局死者有效击杀仍获奖 |
| 死亡等待选择 | 更新保留构筑，不治疗/复活 | 不发身体动作 | 新学能力不自动生效果 | 不刷新 CD |
| 目标死亡 | PendingDeath 不被救活 | 旧目标工作失效 | 不重绑新 Life | 同生命仅一个 DeathKey |
| 复活 | 新 Life 满最新 HP、劲为起始值，清旧控制，建立保护 | 旧生命请求无执行权 | 旧来源效果可继续，不命中被旧 Hit 绑定的新生命 | CD/节拍按时间 |
| 离局/断线 | 立即撤资格、移身体 | 取消并拒新命令 | 立即取消 | 不是击杀；此前事实按冻结资格结算 |
| 野怪 Returning / 到家 | 仍可受伤/控制/死亡 / 原子清临时状态并满血，同刻 PendingDeath 优先 | 不回头追击 / 回等待受击 | 同生命 Hit 照常 | 不发奖、不换 Life |
| 终局 | 先冻结写入再清理 | 拒绝新受理 | 不再伤害/治疗 | 已提交回执可重放 |
| 新局 | 新 Generation + LedgerMatch，重建局内状态 | 旧请求全部拒绝 | 清空 | 新 CD/死亡/事务域 |

## 检查入口

`Lockdown.Rules.DemoCombatEvaluation.*`、`DemoCombatStatus.*`、`DemoCombatSources.*`（含 `InnateDash`）、`DemoOrders.*`、
`DemoAttributeProjection.*` 与 `DemoCore.*` 分别覆盖纯数值、Root/屏障/回复/冷却规则、来源轨道回绑、
攻击移动选敌与订单簿换局/换命隔离、属性及核心身份/许可/时间/投影边界；`DemoBuildExpansion.Slow/TimedBonus/ConditionalTriggers` 覆盖减速、限时增益
与低血条件项的纯规则，`DemoCombatPrimitives.*` 覆盖硬控递减、`CommitControl`、剩余冷却缩减与解除减速，
`DemoAttributeProjection.StatPoints/TemporaryTerms` 覆盖五维点数投影与临时项投影，
`DemoCombatEvaluation.ControlAmplify` 覆盖增伤结算，`DemoMechanics.ShieldEvents/Knockback/Periodic` 覆盖护盾打破判定、
击退方向与区域脉冲时刻；`DemoControl.ActionMatrix/EmpowerOnce/ExpansionContent` 覆盖动作禁止矩阵、强化普攻消费与条目的类型化导入与显示；
`DemoGrowthSkillGrowth` 覆盖 G(L)、成长取整与按等级成长的单级数值；`DemoCombatCues.*` 覆盖动作提示的选取与优先级，
`DemoBodyMotion.*` 覆盖身体表现时间轴（前冲、施法聚能、与权威释放时刻对齐的火花、随攻速变化的冲击帧、闪色、片段定位、出现、倒地与同一动作的重启判定），
`DemoBuildPrimitives.*`覆盖关键词纯规则、沉默与解除、共享破防与灼烧、减速中目标仍被定身、交战窗口的触发与不触发、区域脉冲次数、多次强化普攻与完整减益观察，
`DemoElements.Rules/FogAndSlot/Brace`覆盖反应表与 R、元素槽与潮湿刷新、起雾的火结算与雾区归属、S16 削减记录与死亡不反震，
`Combat.Skills.N4Bag`覆盖书包的生命、只挡敌方弹道、只受敌方伤害、到期与被打掉的爆炸、同技能数量与 3A1 墙及其公开记录，
`Combat.Elements.Matrix/Shock/Rot`覆盖 6 × 6 表与十五种反应在真实目标上的伤害倍数、控制或状态、附带、地面状态与触发者回劲、麻的第三击与溅射、
馊的削减、跳伤与刷新，`Combat.GroundState.Transitions/Caps`覆盖每条“再加元素”转换、元素命中引起的转换、各种站立效果只对敌人、冰面移动参数、
每人 6 / 全场 40 与同一技能一份的淘汰、公开事实与线上记录，`Combat.OneVsOne.Bounce`覆盖 S10 单挑弹回与有他人时先弹他人，
`Combat.Damage.FactorProduct/SameCardAdds/TechCap` 覆盖乘区分组相乘、同名相加与技术上限，`Combat.Chain.Guard` 覆盖连锁深度与每步上限，
`Combat.Chain.Key` 覆盖连锁键的步骤计数与门槛，`Combat.Cards.<编号>`（85 条）各在真实 Combat 里装一份与两份该牌，检查投影接受、每份作为该牌乘区键下的独立效果被读取与位置，
并检查可在无对局时观察的结果（K06 重放次数与延迟、K08/K09 冷却与劲耗、K09/K12 乘区、Y01 反弹与不再反弹、Y13 生命上限、Y15 缺血攻速、
Y25 元素与缺元素拒绝；A18/A19 普攻挂冻与馊、K22–K24 宿主挂冻、馊与吹、D10 路径冰带、D11 挪动一次、Y12 多留一份、Y23 站立乘区与离开清零）；其余牌的触发行为由 `Combat.Events.*` 与原生场景覆盖，未覆盖的写在 PR 里。
`Combat.Events.*`在带已批准世界视线的隔离世界里从真实释放、投送、区域、飞弹、普攻与灼烧入口驱动：S08 火与电同批先火后电及雾的即时潮湿、
S19 触发后引信与到期不爆、S20 队友加速等，
`DemoAssist.*` 覆盖助攻贡献记录与窗口、经验平分、击杀通知/结算行，以及击杀者与助攻者的死亡奖励一次结清，
`DamageRecap.Window` 覆盖承伤样本的记录与过期、窗口边界、按攻击者与来源求和及排序、各来源总量和列表的形状校验，
`DamageRecap.Chain` 覆盖反应伤害按规则链分行、最近一击的距今时长、规则链与助攻提示的形状校验，
`BuildBlueprint.Attribution` 覆盖构筑蓝图台账的版本划分（只升级、同结构异序与 DEV 测试层不另造版本）、按释放版本归属的命中、
时间段与收尾，以及全部事实自开局即采集并按版本分列，`BuildBlueprint.Cards` 覆盖只改安装/位置/元素即开新版本、
每份一行的位置与元素、严格往返与失效安装提示，
`DemoControlFeel.*` 覆盖按住右键的重发节流与预输入判定；`DemoControl.*`（`LDControlRulesTests`）覆盖嘲讽的选取、
强制目标与恢复；成长的纯规则是
`DemoGrowth*`，见 [成长事务](GROWTH_TRANSACTIONS.md)。
`RunDemoCoreCheck.ps1` 用真实 Host 和两个 owning clients 运行明确标记的 DEV 初生/运动接缝，
`RunDemoCombatCheck.ps1` 经普通大厅 Start 用参与的 Listen Host 与一个 owning client 运行 DEV 战斗与订单场景：
Host 站点附近按固定方位确定的收缩边外合法脚点作为点选地面仍被拒、作为被追击身体的终点可完整规划，
已接受行走上打开设置并超过订单租约（订单与心跳继续、设置下的新订单与 S 被本地拒绝、关闭后不重放）、
远程攻击移动接敌与 S、右键击杀的击杀播报与复活延迟、死亡期间小地图平移驱动的静态镜头、
十二点随机复活的精确保护和公共状态，以及空 Q 经同一 Direct 入口被拒但解除保护且不恢复；
随后 Host 的 DEV 成长夹具（只有 Host 带 `-LDDevGrowthFixture`；候选在 Lobby 按 Start 前暂存，第一组即开局候选）为双方预置候选，
并给 Client1 发放经验到首个主动机会等级；双方经普通 `SubmitBuild` 主动选择学会：Host 从开局候选学扒裤子，Client1 从开局候选学锅盖挡脸、
从 3 级候选学甩你一鞋（有空槽即同次学到），
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


其他拥有连接场景（输出在忽略的 `Artifacts`）：`RunDemoGrowthCheck.ps1`（开局在倒计时学 Q、交战窗口内学 W 并加点、死亡中学 E 且复活后仍在、新局无冷却债务）；
`RunDemoCampCheck.ps1`（不先手、全营反击、每生命一次最后一击奖励、清营刷新、脱离回位与无奖励到家重置）；`RunDemoOpponentCheck.ps1`（满房三 AI 自行成长、战斗、喷漆与守护）；
`RunDemoVisionCheck.ps1`、`RunDemoCaptureCheck.ps1`、`RunDemoSessionFaultCheck.ps1` 覆盖观察授权、喷漆/守护与离局。
另有 `DemoCombatSources.CooldownTracks/CommittedBasicRelease`、`DemoCombatStatus.Roots/Shield/Regen/Cooldown`、`DemoVision.ActionCues/Projectiles/Presentation/Privacy`、
`DemoCamp.Retaliation/ReturnReset/Lifecycle/Roster/ObservedFacts`、`CampDecision`、`DemoSpawn.UnbiasedSample`。

未覆盖：拥有连接场景没有验证吸血或暴击的实际命中；预输入与快速施法只有规则测试；战斗场景不含营地或 AI；没有三名真人同时参战的满房场景；
同批互杀、奖励可重试失败、同毫秒离局与命中、终局同刻致死没有拥有连接场景。首版内容是已批准的试玩输入，未经平衡测试。

数据编辑先运行对应 Sync `--check` 与输入测试。测试用的授予、传送或改时钟要明确是夹具，
不能当作正常升级、打野收益或完整对局已经成立。

## 野怪攻击模式

四档营地共十二种，数值与能力见[内容与数值§6.2](../demo/CONTENT_AND_BALANCE.md#62-四档营地与种类池)。
`LDCombatMonsters.cpp`扩展营地的现有Basic动作入口：预警在接受时固定几何，成功释放推进循环，
`LDMonsterRules::Areas/Contains`供预警与命中共用；命中继续进入Combat的PendingHits、伤害/控制和致死事实。
延迟段由同一个权威driver按精确到期点调度；取消的未释放动作不留下伤害，休息清场清效果。
HR同伴死亡通过DeathsAndRewards许可提交到Combat；回位重置清循环/被动/正面防守。
正面防守仅修改同一命中的DeliveryScale，不伪造护盾资源。总监低血攻速通过现有临时属性投影处理。
技能来源使用Basic入口及怪物定义，伤害类别为Ability；怪物普攻仍为无定义的Basic来源。
地面预警和投射物继续走拥有者授权的观察通道，技能名由可见CreatureId查询内容；不公开当前隐藏营地选种。
`ConeWarning`和`LineWarning`具有独立的几何合同。直线预警以锁定起点做视野授权，沿朝向向前显示长度，
避免预警中点在墙后时隐藏仍可能命中的入射段；`Road`仍表示以中心采样的已走交通带。
骑手的合法冲刺前缀由营地拴绳裁剪，没有前向空间时使用普通攻击，不发布零长预警。

`FLDCombatEntityRecord::MonsterLives`按life保留已注册种类，当前种类可在整组刷新时变化。
旧生命已释放效果的伤害记录与击杀来源按旧life查定义，不能套用刷新后的种类。

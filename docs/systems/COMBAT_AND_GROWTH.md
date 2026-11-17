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
| 不变量 | `ALDCombatDirector` 是唯一的战斗、属性、冷却、奖励交接与身体战斗状态所有者；HP/MP/盾只以 P2 存于 `Combat.Bodies.Resources`；进攻意图先解除保护再验动作；未释放不付费，释放后不退款；冷却按来源持久计时，换槽/遗忘/死亡不清洗；一条生命只有一个 `FLDDeathKey`；先锁完本批全部死亡事实再支付奖励 |
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
当前 Match/Body/Life/Alive 与 permit，由 Life 按当前 Actions 真实时间发布一次资格变化。前闸通过后先失保，再查来源、目标、距离、MP/CD、控制与路径；
Rejected 也可能 `ProtectionRemoved=true`。所有角色主动（含自身守护/回复）适用；普通移动、Stop、本地准备与普通 D 不适用。旧局/旧生命、重放、畸形包在前闸拒绝，不改新生命。
在喷漆区内失败的进攻仍可能因失保改变喷漆资格。

**准入与释放**：普攻、Q/W/E 与 D 冲刺经同一 `SubmitAction`（真人 RPC、AI、营地成员各自的服务器 Caller）。冷却、蓝量与定身对位移的禁止由
`LDCombatSourceRules::ActiveGate` 判定，准入、释放和 owner view 共用同一顺序。一个身体同时只有一个未释放动作：普攻前摇中被接受的技能放弃该普攻（`AbandonAction`，节拍不退回），
能力前摇中再提交技能返回 `Busy`。

- **普攻**：Navigation 在 Actions 许可内以订单键调 `StartNavAttack`。合法 AttackStart 固定本次前摇与最早 `NextAttackAt`；取消、移动、改攻速不提前该期限，攻速只影响下一次。
  参赛者节拍是局内持久状态，跨死亡按时间继续；怪物新生命按营地初始化。释放（`ReleaseBasic`）不查距离，只经 `LDCombatSourceRules::BasicReleaseGate`：攻击者被缴械/眩晕/击退
  或被嘲讽转向他人放弃（`ControlBlocked`）；目标那条生命死亡或濒死、离局、换命或换身体放弃（`TargetUnavailable`）；参赛者攻击者重新观察不到该生命放弃（营地感知由营地模块负责）；
  视线被挡放弃（`Blocked`）；否则命中起手时的目标生命。攻击者死亡后 `ReleaseDue` 不再释放其动作。`AdoptNavAttack` 让同一观察目标的新订单接管未出手普攻；
  `FLDCombatBodyRead::ActionReleaseAtMs` 供导航判断本阶段即将出手的普攻不再占住身体。
- **技能**：释放时重验来源/生命、取消、条款、目标、控制、MP/CD 与几何；效果/状态容量预分配且旧状态未被破坏后验 permit，一次提交 MP、本条 ReadyAt（`FORMULA.CooldownMs`）、Effect 与回执。
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
- `EFFECT.ShieldPolicy=ReplaceIfNotWeaker` 比较旧盾**当前剩余量**：新量不少于剩余即以新量、新来源、新截止完整替换；较弱保留旧盾，且只是子效果结果，组合净化照常、MP/CD 照付。
  之后 Max/AP 变化不重算已快照盾。
- **嘲讽**：`taunt` 在释放时让施放者半径内（脚点平面距离含边界）存活、无保护且此刻能观察到施放者的其他参赛者被嘲讽（野怪不受）；同一身体只保留最新一次；落地是一次控制（`LDControl::TauntVictim`）。
  期间 Q/W/E 与 D 经 `ActiveGate`（`bTaunted`）以 `ControlBlocked` 拒绝，普攻只能打嘲讽者那条生命，前摇中被禁止的动作当场放弃。每个 Actions 开头 `RefreshTaunts` 结束过期、嘲讽者死亡/离局
  或观察不到嘲讽者的嘲讽；离局由 `CancelEntityEffects` 清除。订单强制见[移动、输入与镜头](MOVEMENT_INPUT_AND_CAMERA.md)。嘲讽只出现在本人 `CombatView`。
- 目录不用的状态与效果（免死、背后判定、灼烧以外的持续伤害、减疗、易伤）连同处理器已删除，不保留禁用生产者或空 handler。

`ELDCombatReject`：InvalidRequest/NotOwner、StaleMatch/StaleLife/StaleRequest、RequestConflict/ReceiptExpired、Cancelled、StaleSource、SourceUnavailable、
Busy/Cooldown/AttackNotReady/InsufficientMana/ControlBlocked、TargetUnavailable、InvalidDirection/OutOfRange/Blocked、Dead/MatchClosed、AuthorityDataUnavailable/CapacityUnavailable。
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

- HitClass 为 BasicPrimary、BasicSecondary、Ability：只有主普攻允许本次暴击（×`STAT.CritMultiplier`，在加成池之前）；带暴击的技能输入以 Domain 拒绝；反伤与附加伤害不暴击；Basic 拒绝 AP 系数与魔法伤害。
- 完整字段、数值域与精确算术校验先于保护零伤；中间有理数保留到最终一次 floor/ceil；精度与取整只取内容合同（`FORMULA.Crit/Resistance/Damage`、`FORMULA.AttackIntervalMs/AttackWindupMs`）。
  原生公式夹具使用 producer 生成的 `approved_base`，`base` 是独立合成输入，不在夹具缺失时回退生产资产。
- AI 只以自身属性、合法可见成员与公共模板构造保守假设（显式不暴击），缺输入返回 EstimateUnavailable，不当成零风险。

## 属性与资源

`LDAttributeProjection::Project(FLDAttributeBuildValues, Snapshot)`：输入为等级、五维点数、已安装构件（每定义至多一次，按 id 排序）与 DEV 测试层；空数组表示没有该来源。
结果含全部最终字段与按属性顺序的基础/等级/点数/构件/DEV 贡献；构件的 `low_hp_stat_bonus`、`control_damage_amp` 成为条件项，全部已安装构件另列出供触发处理，条件脱战回复单独返回。
初始身体用它初始化 `Bodies.Resources` 与普攻时长；构筑变更经成长事务调用 `PrepareBuildChange`/`ApplyBuildChange`。

| 属性 | 内容键 | 消费者/单位 |
| --- | --- | --- |
| AD/AP | `STAT.AD/AP.*`；`FORMULA.StatProjection/Damage` | AP 只进明确的法术加成，不改普攻类型 |
| Armor/MR | `STAT.Armor/MagicResist.*`；`FORMULA.Resistance` | 每次 Hit 取目标当前值 |
| 攻速/间隔/前摇 | `STAT.BaseAttackSpeed/AttackSpeedPerLevelRatio`；`FORMULA.AttackSpeed/AttackIntervalMs/AttackWindupMs` | 在合法 AttackStart 快照，不缩短能力前摇 |
| 移速 | `UNIT.MoveSpeedCmPerSec`；`FORMULA.MoveSpeed → STAT.MoveSpeedCmPerSec` | cm/s，接 CharacterMovement |
| 攻击距离 | `UNIT.BasicAttackRangeCm` + 构件 `AttackRangeCmFlat`；`FORMULA.AttackRange → STAT.AttackRangeCm` | 脚点 XY 中心距；只限制普攻开始；不等于能力 Range 或群攻 Radius；扩大不创建追踪弹体 |
| 最大/当前 HP/MP | `STAT.MaxHP/MaxMP.*`；`FORMULA.Damage/Regen/ResourceMaxChange` | P2；Combat 唯一真值 |
| 回复 | `STAT.HPRegen/MPRegen.*`；`FORMULA.Regen` | 点/秒，按有效速率区间累计 |
| 暴击 | `STAT.CritChance.Base/CritMultiplier`；`FORMULA.Crit`、`POLICY.CritSampling` | 一次 AttackId 在成功 Release 采样一次，重放不重抽 |
| 冷却 | `STAT.CooldownHaste.Base`；`FORMULA.CooldownMs` | 只影响未来 Release |

- 每项最终属性夹到 `STAT.<属性>.Min/Max`，超界部分不生效、不拒绝构筑；带 DEV 项的属性超上限时拒绝该次投影。当前资源、已有 ReadyAt 与 UI 不反向参与投影；循环依赖在导入或 Prepare 拒绝。
  普攻没有副击：`POLICY.BasicSecondaryTriggers=Disabled`。
- **最大资源变化**：`PrepareBuildChange` 在局部候选上一次验证完整变化、来源、数值、容量与绑定，生成最终属性、资源变换与来源计划；不调 Changed/Sync，不取消动作、不扣机会、不改资源/CD。
  `POLICY.ResourceMaxChange=PreserveRatio` 以旧当前值、旧上限、最终新上限调用一次 `LDAttributeProjection::ResourceMaxChange`，不逐个拆装中间态，不从 HUD 反算，不触发吸血或喷漆清零。
  活体正 HP 降上限后量化为零返回 `NumericUnderflow`，整笔事务不改；非初始化零分母返回 `NumericDomain`（`UndefinedResourceRatio`）。Prepare 还捕获 BodyRef 与属性/资源/状态修订，
  其间受伤、耗蓝、回复或状态到期使旧计划失效，必须重 Prepare。死亡等待选择不对死身体做比例回血，正常复活由生命域满最新上限。
- **回复**：`POLICY.RegenIntegration=CarrySubP2Remainder`，同一资源记录保存有界亚 P2 余数与结算水位；速率/来源变化前先以旧 rate/旧 max 结清到实际边界；满资源丢弃超额与余数；Prepare 不偷偷 Tick；死亡/新生命清余数。
  `LastCombatAt` 只在成功 AttackStart、有伤害效果的成功 Release、承受正伤害（含盾全吸收）时更新；失败尝试、位移、自保盾/回蓝与选卡不刷新。复活以复活时刻初始化脱战起点。
  参赛者满 `REGEN.OutOfCombatDelaySec` 后另加按当前 Max 比例的脱战回复，与宿舍回复各自起算并与基础回复叠加，野怪不享有；纯规则 `LDTactical::SettleRegen`（`Regenerate` 逐身体调用），起点也是 `NextDueMs` 的边界。

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

HP/MP/盾只在 `Combat.Bodies.Resources` 中以 P2 整数保存，Owner 的点数用于显示。`ApplyHit` 是唯一资源入口：验证 Match/permit、Effect/接触键、目标生命、存活/关系、当前保护与几何，
再按 `FORMULA.Damage` 计算全部后果；源输入用 Release 快照，目标抗性/保护/屏障/HP 用当次已提交真值；每个 Hit 的容量与数值失败先于写入。第一次 HP 到零立即 PendingDeath，
不留同批吸血/回复/盾把它治活的窗口，其后 Hit 不夺取致死来源。吸血只按 `HPLostP2`，普攻命中特效系数 1.0、技能按定义，反伤与附加伤害不触发，所有比例相加后受 `COMBAT.LifestealCap` 限制；
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
（本人身体须等自身投影确认该命已死，终局或离局移除身体不播放）；甩你一鞋（`active_snark_bolt`）和馊水糊脸（`active_sock_shot`）的飞行由 `ULDFlightTrailComponent` 拖出粉尘，M13 回程时尾迹变为紫色。
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
`LDAttributeProjection::ApplyTemporary` 用同一公式在构筑项上叠加限时增益、低血条件增益（投影的 `LowHPBonus`，来自已安装构件的 `low_hp_stat_bonus`，B20）与最强一个减速
（负比例），点数类向下取整并夹紧到目录上下限；生命上限/法力上限不接受临时项。`RefreshAllTemporary` 在 Actions
（回复结算后）与 Effects（命中后）各执行一次，到期、血线越界与施加都在下一次运动前生效；`NextDueMs` 包含这些截止时刻。
减速与限时增益的纯规则在 `LDTacticalRules`（`ApplySlow`、`StrongestSlowBp`、`DispelSlows`、`ApplyTimedBonus`、`TemporaryTerms`）；
减速只减移速，按效果实例保存，只取最强一个。破防与灼烧见下文“构筑 v7 战斗原语”。
伤害系数为基础值、`ad_ratio`、`ap_ratio` 与 `max_hp_ratio`（施放者生命上限，`FLDDamageDefinitionValues::MaxHPRatioBp`），同在抗性前相加；普攻不带生命上限系数。技能命中载荷在释放时快照（含施放者 AD/AP/生命上限，以及按施放者当前等级成长后的数值，见“技能等级成长”）：指向单位、弹体或 `self_area`（释放瞬间脚点为圆心、与横扫共用 `CollectAreaTargets`
的逐目标合法性与视线检查）各得一份；控制与减速随命中经 `CommitControl` 施加。
施放者离局时 `CancelEntityEffects` 一并移除其减速、灼烧层、区域与路径扫掠，营地回位清空全部临时项。


**本人视图**：`FLDCombatOwnerView` 由 `PublishOwnerViews` 写入拥有 Controller 的 `CombatView`，随 OwnerState 单一发布通路同步。信封为 Generation、Life、Revision、AsOfMs、bHasBody、SourceRevision，
消费者先拒旧再替换完整快照。内容：资源 `HPP2/MaxHPP2/MPP2/MaxMPP2/ShieldP2`；`FLDCombatAttributeRowView`（Attribute、Base、Level、Choices、Passives、PercentBonus、Final、bClamped）及 MoveSpeedCmPerSec、
AttackRangeCm、bOutOfCombatRegenActive；Q/W/E/Dash 各一行 `FLDCombatEntryView`（SourceKind、DefinitionId、Rank、TargetMode、RangeCm、ManaCostP2、IssuedCooldownId、ReadyAtMs、CooldownMs、仅显示的 Unavailable）；
`FLDCombatStatusView`（StatusId、ExpiresAtMs、AmountP2、ControlBlocks、bIsPassive）、bRooted、NextAttackAtMs、ActionEntry/Phase/Token/ReleaseAtMs、本人提示 `Cues`、`BuildSafeAtMs`。
Alive/ProtectedUntil/LifeRevision 来自生命域 OwnerState。自己受到的状态可解释类型与时限，不借来源字段泄露隐藏施加者。

**对手投影**只由 Vision 的 `FLDObservedUnitView` 维护：合法可见时的名称、等级、HP、MP、状态与 Q/W/E 原生 ID 和等级（`FLDObservedActivesView`，由 `PublicActivesOf` 在构筑安装与身体准备时写入），
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
- **强化普攻**（`empower_basic`）：每个来源（S11、S17 释放时，B12 冲刺完成时；C09 以后）至多一份待用包（`FLDCombatEmpower`：
  来源、按当时属性快照的物理量 `AmountP2`、期限与 `Charges`），同来源重新武装只替换其未用值与期限（`ArmEmpower`），不同来源并存。
  已提交命中的普攻经 `ConsumeEmpowers` 对每个仍有效的包各取一次，相加后在暴击采样前并入这一次 `BasicPrimary`
  （吃暴击与吸血，B06 等计数只算一次主击）；到期在 Effects 阶段移除（`ExpireEmpowers`）。本人视图显示一个 Empowered，期限取最晚一份。
- **反伤**（`thorns`，B03）：构筑触发项，经 `CollectPassiveEffects` 解析。命中后持有者仍存活、伤害为正（含盾吸收）、命中是敌方主普攻、
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
  `SnapshotOnHit(Body, ScaleBp, Hit, Error)` 在出手时快照吸血（B04），`ApplyHit` 按系数提交；`FLDCombatHit::LifestealShareBp`
  是主击自身伤害包在原始伤害中的份额（`LandBasic` 按 自身 /（自身 + 并入强化包）计算，默认 10000），吸血只按该份额的 HP 损失回复。
- **原生直接命中**：`IsNativeDirectHit(Hit)`——技能类命中、`bDirectAbilityDamage`、来源为已学主动，且不是反伤、附加伤害、灼烧、
  状态与账目结算、还款或护盾触发的命中；普攻不是。之后的派生命中只要是宿主主动来源的技能直接伤害、沿用释放 EffectId 与 `CastLedger`，就自动算作原生直接命中。
  `bAddedDamage` 的命中（B05 冲撞追加）不暴击、不触发命中特效；`AddedHit` 是附加伤害命中的构造。
- **伤害求值顺序**（`ApplyHit`）：加成池 `FLDDamageEvaluationInput::DamageBonusBp`（B07 `ControlAmpBp`——目标在命中前被移动减速、
  硬控或击退，单独的攻速降低不算；B17 `IdleReadyAmpBp`；原生直接命中另加 B14 `distance_amp` 与释放锁定的 B15 `AlternateAmpBp`）相加后乘一次，普攻暴击 ×`STAT.CritMultiplier` 在池前；技能不暴击
  → 破防后的抗性（`LDCombatKeywords::BreachedResistanceP2`：共享破防层的固定值）→
  B21 `primary_guard` 主普攻减伤 → 护盾 → 生命；吸血经 `LDCombatKeywords::CapLifestealBp` 夹到 `COMBAT.LifestealCap`，按 `HPLostP2 × LifestealShareBp` 经 `HealBody` 回复；
  灼烧与反伤不吸血。`LD_HIT` 的 `amp_bp` 是 10000 加池，末尾 `pool_bp` 是加成池。
- **剩余冷却缩减**：`ALDCombatDirector::ReduceCooldown(Entity, ELDCombatEntry, AmountMs)` 与按定义查找槽位的
  `ReduceLearnedCooldown`，都经纯函数 `LDCombatSourceRules::ReduceReadyAt`（不早于当前时刻；`MAX_int64` 表示立即就绪；
  已就绪轨道不变）。
- **位移判定**：`LDCombatSourceRules::DisplacesCaster` 按条目的数值判定它是否位移施放者（冲刺类主动）；
  `CastRangeCm` 统一给出技能施法距离：目标定义的射程，加上目标定义 `AttackRangeRatioBp` 允许的额外普攻距离份额（有上限；当前目录没有条目使用）。
- **触发效果查找**：`CollectPassiveEffects(Projection, Op, Out, Error)` 是构筑触发效果的唯一读取口：遍历投影的
  `InstalledComponents`，从目录取每个构件的 Op 效果，固定量按持有者等级以 `LDSkillGrowth::GrowEffect` 成长后按值返回
  （`FLDPassiveEffectRef`：Key=构件 id、`Source.Kind=Component`、`BoundActive`/`Element`）；目录缺构件或成长溢出是明确失败。
  `FindTrigger` 返回第一项并传出失败。
  已退役的效果操作编号不复用（`native_ids.json` 的 `retired_ranges`）。`dash_contact_refund` 由 C03 携带；`mana_on_hit`、`cooldown_refund`、
  `shield_break_retort` 的处理器当前没有构件携带。

### 改装件与连接件（`LDCombatWorkshop.cpp`、`LDWorkshopRules`）

- 安装目标（`FLDComponentTarget`：主动、宿主构件、D、自身，C12 另带 `BoundComponent`）由 `LDWorkshop::TargetShape` 校验，投影把目标
  写进 `FLDPassiveEffectRef`；`Targets(Exit)` 判断一次出手（已学主动、构件来源或 D）是否是该改装的目标。
- 改装件在出手时由 `CompileActive` 编进 `FLDCombatActiveTerms`（伤害包倍率、接触/冲刺倍率、冷却与费用、M01 范围化、M07 压缩），
  命中经 `PackageScaleBp` 进入伤害评估；顺序为伤害包倍率、顶着揍、格挡、护盾前的 M12 延后。
- C02 读角色移动组件在击退中记录的第一次阻挡（静态阻挡或另一合法敌人身体），不用位移近似；击退不再穿过身体。
- C05/C07 挂在 `CommitControl` 末尾（`OnHardControl`）；命中转成的控制带命中的来源（`FLDControlApplication::Source`＝主动、C02 撞墙眩晕、
  C06 定身），C05/C07 按它匹配出口。C08/C11/M12 还款都经 `HealBody`；C09 在元素反应完成后武装下一次普攻，反应的触发来源是新元素的施加来源
  （B10/B11/B18 的水电是构件自己的施加，见“基础构件”）；M11 改变放电与反击的选目标；C02 阻挡读取、M12 到期债务与 C08 周期反击由
  `AdvanceWorkshop` 驱动。
- M01 的扩散（单体主动的扇形、飞弹首击点的圆）一律经 `SpreadCopy`：旁人只受伤害，控制、减速、攻速降低、击退、破防/灼烧层、原生元素与
  S21 削减只给主目标。C10 一次破盾反击给所有被波及者同一个效果 id，C11 在其上的 30G 治疗预算因此是一份。
- M12 到期欠款按原量支付：不再过抗性、加成、延后，也不享受 S16 减伤、不进入其记录。
- M13–M15 的派生命中（回程、穿透的额外身体、S10 额外一跳、回响）都是宿主本次释放的伤害包副本：同一 `EffectId`、同一
  `FLDCombatCastLedger`、仍是原生直接命中（`IsNativeDirectHit`），只按 `damage_ratio` 缩放 `DeliveryScaleBp`，所以 B13/B15/B16/B17
  这类按释放计的预算不会翻倍；派生命中不再产生派生命中。
  - M13 `projectile_return`：`AdvanceProjectiles` 在飞弹命中身体、撞地形或到最远射程时调用 `LaunchReturn`，从终点沿当时指向施法者脚点的
    固定方向飞回，长度为当时距离、速度与半径不变；先检查结束时接触的那具身体（可再次命中），去掉减速、定身、缴械、眩晕、沉默、攻速降低与
    击退，保留元素、灼烧、破防与重伤；施法者缺失或待死不回；回程 `bReturn` 不扩散、不穿透、不再回。
  - M14 `projectile_pierce`：第一个身体得到完整伤害包与 M01 扩散，之后每个不同身体（`Struck` 去重）得到 `PierceScaleBp` 份额并保留减速与控制、
    不扩散，用完次数即停；`chain_extra` 在 `CompileActive` 里把次数加到 S10 弹射上（在 M07 压缩之前，所以压缩合计包含这一跳）。
  - M15 `echo_impact`：`ScheduleEcho` 加一个 `ELDCombatGroundOrigin::Echo` 的地面效果，带自己的 `ObservedKey`，0.8s 后按份额结算一次；
    S18 在第一次落地时排程（同点、同内外圈与衰减），S07 在释放时排程（释放脚点、同半径、加 1 层灼烧），S19 在陷阱爆炸时排程（同半径、无减速）。
    回响不再回响，C06 余震不回应回响；宿主冷却经 `ScaleCooldown` ×1.15。回响走现有地面事实，非陷阱地面事实沿用书影外观，
    所以 S07/S19 的回响预警也显示为书影（没有新增表现系统）。
- C13–C16（连接件，宿主为 `component_hurt_heal` 同一组 18 个伤害主动，C13 另可装 D）都挂在已有入口上，按释放 `EffectId` 记预算
  （`FLDCombatEffectBudget`），所以 M13–M15 的派生命中不会让它们翻倍：
  - C13 `hit_haste_other`：`OnNativeDirectHit` 与 D 的 `ResolveDashContacts`（实际接触敌人时、在 `OnDashContact` 之前）调用
    `HasteOtherActives`，对除宿主外每个仍在冷却的已学主动用 `LDCombatSourceRules::ReduceReadyAt` 减 1.5s，不低于当前时刻；每个释放一次。
  - C14 `setup_mark`：`OnHostDirectHit` 在被打的目标上登记 `FLDCombatSetupMark`（持有者 `SetupMarks`，按目标、构件、宿主去重，再命中刷新 4s）；
    `SetupMarkBonus` 在伤害结算里给同一持有者另一个已学主动的原生直接命中 +25% 进统一加成池，造成伤害后消耗；宿主自己的命中不消耗，
    换命/重开随 `ControlMarks` 一起清空。
  - C15 `reaction_haste`：反应事实带上施加者的 `TriggerEffectId`；`ArmReactionEmpower` 之后 `ReactionHaste` 在触发来源是宿主主动时
    把宿主剩余冷却减 3s，每个宿主释放 `EffectId` 一次。
  - C16 `multi_hit_mana`：`OnHostDirectHit` 在释放预算的 `Struck` 里对不同目标去重，第二个起每个经 `GrantMana` 回 8 法力，每次释放最多 24。
- M16–M20（改装件）在 `CompileActive` 里编进 `FLDCombatActiveTerms`；推开与拉近都是真实的强制位移，经同一条击退路径
  （`ApplyKnockback` → 角色移动的根运动），所以 C02 照常读阻挡，`LD_COMBAT_KNOCKBACK` 带 `pull=` 区分拉近：
  - M16 `zone_tether`：S08/S20 区域半径 ×0.8 并标记 `bTether`，释放时中心在施法者脚点；`AdvanceMechanics` 每次推进区域先把中心移到施法者当前脚点，
    施法者缺失、死亡或待死时停在原处。
  - M17 `defense_shove`：S13/S14 的护盾 ×0.85；该护盾被敌人打破时（`ShoveAround`）把持有者脚点 250cm 内的敌人从该点推开 220cm / 0.2s，
    写 `LD_COMBAT_DEFENSE_SHOVE`；S16 自然结束的反震命中改带同样距离的推开，范围为反震半径。
  - M18 `final_knockback`：S17 的攻速加成 ×0.5，最后一次强化普攻（`LandBasic`）把主目标沿攻击方向击退 250cm / 0.2s，覆盖同次 B06 的推开，
    写 `LD_COMBAT_FINAL_KNOCKBACK`；S22 最后一波从施法者脚点推开同样距离且不再减速。M03 装在 S17 上时同时缩放该距离并降低强化伤害。
  - M19 `empower_reach`：S11/S17 武装的强化包伤害 ×0.7，同一计时加成带普攻距离 +250cm（`AttackRangeCmFlat`），强化用完或到期一起移除。
  - M20 `pull`：S01/S09 的击退命中改为 `bKnockbackPull`，朝施法者当前脚点拉近原击退距离；S15 嘲讽命中的敌人被拉近 150cm / 0.15s。
    拉近在离施法者 120cm 处截止并按比例缩短用时，已在 120cm 内不拉。M03 缩放拉近距离。
- C17–C21（连接件）读取真实的冲刺完成、护盾结束、强化包、硬控与区域；附加伤害都是 `bAddedDamage` 的构件来源命中
  （`MakeHit(Holder,构件,…)`，无释放 EffectId、无 `CastLedger`），所以不是普攻也不是原生直接命中、不暴击、不触发命中效果与吸血，
  也不会再触发 C18/C19/C21 自己：
  - C17 `zone_relocate`：`ResolveDashCompletions` 在 B12 武装之后调用 `RelocateArea`，宿主（D、S11、S12、S13）冲刺完成时取本身体
    最近创建（`CreatedSerial`，来自 `NextAreaSerial`）、仍存活、未挪过的区域或已布置未触发的陷阱，把中心移到当前脚点并置 `bRelocated`；
    M16 跟随区域（`bTether`）不参与。`EndMs`、`Pulsed` 与已排程计时不变，`LD_COMBAT_ZONE_RELOCATE`。
  - C18 `stored_force`（`RatioBp`、`DurationMs`）：S13/S14 授盾时登记 `StoredForceWatch`（护盾 EffectId、宿主），`ApplyHit` 累计该盾实际吸收；
    盾被打破、到期或被替换后 `SettleStoredForce` 存下 吸收 × `RatioBp`；S16 自然结束的反震以记录量同样存入。存量 `StoredForce` 一份、
    新存替换旧存、`DurationMs` 后失效；`SpendOnNativeHit` 在另一个已学主动的原生直接命中敌人时整份打出（同该命中的伤害类型），随即清空，
    `LD_COMBAT_STORED_FORCE`/`LD_COMBAT_STORED_FORCE_SPENT`。
  - C19 `empower_spend`：同一入口里，持有者的 S11/S17/B12 强化包（`FLDCombatEmpower`）未用完时，另一个已学主动的原生直接命中敌人
    减一次 `Charges` 并按该包的 `AmountP2` 打出物理附加命中；用完即移除强化包与它带的计时加成（攻速、M19 距离），之后的普攻不再并入；
    按释放 EffectId 与构件记 `EffectBudgets` 每次释放一次（`LD_COMBAT_EMPOWER_SPENT`）。
  - C20 `control_widen`（`RatioBp`、`DurationMs`）：宿主硬控（定身、眩晕、沉默、缴械、嘲讽）经 `OnHardControl` 落到敌人时武装（刷新）
    `WideToken`；`ReleaseAbility` 支付成本后用 `WidenTerms` 把另一个已学主动本次释放的范围项 ×(1+ratio)：区域、自身范围与弹跳半径，
    落点与陷阱半径/内圈，弹道半径，直线半宽，扇形距离（只对 `ChannelCone`，其他主动的施放距离不变），M01 扩散半径；然后消耗令牌
    （无范围的单体主动照样消耗），`LD_COMBAT_WIDE_TOKEN`/`LD_COMBAT_WIDE_SPENT`。
  - C21 `zone_detonate`（`RatioBp`）：宿主（S07、S10、S18）的原生直接命中打到站在持有者存活 S08/S20 区域里的敌人时，`DetonateZone`
    移除第一个这样的区域与它的加速，对区域内每个敌人打出 剩余跳数 × 每跳包 × `RatioBp`（快照的基础、系数、`PackageScaleBp`、伤害类型、
    每跳灼烧层数，潮湿一次）；不是自然结束，所以不触发 C06；每次宿主释放一次，`LD_COMBAT_ZONE_DETONATE`。
- C01 首次安装时在实体记录上登记 `FireWindowAnchorMs`（`NoteWorkshopInstall`，跨生命、卸下与重装保留），窗口为固定的
  (锚点+(k−1)秒, 锚点+k秒]（`LDCombatMechanics::FixedWindowEndMs`）；同生产火源的实际 HP 伤害计入所在窗口，
  `ApplyPendingHits` 在本边界全部命中提交后才由 `SettleFireWindows` 结算到点的窗口；零输入不给盾，死亡/阶段退役随身体清窗口，
  卸下或改绑丢弃未结窗口。
- 新反馈只写日志行（`LD_COMBAT_AFTERSHOCK`、`LD_COMBAT_PROJECTILE_RETURN`、`LD_COMBAT_ECHO`、`LD_COMBAT_KNOCKBACK_IMPACT`、`LD_COMBAT_LEDGER_*`、`LD_COMBAT_FIRE_SHIELD`、
  `LD_COMBAT_DEBT_REPAID`、`LD_COMBAT_CONTROL_MARK_CONSUMED`、`LD_COMBAT_REACTION_EMPOWER`、`LD_COMBAT_DEFENSE_SHOVE`、`LD_COMBAT_FINAL_KNOCKBACK`、`LD_COMBAT_ZONE_RELOCATE`、`LD_COMBAT_STORED_FORCE*`、`LD_COMBAT_EMPOWER_SPENT`、
  `LD_COMBAT_WIDE_TOKEN`/`LD_COMBAT_WIDE_SPENT`、`LD_COMBAT_ZONE_DETONATE`），没有新增状态编号。


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
  B07 的来源增伤项随区域在释放时快照，是否加成按每跳命中前目标的移动减速、硬控或击退状态判定；
  `zone_slow` 每 `IntervalMs` 判定一次，给区内敌人施加持续 `DurationMs` 的减速（不再有 linger）；有伤害的区域的 `zone_burn` 必须与脉冲同拍
  （同间隔、无延迟、脉冲覆盖整个时长，否则释放时明确失败），每一跳的伤害命中就带 `PulseBurnStacks` 层火，与该命中的 B18 水电同一元素批次；
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

纯规则在 `Combat\LDCombatKeywords`（共享破防与灼烧、吸血上限与 B02 账目批次），
Combat 身体记录上的应用在 `LDCombatKeywordsDirector.cpp`。关键词参数是内容参数 `COMBAT.Breach*`、`COMBAT.Burn*`、`COMBAT.LifestealCap`
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
- **施加途径**：破防的 `stacks` 随命中载荷，构件形式（B08 的 `basic_stacks`）在 `ApplyKeywordStacks` 中按存活施加者对每次普攻主击结算；
  火（命中载荷的 `BurnStacks`、B01 的 `basic_stacks` 与构件的火 `ComponentBurnStacks`，见下节 B18）属于元素批次（`ApplyElements`，见“水火电与反应”）；
  周期、结算、反伤、附加伤害与护盾触发的命中不算；区域的火见上文 `zone_burn`。
- **多次强化普攻**：`empower_basic` 的 `charges`（`FLDCombatEmpower::Charges`，`ConsumeEmpowers` 每次取一层）与 `stats`（期间的限时属性，
  随最后一层或窗口结束）。
- **受伤触发护盾**：`triggered_shield` 的 `Damaged` 触发（`OnDamaged`，B09）：承受敌对正伤害且仍存活后给护盾（按等级成长的 base + 生命上限比例），
  只有真正替换成功才开始 `interval` 冷却；弱盾不授予、不刷新。`AbilityHit` 触发（B16，`OnNativeDirectHit`）在持有者的原生直接命中
  对敌人造成正伤害后按同样规则授盾，冷却记在 `AbilityHitShieldReadyAtMs`；普攻与受击不触发。
- **定身**：`root` 对减速中的目标仍是定身（`DemoBuildPrimitives.RootOnSlowed`）。
- **解除**：解除定身与解除减速在释放前经 `ApplyCleanses` 结算，缴械不被解除。
- **普攻命中缩短三个主动冷却**：触发项 `cooldown_refund` 的 `BasicHit` 触发（无窗口）在每次普攻主击时缩短 Q/W/E 三个技能的剩余冷却。
- 属性 `MPRegenFlat`（法力回复固定值）与 `HPRegenFlat` 同形。

### 水火电与反应（`LDCombatElements`、`LDCombatElementsDirector.cpp`）

纯规则在 `Combat\LDCombatElements`（反应表、R、S16 记录与反震量、S21 削减、命中类别），身体记录上的应用在 `LDCombatElementsDirector.cpp`；
参数是 `COMBAT.Wet*`、`COMBAT.Electrified*`、`COMBAT.Discharge*`、`COMBAT.Reaction*`、`COMBAT.Fog*`、`COMBAT.Conduct*`、`COMBAT.Overload*`
（`FLDElementTerms::From`，不可用即明确失败）。数值单点见[内容与数值](../demo/CONTENT_AND_BALANCE.md) §4.4。

- **元素槽**：每个目标生命一个元素。火 = 存在活的灼烧层；水（潮湿）与电（感电）是 `FLDBodyCombatState::ElementMark`（施加者与其等级、法强快照、
  到期时刻）；潮湿另带一个 `COMBAT.WetSlow` 的减速实例（`SlowEffectId`，与标记同生同灭，最强减速生效）。同元素刷新（施加者换成最新者）。
- **批次**：`ApplyHit` 在伤害、控制与 `ApplyKeywordStacks` 之后、`bControllable` 时调用 `ApplyElements`：先火（命中的 `BurnStacks` 与存活来源的
  触发项 `burn` 与构件火），再水（原生 `bApplyWet`，再构件的 `ComponentWetSource`），再电（原生 `bApplyElectric`，再 `ComponentElectricSource`）；
  每次 `ApplyElement` 安装、刷新或与已有的不同元素反应，反应的即时结果可接后面的元素。原生元素读命中的施加者快照 `ElementLevel/ElementAPP2`
  （释放时由 `StampElementSnapshot` 填写）；构件的水电以该构件为来源、按存活持有者当时的等级与法强施加。
- **反应**（`ELDCombatReaction`，触发者 = 新元素施加者，两种元素都被消费）：R = `COMBAT.ReactionBase` + `COMBAT.ReactionPerLevel`×(L−1) +
  `COMBAT.ReactionAPRatio`×法强（魔法，不乘 G）。火的部分立即结算：已有火按 `BurnRemainingShares` 各归原施加者与来源，新火按其完整预算归触发者；
  这些结算命中带 `bBurnTick`。起雾（火＋水）：主目标受 R，在主目标脚下生成固定雾区（`FLDCombatFog`，每拥有者一个，新雾替换旧雾），
  生成时（`PulseFog`，在造成它的元素批次继续之前，所以同一命中后面的电已经遇到潮湿）与之后每 `COMBAT.FogIntervalSec` 给区内**无元素**敌人潮湿；导电（水＋电）：主目标与其 `COMBAT.ConductRadiusCm` 内此刻潮湿的敌人各受 R
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
- **S20**：`zone_wet` 让每次区域脉冲同时施潮湿，`zone_haste`（`UpdateZoneHaste`）在拥有者及其本场队友（释放时的 PK 隔离组与同一非零 PK 方）
  脚站在区内时给一份以区域效果 id 为键、到区域结束的移速限时增益，离开、死亡或冻结即撤；敌人与区外队友没有。
- **S21 馊水糊脸**（`grievous`，`FLDCombatGrievous`）：命中给目标较强比例与较晚到期；`HealBody` 与 `GrantShieldTo` 对新治疗、新护盾先按比例削减
  （护盾削减后再做替换判断），已有护盾不变；状态 `soured`。
- 观察：`wet`、`electrified`、`braced`、`soured` 是公开状态（30–33）；观察唯一性检查是 64 位掩码，`LDObservation::MaxStatuses` 为 16。

### 基础构件（`LDCombatComponents.cpp`）

已安装的基础构件（`FLDBuildProjectionInput::Components`，每个定义至多一次）先经 `LDAttributeProjection::Project`：未知、重复、非基础构件
或安装参数与绑定不符都明确拒绝；`stat_bonus` 成为属性视图的 `Components` 行（`GrowEffect` 不成长这些量），
`low_hp_stat_bonus` 成为 `LowHPBonus`（B20），`control_damage_amp` 成为 `ControlAmp`（B07），每个构件按 id 排序列入
`InstalledComponents`（含绑定的主动与元素、投影等级）。其余效果由 Combat 经 `CollectPassiveEffects` 读取：

- **B01/B08**：普攻主击加 1 层灼烧/破防（`basic_stacks`）。**B04**：主击实际 HP 损失乘自身伤害包份额（`LifestealShareBp`，并入的强化包不算）的 12% 治疗。
- **B02 账目**（`ledger`，`FLDCombatLedger` 存在目标这条生命上，每拥有者每构件一份）：存活拥有者的每个有效普攻或技能伤害命中
  （HP 损失或盾吸收 >0；灼烧 tick、反应与放电结算、兑现本身不算）按当时 `10G+0.08AP` 记一笔；批次首笔固定门槛 `Count` 与窗口 `DurationMs`，
  满批立即兑现为一次魔法技能类命中（`bLedgerCashOut`，状态/结算类，不再记账、不触发 B18），窗口内无新增则清空未锁定笔数；
  目标换命或拥有者离局清空。
- **B03 反伤**、**B09 受伤护盾**、**B16 主动命中护盾**见上文；**B05** 每次 D 或主动冲刺对每个真实接触的敌人追加一份物理包。
- **B06**（`nth_basic_bonus`）：本条生命每第 `Count` 次普攻主击给主目标附加从持有者推开 `DistanceCm`/`DurationMs` 的击退，无伤害；
  计数换目标不清，死亡随身体清空。
- **B12**：D 或主动冲刺释放时记录 `VoluntaryDash`（起点脚点、结束时刻）；`ResolveDashCompletions` 在 Effects 开头对到点且实际位移
  >0 的冲刺武装 `DashComplete` 的 `empower_basic`（按完成时属性快照，同来源替换）；被击退（`ApplyKnockback` 清除）、死亡或原地不动不算。
- **B13** `cast_hit_refund`（`RatioBp`）：`ReleaseAbility` 把本次实际支付的法力（M06 之后）记为 `CastLedger.PaidManaP2`；
  `OnNativeDirectHit` 在该释放第一次正伤害原生直接命中时返还 `PaidManaP2 × RatioBp`，按释放 EffectId 与构件记入 `EffectBudgets` 只返一次，
  未付法力不返还（`LD_COMBAT_CAST_REFUND`）。
- **B14** `distance_amp`（`DistanceCm`、`RatioBp`）：原生直接命中结算时施法者与目标脚点平面距离 ≥`DistanceCm`，`RatioBp` 进入加成池。
- **B15** `alternate_amp`（`DurationMs`、`RatioBp`）：每次已学主动释放更新身体的 `LastActiveRelease`/`LastActiveReleaseAtMs`（D 不计）；
  与上次不同且相隔 ≤`DurationMs` 的释放把 `RatioBp` 锁进 `CastLedger.AlternateAmpBp`（`LD_COMBAT_ALTERNATE_AMP`），
  只随本次释放的原生直接命中进入加成池。
- **B17** `idle_ready_amp`：释放时从该主动轨道的真实 `ReadyAtMs` 起已等 `DelayMs` 则锁定 `RatioBp`，只随本次释放的原生直接伤害
  （`bDirectAbilityDamage` 的载荷与其区域每一跳）进入加成池；派生、状态伤害不继承；释放后重新等待。
- **B10/B11/B18 元素**：唯一入口 `ApplyComponentElement(Hit, Element, Component)`：火给命中加一层构件来源的灼烧
  （`ComponentBurnStacks`）；水/电记为命中的 `ComponentWetSource`/`ComponentElectricSource`（同一命中两个构件的同种元素是明确错误），
  由元素批次作为该构件自己的施加结算，C09/M11 读到的来源是构件而不是父攻击。B10/B11 作用于普攻主击；B18 只作用于绑定主动（仍已学）的
  原生直接有效命中。
- **B02** 只记普攻主击与技能伤害命中；灼烧 tick、反应/放电/提前结算（`bStatusSettle`）、兑现与欠款都不记。

## 构筑与来源

成长构筑由 `ALDDevelopmentDirector` 持有（`FLDGrowthBuild`：等级、按五维设计顺序（体格、脸皮、力道、脑力、身手，即 native id 升序，`FLDDemoContentDefinitions::StatsInDesignOrder`）每个五维一格的已分配点数、至多三个 `{Id, Slot}`
主动、按 id 排序的待学图纸与 Revision）；
点数、主动机会与命令合法性见 [成长事务](GROWTH_TRANSACTIONS.md)。
`MakeProjectionInput` 把构筑交给 `FLDBuildProjectionInput`（等级、持有点数的五维 `StatPoints`、Q/W/E 主动、构筑修订）；
`LDAttributeProjection` 按每个五维的 `PerPoint`（`stat_physique` 生命上限与生命回复、`stat_thick_skin` 护甲与魔抗、`stat_strength`
攻击力与暴击率、`stat_brains` 法强、技能急速与法力回复、`stat_agility` 攻速与移速比例）把点数作为本人属性视图的 `Choices` 行
（比例项进入 `PercentBonus`）加到等级成长上，最终统一夹到目录上下限。
同一投影还按已分配点数与 `Parameters.KeystoneThresholdPoints` 分别生成五维 `Keystones` 标记；
门槛不从钱包、最终属性或夹紧后的值推导，各项可以独立激活。体格关键石只把自身构筑来源实际恢复的 HP 转为护盾
（不含债务偿还、过量治疗或空来源的环境奖励），经 `GrantShieldTo` 的护盾替换规则结算，成功施加才启动冷却；
脸皮关键石在普通 `PrimaryGuard` 后追加一次敌方本体普攻减伤，只有实际减伤才启动冷却。
力道关键石把对已减速或受硬控目标的加成并入统一 `DamageBonusBp` 池；攻速降低和击退不满足条件。
脑力关键石把施法释放的共享 `CastLedger` 沿元素施加传递，在该释放首次触发反应时返还实际支付法力的一次比例；
身手关键石在自愿冲刺完成时快照一个一次性强化普攻包，与其他强化包在同一主普攻、同一次暴击中结算，
过期、重冲刺和消耗沿用 `FLDCombatEmpower` 的规则。数值只在[内容与数值 §4.1](../demo/CONTENT_AND_BALANCE.md#41-五维属性与成长参数)维护。

AI 的开局时刻/候选位由 `ALDGameMode::DrawOpponentOpeningPlan` 在绑定时从现有服务器 `SpawnRandom` 流抽取，
不是每个 AI 新建 GUID 种子。`LD_AI_OPENING_PLAN` 记录抽取前的流状态 `seed`，可用同状态重放这两次抽取；
该状态只在服务器日志中，纯策略只收到计划的时刻和候选位。
构筑命令或升级都先组成变更后的完整 `FLDBuildProjectionInput`，
携带 Combat 上次安装的 `FLDExpectedBuildVersions` 交给纯函数 `PrepareBuildChange`；
同一许可内整笔成长提交全部可行后才 `ApplyBuildChange`，失败时经验、点数与构筑都不变。只改待学图纸的命令推进构筑修订但不重投影 Combat。
DEV 测试会话的测试属性层（`FLDBuildProjectionInput::DevBonuses`）也只由 Combat 持有：成长域输入不带它，
Combat 把已安装的层带进每次构筑变更，投影把它作为独立的 `Dev` 来源行；见[成长事务](GROWTH_TRANSACTIONS.md)。
来源冷却轨道与回绑只由 Combat 持有：遗忘（目前只有 DEV `LearnActive` 指定槽）不清洗冷却，重学回绑原结束时间。
满槽时普通命令不替换已学主动（`SlotOccupied`/存为图纸）；满槽替换与遗忘只在休息时经休息服务进行。
等级、技能学习和 cooldown 是参赛状态，不应随 UI 或 Pawn 重建而免费重置。

交战窗口（`GROWTH.BuildSafeWindowSec`）由 Combat 记在实体记录的 `LastBuildUnsafeAtMs` 上（跨生命保留，只前进）：它不限制任何成长命令，只供 HUD 在交战中收起野外摘要。
`StartNavAttack` 受理的普攻起手、`LDCombatSourceRules::IsOffensive` 的主动释放、`ApplyHit` 中来源不是自己的正伤害（标记受害者并记入
拥有者，灼烧 tick 同样经此路径）、`CommitControl` 实际落地的硬控（定身、眩晕、缴械、嘲讽、沉默）或击退都刷新它；
减速、护盾、回复与非进攻主动（贴脸补一拳、锅盖挡脸、左右开抽与天生冲刺）不刷新。`LDCombatSourceRules::BuildSafeAtMs` 给出
`LastBuildUnsafeAtMs + 窗口`（没有记录为 0），本人 `CombatView.BuildSafeAtMs` 发布这一值（`ReadBuildSafeAtMs` 保留为读取入口，成长提交与 AI 不再读）；
DEV 重置把它清零。

### 局末构筑蓝图

`ALDCombatDirector` 持有本局唯一的 `FLDBuildLedger`（`Development/LDBuildBlueprint.h`，挂点在 `LDCombatBlueprintLedger.cpp`），
它只读 Combat 已安装的 `FLDBuildProjectionInput`，不是第二份构筑真相。版本按**结构**划分：持有点数的五维、Q/W/E 主动与已安装构件
及其安装参数（`host` 为所在主动/宿主构件或 B18 绑定的主动，参数按键排序：`target_kind` active/component/dash、`element`、
`bound_component`）；只换安装、改目标或改绑定也开新版本，重构前后的收益分段归属。只升级或只改 DEV 测试层的修订留在当前版本（更新等级区间与修订区间，
`dev_layer` 标记测试层），结构相同的修订不另造版本。每个版本带 `[from_ms,to_ms)`（权威毫秒，含开局倒计时）与修订区间；
第一个版本从 0 开始，即开局的空构筑。

事实只在 Combat 提交它的地方记一次：`ApplyBuildChange` 安装后（先补记被替换的开局构筑）、`LandBasic` 落地的普攻与
`ReleaseAbility` 的主动释放（记下 EffectId 所属版本；天生冲刺不计施法）、`ApplyHit` 的已提交命中、`HealBody` 实际回复的生命与
实际偿还的 M12 欠款（分列，还款不算回复）、`SettleRegen` 实际恢复的自然（含宿舍）生命、`GrantShieldTo` 实际授予的护盾。
命中的造成伤害（`DamageP2`，含被目标护盾吸收部分，每次命中一次）记给记功参赛者**释放该效果时**的版本；
没有登记释放的反应命中（反伤、盾破反击、爆盾）记给提交时安装的版本；承受伤害、自身护盾吸收与生命损失、
本人削减实际减掉的量（S16 姿态 `BraceCutP2` 加主击护甲 `GuardCutP2`，记为减免）、一次性回复、还款、自然恢复和获得护盾
记给受影响者提交时安装的版本。自伤与受保护的零结果不记。触发为已安装构件一次产生真实结果的触发（`LedgerComponentTriggered`）；拒绝为本人战斗请求因玩法门槛被拒（忙碌、冷却、普攻间隔、法力、控制、目标、方向、射程、视线；协议、过期与阶段拒绝不计），
直接请求在回执首次转为拒绝时（`StoreReceipt`）计一次，寻路普攻在出手被拒时计。公共休息室中的训练（休息生命与共享真实训练目标，§16.3）
一律不记，不论攻击者是否属于目标对应槽位。早期版本写下的 `collected:false` 与 `null` 照原样读出，不当作 0。

触发的计数点（每次触发给持有者记 1，不按受击人数；读取活着持有者的触发在其生命已结束时不记）：

- 改变一次命中，在 `ApplyHit` 提交时：B04 吸血实际回复或还款；B07 增伤进入一次正伤害命中（每次命中）；B10/B11/B18 元素随可控命中施加；
  B14 增伤进入一次原生直接命中（每次命中）；B15、B17 每次释放的首个正伤害命中（`FLDCombatCastLedger`）；
  B13 法力实际返还；B16 护盾实际授予；B06/C04 的击退实际生效；
  B01/B08 在主击的火层与破防实际施加时（`ApplyElements`、`ApplyKeywordStacks`）。
- 产出自己的投递，在为已解析的目标发出时：B02 兑现（M02 锁定的批次在 `AdvanceWorkshop` 结算成立时计，失效不计）；
  B03/C08 反击（`DeliverCounter`）；B05 每个接触体；C02 撞墙或撞到合法身体；C05 标记被消耗的追击；C06 余震释放；C10 爆盾波及到身体；M13 回程发出；M14 穿透到额外身体（每具一次）或 S10 带额外一跳的弹射发出（每次释放一次）；M15 回响排程；M16 跟随区域释放；M17 推开至少一个敌人；M18 最后一击带出击退；M19 强化包带距离武装；M20 拉近至少一个敌人；C13 实际减少了冷却；C14 记号被消耗；C15 宿主冷却实际减少；C16 法力实际增加；C17 区域或陷阱实际挪动；C18 存量被打出（存入不计）；C19 每次花掉强化；C20 释放消耗令牌；C21 区域被引爆。
- 授予与消耗：B09/C01 护盾实际授予；C03 退还生效；C07 法力实际增加；C11 治疗实际生效；C12 重放已安装的基础构件；
  B12/C09 强化包被落地普攻消耗；M12 一次命中的份额转为欠款。
- 改装件：M01 扩散实际波及另一具身体（每次投递一次；M02 爆点下只塑形，不计）；M02 锁定反击爆点或账目批次；M11 反击或放电转向标记目标。
- 不计：常驻与编译期改写（M03–M10）、预览与候选（B02 记账条目、C01 火伤累计、C05 设标、C08 储存、
  B12/C09 武装）以及被拒、为零或没有目标的触发。`Combat.Components.Ledger` 与 `EmpowerMerge` 检查 B02 兑现与 B12 消耗各计一次、
  记账条目不计。

结果发布时 `ALDGameMode::PublishBuildBlueprints`（`LDGameModeBlueprint.cpp`）用 `LDBuildBlueprint::Compose` 把台账与冻结结果
（公开 ResultId 作为 Match ID、原因、胜负、开始/结束时刻、本人等级与 K/D/A、产品版本、协议版本、目录根摘要与 schema/handler/projection
修订）组成每名在场真人的记录，经 `ULDBuildBlueprintComponent` 只发给其拥有连接；AI 不生成，任何人都收不到别人的构筑。
文件格式、保存、保留与分享见[视野与 UI](VISION_AND_UI.md#构筑蓝图文件与查看)。

### 技能等级成长

技能的固定数值随拥有者**当前等级** L 分段成长：1–10 级每级使用 `SKILL.GrowthPerLevel`（15%），
11–16 级每级使用 `SKILL.GrowthPerLevelAfter10`（7.5%），所以 G(10)=2.35、G(16)=2.8。
每个主动只有一组作者数值（`FLDDemoAbilityValues`：法力、冷却、效果），代码不再有 rank 或高级倍率。

- **成长范围**（`LDSkillGrowth::GrowEffect`）：效果的基础量（伤害、护盾、反伤、强化普攻与周期伤害；法力授予除外）、S16 的记录上限与限时属性
  的点数（攻击力、法强、护甲、魔抗）。法力、比例、持续时间、冷却、射程与层数不成长；属性系数不成长。
- **预计算**：`FLDDemoContentSnapshot` 构造时对每个主动、每个等级 1..`MaxLevel` 用 `LDSkillGrowth::GrowValues` 生成一行；
  `FindValues(Ability, Level)` 返回该行，等级越界或某行算术溢出（内容验证已证明不会发生）时返回空并由调用方明确失败。
- **读取**：主动经 `ActiveOf` 在准入与释放时取成长后的数值，命中载荷在释放时快照；升级重新投影构筑；
  护盾与灼烧在施加时固定（灼烧层快照施加者等级与法强）。卡面用同一 `LDDemoCardText` 与 `FindValues` 渲染当前等级的数字（见[视野与 UI](VISION_AND_UI.md)）。

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
`ApplyCampReset` 与世界成员安装在同一无重入提交中安装状态/上限/满 HP/MP、清仇恨、技能就绪；不改 EntityId/BodyIdentity/Life，不发行新成员、不产生死亡或 XP、不清 Hit/请求/死亡拒重水位。
已清状态的旧到期回调按原 EffectId 与目标生命核对，不重新生效，也不误删后来的新实例；到家前未命中的独立效果之后仍以同一生命正常校验。

## 生命周期矩阵

| 事件 | 构筑/资源/持续贡献 | 未释放动作/订单 | 已释放独立效果 | 冷却/奖励 |
| --- | --- | --- | --- | --- |
| 前闸拒绝 | 不改保护和资源 | 不替换当前单 | 无 | 不写 CD/节拍 |
| 意图后游戏拒绝 | 保护已解除 | 未准入不替换旧单 | 无 | 不扣 MP、不写 CD；失保不回滚 |
| S/新单/取消 | 不撤回已发生状态 | 取消可中断工作 | 不回收、不退款 | 保留已承诺节拍与成本 |
| 换槽/升级 | 一次最终投影 | 验实际来源条款 | 保持旧快照 | 旧 ReadyAt 不重算 |
| 替换/遗忘主动 | 遗忘旧等级与持续贡献 | 旧来源未释放的失效 | 继续原期限 | 原发行记录保留，重学沿原结束 |
| 来源死亡 | 构筑/候选保留，活体持续作用停止，清旧控制 | 取消 | 继续 | CD/节拍继续；在局死者有效击杀仍获奖 |
| 死亡等待选择 | 更新保留构筑，不治疗/复活 | 不发身体动作 | 新学能力不自动生效果 | 不刷新 CD |
| 目标死亡 | PendingDeath 不被救活 | 旧目标工作失效 | 不重绑新 Life | 同生命仅一个 DeathKey |
| 复活 | 新 Life 满最新 HP/MP，清旧控制，建立保护 | 旧生命请求无执行权 | 旧来源效果可继续，不命中被旧 Hit 绑定的新生命 | CD/节拍按时间 |
| 离局/断线 | 立即撤资格、移身体 | 取消并拒新命令 | 立即取消 | 不是击杀；此前事实按冻结资格结算 |
| 野怪 Returning / 到家 | 仍可受伤/控制/死亡 / 原子清临时状态并满血蓝，同刻 PendingDeath 优先 | 不回头追击 / 回等待受击 | 同生命 Hit 照常 | 不发奖、不换 Life |
| 终局 | 先冻结写入再清理 | 拒绝新受理 | 不再伤害/治疗 | 已提交回执可重放 |
| 新局 | 新 Generation + LedgerMatch，重建局内状态 | 旧请求全部拒绝 | 清空 | 新 CD/死亡/事务域 |

## 检查入口

`Lockdown.Rules.DemoCombatEvaluation.*`、`DemoCombatStatus.*`、`DemoCombatSources.*`（含 `InnateDash`）、`DemoOrders.*`、
`DemoAttributeProjection.*` 与 `DemoCore.*` 分别覆盖纯数值、Root/屏障/回复/冷却规则、来源轨道回绑、
攻击移动选敌与订单簿换局/换命隔离、属性及核心身份/许可/时间/投影边界；`DemoBuildExpansion.Slow/TimedBonus/ConditionalTriggers` 覆盖减速、限时增益
与低血条件项的纯规则，`DemoCombatPrimitives.*` 覆盖硬控递减、`CommitControl`、剩余冷却缩减与解除减速，
`DemoAttributeProjection.StatPoints/TemporaryTerms/LowHPTerm` 覆盖五维点数投影、临时项投影与低血条件项，
`DemoCombatEvaluation.ControlAmplify` 覆盖增伤结算，`DemoMechanics.ShieldEvents/Knockback/Periodic` 覆盖护盾打破判定、
击退方向与区域脉冲时刻；`DemoControl.ActionMatrix/EmpowerOnce/ThornsRules/ExpansionContent` 覆盖动作禁止矩阵、强化普攻消费、反伤触发/去重/数值与条目的类型化导入与显示；
`DemoGrowthSkillGrowth` 覆盖 G(L)、成长取整与按等级成长的单级数值；`DemoCombatCues.*` 覆盖动作提示的选取与优先级，
`DemoBodyMotion.*` 覆盖身体表现时间轴（前冲、施法聚能、与权威释放时刻对齐的火花、随攻速变化的冲击帧、闪色、片段定位、出现、倒地与同一动作的重启判定），
`DemoBuildPrimitives.*`覆盖关键词纯规则、伤害加成池（技能暴击被拒）、沉默与解除、共享破防与灼烧、命中中的加成池、吸血上限、减速中目标仍被定身、交战窗口的触发与不触发、区域脉冲次数、多次强化普攻与完整减益观察，
`DemoElements.Rules/FogAndSlot/BraceAndGrievous`覆盖反应表与 R、元素槽与潮湿刷新、起雾的火结算与雾区归属、S16 削减记录与死亡不反震、S21 削减新护盾与治疗，
`Combat.Components.*`覆盖构件投影项与拒绝、多来源强化包并入一次主击（含 B04 份额与 B06）、B04 自身份额吸血（`PunchHealOwnShare`）、B13 释放返蓝（`CastHitRefund`）、
B14 距离增伤（`DistanceAmp`）、B15 换招增伤（`AlternateAmp`）、B16 主动命中护盾（`AbilityHitShield`）、C13 换招减冷却（`HitHasteOther`，含 D 接触入口）、C14 记号增伤（`SetupMark`）、C15 反应减宿主冷却（`ReactionHaste`）、C16 多目标回蓝（`MultiHitMana`）、M16 区域跟随（`ZoneTether`）、M17 破盾/反震推开（`DefenseShove`）、M18 最后一击击退（`FinalKnockback`）、M19 强化普攻距离（`EmpowerReach`）、M20 拉近（`Pull`）、C17 冲刺挪区域（`ZoneRelocate`）、C18 吸收存量打出（`StoredForce`）、C19 花掉强化包（`EmpowerSpend`）、C20 硬控放大范围（`ControlWiden`）、C21 引爆区域（`ZoneDetonate`）、B12 冲刺完成武装、B02 账目批次与兑现、
元素入口（火加层，水/电记为构件自己的施加）与 B18 绑定，
`Combat.Events.*`在带已批准世界视线的隔离世界里从真实释放、投送、区域、飞弹、普攻、灼烧与工坊入口驱动：命中转控制带出口（C05/C07）、
B10/B11 水电的构件来源（C09/M11）、S08 火与 B18 电同批先火后电及雾的即时潮湿、B02 不记结算、S19 触发后引信与到期不爆、
欠款不吃 S16、C10 共享 C11 预算、M01 只扩散伤害、S20 队友加速、C01 固定窗口与边界收齐，
`DemoAssist.*` 覆盖助攻贡献记录与窗口、经验平分、击杀通知/结算行，以及击杀者与助攻者的死亡奖励一次结清，
`DamageRecap.Window` 覆盖承伤样本的记录与过期、窗口边界、按攻击者与来源求和及排序、各来源总量和列表的形状校验，
`DamageRecap.Chain` 覆盖反应伤害按规则链分行、最近一击的距今时长、规则链与助攻提示的形状校验，
`BuildBlueprint.Attribution` 覆盖构筑蓝图台账的版本划分（只升级、同结构异序与 DEV 测试层不另造版本）、按释放版本归属的命中、
时间段与收尾，以及全部事实自开局即采集并按版本分列，`BuildBlueprint.Components` 覆盖只改安装/目标/绑定即开新版本、
构件行的宿主与参数、严格往返与失效安装提示，
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

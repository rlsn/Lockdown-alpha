# Demo 战斗与属性

**目标：v0.10.0 Demo · 职责：M4，衔接 M3/M5/M6/M7/M8/M9/M10 · Unreal 5.8.4**

本报告依据 [App Spec](../APP_SPEC.md)、[共享契约](SHARED_CONTRACTS.md)、
[架构审计](ARCHITECTURE.md)和两份独立专家研究，定义战斗模块的实施接缝。
**这是静态研究及目标设计，不是现有 API、运行通过或已完成裁剪的声明。**
源码证据固定于 `fa11d09a762bccadf495191072e222dd158fc014`；研究工作区的运行文件与该基线一致。
当前组件知识仍见[战斗与成长手册](../systems/COMBAT_AND_GROWTH.md)，实施依赖见
[实施计划](IMPLEMENTATION_PLAN.md)的 I3/I4/I5/I6。

## 1. 结论与已确认边界

**改造现有 `ALDCombatDirector`，不建立第二套战斗、属性、冷却、奖励或位置系统。**
现有来源验证、动作阶段、命中、状态及生命机制可局部复用，但旧六属性、多个执行入口、
家族共享主动冷却、最后接触者推奖、旧世界时钟和效果提示旁路不能直接成为 Demo。
基础身份、命令和提交边界可以据此实施；真实数值行为仍须批准内容方案、接通导入及原生消费者。

以下是已确认规则，不再作为备选方案。公式、具体目录和数值的唯一维护位置为约定的
`CONTENT_AND_BALANCE.md`；本文只引用其键及操作语义，不维护第二份参数表。

| 范围 | 已确认规则 |
| --- | --- |
| 基础与成长 | 所有人初始只有近战单体物理普攻；装备或属性可以增加攻击距离、提供普攻群攻，不能把整局锁死为短距单体。AP 不自动进入普攻。 |
| 入口 | 角色主动 Q/W、装备主动 1/2 进入同一 Combat；装备主动在店外、圈内、两个装备槽已满时仍按战斗条件使用，不依赖商店会话、报价、购物距离或余额。 |
| 重复装备 | 允许同时两件同款；普通基础属性相加，同名被动只生效一次。两个同款主动各自独立冷却，可以连续各用一次，仍需分别满足资源和动作条件。 |
| 冷却 | 换槽、升级、卖买、遗忘重学和死亡不能清洗未完成冷却；来源丢失期间继续计时，重新持有沿用原结束时间。 |
| 资源与死亡 | 最大 HP/MP 变更保持原生命/法力百分比；一次最终投影只换算一次。死亡保留构筑、财产、经验和候选；等待时可确认选择，但不治疗或复活。正常复活满新上限，旧临时控制清除，冷却继续。 |
| 保护 | 通过身份、当前局/生命、规范包、新鲜请求及写闸前验的进攻意图就解除保护；随后目标、来源、距离、蓝量、冷却或控制失败仍解除。旧包、重放、畸形包不改变新生命。 |
| 效果 | 已成功释放的独立效果在来源死亡、出售或遗忘后继续到原结束；未释放动作取消。持续来源贡献随维持条件失效停止。离局立即移除身体、取消未结效果；不追溯回滚此前事实。 |
| 致死 | 最后一击的实际参赛者所有者独占全部金币和 XP；无助攻/近邻分摊，无主不继承历史攻击者，自杀、离局和回位不奖励。已死亡但仍在局的所有者可因有效效果击杀别人获奖。 |
| 营地 | 野怪未受攻击不主动攻击；同营地受击共同反击但不共享隐藏坐标。Returning 仍可受伤/控制/被杀，不回头追击；存活成员成功到家才满血蓝、清临时状态/仇恨、技能就绪，不复活死成员、不发奖。 |
| 视野 | 失视野取消锁定并停止该追击订单，不留 LastSeen 标记。可见对手可展示名称、等级、HP、MP、状态及装备展示内容，不展示技能、CD、完整属性、金币或装备私有身份。 |
| 占领与结束 | 伤害和圈内控制本身不清占领；真实离圈、死亡或资格变化交生命周期/占领域。完成同刻不能跳过到期致死。先锁唯一结果、关闭领域写闸，再清理。 |

“同名被动一次”不授权不同强度来源一律取最强；首版等价同款来源可稳定选代表。
批准目录若出现不等价同 family 组合，必须明确聚合规则或拒绝该输入。
死亡镜头可以拖动静态地图，但尸体、镜头及遗留效果均不生成新的活体视野。

## 2. 已核验实现与真实调用链

下表是当前源码事实，非目标实现。引用的行号均属于上述运行基线。

| 证据 | 真实入口 | 现状与必须改变的接缝 |
| --- | --- | --- |
| E01 | [LDCharacterBuild.h:8–44](../../Source/Lockdown/Public/Development/LDCharacterBuild.h#L8-L44)、[LDCharacterBuild.cpp:86–178](../../Source/Lockdown/Private/Development/LDCharacterBuild.cpp#L86-L178) | 已分 Learned/Gear 来源，但仍有职业、六属性、点数、旧等级与按技能数组的 cooldown；`StartingBuild/TacticalStart` 不是仅普攻初始化，不能将 STR/INT 改显示名冒充 AD/AP。 |
| E02 | [LDCharacterBuild.cpp:183–207](../../Source/Lockdown/Private/Development/LDCharacterBuild.cpp#L183-L207)、[LDV08Loadout.cpp:22–77](../../Source/Lockdown/Private/Development/LDV08Loadout.cpp#L22-L77) | `Prepare/Project/InvalidateSources` 有候选、版本和真实实例验证；当前十一绑定、六共享动作及重复 active-family 拒绝须替换，不能只改 UI 数量。 |
| E03 | [LDGearRules.cpp:10–28](../../Source/Lockdown/Private/Inventory/LDGearRules.cpp#L10-L28)、[LDGearTypes.h:4–33](../../Source/Lockdown/Public/Development/LDGearTypes.h#L4-L33) | 旧属性预算裁剪、modifier bit OR、特殊武器基值和整数攻速间隔并非新聚合模型；缺独立 AP、暴击、Haste 和完整最大资源/回复字段。 |
| E04 | [LDGameMode.cpp:522–535](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L522-L535)、[LDInventoryDirector.cpp:45–57](../../Source/Lockdown/Private/Inventory/LDInventoryDirector.cpp#L45-L57)、[LDCharacterMovementComponent.cpp:102–126](../../Source/Lockdown/Private/Character/LDCharacterMovementComponent.cpp#L102-L126) | `RefreshBuild` 按旧绝对当前值封顶并重置回复时间；`Changed` 不等于完整属性投影；`GetMaxSpeed` 又有玩家/NPC/道路/全图分支。只改属性表或 `MaxWalkSpeed` 不足。 |
| E05 | [LDTacticalRequest.h:6–32](../../Source/Lockdown/Public/Combat/LDTacticalRequest.h#L6-L32)、[LDTacticalRequest.cpp:2–24](../../Source/Lockdown/Private/Combat/LDTacticalRequest.cpp#L2-L24)、[LDPlayerController.cpp:561–580](../../Source/Lockdown/Private/Framework/LDPlayerController.cpp#L561-L580) | 有有界来源/目标/版本请求和拥有连接 RPC；旧 schema/wire、双目标及槽位含义仍在。实际目录入口叫 `RequestContent`，没有现有 `StartContentAction`。 |
| E06 | [LDPlayerController.cpp:276–309](../../Source/Lockdown/Private/Framework/LDPlayerController.cpp#L276-L309)、[459–470](../../Source/Lockdown/Private/Framework/LDPlayerController.cpp#L459-L470)、[552–558](../../Source/Lockdown/Private/Framework/LDPlayerController.cpp#L552-L558) | 回执主要是 request→字符串，没有精确条款指纹和完整阶段；旧反馈 RPC 不带 match/life。取消虽传 Through，却可能直接取消当前较新动作，不能原样继承。 |
| E07 | [LDCombatDirector.cpp:129–184](../../Source/Lockdown/Private/Combat/LDCombatDirector.cpp#L129-L184)、[LDContentActions.cpp:36–114](../../Source/Lockdown/Private/Combat/LDContentActions.cpp#L36-L114)、[LDContentRelease.cpp:22–147](../../Source/Lockdown/Private/Combat/LDContentRelease.cpp#L22-L147) | 普攻起手已有节拍/命中期限，能力主要在释放支付；仍有旧优先级、起手付费特例和多个可旁路入口。装备执行不依赖 Vendor，可直接收敛而非另建商店施法。 |
| E08 | [LDCombatDirector.h:16–153](../../Source/Lockdown/Public/Combat/LDCombatDirector.h#L16-L153)、[LDTacticalExecution.cpp:178–190](../../Source/Lockdown/Private/Combat/LDTacticalExecution.cpp#L178-L190)、[LDTacticalFields.cpp:18–24](../../Source/Lockdown/Private/Combat/LDTacticalFields.cpp#L18-L24) | 身体 Identity、目标 Life 和部分 credit 已存在；普通弹体/Field/PersistentZone 的来源和离局策略不同，弱 Pawn 指针不能承担持久信用。 |
| E09 | [LDTacticalRules.cpp:20–53](../../Source/Lockdown/Private/Combat/LDTacticalRules.cpp#L20-L53)、[LDCombatDirector.cpp:291–351](../../Source/Lockdown/Private/Combat/LDCombatDirector.cpp#L291-L351) | `Damage` 的 contact 包含屏障吸收，不是实际 HP 损失；命中/状态没有统一目标保护闸。旧屏障严格更大替换及旧上限也不等于新内容政策。 |
| E10 | [LDContentGeometry.cpp:46–71](../../Source/Lockdown/Private/Combat/LDContentGeometry.cpp#L46-L71)、[LDContentRelease.cpp:54–75](../../Source/Lockdown/Private/Combat/LDContentRelease.cpp#L54-L75) | `CreateField` 在 staging 就可能销毁旧场，`DestroyField` 可写 CD/经济记录；“先分配后扣费”不证明整个 Prepare 无副作用，也不能原样用作终局清理。 |
| E11 | [LDWorldDirector.cpp:90–110](../../Source/Lockdown/Private/World/LDWorldDirector.cpp#L90-L110)、[LDEconomyLedger.cpp:36–100](../../Source/Lockdown/Private/Economy/LDEconomyLedger.cpp#L36-L100)、[LDEconomyLedger.h:21–60](../../Source/Lockdown/Public/Economy/LDEconomyLedger.h#L21-L60) | 无主 Contact 可留下旧 LastOwner；Reward 新造事务 ID、先账本再 XP，末尾失败不等于未提交；旧 PvP 非统一金币+XP。Ledger 不含 XP/等级/选择。 |
| E12 | [LDGameMode.cpp:388–409](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L388-L409)、[437–453](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L437-L453)、[LDEconomyDirector.cpp:19–45](../../Source/Lockdown/Private/Economy/LDEconomyDirector.cpp#L19-L45) | 生命换代和满资源返回存在，但有旧死亡损失；Ledger GUID 与网络 Generation 未形成目标强类型映射。 |
| E13 | [LDWorldClock.cpp:9–28](../../Source/Lockdown/Private/World/LDWorldClock.cpp#L9-L28)、[LDCombatDirector.cpp:206–224](../../Source/Lockdown/Private/Combat/LDCombatDirector.cpp#L206-L224)、[LDObjectiveDirector.cpp:345–360](../../Source/Lockdown/Private/Objectives/LDObjectiveDirector.cpp#L345-L360) | 旧时钟回拨 World 时间补算当前姿态；环境工作早于 Finished 检查，终局先清服务后设结果。必须改真实 step/事件/permit，而非尾部多调用一次数圈人数。 |
| E14 | [LDVisionDirector.cpp:117–186](../../Source/Lockdown/Private/Vision/LDVisionDirector.cpp#L117-L186)、[240–322](../../Source/Lockdown/Private/Vision/LDVisionDirector.cpp#L240-L322)、[LDContentLifecycle.cpp:41–56](../../Source/Lockdown/Private/Combat/LDContentLifecycle.cpp#L41-L56) | 有逐观察者重查和正常 Host 原生过滤，但自动 Footstep/Trail 的产生、提前授权、发布例外均存在；不能只停一个技能或隐藏一个 Mesh。 |
| E15 | [LDTacticalCatalog.cpp:16–61](../../Source/Lockdown/Private/Combat/LDTacticalCatalog.cpp#L16-L61)、[LDEquipmentCatalog.h:6–27](../../Source/Lockdown/Public/Inventory/LDEquipmentCatalog.h#L6-L27)、[LDEquipmentCatalog.cpp:29–54](../../Source/Lockdown/Private/Inventory/LDEquipmentCatalog.cpp#L29-L54) | tactical importer 不解析 `effects`；`RankKey` 实际是字符串，`Magnitudes` 才是整数数组，且仅单个 scaling key。装备 typed 定义/导入/执行未接通非零主动 MP 成本。 |
| E16 | [LDV08HUDModel.cpp:15–39](../../Source/Lockdown/Private/UI/LDV08HUDModel.cpp#L15-L39)、[SyncB84Data.py:31–105](../../Scripts/SyncB84Data.py#L31-L105) | HUD 另读 JSON 成本键、使用 family-max cooldown、保护禁用；compiler 固定旧目录数量并读取 acquisition 原生文件。现有装备源数据的主动成本均为零，不能声称已复现某件非零装备免费释放。 |

真实链路分为三条，实施时必须一起改接：

1. `StartingBuild/TacticalStart → Development.Prepare / LDV08.Project → Inventory.Changed + RefreshBuild
   → Combat.Body/Sync → SyncOwner → OwnerState/CharacterMovement`。目标投影替换这条链的旧政策，
   不在旁边增加一个只供属性面板使用的计算器。
2. `Input/SendItemAction → ServerTacticalAction → EnqueueAction → QueueContent → Sample
   → RequestContent → RequestBody / RequestEquipped / RequestTactical → Release → Sync/回执`。
   收敛后的外部调用只有下节窄入口；低层执行函数不能继续供 AI 绕过。
3. `Projectile/Content/Basic/World 命中 → Sample → Damage/ApplyStatus/Contact → Die
   → World.Defeated/PvPKill → EndContenderLife → 服务/复活/World/Objective`。
   目标以统一 Hit 和不可变死亡事实替换旧 Contact 推奖，移除旧服务与并行怪物攻击计时。

## 3. SC-09：状态所有者、类型与正式接缝

### 3.1 唯一写者与存储

下面的新增名称均为**拟建类型/函数**。共享核心类型由 `SESSION_MATCH_LIFECYCLE.md`
唯一维护；本模块不另建第二种 Match、Life、时钟或写闸。

| 状态 | 唯一写者与拟建落点 | 只读投影/消费者 |
| --- | --- | --- |
| Match 映射、模拟时钟、生命资格、终局闸 | 现有 `ALDGameMode`；共享 `FLDMatchContext/FLDCombatBodyRef/FLDGameplayWritePermit` | Combat 调度、World、成长；GameState 只放已批准公共事实 |
| 当前 HP/MP/屏障、临时状态、Action、Effect、Hit | 现有 `ALDCombatDirector::Bodies` 及其有限工作集合；新增 typed 资源/效果结果 | `Sync/SyncOwner` 的自身投影；World 过滤后观测 |
| 属性最终值及来源解释 | Combat 纯投影函数，拟 `Combat\LDCombatAttributes.h/.cpp`；结果安装在现有参赛/身体状态 | Movement、执行器、自身 UI/AI；不是另一份可写 HP/MP |
| 学习、等级、永久属性、候选/机会 | 成长域的现有 `FLDCharacterBuild/ALDDevelopmentDirector` | Combat `PrepareBuildChange` 输入；不得全量回写旧 cooldown |
| 金币、真实装备实例 | 精简后的同一 Ledger/Inventory/Vendor | Combat 读取已提交实例/绑定；不拥有另一套库存或金币 |
| 来源绑定、主动 readiness、被动去重历史 | Combat 管理参赛记录中的持久来源状态；买卖/选择的同一提交安装绑定变化 | 逐实例 `IssuedCooldownId/ReadyAt`；UI/Nav 不写 CD |
| 参赛者普攻节拍 | Combat 的局内持久 `NextAttackAt`；当前 Action 仍绑定一次身体生命 | Nav 只查询；取消、重绑身体、死亡不提前未完期限，时间照常流逝 |
| 死亡事实 | Combat 第一次 HP 归零锁存；Life 消费同一事实改变资格 | `CommitDeathReward` 是唯一奖励 sink，营地不重复推奖 |
| 导航订单/运动与观察授权 | 现有 Navigation/CharacterMovement 和 Vision/World | Combat 使用当前样本；不持第二条路线或隐藏目标索引 |

目标协议统一：Generation/Life 为 `uint32`；请求序列、CombatToken、AttackCycle、
事件序号及 Build/Inventory/Item/Account/SourceBinding/Readiness/Attribute/Resource/CombatState/
Owner/Receipt/Action revision 为 `uint64`；身体、参赛者、实例、`IssuedCooldownId` 为 `FGuid`。
期限/边界为 `int64` 毫秒，原始模拟时刻为 `double` 秒；资源采用内容提案的 `int64` P2 编码。
修改 wire 宽度时同批改序列化、生产者、消费者及拒绝测试，不保留旧宽度兼容分支；
计数耗尽明确失败，不能回绕成旧身份。

### 3.2 身份与版本表

| 拟建类型/键 | 字段与语义 | 不变量 |
| --- | --- | --- |
| `FLDMatchContext` | `uint32 Generation; FGuid LedgerMatch` | GameMode 每局建立同一映射；网络只声明 Generation，服务器解析账本 GUID，不能强转或各域独立生成局身份。 |
| `FLDVictimKey` | `Kind; FGuid EntityId; uint32 Life` | EntityId 为稳定逻辑主体；参赛者映射 Contender，野怪由 CampId/MemberId 映射。不是 Actor 地址、观察句柄或物品。 |
| `FLDCombatBodyRef` | `Victim; FGuid BodyIdentity` | 同一逻辑生命只允许一条当前身体绑定；重复 Register/换 Pawn 不发新血量或第二死亡权。新生命由生命域显式换代。 |
| `FLDCombatSourceRef` | `Basic/Learned/Gear`、稳定定义 ID、相关来源修订；Gear 另有 `ItemInstance` | 所有者、Rank、当前绑定由服务器解析。Q/W/1/2 只是入口；新学习/新实例不能获得旧请求执行权。 |
| `FLDCombatRequestKey` | Direct：Generation/Caller/Life/DirectSequence；Nav：Generation/Caller/Life/完整 MoveIdentity/AttackCycle | 两种固定判别形状，不按裸整数混比。Nav 形状仅由服务器当前订单产生，不让客户端伪装。 |
| `CombatToken` | 完整准入时，Combat 为该稳定请求发行的 `uint64` 权威动作序号 | 对同 Caller 本局单调且跨身体保留；重复请求返回同 Token。API 的 ActionId、内容 AttackId 指该 Token 在 Match/Caller 下的同一身份，不另存攻击序列。 |
| `EffectId / FLDCombatHitKey` | Effect 关联 Match/源主体/CombatToken/效果内序号；Hit 再含目标完整 VictimKey 和稳定子序号 | Effect 不是技能定义；Hit 去重不是死亡去重。裸 `Life=1` 在不同单位间不能误合并。 |
| `FLDDeathKey` | `LedgerMatch; FLDVictimKey Victim` | 不含 Killer、EffectId、DeathSerial 或当前时间；改变这些字段不能再奖励同一生命。 |
| `FLDCommittedDeath` | Key、VictimBody、可选 VictimContender、FatalEffectId、实际 CreditOwner 或 NoOwner、ReleasedSourceRef、SourceBodyAtRelease、RewardBasis、DeathCause、EventStamp | 首次 PendingDeath 原子锁定；Life/成长/营地消费同一值，不从 HP/LastOwner/当前 Pawn 另造事实。 |
| `RewardBasis` | RewardDefinitionId；玩家分支 VictimLevelAtDeath、VictimBuildRevisionAtDeath；`bool CreditOwnerEligibleAtDeath`、`NoRewardReason` | 等级/资格按致死真实时刻冻结；之后升级、离局或重 Prepare 不改。内部支付失败不是 NoReward。公共形状由核心唯一声明。 |
| `IssuedCooldownId` | 同 Match/Owner/SourceKind/能力继承定义域内的持久发行身份 | 同款装备最多两条、各自 ReadyAt；学习同定义一条。实例/槽/Rank 不当冷却身份，详见 §5。 |

命令前验使用必要的 Build/Inventory/Item/SourceBinding revision 与 CatalogDigest，
不能用频繁变化的 OwnerRevision 代替。Action 内保留实际来源和能力条款；
释放时重验来源/Rank/所有权，按当期合法属性取源快照。
纯显示换槽不更换来源；与该来源无关的属性变化不把旧 Action 偷换成另一技能。

`PrepareBuildChange` 还捕获 BodyRef、Attribute/Resource/CombatState revision 及旧当前值/上限。
Build 没变不代表期间未受伤、耗蓝或状态到期；这些依赖变化时必须在提交前重 Prepare/拒绝，
不能账本成功后才发现要重新算 HP。来源失效和当前控制/资源修订也不能互相冒充。

### 3.3 单一外部 API 与网络桥

以下签名属于现有 `ALDCombatDirector`；命令/结果建议统一声明于拟建
`Source\Lockdown\Public\Combat\LDCombatTypes.h`。没有返回值的网络桥另在现有 PlayerController。

```cpp
void AdvanceAuthority(const FLDMatchContext& Match, int64 ThroughMs);
void SampleBoundary(const FLDAuthorityBoundary& Boundary);

FLDCombatActionReceipt SubmitAction(
    const FLDCombatCaller& Caller, const FLDCombatActionRequest& Request);
FLDCombatCancelReceipt CancelAction(
    const FLDCombatCaller& Caller, const FLDCombatCancelRequest& Request);
FLDCombatActionReceipt QueryActionReceipt(
    const FLDCombatCaller& Caller, const FLDCombatRequestKey& Key) const;
FLDCombatAvailability QueryAvailability(
    const FLDCombatCaller& Caller, const FLDCombatSourceRef& Source) const;

FLDCombatPrepareResult PrepareBuildChange(
    const FLDCombatCaller& Caller, const FLDBuildProjectionInput& Input,
    const FLDExpectedBuildVersions& Expected) const;
void ApplyBuildChange(
    FLDPreparedCombatProjection&& Prepared, const FLDGameplayWritePermit& Permit);
FLDCampResetPrepareResult PrepareCampReset(
    const FLDCampResetInput& Input, const FLDGameplayWritePermit& Permit) const;
void ApplyCampReset(
    FLDPreparedCampReset&& Prepared, const FLDGameplayWritePermit& Permit);
FLDCombatOwnerView BuildOwnerCombatView(const FLDCombatCaller& Caller) const;

DECLARE_MULTICAST_DELEGATE_OneParam(
    FLDOnActionFinished, const FLDCombatActionOutcome&);
```

`OnActionFinished` 是上述 delegate 的实例。它只通知一次动作终态，不代表已释放弹体全部消失。
`ApplyBuildChange` 仅供已验证复合提交内部的不失败安装；不开放为 UI/AI 独立写入口。
Caller 由拥有 Controller/服务器 AI/注册野怪解析，带同一 Match/Body 上下文，不进 wire。
对无当前 Pawn 的死亡选择、既成死亡结算使用合法持久参赛上下文，不伪造 Alive Body。

网络桥拟为 `ServerSubmitCombatAction(Request)`、`ServerCancelCombatAction(Request)`、
`ClientCombatReceipt(Receipt)`，均为 `void` 的 UE RPC。
请求只带本命令所需的四类目标之一、来源/入口尝试和预期版本，不传可信 AD/Rank/HP/ReadyAt、
目标列表、客户端时钟或实际所有者。长度、编码、有限数值、协议枚举属于前闸 shape；
来源是否存在/获准、TargetMode 是否匹配该来源及当前目录条款属于失保后的语义校验。
缺来源用规范 `Unbound/EntrySlot` 激活尝试表达，不伪造一个可执行 SourceRef。
CatalogDigest 覆盖本局整套批准的公式/角色/技能/装备/政策输入，不能仅用旧 combat 子载荷摘要冒充全包一致。

采用与成长相同的 `FLDCatalogDigest { uint8 Bytes[32]; }`：SHA-256 的 32 原始字节进 wire，
日志/文档为 64 位小写 hex，不把 64 字符当 64 字节。现有 tactical wire 已采用这一字节形状，
但旧 combat/equipment/acquisition 是各自摘要，目标统一读取
`Content\Lockdown\Data\Demo\manifest.json` 的规范文件字节 SHA-256。
该根 manifest 不含自身摘要，由唯一 Demo compiler 生成并写入 `LDDemoContentContract.h` 及加载身份，
绑定完整内容；Combat/纯值 FormulaSet 不再选择旧子目录摘要或旧 catalog 作为数据入口。
证据见 [LDTacticalRequest.cpp:5–17](../../Source/Lockdown/Private/Combat/LDTacticalRequest.cpp#L5-L17)、
[CatalogTools.py:20–21](../../Scripts/CatalogTools.py#L20-L21)、
[SyncB84Data.py:82–103](../../Scripts/SyncB84Data.py#L82-L103)。
来源定义的有界名字沿现有最多 63 个 UTF-8 **字节**、64 字节零填充形状收敛；
新 decoder 严格拒绝无终止/非法 UTF-8 等坏编码，不靠截断或 FName 大小写折叠变成另一合法来源；
语法合法但未知/非规范的目录引用在失保后按 SourceUnavailable 等语义失败，不伪装 Malformed。
Unbound 由独立判别项表达，不用空名字伪装真实来源；wire 宽度/版本、FName 字典与原生 ID 一起迁移。
现有生成字节按 UTF-8 JSON 加 LF，不能擅自排序键或格式化 SourceData 后声称摘要不变。

AI 经服务器绑定上下文直接调用同一服务，不调用下层 `RequestBody/RequestContent/ApplyStatus/Die/Sample`
或假造 RPC。参赛 AI 使用同一持久参赛构筑，不落入旧身体私有 `NPCBuild` 作为第二种成长规则。
普通怪物也复用动作/伤害原语，营地只决定意图、回位与刷新，不另写平行命中/奖励。
CampReset 是世界域提供到家证据后调用的服务器内部专用操作，不是玩家 RPC 或通用满恢复布尔开关。

### 3.4 真实执行与 AI 估计共用的纯值接口

拟在现有 `Public\Combat\LDTacticalRules.h` / `Private\Combat\LDTacticalRules.cpp`
抽取以下纯函数，输入/结果类型归同一 `LDCombatTypes.h`，不另建预测伤害公式或世界扫描服务：

```cpp
namespace LDTactical
{
    FLDDamageEvaluationResult EvaluateDamage(
        const FLDDamageEvaluationInput& Input, const FLDCombatFormulaSet& Formulas);
    FLDBasicTimingResult EvaluateBasicTiming(
        const FLDBasicTimingInput& Input, const FLDCombatFormulaSet& Formulas);
}
```

| 值类型 | 最小输入/输出与使用边界 |
| --- | --- |
| `FLDCombatFormulaSet` | 对应 CatalogDigest 的已批准、编译后不可变公式参数/定义集合；不执行任意公式字符串，不持 Actor/World/Bodies 指针。 |
| `FLDDamageEvaluationInput` | 已解析伤害定义/等级参数、DamageType、HitClass、实际所需源属性、显式本次暴击结果，以及目标抗性/HP/盾/保护的值。执行器填真实合法快照；估计器填获准信息下的明确假设。 |
| `FLDDamageEvaluationResult` | 明确成功或定义/数值域错误；成功时给减伤量化量、盾吸收、实际 HP 损失、过量及结果资源。只是数值结果，不是已提交 Hit、PendingDeath 或奖励。 |
| `FLDBasicTimingInput` | 已编译单位普攻定义中的基础攻击速度/前摇，以及已解析当前攻击速度；玩家来自 `STAT.BaseAttackSpeed/UNIT.BasicAttackWindupSec`，怪物来自 `CAMP.Low/High.AttackSpeed/AttackWindupSec`，均接相同公式语义。 |
| `FLDBasicTimingResult` | 明确成功或错误，成功给 IntervalMs/WindupMs 时长；不生成绝对 NextAttackAt、ReadyAt 或权威时间。 |

P2/R4/Rate/Sec 的精度、检查和取整只取内容合同，`EvaluateDamage` 复用
`FORMULA.Crit/Resistance/Damage`，`EvaluateBasicTiming` 复用
`FORMULA.AttackIntervalMs/AttackWindupMs`。旧 Damage/间隔算法需由此替换或抽取，
不能留一套实际执行算法再为 AI 包一套近似算法。
真实 `ApplyHit/AttackStart` 调用同一纯函数后，仍须自己完成身份、几何、来源和 permit 验证及原子提交。
纯函数不查世界/营地运行表、不读时钟、不采随机数、不发事件、不分配 Token 或修改任何状态。

AI 只以自身派生属性、合法可见成员及公共模板构造保守满营地假设，
使用显式“不暴击”等估计条件，不取得权威随机样本或隐藏实际状态。
营地受击后的行动时间线、策略循环和预测后剩余 HP 门槛归 AI；缺合法输入或计算失败返回
EstimateUnavailable，不当成零风险/必胜。预测不表示实际命中、真实生存保证或实战已通过。

## 4. 属性聚合、资源及更新

### 4.1 完整属性的消费者与内容键

以下是正式键引用和单位，不是另一份数值表。具体计算/上限/精度状态在内容报告批准后进入生成输入。

| 属性/结果 | 内容输入与公式 | 实际消费者/单位 |
| --- | --- | --- |
| AD/AP | `STAT.AD/AP.Base/PerLevel`；`FORMULA.StatProjection/Damage` | 点；AD 供声明物理加成，AP 仅供明确的法术加成，不自动改普攻类型。 |
| Armor/MR | `STAT.Armor/MagicResist.Base/PerLevel`；`FORMULA.Resistance` | 抗性点；每次 Hit 取目标当前值，不能混用旧比率和新抗性点。 |
| AS/攻击间隔/前摇 | `STAT.BaseAttackSpeed/AttackSpeedPerLevelRatio`；`FORMULA.AttackSpeed/AttackIntervalMs/AttackWindupMs` | AS 为次/秒；时长输出毫秒；在合法 AttackStart 快照，不默认缩短所有能力前摇。 |
| MS | `UNIT.MoveSpeedCmPerSec` 与合法属性贡献；`FORMULA.MoveSpeed → STAT.MoveSpeedCmPerSec` | 厘米/秒；最终值接现有 CharacterMovement，导航不再计算一份速度。 |
| 攻击距离 | `UNIT.BasicAttackRangeCm`、`CHOICE.attr_reach.AttackRangeCmFlat`、装备同名贡献；`FORMULA.AttackRange → STAT.AttackRangeCm` | 厘米；脚点 XY 中心距，不增减胶囊。FindApproach、AttackStart、Release、预览同源；不等于能力 Range 或群攻 Radius。 |
| 最大 HP/MP | `STAT.MaxHP/MaxMP.Base/PerLevel`；`FORMULA.StatProjection` | P2 点；构筑属性，不是当前资源；一次最终变更调用 `FORMULA.ResourceMaxChange`。 |
| 当前 HP/MP | `FORMULA.Damage/Regen/ResourceMaxChange` 及已批准效果/成本 | Combat 唯一 P2 真值；伤害、回复、花费、比例变更、正常复活分别有明确提交原因。 |
| HP/MP 回复 | `STAT.HPRegen/MPRegen.Base/PerLevel`；`FORMULA.Regen` | 点/秒；按有效速率区间累计，不能每帧舍掉小恢复或使用新速率回算过去。 |
| 暴击率/倍率 | `STAT.CritChance.Base/STAT.CritMultiplier`；`FORMULA.Crit`、`POLICY.CritSampling` | 概率/无量纲倍率；只作用批准标签。一次 AttackId 在成功 Release 确定一次，重放不重抽。 |
| 冷却属性 | `STAT.CooldownHaste.Base`；`FORMULA.CooldownMs` | Haste 点；内容方案仅影响未来 Release，已建立 ReadyAt 不因换装/升级重算。 |

聚合单向依赖为：基础和等级成长、永久选择、已学被动、两个真实装备的普通贡献、
有效临时状态 → 来源合法性/唯一被动组 → `FORMULA.StatProjection` → 最终属性及来源分解。
当前资源、已存在的 ReadyAt 和 UI 格式化结果不反向参与投影。
首版无消费者的属性反馈环不保留；非法依赖/循环在导入或 Prepare 明确拒绝。

普通属性逐实例相加，与同名被动 once-per-family 分开。等价被动以稳定 SourceKey 选代表，
代表移除后在同次提交换成剩余来源；组触发历史不寄存在可出售的代表实例上。
不得把同一贡献既当装备普通属性又经被动再加一次。

### 4.2 一次最终投影与资源比例

`PrepareBuildChange` 在局部候选上验证完整变化、来源、数值、容量、绑定和所有依赖，
一次生成最终属性、当前资源变换、被动组和来源计划。它不调用 Changed/Sync/Delegate，
不取消动作、不扣钱/机会、不改资源/CD；全部可失败工作在任何权利提交之前完成。
同一有效 permit 的无重入提交安装构筑/库存、Combat 投影及源绑定，再发布。

最大资源按 `POLICY.ResourceMaxChange=PreserveRatio`，读取权威旧当前值、旧上限和最终新上限，
一次调用 `FORMULA.ResourceMaxChange`。不逐个拆装中间态换算，不从 HUD 百分比反算，
不把投影当敌方 Damage/普通 Heal/PrimaryBasicContact，也不触发吸血、群攻或占领清零。
两笔真实卖出/购买各做一次变换；没有其它资源事件的往返不能净获取整收益。

内容当前 P2 方案为最终一次向下量化；活体正 HP 降上限后量化为零，Prepare 返回
`NumericUnderflow`，整笔事务不改钱物/资源/机会，不抬到最小生命或制造数学死亡。
零旧上限且当前资源为零、新上限仍为零可无变化；其它非初始化零分母转换返回 `NumericDomain`
（内部原因 `UndefinedResourceRatio`）。这些精度/边界是待审内容方案，不是另一个玩法备选。
当前首版内容没有强制到期的最大资源增益；NumericUnderflow 只拒绝可拒绝的变更，
不能用 Prepare 失败保留一个依法必须到期/离局移除的来源。
若引入此类强制移除内容，必须先补齐其量化语义和验收，不以旧值 fallback 偷渡。

死亡等待选择只更新保留构筑/来源和下次生命属性，不对死身体执行比例回血；
正常复活由生命域满最新上限。Prepare 后发生伤害、耗蓝、回复、状态到期或生命变化，
其旧 Resource/CombatState revision 必须失效；绝不能用旧计划覆盖新资源。

自然回复按 `POLICY.RegenIntegration=CarrySubP2Remainder`，由同一 Combat 资源记录保存必要的有界亚 P2 积分余数及结算水位，
不是第二份 HP/MP。等长真实时间的等价分段应得到相同恢复；满资源不囤积超额恢复。
速率/来源改变前先由正常阶段以旧 rate/旧 max 结清至实际变更边界，再换速率或做一次上限投影；
余数仍是原资源点的未入账小量，不随 Max 倍乘，满资源时丢弃超额及余数。
Prepare 本身不偷偷 Tick；死亡/新生命清瞬时余数。
脱战回复读身体真实战斗事件，不借卖装/换被动代表或复用旧 `QuietForPreparation` 重置计时。
当前 `ABILITY.passive_second_wind` 方案只在成功 AttackStart、具有伤害效果的成功 Release、
承受正减伤/量化后伤害时更新 LastCombatAt；盾全吸收也算受击，不只看实际 HP 损失。
失败尝试即使已失保也不刷新；纯移动/位移、自保盾/回蓝、购物和选卡不刷新。
达到该定义的脱战期限后才启额外回复，基础回复仍按资源规则；
时钟事件先结清旧速率到实际发生时刻，再切换有效速率，不另取帧 dt 默认值。
正常复活时以该次复活的实际时刻初始化新身体的脱战时间起点，不沿用上一生命的就绪窗口；
这不修改已保留的技能/装备 cooldown 或普攻端点。

## 5. 来源、独立冷却与反套利

### 5.1 两份持久发行身份

沿现有参赛记录保存 Combat 唯一写入的 readiness，键为
`LedgerMatch + Contender + SourceKind + 能力继承定义 + IssuedCooldownId`。
条目含可空当前来源绑定、绝对 ReadyAt 和提交修订；实例只是绑定，槽位不入键。
每装备主动定义最多两条；同一可学习角色主动只需一条，遗忘不会再发第二条。
当前目录不同定义是否共用继承域必须由内容明确，不能按显示名称猜测。

同款两件绑定不同 ID；一件 Release 只更新本条 ReadyAt，不得再叠加 family-max、
全装备共享锁或把另一条期限延长。Family 是定义/继承标识，不是唯一 ReadyAt。
参赛者死亡不解绑所保留的装备/技能；两条都按同一服务器时钟继续。

获得来源的纯 Prepare 按当前同一边界选择未绑定记录：

1. 有未结束的未绑定债务时，优先 ReadyAt 最大者，平局按稳定 IssuedCooldownId。
2. 没有未绑定债务，才复用已就绪的未绑定旧记录，按稳定 ID 选择。
3. 该定义域尚无已发行记录且本次合法首次取得来源时，准备第一条无继承债的初始就绪记录。
   已有记录均合法绑定、确实增加第二件装备且未达上限时，才准备第二条；角色主动不发行第二条。
   只要存在未绑定旧条，仍先走前两步，不能把新 ItemInstance 或重学伪装为该定义首次取得。
4. 选择/发行、实例绑定、SourceBindingRevision、钱物/机会和投影一并提交；失败全不安装。

| 边界 | 结果 |
| --- | --- |
| 第一件仍持有且冷却，真实买第二件 | 第二件有独立就绪条，可按资源/动作条件使用；第一件期限不变。 |
| 卖冷却件只买回一件 | 必须承接未绑定债务，不能另发空白第二条。 |
| 两件不同截止均卖出再买两件 | 先回绑较晚期限，再回绑另一条；分别保留原截止，不合并、不清零。 |
| 未绑定记录一条冷却、一条已就绪 | 先回绑冷却条；不能用发行顺序/FIFO 先挑就绪条，把刚卖的债留在背后。 |
| 一条仍绑定，另一就绪条卖出重买 | 可复用该未绑定就绪条；不拖慢仍绑定条，也不生成第三条。 |
| 换槽/升级/遗忘重学/死亡/窗口重开 | 保持 CD；重学等级从一级，但回绑原结束时间。 |
| Prepare 后先 Release、两购买抢同一条 | 最终重新核验 readiness/SourceBinding revision；不得用旧 ReadyAt 覆盖新债、双绑或部分扣钱。 |

固定目录下每定义保留至多两条到新局即可有界，不需要随到期删除再发新身份。
时间自然流逝不按 tick 推进 revision；回绑预览跨边界时仍要重新检查选择，不凭旧版本保证时钟未变。
来源丢失继续计时，不存另一份冻结 Remaining；已释放效果后来命中/到期也不再次写 CD。
买卖自身报价/回执应说明继承的可用期限，但不向对手公开发行身份。

### 5.2 持续贡献与已释放快照

未 Release 的 Action 必须有当前合法生命、实际来源及能力条款；死亡、出售、遗忘或来源失效即取消。
已 Release 的独立 Effect 保存源所有者、SourceBodyAtRelease、SourceRef、Rank、CatalogDigest、
实际用到的 AD/AP/暴击及有限被动资格、空间数据与结束条件；不靠 Pawn 存活或当前库存续权。
释放者死亡/重生、卖出、遗忘不改快照、期限或预算。

持续属性、未来被动注册和需要维持条件的作用在条件失效时停止，不因有 EffectId 就变独立。
旧攻击已承诺的有限子命中不是继续保留一个永久被动。
目标生命始终严格：self 盾/控制等目标就是已死亡的 Body 时应清除，不能转到复活身体；
允许旧来源继续不是允许旧目标 Hit 改绑新 Life。

离局按持久 OwnerContender 找齐并取消未结 Effect/目标状态/维持贡献，立即移除身体。
此前已损失的 HP、已经消耗的盾、过去控制时段和锁定死亡事实不回滚；
之后不能再由旧回调生成新 Hit、aura 或新死亡。

## 6. 从意图到释放：命令、回执与控制

### 6.1 收件域、取消与阶段

DirectSequence 由同一输入源供 Q/W/1/2 共用；AI 的直接意图同样走服务器绑定的 Direct 域。
Nav 用当前 MoveIdentity+AttackCycle，不能自增 Controller 的 Direct 序列。
完整准入才分配权威 CombatToken；原请求键、动作身份和导航订单各有职责。

`FLDCombatActionReceipt` 至少带 RequestKey、可空 CombatToken、ReceiptRevision、
阶段/拒绝码、相关自身版本及 `FLDProtectionReceipt`。阶段为 Queued、ActionAccepted、
ReleaseCommitted、Finished 或 Rejected；收件不是接受，接受不是支付，支付不是命中。
同一原记录按阶段推进，终态不可改；Query/重复请求返回其已知进展，不重执行。

Direct 拒旧水位、Nav 当前订单/周期水位、取消水位和回执发布游标分开。
同 key 同条款只重放；保留窗内异条款 `RequestConflict`；过窗 `ReceiptExpired/StaleRequest`
并保留拒旧水位，不能成功形状返回或换一个 ID 自动重施。
较新无效请求不撤销已接受旧订单，但更老尚未执行 Pending 不能迟到激活。
活跃请求/Action 不在回执压缩前丢失；旧 Life/Match 另由关闭代次拒绝。

取消可指原 RequestKey、ThroughDirectSequence 或已知 CombatToken，并关联需要停止的 MoveIdentity。
回包没到也可取消原请求；不能将 ThroughDirectSequence 与服务器分配的 CombatToken 数值比较，
不能用旧 Through 直接取消当前更晚动作。S/新订单只停止可中断工作，不回收已释放效果或退款。

### 6.2 进攻意图先解除保护

唯一生命入口由核心维护：

```cpp
FLDProtectionReceipt EndProtectionForAggressiveIntent(
    AController& Caller, const FLDMatchContext& Match,
    const FLDCombatBodyRef& Body, const FLDAggressiveIntent& Intent,
    const FLDGameplayWritePermit& Permit);
```

Intent.Kind 为 AttackUnit/AttackMove/BasicAttack/ActivateCharacter/ActivateEquipment；
`FLDAggressionRequestKey` 含 Origin、Sequence、CancelEpoch。
Navigation 使用原 Order/Epoch；Combat 使用 **DirectSequence**，CancelEpoch 固定零，
不能使用此时尚未分配的权威 CombatToken。派生 RepeatAttack 沿原导航意图，不另发行失保序列。
域先 shape/freshness/冲突检查，核心再验 Caller 登记、当前 Match/Body/Life/Alive 和 permit，
由 Life 自己按当前 Actions 的真实时间发布一次资格变化；调用者不填时间。

前闸通过后先提交失保，再查 source、target、距离、MP/CD、控制、路径和实际动作准入。
空技能、无装备主动和规范的过期绑定是游戏性失败，不伪装为畸形包。
所有角色/装备主动含自身守护/回复均适用；普通移动、Stop、商店和本地未确认 Armed 不适用。
客户端不能用本地 MP/CD/目标预测静默吞掉已确认/直接激活尝试，从而绕过失保。

Rejected 也可能 `ProtectionRemoved=true`，附 LifeRevision/资格 EventStamp；
动作失败不扣 MP、不设能力 CD、不开始普攻节拍，但已解除的保护不恢复。
旧 match/life、被取消/重放/异条款请求、非法枚举/NaN/坏 shape 在前闸拒绝，不改新生命。
在圈内失败的规范进攻仍可能因失保改变占领资格，不能等待动作成功才通知 Capture。

### 6.3 动作准入、时序和支付

Combat 完成无副作用准备后，与 Navigation 的
`CommitActionHandoff(Permit, Caller, ProposedMoveIdentity, ActionId) → FLDOrderCommitResult`
在同一不可重入准入中提交。Handoff 拒绝必须双方未接受；成功后 Combat 安装不得再有可失败分配。
Queued 不停旧路；实际动作准入失败不替换旧单，但不回滚已经提交的失保。

普攻合法 AttackStart 固定本次前摇及最早 NextAttackAt；取消/移动/改 AS 不提前该期限，
AS 仅影响下一次新 AttackStart。伤害源属性到成功 Release 再快照。
参赛者节拍跨死亡按时间继续，不能因新 Body 默认零而洗掉；怪物新生命按营地初始化。

能力 Release 再验当前源/生命、取消、条款、目标样本、控制、MP/CD和几何；
所有效果/状态容量预分配且无旧状态破坏后，验证 permit，一次提交 MP、本条 ReadyAt、Effect 和回执。
不得先扣费再生成，亦不能 staging 就销毁旧盾/场/状态。
释放前失败不付能力成本；释放后的 Miss/阻挡/目标死亡不是退款理由。
已接受新动作后来 Release 失败不恢复历史订单。

`OnActionFinished` 仅结束当次执行/前摇/恢复状态，带同 RequestKey、CombatToken、
ActionRevision、是否已释放、原因及 MoveIdentity/AttackCycle。
Nav 的 `OnCombatOutcome` 只更新当前订单或登记下一意图，不在 Effects/Publish 回调中递归 Release。
下一普攻仍由合法 Actions 阶段重新验证；没有第二条回调驱动战斗循环。

### 6.4 首版控制与拒绝

首版内容提案需要 Root、屏障及 Root 净化；不是保留旧状态全集的授权。

| 状态 | 移动/追击推进 | 原地合法普攻 | 非位移 Q/W、1/2 | 位移主动 | 被动/自然回复 |
| --- | --- | --- | --- | --- | --- |
| 正常 | 允许 | 允许 | 允许 | 合法路径/落点下允许 | 按内容条件 |
| Root | 禁止 | 允许 | 允许，包括自身盾+净化 Root | 禁止，不因来源是装备绕过 | 不因 Root 一律关闭 |
| PendingDeath/Dead | 禁止新动作 | 禁止 | 禁止 | 禁止 | 不回血治活；保留构筑不等于活体持续作用仍运行 |
| 复活保护 | 普通移动允许，不争圈 | 规范进攻意图先失保，再验动作 | 同左 | 同左，解除后仍查 Root | 自动被动/旧效果不是新生命主动进攻意图 |
| Left/Closed | 禁止 | 禁止 | 禁止 | 禁止 | 取消未结影响；清理不重新提交 |

按当前内容方案，Root 以 effect/cast+完整目标生命保存独立实例和原期限，任一未到期实例即生效，
显示最大剩余时间；同实例重发不续期，一个较早到期不能清掉另一来源。
守护净化删除当时全部 Root，不清其它状态/CD，不带后续免疫；之后按权威次序新施加仍可再次 Root。

`EFFECT.ShieldPolicy=ReplaceIfNotWeaker` 比较旧盾的**当前剩余量**：
新量不少于剩余量时，用新量、新 source context、新截止完整替换，相等也刷新；
较弱则保留旧量/旧源/旧期限。弱盾抑制是明示的子效果结果，不是整次释放失败，
组合 Root 净化或电池回蓝仍正常执行，合法 Release 仍支付 MP/CD。
后续 Max/装备/AP 变化不重算已快照盾，不能继承旧 helper 的严格大于判定或旧盾上限。
这些逐定义语义来自内容单点的当前待审方案，不在这里维护另一组参数。
未选 Stun/Silence/Mute/Disarm/免死/反射等随 C1 真实删除，不保留禁用生产者或空 handler。

公开拒绝统一映射：身份/协议与 StaleMatch/Life/Request、RequestConflict/ReceiptExpired、
StaleBuild/Inventory/Item/SourceBinding/Catalog、SourceUnavailable/NoActiveEffect、
Busy/Cooldown/AttackNotReady/InsufficientMana/ControlBlocked、TargetUnavailable、
InvalidGround/Direction/OutOfRange/Blocked、Dead/MatchClosed、NumericUnderflow/NumericDomain、
AuthorityDataUnavailable/CapacityUnavailable。
隐藏、撤销、过期目标统一 TargetUnavailable；内部详细因果仅供服务器诊断。
分配/算术/缺边界证据不是正常 Miss，不能用零伤害或默认坐标吞掉。

## 7. 命中、有限被动与致死交接

### 7.1 观察锁定和权威接触

世界域唯一提供 `ResolveObservedUnit/TraceSight/FindApproach/QueryCombatContacts`。
单位新锁定、每次追击、AttackStart 与需要单位许可的 Release 均重新解析观察，
样本含 Victim/Body、FootCm、Shape、WorldRevision、ViewGeneration/ViewRevision，
只在当前提交阶段有效。失视野取消该锁定并停该追击，不改走最后坐标、不自动重现续单。
Ground 载荷沿世界/操作域携带 `SurfaceHint:FName`，不设默认支持面；
None/未知/错支持面/NotGrounded 是世界语义失败，不因客户端预判而吞掉明确进攻确认，
也不提升成 Malformed 绕过失保。只在统一世界查询成功后获得可执行的地面样本。

`QueryCombatContacts(AuthorityEffectContext, BoundedShape, WorldRevision)` 只允许注册的、
已接受 Effect 在权威内部调用，读取**同一** Combat Bodies；世界负责静态地形/层面/LOS 裁剪。
合法方向效果或普攻群攻可以接触隐藏身体，但结果不授予新的观察句柄，不交 UI/AI/Nav。
方向效果在真实首次接触时取得目标生命；已经绑定的旧目标 Hit 不得换成该实体的新 Life。
容量不足/缺真实 pose 明确失败，不能无声明截断为“最近若干目标”或假 Miss。

### 7.2 一次真实 Hit

`ApplyHit` 为现有 Combat 的内部唯一资源入口：验证 Match/permit、Effect/接触键、目标生命、
存活/关系、当前保护、几何，再按 `FORMULA.Damage` 计算全部后果。
输出明确区分减伤/量化后命中量、屏障吸收、实际 HP 损失、过量、控制应用及第一次致零。
不能拿旧 contact 当吸血量；保护免伤免控必须在命中点统一检查，不能仅相信接受时保护状态。

源输入/有限触发资格使用 Release 快照，目标抗性/保护/屏障/HP 使用当次已提交真值。
每个 Hit 的容量及数值失败均先于写入。实际损失只算未被盾吸收且不超过当时 HP 的部分。
第一次 HP 到零立即 PendingDeath，不留同批吸血/回复/盾把它治活的窗口；
其后 Hit 不夺取致死来源或新增该生命死亡。

一次允许暴击的 Basic 只在成功 Release 采样一次，绑定 AttackId，取消前摇不能提前预览随机结果。
被动去重键含完整 Match、AttackId、VictimKey 和 PassiveFamily，代表更换/重复实例不重复触发。
吸血只按 `POLICY.LifestealBasis=PrimaryBasicAttackHPLost`，盾、过量、副击均不计；
治疗指向原释放身体生命，不能治疗该所有者的新复活身体或 PendingDeath。
这些具体触发规则与数值仍属内容提案，批准后用一条 Damage/Heal 通路落实。

### 7.3 可获得射程与普攻群攻

内容当前提案为 `CHOICE.attr_reach` 及 `ITEM.item_echo_maul` 的
`Stats.AD/AttackRangeCmFlat`、`Passive.Family=passive_cleave`、
`Trigger=PrimaryBasicContact/CleaveRatio/RadiusCm`，只引用这些键，不在本文复制倍率或半径。
扩大最终 Range 不隐式设置旧 Gear.Ranged 或创建追踪弹体。

同一普攻只有一个 AttackId、一次节拍、一次暴击采样。分类固定为 BasicPrimary/BasicSecondary：
成功主击且减伤/量化后命中量为正才产生一轮次击；盾全吸收仍可触发，Miss/保护/量化为零不触发。
保存主击当时脚点作为圆形余波锚点，不能在主目标死亡销毁后重新读 Actor 位置。
排主目标、自身、非战斗、死亡/保护身体，从锚点逐副目标做世界 LOS。

每个次击独立 VictimKey/HitKey，读取当前目标防御，沿同一物理 Damage 和 DeathKey 通路。
所有次击共用该普攻源 AD 快照，不因首个击杀奖励升级改变伤害。
两同款锤普通属性相加，但 family 只一轮；每 `(AttackId, 完整 VictimKey, passive_cleave)` 最多一击。
按 `POLICY.BasicSecondaryTriggers=Disabled`，副击不暴击、不 on-hit/吸血/再群攻、
不创建新 Attack 或重抽。不是对 N 个目标调用 N 次 RequestBody，也不是保留旧反应体系。
该限制只禁止攻击者的追加 proc，不是 `SuppressAllTriggers`：
副击仍发布固有的受击/HP 变化、受害方 LastCombatAt、死亡/奖励及营地有效受击反击事实。

### 7.4 死亡事实、奖励基准和失败

Combat 首次 PendingDeath 同次冻结 `FLDCommittedDeath`：
真正致死 Effect/Source/Owner、稳定受害生命、目录和 RewardBasis。
无主不能回退 LastOwner；无奖也有终态事实。自杀判断用稳定 Owner/Entity，不仅比较失效 Pawn。

**每个 Effects 致死集合先锁完全部事实及 RewardBasis，再在 DeathsAndRewards 逐项支付。**
互杀中第一笔 XP 使某受害者升级，另一笔仍读其 VictimLevelAtDeath；
该 BuildRevision 是冻结证据，不要求支付时仍等于当前版本。
Transactions 后明确因果子批有自己的“先锁完再奖励”集合，不要求预知未来因果死亡。

Life 的 `EndContenderLife(Fact, Permit)` 消费同 DeathKey 并发布一次资格变化，Combat 不再另发 Died。
成长 sink 为精简后的
`ALDEconomyDirector::CommitDeathReward(const FLDCommittedDeath&, const FLDGameplayWritePermit&)
→ FLDRewardReceipt`。
其 Prepare 预计算 Gold/XP/等级/机会、必要当前候选、Ledger delta/回执、Combat 投影及全部容量。
同一有效 permit 下 Ledger 与不失败的成长安装之间不得回调、发布或重入；
若做不到，不得声称复合原子。Ledger 本身只保证其钱/物范围。

信用资格按原始 OccurredAtSeconds 冻结：死亡但仍在局 Owner eligible；Hit 致死先于 Leave，
后来离局/重 Prepare 不得改 NoReward；Leave 先则取消效果，不能产生新的致死。
必要旧参赛/账户记录仅服务已锁事实结算，不恢复在局、不生成身体/被动/新命令权限。
任何补处理仍需当前 permit，不能越过终局。

必需奖励准备失败先阻断边界/水位，保留原事实，不立即调用终止入口。
首次明确 `RetryableNotCommitted` 且完全未提交时，先释放旧 permit 和全部预留，
在仍开放的同一边界内仅受控重 Prepare 一次，再按核心规则取得新有效许可。
任何已部分提交的结果都不能进入这个重试分支，不能重新发已付金币或伪称什么都未发生。

首次非可重试失败，或这唯一一次重 Prepare 仍失败时，**才**调用核心
`FailAuthorityBoundary(Boundary, Fault) → FLDFaultReceipt`。
该调用是一次终止关闭：明确 Faulted/实际未支付状态并反馈退出，不造 NoReward、普通胜利结果或 fence；
返回后不再 Prepare/Apply、补款或继续 Capture，也不覆盖已经合法锁定的胜者。
死亡本身不回滚成活人，错误不能成为半奖成功。

### 7.5 Returning 与 CampReset

Returning 不产生无敌、不可选中或“离局”资格；途中 Hit/Root/死亡仍走同一 Combat，
但世界策略不恢复追击。内部 `ApplyHit` 成功提交后才产生拟建 `FLDCombatHostileContact`，
含 HitKey、VictimBody、ReleasedSourceRef、SourceBodyAtRelease、可空实际信用所有者、
EventStamp 及已提交敌对接触/控制结果；内容必须将该实际作用判为敌对。
世界的拟建 `OnCombatHostileContact(Fact, Permit)` 幂等消费此事实以产生同营地反击意图，
不再伤害或发奖；无效攻击意图、Miss/TargetUnavailable 不触发野怪反击。
这与玩家进攻意图即解除保护是两种不同事实。
同营地广播来源身份/因果，不广播隐藏坐标或权威 Actor 给成员；
各成员独立观察和来源生命重查，不把旧来源的重生身体自动改绑为报复目标。

`FLDCampResetInput` 由 World 构造：CampId/MemberId、当前 BodyRef、Returning/真实 HomeProof、
`uint64 ReturnEpoch`、世界/成员/资源/状态修订；不能由客户端或普通参赛者构造。
重置键为 `LedgerMatch + 完整 VictimKey + ReturnEpoch`，World 持有返回代次与已重置水位。
Prepare 先查同键既有结果，已提交只重放原回执，不再次 Apply；
ReturnEpoch 只在进入新的 Returning 周期时递增；周期中受击只更新 Resource/CombatState 修订、
使旧 Prepared 失效，不重启返回、不增 ReturnEpoch、不清幂等水位。
成功重置后的新遭遇若再次进入 Returning 才使用新 epoch；旧到家回调不能再补满资源或刷新技能。
`PrepareCampReset` 在 RespawnsAndWorld 阶段、当期 Effects/DeathsAndRewards 完成后，
检查真实已到家、仍为该存活成员、HP 正且非 PendingDeath、资源/状态修订及 permit。
失败不补满；更早或同刻已致死者必须走原 DeathKey，不能被到家覆盖。

Prepare 只在局部候选中移除应清的临时状态，再投影重置后的最终 MaxHP/MaxMP，
最后按这两个最终上限准备满资源结果；不先补旧 Max 再清状态，不在纯 Prepare 中改活体。
`ApplyCampReset` 与世界成员安装在同一无重入提交中：一次安装已准备的最终状态/上限/满 HP/MP、
清仇恨并使该怪技能就绪，返回营地等待受击策略。既不超出移除临时增益后的新上限，
也不在临时上限压低效果被移除后只恢复到旧上限。
不改 EntityId/BodyIdentity/Life，不发行新成员，不产生死亡/XP/金币，不清 Hit/请求/死亡拒重水位。
这是野怪已确认的到家重置，不是角色比例变换、正常复活或玩家 IssuedCooldownId 洗冷却权限。
新输入错误或失败必须明确，不能将非法玩家 reset 当普通成功。

已清 Root/盾等目标状态的旧到期回调须按原 EffectId 与目标生命核对，不得重新生效，
也不能仅按状态类型删除后来施加的新实例；但不因同 Life 的到家就给所有外部独立弹体
制造一次目标死亡或无敌。到家前尚未实际命中的独立效果，之后仍以同一生命和当次真值正常校验；
已消费的旧 Hit 不能重放。野怪已释放效果也不凭重置重发预算或重用 Action/Effect 身份。

## 8. 真实时间、确定顺序与水位

只有 Combat 的 `AdvanceAuthority(Match, ThroughMs)/SampleBoundary(Boundary)` 推进权威批次。
GameMode 只拥有同一游戏模拟时钟、`BeginAuthorityStep/ValidateMotionStep/CompleteBodyMotion`、
phase permit、生命与结果；不再定义另一个 AdvanceAuthority 或平台墙钟。
旧回拨 `UWorld.TimeSeconds` 和用最终姿态补过去的路径必须删除。

| 核心输入 | 正式含义 |
| --- | --- |
| `FLDAuthorityStep` | Match、StepSerial、double StartServerSeconds/EndServerSeconds；运动保留真实分段。 |
| `FLDAuthorityEventStamp` | Generation、Provider、StreamId、ProducerSequence、double OccurredAtSeconds、OccurredAtMs、RecordedAtMs、CommitOrdinal。 |
| `FLDAuthorityBoundary` | Match、BatchSerial、精确整数 AtMs。 |
| `FLDProviderFence` | Generation、Provider、StreamId、ThroughMs、ThroughSequence；证明该流闭合到真实边界。 |
| `FLDGameplayWritePermit` | Boundary、Phase、GateRevision；最终写入须 `ValidateWritePermit`，早期 open bool 不代替许可。 |

先按真实 OccurredAtSeconds 排因果，真正同刻才用 phase、父因果及稳定 ordinal/子序号。
这个次序由唯一调度在生产提交时确定并赋 CommitOrdinal；Capture/其它消费者只消费已生成次序，
不能各自按 Provider/Phase 再排序一次。
事件毫秒是 checked ceil 桶，不得把同桶的较晚 Leave 排到较早 Hit 前面。
DueAt 是计划期限，RecordedAt 是处理记录时间，均不冒充实际发生时间；
缺当时 pose/接触证据必须阻断，不以当前坐标回填或默认 Miss。

新 CD/攻击截止使用原始开始/释放时刻向上到毫秒，加内容向上时长；
ready 判断使用实际模拟 now 或安全向下的完整毫秒，不用事件 ceil 桶提前开放。
已有整数期限不经 double 往返转换；任何加法/转换均检查定义域和溢出。
UI 同步时钟只作倒计时，不提交释放/复活/胜利。

| 同一真实边界的阶段 | 工作与闭合要求 |
| --- | --- |
| Session | 离开/HostEnd、保护自然到期、开圈条件；保护到期 t 在 t 命中前生效。更早真实 Hit 不因同毫秒桶被较晚 Leave 推翻。 |
| Motion | 原生运动/碰撞分段及存在事件；初次仅 CompleteBodyMotion，不提前封 fence。到期 Root 等先提交，再推进其后的原生段。 |
| Actions | 身份/新鲜性前闸、规范意图失保、实际订单/动作原子准入、AttackStart 和到期 Release。 |
| Effects | 有序接触/命中/状态/恢复；统一保护闸，首次致零立即锁存。 |
| DeathsAndRewards | 当前致死集合先全锁 RewardBasis；Life 消费同 Key，成长复合提交；必需故障不能带着未结工作判胜。 |
| RespawnsAndWorld | 合法复活、保护与营地生命周期；不重新授予保留构筑，回位不算死亡。 |
| Transactions | 当前合法选择/买卖及最终投影；如产生明确同刻 Combat 因果，Capture 前排空 Effects→DeathsAndRewards/生命投影子批，不重开任意 Actions。 |
| Capture 前 Sealing | 把 Actions/Effects/复活/事务派生的位移、生命和事件全部纳入后，闭合精确整数边界与流序号。 |
| Capture | 只消费 fence 齐备的真实中间进出/资格事件，重查连续独占；受伤/圈内 Root 不造清零事件。 |
| Close → Publish | 先 TryCloseMatch 原子锁结果/闸，再 CloseCombat 清未结动作/效果，最后发布获准状态/回执；清理不再制造玩法写入。 |

`PublishCapturePresence/PublishCaptureEligibility` 是单圈的窄接口，不新增通用 eventbus。
死亡/保护资格只由 Life 发，位移存在事件由真实 Motion/World 发。
提供者完整发布、消费方确认水位后才释放记录；重复/旧局/旧生命/倒退序号不能再次作用。
完成到 `1.0002 s` 不能封 `1001 ms`；先 CompleteBodyMotion 也不证明后续位移已齐。
缺 provider 或未结奖励不能靠最终位置快照、空列表或假 fence 变成成功。

## 9. 全生命周期矩阵

| 事件 | 构筑/当前资源/持续贡献 | 未释放动作/订单 | 已释放独立效果 | 冷却/死亡奖励 |
| --- | --- | --- | --- | --- |
| 本地预览取消、前闸拒绝 | 不改保护和资源 | 不替换当前单；旧请求按水位拒绝 | 无本次效果 | 不写 CD/节拍或奖励 |
| 规范意图后游戏拒绝 | 保护已按 Life 回执解除；其余不因动作失败改变 | 未准入不替换旧单 | 无本次效果 | 不扣 MP/不写 CD/节拍；失保不回滚 |
| S/合法新单、动作取消 | 不撤回已真实发生状态 | 取消适用当前工作，不恢复历史单 | 不因 UI 取消回收/退款 | 保留已承诺节拍和成本 |
| 换槽/升级 | 一次最终投影；来源不凭图标位置变化 | 验实际源条款，不能执行新槽替代旧技能 | 保持旧源快照/Rank/预算 | 旧 ReadyAt 不重算；新 Release 才用新输入 |
| 替换/遗忘角色主动 | 遗忘旧等级及持续贡献；新学/机会同次提交 | 旧来源未 Release 失效 | 继续原期限，不保留未来被动/维持权 | 原发行记录保留；重学一级沿原结束，机会不返 |
| 出售装备 | 同次解绑/移除普通与持续贡献，最终资源比例换算；失败整笔不改 | 旧实例未 Release 失效 | 继续原期限，不追改快照 | 只解绑不清债；卖价/实例/来源同一事务 |
| 同款重购/双持 | 两实例属性相加，被动一组 | 新实例用新源资格/修订，不接旧请求 | 不复制旧 Effect/预算 | 两独立发行记录，债优先回绑、无第三条 |
| 来源死亡 | 财产/构筑/候选保留，活体持续作用停止；清旧控制 | 旧生命未释放及追击/交互取消 | 独立效果继续；SourceLife 仅因果，不改成新身体 | CD/普攻期限继续；在局死者有效击杀仍获奖 |
| 死亡等待选择 | 一次更新保留构筑/机会；不治疗/复活 | 不发身体动作，不恢复旧单 | 不从新学能力自动生效果 | 不刷新来源 CD；保留旧候选，不重抽 |
| 目标死亡 | PendingDeath 不被回复救活；清目标生命临时状态 | 旧目标工作失效 | 旧 TargetLife 不重绑；self 盾/控制不跨新 Life | 同生命仅一个 DeathKey |
| 复活 | 新 Life 满最新 HP/MP，清旧临时控制，建立保护 | 旧 life 请求/回执无执行权 | 允许旧来源效果继续，但不能命中被旧 Hit 绑定的新生命 | 不再发初始奖；已有 CD/节拍按时间 |
| 离局/断线 | 立即撤参赛资格、移 Body/持续作用，不转 AI | 取消动作/交互，拒新命令 | 立即取消未结影响，不继续干预战斗 | 不是击杀；此前事实按冻结资格结算且仍受 permit |
| 野怪 Returning | 仍可受伤/控制/死亡，不因回位撤销可攻击资格 | 不回头追击，世界只维护合法回家意图 | 正常同生命 Hit/控制仍有效 | 不因开始回位重置技能、发奖或换 Life |
| 存活野怪到家 CampReset | 局部候选先清临时状态、投影最终Max，再原子装满血蓝/清仇恨；同刻PendingDeath优先 | 回等待受击策略，不恢复旧追击 | 旧状态回调不复生/不误删新实例，合法在途独立Hit仍按同生命验证 | 技能就绪但不改身份/拒重水位，不复活死成员、不发奖；不是玩家CD权限 |
| 终局/Host 结束 | 先冻结写入，再清理 | 拒绝新受理/提交，终止未决工作 | 不再伤害/治疗/触发 | 已提交只读回执可重放，关闸后不补发奖励 |
| 新局 | 新 Generation+LedgerMatch；重建局内构筑/资源/装备/机会 | 旧请求/取消/订单全部拒绝 | 旧效果/状态/观察执行权清空 | 新 CD/Death/事务域；仅本地名称及设置保留 |

## 10. 自身、观察与表现权限

`FLDCombatOwnerView` 嵌入现有 OwnerState 的单一发布通路，不另广播可写副本。
Envelope 包含 Generation、Contender、Body/Life、OwnerRevision、Build/Inventory/SourceBinding/
Attribute/Resource/CombatState revision、CatalogDigest、AsOfMs；消费者先拒旧再替换完整快照。

| 自身 DTO | 字段与消费者 |
| --- | --- |
| Resources | 当前/最大 HP、MP、Shield 的 P2 数值；UI 只格式化，AI 只读自身。 |
| `FLDAttributeBreakdown` | AttributeId、Unit、Base、LevelGrowth、PermanentChoice、EquipmentFlat、PassiveContribution、TemporaryContribution、PercentContribution、Final、FormulaId、ClampApplied。解释结果来自同一投影，不在 UI 重算。 |
| `FLDCombatBindingView` | Q/W 与 1/2 分栏；真实 SourceRef、Rank/装备非角色等级说明、HasActive、IssuedCooldownId、ReadyAt、SourceBindingRevision、可用性原因。两个同款各显示自己的期限。 |
| 状态/动作 | StatusFamily、ExpiresAt、ControlBlocks、IsPassive、合法自身 SourceCount/来源解释、CurrentAction/ActionRevision、NextAttackAt；Alive/ProtectionUntil/LifeRevision 来自生命域。 |
| 回执 | 原 RequestKey、阶段/终态、ProtectionRemoved、权威版本和自身可理解拒绝；无效动作也可已有合法失保。 |

自己受到的状态可解释类型/时限，不借状态来源字段泄露隐藏施加者的位置或构筑。
对手投影只由 World 的 observer DTO 维护：当前合法可见时的名称/等级/HP/MP/状态/装备定义与槽位，
没有 ItemInstance、IssuedCooldownId、SourceBindingRevision、技能/CD/完整属性/金币。
“状态”只取该白名单批准的战斗状态输出，不复用 owner 的来源解释、技能 SourceRef 或已学被动名单。
这些字段获准可见不等于全场公开；失权立即清面板、标记、目标关联及附着表现，旧包不能恢复。

Normal Listen Host 持有权威 Actor 也必须经同一呈现授权，覆盖 Mesh/阴影/名字/装备、
盾/Root/弹体/命中特效、灯光和声音。保留服务器身体碰撞，不以销毁权威对象遮雾；
原生与观测代理不得双重输出。删除 Footstep/Trail 的生产、授权、发布、proxy/HUD 和 wire 例外。
合法隐藏次击结果不能整包发给攻击者，不能自动成为 AI/Nav 新目标。
公共圈和 PvP 击杀通知只带批准身份/原因，不附隐藏坐标或构筑；普通打野不刷全场横幅。

## 11. 精确复用、改造、新增与删除

下表细化架构 R/D 与本模块直接相关的依赖，不复制整体架构章。
路径省略根的 Public/Private 均为 `Source\Lockdown`；所有删除都是后续实施，不是本次操作。

### 11.1 复用与新增落点

| 映射/方式 | 精确落点与保留能力 | 改造/新增及退出条件 |
| --- | --- | --- |
| R01/R02/R05 原样局部机制 | `Character\LDCharacterMovementComponent.cpp` 的 saved move/网络身份；`Framework\LDCommandLiveness.h::Fresh`、`Core\LDRules.h::FViewCursor` | 保留预测/拒旧，不附带 sprint、道路、B91 分派；运动上限消费唯一属性与控制。 |
| R06/R07 改造 | `Framework\LDGameMode.{h,cpp}` 的 StateFor/RefreshBuild/SyncOwner/EndContenderLife；`Combat\LDCombatDirector.{h,cpp}` 的 Register/Queue/Sample/Die/Sync | 单一注册/资源/死亡事实、目标签名/阶段/permit；删除直接 World 推奖和旧损失，不再返回可随意覆盖的全状态副本。 |
| R07 抽取 | `LDTacticalRules.cpp` 的动作禁止/状态到期、`LDTacticalExecution.cpp` 的必要命中和释放前验证 | 仅保留批准 Root/盾/净化/伤害/投射物/位移原语；公式/状态全集/旧取消恢复/旧盾上限不原样带入。 |
| R07/R09 改造 | `LDContentActions/Release/Lifecycle.cpp`、`LDV08Loadout.cpp`、`LDV08Modifiers.cpp` | Source/阶段重验、有限 proc 去重、双独立 CD；删除 family-max 和固定 bit 表。现有低层入口内部化，无旧公共旁路。 |
| R08 抽取 | `Economy\LDEconomyLedger.{h,cpp}` 的同条款幂等、溢出/版本拒绝、全验证后写、Reconcile | 金币仍同账本；成长域补复合死亡提交，不能把 XP 放第二金币真相。 |
| R10/R11 改造/抽取 | `World\LDWorldDirector.{h,cpp}` 的注册/目标/回位；`CharacterMovement` 的 RootMotionSource、`LDTacticalFields.cpp` swept capsule、`LDContentGeometry.cpp` 合法落点 | 怪物执行接同 Combat，世界负责真正几何/生命映射；位移不是寻路瞬移替代，默认旧距离/地图分支删除。 |
| R12/R13 改造 | `Vision\LDVisionDirector/LDWorldViewRelay/LDViewTypes`、`Framework\LDPlayerController`、`UI\LDV08HUDModel` | 当前 owner/observer 白名单、typed 结果、正常 Host 附件过滤；保持 cursor/Conceal 不变量。 |
| N03/N04/N05 必要新增 | 拟 `Public\Combat\LDCombatTypes.h`、`Public\Combat\LDCombatAttributes.h`、`Private\Combat\LDCombatAttributes.cpp` | 放置本文窄 DTO/纯规则和检查结果；执行留现有 Director/拆分 cpp，不加第二 Director、GAS 状态层或通用事件平台。 |
| N02/N09 配套 | 现有 CombatTests/TacticalTests/LoadoutTests/ViewTests、相关 runner；旧 compiler/importer 的必要严格校验机制 | 机制抽取到内容域唯一 Demo compiler/`ULDDemoContentCatalog`，补 typed effects/rank/MP/属性读取及下节断言；旧生成/加载入口退出，不靠类型表宣称效果已接通。 |

### 11.2 确定删除与 C1 条件删除

| 架构项 | 本模块精确删除面 | 先替代/断开及联动 |
| --- | --- | --- |
| D07 | `Development\LDWorldChecks.{h,cpp}`、`Region\LDRegionDirector.{h,cpp}` 的旧任务链及 Resident/WildSentinel/WatchControl；`LDSkillExecution.cpp` 中 SkillTasks/ReleasedSkills/职业操作 | 必要注册/统一野怪和 AI 接回 World/Combat，再断生产者、TFunction 捕获、include/friend/目标 kind/取消和 UI。不能删调用后留空旧接口。 |
| D10/D12 | `Private\World\LDWorldClock.cpp` 旧调度政策；`Private\Combat\LDCombatCheckpoint.cpp::EnvironmentSettled` 及 Director 声明 | 先接统一 step/事件/permit；断 WorldCheckpoint 的 CheckpointSettled、console/自动恢复/probe/runner。保留本地 ControlSettings.Save，非当前局恢复。 |
| D14 确定项 | `LDCombatDirector.cpp` 的暗杀/quiet/sprint入口；`LDSkillExecution.cpp` 旧职业/服务分支；`LDContentLifecycle.cpp` 的 Scramble、自动 Footstep/Mark Trail | 同时删除 AssassinReady/Quiet/Watch/Salve 等失去消费者字段、旧 cue 和授权例外，不只停按钮。 |
| D14 C1 项 | `LDContentActions/Release/Lifecycle/Geometry.cpp`、`LDTacticalExecution/Fields/Geometry/Rules.cpp`、`LDV08Catalog/Modifiers.cpp` 的未选 switch/状态/handler | 批准 ID→handler→字段→wire→资产→测试映射后，只保留所选原语；双目标交换、channel、DoT、侦察、反射、免死、旧场机制无消费者即真删。 |
| D14/D11 C1 项 | `LDRiverEnvironment.cpp/LDRiverRules.cpp/LDEnvironmentRules.cpp/LDElementContact.cpp/LDIgnitionEnvironment.cpp/LDFullMapSurfaces.cpp` 及对应 Public 头/Director 字段 | 当前小目录无需天气、电火水冰、租约池、RiverCells/BurningUnits/FrostReceipts。先把必要静态地面/LOS 接世界，断 Movement/Vision/WorldObjects/Checkpoint 消费；不抽备用环境服务。 |
| D15 | `LDCharacterBuild.h/.cpp` 六属性/职业/点数/书籍/天然门槛、旧构造/start/profile/整体 Prepare 草稿；`LDGearRules.cpp` 六点预算 | 同基础角色、MOBA 投影、持久成长和 CD 分离先接好；必要角色记录迁入 Demo 作者域，旧 native Validate 的公式字符串/职业任务要求与 prototype 作者输入随旧链退出。 |
| D15 | `LDV08Loadout.cpp` 重复 active-family 拒绝/十一绑定/六共享动作；`LDInventoryTypes/Director` 背包/重量/Quick/掉落/旧 metadata | 两实例/QW/12/来源事务替代；删旧 family-max 的 Actions/Release/HUD 三端，不只换保管 CD 的字段。 |
| D16/D27 | `LDPlayerController.h/.cpp`、`LDPlayerInventory.cpp` 旧多个 RPC/字符串回执/hold/probe/Pending；DevelopmentPanel/InventoryPanel 的旧 UFUNCTION Route、OnClicked/AddDynamic 和 Press/Release | 新 void RPC、双请求域、typed 回执、规范空入口先解保护及窄 UI 动作接通；删六属性/旧服务页签及绑定，不留死委托。 |
| D17 | `LDVisionDirector/LDViewTypes/LDWorldViewRelay` 旧来源/kind/AuthorizedCue；`LDCharacter/LDHumanoidVisualComponent` 旧居民/工具/状态 marker | 生产→授权→发布→proxy/HUD/Host 附件→wire 白名单闭合，保留必要身体/碰撞/观测游标。 |
| D08/R10 | `LDWorldDirector.cpp::Contact/Reward/Defeated/PvPKill` 的 LastOwner 兜底、每调用新事务、只 XP/限额/间隔及平行 NextAttack | 同 DeathKey/RewardBasis/CommitDeathReward 和 Camp/Member 接线替代；保留 self 拒绝目的，回位不是死亡。 |

### 11.3 数据、反射、资产与工具闭包

| 层/映射 | 必须一起关闭的真实链 |
| --- | --- |
| D20 V07 | `Content\Lockdown\Data\V07\combat/equipment/acquisition/world.json` → Bootstrap 无条件导入 → `Generated\V07\DA_*Catalog` → catalog 非当前 loader fallback。先改 Bootstrap、Sync/native contract、Load、RuntimeDependencies/cook 四边，再删无用载荷/资产。 |
| D20 历史输出 | V08 的 `combat_b81/b82/b83.json`、`equipment_b81/b82/b83.json`、对应 manifest、`catalog.json/ImportManifest.json` 与 `LDB81/82/83Contract.h`；文本无消费者不等于二进制安全，C3 后才删。 |
| D21 旧链与迁移前提 | 当前 `SourceData\Catalogs\V08` 的 abilities/items/acquisition/starter_kits/environment/modifier_contracts/native_ids/catalog.schema → `SyncB84Data.py` → B84 三载荷/manifest/`LDB84Contract.h` → 旧 native Import/Validate/Load 是审计基线，不是目标生成链。旧 compiler 还读 CharacterBuild、InventoryTypes、EquipmentCatalog、AcquisitionCatalog 原生文件；先提取实际需要的记录、稳定编号和校验机制、改接消费者，不能先删依赖后掩盖失败。 |
| D21 唯一 Demo 内容链 | 按 `CONTENT_AND_BALANCE.md` §10.1，作者域固定为 `SourceData\Demo\content.json/content.schema.json/native_ids.json`，经内容域唯一 Demo compiler 生成 `Content\Lockdown\Data\Demo\demo_content.json`、同目录 `manifest.json` 及 `LDDemoContentContract.h`，由 `ULDDemoContentCatalog` 统一 typed Import/Validate/Load 和 DataAsset/Bootstrap 接入。战斗、属性/纯值公式、成长、装备、商店、奖励、RPC、UI/AI 都消费这一个目录/根身份；不向 V08/B84 再生成一份 Demo，不另造 Combat 专属 compiler。 |
| D21/D32 语义与旧链退出 | 多字段 Rank/effects、非零装备 MP、属性/政策必须真实投影到统一目录和 handler，并有行为断言；只保存 JSON 不算支持。新链/拒绝夹具/全部加载和 staging/cook 迁移完成后，删除旧 V08 作者输入、B84 载荷/manifest/contract、`SyncB84Data.py` 及无消费者旧 importer/多 schema fallback。可抽取旧代码和记录，不能保留旧目录继续生成或加载另一套 Demo；不手改摘要。 |
| D22/D23 | 断 Combat 对完整世界/人口/物件输入的消费，地图/C2 归世界。`prototype_rules.json → ULDCharacterCatalog::Import/Validate → DA_CharacterCatalog` 是待退出旧链；必要角色参数迁到唯一 Demo 作者域/目录，完成 Bootstrap/资产引用迁移后删除旧原型输入、旧角色目录资产及无消费者导入器。`DA_PrototypeTuning/Character.BeginPlay` 的必要几何/引擎接线由对应所有者核验复用，不作为旧战斗内容回退入口；缺必要输入显式失败。 |
| D24/C3 | 旧 IA_Assassinate/Dash/Interact/Ward/Sprint 与 Bootstrap 十三固定映射、旧 IMC/Widget 回调按当前输入迁移；键仍可有新用途。M_Character/M_Fog/T_FogClosed/WBP_UIRoot 先查真实引用，不能因占位资源盲删。 |
| 反射/C3 | CombatDirector/ArcProjectile/TacticalField/Corpse/Character/Resident/WorldPawn/WorldSite/catalog 的 UCLASS；Build/Plan/ActionRef/GearStats/TacticalRequest/Inventory/Owner/View 的 USTRUCT/UPROPERTY、RPC、dynamic delegate、CDO/Blueprint/地图引用都核查。旧类仍可加载时先迁移重存存活资产，再删类型。 |
| D26/C4 | Runtime/Editor Build.cs、DefaultGame 广泛 AlwaysCook、Package/OpenStandalone/Bootstrap 的旧图/profile/staging 需按实际消费者收敛。保留确需的 NetCore/Json/UMG/Nav/AI/Fog/OpenSSL 等，不为掩盖问题改共享引擎/插件安装。 |
| D27–D34 | 旧 probe/client helper/专用 runner、生成测试夹具、旧计数/指定 TradePrivacy 门槛随闭包迁移；六份 systems 和 README/DEVELOPMENT 同实施校准。删除旧专测前先迁有效身份/原子/来源/隐私断言，不仅降低成功数量。 |

本次未加载二进制资产；没有 AssetRegistry/Reference Viewer、hard/soft/management/dynamic root、
UHT/Blueprint 重存、Cook 及包内清单证据，**不声明任何资产已验证可安全删除**。
不保留 Legacy/Archive/Backup、注释旧实现、兼容壳或第二运行模式；历史仅在既有 Git。

## 12. 可分派实施顺序与退出证据

每个切片按架构 P0–P5 自身闭包执行，不等全部内容才开展独立基础工作；本轮不实施。

| 切片 | 前置与范围 | 最小退出证据 |
| --- | --- | --- |
| C01 身份/请求/统一调度 | 核心共享类型、操作/世界接口；改双请求域、取消、一次身体注册、SampleBoundary/permit | 序列/生命/重放/新局拒绝，Queued/意图/Accepted/Release 及原子 Nav 交接成立，旧时钟不再被调用。 |
| C02 纯属性及内容导入 | 内容公式/字段和实际目录批准；改 CharacterBuild/GearRules 消费者，接唯一 Demo compiler/`ULDDemoContentCatalog`，同一 Prepare/Apply | 所有属性真实投影，Demo 载荷/根 manifest/contract 一致，非法输入/多字段 Rank/非零装备成本可验证；旧 V08/B84/SyncB84 生产加载退出，P2/比例/回复/AS/CD 精度与来源解释一致。 |
| C03 来源/两主动/被动 | C01、C02，成长最终两装备事务 | 两条 IssuedCooldownId 独立、债优先回绑，无旧 family-max；被动代表换位不重复，失败无半绑定/半扣费。 |
| C04 效果/死亡/奖励 | C01–C03、世界真实接触、生命/成长 sink | 统一保护/Hit/PendingDeath/RewardBasis，独立效果与离局语义、主次击不递归，Gold+XP+机会一次逻辑提交。 |
| C05 观察/界面/圈联接 | C04 与 World DTO、真实运动/资格提供者 | 正常 Host/客户端不透雾，typed 回执包含失败失保；真实秒、完整 Sealing/fence、同刻死亡/圈竞争正确。 |
| C06 删除及资产联动 | 各切片实际消费者确定；本闭包 C1–C4 | 原生声明/调用、数据/loader/asset/cook、runner/手册无旧消费者；Editor/Game/UHT、Bootstrap 及当前规则检查。 |
| C07 真实场景 | 上述接线与批准实际输入 | 正常 Listen Host+拥有客户端及满规模，正常普攻起步→成长/购物→争圈→结算/同房间新局；不靠调试授予/传送/强制判胜。 |

## 13. 正反验收矩阵

以下是**待实现/待执行**的目标断言，不是本次游戏测试结果。
记录权限受控的 RequestKey/CombatToken、Match/Body/Victim、Source/IssuedCooldownId、
源/资源修订、Release/Hit/DeathKey/RewardBasis、真实时间/phase/permit/fence 及事务前后值。
测试输出归忽略的 `Artifacts`，不向普通客户端泄露内部日志。

| ID | 操作/故障 | 必须断言 |
| --- | --- | --- |
| A01 全属性 | 正常仅普攻开局，分别改变各批准属性、等级与来源 | AD/AP不混；双抗类型正确；AS/MS/Range/血蓝/回复/暴击/Haste各有真实消费者，面板同源。 |
| A02 重复装备 | 买两同款，卖代表再买；尝试第三件 | 普通属性两份、被动一次，组历史不断档不双触发；第三件钱物不变。 |
| A03 双主动 | 店外/圈内/满两槽，两同款依次合法 Release | 两条独立 ReadyAt，各自 MP/控制/动作验证；一条不锁另一条，不要求 ShopSession。 |
| A04 首发/债优先 | 空集合首次买装备/学技能、新局首次取得及重发；一件冷却买第二件；卖冷却件重买；两债不同期限都卖买；未绑定一债一 ready | 首次仅发行第一条，同请求只重放原绑定；角色无第二条。真第二件独立可用，回购先最长未绑定债，两截止分别保留，无第三条/共享max/空白绕债。 |
| A05 绑定竞态 | 两准备计划抢一条；出售 Prepare 后先 Release；旧实例请求晚到 | 版本/permit拒双绑；保留最新 ReadyAt；旧实例不得操作新绑定；失败钱物/机会/源全不改。 |
| A06 来源变化 | 换槽、升级、遗忘重学、死亡及新局 | 旧期限不中断；重学一级不洗 CD；旧 Source/目标请求拒绝，新局双 Match 域彻底隔离。 |
| A07 数值拒绝 | 超精度、NaN/Infinity、非法 Rank/引用、零分母、中间溢出 | 整次明确失败，不零效果成功、旧值 fallback 或手改生成 hash；每个合法字段有实际 native 行为断言。 |
| A08 资源比例 | 同事务多来源、无事件往返、最小正 HP 降上限、零 MaxMP；Prepare 后受伤/耗蓝 | 最终一次换算、无净取整获益；按内容 NumericUnderflow/NumericDomain 全拒；旧资源版本不覆盖新状态。 |
| A09 回复 | 同elapsed不同分段、速率改变、满资源后受伤、死亡/复活；攻击起手/伤害释放/受正伤(含盾全吸收)、失败失保/纯位移/自保/购物 | 余数/水位一致，不按帧丢回复、不回算新速率、不存满血免费量，不治活；LastCombatAt只由内容明确事件更新，失败失保/非战斗操作不重置。 |
| A10 攻速与取消 | AttackStart 后 S/移动/改 AS/死亡再复活 | NextAttackAt不提前、Nav无第二时钟；新 AS仅未来开始；未准入不产生节拍。 |
| A11 两请求域 | DirectSequence 与 AttackCycle 数值相等、重复 NavKey、伪造 Nav 来源 | 不撞号，一请求只一权威 Token；客户端无法冒充内部周期；取消的用户序号不误比服务器 Token。 |
| A12 回执/重放 | 同 key 同/异条款、缓存过窗、晚到 Queued/Finished、旧 life 查询 | 只重放已知阶段，异条款冲突、过窗不执行/假成功；旧发布不重启动作或新身体。 |
| A13 原子交接 | Queued 后拒绝、Handoff 失败、接受后 Release 失败、回调自动下一击 | 收件不改旧单；准入双不提交或双接受；不恢复历史单；回调不越阶段递归释放。 |
| A14 失保语义 | 有保护时空 Q/无主动/旧绑定/无蓝/CD/目标失效、远距 AttackUnit/AttackMove | 规范新鲜意图先解除，Rejected带ProtectionRemoved/LifeRevision；MP/CD/节拍不因失败写，不等追到范围。 |
| A15 失保前闸 | 旧 match/life、重放/异条款、NaN/坏 enum、Armed未确认、Move/Stop/Shop | 不改变当前保护；本地预测不能吞已确认有效形状的进攻尝试；旧回执不再发资格事件。 |
| A16 Root/盾 | Root下原地普攻/自解/位移，独立Root重发/到期/净化后新施加；入盾量低于/等于/高于旧剩余，弱盾组合净化/回蓝 | Root限制准确、重发不续期；净化只清当时全部Root，新Root可再生效。盾按剩余比较，相等更新源/期限、弱盾保留旧记录但其它合法子效果/费用/CD照常，无旧上限/隐含免疫。 |
| A17 观察/Range | 接收/起手/释放间失视野或换 Life；射程边界、胶囊变化、隔墙近距 | 每阶段新样本；失视野停追击无LastSeen/自动恢复；脚点Range与LOS同验，不因长射程自动生成追踪弹。 |
| A18 释放原子 | 角色/装备非零 MP、容量/分配失败、取消、来源改变 | 成功Release才一次支付/注册；准备不毁旧状态；失败无空效果成功，已释放Miss不退款。 |
| A19 源/目标时态 | Release 后源升级/卖装/遗忘/死亡重生，目标买抗性/加盾 | 源快照不变、目标当次防御生效；独立效果继续，旧目标 Hit 不改绑新 Life。 |
| A20 接触/吸血/暴击 | 仅盾吸收、实际损失、过量、保护、重复 AttackId；非暴击技能 | contact≠HP损失；一次采样，主击实际损失一次吸血，不治死者/新身体、不递归。 |
| A21 普攻群攻 | 正主击/盾全吸收/Miss/保护/量化零，双锤、副目标阻挡/隐藏/死亡、主击杀升级、副击野怪 | 只正contact一轮次击；主脚点锚/逐副LOS，共用旧AD/AttackId；副击无攻击者crit/proc/吸血/再群攻，但固有受击/受害时钟/营地反击/死亡照常，每生命独立DeathKey。 |
| A22 一生命唯一 | 同时多来源致零、重复 Die、改 DeathSerial、同 VictimLife 重注册Body | 首次PendingDeath唯一致死源/Key；不被后续伤害夺奖，不因重注册再奖励。 |
| A23 同批互杀 | 双方独立效果互杀；第一份奖励使另一已死受害者升级，交换支付顺序 | 当前Effects集合先锁完受害Level/BuildRevision与信用；奖励输入不受顺序污染，来源Alive不是领奖条件。 |
| A24 无主/非击杀 | 先玩家打伤后无主致死、自杀、离局清理、营地回位 | 无历史Owner兜底；NoReward也终态；回位不奖、不覆盖更早PendingDeath。 |
| A25 奖励故障 | 首次RetryableNotCommitted后分别成功/再次失败；首次不可恢复/部分提交故障及终局 | 原Key/基准不变且不半奖。唯一重Prepare成功时FailAuthorityBoundary调用数为零；不可恢复或再次失败只终止一次，返回后新permit/Prepare/Apply/奖励均拒绝、不封成功fence、不继续Capture，不伪NoReward或再次发钱。 |
| A26 生存边界 | 致零后安排吸血/回复/盾；死亡等待确认选择并重发，复活同刻旧确认 | PendingDeath不治活；死亡选卡一次保留不回血，复活满新max，旧确认不操作新Life、不授予第二初始奖。 |
| A27 独立/持续/退出 | Release前后分别死/售/忘；持续贡献失源；退出前后命中及同ceil桶 | 未释放取消、独立续原期限、持续贡献停止；Leave先无新Hit，Hit先冻结资格不因CurrentInMatch改NoReward。 |
| A28 终局/占点 | 完成同刻致死/离圈/争夺，对照非致死伤害/圈内Root；排队交易/选择/清理回调 | 破坏条件先纳入，单纯战斗不清圈；关闸前事务保留、之后零新玩法写，旧permit拒绝。 |
| A29 真时/水位 | 同段进出再进、1000.8ms离圈与1001ms到期、只完成1.0002s请求封1001ms、缺pose/流 | 先真实秒、真同刻才phase；中间事件不丢、桶不当先后/fence，不回填/假Miss/先判胜。 |
| A30 完整Sealing | Root在长step中到期；Actions/Effects/Respawns/Transactions派生位移/致死 | 最早边界先提交控制再跑后段；初次Motion不封口，全部因果子批/资格完成后Capture才消费。 |
| A31 读权限 | 正常可视Host+拥有客户端；敌人入雾、可见检查、死后拖图、合法隐藏次击 | 可见名称/等级/HP/MP/状态/装备完整；无私有实例/源/CD/属性/金币，入雾停单清显示；原生/代理/音效不旁路，死后无新增活体视野。 |
| A32 公开例外 | PvP/普通野怪击杀、圈状态、AI读模型 | PvP有限通知、野怪不刷全场横幅；无隐藏坐标/构筑/营地资源偷渡，AI不查内部Contacts/全Bodies。 |
| A33 正常闭环/新局 | 批准内容下普攻打野→奖励→选卡/买装→QW/12/射程群攻→死亡复活→占领→新局；含三真人三AI | 无调试授予/传送/强制胜；真实拥有连接/延迟乱序下只结一次，新局旧请求/Effect/Death/交易/观察无执行权。 |
| A34 营地反击/到家 | 未受击接近、无效意图/实际敌对命中、Returning连续受伤/Root/致死、到家、同刻致死/在途弹体、重置后新伤害/新返回并重放旧epoch、玩家伪造Reset | 只真实作用事实触发反击，无隐藏坐标共享；Returning可被杀不追击，受击只变资源/状态修订，不增epoch/重启返回。存活到家才重置、同刻死亡优先；同ResetKey只读重放，旧epoch不补满新伤害。Life/Hit/Death水位保留，无复活/奖励/伪Life隔离，玩家不可洗CD。 |

A34 的纯投影夹具另覆盖临时上限增益/压低效果移除后的顺序：先清候选状态、
再算最终 Max、最后填满；结果既不超新上限也不少补，失败前不改活体。
这不将未选临时 Max 内容加入首版目录。清旧 Root/盾后再施加同类型新实例，
旧到期回调不得复生旧状态或删除新实例；同 Life 的外部独立新接触仍按正常规则作用。

A01/A07 同时检查纯值入口：实际执行和 AI 估计提交相同值/公式集时，数值结果逐字段一致；
重复求值不采 RNG、不读 Actor/营地运行表、不推进时钟或写入资源/Token/Hit/Death。
错误显式返回且估计不伪装零风险；A26 另核对复活后的脱战基准取本次实际复活时刻。

现有最小入口可迁移，而非直接宣称已验证上述目标：
`RunTests.ps1` 的 Lockdown.Rules、`RunCombatCheck.ps1`、
`RunCombatNetworkCheck.ps1`、`RunTacticalCheck.ps1`、`RunLoadoutCheck.ps1`、
`RunContentNetworkCheck.ps1`、`RunViewNetworkCheck.ps1`、库存/商店/占领/重开对应检查。
旧 runner 含旧图/profile、固定数量和非参赛 authority，不能臆造 Demo selector 或任意 TestFilter。
保留 CharacterBuild/V08Loadout/Tactical/Economy/View 的身份、失败不变、原子和隐私断言，
迁掉六属性/旧等级/开局技能/死亡税/全集数量/旧 family 规则，再同步 runner 必需用例清单。

后续数据变更先 Sync `--check`、输入拒绝与生成一致性；原生/反射再 Editor/Game 构建、
Bootstrap Validate 和相关场景；网络、成长、交易、胜负必须到真实拥有连接。
纯文本、Host全知 probe、截图、旧测试 PASS 都不能代替本模块真实验收。

## 14. 研究与审核证据

| 角色 | 实际独立身份 | 职责/证据范围 |
| --- | --- | --- |
| 战斗/属性/效果领域专家 | `combat-domain-expert`；`2b0c233f-6fc7-47f2-919f-694af3032a6a` | 独立从 Spec、手册和已确认规则设计最小状态、来源、时序、被动/资源边界与正反案例；未兼任 QA。 |
| UE 原生/权威/来源工程专家 | `combat-ue-auditor`；`52104ff7-578b-447d-8f8a-92df8d440a29` | 独立追查真实调用、Source/生成/反射/资产/runner 消费链；核验行号与公开 UE 资料；未兼任 QA。 |
| 独立战斗/生命周期 QA | `combat-lifecycle-qa`；`2ccf117f-bbee-46c3-a4c6-196e1648c23b` | 两份研究与完整整合稿齐备后独立审核，未参与写作；直接核对源码、生命周期/操作定稿及修订后的数值、来源、时间、反击/重置和权限边界。 |

**独立审核结论：文档有条件通过，当前无高置信度必须修订项。**
条件是内容/数值批准、完整文档包接缝统一，以及后续实现、资产和运行验证；
不是带着已知未修文档缺陷放行。两份研究均由该 QA 完整阅读，关键源码已抽核，
50 个内部链接及引用行界已检查。该结论只属于静态文档审查，不等于 D2、游戏通过或资产安全删除。

公开资料由工程专家通过 Context7 的 Epic 官方集合检索并读取原文，只支持框架机制，
不替代项目源码或 UE 5.8.4 本机头文件/运行验证：

| 来源 | 本报告使用范围 |
| --- | --- |
| [Actor Owner and Owning Connection](https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-owner-and-owning-connection-in-unreal-engine) | 拥有 RPC/复制与 possession；效果信用不能依赖当前 Pawn 控制器。 |
| [Remote Procedure Calls](https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-procedure-calls-in-unreal-engine) | RPC 为 void；返回 typed 结果的是服务器域 API，不是同步网络返回。 |
| [Replicated Object Execution Order](https://dev.epicgames.com/documentation/en-us/unreal-engine/replicated-object-execution-order-in-unreal-engine) | 跨 Actor/OnRep 无业务全局顺序，必须使用身份/阶段/修订。 |
| [Actor Relevancy](https://dev.epicgames.com/documentation/en-us/unreal-engine/actor-relevancy-in-unreal-engine) | 连接相关性不自动等于战争迷雾，更不控制 Host 已有权威 Actor 的全部本地表现。 |
| [Game Mode and Game State](https://dev.epicgames.com/documentation/en-us/unreal-engine/game-mode-and-game-state-in-unreal-engine) | GameMode 权威、GameState 有限公共状态与同步显示时间，不替代业务时钟/写闸。 |

检索未提供独立的 5.8.4 补丁文档集合，部分交叉链接带其它引擎版本；
不能据此宣称该补丁 API/UHT/物理已通过。
两份原始研究留在会话工件，不将聊天、撤回方案或多轮审核日志附入仓库。

## 15. 就绪范围与未执行限制

本模块初始待确认的玩法边界均已有用户结论；不再把双持、独立主动 CD、失源计时、
比例资源、死亡选择、意图失保、观察粒度、独立效果存续/离局作为待选玩法。
**仍待批准的是内容方案及其精确实现输入**：公式/倍率层、P2/数值域、具体角色/技能/装备/营地参数、
Root/屏障/回复/主次命中等逐定义语义和实际载荷。
这些只阻塞相应真实数值切片，不阻塞无关身份、拒旧、写闸及确定旧政策的依赖准备。

本轮只产出文档，未实现/删除 Source、Config、Content、SourceData 或 Scripts；
未执行 Unreal 构建、生成器、Bootstrap、原生/游戏/联机/平衡测试或资产加载。
正常 Host/真实客户端、同刻运动和水位、原子奖励、二进制引用及完整 Demo 的运行证明仍须后续提供。

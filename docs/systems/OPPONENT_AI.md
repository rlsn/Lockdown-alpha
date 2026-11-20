# 对手 AI

按任务跳转：[接口与权限](#3-sc-09状态正式接口与权限) · [生命周期](#4-sc-09全生命周期矩阵) ·
[实现文件](#5-实现文件与边界) · [策略优先级](#61-状态与优先级) · [营地与回防](#62-营地目标与公开回防) ·
[战斗来源](#63-普攻与每一个主动来源) · [成长构筑](#64-持久选择与构筑) · [重试](#65-有界重试与可观察决策) ·
[野外目标与死亡选点](#66-野外目标与死亡选点) · [参数归属](#7-sc-09参数与内容唯一来源) · [验收](#8-验收与证据) · [限制](#9-已知限制)。

| 项 | 内容 |
| --- | --- |
| 规则 | [App Spec](../APP_SPEC.md) §6.2；AI 参数只在[内容与数值 §6.3](../demo/CONTENT_AND_BALANCE.md)；跨域见[共享契约](../demo/SHARED_CONTRACTS.md) |
| 文件 | `Source\Lockdown\{Public,Private}\AI`：`LDOpponentController`（唯一服务器适配器）、`LDOpponentPolicy`（纯值策略与成本估计）、`LDOpponentFrame`（受限投影映射）、`LDOpponentRequests`（意图→领域请求、回执→失败记录） |
| 入口函数 | 绑定与读取 `ALDGameMode::RegisterAIContender` / `ReadAISelf` / `ReadPublicMatch`；订单 `ULDNavigationComponent::SubmitOrder` / `CancelOrder` / `QueryOrder`；动作 `ALDCombatDirector::SubmitAction` / `CancelAction` / `QueryActionReceipt` / `QueryAvailability` / `BuildOwnerView`；成长 `ALDDevelopmentDirector::QueueBuild` / `QueryBuildReceipt`；观察 `ALDVisionDirector::EnumerateVisibleUnits`；路径估计 `ULDMapRegistry::QueryGround` / `QueryCompletePath`；数值 `LDTactical::EvaluateDamage` / `EvaluateBasicTiming` |
| 不变量 | AI 不拥有任何玩法状态，只读获准投影并经与真人相同的服务器入口提交；一种固定优先级策略，没有 GOAP、人格、多难度或第二套世界模拟；不持有写许可；只用当前可见单位，不保存 LastSeen；只处理自己的选择，不替真人选择 |
| 检查 | `Lockdown.Rules.DemoOpponentPolicy.*`；场景 `RunDemoOpponentCheck.ps1`（见 §8） |

对手 AI 是能独立完成成长并按相同条件获胜的参赛者，不是占席、巡逻或靶子；产品约束（名单、成长、营地反击、冷却、死亡、保护、视野与结束）见 App Spec，本文只写 AI 侧的接口与策略。
恢复只能依靠同规则的自然回复或已拥有的合法能力，不能通过重建身体、刷新构筑或调试授予恢复。
AI 加点先比较已学主动的 AD/AP 系数总和，将点数投向较强的一侧对应属性直到激活关键石；相同或无主动系数时优先力道（若基础攻击不提供 AD，则优先体格）。达到门槛后，再在相关属性间均衡分配。该策略使用目录的既有系数与关键石门槛，不引入新的数值。

## 3. SC-09：状态、正式接口与权限

### 3.1 唯一新增适配，不新增领域所有者

选定 `ALDOpponentController : ALDPlayerController` 作为唯一服务器适配器，实现于
`Source\Lockdown\Public\AI\LDOpponentController.h` 和
`Source\Lockdown\Private\AI\LDOpponentController.cpp`。纯值策略独立实现于
`Source\Lockdown\Public\AI\LDOpponentPolicy.h`、`Source\Lockdown\Private\AI\LDOpponentPolicy.cpp`
（下表的帧/状态/决策类型与成本估计）；受限投影映射 `LDOpponentFrame` 与意图→领域请求、
回执→失败记录的纯翻译 `LDOpponentRequests` 同在 `AI` 目录，均由 `Lockdown.Rules.DemoOpponentPolicy.*` 覆盖。
适配器所在文件以 `LDMatchProviders::FRegistrar` 注册 `ELDMatchProvider::OpponentAI`：Starting 的 Prepare
为每个冻结名额生成一个控制器，经核心注册后绑定；MatchClosed 只停止活动；MatchReset 只停止活动，
待 RetireMatch 按真人路径退役身体、丢弃 AI 记录并换代后，控制器在下一 tick 自行销毁，
避免在仍有记录时经 `APlayerController::Destroyed` 走一遍离局路径。
它直接继承精简后的现有基类，**不经过 B91/B92**。使用这一无连接服务器适配器，
是为了复用当前 StateFor/BuildFor/Holder 绑定，不是模拟真人网络连接。

核心创建同类 `FLDContenderRecord.Session`，按 `ELDContenderKind` 显式登记 Human/OpponentAI；人数统计不能依靠
`IsPlayerController()`。适配器无 LocalPlayer、输入映射、UI、拥有连接或客户端 RPC 通道，
不以 `_Implementation` 调用跳过领域验证。共享基类中的本地初始化与表现必须按真正本地拥有者隔离。
无连接服务器控制器上的 Client RPC 会在本机执行，故适配器覆盖 `ClientTravelInternal_Implementation`，
从不让 Host 进程随 AI 旅行。
GameMode 的注册/注销仍是唯一入口，不让新 AI 获得旧 Roster 的 friend 写权限。

| 状态 | 正式写入者 | 类型/函数、AI 能保存的内容 |
| --- | --- | --- |
| AI 名额、Contender、Match、Life、阶段和复活 | GameMode 生命周期域 | 复用 `FLDMatchContext{Generation,LedgerMatch}`、`FLDVictimKey{Kind,EntityId,Life}`、`FLDCombatBodyRef{Victim,BodyIdentity}`；AI 的 Victim.Kind 必须为 Contender。 |
| 策略状态 | 本 AI 的 `ALDOpponentController` | `LDOpponentPolicy.h` 的 `FLDOpponentPolicyState`：当前模式、公共 POI、当前合法目标句柄、有限访问/失败记录。请求游标与下次决策期限归适配器。不是第二份权威身体或成长。 |
| 一次决策输入 | 各领域投影；AI 只读副本 | `FLDOpponentDecisionFrame`：本人 owner/progression/readiness、已发布 WorldView、公共 MatchView、只读公共目录。纯策略不接 UWorld/Actor/Ledger 指针。 |
| 营地成本估计 | AI 纯值策略计算，不是玩法写入 | `LDOpponentPolicy::EstimateCampCost(FLDCampCostInput, 公共怪物定义, 目录) -> FLDCampCostEstimate`；输入只有本人 HP/派生属性/普攻 readiness 与公共营地模板。结果区分有数值的 Ok、预算/生存条件不允许的 NotFeasible、输入/公式失败的 EstimateUnavailable；数值原语复用 R07，不推进世界、RNG、CD 或真实血量。 |
| 意图 | AI 产生；对应领域验证/提交 | `FLDOpponentDecision` 是 Body（Order/Action）与 Build 的有界判别结果，不含可写权限或另一个参赛者 ID。 |
| 绑定 | GameMode 唯一注册、核心注册后通知 | `RegisterAIContender(AController&, uint8 RosterSlot, const FLDMatchContext&) -> FLDAIRegistrationResult` 仅 Starting 使用冻结 AI 名额；返回核心 `FLDAIContenderBinding{Match,ContenderId}`，再调用 adapter 的 `BindRegisteredSelf(const FLDAIContenderBinding&)`。不得让策略任意选择 ID。 |
| 本人快照 | 核心与原领域只读投影 | 核心 `ReadAISelf(binding) -> FLDAISelfReadResult`（含 `FLDOwnerState.Growth` 发布）与 `ReadPublicMatch() -> FLDPublicMatchView`；adapter 的 `BuildFrame` 再组合本人 `BuildOwnerView` 与 `EnumerateVisibleUnits`，不在核心另建快照真相。缺服务/旧局/已离局是失败，不返回成功形状空快照。 |
| 决策与发送 | AI / 原领域 | 每个 `AI.DecisionIntervalSec` 的权威时间 `BuildFrame` → 纯 `Decide(Frame, PolicyState)` → `DispatchBody`/`DispatchBuild`；后者用核心绑定解析的本人 Caller 调用各域服务器入口。 |
| 回执与生命周期反应 | 原域先提交，AI 后消费 | `SettleOrders`/`SettleDirectAction`/`SettleBuild` 查询原 ID，`HandleLifeCommitted`、`HandleMatchClosed`、`HandleMatchReset` 只更新策略状态/游标。迟到事件不能恢复已结束的旧订单或旧生命。 |

以上类型和入口均已实现。核心当前局映射不被 AI 强转或复制成新的真相：
Generation 对应网络 Match，LedgerMatch 保留 FGuid；BodyRef 每次从当前本人投影取得。
策略中缓存的代次只是拒旧条件，不具有开局、复活或授予资格的权力。

### 3.2 调度与读取合同

唯一顺序为 `Session → Motion → Actions → Effects → DeathsAndRewards →
RespawnsAndWorld → Transactions → Capture → Close → Publish`，由生命周期域持有。
AI 在 Publish 后读取已经完成的获准快照，只产生**下一实际边界**的意图；
不能在当前批次后半段插入回溯攻击，也不能推进权威时钟或改 World.TimeSeconds。
`GetAuthorityNowMs()` 是唯一可用性时基，使用核心 safe-floor 的 NowMs，不自行 ceil 提前到期；
事件真序由核心按权威事件时刻处理，`GetAuthorityNowSeconds()` 处于同一原始时间域。策略期限是调度提示，
不是复活、刷新或冷却的第二计时器。
AI 不持有 `FLDGameplayWritePermit`，更不能直接调用 `ProcessAuthorityOrders` 等提交入口。
只聚合同一已完成发布边界的本人/公共/观察投影，不用不同边界的资源、来源和目标拼造准入；
执行时仍由各域重新验证当前状态。
核心提交后提供 `LifeNotice{Match,BodyRef,Change,LifeRevision,Stamp}`、
`MatchClosed{Context,ResultReceipt}`、`MatchReset{OldContext,NewContext,LobbyRevision}`；
它们是事实通知，不给回调重新打开写闸的权限。

| 获准输入 | 正式来源 | 限制与撤销 |
| --- | --- | --- |
| 自身 | 核心 ReadAISelf（含 Growth 发布）、adapter BuildFrame；本人 Combat BuildOwnerView | 可以读本人生命/劲/资源/等级/技能（含技能分支与技能点）、图纸、属性点、控制、真实 readiness、候选和回执（ AI 不再读 `BuildSafeAtMs`）；不能取得可写 StateFor 或全账本。 |
| 公共对局 | `FLDPublicMatchView` → `FLDOpponentPublicMatch`；休息中另读公开房间名单 `ReadLobby`（成员 ID，`Roster`） | 名单只用于按 HUD 规则认出获准观察帧里的共享真实训练目标（§6.4），不借此读取所属玩家私有成长。Phase、OpenAtMs、CircleState（`Empty / Solo / Contested / Guarded`）、Occupier、CaptureEpoch、SoloSinceMs、DueAtMs、GraffitiOwner（策略帧中为 `Owner`）、GuardDueAtMs、Result；Occupier 在 Solo（独自喷漆）和争夺暂停（Contested 且有人持有喷漆进度）时有效，DueAtMs 只在 Solo 时有效；Owner 是当前涂鸦主人，GuardDueAtMs 是运行中守护的获胜时刻，只在 Guarded（主人独自在区内）时非 0；Empty（主人在区外、区内无人，守护暂停）与 Solo/Contested（区内有主人以外的有效参赛者，守护冻结）时为 0。喷漆者和涂鸦主人的身份与进度不附送其隐藏精确坐标。 |
| 当前可见单位 | `EnumerateVisibleUnits(ObserverContext, Purpose) -> ObservedFrame` | 带 Match、ObserverLife、ViewGeneration/Revision、MapRevision、ServerTime、Handle/ObservationEpoch。AI 与真人使用同构投影；只含已批准展示字段。 |
| 可见营地回位提示 | 同一观察帧单位 `Statuses` 中只读 `StatusId=CampReturning`（`ELDDemoStatusId`，内容 id `camp_returning`） | Vision 仅为当前获准可见且 Returning 的 CampMember 派生“回位”；不含堆叠、期限、Home、路径目标或仇恨，不是可写 Combat 状态。Root 暂停不清提示；退出 Returning 或失视野即撤销，隐匿时不更新。 |
| 可见资源 | 上述观察帧的展示字段 | 不含 IssuedCooldownId、ReadyAt、SourceBindingRevision、技能列表或完整属性。HP 按世界 Resources 的 P2 Current/Max 读取，劲只有公开比例；NPC 不适用的 Level 是 NotApplicable，不补零猜测。失视野整条动态记录撤销。 |
| 静态世界与内容 | 世界公开地图/POI、公共目录 | Camp/Capture 固定位置、营地档位、地形和公开定义可知；不能把定义里的原始营地人数/刷新周期当作当前存活人数或剩余刷新时间。 |
| 路径 | `ULDMapRegistry::QueryGround(WorldRevision, FootCm, Capsule, SurfaceHint)`、`QueryCompletePath(WorldRevision, FromGround, ToGround, Capsule)`；接近公共地点与单位由 Navigation 订单规划 | QueryGround 四参，无默认/三参重载；SurfaceHint 为必填 FName 注册 SurfaceId，仍须重查真实支持面。AI 只用自身真实位置与当前地图估计到喷漆区的完整路径长度（厘米）；不含隐藏动态身体定位。 |
| 单位解析 | `ResolveObservedUnit(ObserverContext, Handle, Purpose)` | 仅供导航/战斗执行域重查可见性、目标生命和 epoch；返回的 authority 样本不进入策略黑板。 |
| 历史 | 本 AI 的公共 POI 访问和失败事实 | 不保存单位 LastSeen、Actor 指针、外推轨迹或隐藏营地倒计时。未见到目标不是“营地已清空”的证明。 |

不能用 `GetAllActors`、`TActorIterator`、GameMode 全参赛表、Combat Bodies/Nearest、
World Pawns/CampNext/LifeOwners 补全感知。
世界域为了生成受限帧而遍历权威实体，与策略直接扫描全世界是不同权限。
正常 Listen Host 的表现与 AI 都须经过同一白名单；此边界不承诺隔离恶意修改的 Host 进程。

SurfaceHint 仅从公开地面 trace 或公共 POI 注册取得；当前身体的支持面由权威
CMC.CurrentFloor 的真实有效 floor 映射 Registry，经原域提供给 adapter，
纯策略不拿 floor Actor 指针。hint 只是期望，不是接地证明；
不传空 hint 向下投影、不沿用历史 floor、不猜默认平面或静默吸附另一层。
NAME_None/未知提示返回 InvalidSurface，支持面不符返回 WrongSurface；
身体无合法 floor 返回 NotGrounded，即使投影 XY 在喷漆区内也不计入喷漆区人数。
NotGrounded 是明确否定结果，原域仍完成已处理边界，不是缺数据/查询未完成的系统故障。

### 3.3 命令、回执与拒旧

| 域与正式接口 | AI 发送的最少条款 | 提交与回执规则 |
| --- | --- | --- |
| `ULDNavigationComponent::SubmitOrder(FLDOrderCaller, FLDSpatialOrderRequest)`；`CancelOrder`；`QueryOrder` | 绑定本人的 MatchContext/BodyRef；`FLDMoveIdentity{Match,Life,Order,Epoch}`；按 kind 为单一 Ground/ObservedUnit 目标。Ground 带脚点 cm、必要 MapRevision 和必填 SurfaceHint；POI 从注册记录给支持面，目标可显式 Missing，不补默认值。 | Submit 只收件。Actions 阶段的 `ProcessAuthorityOrders` 才可激活；Queued 不取消旧单，完整准入后才替换现役订单。OrderRevision/ReceiptSerial 单调，Query 只读且不延寿。 |
| `ALDCombatDirector::SubmitAction`、`QueryActionReceipt`、`QueryAvailability`、`CancelAction` | `FLDCombatActionRequest{Generation,Life,DirectSequence,Entry(Q/W/E/Dash),Target(Missing/Self/Unit/Direction),ExpectedSourceRevision}`；DirectSequence 由 adapter 单调分配，Q/W/E/D 共用一个序列；Unit 目标是观察句柄。`FLDCombatRequestKey` 的 Direct 键由此生成，Nav 键只由服务器 Navigation 的攻击订单内部生成。 | 两种 RequestKey 不撞号；准入成功后由 Combat 统一分配 authority CombatToken:uint64，ActionId/AttackId 是该 Token 在 Match/Caller 下的身份，不另发序列。Queued 无已接受 Token；查询/取消用原 RequestKey。导航独占 AttackCycle，Combat 独占 NextAttackAt，AI 不另开普攻循环或伪造 Nav 键。 |
| `ALDDevelopmentDirector::ReadProgression`、`QueueBuild`、`QueryBuildReceipt` | `FLDBuildCommand`：Generation、当前 Life、ExpectedLifePhase、独立 RequestId、ExpectedBuildRevision、CatalogDigest 与 Kind。ActiveChoice 带当前 OfferId/Revision、Option 和 `bLearnIfPossible=true`（Slot 为 AnySlot，由服务器取最低空槽）；LearnBlueprint 带图纸与最低空槽；AllocatePoints 带按五维设计顺序（体格、脸皮、力道、脑力、身手）每项一个点数；PickCard 带当前牌组与所选牌（有位置时带位置），InstallCard/UninstallCard 带序号，ReplaceActive 带图纸与已学槽位，ShopBuy 带货架位（或技能点）与商店修订，Respec 只带守卫字段。由 `LDOpponentRequests::BuildCommand` 从本人最新成长投影构造，候选、牌组、图纸、槽位或牌已不是当前状态时拒绝构造。 | 原候选不重抽；选择（学到空槽或存为图纸）与机会消费原子提交后才生成下一组。服务器提交时按阶段与生死重判（没有构筑安全窗）；死亡阶段合法的选择、学习与加点可成功，死前 Alive 请求不能冒充当前 Dead 请求。 |
| `LDTactical::EvaluateDamage(const FLDDamageEvaluationInput&, const FLDDemoContentSnapshot&) -> FLDDamageEvaluationResult`；`EvaluateBasicTiming(const FLDBasicTimingInput&, const FLDDemoContentSnapshot&) -> FLDBasicTimingResult` | 仅数值假设：公开编译定义、本人所需属性、显式非暴击假设、匿名模板抗性/HP/盾/保护；Timing 用对应单位普攻定义及已解析 AS。目录快照是批准编译的不可变内容。 | 位于 LDTacticalRules，成功给数值/时长，定义域失败显式返回错误；无 Actor/World/Bodies/Camp 查询、时钟、RNG、状态或 Token。真实 ApplyHit/AttackStart 也必须调用这两个同源纯函数，再经 Permit 提交；估计结果不是 Hit/Death/奖励事实。 |

每域独立请求空间和有界回执窗口：同 ID 同条款只重放原结果；同 ID 异条款
RequestConflict；过窗仍保留拒旧高水位并明确 ReceiptExpired。没有收到回执不能改新 ID
重做同一选择/释放。先查询原 ID；只在取得终态，或领域拒旧水位已证明该旧请求
不可能再执行且已读到当前权威投影后，才基于当前状态作新决策。
ReceiptExpired 不是业务成功，不虚构原结果。
Pending/Queued 不等于成功，ActionFinished 也不等于所有已释放效果消失。
直接意图取消的 ThroughDirectSequence 只比较直接请求序列，不与 authority CombatToken 比较。
派生普攻继承当前导航进攻意图的解除保护事实，不由 AI 再生成一条独立 Basic 请求；
Rejected 可以有 ProtectionRemoved 而没有 ActionId。

成长的正式查询是
`QueryBuildReceipt(const FGuid& ContenderId, const FLDReceiptQuery&) -> FLDBuildReceiptQueryResult`；
ContenderId 仍由绑定 adapter 解析。Query 带 Generation:uint32、
Family:Build、RequestId:uint64，**先拒旧 Generation 再查记录**；
不能把新局 RequestId=1 映射到上局记录。查询返回包装结果，不是业务终态 Receipt：
LookupStatus=Pending 时仅 PendingInfo 存在，其中 OriginalLife/OriginalLifePhase
来自已受理请求，没有 Outcome 或成功载荷；
Final 时仅匹配原查询键的对应业务 Receipt 存在；
NotFound/ReceiptExpired/InvalidContext 时两种载荷均不存在，只返回原查询键和状态。
不存在的数据不补默认 OriginalLife，也不伪造 Applied/Rejected 或选择结果。
Queue 结果区分 AcceptedPending、Final、AdmissionRejected，并携 SequenceReserved。
未受理且 SequenceReserved=false 的拒绝不占用未来序号；
已受理的最终拒绝也消耗该序号，已保留 ID 的异条款仍冲突。
Pending 只查询/重发同条款，不先送下一笔业务。
Build/Offer/Progression/SourceBinding/Readiness 修订均为 uint64；
OfferId/IssuedCooldownId 均为 FGuid。
技能 DefinitionId 服务端为 FName，wire 使用固定 64 字节 UTF-8 缓冲：
最多 63 字节内容、NUL 终止、余字节零填充；批准 ID 使用 `[a-z][a-z0-9_]*` ASCII 子集。
先检查编码、长度、终止/填充、规范和目录，不截断未知/非法 ID，也不降级成默认能力。
CatalogDigest 使用读模型给定的根摘要，wire 为 SHA256 的 32 raw bytes，
显示为 64 位小写 hex；AI 不自行重算或拼接另一摘要。

Stop 提高 Order 和 Epoch；服务器因目标失效结束原 Identity，不制造调用者未知的新 Epoch。
接受回执时核对局、生命、域、请求身份与单调修订；旧 ACK、旧 Offer 确认和旧 Outcome
不能重建动作。取消不退已释放资源、不清未结束冷却、不重抽候选。
同局跨 Life 的历史 Build 回执可以只读核对原 RequestKey 的终态，
不能将其中旧身体状态应用到新身体；持久构筑以当前本人投影为准。
AI 订单的存活性来自仍有效的服务器策略/参赛记录绑定，不发送伪造 ServerHeartbeat；
记录、controller、生命失效立即结束。QueryOrder 不刷新租约，普通 UI 焦点也不影响 AI 身份。

**Rejected 不一律等于零状态变化。** 合法身份的新 AttackUnit/AttackMove 或角色
进攻意图会先经生命域解除保护，回执可带 ProtectionRemoved 与 LifeRevision；
随后 TargetUnavailable、NoCompletePath、资源或冷却拒绝仍保留该变化，但不消耗失败动作的劲/冷却。
旧局/旧生命、重放、越权和真正畸形请求不触发这一步。AI 必须重读本人状态，不能恢复本地保护标记。
InvalidSurface/WrongSurface/NotGrounded 属于世界可执行性拒绝，不伪装成 Malformed；
已识别的规范化进攻意图仍按同一前置解除保护。

| 结果类别 | 单一恢复路径 |
| --- | --- |
| Queued/Pending | 保留原请求；按有限查询节拍 Query 原 ID，不创建新的等价操作。 |
| StaleBuild/SourceBinding、StaleVersion | 读取本人/原 Offer，废弃旧确认；按新条款重新判断，不悄悄改选项。 |
| TargetUnavailable | 不区分隐藏、销毁、过期或换生命；清目标、停该单位追击。禁止跟到最后坐标或换一个全局 ID 重试。 |
| NoCompletePath/PathBlocked/NoProgress | 接受原订单终结；有限退避并选择其他公共可达目标。不 partial 成功、不传送、不缩短射程、不每帧重算。 |
| InvalidSurface/WrongSurface/NotGrounded | 重读合法支持面/本人真实接地结果，等相关条件变化后再规划；不伪造 SurfaceId 或把身体向下投影成接地，不以此错误自报系统故障或喷漆成功。 |
| Cooldown/AttackNotReady/MomentumShort/ControlBlocked | 使用本人 readiness 和控制投影等待或换合法动作；策略只保存再评估期限，不写第二份 CD。 |
| Dead/StaleLife | 移动/战斗作废；当前 Dead 阶段的选择与加点使用新的合法生命阶段提交，候选不变；等核心真实复活。 |
| PhaseClosed/NoActiveOffer（成长） | 阶段或候选在读取与提交之间变了：丢弃该请求、不计失败，下次决策重读本人投影后再决定。 |
| MatchClosed/LeftMatch/StaleMatch | 停止全部新增意图，最多读取已提交结果；不重建 adapter、补席或重试旧局。 |
| 缺内容/缺服务/非法参数 | 明确阻塞并输出受限诊断；不加载旧 profile、不注入默认战力、不伪造空视图成功。 |

`LDOpponentRequests::Settle*` 按上表把回执归为 InFlight（只查询原 ID）、Succeeded、Failed（有界退避后抑制）、
TargetLost（抑制该观察句柄并释放当前目标）或 Dropped（换命、终局、自身新请求、记录过期，或成长的 PhaseClosed/NoActiveOffer，不计失败）。
陈旧守卫与来源修订也计为有界失败，系统性错配不会形成无界请求循环。
每个移动、攻击、动作与构筑意图自带 `FLDOpponentRetryKey`，终态回执只记到产生它的那把键上。
Navigation 在替换订单通过准入前保留现役订单，adapter 因此分别跟踪现役与候选订单，候选被拒时现役照常可见。
排队订单在下一阶段、已接受动作在 Combat 发布的释放时刻之后，再等 `AI.RetryPolicy.MaxBackoffSec`
仍未终结即计一次失败（动作另发一次 `CancelAction`）；成长请求不可取消，只报告超期并继续查询原 ID。

## 4. SC-09：全生命周期矩阵

| 状态/工作 | 取消/换目标 | 死亡等待与复活 | 离局 | 终局 | 新局 |
| --- | --- | --- | --- | --- | --- |
| 策略模式、目标与路径 | 取消适用订单；不把迟到回执接回当前状态 | 清身体目标/路径；Dead 保留选技能与加点能力；复活从新 BodyRef 重新决策 | 注销本人绑定；其他人离开只撤目标，不增 AI | Close 先锁闸，再取消策略；无新 intent | 全清并绑定新 Match；不复用旧请求或随机上下文 |
| 已发命令/查询 | 已提交只读重放，不退款；待执行依域取消 | 旧身体动作失效；构筑请求重新核对当前 LifePhase | 不接管真人的请求/资源；本人回调不得唤醒已注销策略 | 查询不延寿、不重执行 | 旧 Match 拒绝，不能因缓存清空变新操作 |
| XP/技能/图纸/点数/候选 | 不变；构筑只由原域提交 | 原域保留；可有在局死者的合法延迟奖励、主动选择与加点；复活不重发开局点数或开局机会 | 冻结/退休由原域处理，不当击杀 | 冻结，不继续提交构筑/奖励 | 原域按新局输入初始化 |
| 来源冷却 | 不因取消、换槽或遗忘消失 | 按原时间继续；再次拥有沿用原期限 | 无新释放资格 | 不再释放；原回执不增加效果 | 原域新时间域，无跨局继承 |
| 已释放独立效果 | AI 不回滚，按 Combat/内容生命周期 | 正常死亡继续原期限；不得打中同 ID 的新生命 | 核心/Combat 取消本人未结束效果，身体立即退出 | 关闭写闸后不得继续结算 | 全部换代 |
| 观察/公共 POI 访问记忆 | 失视野立即丢动态目标；无 LastSeen | 死亡无身体视野；静态知识可读；复活新 observer 代次，不沿用旧句柄 | 通道/句柄失效；不跟踪移除身体 | 不保持实时隐藏更新 | 清访问/失败缓存及观察游标 |
| 生命/复活/保护/喷漆与涂鸦 | 进攻意图按已确认规则解保护；受伤不由 AI 清喷漆或守护计时 | 核心复活满血、起始劲/新 Life；死亡已由核心清零喷漆进度并清除本人涂鸦；AI 不改 RespawnAt 或自行声明喷漆资格 | 立即失去战斗/喷漆资格并清除其涂鸦，不为剩余人判胜 | 不复活、不再喷漆 | 核心重建发育期与旗杆状态 |

离局前已提交的死亡事实，其 `RewardBasis.CreditOwnerEligibleAtDeath` 由核心/成长域冻结；
允许原域完成这一事实的 settlement-only 处理，不等于离局者恢复行动资格。
AI 的已注销绑定不能因迟到奖励/回执重新规划、选择、生成身体或恢复效果。

## 5. 实现文件与边界

| 文件（`S\` 指 `Source\Lockdown\`） | 职责 |
| --- | --- |
| `S\Public\AI\LDOpponentPolicy.h`、`S\Private\AI\LDOpponentPolicy.cpp` | 纯值策略：决策帧/状态/结果类型、§6 的优先级、营地成本估计、选择排序、回防紧急度、有界重试。不接 UWorld/Actor/Ledger。 |
| `S\Public\AI\LDOpponentFrame.h`、`S\Private\AI\LDOpponentFrame.cpp` | 把本人成长/战斗投影与受限观察帧映射成策略输入；FName 定义 id 在此经 `CanonicalId` 统一转小写（打包版不保留 FName 大小写），策略内做大小写敏感的精确比较。 |
| `S\Public\AI\LDOpponentRequests.h`、`S\Private\AI\LDOpponentRequests.cpp` | 意图→领域请求的纯构造；回执→InFlight/Succeeded/Failed/TargetLost/Dropped 的纯归类。 |
| `S\Public\AI\LDOpponentController.h`、`S\Private\AI\LDOpponentController.cpp` | 服务器适配器与 OpponentAI 提供者：绑定、组帧、调度、发送、回执消费、受限诊断与请求计数。 |
| `Source\LockdownTests\Private\LDOpponentPolicyTests.cpp` | `Lockdown.Rules.DemoOpponentPolicy.*` 规则测试（§8.1）。 |
| `S\Private\Probes\LDDemoOpponentProbe.h`、`S\Private\Probes\LDDemoOpponentProbe.cpp`、`Scripts\RunDemoOpponentCheck.ps1` | 满员联机场景（§8.2）；probe 只在 Host 核对权威数据，从不回送给策略。 |
| `S\Private\Probes\LDDemoOpponentTelemetry.cpp`、`Scripts\SummarizeOpponentTelemetry.py` | `RunDemoOpponentCheck.ps1 -Telemetry`：一名闲置 Host 加三个 AI 连打多局的遥测（§8.2 末），只读权威数据、不判断对错。 |

AI 参数接入内容域唯一的生成输入/载荷/contract，不新增 AI 私有 JSON 或运行时读 docs。
不新增行为树、黑板资产、AI 专用地图、不同身体、独立 NavMesh 或旧模式开关；
沿唯一 Demo Character、地图、普通导航和内容源。共享头文件与地图变更由原所有者实施。

## 6. 单一自主策略

### 6.1 状态与优先级

采用固定优先级选择器及少量在途子状态。每次从高到低选择**当前可执行**的意图；
等待一个回执不阻塞其他合法通道。没有“选择另一人格/旧居民模式”的回退路径。

| 优先级 / 状态 | 授权输入、进入条件与动作 | 退出、重规划与禁止捷径 |
| --- | --- | --- |
| P0 Stopped | Results/Closing/Faulted、非运行阶段、离局或绑定失效；停止新提交 | 只有核心发布合法新局才重新绑定；不是不断尝试旧局命令。 |
| P1 DeadWaiting | 本人 Dead/RespawnAt、当前候选与未分配点数；等待身体，并在当前 Dead 阶段选择主动（有空槽即学到）、学图纸、选牌、装牌与加点 | 新 BodyRef 到来才恢复身体动作；不移动、攻击，不恢复旧路线。 |
| P2 Disengage | 本人危险阈值、可见接触压力、自保 readiness、静态可达地面；停止继续投入，合法自保/撤离 | “正在脱险”才保持最高空间优先级：低于 RetreatHPRatio 且没有就绪自保屏障时，脱险压过回防，也不向公开喷漆者或涂鸦主人冲锋；回到这条线以上后的回血等待让位于紧急回防。已在喷漆区内参与争夺时留在区内对付区内对手，不因独自喷漆被打断、回防不再紧急就跑出区：一碰就走只会让持有者接着暂停的喷漆进度继续；只有跌破止损线又无就绪自保时才撤离。涂鸦主人恢复中（`bRecovering`）且未跌破止损线时仍回到并留在喷漆区内，因为守护只在他独自在区内时计时、离区即暂停，冻结期间走出区还会让守护清零；跌破止损线又无就绪自保时才脱离，因为他死亡会清除涂鸦。撤离选一个远离可见威胁、终点在其视野半径外的公共地面点，并走完这一程，只有到达或不再远离时才重选；威胁失去视野后也不原地停下，追兵忽隐忽现也不改道，到达后看不到威胁、又没有可走的躺平角时原地等待回复（`DemoOpponentPolicy.RecoveryEscapeKept`）。区内有合法余地时先在区内移动，不机械跑出区。 |
| P3 DefendOrHold | 自己正持有喷漆进度（独自喷漆，或争夺暂停中的持有者，原因 `HoldCapture`）；自己是涂鸦主人且未跌破止损线（含恢复中；守护只在主人独自在区内时计时）（`GuardGraffiti`）；或公开的对手喷漆到期/对手守护获胜时刻即将达到自身回防预算（`PublicCaptureUrgent`，有对手涂鸦时为 `BesiegeOwner`）；走公开喷漆区入口、处理当前可见威胁 | 压过新的打野；阶段、CaptureEpoch、有效资格、路线或风险改变时重评；公开喷漆者或涂鸦主人的 ID 不能直接变成单位目标，只能匹配可见单位的授权 ContenderId。 |
| P4 Choose（构筑） | 没有在途构筑请求时，每次决策按 §6.4 的顺序至多提出一个构筑请求（原因 `ChooseOffer`），与同节拍的身体意图并行：选择、选牌、学图纸、装牌，休息中的替换、洗点与商店，最后加点。休息中只在构筑开放步骤（构筑/兑现）做构筑决定；无事可做时训练（§6.4）；开局休息 R0 内另要等到本 AI 的 `OpeningPickAtMs`（adapter 每局从服务器随机流抽一次，R0 开始后第 1–4 秒间）之后 | 不冻结、不替换发育期正在进行的合法运动或紧急反应；成功后才读下一组，冲突不重抽。 |
| P5 Engage | 当前合法可见目标、本人可用资源/动作、未超投入预算；Nav 攻击订单与 §6.3 主动 | 失视野、无效目标、危险、回防、超预算或生命变化时退出；不读对手内部战力来保证击杀。 |
| P6 Contest | 旗杆已开放，无更高可执行目标；自身风险门允许进入公开喷漆区（`ContestCapture`）；旗杆上是对手涂鸦时为不紧急的围攻/改漆（`BesiegeOwner`） | 到位后独自喷漆或争夺；不能有效入场时换静态合法入口或继续成长/探索，不用保护者骗争夺人数。 |
| P7 Farm | 没有更高目标；根据公开档位与自身状态选择营地，观察到合法成员才攻击 | 无合法目标、止损、预算到期或紧急回防即结束。Returning 不等于无敌；只继续 §6.2 允许的当前射程内攻击，不新起回营追逐。 |
| P8 Explore | 其他目标不可执行；选择未被失败抑制的公共 POI | 新合法观察、资源/Offer/公共喷漆与涂鸦变化、订单终态触发重评；不为移动而扫描全图身体。 |

一个 AI 同时至多持有一个当前空间订单、一个对应身体动作控制上下文、
一个成长构筑变更在途请求；导航与 Combat 的关联仍由原域管理。
普通节拍最多提出一个新的身体意图和一个不冲突的构筑事务意图，不能排一串未来技能。
新 Offer 不阻塞脱险，已提交的构筑请求也不能被高优先级目标反向回滚。
只有相关条件变化或真实期限到达才重评；保持仍合法的已有订单，不每节拍重发同一目标。

核心终局、离局、死亡和观察撤销按通知立即清理，不等待常规规划周期。
长帧后最多对最新已发布帧作一次规划，不“补跑”漏掉的历史 AI 决策。
每个 AI 由各自控制器按 `AI.DecisionIntervalSec` 独立决策；
它们不共享黑板/战术目标/视野/资源，也不因对手是 Host、真人或 AI 而改变仇恨。
纯平局以本局本人绑定与公共稳定键形成稳定顺序，不查询其他 AI 的计划或隐藏状态。

### 6.2 营地、目标与公开回防

**营地。** 原始构筑优先低档；开局主动选择与开局点数也算原始构筑，升过级后才把高档加入主动尝试候选。
这只是机器人的保守偏好，不限制真人进入高档。高档入口先查 HighCampEntryHPRatio（精英营地沿用高档的入场与保留规则，成本估计单独计算），
各档都要评估本人实际构筑的保守打野成本：**预测清营后的剩余 HP 比例**
须达到相应 HighCampReserveHPRatio/LowCampReserveHPRatio。
它们是入场成本门槛，不是当前 HP 止损线；实际受压/危险退出继续使用 RetreatHPRatio、
当前可见压力及 EngagementBudgetSec，不把同一参数改作另一个规则。
排除无完整路径或失败抑制的 POI，优先继续有效访问，其次当前可见的合适成员，
再按访问新鲜度、合法接近成本和稳定键选择公共地点。

`EstimateCampCost` 只做一次有界、匿名的公共目录成本计算：以本人当前 HP/MaxHP、
真实派生 AD/护甲/AS 与普攻 readiness 为输入，按目标公开档位的 CountPerCamp、
HP/Armor/Attack/AttackSpeed/AttackWindupSec 采用满营地模板。
模板是假设的保守上限，不声明实际隐藏成员存活/残血/已刷新；不靠未观察死亡减成员数。
通过 §3.3 的 `LDTactical::EvaluateDamage/EvaluateBasicTiming` 消费同一
`FORMULA.Damage/Resistance/AttackIntervalMs/AttackWindupMs` 原语，
根据各成员所需普通主击数、预计击杀时刻以及存活窗口内可到期的反击数累计成本，
不另抄伤害/减伤/取整算法。首个确认命中后才起存活成员反击前摇，同刻到期反击计入，
不靠“玩家先结算”抹掉成本。三种野怪只有普通物理攻击，没有待猜测的技能/CD。

这是站桩成本模型的保守决策用途：
不计暴击、技能/护盾、吸血与回复的额外收益，不运行真实效果或消耗随机流。
预计投入超过 EngagementBudgetSec、预测 HP 耗尽、单击无有效伤害或不能得到有限清营结果时，
停止并输出 NotFeasible；缺公式/非法输入则显式 EstimateUnavailable，不补最低伤害或成功估计。
模型只适用于合法互攻距离、无第三方/额外走位的假设，不是实际伤害结算或必胜保证。
估计不每次攻击按“完整新营地”重算；实际战斗按当前危险/预算退出，
构筑变化或重新开始一次营地访问时才重新评估，不形成另一套世界 Tick 或持久怪物状态。

到地点未见目标只记录“本次未发现可见可打目标”，不能声明整组已清空。
ExploreRevisitIntervalSec 是自己的探索节奏，不是从 CAMP.RespawnSec 推算隐藏倒计时。
同一实际构筑下失败的高档尝试，不因复活满血或无关 revision 变化立刻重演；
其 POI 失败记录等待真实能力改善后才解除。不缓存某个隐藏成员的残血状态。

未受攻击营地不主动进攻路过者，策略不能把“接近营地”当成已发生伤害或敌方仇恨。
主动打野前将**可能的同组反击**作为公开营地规则纳入保守入场/止损，
但不查询隐藏成员坐标、当前仇恨或把同组响应变成共享感知。
Returning 只从同一授权观察的只读 `camp_returning` 状态提示识别，不访问营地运行表。
当前目标进入 Returning 后仍可受伤/控制/被杀：
若它仍合法可见、在本人当前攻击范围内，且止损/投入预算允许，则可完成合法攻击；
否则取消追击并结束访问，不新建回营追逐订单。
这只是 AI 的止损偏好，不给真人增加“不能追杀回位者”的世界限制。
回位者不回头追击也不等于回位路线成为安全区，其他参赛者仍可能攻击。
存活成员到家重置生命、临时状态、仇恨及技能 readiness 属于世界的营地规则；
AI 只能消费获准表现，不能推断隐藏的到家时刻或借回位索要奖励/复活已死成员。

**目标。** 当前授权候选中，优先正在阻止自己喷漆、守护或回防的可见威胁，
其次仍合法且未超预算的当前目标，再按可见 HP、合法接近成本、稳定句柄排序。
低 HP 只影响偏好，不声称下一击必杀；不同单位不适用字段不当作零生命或零等级。
EngagementBudgetSec 从这次连续目标/营地投入开始计，普攻 tick、改发 Order、
普通快照或被自己触发的 revision 不重新计时。受控导致不能动时消费领域原因，
不将其误判成几何卡路。

失视野终结 AttackUnit 或正在追敌的 AttackMove，不恢复旧 A 地面目的地；
重新可见也不续旧单。只有世界 `FLDObservedUnitTermination` 的授权可见死亡事实，
与未取消原订单的 handle/epoch 匹配后，才可由操作域继续原 A 订单的既定搜敌规则；
AI 只消费该订单的正式回执，不自行查死亡表。
Conceal/stale/FullSetRemoved/TargetUnavailable、离局或隐藏死亡绝不冒充可见死亡。
AI 可以根据新的公开喷漆/涂鸦状态或静态 POI 决策发**新明确订单**，但不能把旧目标坐标改装成地面追踪。

**回防。** 对手独自喷漆时用公开 DueAtMs，旗杆上是对手涂鸦且守护在运行（主人独自在区内）时用公开 GuardDueAtMs；
守护暂停（主人不在区内）或冻结时没有获胜时刻，不产生回防紧急度。有到期时刻时
与服务器当前时间得到剩余量，再与本人到静态合法喷漆区入口的完整路径/当前移速所需时间、当前控制/保护剩余限制、
AI.CaptureReactionBudgetSec 的余量比较。被零移速或控制阻止时不能除零或假定立刻到位；
预算是保守估计，不是另一个喷漆/守护截止或额外等待时长。
公开期限逼近这个预算就停止新增刷野投入，不能为“最后一只怪”故意拖延。

风险门只使用本人 HP/受压、当前可见接触压力、可达入口/退路与本人可用能力。
在止损线以下又无可用自保/有效入场路线时先脱险或换公开入口，不要求必死冲锋；
无法阻止时允许失败，不靠额外血量、传送或隐藏目标定位补救。
保护者可以合法移动接近，但不能自报有效争夺；进攻解除或真实到期由核心更新资格。

**喷漆、守护与围攻。** 争夺只暂停持有者的喷漆进度，离区、死亡、离局或被定身/眩晕/嘲讽/击退才清零，别人不能继承。
因此 AI 持有喷漆进度时，无论独自喷漆还是被争夺暂停，都留在区内迎战，不为追杀离区目标轻易放弃；
只有跌破止损线又无就绪自保时才撤离，并接受真实清零。争夺消耗次数用尽也不让暂停中的持有者放弃进度。

AI 自己是涂鸦主人时，只要未跌破止损线（含恢复中），就回到并留在喷漆区内与区内对手交战：守护只在主人独自在区内时计时，
主人离区、区内无人时守护暂停；主人站在区内也会让任何人都无法独自喷漆；
主人以外有人进区时守护冻结，冻结期间主人从区内走出时守护暂停在原值。只有跌破止损线又无就绪自保时才脱离战斗，
因为他死亡会立即清除涂鸦（暂停或清零好过丢涂鸦），回到止损线以上后再回区续计。其他每个 AI 都回应对手的涂鸦（`BesiegeOwner`）：
在喷漆区外时，若可见的涂鸦主人在本人搜敌半径内，不论他站在哪里都去打他；否则走进喷漆区改漆，进区本身就冻结对手守护。
在喷漆区内先打公开的喷漆进度持有者，再打对手涂鸦主人，最后打区内其他对手。
AI 只能凭可见单位的授权 ContenderId 与公开 Occupier/Owner 相等来认出喷漆者或涂鸦主人，看不到就按普通可见对手处理。
争夺期间没有喷漆到期时刻，对手守护也因区内有人而冻结（`GuardDueAtMs` 为 0），不产生回防紧急度，按普通争夺或围攻进入；
对手主人不在区内而守护暂停时同样为 0，按普通围攻或进区改漆处理。

打断喷漆时，目标正是公开的独自喷漆者，第二档控制除定身外还会使用就绪的眩晕，或在嘲讽半径内使用自身嘲讽；
这些硬控在施加瞬间清零他的喷漆进度。受伤、施法或区内移动本身不由策略重置进度；达到显示期限也不自行判胜。

**恢复。** RetreatHPRatio 判断受压脱险，ResumeFarmHPRatio 是脱险后重新主动刷野的门槛。
恢复等待不禁构筑请求、重新评估公共回防或低风险移动；不要求劲才能普攻。
没有有效回复来源时，不永久等“回满”，转可达探索或合法喷漆/围攻机会。
躺平角是公开的快回地点，但不是安全区：恢复中且没有可见威胁时，走向最近的、未被失败抑制的躺平角；
站进躺平角后一直留到生命回满才结束恢复（不在 ResumeFarm 线离开），可见威胁照常触发撤离，紧急回防照常优先
（`DemoOpponentPolicy.DormRecovery`）。对手正独自喷漆或守护其涂鸦时不走向躺平角，就地等待自然回复：走向地图角落会让公开回防紧急度每次决策都翻转。
躺平角判定与权威一致：本人脚点在公开 `DORM.RadiusCm` 圆内。
自保/治疗能力必须真的持有、可用且走同一 Combat 提交。

天降外卖读取公开对局视图中当前外卖的落点（只有已公开或已落地时才有），与真人看到的相同；
后续落点由服务器随机决定，AI 与真人一样只能在公告后反应。
没有恢复需求、不在交战中的营地里时，走向该公开落点，站上点后原地等待落地；
它排在回防、喷漆、守护涂鸦、围攻和区内争夺之后、普通喷漆与打野之前，模式 `Airdrop`、原因 `ContestAirdrop`（`DemoOpponentPolicy.Airdrop`）。
抢外卖不是争夺旗杆：从外卖处止损撤离不计为一次喷漆失败，不会让 AI 失去回防资格。
拾取由权威按脚点判定，AI 不另行提交请求；自己是否抢到来自本人视图（`AirdropClaimedSerial`），公开视图不说是谁。
外卖落地 30 s 无人领取即从公开视图消失，AI 随之不再把它当目标。领取会同时恢复生命、获得劲并提供短暂移速加成；
恢复中的 AI 在没有更高优先级威胁或退避目标时，可把公开外卖作为恢复目标。

标记营地读取公开对局视图中开放（已标记、未领走、未过期）的营地标记，与真人看到的相同（`FLDOpponentPublicMatch::MarkedCamps`）。
打野选营地时已标记的营地排在最前，但仍受原有的进入门槛、成本估计、失败抑制与访问记忆约束（低于高级营地进入门槛的 AI 不会因标记去打高档或精英营地）；
领取由权威按清场死亡的击杀归属判定，AI 不另行提交请求。外卖仍排在标记营地之前（`DemoOpponentPolicy.Airdrop`）。

上热搜读取公开对局视图中上热搜者与模糊圆心，与真人看到的相同；上热搜者若不可见，AI 不知道其实时位置。
本人等级不低于 `LEADER.MinLevel`、上热搜者的公开等级不比本人高出超过 `AI.LeaderHuntMaxLevelGap`、生命不低于进入高级营地的门槛、不是上热搜者本人、不在交战中的营地里时，
走向圆内离圆心最近、未被失败抑制的公开地点；看到上热搜者后由普通交战分支接手。
走到圆内地点仍看不到上热搜者时，这次到达记入访问记忆，最近到过的圆内地点在重访间隔内都不再下单：不会每次决策在到达瞬间重发，也不会在走向下一个地点途中折回刚到过的地点；圆内地点都刚到过时回到打野。
它排在空投和普通喷漆之后、打野之前，原因 `HuntLeader`（`DemoOpponentPolicy.HuntLeader`）。

画饼老板 Boss只在合法视野内可见，与其他单位相同；AI 按其公开生物编号认出它，始终把可见的 Boss 计为威胁。
本人脚点在 Boss 的索敌带（`aggro_radius` 加它的 `attack_range`）内时，走向背离 Boss 的公开地点，原因 `AvoidBoss`；
这排在喷漆、守护涂鸦与围攻之后、追打对手之前；恢复中的 AI 照常以恢复撤离。AI 不主动攻击 Boss（`DemoOpponentPolicy.AvoidBoss`）。
走出索敌带后，这次撤离继续走到它的地点，直到走到、订单结束或 Boss 离开视野，不在带边折返；
否则每个决策节拍都会在撤离和把它带回 Boss 身边的原事务之间来回下单（`DemoOpponentPolicy.BossEscapeKept`）。

### 6.3 普攻与每一个主动来源

持续普攻只由 Nav AttackUnit/AttackMove 的 AttackCycle 驱动。
接近距离使用本人当前 `STAT.AttackRangeCm`，不是初始 UNIT.BasicAttackRangeCm。

对真正绑定的 Q/W/E 与 D 枚举自身 QueryAvailability，固定考虑：
有意义的合法自保/恢复 → 必要合法机动 → 可见威胁控制 → 合法伤害 → 保持普攻/移动。
优先级只决定策略先选哪个动作，不给效果追加类型或数值；SourceRef 和真实目录决定动作。
每个 Q/W/E 来源读该主动唯一的一组效果值（`ResolveSource`，按本人该技能的分支取值；数值随拥有者等级与技能等级成长由 Combat 结算）。

**劲、爆劲与闪避**（Issue #287 规范 §13）：

- 劲不足的来源由真实 readiness 报 `MomentumShort`，策略只等待或换动作，不另算劲。
- 伤害档（第 2 档及以上）选中技能时，若本人劲不少于 `OVERDRIVE.Threshold` 且目标可见生命高于 40%，意图带 `bOverdrive`：`ALDOpponentController` 先经 `SubmitCharge` 开始蓄力，`OVERDRIVE.HoldSec` 后再提交带爆劲的释放（`FChargeTrack`），与真人同一服务器校验。蓄力只属于发起它的生命；策略改为其他意图（保持、停止、移动、换技能）时经 `AbandonCharge` 通知服务器结束蓄力；本人战斗视图的 `ChargingEntry` 显示服务器已结束蓄力（控制或劲不足）时，下次重新发起并重新计时。
- 观察帧的 `Warnings` 列出可见的敌方预警（`ReadWarnings`：未结算的地面落点、陷阱、扇形与营地预警，只有公开轮廓与剩余时间）。D 就绪、某个预警剩余不超过 450 ms 且本人脚点在其轮廓内时，按 `AI.DodgeChance`（每个预警对每个 AI 一次稳定掷骰）向侧面或背离中心冲刺（`DodgeIntent`），优先于其他身体意图；完美闪避由服务器判定，AI 不自报。
- 观察帧的 `Grounds` 是视线内的公开地面状态（`ReadGrounds`，主人座位与本人座位不同即为敌方）。野外步行订单发出前，`ALDOpponentController::RouteAroundGrounds` 用注册表的真实完整路径比较直走与绕开每个挡路敌方地面状态两侧的一个绕行点（`GroundDetours`，足迹外 `GroundDetourMarginCm`），路线落在敌方地面状态里的长度乘 `AI.EnemyGroundPathCost`（`GroundWeightedCm`，每 25 cm 取样）；绕行更省时先走到绕行点，这一段完成后控制器自行发出最后一段（再次比较），策略始终只看到这次步行，到达仍以最后一段完成为准。休息步行与追击（`AttackUnit`，由导航自行寻路）不绕行。被击退或推进敌方地面状态时，D 就绪即背离其中心冲出（`LeaveGroundIntent`）。
施法用途全部由 typed 效果推出（`ReadEffects`），不按技能名称或构筑配方分支。
同一时刻只送当前动作系统可接收的一项；前摇、控制、资源、来源和占用均不得绕过。
成功 handoff 后旧运动不由 AI 私下恢复，按新合法观察重新决策；失败则消费真实回执。

| 档位与效果形状 | 当前目录中的条目 | 目标与使用规则 |
| --- | --- | --- |
| 自保：Self 且带护盾或解除定身 | `active_not_mad` | 需要防护，或 Root 妨碍脱险/进喷漆区时优先使用；不能越过沉默、眩晕等施法限制。不治疗、不免疫伤害，不清 CD/死亡或重新保护。 |
| 机动：DirectionDash | `active_bail_out`、`active_apology_slide`、D 冲刺 | 撤退与抢旗杆用已选方向；追击（`ChaseDirection`）用该来源自己的冲刺距离判断：目标在“普攻距离 + 半个冲刺”之外、“普攻距离 + 一个冲刺”之内才朝其当前可见脚点冲刺。带伤害的冲刺（`active_apology_slide`）在允许伤害且目标在冲刺距离内时也朝目标冲刺。`active_bail_out` 只在这一档使用，不因其强化普攻而在伤害档施放。 |
| 控制：带定身；或目标是公开的独自喷漆者时带眩晕/沉默 | `active_pants_down`；打断时另有 `active_crotch_grab`、`active_account_banned` | Unit；当前可见威胁的观察句柄，射程内才施放，执行时重查；不是凭公开喷漆者或涂鸦主人 ID 施法。 |
| 控制：Self 嘲讽 | `active_stare_down` | 只用于打断：目标是公开的独自喷漆者且在嘲讽半径内。 |
| 伤害：Unit | `active_pants_down`、`active_crotch_grab`、`active_elbow`、`active_account_banned`、`active_unplug` | 当前可见目标在射程内时施放；击退、缴械等附带效果由 Combat 结算，AI 不另行规划。 |
| 伤害：DirectionProjectile / DirectionCone | `active_snark_bolt`、`active_open_mic` | 用当前合法可见目标形成方向，归一化/几何由 Combat 重验；不随隐藏目标持续修正。 |
| 伤害：GroundPoint 区域 | `active_quick_meeting`、`active_calculus_drop`、`active_flyer_trap` | 以射程内可见目标的公开脚点为落点（`FLDOpponentBodyIntent.Point`，请求构造为 `ELDCombatTargetKind::Point`）；落点由 Combat 夹到射程。 |
| 伤害：Self 区域 | `active_seethe` | 目标在其自身区域半径内才施放。 |
| 伤害：Self 强化普攻（无位移） | `active_triple_combo` | 目标在本人当前普攻距离内才施放，随后由普通攻击订单消费强化。 |

D 键通用冲刺是每名参赛者的固定来源，AI 经同一 owner view 的 `ELDCombatEntry::Dash` 读到它
（`ELDOpponentSourceSlot::Dash`），位移距离取权威给出的 `RangeCm`，不在策略里重算；
冷却、定身与起步阻挡都在 Combat 重验，`bReady` 只取权威的可用原因与 ReadyAt。它在“必要合法机动”这一级使用：

| 时机 | 方向与条件 |
| --- | --- |
| 追击 | 正在交战的可见对手参赛者（不是营地野怪）离自己超过“普攻距离 + 半个冲刺”且不超过“普攻距离 + 一个冲刺”，本人未在撤退、未定身：朝其当前可见脚点冲刺，落点在普攻距离内。 |
| 撤退 | 脱战撤退（低血且有可见威胁）时，朝已选的公开撤退地点冲刺。 |
| 抢旗杆 | 公开的对手喷漆到期或对手守护获胜时刻让回防变得紧急时，朝旗杆冲刺；不紧急的进区不花冲刺。 |

验收见 `Lockdown.Rules.DemoOpponentPolicy.InnateDash`；满员局中的实际冲刺由 `RunDemoOpponentCheck.ps1` 记录为报告项（§8.2）。

角色主动按真实 TargetMode 进入同一执行通路；Unit 使用授权句柄，
Direction 使用当时获准方向，GroundPoint 使用当时可见目标的公开脚点，Self 检查本人状态。
尚未提交、依赖已撤销观察的瞄准计划作废，已经释放的独立效果不会持续修正隐藏目标。
验收见 `Lockdown.Rules.DemoOpponentPolicy.V7Activations`。

来源失效后等待正式新绑定/ReadyAt，不自己清冷却或把换槽当新来源。

### 6.4 持久选择与构筑

构筑意图与真人使用同一 `FLDBuildCommand`，同一时间只有一个在途（`bBuildChangeInFlight`）；每次决策按下列顺序至多提出一个，
每一步都先用成长域的同一闸门（`LDProgression::CommandGate`/`InstallGate`，按阶段与本人生死）判断现在是否允许：

1. **选择**：有当前候选且允许选择（休息的构筑开放步骤、野外各段含交战中，存活或死亡）时，从候选中按下面的主动偏好选一个，
   总是带 `bLearnIfPossible`：服务器在有空槽时学到最低空槽，Q/W/E 已满时存为图纸。
2. **终局改装**：R3 有未决终局候选、没有未决主动候选且成长闸门开放时，从候选中均匀随机选一张（由服务器随机的候选身份决定，不看构筑）。
   这次限时选择先于休息服务与商店整理，避免真人提前准备结束窗口后漏选。
3. **选牌**（spec §13）：有当前牌组时（与主动选择同闸）用 `SelectCard` 选一张：每张牌的分数是 `AI.CardVocabScore` × 它与本构筑
   （已装的牌与已学技能，`BuildVocab`）共享的词汇键数，加一个由参赛者、对局与牌决定的确定性小抖动；本局有**质变目标**时
   （开局时以 `AI.BreakoutChance` 的概率从九个 P2 样板里抽一个，`DrawBreakoutGoal`，确定性哈希），目标样板里的牌再加 `AI.BreakoutCardScore`。
   P5 之前 AI 从不跳过牌组，也不重抽。选中的牌在有空格且有合法位置时同次装上（`CardTarget`：形式的唯一位置；技能牌装在与它共享
   词汇最多的已学技能上，同分按 Q/W/E；Y25 取第一种元素），否则进牌库。
4. **学图纸**：有图纸、有空的 Q/W/E 且允许学习（休息的构筑开放步骤，或野外各段，存活或死亡）时，按同一偏好选一份图纸学到最低空槽。
   Q/W/E 已满时不学，图纸留待休息时的替换。
5. **替换**（只在休息构筑窗口，且没有未决候选时）：Q/W/E 全满且有图纸时，每个已学主动只以另外两个已学主动覆盖的用途
   按主动偏好排名，取排名最低的一个；图纸以同样那两个主动覆盖的用途排名，最好的一份严格排在它前面才用同一 `ReplaceActive`
   命令替换它（`SelectReplacement`）。被替换的主动被遗忘、不回到图纸，所以每次替换都用掉一份图纸，替换必然结束；
   装在被遗忘主动上的技能牌由成长域同次退回牌库，由下一步装回。
6. **装牌**：牌库里有牌、有空格时，按序号把第一张有合法位置的牌装上（`SelectCardInstall`，与学图纸同闸，野外与死亡中也可以）；
   AI 不搬动或卸下已装的牌，容量满时不按价值替换。
7. **洗点**（只在休息构筑窗口）：某个已分配属性不在下面加点的相关属性里（例如替换拿走了最后一个 AP 主动）时洗点一次（`WantsRespec`），
    下一步重新加点；加点只给相关属性，所以已学主动不变就不会再洗。
8. **商店**（中场休息 R1–R3 的构筑窗口、商店已进货，spec §13）：未分配点数不少于技能点价且本局还能买时买技能点，否则买买得起、未售出的
    牌里得分最高的一张（与选牌同一打分，`SelectShopBuy`，`ShopBuy`）；从不买技能、不刷新。牌三选一里没有一张与构筑共享词汇时，
    以 `AI.CardSkipChance` 跳过换属性点（`WantsCardSkip`，按牌组确定性抽取）。
    **升技能**：有技能点时，`PickRankUp` 选按本人当前攻击力/法强算一次释放直接伤害最高的已学技能（平局取靠前的槽），分支按词汇选（spec §13）：下一步的两个分支里，与已装牌共享词汇键更多的那个，同分取 A（或这一对的第一个），经 `RankUpSkill` 提交；对局中随时可用。
9. **加点**：有未分配点数且允许加点（休息的构筑开放步骤，或野外各段，存活或死亡）时，一次分配全部未分配点：
   每一点给相关属性中当前点数最少的一项，平局按五维设计顺序（体格、脸皮、力道、脑力、身手）；相关属性固定含体格与脸皮，基础普攻或任一已学主动有 AD 系数时加力道，
   任一已学主动有 AP 系数时加脑力；身手不在其中。

`LDOpponentPolicy::DecideFollowUpBuild`（DEV 构筑评估也用）与 `Decide` 的构筑步骤相同，但不做第 1 步：主动候选留给调用者。
各规则都没有新增可调数值，只用已批准的目录参数与确定性的目录顺序。

主动偏好（`SelectActive`/`SelectBlueprint`/`SelectReplacement` 共用）：先选能补上已学主动与图纸都没覆盖的用途（交战或自保）的主动；
再按目标形式 Unit > GroundPoint > DirectionCone > DirectionProjectile > Self > DirectionDash；最后按目录顺序。
用途由 `LDOpponentPolicy::AbilityUsage` 从已批准 typed 效果读取，只分三类：目标不是自身的伤害/控制为交战，
自身护盾或自身解除定身/减速为自保，`Dash`/`DashContactRefund` 为机动；偏好只比较交战与自保两类，
不靠名称、未学习技能的私有状态或临时战力公式推断。没有重抽；已学主动只由第 5 步在休息中替换，AI 从不遗忘主动。

休息中只在构筑开放步骤产生构筑意图（这是 AI 策略的取舍；服务器在返回倒计时也受理自己的构筑命令），开局休息 R0 内还要等到本 AI 的 `OpeningPickAtMs`；
adapter 每局只从服务器随机流抽一次这个时刻，策略不读隐藏技能，不看未来随机结果。
洗点、替换与商店都不需要走到任何地方，直接经原成长事务提交。其余身体意图为下面的训练。

**训练**：休息构筑窗口内到了 `OpeningPickAtMs`、没有可做的构筑也没有在途构筑请求时，
存活的 AI 用生产交战（§6.3 的主动顺序，然后普攻）攻击本人获准观察帧中的训练目标（模式 `Engage`、原因 `Train`，`DemoOpponentPolicy.Training`）。
共享训练目标只按 HUD 的同一规则（`LDHUD::ObservedContenderName`）认出：休息中本人观察帧里可见、ID 不在公开房间名单
（`FLDLobbyView` 成员，adapter 只在休息中读取）上的参赛者类单位（`LDOpponentPolicy::TrainingDummy`）；本人不在名单上时不认任何单位，
训练目标按当前观察句柄从小到大选择，不保证对应本人的名单槽位；受保护或已被失败记录抑制的单位不打。
获准观察代理呈现同一真实目标的生命、位置、受击与控制结果，不设公共静态替身；目标可被任一休息室参赛者攻击，从不死亡、不给经验或奖励，训练消耗的资源与冷却不带回野外。
构筑窗口关闭（返回倒计时）时仍在进行的训练订单
由一次 Stop 结束，已受理的动作先完成。
发育期和旗杆开放后，构筑意图与身体意图在同一决策里并行：AI 不为构筑停止交战、回防或撤离。
交战中与死亡等待中都按上面的顺序选择、选牌、学图纸、装牌与加点；替换和休息服务留到休息构筑窗口。

条款取自本人最新成长投影：Offer/OfferRevision、牌组、选项或图纸与槽位、点数、牌，以及 Build/LifePhase。
不同请求不得竞争消费同一机会；冲突后重新读原 Offer 和本人构筑，重新决定，
不改卡、不跳过当前候选、不看未来随机结果。成功后才处理下一组，满级后仍可处理已积累的机会与点数。
死时使用同一顺序但以当前 Dead 阶段提交（选择并学到空槽、学图纸、首次安装、加点）；属性改变由统一投影保持资源比例，不复活/治疗。
失败按种类记入失败记录（选择另以 OfferId、学图纸与替换另以图纸、选牌以牌组为键），
指纹是本人 BuildRevision 与当前候选修订，构筑或候选变化后重新计次；
PhaseClosed、NoActiveOffer 的拒绝只丢弃该请求、不计失败（§3.3）。

### 6.5 有界重试与可观察决策

失败记录按“公共 POI 或当前业务目标 + 原因类别 + 相关本人条件指纹”保存，
目标撤销时不留下单位位置/血量历史。固定地点有限；只保留当前观察和在途请求，
不随无限时长对局积累全单位/全回执日志。
RetryPolicy 的 MaxAttempts 包含首次提交，退避逐次增加且不超过 MaxBackoffSec；
次数到界后明确进入目标抑制或等待原域终态，不偷偷无限生成新 token。
争夺旗杆也是有界投入：一次回防、争夺或围攻以跌破止损线撤离告终，就记为旗杆在当前构筑下的一次失败；
用满 MaxAttempts 后不再回应旗杆，直到等级或选择改变构筑，以免较弱一方反复进区又被打出。
自己独自喷漆或自己是涂鸦主人时不记这类失败。
导航在准入时以地面或路径理由拒绝行走（移动或追击）且换了一个目标仍被拒时，问题在身体所站之处而不在目标：
这类拒绝另记在“本人行走 + 所站位置（精确到厘米）与生命”上，同样按 RetryPolicy 退避，用满后在原地不再发起新的行走，
直到身体移动或复活；任何一次行走被受理即清零（`DemoOpponentPolicy.WalkRefusals`）。满员场景报告逐个 AI 列出准入时被拒的订单（`orders_rejected`），它们不计入“请求有界”的被拒订单：身体站在无支撑处时导航拒绝一切行走，是世界问题，AI 已不会因此循环下单。

无路/无进展先消费导航域的有限重算和失败，AI 才换另一个合法公共接近点/POI，
不叠加第二套卡路计时。资源拒绝等 readiness/相关资源变化；控制拒绝等对应限制解除；
Offer 冲突等同域终态或相关修订；普通帧更新不刷新次数。
回执不明只查询原 ID，查询预算耗尽后请求仍算在途，决策日志带 `problem=build receipt overdue`，并允许无冲突脱险，
不能宣称成功或重新提交一次；收到终态/拒旧水位并重新同步后才解除该等待。

每个决定输出受限原因码及所用输入版本，至少区分 NoLegalTarget、Recover、
PublicCaptureUrgent、HoldCapture、GuardGraffiti、BesiegeOwner、ContestCapture、TargetSuppressed、
ProviderUnavailable、Stopped。日志可解释“为何继续发育/为何回防/为何失败”，
但不得记录从未授权给该 AI 的隐藏坐标或内部来源，再反向作为决策输入。
每局关闭时，每个 AI 另输出一行 `LD_AI_SUMMARY`，汇总本局送出的订单、Stop、动作、构筑请求（`builds`）次数，
当场被拒的次数，以及争夺暂停下的坚持/争夺决策计数和打向对手涂鸦主人的攻击订单数（`owner_attacks`），
末尾 `objectives` 为转向野外目标的决策次数。它用来核对等待、拒绝和重试是否有界，
只是证据，不是策略输入。

### 6.6 野外目标与死亡选点

AI 只读公开对局视图里的野外目标（`FLDPublicMatchView::Objective`：种类、阶段、`mini_flag_*` 点或旗杆、圆心与半径），不读服务器抽签或未来时刻。
`LDOpponentFrame::ReadPublicPOIs` 把 `mini_flag_*` 锚点作为公开地点（种类 `MiniFlag`）；外卖柜不是静态 AI 地点。
正在出现的外卖柜（公开 `PublicMatch.Lockers`）每帧作为种类 `Locker` 的地点加入，按注册锚点取地面。

1. **去目标**：目标预告或开放中、本人生命不低于 `AI.ObjectiveEntryHPRatio`、离目标地点（小迷你旗点，擂台为旗杆）不超过 `AI.ObjectiveReachCm`、
   不在自己开打的营地战中、去该地点的路径没有被失败记录抑制时，走向目标（模式 `Objective`、原因 `ContestObjective`）；
   到了圆内中心一带就停下等开放或喷漆。它排在天降外卖之后、旗杆争夺与刷野之前；圈内看见对手时由普通交战分支先处理。
2. **藏起来的东西**（排在野外目标之后、上热搜追猎与刷野之前，同样不离开自己开打的营地战）：
   生命不低于 `AI.ObjectiveEntryHPRatio` 时，先打 `AI.ObjectiveReachCm` 内可见的二手贩子（原因 `ChaseDealer`，普通攻击订单），
   再走向这个距离内最近的外卖柜，站到柜子半径一半以内就停下读条（原因 `OpenLocker`）。
3. **死亡选点**：死亡时服务器立即替 AI 选候选出生点（`ALDGameMode::ChooseSpawnForAI` 调 `LDFieldPacing::NearestCandidate`），
   选离最近公开目标（开放或预告中的野外目标中心、旗杆）更近的一个；不经策略，也不读隐藏信息。

`DemoOpponentPolicy.Explore` 覆盖外卖柜与二手贩子；`DemoOpponentPolicy.Objective` 覆盖去目标的条件、到点停下、过期与已决目标不去、未知地点不去、擂台走向旗杆和天降外卖优先。

### 6.6.1 终局改装

终局里 AI 只读公开对局视图的持卡表与旗杆状态（`FinalMods`、`PoleCentreCm/PoleRadiusCm`、`FadeAtMs`、`CageUntilMs` 等、
`PrivatePoles`），由 `LDOpponentFrame`/`ALDOpponentController` 照抄进 `FLDOpponentPublicMatch`；旗杆地点跟随公开圆心。
`FinalModBody` 先于普通争旗执行最小行为：残血时远离视野内的篡位或清场持有者；铁笼站立时圈外不往里挤；
扛旗的涂鸦主人远离最近敌人但不出封街终圈半径；打一枪换个地方喷上后立即出圈；别人的私人旗杆守护过三分之一时去站圈冻结；
私人旗杆持有者去自己的旗杆喷漆并只打进圈的人；清场与人头换秒持有者主动追杀视野内敌人。其余情况沿用普通争旗。
导航网格是静态的，铁笼不改路径，AI 只按公开围栏状态避免硬顶围栏。

### 6.7 休息 PK 与观战

AI 只读公开对局视图里的时间线与 `FLDPublicMatchView::PK`（参赛者及阵营、可押注者、押注、场地中心、开打时刻），不读未来赛制或随机结果；
请求走真人同一入口，由权威重新校验。没有专属数值：缩圈用 `PK.*`，投掷距离与冷却用 `THROW.*`。

1. **报名**：R1、R2 的报名窗口内，存活的 AI 以普通移动订单走向本人名单槽位的 `pk_signup_<槽位>`（`Frame.SignupPOI`），进圈后留到锁定；窗口内不训练。
2. **押注**：押注步骤中本人有资格且还没押时，押座位最低的参赛者所在的一方，每场只发一次。
3. **投掷**：对打进行中，已押注的观众在本人冷却结束后，朝观察帧里可见、不属于所押一方、在 `THROW.RangeCm` 内的存活参赛者中座位最低者的脚点投掷；看不到就不投。
4. **参赛**：沿用野外的交战策略，但只把其他阵营的存活参赛者当作目标与威胁；脚点在当前圈外或下一圈预告外时先走回场地中心。准备步骤输入锁定，什么也不发。

模式 `PK`，原因 `PKSignUp`/`PKBet`/`PKThrow`/`PKFight`/`PKRing`；纯判断在 `LDOpponentPolicy`（`WantsPKSignUp`、`PKBetSide`、`PKThrowTarget`、`PKRingRetreat`），
请求在 `ALDOpponentController::DispatchPK`，押注与投掷各输出一行 `LD_AI_PK_BET`/`LD_AI_PK_THROW`（含结果码）。这部分没有规则测试与拥有连接场景。

## 7. SC-09：参数与内容唯一来源

以下键由内容域定义并已批准，**本文不提供第二份数值表**。
由 `CONTENT_AND_BALANCE.md` 维护具体数值、批准依据、单位、范围及生成映射。
运行时不读 Markdown；未批准/缺字段/非有限输入须由唯一 compiler/校验入口拒绝，
不能以旧 Routines 常量或技术默认值补齐。

| 参数键 | 类型/单位与消费 | 必须可检验的约束 |
| --- | --- | --- |
| `AI.RetreatHPRatio` | 无量纲比例；受压脱险线 | 在零至一的有效健康比例域内，低于 ResumeFarmHPRatio，避免往返振荡。 |
| `AI.ResumeFarmHPRatio` | 无量纲比例；恢复后重新主动刷野 | 不要求劲、不阻止死时选择与加点/公开回防；与低档止损和入场能力一致。 |
| `AI.HighCampEntryHPRatio` | 无量纲比例；高档入场的当前 HP 门槛 | 高档还须等级 ≥ 2（开局主动选择与开局点数属于起始构筑）并通过保守成本评估；不保证“进了就能打赢”。 |
| `AI.HighCampReserveHPRatio`、`AI.LowCampReserveHPRatio` | 无量纲比例；相应营地预测清空后的剩余 HP 门槛 | 使用本人实际构筑与公共目录的限定成本模型，不读隐藏血量；不是当前 HP 止损线/额外免伤。实际止损沿 RetreatHPRatio，模型不能保证第三方干扰下安全。 |
| `AI.CaptureReactionBudgetSec` | 正有限秒数；回防估计余量 | 包含常规规划/排队的不利延迟，不是等待此时长才行动；不得改变喷漆或守护的真实截止。 |
| `AI.DecisionIntervalSec` | 正有限秒数；常规策略节拍 | 三 AI 最坏调度可纳入回防预算；终局/身份/观察失效立即处理，长帧不补算旧决定。 |
| `AI.RetryPolicy.MaxAttempts` | 正整数，含首次 | 有界业务/路径尝试；缓存过窗不是重新发起同一操作的理由。 |
| `AI.RetryPolicy.InitialBackoffSec`、`AI.RetryPolicy.MaxBackoffSec` | 正有限秒数 | 最大值不小于初值；重试只在原操作确定终结/拒旧及当前合法条件下发生。 |
| `AI.ExploreRevisitIntervalSec` | 正有限秒数 | 只描述本 AI 公共 POI 再探节奏；不随隐藏营地刷新事件调整。 |
| `AI.EngagementBudgetSec` | 正有限秒数 | 覆盖批准基础低档战斗的合理投入；同一次目标/营地过程累计，不随普通攻击、Order 或帧重置。 |
| `STAT.AttackRangeCm`、本人移速/生命/劲/readiness | 原领域权威派生；厘米、厘米/秒、资源 P2、绝对服务器毫秒 | AI 不用初始射程覆盖成长，不另存技能 CD，也不读或另算交战窗口（构筑没有安全窗）；字段单位由提供者定义并集中转换。 |
| `ABILITY.<id>.TargetMode/RangeCm/WindupSec/CooldownFamily/MomentumCost/CooldownSec`、分支、`stats` | 公共定义；按字段单位/类型读取 | 每个主动 1 级一组效果值、六个分支各一组；按实际目标模式（Self/Unit/DirectionProjectile/DirectionCone/DirectionDash/GroundPoint）覆盖全部非空来源；加点只读五维目录与效果的 AD/AP 系数；未知 enum/缺作用条件拒绝而非默认为普攻。 |
| `CAMP.Low/High`、公开地点；`FORMULA.Damage/Resistance/AttackIntervalMs/AttackWindupMs/ResourceMaxChange` | 只读公共定义/统一数值原语 | 成本估计复用原语但不执行战斗，不读隐藏当前营地成员/HP/刷新期限；资源比例变化只消费 Combat 最终投影。 |

工程级回执容量、订单 liveness、无进展/求路预算沿操作/领域合同集中定义，
不再加 AI 私有攻击周期、卡路阈值、单位历史 TTL 或敌方战力权重。
首版策略只有固定选择顺序。

## 8. 验收与证据

### 8.1 规则测试

`Lockdown.Rules.DemoOpponentPolicy.*` 用授权 DTO 直接驱动纯策略与映射，均属必需规则集：

| 测试 | 覆盖 |
| --- | --- |
| `CampCost` | 营地成本估计与 NotFeasible/EstimateUnavailable 边界 |
| `ChoiceOrder` | §6.4 主动偏好（缺失用途、目标形式、目录顺序）、图纸学到最低空槽、加点分配，以及学图纸/加点的时机闸与单一在途请求 |
| `CaptureUrgency` | 回防紧急度：路径、移速、控制、预算，零移速与未知路径 |
| `RetryBounds` | §6.5 有界重试、退避与指纹解除 |
| `Priorities` | §6.1 优先级、撤离与回防、有界争夺、开局选择与高档营地门槛、争夺暂停下的坚持与争夺；喷漆/守护/围攻：涂鸦主人守区（恢复中未到止损线也回区留守，离区守护暂停）、跌破止损线才脱离、守护暂停时对手不产生回防紧急度、区外围攻可见主人、区内先打喷漆者再打主人、对独自喷漆者用眩晕/嘲讽打断 |
| `Projections` | 本人/观察帧映射与 FName 大小写规范化 |
| `Outcomes` | 回执结算对失败记录与当前目标的影响 |
| `Requests` | 意图→请求构造与回执归类；选牌（当前牌组、带位置时同次安装）、装牌/卸牌、休息服务（洗点、替换）的条款与过时/未持有的拒绝 |
| `Objective` | §6.6 野外目标：预告与开放时去、到点停下、生命或距离不够不去、过期与已决不去、未知地点不去、擂台走向旗杆、天降外卖优先 |
| `RestRebuilds` | §6.4 休息决策：替换（排名最低的已学主动、图纸用尽即止）、替换后洗点再加点；野外与死亡时均不做；每次连续决策都到达不动点 |
| `ShopBuy` | 未分配点数够且未买满时买技能点，否则买得分最高的买得起的牌；没进货或不在构筑窗口时不买；命令带货架位与商店修订 |
| `Training` | §6.4 训练：只打名单外的本人假人（生产交战的主动顺序与普攻）、保持在途订单与引导；有构筑、构筑在途、`OpeningPickAtMs` 前、返回倒计时、名单未读、受保护或死亡时不训练；窗口关闭时 Stop |
| `GroundPathCost` | 敌方地面状态里的路线长度按 `AI.EnemyGroundPathCost` 计价、本人地面状态不加价；只为挡路的敌方足迹给两侧绕行点，绕行比穿过更便宜 |

### 8.2 满员联机场景

`RunDemoOpponentCheck.ps1` 的场景是满员房间（参赛 Listen Host、两个拥有客户端、三个 AI）经正常大厅开局，真人不参与。
它核对 AI 的独立绑定与不补位、每个 AI 自主移动、获胜 AI 升级并确认选择、
死亡期间不发任何身体请求、死亡后按批准时长复活并继续行动、请求速率有界、由 AI 按喷漆 + 守护规则以 `GuardSucceeded` 获胜且获胜 AI 是公开涂鸦主人，最后回到新 Lobby 并退役全部 AI。
它还核对每个 AI 在开局休息 R0 的构筑步骤内处理掉开局主动选择（落在开局后 1000 ms 至 R0 构筑结束之间）。公共休息室中的身体在构筑时保持静止，首次报告原因 `Train` 之前不离开起始位置（报告 `rest0_trainers`；探针不核对观察帧中的训练目标句柄）；构筑步骤结束后只为训练移动。请求速率上界在休息中照常计数。
Host 另用夹具在真实驱动中检查 R01/R14：R1 前尝试压缩 `Lives`，分别核对假人槽位与生成假人时实际分配是否增长；未增长时扩容检查记为 NotExercised，不计为通过。R2 返场时挡住最后一人的近处与全部出生锚点，核对全员都未返场、T 与旗杆不开始，移除阻挡后共同返场；
返场受阻时休息生命已退役，夹具检查权威生命无身体、无待提交身体且 T 冻结；`Lockdown.Rules.DemoOpponentProbe.ReturnHold` 覆盖生命状态判定。
旗杆开放前每个 AI 都取得过经验，除非发育期至少一半时间处于死亡或回血恢复（1 级开局互打后按批准回血等回 ResumeFarm 可能占去大半个发育期，这是批准节奏，不算卡住）；以及争夺暂停下的两类决策：
AI 持有被暂停的喷漆进度时留在区内迎战，只在止损线离开；AI 在区内面对对手的暂停进度时继续争夺，不离开去刷野。
这两类决策由各 adapter 按自己的帧计数（`paused_holds*`、`challenges*`），另报打向公开喷漆进度持有者的攻击订单数（`holder_attacks`）和打向对手涂鸦主人的攻击订单数（`owner_attacks`），仅作证据，不回送策略。
死亡与复活、以及争夺暂停取决于对局走向：本局没有 AI 死亡、复活或相应争夺时，相应各项报告为未覆盖（NotExercised），单独列出，不计为通过。
“至少一个 AI 实际完成 D 冲刺”（各 adapter 计已提交的冲刺 `dashes` 与 Combat 完成的冲刺 `dashes_landed`）只作报告项：
未出现时记为 NotExercised，不卡门槛；是否升为硬门槛要等多局与打包版运行稳定后再定。
零到二个 AI 开局、真人离开、每个 AI 各自的成长、逐段回执链、三个槽位分别获胜，
目前没有自动验收。
纯模型证明策略决定，原生场景证明接线，真实拥有连接证明授权和运行拓扑，三者不能互相冒充。

渲染的 Host 在开局休息 R0 的构筑步骤里做界面核对（名称以 `UI:` 开头）并留评审截图（1280x720 与 1920x1080、文字 100% 与 125%）。
界面核对失败不中止：截图照常继续，角色报告结束时把每项失败及其每次出现的细节（哪张截图、相关控件的矩形与可见性）一并列出，
一轮即可看到全部界面问题；脚本逐行打印 `FAILED: <名称> -- <细节>`。
`RunDemoOpponentCheck.ps1 -UIOnly` 是这一段的单独入口：同样三个角色经正常大厅开局，Host 完成 R0 的界面核对与全部截图后即报告并结束，
不打完整局，也不产生对局结论；R0 结束时评审还没做完记为失败。

`RunDemoOpponentCheck.ps1 -Telemetry -MatchCount N` 是另一回事：一名无渲染、闲置的 Listen Host 与三个 AI 连打 N 局，
每局记录时长、首杀、每名参赛者的等级曲线、喷漆尝试与争夺次数和胜负方式，
以及度量（每名参赛者的首个主动时刻、最终 Q/W/E、图纸与五维点数、未分配点数、主动选择次数与每次学习时的等级，每次死亡的击杀者/助攻者与奖励、喷漆/改漆/涂鸦清除原因与守护用时），并汇总为一份报告
（字段见[验证手册](VERIFICATION.md#合并前与里程碑检查)）。它只是对局数据，供 AI 与节奏评审参考：不核对上述任何一项，
也不把某个数字当作目标；真人闲置时 AI 也会击杀 Host，这些击杀照常计入。

## 9. 已知限制

- **节奏。** 1 级开局与对手互打后，按批准的 RetreatHPRatio/ResumeFarmHPRatio 与 1 级基础回血，AI 可能要等约 2 分钟才恢复刷野，
  个别 AI 会整个发育期停在 1 级。这是批准数值下的节奏，是否调参由试玩后决定，策略不另加回复。
- **覆盖。** §8.2 列出的未覆盖项没有自动验收；死亡复活与争夺暂停的场景检查取决于对局走向，可能报告 NotExercised。
- **公平性证明。** 策略只读获准输入由结构保证（纯策略不接 UWorld/Actor，帧只由授权投影组成），
  但尚无改变隐藏状态的非干扰测试，也没有 AI 侧的字段白名单场景。

# Demo 对手 AI

**目标：v0.10.0 · 职责：M12 · 引擎：Unreal 5.8.4**

本文描述对手 AI 的当前实现、约束与验收边界，对应 [App Spec](../APP_SPEC.md) §7.2
与已批准决定 D01（含 AI 参数）、D03、D10（开局三选一），以及 [#112](https://github.com/rlsn/Lockdown-alpha/issues/112) §9 的喷漆 + 守护终局。
共同身份、时序、权限及回执遵循 [共享契约](SHARED_CONTRACTS.md)；模块依赖见 [架构](ARCHITECTURE.md)。
AI 参数数值只由 `CONTENT_AND_BALANCE.md` 维护并经内容管线进入运行载荷，本文不另列数值。

## 1. 范围与已确认约束

对手 AI 必须是**能独立完成成长并按相同条件获胜的参赛者**，不是占席、巡逻或靶子。
仅选择一种固定优先级策略，不引入通用 GOAP、人格、多难度、学习平台或第二套世界模拟。

| 边界 | 本模块遵守的产品约束 |
| --- | --- |
| 名单 | Host 选择零至三个 AI；与最多三名真人（含 Host）共存，各自 FFA，不补满、不组队、不接管断线真人。 |
| 起步与成长 | 同一基础角色，初始近战单体物理普攻；出生即有一次开局三选一（D10），之后正常打野/对抗获得最后一击的经验和金币，再三选一、购物、喷漆、守护涂鸦和围攻别人的涂鸦主人。装备/属性可以增加攻击距离或群攻。 |
| 营地反击与回位 | 未受攻击营地不主动攻击路过者；确认的合法命中触发同组存活成员反击，即使被击成员当次死亡，也不共享隐藏位置。预览/挥空/被拒命令/单纯碰撞不触发。Returning 仍可受伤、控制、被杀，不回头追击；存活成员到家满血蓝、清临时状态/仇恨、技能就绪，不复活已死成员、不因回位发奖。 |
| 技能与装备 | Q/W 两个角色主动槽；装备没有主动；角色被动无额外槽位上限；开局一次加升级九次、整局最多十次选择（D10），每技能两档（初级、高级）。满 Q/W 学新主动必须明确确认替换并遗忘旧技能及等级，新主动从初级开始。 |
| 同款装备与冷却 | 两件同款合法，基础属性相加，同名被动只生效一次。换槽、遗忘重学和死亡不能洗冷却；失去来源期间原期限继续流逝。 |
| 死亡 | 保留成长、财产、候选和冷却；死亡等待可以确认三选一，只更新持久构筑，不治疗尸体或提前复活。最大血蓝变化按统一公式保持原百分比；AI 不另算公式。 |
| 保护 | 经过身份、当前 Match/Life、请求新鲜度、形状及写闸检查的进攻意图先解除保护；随后目标、距离、路径、法力或冷却检查失败也不恢复保护。只读查询、普通移动、商店、Stop 和本地预览不是进攻。 |
| 视野 | 失视野取消单位锁定并停止该追击订单，不续走 LastSeen，不保留地图 LastSeen 单位标记。只读取当前可见对手的名称、等级、血量、蓝量、状态和展示装备；不读取技能、冷却、内部实例/构筑、属性或金币。 |
| 死亡与离局效果 | 正常死亡后，已成功释放的独立效果继续至原期限，在局的阵亡所有者仍可获得合法击杀归属。正式离局/断线立即移除身体并取消其未结束独立效果；AI 不追踪已离局身体。 |
| 结束 | 唯一终局闸关闭后停止新增意图；返回 Lobby 的新局重新初始化，不沿用旧目标、订单或局内战力。死亡无活体视野；能查看静态地图不等于获得隐藏玩家跟随。 |

AI 自动确认的是**自己的**候选和替换，不替真人选择。商店不是安全区，选卡不暂停世界。
购物不额外检查 LOS；仍须存活、比赛进行中、真实位置处于合法区域并满足交互距离。
恢复只能依靠同规则的自然回复或已拥有的合法能力，不能通过重建身体、刷新构筑或调试授予恢复。

## 2. 代码入口

AI 只新增 `Source\Lockdown\{Public,Private}\AI` 下的文件，不拥有任何玩法状态；它读取的投影与提交的请求都属于原领域，
与真人使用同一服务器入口：

| 用途 | 入口 | 说明 |
| --- | --- | --- |
| 绑定与读取 | `ALDGameMode::RegisterAIContender`、`ReadAISelf`、`ReadPublicMatch` | 名额在 Starting 冻结；`ReadAISelf` 含 `FLDOwnerState.Growth` 发布。 |
| 空间订单 | `ULDNavigationComponent::SubmitOrder`/`CancelOrder`/`QueryOrder` | 与真人 `ServerSubmitOrder` 共用订单簿；激活由 Actions 阶段的 `ProcessAuthorityOrders` 统一处理。 |
| 直接动作（Q/W/D） | `ALDCombatDirector::SubmitAction`/`CancelAction`/`QueryActionReceipt`/`QueryAvailability`、`BuildOwnerView` | Caller 由核心绑定解析；AI 只读本人 `FLDCombatOwnerView`。 |
| 三选一 | `ALDDevelopmentDirector::QueueChoice`/`QueryChoiceReceipt` | 含 D10 开局 Offer；Transactions 阶段提交。 |
| 商店 | `ALDVendorDirector::QueueShopOpen`/`ReadShop`/`QueueShopCommand`/`QueryShopReceipt` | `ApproachShop` 订单到店后入开店队列；Transactions 阶段由 `OpenPendingShop` 打开。 |
| 观察 | `ALDVisionDirector::EnumerateVisibleUnits` | 与真人同一受限帧；AI 只用当前可见单位。 |
| 进喷漆区路径估计 | `ULDMapRegistry::QueryGround`/`QueryCompletePath` | 只为回防紧急度计算本人到喷漆区的完整路径长度。 |
| 数值原语 | `LDTactical::EvaluateDamage`/`EvaluateBasicTiming` | 营地成本估计与真实战斗共用。 |

已退役的旧机制（居民、B91/B92 席位接管、旧成长/商店/营地）与不得恢复的内容以
[架构 §3.2](ARCHITECTURE.md#32-删除清单与防回归约束)为准。

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
| 一次决策输入 | 各领域投影；AI 只读副本 | `FLDOpponentDecisionFrame`：本人 owner/progression/equipment/readiness、已发布 WorldView、公共 MatchView、只读公共目录。纯策略不接 UWorld/Actor/Ledger 指针。 |
| 营地成本估计 | AI 纯值策略计算，不是玩法写入 | `LDOpponentPolicy::EstimateCampCost(FLDCampCostInput, 公共怪物定义, 目录) -> FLDCampCostEstimate`；输入只有本人 HP/派生属性/普攻 readiness 与公共营地模板。结果区分有数值的 Ok、预算/生存条件不允许的 NotFeasible、输入/公式失败的 EstimateUnavailable；数值原语复用 R07，不推进世界、RNG、CD 或真实血量。 |
| 意图 | AI 产生；对应领域验证/提交 | `FLDOpponentDecision` 是 Order/Action/Choice/Shop 的有界判别结果，不含可写权限或另一个参赛者 ID。 |
| 绑定 | GameMode 唯一注册、核心注册后通知 | `RegisterAIContender(AController&, uint8 RosterSlot, const FLDMatchContext&) -> FLDAIRegistrationResult` 仅 Starting 使用冻结 AI 名额；返回核心 `FLDAIContenderBinding{Match,ContenderId}`，再调用 adapter 的 `BindRegisteredSelf(const FLDAIContenderBinding&)`。不得让策略任意选择 ID。 |
| 本人快照 | 核心与原领域只读投影 | 核心 `ReadAISelf(binding) -> FLDAISelfReadResult`（含 `FLDOwnerState.Growth` 发布）与 `ReadPublicMatch() -> FLDPublicMatchView`；adapter 的 `BuildFrame` 再组合本人 `BuildOwnerView`、`EnumerateVisibleUnits` 与 `ReadShop`，不在核心另建快照真相。缺服务/旧局/已离局是失败，不返回成功形状空快照。 |
| 决策与发送 | AI / 原领域 | 每个 `AI.DecisionIntervalSec` 的权威时间 `BuildFrame` → 纯 `Decide(Frame, PolicyState)` → `DispatchBody`/`DispatchBuild`；后者用核心绑定解析的本人 Caller 调用各域服务器入口。 |
| 回执与生命周期反应 | 原域先提交，AI 后消费 | `SettleOrders`/`SettleDirectAction`/`SettleShop`/`SettleBuild` 查询原 ID，`HandleLifeCommitted`、`HandleMatchClosed`、`HandleMatchReset` 只更新策略状态/游标。迟到事件不能恢复已结束的旧订单或旧生命。 |

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
AI 不持有 `FLDGameplayWritePermit`，更不能直接调用 `ProcessAuthorityOrders`、`OpenPendingShop` 等提交入口。
只聚合同一已完成发布边界的本人/公共/观察投影，不用不同边界的余额、来源和目标拼造准入；
执行时仍由各域重新验证当前状态。
核心提交后提供 `LifeNotice{Match,BodyRef,Change,LifeRevision,Stamp}`、
`MatchClosed{Context,ResultReceipt}`、`MatchReset{OldContext,NewContext,LobbyRevision}`；
它们是事实通知，不给回调重新打开写闸的权限。

| 获准输入 | 正式来源 | 限制与撤销 |
| --- | --- | --- |
| 自身 | 核心 ReadAISelf（含 Growth 发布）、adapter BuildFrame；本人 Combat BuildOwnerView、Vendor ReadShop | 可以读本人血蓝/资源/等级/技能/装备、控制、真实 readiness、候选和回执；不能取得可写 StateFor 或全账本。 |
| 公共对局 | `FLDPublicMatchView` → `FLDOpponentPublicMatch` | Phase、OpenAtMs、CircleState（`Empty / Solo / Contested / Guarded`）、Occupier、CaptureEpoch、SoloSinceMs、DueAtMs、GraffitiOwner（策略帧中为 `Owner`）、GuardDueAtMs、Result；Occupier 在 Solo（独自喷漆）和争夺暂停（Contested 且有人持有喷漆进度）时有效，DueAtMs 只在 Solo 时有效；Owner 是当前涂鸦主人，GuardDueAtMs 是运行中守护的获胜时刻，只在 Guarded（主人独自在区内）时非 0；Empty（主人在区外、区内无人，守护暂停（2026-11-07 用户决定，原话：“胜利判断好像还是有问题 - 当旗杆被占领但无人在圈内的时候不计算时间，时间停止，现在好像是时间继续计算是吗”））与 Solo/Contested（区内有主人以外的有效参赛者，守护冻结）时为 0。喷漆者和涂鸦主人的身份与进度不附送其隐藏精确坐标。 |
| 当前可见单位 | `EnumerateVisibleUnits(ObserverContext, Purpose) -> ObservedFrame` | 带 Match、ObserverLife、ViewGeneration/Revision、MapRevision、ServerTime、Handle/ObservationEpoch。AI 与真人使用同构投影；只含已批准展示字段。 |
| 可见营地回位提示 | 同一观察帧单位 `Statuses` 中只读 `StatusId=CampReturning`（`ELDDemoStatusId`，内容 id `camp_returning`） | Vision 仅为当前获准可见且 Returning 的 CampMember 派生“回位”；不含堆叠、期限、Home、路径目标或仇恨，不是可写 Combat 状态。Root 暂停不清提示；退出 Returning 或失视野即撤销，隐匿时不更新。 |
| 可见装备与资源 | 上述观察帧的展示定义 | 装备只有两个展示 DefinitionId，不含内部 ItemInstance、IssuedCooldownId、ReadyAt、SourceBindingRevision、技能列表或完整属性。HP/MP 按世界 Resources 的 P2 Current/Max 读取；NPC 不适用的 MP/Level/Equipment 是 NotApplicable，不补零猜测。失视野整条动态记录撤销。 |
| 静态世界与内容 | 世界公开地图/POI、公共目录 | Camp/Shop/Capture 固定位置、营地档位、地形和公开定义可知；不能把定义里的原始营地人数/刷新周期当作当前存活人数或剩余刷新时间。 |
| 路径 | `ULDMapRegistry::QueryGround(WorldRevision, FootCm, Capsule, SurfaceHint)`、`QueryCompletePath(WorldRevision, FromGround, ToGround, Capsule)`；接近公共地点与单位由 Navigation 订单规划 | QueryGround 四参，无默认/三参重载；SurfaceHint 为必填 FName 注册 SurfaceId，仍须重查真实支持面。AI 只用自身真实位置与当前地图估计到喷漆区的完整路径长度（厘米）；不含隐藏动态身体定位。 |
| 单位解析 | `ResolveObservedUnit(ObserverContext, Handle, Purpose)` | 仅供导航/战斗执行域重查可见性、目标生命和 epoch；返回的 authority 样本不进入策略黑板。 |
| 历史 | 本 AI 的公共 POI 访问和失败事实 | 不保存单位 LastSeen、Actor 指针、外推轨迹或隐藏营地倒计时。未见到目标不是“营地已清空”的证明。 |

不能用 `GetAllActors`、`TActorIterator`、GameMode 全参赛表、Combat Bodies/Nearest、
World Pawns/CampNext/LifeOwners、Inventory 全表或 Ledger 导出补全感知。
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
| `ULDNavigationComponent::SubmitOrder(FLDOrderCaller, FLDSpatialOrderRequest)`；`CancelOrder`；`QueryOrder` | 绑定本人的 MatchContext/BodyRef；`FLDMoveIdentity{Match,Life,Order,Epoch}`；按 kind 为单一 Ground/ObservedUnit/公共 Shop 目标。Ground 带脚点 cm、必要 MapRevision 和必填 SurfaceHint；POI 从注册记录给支持面，目标可显式 Missing，不补默认值。 | Submit 只收件。Actions 阶段的 `ProcessAuthorityOrders` 才可激活；Queued 不取消旧单，完整准入后才替换现役订单。OrderRevision/ReceiptSerial 单调，Query 只读且不延寿。 |
| `ALDCombatDirector::SubmitAction`、`QueryActionReceipt`、`QueryAvailability`、`CancelAction` | `FLDCombatActionRequest{Generation,Life,DirectSequence,Entry(Q/W/Dash),Target(Missing/Self/Unit/Direction),ExpectedSourceRevision}`；DirectSequence 由 adapter 单调分配，Q/W/D 共用一个序列；Unit 目标是观察句柄。`FLDCombatRequestKey` 的 Direct 键由此生成，Nav 键只由服务器 Navigation 的攻击订单内部生成。 | 两种 RequestKey 不撞号；准入成功后由 Combat 统一分配 authority CombatToken:uint64，ActionId/AttackId 是该 Token 在 Match/Caller 下的身份，不另发序列。Queued 无已接受 Token；查询/取消用原 RequestKey。导航独占 AttackCycle，Combat 独占 NextAttackAt，AI 不另开普攻循环或伪造 Nav 键。 |
| `ALDDevelopmentDirector::ReadProgression`、`QueueChoice`、`QueryChoiceReceipt` | Generation、当前 Life、ExpectedLifePhase、独立 RequestId、队头 OfferId/Revision、OptionId、ExpectedBuildRevision；满槽明确 ReplacementSlot、ExpectedOldSkillId/Rank。 | 原候选不重抽；原子遗忘/装槽/消费一次机会后才生成下一组。死亡阶段合法确认可成功；死前 Alive 请求不能冒充当前 Dead 确认。 |
| `ALDInventoryDirector::ReadEquipment` | 绑定本人的读取上下文。 | 返回 Gold/AccountRevision、两个 Instance/Definition/ItemRevision、InventoryRevision/SourceBindingRevision、IssuedCooldownId/ReadyAtMs；纯投影，不授予修改权。 |
| 开店意图进入现有核心队列；`ALDVendorDirector::QueueShopCommand`、`QueryShopReceipt` | 开店 `FLDShopOpenCommand{Generation,Life,ShopId,ExpectedWorldRevision}` 不携 Permit。Buy/Sell 另带 RequestId、ShopSession/QuoteRevision/CatalogDigest、ExpectedWorldRevision 和预期 Account/Inventory/Build/SourceBindingRevision；Buy 商品/展示价，Sell 精确实例/ItemRevision/展示卖价。 | Transactions 才调用内部 `OpenPendingShop(ContenderId, Permit)`；每次开店/Buy/Sell 都由核心借当次 Permit，经 `QueryShopEligibility(Permit, BuyerBody, ShopId, ExpectedWorldRevision)` 重查当前身体、合法区域/距离与版本，不额外加 LOS。AI 不直接调用内部 OpenPendingShop 或受许可几何；Sell 真成功后重读再单独 Buy。 |
| `LDTactical::EvaluateDamage(const FLDDamageEvaluationInput&, const FLDDemoContentSnapshot&) -> FLDDamageEvaluationResult`；`EvaluateBasicTiming(const FLDBasicTimingInput&, const FLDDemoContentSnapshot&) -> FLDBasicTimingResult` | 仅数值假设：公开编译定义、本人所需属性、显式非暴击假设、匿名模板抗性/HP/盾/保护；Timing 用对应单位普攻定义及已解析 AS。目录快照是批准编译的不可变内容。 | 位于 LDTacticalRules，成功给数值/时长，定义域失败显式返回错误；无 Actor/World/Bodies/Camp 查询、时钟、RNG、状态或 Token。真实 ApplyHit/AttackStart 也必须调用这两个同源纯函数，再经 Permit 提交；估计结果不是 Hit/Death/奖励事实。 |

每域独立请求空间和有界回执窗口：同 ID 同条款只重放原结果；同 ID 异条款
RequestConflict；过窗仍保留拒旧高水位并明确 ReceiptExpired。没有收到回执不能改新 ID
重做同一交易/选择/释放。先查询原 ID；只在取得终态，或领域拒旧水位已证明该旧请求
不可能再执行且已读到当前权威投影后，才基于当前状态作新决策。
ReceiptExpired 不是业务成功，不虚构原成交结果。
Pending/Queued 不等于成功，ActionFinished 也不等于所有已释放效果消失。
直接意图取消的 ThroughDirectSequence 只比较直接请求序列，不与 authority CombatToken 比较。
派生普攻继承当前导航进攻意图的解除保护事实，不由 AI 再生成一条独立 Basic 请求；
Rejected 可以有 ProtectionRemoved 而没有 ActionId。

成长/商店的正式查询是
`QueryChoiceReceipt(const FGuid& ContenderId, const FLDReceiptQuery&) -> FLDChoiceReceiptQueryResult` /
`QueryShopReceipt(const FGuid& ContenderId, const FLDReceiptQuery&) -> FLDShopReceiptQueryResult`；
ContenderId 仍由绑定 adapter 解析。Query 带 Generation:uint32、
Family:Choice/Shop、RequestId:uint64，**先拒旧 Generation 再查记录**；
不能把新局 RequestId=1 映射到上局记录。查询返回包装结果，不是业务终态 Receipt：
LookupStatus=Pending 时仅 PendingInfo 存在，其中 OriginalLife/OriginalLifePhase
来自已受理请求，没有 Outcome 或成功载荷；
Final 时仅匹配原查询键的对应业务 Receipt 存在；
NotFound/ReceiptExpired/InvalidContext 时两种载荷均不存在，只返回原查询键和状态。
不存在的数据不补默认 OriginalLife，也不伪造 Applied/Rejected、物品或选择结果。
Queue 结果区分 AcceptedPending、Final、AdmissionRejected，并携 SequenceReserved。
未受理且 SequenceReserved=false 的拒绝不占用未来序号；
已受理的最终拒绝也消耗该序号，已保留 ID 的异条款仍冲突。
Pending 只查询/重发同条款，不先送下一笔业务。
Build/Inventory/Item/Account/Offer/Progression/SourceBinding/Readiness 修订均为 uint64；
Instance/Offer/Option/IssuedCooldownId 均为 FGuid。
技能/装备 DefinitionId 服务端为 FName，wire 使用固定 64 字节 UTF-8 缓冲：
最多 63 字节内容、NUL 终止、余字节零填充；批准 ID 使用 `[a-z][a-z0-9_]*` ASCII 子集。
先检查编码、长度、终止/填充、规范和目录，不截断未知/非法 ID，也不降级成默认能力。
CatalogDigest 使用读模型给定的根摘要，wire 为 SHA256 的 32 raw bytes，
显示为 64 位小写 hex；AI 不自行重算或拼接另一摘要。

开店复用当前交互绑定/取消和核心权威队列，每人最多一个未办理意图，同一意图重复受理合并；
不另建开店请求流或第二 Pending 队列。
`FLDShopOpenResult{Generation,Life,ShopId,OpenStatus,Reason,可选ShopView}` 中，
Pending 只表示队列受理，Opened 只来自 Transactions 成功且独占合法会话/报价 View；
Rejected 可来自受理或阶段拒绝，Pending/Rejected 均没有 View。
旧订单/生命的 Opened 不给新身体会话执行权，AI 不把排队回执当成已开店。

Stop 提高 Order 和 Epoch；服务器因目标失效结束原 Identity，不制造调用者未知的新 Epoch。
接受回执时核对局、生命、域、请求身份与单调修订；旧 ACK、旧 Offer 确认和旧 Outcome
不能重建动作。取消不退已释放资源、不清未结束冷却、不回滚成交或重抽候选。
同局跨 Life 的历史 Choice/Shop 回执可以只读核对原 RequestKey 的终态，
不能将其中旧身体状态应用到新身体；持久构筑/库存以当前本人投影为准。
AI 订单的存活性来自仍有效的服务器策略/参赛记录绑定，不发送伪造 ServerHeartbeat；
记录、controller、生命失效立即结束。QueryOrder 不刷新租约，普通 UI 焦点也不影响 AI 身份。

**Rejected 不一律等于零状态变化。** 合法身份的新 AttackUnit/AttackMove 或角色/装备
进攻意图会先经生命域解除保护，回执可带 ProtectionRemoved 与 LifeRevision；
随后 TargetUnavailable、NoCompletePath、资源或冷却拒绝仍保留该变化，但不消耗失败动作的法力/冷却。
旧局/旧生命、重放、越权和真正畸形请求不触发这一步。AI 必须重读本人状态，不能恢复本地保护标记。
InvalidSurface/WrongSurface/NotGrounded 属于世界可执行性拒绝，不伪装成 Malformed；
已识别的规范化进攻意图仍按同一前置解除保护。

| 结果类别 | 单一恢复路径 |
| --- | --- |
| Queued/Pending | 保留原请求；按有限查询节拍 Query 原 ID，不创建新的等价操作。 |
| StaleBuild/Inventory/SourceBinding、StaleVersion/Quote | 读取本人/原 Offer 或新报价，废弃旧确认；按新条款重新判断，不悄悄改选项。 |
| TargetUnavailable | 不区分隐藏、销毁、过期或换生命；清目标、停该单位追击。禁止跟到最后坐标或换一个全局 ID 重试。 |
| NoCompletePath/PathBlocked/NoProgress | 接受原订单终结；有限退避并选择其他公共可达目标。不 partial 成功、不传送、不缩短射程、不每帧重算。 |
| InvalidSurface/WrongSurface/NotGrounded | 重读合法支持面/本人真实接地结果，等相关条件变化后再规划；不伪造 SurfaceId 或把身体向下投影成接地，不以此错误自报系统故障或喷漆成功。 |
| Cooldown/AttackNotReady/InsufficientMana/ControlBlocked | 使用本人 readiness 和控制投影等待或换合法动作；策略只保存再评估期限，不写第二份 CD。 |
| InsufficientGold/Full/OutsideRange | 等真实资源/库存变化，或合法回店；不盲卖、不远程成交、不忙循环。售后买失败时承认当前空槽和实际金币。 |
| Dead/StaleLife | 移动/战斗/购物作废；当前 Dead 选择使用新的合法生命阶段确认，候选不变；等核心真实复活。 |
| MatchClosed/LeftMatch/StaleMatch | 停止全部新增意图，最多读取已提交结果；不重建 adapter、补席或重试旧局。 |
| 缺内容/缺服务/非法参数 | 明确阻塞并输出受限诊断；不加载旧 profile、不注入默认战力、不伪造空视图成功。 |

`LDOpponentRequests::Settle*` 按上表把回执归为 InFlight（只查询原 ID）、Succeeded、Failed（有界退避后抑制）、
TargetLost（抑制该观察句柄并释放当前目标）或 Dropped（换命、终局、自身新请求或记录过期，不计失败）。
陈旧守卫、报价与来源修订也计为有界失败，系统性错配不会形成无界请求循环。
每个移动、攻击、动作、选择与商店意图自带 `FLDOpponentRetryKey`，终态回执只记到产生它的那把键上。
Navigation 在替换订单通过准入前保留现役订单，adapter 因此分别跟踪现役与候选订单，候选被拒时现役照常可见。
排队订单与开店在下一阶段、已接受动作在 Combat 发布的释放时刻之后，再等 `AI.RetryPolicy.MaxBackoffSec`
仍未终结即计一次失败（动作另发一次 `CancelAction`）；成长请求不可取消，只报告超期并继续查询原 ID。

## 4. SC-09：全生命周期矩阵

| 状态/工作 | 取消/换目标 | 死亡等待与复活 | 离局 | 终局 | 新局 |
| --- | --- | --- | --- | --- | --- |
| 策略模式、目标与路径 | 取消适用订单；不把迟到回执接回当前状态 | 清身体目标/路径；Dead 保留选卡能力；复活从新 BodyRef 重新决策 | 注销本人绑定；其他人离开只撤目标，不增 AI | Close 先锁闸，再取消策略；无新 intent | 全清并绑定新 Match；不复用旧请求或随机上下文 |
| 已发命令/查询 | 已提交只读重放，不退款；待执行依域取消 | 旧身体动作/商店失效；Choice 重新核对当前 LifePhase | 不接管真人的请求/资源；本人回调不得唤醒已注销策略 | 查询不延寿、不重执行 | 旧 Match 拒绝，不能因缓存清空变新操作 |
| XP/金币/技能/装备/候选 | 不变；选择/成交只由原域提交 | 原域保留；可有在局死者的合法延迟奖励和选卡；复活不重发初始资源 | 冻结/退休由原域处理，不当击杀 | 冻结，不继续提交选择/买卖/奖励 | 原域按新局输入初始化 |
| 来源冷却 | 不因取消、换槽或遗忘/出售消失 | 按原时间继续；再次拥有沿用原期限 | 无新释放资格 | 不再释放；原回执不增加效果 | 原域新时间域，无跨局继承 |
| 已释放独立效果 | AI 不回滚，按 Combat/内容生命周期 | 正常死亡继续原期限；不得打中同 ID 的新生命 | 核心/Combat 取消本人未结束效果，身体立即退出 | 关闭写闸后不得继续结算 | 全部换代 |
| 观察/公共 POI 访问记忆 | 失视野立即丢动态目标；无 LastSeen | 死亡无身体视野；静态知识可读；复活新 observer 代次，不沿用旧句柄 | 通道/句柄失效；不跟踪移除身体 | 不保持实时隐藏更新 | 清访问/失败缓存及观察游标 |
| 血蓝/复活/保护/喷漆与涂鸦 | 进攻意图按已确认规则解保护；受伤不由 AI 清喷漆或守护计时 | 核心复活满血蓝/新 Life；死亡已由核心清零喷漆进度并清除本人涂鸦；AI 不改 RespawnAt 或自行声明喷漆资格 | 立即失去战斗/喷漆资格并清除其涂鸦，不为剩余人判胜 | 不复活、不再喷漆 | 核心重建发育期与旗杆状态 |

离局前已提交的死亡事实，其 `RewardBasis.CreditOwnerEligibleAtDeath` 由核心/成长域冻结；
允许原域完成这一事实的 settlement-only 处理，不等于离局者恢复行动资格。
AI 的已注销绑定不能因迟到奖励/回执重新规划、选择、生成身体或恢复效果。

## 5. 实现文件与边界

| 文件（`S\` 指 `Source\Lockdown\`） | 职责 |
| --- | --- |
| `S\Public\AI\LDOpponentPolicy.h`、`S\Private\AI\LDOpponentPolicy.cpp` | 纯值策略：决策帧/状态/结果类型、§6 的优先级、营地成本估计、选择与装备排序、回防紧急度、有界重试。不接 UWorld/Actor/Ledger。 |
| `S\Public\AI\LDOpponentFrame.h`、`S\Private\AI\LDOpponentFrame.cpp` | 把本人成长/装备/战斗投影与受限观察帧映射成策略输入；FName 定义 id 在此经 `CanonicalId` 统一转小写（打包版不保留 FName 大小写），策略内做大小写敏感的精确比较。 |
| `S\Public\AI\LDOpponentRequests.h`、`S\Private\AI\LDOpponentRequests.cpp` | 意图→领域请求的纯构造；回执→InFlight/Succeeded/Failed/TargetLost/Dropped 的纯归类。 |
| `S\Public\AI\LDOpponentController.h`、`S\Private\AI\LDOpponentController.cpp` | 服务器适配器与 OpponentAI 提供者：绑定、组帧、调度、发送、回执消费、受限诊断与请求计数。 |
| `S\Private\Tests\LDOpponentPolicyTests.cpp` | `Lockdown.Rules.DemoOpponentPolicy.*` 规则测试（§8.1）。 |
| `S\Private\Tests\LDDemoOpponentProbe.h`、`S\Private\Tests\LDDemoOpponentProbe.cpp`、`Scripts\RunDemoOpponentCheck.ps1` | 满员联机场景（§8.2）；probe 只在 Host 核对权威数据，从不回送给策略。 |
| `S\Private\Tests\LDDemoOpponentTelemetry.cpp`、`Scripts\SummarizeOpponentTelemetry.py` | `RunDemoOpponentCheck.ps1 -Telemetry`：一名闲置 Host 加三个 AI 连打多局的遥测（§8.2 末），只读权威数据、不判断对错。 |

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
| P1 DeadWaiting | 本人 Dead/RespawnAt、当前队头 Offer；等待身体，并可用当前 Dead 阶段确认选择 | 新 BodyRef 到来才恢复身体动作；不移动、攻击、购物，不恢复旧路线/商店步骤。 |
| P2 Disengage | 本人危险阈值、可见接触压力、自保 readiness、静态可达地面；停止继续投入，合法自保/撤离 | “正在脱险”才保持最高空间优先级：低于 RetreatHPRatio 且没有就绪自保屏障时，脱险压过回防，也不向公开喷漆者或涂鸦主人冲锋；回到这条线以上后的回血等待让位于紧急回防。已在喷漆区内参与争夺时留在区内对付区内对手，不因独自喷漆被打断、回防不再紧急就跑出区：一碰就走只会让持有者接着暂停的喷漆进度继续；只有跌破止损线又无就绪自保时才撤离。涂鸦主人恢复中（`bRecovering`）且未跌破止损线时仍回到并留在喷漆区内，因为守护只在他独自在区内时计时、离区即暂停，冻结期间走出区还会让守护清零（2026-11-07 用户决定）；跌破止损线又无就绪自保时才脱离，因为他死亡会清除涂鸦。撤离选一个远离可见威胁、终点在其视野半径外的公共地面点，并走完这一程，只有到达或不再远离时才重选；威胁失去视野后也不原地停下，也不在半途转去购物（`DemoOpponentPolicy.EscapeBeforeShop`）。区内有合法余地时先在区内移动，不机械跑出区。 |
| P3 DefendOrHold | 自己正持有喷漆进度（独自喷漆，或争夺暂停中的持有者，原因 `HoldCapture`）；自己是涂鸦主人且未跌破止损线（含恢复中；守护只在主人独自在区内时计时，2026-11-07 用户决定）（`GuardGraffiti`）；或公开的对手喷漆到期/对手守护获胜时刻即将达到自身回防预算（`PublicCaptureUrgent`，有对手涂鸦时为 `BesiegeOwner`）；走公开喷漆区入口、处理当前可见威胁 | 压过新的打野/购物；阶段、CaptureEpoch、有效资格、路线或风险改变时重评；公开喷漆者或涂鸦主人的 ID 不能直接变成单位目标，只能匹配可见单位的授权 ContenderId。 |
| P4 Choose | 队头 Offer 存在且无在途构筑变更；用 §6.4 选择一次。开局 Offer（D10）走同一请求路径，出生后第一次决策即处理 | 不冻结正在进行的合法运动或紧急反应；成功后才读下一组，冲突不重抽。 |
| P5 Engage | 当前合法可见目标、本人可用资源/动作、未超投入预算；Nav 攻击订单与 §6.3 主动 | 失视野、无效目标、危险、回防、超预算或生命变化时退出；不读对手内部战力来保证击杀。 |
| P6 Shop | 自身余额与同目录公开价格/出售规则支持一个非约束性改善计划，不要求已有 ShopView；ApproachShop 到位后将开店意图入核心队列，Opened 后按真实报价重新判断 Buy 或 Sell→Buy | Pending 不等于有会话；危险/越界/死亡/终局打断未提交步骤，已成交不回滚。无改善就离店，不为更换排序 ID 循环买卖。 |
| P7 Contest | 旗杆已开放，无更高可执行目标；自身风险门允许进入公开喷漆区（`ContestCapture`）；旗杆上是对手涂鸦时为不紧急的围攻/改漆（`BesiegeOwner`） | 到位后独自喷漆或争夺；不能有效入场时换静态合法入口或继续成长/探索，不用保护者骗争夺人数。 |
| P8 Farm | 没有更高目标；根据公开档位与自身状态选择营地，观察到合法成员才攻击 | 无合法目标、止损、预算到期或紧急回防即结束。Returning 不等于无敌；只继续 §6.2 允许的当前射程内攻击，不新起回营追逐。 |
| P9 Explore | 其他目标不可执行；选择未被失败抑制的公共 POI | 新合法观察、资源/Offer/公共喷漆与涂鸦变化、订单终态触发重评；不为移动而扫描全图身体。 |

一个 AI 同时至多持有一个当前空间订单、一个对应身体动作控制上下文、
一个成长/装备构筑变更在途请求；导航与 Combat 的关联仍由原域管理。
普通节拍最多提出一个新的身体意图和一个不冲突的构筑事务意图，不能排一串未来技能。
新 Offer 不阻塞脱险，已提交 Buy/Choice 也不能被高优先级目标反向回滚。
只有相关条件变化或真实期限到达才重评；保持仍合法的已有订单，不每节拍重发同一目标。

核心终局、离局、死亡和观察撤销按通知立即清理，不等待常规规划周期。
长帧后最多对最新已发布帧作一次规划，不“补跑”漏掉的历史 AI 决策。
每个 AI 由各自控制器按 `AI.DecisionIntervalSec` 独立决策；
它们不共享黑板/战术目标/视野/资源，也不因对手是 Host、真人或 AI 而改变仇恨。
纯平局以本局本人绑定与公共稳定键形成稳定顺序，不查询其他 AI 的计划或隐藏状态。

### 6.2 营地、目标与公开回防

**营地。** 原始构筑优先低档；开局三选一（D10）也算原始构筑，升过级或有装备后才把高档加入主动尝试候选。
这只是机器人的保守偏好，不限制真人进入高档。高档入口先查 HighCampEntryHPRatio，
两档都要评估本人实际构筑的保守打野成本：**预测清营后的剩余 HP 比例**
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
不靠“玩家先结算”抹掉成本。首版两种野怪只有普通物理攻击，没有待猜测的技能/CD。

这是 `CONTENT_AND_BALANCE.md` §7.2 限定站桩成本模型的保守决策用途：
不计暴击、技能/护盾、吸血/群攻与回复的额外收益，不运行真实效果或消耗随机流。
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
回位者不回头追击也不等于店旁/回位路线成为安全区，其他参赛者仍可能攻击。
存活成员到家重置血蓝、临时状态、仇恨及技能 readiness 属于世界的营地规则；
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
守护暂停（主人不在区内）或冻结时没有获胜时刻，不产生回防紧急度（2026-11-07 用户决定）。有到期时刻时
与服务器当前时间得到剩余量，再与本人到静态合法喷漆区入口的完整路径/当前移速所需时间、当前控制/保护剩余限制、
AI.CaptureReactionBudgetSec 的余量比较。被零移速或控制阻止时不能除零或假定立刻到位；
预算是保守估计，不是另一个喷漆/守护截止或额外等待时长。
公开期限逼近这个预算就停止新增刷野/购物投入，不能为“最后一只怪/再买一件”故意拖延。
已 Sell 成功而 Buy 未提交时，可以带真实空槽回防。

风险门只使用本人 HP/受压、当前可见接触压力、可达入口/退路与本人可用能力。
在止损线以下又无可用自保/有效入场路线时先脱险或换公开入口，不要求必死冲锋；
无法阻止时允许失败，不靠额外血量、传送或隐藏目标定位补救。
保护者可以合法移动接近，但不能自报有效争夺；进攻解除或真实到期由核心更新资格。

**喷漆、守护与围攻。** 争夺只暂停持有者的喷漆进度，离区、死亡、离局或被定身/眩晕/嘲讽/击退才清零，别人不能继承。
因此 AI 持有喷漆进度时，无论独自喷漆还是被争夺暂停，都留在区内迎战，不为追杀离区目标轻易放弃；
只有跌破止损线又无就绪自保时才撤离，并接受真实清零。争夺消耗次数用尽也不让暂停中的持有者放弃进度。

AI 自己是涂鸦主人时，只要未跌破止损线（含恢复中），就回到并留在喷漆区内与区内对手交战：守护只在主人独自在区内时计时，
主人离区、区内无人时守护暂停（2026-11-07 用户决定，原话：“胜利判断好像还是有问题 - 当旗杆被占领但无人在圈内的时候不计算时间，时间停止，现在好像是时间继续计算是吗”）；主人站在区内也会让任何人都无法独自喷漆；
主人以外有人进区时守护冻结，冻结期间主人从区内走出会让守护清零（2026-11-07 用户决定）。只有跌破止损线又无就绪自保时才脱离战斗，
因为他死亡会立即清除涂鸦（暂停或清零好过丢涂鸦），回到止损线以上后再回区续计。其他每个 AI 都回应对手的涂鸦（`BesiegeOwner`）：
在喷漆区外时，若可见的涂鸦主人在本人搜敌半径内，不论他站在哪里都去打他；否则走进喷漆区改漆，进区本身就冻结对手守护。
在喷漆区内先打公开的喷漆进度持有者，再打对手涂鸦主人，最后打区内其他对手。
AI 只能凭可见单位的授权 ContenderId 与公开 Occupier/Owner 相等来认出喷漆者或涂鸦主人，看不到就按普通可见对手处理。
争夺期间没有喷漆到期时刻，对手守护也因区内有人而冻结（`GuardDueAtMs` 为 0），不产生回防紧急度，按普通争夺或围攻进入；
对手主人不在区内而守护暂停时同样为 0，按普通围攻或进区改漆处理。

打断喷漆时，目标正是公开的独自喷漆者，第二档控制除定身外还会使用就绪的眩晕，或在嘲讽半径内使用自身嘲讽；
这些硬控在施加瞬间清零他的喷漆进度。受伤、施法或区内移动本身不由策略重置进度；达到显示期限也不自行判胜。

**恢复。** RetreatHPRatio 判断受压脱险，ResumeFarmHPRatio 是脱险后重新主动刷野的门槛。
恢复等待不禁选卡、重新评估公共回防或低风险移动；不要求满蓝才能普攻。
没有有效回复来源时，不永久等“回满”，转可达探索、真实可负担购物或合法喷漆/围攻机会。
躺平角（D21）是公开的快回地点，但不是安全区：恢复中、没有可见威胁且没有可负担的改善购物时，走向最近的、未被失败抑制的躺平角；
站进躺平角后先等快回再去购物，并一直留到生命回满才结束恢复（不在 ResumeFarm 线离开），可见威胁照常触发撤离，紧急回防照常优先
（`DemoOpponentPolicy.DormRecovery`）。对手正独自喷漆或守护其涂鸦时不走向躺平角，就地等待自然回复：走向地图角落会让公开回防紧急度每次决策都翻转。
躺平角判定与权威一致：本人脚点在公开 `DORM.RadiusCm` 圆内。
自保/治疗能力必须真的持有、可用且走同一 Combat 提交。

天降外卖（D26）读取公开对局视图中当前外卖的落点（只有已公开或已落地时才有），与真人看到的相同；
后续落点由服务器随机决定，AI 与真人一样只能在公告后反应。
没有恢复需求、没有可负担的改善购物、不在交战中的营地里时，走向该公开落点，站上点后原地等待落地；
它排在回防、喷漆、守护涂鸦、围攻和区内争夺之后、普通喷漆与打野之前，模式 `Airdrop`、原因 `ContestAirdrop`（`DemoOpponentPolicy.Airdrop`）。
抢外卖不是争夺旗杆：从外卖处止损撤离不计为一次喷漆失败，不会让 AI 失去回防资格。
拾取由权威按脚点判定，AI 不另行提交请求；自己是否抢到来自本人视图（`AirdropClaimedSerial`），公开视图不说是谁。

上热搜（D27）读取公开对局视图中上热搜者与模糊圆心，与真人看到的相同；上热搜者若不可见，AI 不知道其实时位置。
本人等级不低于 `LEADER.MinLevel`、生命不低于进入高级营地的门槛、不是上热搜者本人、不在交战中的营地里时，
走向圆内离圆心最近、未被失败抑制的公开地点（不含商店）；看到上热搜者后由普通交战分支接手。
走到圆内地点仍看不到上热搜者时，这次到达记入访问记忆，最近到过的圆内地点在重访间隔内都不再下单：不会每次决策在到达瞬间重发，也不会在走向下一个地点途中折回刚到过的地点；圆内地点都刚到过时回到打野。
它排在空投和普通喷漆之后、打野之前，原因 `HuntLeader`（`DemoOpponentPolicy.HuntLeader`）。

画饼老板 Boss（D26）只在合法视野内可见，与其他单位相同；AI 按其公开生物编号认出它，始终把可见的 Boss 计为威胁。
本人脚点在 Boss 的索敌带（`aggro_radius` 加它的 `attack_range`）内时，走向背离 Boss 的公开地点，原因 `AvoidBoss`；
这排在喷漆、守护涂鸦与围攻之后、追打对手之前；恢复中的 AI 照常以恢复撤离。AI 不主动攻击 Boss（`DemoOpponentPolicy.AvoidBoss`）。
走出索敌带后，这次撤离继续走到它的地点，直到走到、订单结束或 Boss 离开视野，不在带边折返；
否则每个决策节拍都会在撤离和把它带回 Boss 身边的原事务之间来回下单（#160，`DemoOpponentPolicy.BossEscapeKept`）。

### 6.3 普攻与每一个主动来源

持续普攻只由 Nav AttackUnit/AttackMove 的 AttackCycle 驱动。
接近距离使用本人当前 `STAT.AttackRangeCm`，不是初始 UNIT.BasicAttackRangeCm。
群攻由 Combat 结算，不让 AI 给每个隐藏副目标补发一次攻击订单。

对真正绑定的 Q/W 与 D 枚举自身 QueryAvailability，固定考虑：
有意义的合法自保/恢复 → 必要合法机动 → 可见威胁控制 → 合法伤害 → 保持普攻/移动。
优先级只决定策略先选哪个动作，不给效果追加类型或数值；SourceRef 和真实目录决定动作。
每个 Q/W 来源只读本人已学的那一档（`ResolveSource(Source, Rank, …)`，rank 来自本人构筑）：例如我没急的解除定身只在高级时存在，
初级不会被当作 Root 净化来源。施法用途全部由 typed 效果推出（`ReadEffects`），不按技能名称或构筑配方分支。
同一时刻只送当前动作系统可接收的一项；前摇、控制、资源、来源和占用均不得绕过。
成功 handoff 后旧运动不由 AI 私下恢复，按新合法观察重新决策；失败则消费真实回执。

| 档位与效果形状 | 当前目录中的条目 | 目标与使用规则 |
| --- | --- | --- |
| 自保：Self 且带护盾或解除定身 | `active_not_mad` | 需要防护，或 Root 妨碍脱险/进喷漆区（且本人已学高级）时优先使用；不能越过沉默、眩晕等施法限制。不治疗、不免疫伤害，不清 CD/死亡或重新保护。 |
| 机动：DirectionDash | `active_bail_out`、D 冲刺 | 撤退与抢旗杆用已选方向；追击（`ChaseDirection`）用该来源自己的冲刺距离判断：目标在“普攻距离 + 半个冲刺”之外、“普攻距离 + 一个冲刺”之内才朝其当前可见脚点冲刺。润了润了只在这一档使用，不因其强化普攻而在伤害档施放。 |
| 控制：带定身；或目标是公开的独自喷漆者时带眩晕/沉默 | `active_pants_down`；打断时另有 `active_crotch_grab`、`active_account_banned` | Unit；当前可见威胁的观察句柄，射程内才施放，执行时重查；不是凭公开喷漆者或涂鸦主人 ID 施法。 |
| 控制：Self 嘲讽 | `active_stare_down` | 只用于打断：目标是公开的独自喷漆者且在嘲讽半径内。 |
| 伤害：Unit | `active_pants_down`、`active_crotch_grab`、`active_elbow`、`active_account_banned`、`active_unplug` | 当前可见目标在射程内时施放；击退、缴械等附带效果由 Combat 结算，AI 不另行规划。 |
| 伤害：DirectionProjectile | `active_snark_bolt` | 用当前合法可见目标形成方向，归一化/几何由 Combat 重验；不随隐藏目标持续修正。 |
| 伤害：GroundPoint 区域 | `active_quick_meeting` | 以射程内可见目标的公开脚点为落点（`FLDOpponentBodyIntent.Point`，请求构造为 `ELDCombatTargetKind::Point`）；落点由 Combat 夹到射程。 |
| 伤害：Self 区域 | `active_seethe` | 目标在其自身区域半径内才施放。 |
| 伤害：Self 强化普攻（无位移） | `active_triple_combo` | 目标在本人当前普攻距离内才施放，随后由普通攻击订单消费强化。 |

D 键通用冲刺（D19）是每名参赛者的固定来源，AI 经同一 owner view 的 `ELDCombatEntry::Dash` 读到它
（`ELDOpponentSourceSlot::Dash`），位移距离取权威给出的 `RangeCm`（含《润学指南》加成），不在策略里重算；
冷却、定身与起步阻挡都在 Combat 重验，`bReady` 只取权威的可用原因与 ReadyAt。它在“必要合法机动”这一级使用：

| 时机 | 方向与条件 |
| --- | --- |
| 追击 | 正在交战的可见对手参赛者（不是营地野怪）离自己超过“普攻距离 + 半个冲刺”且不超过“普攻距离 + 一个冲刺”，本人未在撤退、未定身：朝其当前可见脚点冲刺，落点在普攻距离内。 |
| 撤退 | 脱战撤退（低血且有可见威胁）时，朝已选的公开撤退地点冲刺。 |
| 抢旗杆 | 公开的对手喷漆到期或对手守护获胜时刻让回防变得紧急时，朝旗杆冲刺；不紧急的进区不花冲刺。 |

验收见 `Lockdown.Rules.DemoOpponentPolicy.InnateDash`；满员局中的实际冲刺由 `RunDemoOpponentCheck.ps1` 记录为报告项（§8.2）。

装备没有主动；首版七个装备定义逐项绑定，AI 只消费其统一派生属性与被动，不为装备发动作请求：

| 公开装备定义 | 内容域给定的 typed 能力 | AI 使用与目标规则 |
| --- | --- | --- |
| `item_iron_plate`、`item_edge_blade`、`item_runic_focus`、`item_siphon_emblem`、`item_arcane_battery` | 具体 Stats/Passive 由内容域定义 | 正常购买/装备后消费统一派生与被动，无主动按钮或伪施法。 |
| `item_phase_anchor` | `passive_stride` 的 `dash_boost` 强化 D 冲刺 | 由 Combat 投影进 D 冲刺的 `RangeCm` 与冷却；AI 不为它发主动请求。 |
| `item_echo_maul` | `passive_cleave` 响应 PrimaryBasicContact | 通过正常普攻触发，AI 不向副目标追加攻击订单；不把群攻变成隐藏单位锁定。 |

角色主动按真实 TargetMode 进入同一执行通路；Unit 使用授权句柄，
Direction 使用当时获准方向，GroundPoint 使用当时可见目标的公开脚点，Self 检查本人状态。
尚未提交、依赖已撤销观察的瞄准计划作废，已经释放的独立效果不会持续修正隐藏目标。
验收见 `Lockdown.Rules.DemoOpponentPolicy.V7Activations`。

来源失效后等待正式新绑定/ReadyAt，不自己清冷却或把换槽当新来源。

### 6.4 持久选择与构筑

只枚举队头三张卡及其合法替换结果。选择规则固定为：
先补角色主动中缺少的交战/自保用途且尽量不丢现有唯一用途；
随后依次考虑已有技能升级、新主动填空、新被动、属性、其余需要替换的新主动。
同层先减少要遗忘的已投入等级（新主动从初级开始，被替换技能高于初级的等级算作遗忘），再用公共定义稳定顺序和槽位顺序消歧。
三张里最好的只是属性卡或会丢失现有用途的替换时，若本人还有免费重抽令牌且本组尚未重抽，就提交一次重抽
（同一选择请求路径，`OptionId` 为空），收到新组后按同样规则选择；重抽后的组不再重抽。
用途从已批准 typed 效果读取：伤害/控制用于交战，自身防护/恢复用于自保，
不靠名称、未学习技能的私有状态或临时战力公式推断。
这能处理两个交战主动已满时选择自保并明确替换，也能处理被动/属性卡，不强迫卡片类别各异。

选项内容、目标 rank、必要旧技能 ID/rank/槽位和 Offer/Build/LifePhase 组成一次确认条款。
不同请求不得竞争消费同一机会；冲突后重新读原 Offer 和本人构筑，重新决定是否确认，
不改卡、不跳队头、不看未来随机结果。成功后才处理下一组，最高等级仍可处理已积累机会。
死时使用同一选择器但以当前 Dead 阶段提交；属性改变由统一投影保持资源比例，不复活/治疗。
遗忘不建立备用技能库；被遗忘的技能以后重新得到从一级开始，readiness 沿来源域原期限，不以清 CD 作为选择收益。

**装备偏好。** 对当前装备对及每个可负担候选对，按以下结构键作字典序比较：
已填槽数、是否覆盖普攻发育、是否覆盖自身生存、合法主动来源数、不同被动定义数。
普攻用途来自目录的正向攻击/攻速/攻击距离或明确普攻增益；
生存用途来自正向最大生命/双抗/生命回复或明确自身防护/恢复。
这是编译后 typed 字段的有限判别，不新增可调 DPS 权重或真实战斗公式。
空槽优先填；同层用公共目录稳定顺序消歧。同款的两个主动算两个合法来源，
同名被动只计一次；真实属性加成只相信统一投影。
这是一种有取舍的固定策略，不是宣称存在经平衡验证的最优构筑。

满槽换装必须让完整候选装备对的上述实质偏好严格改善；
只有稳定 ID 排名改变不允许卖买，不因某件主动当前冷却而卖出。
到店前可用同 CatalogDigest 的公开 `ITEM.<id>.BuyPrice`、出售规则及本人余额/装备
作非约束性预算，也可评估公开规则下的预计卖出回报；不需要 ShopView、会话或 Permit。
这只是决定是否值得前往，不改钱包、不授予装备、不预支未来击杀或实际成交，
所以首次购物或旧报价失效时仍能主动到店。
实际交易必须等待 Transactions 返回 Opened，再用合法报价和当前本人状态重新评估价格、
出售回报与完整换装结果；不符合就重新计划或离店。
开店 Pending/Rejected 没有可用 View，不发 Buy/Sell；AI 也不持有或制造 Permit。
先明确 Sell 具体实例，收到最终成功后重读余额/库存/来源/报价再用新请求 Buy。
后买失败保留已售事实和空槽，不恢复旧物、不补钱、不伪造新实例。
同一失败换装计划的预算不因自己导致的库存 revision 自动重置。
没有值得的买卖时，本局尚未进阶且余额够 `SHOP.UpgradeCost`，就在已开店会话里按槽位顺序进阶第一件可进阶的已穿装备
（商店 Upgrade 命令，同一回执与预算路径）；进阶状态随该件出售而失去，AI 不为再次进阶而卖出。
自身装备每次变动后重新读取 abilities，确保任何新装主动不是“只买不用”。

### 6.5 有界重试与可观察决策

失败记录按“公共 POI 或当前业务目标 + 原因类别 + 相关本人条件指纹”保存，
目标撤销时不留下单位位置/血量历史。固定地点有限；只保留当前观察和在途请求，
不随无限时长对局积累全单位/全回执日志。
RetryPolicy 的 MaxAttempts 包含首次提交，退避逐次增加且不超过 MaxBackoffSec；
次数到界后明确进入目标抑制或等待原域终态，不偷偷无限生成新 token。
争夺旗杆也是有界投入：一次回防、争夺或围攻以跌破止损线撤离告终，就记为旗杆在当前构筑下的一次失败；
用满 MaxAttempts 后不再回应旗杆，直到等级、选择或装备改变构筑，以免较弱一方反复进区又被打出。
自己独自喷漆或自己是涂鸦主人时不记这类失败。
导航在准入时以地面或路径理由拒绝行走（移动、到店或追击）且换了一个目标仍被拒时，问题在身体所站之处而不在目标：
这类拒绝另记在“本人行走 + 所站位置（精确到厘米）与生命”上，同样按 RetryPolicy 退避，用满后在原地不再发起新的行走，
直到身体移动或复活；任何一次行走被受理即清零（#160：一个落在无支撑处的身体曾每个决策节拍换一个地点下单，30 秒 80 个请求；
`DemoOpponentPolicy.WalkRefusals`）。满员场景的“请求有界”检查把受理时与准入时的拒绝都计为被拒订单。

无路/无进展先消费导航域的有限重算和失败，AI 才换另一个合法公共接近点/POI，
不叠加第二套卡路计时。资源拒绝等 readiness/相关资源变化；控制拒绝等对应限制解除；
Offer/报价冲突等同域终态或相关修订；普通帧更新不刷新次数。
回执不明只查询原 ID，查询预算耗尽后保持显式 AwaitReceipt/诊断并允许无冲突脱险，
不能宣称成功或重新买一次；收到终态/拒旧水位并重新同步后才解除该等待。

每个决定输出受限原因码及所用输入版本，至少区分 NoLegalTarget、Recover、
PublicCaptureUrgent、HoldCapture、GuardGraffiti、BesiegeOwner、ContestCapture、NoAffordableImprovement、AwaitReceipt、TargetSuppressed、
ProviderUnavailable、Stopped。日志可解释“为何继续发育/为何回防/为何失败”，
但不得记录从未授权给该 AI 的隐藏坐标、钱包或内部来源，再反向作为决策输入。
每局关闭时，每个 AI 另输出一行 `LD_AI_SUMMARY`，汇总本局送出的订单、Stop、动作、开店、选择、交易次数，
当场被拒的次数，以及争夺暂停下的坚持/争夺决策计数和打向对手涂鸦主人的攻击订单数（`owner_attacks`）（`ALDOpponentController::GetRequestTally`）。它用来核对等待、拒绝和重试是否有界，
只是证据，不是策略输入。

## 7. SC-09：参数与内容唯一来源

以下键由内容域定义并随 D01 批准，**本文不提供第二份数值表**。
由 `CONTENT_AND_BALANCE.md` 维护具体数值、批准依据、单位、范围及生成映射。
运行时不读 Markdown；未批准/缺字段/非有限输入须由唯一 compiler/校验入口拒绝，
不能以旧 Routines 常量或技术默认值补齐。

| 参数键 | 类型/单位与消费 | 必须可检验的约束 |
| --- | --- | --- |
| `AI.RetreatHPRatio` | 无量纲比例；受压脱险线 | 在零至一的有效健康比例域内，低于 ResumeFarmHPRatio，避免往返振荡。 |
| `AI.ResumeFarmHPRatio` | 无量纲比例；恢复后重新主动刷野 | 不要求满蓝、不阻止死时选卡/公开回防；与低档止损和入场能力一致。 |
| `AI.HighCampEntryHPRatio` | 无量纲比例；高档入场的当前 HP 门槛 | 高档还须等级 ≥ 2 或已有装备（开局三选一属于起始构筑，D10）并通过保守成本评估；不保证“进了就能打赢”。 |
| `AI.HighCampReserveHPRatio`、`AI.LowCampReserveHPRatio` | 无量纲比例；相应营地预测清空后的剩余 HP 门槛 | 使用本人实际构筑与公共目录的限定成本模型，不读隐藏血量；不是当前 HP 止损线/额外免伤。实际止损沿 RetreatHPRatio，模型不能保证第三方干扰下安全。 |
| `AI.CaptureReactionBudgetSec` | 正有限秒数；回防估计余量 | 包含常规规划/排队的不利延迟，不是等待此时长才行动；不得改变喷漆或守护的真实截止。 |
| `AI.DecisionIntervalSec` | 正有限秒数；常规策略节拍 | 三 AI 最坏调度可纳入回防预算；终局/身份/观察失效立即处理，长帧不补算旧决定。 |
| `AI.RetryPolicy.MaxAttempts` | 正整数，含首次 | 有界业务/路径尝试；缓存过窗不是重新发起同一操作的理由。 |
| `AI.RetryPolicy.InitialBackoffSec`、`AI.RetryPolicy.MaxBackoffSec` | 正有限秒数 | 最大值不小于初值；重试只在原操作确定终结/拒旧及当前合法条件下发生。 |
| `AI.ExploreRevisitIntervalSec` | 正有限秒数 | 只描述本 AI 公共 POI 再探节奏；不随隐藏营地刷新事件调整。 |
| `AI.EngagementBudgetSec` | 正有限秒数 | 覆盖批准基础低档战斗的合理投入；同一次目标/营地过程累计，不随普通攻击、Order 或帧重置。 |
| `STAT.AttackRangeCm`、本人移速/血蓝/readiness | 原领域权威派生；厘米、厘米/秒、资源 P2、绝对服务器毫秒 | AI 不用初始射程覆盖成长，不另存技能 CD；字段单位由提供者定义并集中转换。 |
| `ABILITY.<id>.TargetMode/RangeCm/WindupSec/CooldownFamily/Rank[r].ManaCost/CooldownSec`；`ITEM.<id>.BuyPrice/Stats/Passive/Active` | 公共定义；按字段单位/类型读取；装备效果子字段见 §6.3 | 技能最高两档；按实际目标模式（Self/Unit/DirectionProjectile/DirectionDash/GroundPoint）覆盖全部非空来源；未知 enum/缺作用条件拒绝而非默认为普攻。 |
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
| `ChoiceOrder` | §6.4 选择顺序与明确替换 |
| `EquipmentPlan` | §6.4 装备偏好、严格改善与换装计划 |
| `CaptureUrgency` | 回防紧急度：路径、移速、控制、预算，零移速与未知路径 |
| `RetryBounds` | §6.5 有界重试、退避与指纹解除 |
| `Priorities` | §6.1 优先级、撤离与回防、有界争夺、D10 开局选卡与高档营地门槛、争夺暂停下的坚持与争夺；喷漆/守护/围攻：涂鸦主人守区（恢复中未到止损线也回区留守，离区守护暂停）、跌破止损线才脱离、守护暂停时对手不产生回防紧急度、区外围攻可见主人、区内先打喷漆者再打主人、对独自喷漆者用眩晕/嘲讽打断 |
| `Projections` | 本人/观察帧映射与 FName 大小写规范化 |
| `Outcomes` | 回执结算对失败记录与当前目标的影响 |
| `Requests` | 意图→请求构造与回执归类 |

### 8.2 正反验收与证据

`RunDemoOpponentCheck.ps1` 已部分覆盖下表：满员房间（参赛 Listen Host、两个拥有客户端、三个 AI）经正常大厅开局，真人不参与。
它核对 AI-A01 的独立绑定与不补位，以及 AI-A02 中的每个 AI 自主移动、获胜 AI 升级并确认选择、至少一个 AI 到店购买、
死亡期间不发任何身体请求、死亡后按批准时长复活并继续行动、请求速率有界、由 AI 按喷漆 + 守护规则以 `GuardSucceeded` 获胜且获胜 AI 是公开涂鸦主人，最后回到新 Lobby 并退役全部 AI。
它还核对每个 AI 在开局十秒内处理掉开局三选一（D10）；旗杆开放前每个 AI 都取得过经验，除非发育期至少一半时间处于死亡或回血恢复（1 级开局互打后按批准回血等回 ResumeFarm 可能占去大半个发育期，这是批准节奏，不算卡住）；以及争夺暂停下的两类决策：
AI 持有被暂停的喷漆进度时留在区内迎战，只在止损线离开；AI 在区内面对对手的暂停进度时继续争夺，不离开去刷野或购物。
这两类决策由各 adapter 按自己的帧计数（`paused_holds*`、`challenges*`），另报打向公开喷漆进度持有者的攻击订单数（`holder_attacks`）和打向对手涂鸦主人的攻击订单数（`owner_attacks`），仅作证据，不回送策略。
死亡与复活、以及争夺暂停取决于对局走向：本局没有 AI 死亡、复活或相应争夺时，相应各项报告为未覆盖（NotExercised），单独列出，不计为通过。
“每个 AI 都到店购买”与“至少一个 AI 实际完成 D 冲刺”（各 adapter 计已提交的冲刺 `dashes` 与 Combat 完成的冲刺 `dashes_landed`）只作报告项：
未出现时记为 NotExercised，不卡门槛；多局、打包版下是否稳定见 [#117](https://github.com/rlsn/Lockdown-alpha/issues/117)，验证后再定是否升为硬门槛。
下表其余条目，以及这两条中 runner 没有覆盖的部分（零到二个 AI 开局、真人离开、每个 AI 各自的成长、逐段回执链、三个槽位分别获胜），
目前没有自动验收。
纯模型证明策略决定，原生场景证明接线，真实拥有连接证明授权和运行拓扑，三者不能互相冒充。

`RunDemoOpponentCheck.ps1 -Telemetry -MatchCount N` 是另一回事：一名无渲染、闲置的 Listen Host 与三个 AI 连打 N 局，
每局记录时长、首杀、每名参赛者的等级与金币曲线、购买、喷漆尝试与争夺次数和胜负方式，
以及 #112 §13 的度量（首个主动与高级技能数、重抽与主动替换、每次死亡的击杀者/助攻者与奖励、喷漆/改漆/涂鸦清除原因与守护用时），并汇总为一份报告
（字段见[开发指南](../DEVELOPMENT.md)）。它只是对局数据，供 AI 与节奏评审参考：不核对上表任何一项，
也不把某个数字当作目标；真人闲置时 AI 也会击杀 Host，这些击杀照常计入。

| ID / 场景 | 正例与反例动作 | 精确可观察结果 |
| --- | --- | --- |
| AI-A01 名单与独立性 | 零/一/二/三 AI 分别开局；满规模 Host+两 owning clients+三 AI；断开一真人 | 与 Host 选择数量一致，不补人/不接管；各 AI 有不同 Contender、独立观察/请求/钱包，仍各自 FFA。 |
| AI-A02 真实闭环 | 从无装备/仅普攻开局，逐个 AI 观察成长、选卡、到店买卖、主动、撤退恢复、回防、死亡复活、喷漆、守护与围攻 | 每段能由该 Contender 的正式订单/死亡奖励/Choice/Shop/Action/结果回执连起来；不是 Script 代发策略。分场景证明三个 AI 都能成为唯一胜者，不要求同一局三胜者。 |
| AI-A03 隐藏状态非干扰 | 固定同一授权输入/自身状态/静态图/平局种子，改变隐藏敌人坐标、实例、CD、技能/属性/金币和隐藏营地刷新；另只增加另一 AI 的合法观察 | 本 AI 的决策及请求条款不变；没有跨 AI 感知共享。再用真实遮挡/帧载荷证明生产者确实过滤，不只给模型手写空帧。 |
| AI-A04 字段白名单/死亡视野 | 当前可见时检查姓名/等级/HP/MP/状态/展示装备；遮挡、死亡、拖静态镜头、公开喷漆/涂鸦状态与击杀通知 | 动态字段撤销，无技能/CD/内部实例/属性/金币或 LastSeen；死亡无新身体视野；公共信息不附精确隐藏坐标。 |
| AI-A05 目标撤销/可见死亡 | 在提交前、追击、前摇中 Conceal/stale；晚到旧视图/ACK；另给予匹配原订单/handle epoch 的 FLDObservedUnitTermination 授权死亡 | 失视野终结 AttackUnit/追敌 A，无旧 A 目的地/LastSeen 续走；前摇中失视野的普攻仍按 2026-11-07 规则出手命中，订单在出手后才结束；再次可见须新 Order。只有未取消原单的授权死亡能走操作域原 A 继续；隐藏死亡/移除/离局不行。 |
| AI-A06 支持面/不可达/卡路 | None/未知/错误 SurfaceHint、无真实 CurrentFloor 但 XY 在喷漆区内；无完整路径、阻挡更新、受控停走、目标换生命、异观察者 handle | 四参 QueryGround 明确拒绝 InvalidSurface/WrongSurface/NotGrounded，无空 hint/默认面/向下投影兜底；无 floor 不计入喷漆区但不冻结已处理边界。无 partial/穿墙/传送/隐藏实体泄露；控制不误计卡路，有限重试后换合法 POI。 |
| AI-A07 订单/回执竞争 | 新意图 Queued 后拒绝/接受，Stop 后延后 ACK/Outcome；同 ID 同/异条款，过窗，Query 已取消单；Choice/Shop 查询覆盖所有 LookupStatus 与旧局同号 | Queued 不替旧单；handoff 原子；旧包不续走/续打/延寿；冲突/过窗明确，零重复提交。查询 Pending 只有 PendingInfo，Final 只有匹配业务 Receipt，缺失/过窗/错上下文无伪终态或默认生命载荷。 |
| AI-A08 持久 Offer 竞争 | 两次重复/不同选择请求、Build 冲突、连升、满 Q/W 替换、全部技能高级、死前 Alive 请求延到 Dead、Dead 选择与复活竞争 | 同一机会只消费一次，原卡保留不重抽；十次（含开局一次）/十级/高级边界；明确遗忘/再学从初级开始。当前 Dead 选择不治疗/提前复活；旧 LifePhase 确认拒绝而候选不变。 |
| AI-A09 买卖资格/失败 | 无 ShopView 但公开目录预算足够的首次/旧报价失效购物；开店 Pending/重复/取消/Opened/Rejected；LOS 被挡；旧路径、非法区域、无钱/满槽/旧报价/旧世界修订/越界/死亡/终局/分配失败；Sell 后 Buy 被拒及重放 | 无 ShopView 仍会据非约束预算主动到店；Pending 不提交 Buy/Sell。每人一个待办，只有核心 Transactions 借当次 Permit 进内部 OpenPendingShop/资格查询；只有 Opened 带 View并触发重新预算，后续交易再验当前条件且无额外 LOS。失败不部分改钱物，已 Sell 不倒回，无免费实例或预算刷新。 |
| AI-A10 来源与全目录 | 穷举三角色主动，guard_pulse 在有 Root/无 Root及其他施法限制下释放；D 冲刺（含《润学指南》加成）；换槽/遗忘重学/卖买/死亡；只有属性/被动的装备 | 真来源走同一 Combat；guard_pulse 只有成功释放才给屏障/移除全部 Root，不清其他控制、CD或死亡，不把 Root 误判成禁 Skill。属性两份/同名被动一次，保留原 CD；装备不伪造主动能力，echo_maul 无副攻击订单。 |
| AI-A11 进攻解除保护 | 喷漆区内保护 AI 发当前有效新意图，分别 Missing 目标/错误支持面/未接地/无路/距离远/蓝少/CD未到；对照 Query、旧包、异生命、终局 | 前组为语义执行失败，即使 Rejected 也 ProtectionRemoved/LifeRevision 更新，失败不扣动作 MP/CD；资格事件在 Capture 前生效，保护时段不追算喷漆，未接地仍无资格；后组不解除。 |
| AI-A12 死亡/离局效果 | 正常死亡时有已释放独立效果，效果击杀合法目标；另在效果存续时正式离局 | 正常死亡继续原期限并正常发给仍在局死者奖励，可产生 Dead Offer；离局立刻移除身体/效果且离开不发奖；均不能打中同 ID 新生命。 |
| AI-A13 发育/回防/恢复 | 当前 HP 达高营入口但预测剩余不足 Reserve；合适低营；同刻反击/零伤害/超预算/隐藏成员变化；公开喷漆或守护紧急，低血/零移速/保护/无回复，区内敌人离区 | Reserve 只控制模型入场，实际止损沿 Retreat；成本估计不写 HP/CD/RNG、不读隐藏状态、不补最低伤害或抹同刻反击，不将估计当实战保证。回防无除零/全知/等回满死锁，不为一只怪拖延；必要离区接受喷漆清零。 |
| AI-A14 终局/新局/长帧 | 结果边界同时有在途攻击、奖励、选卡、Sell/Buy、复活和 AI 决策；延迟旧通知到下一局；长帧/长期运行 | Close 后新增玩法提交为零；不补跑历史决策；缓存/队列/每 AI 工作有界；仅新 Match 的上下文可继续，旧回包不唤醒策略。 |
| AI-A15 营地反击/Returning | 路过/碰撞/预览/挥空/被拒命令，对照确认合法命中且首个成员当次死亡；遮挡同组/目标；回位途中 Root/伤害/击杀、存活到家 | 只有确认命中触发存活同营反击，各自重查观察不共享坐标。可见 camp_returning 无仇恨/期限/路径，Root 保留、退出或 Conceal 撤销；Returning 可伤控杀不重追，不限制真人追杀。死亡正常恰一次奖励；到家重置不发奖/不复活死者，隐藏重置不进入 AI 输入。 |

运行产物写忽略的 Artifacts。每条证据至少带本次 commit/内容摘要、真实连接拓扑、
Match/Contender/BodyRef、授权 Frame/Map 修订、策略原因/公共目标、
Order/Action/Choice/Shop 请求和回执、稳定死亡奖励键、核心阶段/结果。
权威 observer-checker 可以比较真值以检查泄露，但不得把这份全知检查数据送回策略。
只看屏幕或 Host 日志、仅 SpawnActor 模拟“human”以及模型实验都不足以通过 AI-A02。

## 9. 已知限制

- **节奏。** 1 级开局与对手互打后，按批准的 RetreatHPRatio/ResumeFarmHPRatio 与 1 级基础回血，AI 可能要等约 2 分钟才恢复刷野，
  个别 AI 会整个发育期停在 1 级。这是批准数值下的节奏，是否调参由试玩后决定，策略不另加回复。
- **覆盖。** §8.2 表中 runner 未覆盖的条目没有自动验收；死亡复活与争夺暂停的场景检查取决于对局走向，可能报告 NotExercised。
- **公平性证明。** 策略只读获准输入由结构保证（纯策略不接 UWorld/Actor，帧只由授权投影组成），
  但尚无改变隐藏状态的非干扰测试（AI-A03），也没有 AI 侧的字段白名单场景（AI-A04）。

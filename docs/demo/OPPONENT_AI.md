# Demo 对手 AI

**目标：v0.10.0 · 职责：M12 · 引擎：Unreal 5.8.4**

本报告是 [App Spec](../APP_SPEC.md) 的实施准备，不是已经实现的 AI 或运行验收声明。
共同身份、时序、权限及回执遵循 [共享契约](SHARED_CONTRACTS.md)；
依赖与裁剪编号引用 [架构审计](ARCHITECTURE.md) 和 [实施计划](IMPLEMENTATION_PLAN.md)。
现有机制与拟建接口分别标明；参数数值只由 `CONTENT_AND_BALANCE.md` 维护，
不把旧 SourceData、策略估计或静态分析当作已批准的运行输入。

## 1. 范围与已确认约束

对手 AI 必须是**能独立完成成长并按相同条件获胜的参赛者**，不是占席、巡逻或靶子。
仅选择一种固定优先级策略，不引入通用 GOAP、人格、多难度、学习平台或第二套世界模拟。

| 边界 | 本模块遵守的产品约束 |
| --- | --- |
| 名单 | Host 选择零至三个 AI；与最多三名真人（含 Host）共存，各自 FFA，不补满、不组队、不接管断线真人。 |
| 起步与成长 | 同一基础角色，初始近战单体物理普攻；正常打野/对抗获得最后一击的经验和金币，再三选一、购物、争圈。装备/属性可以增加攻击距离或群攻。 |
| 营地反击与回位 | 未受攻击营地不主动攻击路过者；确认的合法命中触发同组存活成员反击，即使被击成员当次死亡，也不共享隐藏位置。预览/挥空/被拒命令/单纯碰撞不触发。Returning 仍可受伤、控制、被杀，不回头追击；存活成员到家满血蓝、清临时状态/仇恨、技能就绪，不复活已死成员、不因回位发奖。 |
| 技能与装备 | Q/W 两个角色主动槽；装备 1/2 不占 Q/W；角色被动无额外槽位上限；最多九次选择、每技能最高三级。满 Q/W 学新主动必须明确确认替换并遗忘旧技能及等级。 |
| 同款装备与冷却 | 两件同款合法，基础属性相加，同名被动只生效一次；两份主动有独立冷却、可以连续使用。换槽、售出重买、遗忘重学和死亡不能洗冷却；失去来源期间原期限继续流逝。 |
| 死亡 | 保留成长、财产、候选和冷却；死亡等待可以确认三选一，只更新持久构筑，不治疗尸体或提前复活。最大血蓝变化按统一公式保持原百分比；AI 不另算公式。 |
| 保护 | 经过身份、当前 Match/Life、请求新鲜度、形状及写闸检查的进攻意图先解除保护；随后目标、距离、路径、法力或冷却检查失败也不恢复保护。只读查询、普通移动、商店、Stop 和本地预览不是进攻。 |
| 视野 | 失视野取消单位锁定并停止该追击订单，不续走 LastSeen，不保留地图 LastSeen 单位标记。只读取当前可见对手的名称、等级、血量、蓝量、状态和展示装备；不读取技能、冷却、内部实例/构筑、属性或金币。 |
| 死亡与离局效果 | 正常死亡后，已成功释放的独立效果继续至原期限，在局的阵亡所有者仍可获得合法击杀归属。正式离局/断线立即移除身体并取消其未结束独立效果；AI 不追踪已离局身体。 |
| 结束 | 唯一终局闸关闭后停止新增意图；返回 Lobby 的新局重新初始化，不沿用旧目标、订单或局内战力。死亡无活体视野；能查看静态地图不等于获得隐藏玩家跟随。 |

AI 自动确认的是**自己的**候选和替换，不替真人选择。商店不是安全区，选卡不暂停世界。
购物不额外检查 LOS；仍须存活、比赛进行中、真实位置处于合法区域并满足交互距离。
恢复只能依靠同规则的自然回复或已拥有的合法能力，不能通过重建身体、刷新构筑或调试授予恢复。

## 2. 现有源码证据与缺口

源码核验基线为 `fa11d09a762bccadf495191072e222dd158fc014`；
研究工作区文档基线 `7e7a40d39ec2f99f18b809b83e89479bd34a823d` 相对此提交的
Source、Config、Content、SourceData、Scripts 均未改变。以下行号对应这一源码基线。
没有运行 Unreal、构建、生成器、Bootstrap 或实际对局，也没有加载二进制资产。

| 证据 | 当前真实行为 | Demo 处置 |
| --- | --- | --- |
| [LDB92Roster.h:87–104](../../Source/Lockdown/Public/Population/LDB92Roster.h#L87-L104)、[LDB92Roster.cpp:29–45](../../Source/Lockdown/Private/Population/LDB92Roster.cpp#L29-L45) | Seat 经 B92/B91 继承 PlayerController，恒非 local，BeginPlay 关闭 Actor Tick；没有自主策略。 | 不继承 Seat/B92/B91，不把三张席位当三 AI。 |
| [LDB92Roster.cpp:74–199](../../Source/Lockdown/Private/Population/LDB92Roster.cpp#L74-L199) | 初始化二十四席、职业起始构筑及其他人口；日志明确 `contenderPolicy=not_implemented`。Transfer/Admit/Release 搬运身体/账户并在断线后接管。 | D06 全链退役，核心按所选数量创建真正参赛 AI；不迁移接管功能。 |
| [LDB92Routines.cpp:25–101](../../Source/Lockdown/Private/Population/LDB92Routines.cpp#L25-L101) | 日程自己的 StrategicRoute、MoveToLocation、全身体 Danger 扫描和 Combat 感知。 | 不是公平参赛策略，不保留日程、全知 Sense 或第二路由器。 |
| [LDNavigationComponent.cpp:66–124](../../Source/Lockdown/Private/Navigation/LDNavigationComponent.cpp#L66-L124)、[237–305](../../Source/Lockdown/Private/Navigation/LDNavigationComponent.cpp#L237-L305) | Owner 强依赖 PlayerController；Navigate/Stop 为 local-only；服务器检查后，真正沿路径 AddMovementInput 又在 local 条件之后。 | 同组件抽取服务器方向驱动和领域入口，不能假造 ClientOrder 或 LocalPlayer 使机器人行走。 |
| [LDNavigationComponent.cpp:126–204](../../Source/Lockdown/Private/Navigation/LDNavigationComponent.cpp#L126-L204)、[LDCharacterMovementComponent.cpp:22–98](../../Source/Lockdown/Private/Character/LDCharacterMovementComponent.cpp#L22-L98) | 有完整路径、Match/Life/Order/Epoch、回执/拒旧、SavedMove 和原生运动；当前新收件会先停旧单。 | R01/R05 局部复用；目标改为 Queued 不替旧单，完整准入后 handoff，不写第二位置积分器。 |
| [LDGameMode.h:78–93](../../Source/Lockdown/Public/Framework/LDGameMode.h#L78-L93)、[LDGameMode.cpp:378–453](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L378-L453) | Session 是持久参赛身份；当前以 Controller 找记录，死亡仍扣金/掉落，出生仍有安全/原席位政策。 | R06 由生命周期域改造；AI 不直接写 Contenders、不生成 Life 或选择“安全”出生点。 |
| [LDSkillExecution.cpp:17–34](../../Source/Lockdown/Private/Combat/LDSkillExecution.cpp#L17-L34)、[LDContentActions.cpp:36–55](../../Source/Lockdown/Private/Combat/LDContentActions.cpp#L36-L55) | PlayerController 构筑来自参赛记录，其他身体可能落入 NPCBuild；RequestContent 接受已经解析的 Actor，依赖外层准入。 | 参赛 AI 不用 NPCBuild；只进统一 SubmitAction，不把 C++ public 当成授权 API。 |
| [LDContentLifecycle.cpp:35–57](../../Source/Lockdown/Private/Combat/LDContentLifecycle.cpp#L35-L57)、[LDVisionDirector.cpp:137–186](../../Source/Lockdown/Private/Vision/LDVisionDirector.cpp#L137-L186)、[285–300](../../Source/Lockdown/Private/Vision/LDVisionDirector.cpp#L285-L300) | 范围内循环普攻存在，非完整追击；Footstep/Trail 有产生和提前授权旁路。网格观察不等于 B91 稀疏观察。 | R07/R12：同一订单驱动普攻，世界统一稀疏 FLDWorldView；未许可提示同时删除产生/授权，不只隐藏图标。 |
| [LDWorldDirector.cpp:35–110](../../Source/Lockdown/Private/World/LDWorldDirector.cpp#L35-L110) | 旧三营地/Boss/Tower、有限刷新；Reward 先账本后 XP，PvPKill 只给 XP 且有限额。 | M9C/M6 提供五营地与稳定死亡键的正常复合奖励。AI 不补发钱或经验。 |
| [LDDevelopmentDirector.cpp:75–207](../../Source/Lockdown/Private/Development/LDDevelopmentDirector.cpp#L75-L207)、[LDVendorDirector.cpp:19–92](../../Source/Lockdown/Private/Inventory/LDVendorDirector.cpp#L19-L92) | 是旧准备/读书/售书等链；尚非持久三选一与两装备买卖。 | 复用身份/版本/事务机制，等待成长域真实实现目标接口，不用旧服务冒充闭环。 |
| [LDV08Loadout.cpp:22–77](../../Source/Lockdown/Private/Development/LDV08Loadout.cpp#L22-L77)、[LDContentActions.cpp:39–40](../../Source/Lockdown/Private/Combat/LDContentActions.cpp#L39-L40)、[LDEconomyLedger.cpp:36–103](../../Source/Lockdown/Private/Economy/LDEconomyLedger.cpp#L36-L103) | 来源失效、条款比较和全验证后提交有价值；旧同族动作排斥/取最大冷却不能实现同款双主动。 | R08/R09 保留不变量，不保留旧槽数/同族锁；AI 仅消费正式 readiness 和真实成交回执。 |

## 3. SC-09：状态、正式接口与权限

### 3.1 唯一新增适配，不新增领域所有者

选定拟建 `ALDOpponentController : ALDPlayerController`，文件为
`Source\Lockdown\Public\Population\LDOpponentController.h` 和
`Source\Lockdown\Private\Population\LDOpponentController.cpp`。
它直接继承精简后的现有基类，**不经过 B91/B92**。使用这一无连接服务器适配器，
是为了复用当前 StateFor/BuildFor/Holder 绑定，不是模拟真人网络连接。

核心创建同类 `FLDContenderRecord.Session`，按 `ELDContenderKind` 显式登记 Human/OpponentAI；人数统计不能依靠
`IsPlayerController()`。适配器无 LocalPlayer、输入映射、UI、拥有连接或客户端 RPC 通道，
不以 `_Implementation` 调用跳过领域验证。共享基类中的本地初始化与表现必须按真正本地拥有者隔离。
GameMode 的注册/注销仍是唯一入口，不让新 AI 获得旧 Roster 的 friend 写权限。

| 状态 | 正式写入者 | 拟建类型/函数、AI 能保存的内容 |
| --- | --- | --- |
| AI 名额、Contender、Match、Life、阶段和复活 | GameMode 生命周期域 | 复用 `FLDMatchContext{Generation,LedgerMatch}`、`FLDVictimKey{Kind,EntityId,Life}`、`FLDCombatBodyRef{Victim,BodyIdentity}`；AI 的 Victim.Kind 必须为 Contender。 |
| 策略状态 | 本 AI 的 `ALDOpponentController` | 同文件内 `FLDOpponentPolicyState`：当前模式、公共 POI、当前合法目标句柄、请求游标、有限失败记录及下次决策期限。不是第二份权威身体或成长。 |
| 一次决策输入 | 各领域投影；AI 只读副本 | `FLDOpponentDecisionFrame`：本人 owner/progression/equipment/readiness、已发布 WorldView、公共 MatchView、只读公共目录。纯策略不接 UWorld/Actor/Ledger 指针。 |
| 营地成本估计 | AI 同文件内纯值策略计算，不是玩法写入 | `EstimateCampCost(Frame, CampId) -> FLDCampCostEstimate`；CampId 仅引用公共 POI。结果区分有数值的 Ok、预算/生存条件不允许的 NotFeasible、输入/公式失败的 EstimateUnavailable；数值原语复用 R07，不推进世界、RNG、CD 或真实血量。 |
| 意图 | AI 产生；对应领域验证/提交 | `FLDOpponentDecision` 是 Order/Action/Choice/Shop 的有界判别结果，不含可写权限或另一个参赛者 ID。 |
| 绑定 | GameMode 唯一注册、核心注册后通知 | `RegisterAIContender(AController&, uint8 RosterSlot, const FLDMatchContext&) -> FLDAIRegistrationResult` 仅 Starting 使用冻结 AI 名额；返回核心 `FLDAIContenderBinding{Match,ContenderId}`，再调用 adapter 的 `BindRegisteredSelf(const FLDAIContenderBinding&)`。不得让策略任意选择 ID。 |
| 本人快照 | 核心与原领域只读投影 | 核心 `ReadAISelf(binding) -> FLDAISelfReadResult` 与 `ReadPublicMatch() -> FLDPublicMatchView`；adapter 的 `RequestAISnapshot` 组合这两个和 Observer/领域投影，不在核心另建快照真相。缺服务/旧局/已离局是失败，不返回成功形状空快照。 |
| 决策与发送 | AI / 原领域 | `OnPublishedFrame(Frame)` → 纯 `Decide(Frame, PolicyState)` → `DispatchIntent(Decision)`；后者为绑定本人构造各域 Caller。 |
| 回执与生命周期反应 | 原域先提交，AI 后消费 | `AcceptReceipt`、`OnLifecycleCommitted` 只更新策略状态/游标。迟到事件不能恢复已结束的旧订单或旧生命。 |

这些都是**拟建接口和类型**，不是现有 API。核心当前局映射不被 AI 强转或复制成新的真相：
Generation 对应网络 Match，LedgerMatch 保留 FGuid；BodyRef 每次从当前本人投影取得。
策略中缓存的代次只是拒旧条件，不具有开局、复活或授予资格的权力。

### 3.2 调度与读取合同

唯一顺序为 `Session → Motion → Actions → Effects → DeathsAndRewards →
RespawnsAndWorld → Transactions → Capture → Close → Publish`，由生命周期域持有。
AI 在 Publish 后读取已经完成的获准快照，只产生**下一实际边界**的意图；
不能在当前批次后半段插入回溯攻击，也不能调用 Sample/AdvanceAuthority 或改 World.TimeSeconds。
`GetAuthorityNowMs()` 是唯一可用性时基，使用核心 safe-floor 的 NowMs，不自行 ceil 提前到期；
事件真序仍由核心 `FLDAuthorityEventStamp.OccurredAtSeconds` 处理，
`GetAuthorityNowSeconds()` 处于同一原始时间域。策略期限是调度提示，
不是复活、刷新或冷却的第二计时器。
AI 不持有 `FLDGameplayWritePermit`，更不能直接调用 CommitPendingOrders 等提交入口。
只聚合同一已完成发布边界的本人/公共/观察投影，不用不同边界的余额、来源和目标拼造准入；
执行时仍由各域重新验证当前状态。
核心提交后提供 `LifeNotice{Match,BodyRef,Change,LifeRevision,Stamp}`、
`MatchClosed{Context,ResultReceipt}`、`MatchReset{OldContext,NewContext,LobbyRevision}`；
它们是事实通知，不给回调重新打开写闸的权限。

| 获准输入 | 正式来源 | 限制与撤销 |
| --- | --- | --- |
| 自身 | 核心 ReadAISelf、adapter RequestAISnapshot；ReadProgression、ReadEquipment、QueryAvailability | 可以读本人血蓝/资源/等级/技能/装备、控制、真实 readiness、候选和回执；不能取得可写 StateFor 或全账本。 |
| 公共对局 | `FLDPublicMatchView` | Phase、OpenAtMs、CircleState、Occupier、CaptureEpoch、SoloSinceMs、DueAtMs、Result；占领者身份和进度不附送其隐藏精确坐标。 |
| 当前可见单位 | `EnumerateVisibleUnits(ObserverContext, Purpose) -> ObservedFrame` | 带 Match、ObserverLife、ViewGeneration/Revision、MapRevision、ServerTime、Handle/ObservationEpoch。AI 与真人使用同构投影；只含已批准展示字段。 |
| 可见营地回位提示 | 同一 `ObservedFrame/FLDObservedUnitView.Statuses` 中只读 `StatusDefinitionId=camp_returning` | Vision 仅为当前获准可见且 Returning 的 CampMember 派生“回位中”；不含堆叠、期限、Home、路径目标或仇恨，不是可写 Combat 状态。Root 暂停不清提示；退出 Returning 或失视野即撤销，隐匿时不更新。 |
| 可见装备与资源 | 上述观察帧的展示定义 | 装备只有两个展示 DefinitionId，不含内部 ItemInstance、IssuedCooldownId、ReadyAt、SourceBindingRevision、技能列表或完整属性。HP/MP 按世界 Resources 的 P2 Current/Max 读取；NPC 不适用的 MP/Level/Equipment 是 NotApplicable，不补零猜测。失视野整条动态记录撤销。 |
| 静态世界与内容 | 世界公开地图/POI、公共目录 | Camp/Shop/Capture 固定位置、营地档位、地形和公开定义可知；不能把定义里的原始营地人数/刷新周期当作当前存活人数或剩余刷新时间。 |
| 路径 | `QueryGround(WorldRevision, FootCm, Capsule, SurfaceHint)`、`QueryCompletePath(WorldRevision, FromGround, ToGround, Capsule)`、`FindApproach(ObserverContext, Origin, UnitHandle或PublicPOI, RangeCm, Capsule)` | QueryGround 四参，无默认/三参重载；SurfaceHint 为必填 FName 注册 SurfaceId，仍须重查真实支持面。使用自身真实位置和当前地图；完整路径，脚点 XY 中心距，厘米；预览不含隐藏动态身体定位。 |
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
身体无合法 floor 返回 NotGrounded，即使投影 XY 在圈内也无占领资格。
NotGrounded 是明确否定结果，原域仍完成已处理边界，不是缺数据/查询未完成的系统故障。

### 3.3 命令、回执与拒旧

| 域与拟建正式接口 | AI 发送的最少条款 | 提交与回执规则 |
| --- | --- | --- |
| `ULDNavigationComponent::SubmitOrder(FLDOrderCaller, FLDSpatialOrderRequest)`；`CancelOrder`；`QueryOrder` | 绑定本人的 MatchContext/BodyRef；`FLDMoveIdentity{Match,Life,Order,Epoch}`；按 kind 为单一 Ground/ObservedUnit/公共 Shop 目标。Ground 带脚点 cm、必要 MapRevision 和必填 SurfaceHint；POI 从注册记录给支持面，目标可显式 Missing，不补默认值。 | Submit 只收件。Actions 的 CommitPendingOrders 才可激活；Queued 不取消旧单，完整准入后 CommitActionHandoff。OrderRevision/ReceiptSerial 单调，Query 只读且不延寿。 |
| `ALDCombatDirector::SubmitAction`、`QueryActionReceipt`、`QueryAvailability`、`CancelAction`、`OnActionFinished` | `FLDCombatRequestKey` 为 `Direct{Generation,Contender,Life,DirectSequence:uint64}` 或 `Nav{Generation,Contender,Life,MoveIdentity,AttackCycle}`；前者由绑定本人的 AI 直接意图生产，后者仅服务器 Navigation 内部生产。另带 Basic/Learned/Gear SourceRef、Gear 精确实例、TypedTarget 和必要 Build/Inventory/Item/SourceBindingRevision、CatalogDigest。 | 两种 RequestKey 不撞号；准入成功后由 Combat 统一分配 authority CombatToken:uint64，ActionId/AttackId 是该 Token 在 Match/Caller 下的身份，不另发序列。Queued 无已接受 Token；查询/取消用原 RequestKey。导航独占 AttackCycle，Combat 独占 NextAttackAt，AI 不另开普攻循环或伪造 Nav 键；Gear 不检查商店条件。 |
| `ALDDevelopmentDirector::ReadProgression`、`QueueChoice`、`QueryChoiceReceipt` | Generation、当前 Life、ExpectedLifePhase、独立 RequestId、队头 OfferId/Revision、OptionId、ExpectedBuildRevision；满槽明确 ReplacementSlot、ExpectedOldSkillId/Rank。 | 原候选不重抽；原子遗忘/装槽/消费一次机会后才生成下一组。死亡阶段合法确认可成功；死前 Alive 请求不能冒充当前 Dead 确认。 |
| `ALDInventoryDirector::ReadEquipment` | 绑定本人的读取上下文。 | 返回 Gold/AccountRevision、两个 Instance/Definition/ItemRevision、InventoryRevision/SourceBindingRevision、IssuedCooldownId/ReadyAtMs；纯投影，不授予修改权。 |
| 开店意图进入现有核心队列；`ALDVendorDirector::QueueShopCommand`、`QueryShopReceipt` | 开店 `FLDShopOpenCommand{Generation,Life,ShopId,ExpectedWorldRevision}` 不携 Permit。Buy/Sell 另带 RequestId、ShopSession/QuoteRevision/CatalogDigest、ExpectedWorldRevision 和预期 Account/Inventory/Build/SourceBindingRevision；Buy 商品/展示价，Sell 精确实例/ItemRevision/展示卖价。 | Transactions 才调用内部 `OpenShop(ContenderId, Command, Permit)`；每次开店/Buy/Sell 都由核心借当次 Permit，经 `QueryShopEligibility(Permit, BuyerBody, ShopId, ExpectedWorldRevision)` 重查当前身体、合法区域/距离与版本，不额外加 LOS。AI 不直接调用内部 OpenShop 或受许可几何；Sell 真成功后重读再单独 Buy。 |
| `LDTactical::EvaluateDamage(const FLDDamageEvaluationInput&, const FLDCombatFormulaSet&) -> FLDDamageEvaluationResult`；`EvaluateBasicTiming(const FLDBasicTimingInput&, const FLDCombatFormulaSet&) -> FLDBasicTimingResult` | 仅数值假设：公开编译定义、本人所需属性、显式非暴击假设、匿名模板抗性/HP/盾/保护；Timing 用对应单位普攻定义及已解析 AS。FormulaSet 是该 CatalogDigest 下批准编译的不可变参数。 | 位于原 LDTacticalRules，成功给数值/时长，定义域失败显式返回错误；无 Actor/World/Bodies/Camp 查询、时钟、RNG、状态或 Token。真实 ApplyHit/AttackStart 也必须调用这两个同源纯函数，再经 Permit 提交；估计结果不是 Hit/Death/奖励事实。 |

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
来自已受理请求，没有 Outcome/CommitOrdinal 或成功载荷；
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
| InvalidSurface/WrongSurface/NotGrounded | 重读合法支持面/本人真实接地结果，等相关条件变化后再规划；不伪造 SurfaceId 或把身体向下投影成接地，不以此错误自报系统故障或占领成功。 |
| Cooldown/AttackNotReady/InsufficientMana/ControlBlocked | 使用本人 readiness 和控制投影等待或换合法动作；策略只保存再评估期限，不写第二份 CD。 |
| InsufficientGold/Full/OutsideRange | 等真实资源/库存变化，或合法回店；不盲卖、不远程成交、不忙循环。售后买失败时承认当前空槽和实际金币。 |
| Dead/StaleLife | 移动/战斗/购物作废；当前 Dead 选择使用新的合法生命阶段确认，候选不变；等核心真实复活。 |
| MatchClosed/LeftMatch/StaleMatch | 停止全部新增意图，最多读取已提交结果；不重建 adapter、补席或重试旧局。 |
| 缺内容/缺服务/非法参数 | 明确阻塞并输出受限诊断；不加载旧 profile、不注入默认战力、不伪造空视图成功。 |

## 4. SC-09：全生命周期矩阵

| 状态/工作 | 取消/换目标 | 死亡等待与复活 | 离局 | 终局 | 新局 |
| --- | --- | --- | --- | --- | --- |
| 策略模式、目标与路径 | 取消适用订单；不把迟到回执接回当前状态 | 清身体目标/路径；Dead 保留选卡能力；复活从新 BodyRef 重新决策 | 注销本人绑定；其他人离开只撤目标，不增 AI | Close 先锁闸，再取消策略；无新 intent | 全清并绑定新 Match；不复用旧请求或随机上下文 |
| 已发命令/查询 | 已提交只读重放，不退款；待执行依域取消 | 旧身体动作/商店失效；Choice 重新核对当前 LifePhase | 不接管真人的请求/资源；本人回调不得唤醒已注销策略 | 查询不延寿、不重执行 | 旧 Match 拒绝，不能因缓存清空变新操作 |
| XP/金币/技能/装备/候选 | 不变；选择/成交只由原域提交 | 原域保留；可有在局死者的合法延迟奖励和选卡；复活不重发初始资源 | 冻结/退休由原域处理，不当击杀 | 冻结，不继续提交选择/买卖/奖励 | 原域按新局输入初始化 |
| 来源冷却 | 不因取消、换槽或遗忘/出售消失 | 按原时间继续；再次拥有沿用原期限 | 无新释放资格 | 不再释放；原回执不增加效果 | 原域新时间域，无跨局继承 |
| 已释放独立效果 | AI 不回滚，按 Combat/内容生命周期 | 正常死亡继续原期限；不得打中同 ID 的新生命 | 核心/Combat 取消本人未结束效果，身体立即退出 | 关闭写闸后不得继续结算 | 全部换代 |
| 观察/公共 POI 访问记忆 | 失视野立即丢动态目标；无 LastSeen | 死亡无身体视野；静态知识可读；复活新 observer 代次，不沿用旧句柄 | 通道/句柄失效；不跟踪移除身体 | 不保持实时隐藏更新 | 清访问/失败缓存及观察游标 |
| 血蓝/复活/保护/占点 | 进攻意图按已确认规则解保护；受伤不由 AI 清占点计时 | 核心复活满血蓝/新 Life；AI 不改 RespawnAt 或自行声明有效占点 | 立即失去战斗/占点资格，不为剩余人判胜 | 不复活、不再占点 | 核心重建发育期与圈状态 |

离局前已提交的死亡事实，其 `RewardBasis.CreditOwnerEligibleAtDeath` 由核心/成长域冻结；
允许原域完成这一事实的 settlement-only 处理，不等于离局者恢复行动资格。
AI 的已注销绑定不能因迟到奖励/回执重新规划、选择、生成身体或恢复效果。

## 5. 接线、复用与真实删除

以下均为**后续实施清单**。本报告不执行删除，不授权用文本无命中代替反射/资产证明。
表中 `S\` 精确表示 `Source\Lockdown\`；路径列不省略实际文件名。

### 5.1 必需复用与新增

| 类别 / 架构锚点 | 文件与符号 | 实施边界和实际消费者 |
| --- | --- | --- |
| 局部原样复用 R01/R02 | `S\Public\Navigation\LDNavigationComponent.h` 的 FLDMoveIdentity；`S\Private\Character\LDCharacterMovementComponent.cpp` 的 SavedMove/Serialize；`S\Public\Framework\LDCommandLiveness.h` | 保留代次/取消/拒旧与原生运动，不复制身份或位置系统；输入、导航、Combat、AI 共用。 |
| 改造 R05/N06 | `S\Public\Navigation\LDNavigationComponent.h`、`S\Private\Navigation\LDNavigationComponent.cpp` | 原组件承载 SubmitOrder/CancelOrder/QueryOrder 与 server adapter 的方向跟随；拆 local UI 限制，移除 B91/旧 Site 分派；Combat 的旧 `PC->Navigation->Stop()` 改接同一 handoff/cancel。 |
| 改造 R04/R06 | `S\Public\Framework\LDGameMode.h`、`S\Private\Framework\LDGameMode.cpp`、`S\Public\Framework\LDPlayerController.h`、`S\Private\Framework\LDPlayerController.cpp`、`S\Private\Character\LDCharacter.cpp`、`S\Private\Character\LDCharacterMovementComponent.cpp` | 核心 RegisterAIContender/ELDContenderKind/ReadAISelf/ReadPublicMatch 与提交后通知；基类的 UI、网络与本地运动隔离；删除居民、人口速度和搬运政策。不能只新增一个 Controller 然后保留 local-only 假设。 |
| 抽取/改造 R07–R09 | `S\Private\Combat\LDCombatDirector.cpp`、`LDContentActions.cpp`、`LDSkillExecution.cpp`；`S\Public\Combat\LDTacticalRules.h`、`S\Private\Combat\LDTacticalRules.cpp`；`S\Private\Development\LDDevelopmentDirector.cpp`、`LDV08Loadout.cpp`；`S\Private\Inventory\LDInventoryDirector.cpp`、`LDVendorDirector.cpp`；`S\Private\Economy\LDEconomyLedger.cpp` 及各自 Public 头文件 | 所有者分别是 Combat、成长/经济；同准入、来源和事务，不迁入 AI 文件。Damage/Timing 纯值原语抽到同一 LDTacticalRules，实际执行与 AI 估计共用，不留旧伤害/间隔公式并行。保留条款比较、原子提交、来源失效；去旧职业/书籍/槽位政策。 |
| 抽取/改造 R10–R12 | `S\Private\World\LDWorldDirector.cpp`、`S\Private\Core\LDGroundQueries.cpp`、`S\Private\Vision\LDVisionDirector.cpp`、`LDWorldViewRelay.cpp`、`S\Public\Vision\LDViewTypes.h` | 世界提供五营地、正常致死信用、静态查询和唯一受限帧；AI 不消费全表，不保留旧完整地图第二身体流。 |
| 新增 N06/M12 | `S\Public\Population\LDOpponentController.h`、`S\Private\Population\LDOpponentController.cpp` | 放 §3 的 adapter、value policy、决策输入/结果与回执消费；不另建 Brain/Perception/Economy/Clock Manager。 |
| 新增目标验证 | `S\Private\Tests\LDOpponentPolicyTests.cpp`；`S\Public\Tests\LDOpponentAIProbe.h`；`S\Private\Tests\LDOpponentAIProbe.cpp`；`Scripts\RunOpponentAICheck.ps1`、`Scripts\CheckOpponentAIEvidence.py` | 名称为拟建；纯策略用授权 DTO，真实场景使用正常订单及真实拥有连接。checker 只复核本次运行，不回放旧档案作为成功。 |

AI 新参数接入内容域选定的唯一生成输入/载荷/contract，不新增 AI 私有 JSON 或运行时读 docs。
不新增行为树、黑板资产、AI 专用地图、不同身体、独立 NavMesh 或旧模式开关；
沿唯一 Demo Character、地图、普通导航和内容源。共享头文件与地图变更由原所有者实施。

### 5.2 D06：完整人口/席位/服务生产文件

以下二十一个生产文件是同一旧闭包，不因 AI 只用其中一个类就保留其余调度。

| 实际删除文件 | 实际符号/消费者与先决替代 | 协调所有者 |
| --- | --- | --- |
| `S\Public\Population\LDB92Roster.h`；`S\Private\Population\LDB92Roster.cpp` | Seat/Person、B92Controller/SeatController/GameMode/HUD/Roster；先替代 GameMode 的 Spawn/Admit/Release/Logout/ReturnSpawn、账户与 AI 注册，删除接管而非空实现。 | 生命周期、AI |
| `S\Public\Population\LDB92Routines.h`；`S\Private\Population\LDB92Routines.cpp`；`S\Private\Population\LDB92PopulationContract.h` | RoutineController、StrategicRoute、Sense/Danger、日程；先断 Roster/RoleDied、环境 SealedCargo 表现和 probe。 | AI、世界/视野 |
| `S\Public\Economy\LDB92Economy.h`；`S\Private\Economy\LDB92Economy.cpp`；`S\Private\Economy\LDB92EconomyContract.h`；`S\Private\Economy\LDB92WorldLedger.cpp` | 旧席位/居民钱包、银行/机构库存、ImportWorld/WorldProjection 和独立 Tick；先接精简 Ledger/正常奖励，断 FCatalog.LoadFull 与 B91 物理来源同步。 | 成长经济、世界 |
| `S\Public\Economy\LDB92Services.h`；`S\Private\Economy\LDB92Services.cpp`；`S\Private\Economy\LDB92ServiceClient.cpp` | MarketView/Order、旧 RPC、用户进店导航、Markers、B92HUD；由 Vendor/同一导航/合法自身 UI 替代。 | 成长经济、操作、UI |
| `S\Private\Economy\LDB92Supply.cpp` | Endpoints/SupplyOffers/PurchaseOfficial，由 Services 调用；不是 Demo 商店目录。 | 成长经济、内容 |
| `S\Private\Economy\LDB92Work.cpp` | WorkTerms/CommitWork/BeginWork/SampleWork/StopWork，把制作/采购与 Action=8、DevelopmentKind 接在一起；无 Demo 消费。 | 成长经济、Combat |
| `S\Private\Economy\LDB92Courier.cpp` | CourierBoards/Distance/View/Terms/Command，由 Services/Work 调用；任务路线、送件和有限奖励全部退出。 | 成长经济、世界 |
| `S\Private\Economy\LDB92Charter.cpp` | PurchaseCharter 由 Services.Order 调用，旧终局资格购买；不是占领前置。 | 成长经济、生命周期 |
| `S\Public\Economy\LDB92Deposits.h`；`S\Private\Economy\LDB92Deposits.cpp`；`S\Private\Economy\LDB92Extraction.cpp` | 自动采集 Tick、Survey、Combat 注册任务；不是合法打野收益。 | 世界、成长经济、Combat |
| `S\Public\Economy\LDB92Witness.h`；`S\Private\Economy\LDB92Witness.cpp` | Profession/Execute/RecordWitness、ALDCharterTask；先断 Combat 职业例外与展示字段。 | Combat、成长经济 |

### 5.3 混合调用者与 B91/居民闭包

| 架构编号 | 精确受影响文件/符号 | 替代前提与删除边界 |
| --- | --- | --- |
| D01/D06 | `S\Public\Framework\LDGameMode.h` 的 Population/friends；`S\Private\Framework\LDGameMode.cpp` 的 Humans/profile/class、Population Spawn/Admit/Release、ReturnSpawn/RecordGold 分派 | 由核心同一名单/生命/账本适配替代；不保留恒真 IsPopulationProfile。 |
| D06/D14 | `S\Public\Combat\LDCombatDirector.h` 的 B92 friends；`S\Private\Combat\LDCombatDirector.cpp` 的 RoleDied；`LDContentActions.cpp` 的 Witness::Profession；`LDSkillExecution.cpp` 的旧任务查找 | 接普通动作/死亡事实后删 callback、include 与特免；不删真正的统一战斗能力。 |
| D06/D15 | `S\Private\Development\LDDevelopmentDirector.cpp` 的人口 Preparation；`S\Private\Inventory\LDInventoryDirector.cpp` 的 B92 pre-consume；`S\Public\Economy\LDEconomyCatalog.h`、`S\Private\Economy\LDEconomyCatalog.cpp` 的 LoadFull/Full schema | 先统一成长、来源和两装备服务，再断旧 Catalog，不能删 contract 后留下 LoadFull。 |
| D06/D11/D17 | `S\Public\World\LDEnvironmentDirector.h`、`S\Private\World\LDEnvironmentDirector.cpp` 的 Routines/SealedCargo 与旧 wire；`S\Public\World\LDWorldObjects.h`、`S\Private\World\LDWorldObjects.cpp` 的 B92 friends/RecordConsumption/机构 Holder 特例 | 世界统一授权/稀疏 FLDWorldView 和事务后退出；字段生产、序列化、表现一起删。 |
| D07 | `S\Public\Character\LDCharacter.h`、`S\Private\Character\LDCharacter.cpp` 中 ALDResident；`S\Public\Region\LDRegionDirector.h`、`S\Private\Region\LDRegionDirector.cpp`；`S\Public\Development\LDWorldChecks.h`、`S\Private\Development\LDWorldChecks.cpp` | 删除居民、训练/随机游走、钱袋/偷窃/看守/旧 Sentinel 奖励；同时断 GameMode 八居民生成、Combat Residents 返回和 RegisterSkillTask 旧消费者。 |
| D07/D23 | `S\Public\Region\LDRegionDefinition.h`、`S\Private\Region\LDRegionDefinition.cpp` 的居民数组/验证；`Config\RegionLayout.json`、`Config\V07RegionLayout.json`、`Config\V08RegionLayout.json` 的居民/训练居民旧布局 | 由唯一 Demo 地图/角色生成替代；不能只停 Spawn 还要求旧八居民。 |
| D11 | `S\Public\World\LDEnvironmentDirector.h` 中 ALDB91Controller/Mode/HUD；`S\Private\World\LDB91Controller.cpp`、`LDB91ObjectsClient.cpp`、`LDB91SourceControls.cpp`、`LDB91SourceHUD.cpp`、`LDB91SurfaceClient.cpp`、`LDB91NetClient.cpp` | B92 父类也有其他消费者；操作/世界/视野改接后，按 D11/C3 关闭整链，不由 AI 单独盲删。 |
| R05/D11 | `S\Private\Navigation\LDNavigationComponent.cpp` 的 B91 target/object；`S\Private\Character\LDCharacterMovementComponent.cpp` 的 population/cargo 分支 | 同一地图查询与自身属性投影替代；不留第二套路径与速度政策。 |

### 5.4 数据、资产、配置和工具

| 实际路径 | 消费与退役条件 |
| --- | --- |
| `SourceData\World\population.json`、`npc_schedules.json`；`Content\Lockdown\Data\V09\population_b92.json`、`schedules_b92.json`；`Scripts\SyncB92Population.py` | Roster/Routines 与生成 contract 一起退役；不把 population 的描述当自主策略。 |
| `SourceData\World\economy.json`；`Content\Lockdown\Data\V09\economy_b92.json`；`Scripts\SyncB92Economy.py` | 断 FCatalog.LoadFull/B92 服务、WorldLedger 后删；纯 Ledger 留在原经济域。 |
| `SourceData\World\security.json` | 实际由 `Scripts\SyncV09Data.py` 读入 `map_b90.json` 的 security/civic/source hash；先修改世界 compiler/definition/validator，不直接删输入断生成。不存在独立 security_b92 载荷。 |
| `SourceData\World\rules.json`、`pois.json`、`routes.json`、`settlements.json`、`spawn_pads.json`、`buildings.json`、`terrain.json`、`services.json`、`legacy_aliases.json`、`travel_table.csv` | B92 的旧完整世界依赖，归 D22 世界清单；单图定义接管后删除旧城镇/路线/二十四出生位数组或整文件，不留并行旧数组。 |
| `Scripts\SyncV09Data.py`；`Content\Lockdown\Data\V09\map_b90.json`；`S\Private\Region\LDB90Contract.h` | 地图域关闭旧 producer/loader 后按 D32 删除或收敛唯一当前生成链。AI 不新建地图 compiler。 |
| `Content\Lockdown\Generated\V09\L_V09Full.umap`、`DA_FullMap.uasset`、`M_RiverInstanced.uasset`、`M_SoilInstanced.uasset` | D19/C2/C3：只有证实被唯一当前地图采用的必要几何/材质可抽取，旧图根退役。Builder `LDB90BootstrapCommandlet.cpp` 存 ALDB90PreviewMode，B92 是 runner 的 URL override；不存在“已经验证的 B92 专用地图”。 |
| `Source\Lockdown\Lockdown.Build.cs`；`Config\DefaultGame.ini`；`Scripts\PackageWin64.ps1` | staging map/environment/objects/tools/traversal、广泛 AlwaysCook Generated、旧多图打包随 D19/D25 收敛；当前无显式 staging 三个 B92 JSON，不能声称现有包含正常 AI 完整链。 |

B92 ImportWorld 还依赖 B91 物理来源，关闭检查必须覆盖
`Scripts\SyncB91Data.py`、`SyncB91Objects.py`、`SyncB91Tools.py`、
`SyncB91Traversal.py`、`SyncB91Forestry.py`、`SyncB91Grass.py`、`SyncB91Forest.py`
及其 D19/D22/D32 载荷/contract。这些属于世界域的精确闭包，AI 不另列第二份地形裁剪方案。
不要只删 B92 消费者而让旧 producer 永久重建已弃人口世界。

二进制资产删除前必须检查 AssetRegistry/Reference Viewer、Blueprint/UPROPERTY、软引用、
动态加载、地图 WorldSettings 和 Cook。源码无命中、旧 probe 不运行都不构成资产可删除证明。
当前系统手册中的有效导航、事务、来源和视野知识保留校准，不删除知识来掩盖缺口。

### 5.5 旧检查退役与有效不变量迁移

| 精确删除集合 | 替代条件 |
| --- | --- |
| `S\Public\Tests\LDB92EconomyProbe.h`、`LDB92RosterProbe.h`、`LDB92RoutinesProbe.h`、`LDB92ServicesProbe.h` | 新正常 AI 场景取代旧席位/日程验收；GameMode 的 include/FParse/Spawn/dispatch 同步退出。 |
| `S\Private\Tests\LDB92EconomyProbe.cpp`、`LDB92EconomyRulesTests.cpp`、`LDB92RosterProbe.cpp`、`LDB92RoutinesProbe.cpp`、`LDB92ServicesProbe.cpp`、`LDB92ServicesRulesTests.cpp`、`LDB92ServicesClient.cpp`、`LDB92CourierProbe.cpp`、`LDB92CharterProbe.cpp` | 迁移身份唯一、条款比较、序列化白名单、实例/资金原子性；删除 220 身体/24 席位、接管、死亡税、掉工具、旧职业/槽数断言。 |
| `Scripts\RunB92Roster.ps1`、`RunB92Routines.ps1`、`RunB92Economy.ps1`、`RunB92EconomyRegressions.ps1`、`RunB92Services.ps1`、`RunB92Courier.ps1`、`RunB92Charter.ps1` | Roster 的 SpawnActor“human admission”、不参赛 authority 与重连接管场景不能代替真实三真人加三 AI。 |
| `Scripts\CheckB92Roster.py`、`CheckB92Routines.py`、`CheckB92Economy.py`、`CheckB92Services.py`、`CheckB92Courier.py`、`CheckB92Charter.py` | 各 checker 的 full_stage_acceptance=false 本就不认证目标 Demo；不用换标题或旧报告满足新门禁。 |
| `S\Private\World\LDB91CombatProbeClient.cpp`、`LDB91EncounterProbeClient.cpp`、`LDB91FieldsProbeClient.cpp`、`LDB91GrassProbeClient.cpp`、`LDB91IgnitionProbeClient.cpp`、`LDB91ObjectsProbeClient.cpp`、`LDB91SaplingsProbeClient.cpp`、`LDB91ToolsProbeClient.cpp`、`LDB91TraversalProbeClient.cpp` | 属于旧父类上的 D27 检查，随各世界闭包及真实替代一起退役；不能只删 Tests 中的声明。 |

`LDNavigationProbe.cpp` 的阻挡/无进展/旧 ACK/Stop/死亡换代、
`LDInputRulesTests.cpp` 的 Fresh、`LDRulesTests.cpp` 的 ViewOrdering、
`LDViewTests.cpp` 的字段白名单、`LDEconomyRulesTests.cpp` 的幂等/原子拒绝、
`LDV08LoadoutTests.cpp` 的来源失效不洗冷却均迁入保留的对应领域测试。
`Scripts\RunTests.ps1` 的固定十九项和 TradePrivacy 门槛改为明确的当前用例集合，
不靠降低数量、吞失败或删除有效断言换通过。当前没有 `TestB92Import.py`，不列虚构入口。

删除顺序：P1 接通核心绑定/领域验证/受限投影并迁移必要断言；
断旧 Spawn/Admit/Release、居民/任务/经济/视野与 probe 回调；
P2 更新生成消费和唯一地图根、完成本批 C3/C4；
P3 真删旧文件、RPC、friend、字段、输入/载荷/contract 与 runner；
P4 构建/UHT、Bootstrap、生成拒绝及对应真实场景确认闭包。
这不是等待完整 AI 才允许 B01–B03，也不是未经引用核验先删所有资产。

## 6. 单一自主策略

### 6.1 状态与优先级

采用固定优先级选择器及少量在途子状态。每次从高到低选择**当前可执行**的意图；
等待一个回执不阻塞其他合法通道。没有“选择另一人格/旧居民模式”的回退路径。

| 优先级 / 状态 | 授权输入、进入条件与动作 | 退出、重规划与禁止捷径 |
| --- | --- | --- |
| P0 Stopped | Results/Closing/Faulted、非运行阶段、离局或绑定失效；停止新提交 | 只有核心发布合法新局才重新绑定；不是不断尝试旧局命令。 |
| P1 DeadWaiting | 本人 Dead/RespawnAt、当前队头 Offer；等待身体，并可用当前 Dead 阶段确认选择 | 新 BodyRef 到来才恢复身体动作；不移动、攻击、购物，不恢复旧路线/商店步骤。 |
| P2 Disengage | 本人危险阈值、可见接触压力、自保 readiness、静态可达地面；停止继续投入，合法自保/撤离 | “正在脱险”才保持最高空间优先级；脱险后的回血等待不永久压住回防。圈内有合法余地时先圈内移动，不机械跑出圈。 |
| P3 DefendOrHold | 自己正在合法守圈，或公开独占即将达到自身回防预算；走公开圈入口、处理当前可见威胁 | 压过新的打野/购物；阶段、CaptureEpoch、有效资格、路线或风险改变时重评；公开占领者不能直接变成单位目标。 |
| P4 Choose | 队头 Offer 存在且无在途构筑变更；用 §6.4 选择一次 | 不冻结正在进行的合法运动或紧急反应；成功后才读下一组，冲突不重抽。 |
| P5 Engage | 当前合法可见目标、本人可用资源/动作、未超投入预算；Nav 攻击订单与 §6.3 主动 | 失视野、无效目标、危险、回防、超预算或生命变化时退出；不读对手内部战力来保证击杀。 |
| P6 Shop | 自身余额与同目录公开价格/出售规则支持一个非约束性改善计划，不要求已有 ShopView；ApproachShop 到位后将开店意图入核心队列，Opened 后按真实报价重新判断 Buy 或 Sell→Buy | Pending 不等于有会话；危险/越界/死亡/终局打断未提交步骤，已成交不回滚。无改善就离店，不为更换排序 ID 循环买卖。 |
| P7 Contest | 圈已开放，无更高可执行目标；自身风险门允许进入公开圈 | 到位转守圈；不能有效入场时换静态合法入口或继续成长/探索，不用保护者骗争夺人数。 |
| P8 Farm | 没有更高目标；根据公开档位与自身状态选择营地，观察到合法成员才攻击 | 无合法目标、止损、预算到期或紧急回防即结束。Returning 不等于无敌；只继续 §6.2 允许的当前射程内攻击，不新起回营追逐。 |
| P9 Explore | 其他目标不可执行；选择未被失败抑制的公共 POI | 新合法观察、资源/Offer/公共圈变化、订单终态触发重评；不为移动而扫描全图身体。 |

一个 AI 同时至多持有一个当前空间订单、一个对应身体动作控制上下文、
一个成长/装备构筑变更在途请求；导航与 Combat 的关联仍由原域管理。
普通节拍最多提出一个新的身体意图和一个不冲突的构筑事务意图，不能排一串未来技能。
新 Offer 不阻塞脱险，已提交 Buy/Choice 也不能被高优先级目标反向回滚。
只有相关条件变化或真实期限到达才重评；保持仍合法的已有订单，不每节拍重发同一目标。

核心终局、离局、死亡和观察撤销按通知立即清理，不等待常规规划周期。
长帧后最多对最新已发布帧作一次规划，不“补跑”漏掉的历史 AI 决策。
三 AI 使用相同有界预算，轮换服务顺序以避免固定第一个长期抢占预算；
它们不共享黑板/战术目标/视野/资源，也不因对手是 Host、真人或 AI 而改变仇恨。
纯平局以本局本人绑定与公共稳定键形成稳定顺序，不查询其他 AI 的计划或隐藏状态。

### 6.2 营地、目标与公开回防

**营地。** 原始构筑优先低档；有真实成长选择或装备后才把高档加入主动尝试候选。
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

**目标。** 当前授权候选中，优先正在阻止自己守圈/回防的可见威胁，
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
AI 可以根据新的公开圈/静态 POI 决策发**新明确订单**，但不能把旧目标坐标改装成地面追踪。

**回防。** 用公开 DueAtMs 和服务器当前时间得到当前独占剩余量，
与本人到静态合法入圈点的完整路径/当前移速所需时间、当前控制/保护剩余限制、
AI.CaptureReactionBudgetSec 的余量比较。被零移速或控制阻止时不能除零或假定立刻到位；
预算是保守估计，不是另一个占领截止或额外等待时长。
公共独占逼近这个预算就停止新增刷野/购物投入，不能为“最后一只怪/再买一件”故意拖延。
已 Sell 成功而 Buy 未提交时，可以带真实空槽回防。

风险门只使用本人 HP/受压、当前可见接触压力、可达入口/退路与本人可用能力。
在止损线以下又无可用自保/有效入场路线时先脱险或换公开入口，不要求必死冲锋；
无法阻止时允许失败，不靠额外血量、传送或隐藏目标定位补救。
保护者可以合法移动接近，但不能自报有效争夺；进攻解除或真实到期由核心更新资格。
守圈时不为追杀离圈目标轻易放弃独占；必要撤离则接受真实清零。
受伤、施法、控制或圈内移动本身不由策略重置进度；达到显示期限也不自行判胜。

**恢复。** RetreatHPRatio 判断受压脱险，ResumeFarmHPRatio 是脱险后重新主动刷野的门槛。
恢复等待不禁选卡、重新评估公共回防或低风险移动；不要求满蓝才能普攻。
没有有效回复来源时，不永久等“回满”，转可达探索、真实可负担购物或合法争圈机会。
地图没有免费疗养区；自保/治疗能力必须真的持有、可用且走同一 Combat 提交。

### 6.3 普攻与每一个主动来源

持续普攻只由 Nav AttackUnit/AttackMove 的 AttackCycle 驱动。
接近距离使用本人当前 `STAT.AttackRangeCm`，不是初始 UNIT.BasicAttackRangeCm。
群攻由 Combat 结算，不让 AI 给每个隐藏副目标补发一次攻击订单。

对真正绑定的 Q/W、1/2 枚举自身 QueryAvailability，固定考虑：
有意义的合法自保/恢复 → 必要合法机动 → 可见威胁控制 → 合法伤害 → 保持普攻/移动。
优先级是策略提案，不给效果追加类型或数值；SourceRef 和真实目录决定动作。
同一时刻只送当前动作系统可接收的一项；前摇、控制、资源、来源和占用均不得绕过。
成功 handoff 后旧运动不由 AI 私下恢复，按新合法观察重新决策；失败则消费真实回执。

| 公开角色定义 | 目标与使用规则 |
| --- | --- |
| `active_arc_bolt` | Direction；用当前合法可见目标形成方向，归一化/几何由 Combat 重验；不随隐藏目标持续修正。 |
| `active_guard_pulse` | Self；真实效果为屏障并移除本人当前全部 Root 来源。需要防护，或 Root 妨碍脱险/入圈且 Skill/资源/readiness 允许时优先使用；Root 不禁止 Skill，但不能越过其他施法限制。不治疗、不免疫伤害，不清其他控制/CD/死亡或重新保护。 |
| `active_binding_snare` | Unit；当前可见威胁的观察句柄，执行时重查；不是凭公开占领者 ID 施法。 |

首版七个装备定义逐项绑定；当前只有两种非空 Active，不以一个“会用装备”的泛化断言代替：

| 公开装备定义 | 内容域给定的 typed 能力 | AI 使用与目标规则 |
| --- | --- | --- |
| `item_phase_anchor` | Active：Direction/Dash；RangeCm 来自 DashDistanceCm，另有 DashDurationSec/WindupSec/ManaCost/CooldownSec/CooldownFamily | 脱险或合法入圈机动时，从自身到已查询公共合法落脚方向构造有限非零方向；使用目录真实位移范围，落点/沿途几何由世界和同一 Combat/运动边界重验。没有合法方向就不用，不追隐藏 Actor、不直接改坐标。 |
| `item_arcane_battery` | Active：Self/RestoreMana+Shield；ManaRestoreRatio、ShieldBase/ShieldAPRatio/ShieldDurationSec 及动作/来源字段 | 本人 MP 有真实缺口，或当前脱险/守圈需要防护时使用；收益和叠加由统一效果规则决定。Dead 不施放，不推断该物品还能回血或清控制。 |
| `item_iron_plate`、`item_edge_blade`、`item_runic_focus`、`item_siphon_emblem` | Active 为空；具体 Stats/Passive 由内容域定义 | 正常购买/装备后消费统一派生与被动，无主动按钮或伪施法。 |
| `item_echo_maul` | Active 为空；`passive_cleave` 响应 PrimaryBasicContact | 通过正常普攻触发，AI 不向副目标追加攻击订单；不把群攻变成隐藏单位锁定。 |

角色和装备均按真实 TargetMode 进入同一执行通路；Unit 使用授权句柄，
Direction 使用当时获准方向，Self 检查本人状态。
尚未提交、依赖已撤销观察的瞄准计划作废，已经释放的独立效果不会持续修正隐藏目标。
首版没有 Ground 主动，不为“覆盖四个模式”新增技能；通用领域 Ground 合同保留，
但本模块不把未出现的内容宣称为已完成的目录覆盖。

同款两件的主动逐实例查询和提交，可以在正常动作占用允许时连续使用。
不能由策略把同名 CooldownFamily 合并成共享锁，也不因另一个实例释放而假定本实例不可用。
店外、圈内、没有报价、金币不足、装备槽已满均不妨碍已有 1/2 的合法战斗释放。
来源失效后等待正式新绑定/ReadyAt，不自己清冷却或把换槽当新来源。

### 6.4 持久选择与构筑

只枚举队头三张卡及其合法替换结果。选择规则固定为：
先补角色主动中缺少的交战/自保用途且尽量不丢现有唯一用途；
随后依次考虑已有技能升级、新主动填空、新被动、属性、其余需要替换的新主动。
同层先减少要遗忘的已投入等级，再用公共定义稳定顺序和槽位顺序消歧。
用途从已批准 typed 效果读取：伤害/控制用于交战，自身防护/恢复用于自保，
不靠名称、未学习技能的私有状态或临时战力公式推断。
这能处理两个交战主动已满时选择自保并明确替换，也能处理被动/属性卡，不强迫卡片类别各异。

选项内容、目标 rank、必要旧技能 ID/rank/槽位和 Offer/Build/LifePhase 组成一次确认条款。
不同请求不得竞争消费同一机会；冲突后重新读原 Offer 和本人构筑，重新决定是否确认，
不改卡、不跳队头、不看未来随机结果。成功后才处理下一组，最高等级仍可处理已积累机会。
死时使用同一选择器但以当前 Dead 阶段提交；属性改变由统一投影保持资源比例，不复活/治疗。
遗忘不建立备用技能库；重新得到从一级开始，readiness 沿来源域原期限，不以清 CD 作为选择收益。

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
自身装备每次变动后重新读取 abilities，确保任何新装主动不是“只买不用”。

### 6.5 有界重试与可观察决策

失败记录按“公共 POI 或当前业务目标 + 原因类别 + 相关本人条件指纹”保存，
目标撤销时不留下单位位置/血量历史。固定地点有限；只保留当前观察和在途请求，
不随无限时长对局积累全单位/全回执日志。
RetryPolicy 的 MaxAttempts 包含首次提交，退避逐次增加且不超过 MaxBackoffSec；
次数到界后明确进入目标抑制或等待原域终态，不偷偷无限生成新 token。

无路/无进展先消费导航域的有限重算和失败，AI 才换另一个合法公共接近点/POI，
不叠加第二套卡路计时。资源拒绝等 readiness/相关资源变化；控制拒绝等对应限制解除；
Offer/报价冲突等同域终态或相关修订；普通帧更新不刷新次数。
回执不明只查询原 ID，查询预算耗尽后保持显式 AwaitReceipt/诊断并允许无冲突脱险，
不能宣称成功或重新买一次；收到终态/拒旧水位并重新同步后才解除该等待。

每个决定输出受限原因码及所用输入版本，至少区分 NoLegalTarget、Recover、
PublicCaptureUrgent、NoAffordableImprovement、AwaitReceipt、TargetSuppressed、
ProviderUnavailable、Stopped。日志可解释“为何继续发育/为何回防/为何失败”，
但不得记录从未授权给该 AI 的隐藏坐标、钱包或内部来源，再反向作为决策输入。

## 7. SC-09：参数与内容唯一来源

以下键已与内容域收敛为首版提案的正式引用，**不在本报告提供第二份数值表**。
由 `CONTENT_AND_BALANCE.md` 维护具体提案值、批准状态、依据、单位、范围及生成映射。
运行时不读 Markdown；未批准/缺字段/非有限输入须由唯一 compiler/校验入口拒绝，
不能以旧 Routines 常量或技术默认值补齐。

| 参数键 | 类型/单位与消费 | 必须可检验的约束 |
| --- | --- | --- |
| `AI.RetreatHPRatio` | 无量纲比例；受压脱险线 | 在零至一的有效健康比例域内，低于 ResumeFarmHPRatio，避免往返振荡。 |
| `AI.ResumeFarmHPRatio` | 无量纲比例；恢复后重新主动刷野 | 不要求满蓝、不阻止死时选卡/公开回防；与低档止损和入场能力一致。 |
| `AI.HighCampEntryHPRatio` | 无量纲比例；高档入场的当前 HP 门槛 | 还须实际成长/装备及保守成本评估，不只看等级或当前血量，不保证“进了就能打赢”。 |
| `AI.HighCampReserveHPRatio`、`AI.LowCampReserveHPRatio` | 无量纲比例；相应营地预测清空后的剩余 HP 门槛 | 使用本人实际构筑与公共目录的限定成本模型，不读隐藏血量；不是当前 HP 止损线/额外免伤。实际止损沿 RetreatHPRatio，模型不能保证第三方干扰下安全。 |
| `AI.CaptureReactionBudgetSec` | 正有限秒数；回防估计余量 | 包含常规规划/排队的不利延迟，不是等待此时长才行动；不得改变 Capture 的真实截止。 |
| `AI.DecisionIntervalSec` | 正有限秒数；常规策略节拍 | 三 AI 最坏调度可纳入回防预算；终局/身份/观察失效立即处理，长帧不补算旧决定。 |
| `AI.RetryPolicy.MaxAttempts` | 正整数，含首次 | 有界业务/路径尝试；缓存过窗不是重新发起同一操作的理由。 |
| `AI.RetryPolicy.InitialBackoffSec`、`AI.RetryPolicy.MaxBackoffSec` | 正有限秒数 | 最大值不小于初值；重试只在原操作确定终结/拒旧及当前合法条件下发生。 |
| `AI.ExploreRevisitIntervalSec` | 正有限秒数 | 只描述本 AI 公共 POI 再探节奏；不随隐藏营地刷新事件调整。 |
| `AI.EngagementBudgetSec` | 正有限秒数 | 覆盖批准基础低档战斗的合理投入；同一次目标/营地过程累计，不随普通攻击、Order 或帧重置。 |
| `STAT.AttackRangeCm`、本人移速/血蓝/readiness | 原领域权威派生；厘米、厘米/秒、资源 P2、绝对服务器毫秒 | AI 不用初始射程覆盖成长，不另存技能 CD；字段单位由提供者定义并集中转换。 |
| `ABILITY.<id>.TargetMode/RangeCm/WindupSec/CooldownFamily/Rank[r].ManaCost/CooldownSec`；`ITEM.<id>.BuyPrice/Stats/Passive/Active` | 公共定义；按字段单位/类型读取；装备效果子字段见 §6.3 | 技能最高三级；按实际三种目标覆盖全部非空来源；未知 enum/缺作用条件拒绝而非默认为普攻，不虚构 Ground 内容。 |
| `CAMP.Low/High`、公开地点；`FORMULA.Damage/Resistance/AttackIntervalMs/AttackWindupMs/ResourceMaxChange` | 只读公共定义/统一数值原语 | 成本估计复用原语但不执行战斗，不读隐藏当前营地成员/HP/刷新期限；资源比例变化只消费 Combat 最终投影。 |

工程级回执容量、订单 liveness、无进展/求路预算沿操作/领域合同集中定义，
不再加 AI 私有攻击周期、卡路阈值、单位历史 TTL 或敌方战力权重。
首版策略只有固定选择顺序；内容未批准不影响可先做权限/身份/拒旧测试，
但不能用这些测试宣称真实 AI 闭环已完成。

## 8. 最小实施顺序与可观察验收

### 8.1 真正自主闭环的最小任务顺序

| 切片 | 前置与最小交付 | 退出证据，不以 proxy 代替 |
| --- | --- | --- |
| AI01 绑定/受限输入/拒旧 | I1/I2 与世界正式 DTO；新增 adapter 和纯策略边界，按核心 Publish 调度 | 零至三 AI 的注册/不补满、隔离输入、旧局/生命/观察/终局拒绝；此时尚不是可玩 AI。 |
| AI02 普攻起步 | I2/I3/I6；正常身体/完整路径/低档营地和最后一击复合奖励 | 无授予/传送，从开局近战普攻获得本人首次真实 XP/金币；无私有奖励入口。 |
| AI03 成长与所有主动 | I4 正式候选/来源、批准内容 | 原 Offer 逐队头消费、明确替换、死亡选卡；Q/W 与所有非空装备 Active 按合法目标使用。 |
| AI04 商店构筑 | I4 Vendor/Inventory、静态店/接近查询 | 正常购买、同款双持、Sell→新 Buy、购买拒绝恢复；来源 CD/被动/属性遵循统一规则。 |
| AI05 回防与生命循环 | I5 连续独占、I3 生命、完整观察与公开状态 | 发育/恢复/回防/守圈有实际决策；正常死亡/独立效果/复活继续；只有同一 Capture 可判胜。 |
| AI06 三 AI 与最终裁剪 | AI01–05、内容批准、相关 P1–P4 | 正常 Listen Host + 两真实拥有客户端 + 三 AI；完整对局/新局/反例与精确旧闭包退役。 |

按依赖逐切片删旧消费者，不把 B92 日程留到发布时作为隐藏备用。
AI03 的动作覆盖可以用明确标注的源/Offer 夹具穷举稀有分支，
但 AI02/AI06 的正常闭环不得借这些夹具授予资源或强制胜者。

### 8.2 正反验收与证据

以下都是**需新增或改造的未来验收**，当前未实现/未运行。
纯模型证明策略决定，原生场景证明接线，真实拥有连接证明授权和运行拓扑，三者不能互相冒充。

| ID / 场景 | 正例与反例动作 | 精确可观察结果 |
| --- | --- | --- |
| AI-A01 名单与独立性 | 零/一/二/三 AI 分别开局；满规模 Host+两 owning clients+三 AI；断开一真人 | 与 Host 选择数量一致，不补人/不接管；各 AI 有不同 Contender、独立观察/请求/钱包，仍各自 FFA。 |
| AI-A02 真实闭环 | 从无装备/仅普攻开局，逐个 AI 观察成长、选卡、到店买卖、主动、撤退恢复、回防、死亡复活与争圈 | 每段能由该 Contender 的正式订单/死亡奖励/Choice/Shop/Action/结果回执连起来；不是 Script 代发策略。分场景证明三个 AI 都能成为唯一胜者，不要求同一局三胜者。 |
| AI-A03 隐藏状态非干扰 | 固定同一授权输入/自身状态/静态图/平局种子，改变隐藏敌人坐标、实例、CD、技能/属性/金币和隐藏营地刷新；另只增加另一 AI 的合法观察 | 本 AI 的决策及请求条款不变；没有跨 AI 感知共享。再用真实遮挡/帧载荷证明生产者确实过滤，不只给模型手写空帧。 |
| AI-A04 字段白名单/死亡视野 | 当前可见时检查姓名/等级/HP/MP/状态/展示装备；遮挡、死亡、拖静态镜头、公开圈与击杀通知 | 动态字段撤销，无技能/CD/内部实例/属性/金币或 LastSeen；死亡无新身体视野；公共信息不附精确隐藏坐标。 |
| AI-A05 目标撤销/可见死亡 | 在提交前、追击、前摇中 Conceal/stale；晚到旧视图/ACK；另给予匹配原订单/handle epoch 的 FLDObservedUnitTermination 授权死亡 | 失视野终结 AttackUnit/追敌 A，无旧 A 目的地/LastSeen 续走；再次可见须新 Order。只有未取消原单的授权死亡能走操作域原 A 继续；隐藏死亡/移除/离局不行。 |
| AI-A06 支持面/不可达/卡路 | None/未知/错误 SurfaceHint、无真实 CurrentFloor 但 XY 在圈内；无完整路径、阻挡更新、受控停走、目标换生命、异观察者 handle | 四参 QueryGround 明确拒绝 InvalidSurface/WrongSurface/NotGrounded，无空 hint/默认面/向下投影兜底；无 floor 不具占领资格但不冻结已处理边界。无 partial/穿墙/传送/隐藏实体泄露；控制不误计卡路，有限重试后换合法 POI。 |
| AI-A07 订单/回执竞争 | 新意图 Queued 后拒绝/接受，Stop 后延后 ACK/Outcome；同 ID 同/异条款，过窗，Query 已取消单；Choice/Shop 查询覆盖所有 LookupStatus 与旧局同号 | Queued 不替旧单；handoff 原子；旧包不续走/续打/延寿；冲突/过窗明确，零重复提交。查询 Pending 只有 PendingInfo，Final 只有匹配业务 Receipt，缺失/过窗/错上下文无伪终态或默认生命载荷。 |
| AI-A08 持久 Offer 竞争 | 两次重复/不同选择请求、Build 冲突、连升、满 Q/W 替换、全部技能三级、死前 Alive 请求延到 Dead、Dead 选择与复活竞争 | 同一机会只消费一次，原卡保留不重抽；九次/十级/三级边界；明确遗忘/再学一级。当前 Dead 选择不治疗/提前复活；旧 LifePhase 确认拒绝而候选不变。 |
| AI-A09 买卖资格/失败 | 无 ShopView 但公开目录预算足够的首次/旧报价失效购物；开店 Pending/重复/取消/Opened/Rejected；LOS 被挡；旧路径、非法区域、无钱/满槽/旧报价/旧世界修订/越界/死亡/终局/分配失败；Sell 后 Buy 被拒及重放 | 无 ShopView 仍会据非约束预算主动到店；Pending 不提交 Buy/Sell。每人一个待办，只有核心 Transactions 借当次 Permit 进内部 OpenShop/资格查询；只有 Opened 带 View并触发重新预算，后续交易再验当前条件且无额外 LOS。失败不部分改钱物，已 Sell 不倒回，无免费实例或预算刷新。 |
| AI-A10 来源与全目录 | 穷举三角色主动，guard_pulse 在有 Root/无 Root及其他施法限制下释放；phase_anchor、arcane_battery；两同款主动连续用；换槽/遗忘重学/卖买/死亡；五无主动物品 | 真来源走同一 Combat；guard_pulse 只有成功释放才给屏障/移除全部 Root，不清其他控制、CD或死亡，不把 Root 误判成禁 Skill。双主动独立、属性两份/同名被动一次，保留原 CD；店外/圈内/无钱/满槽仍按战斗条件可用，空 Active 不伪造能力，echo_maul 无副攻击订单。 |
| AI-A11 进攻解除保护 | 圈内保护 AI 发当前有效新意图，分别 Missing 目标/错误支持面/未接地/无路/距离远/蓝少/CD未到；对照 Query、旧包、异生命、终局 | 前组为语义执行失败，即使 Rejected 也 ProtectionRemoved/LifeRevision 更新，失败不扣动作 MP/CD；资格事件在 Capture 前生效，保护时段不追算占领，未接地仍无资格；后组不解除。 |
| AI-A12 死亡/离局效果 | 正常死亡时有已释放独立效果，效果击杀合法目标；另在效果存续时正式离局 | 正常死亡继续原期限并正常发给仍在局死者奖励，可产生 Dead Offer；离局立刻移除身体/效果且离开不发奖；均不能打中同 ID 新生命。 |
| AI-A13 发育/回防/恢复 | 当前 HP 达高营入口但预测剩余不足 Reserve；合适低营；同刻反击/零伤害/超预算/隐藏成员变化；公开占领紧急，低血/零移速/保护/无回复，守圈敌人离圈 | Reserve 只控制模型入场，实际止损沿 Retreat；成本估计不写 HP/CD/RNG、不读隐藏状态、不补最低伤害或抹同刻反击，不将估计当实战保证。回防无除零/全知/等回满死锁，不为一只怪拖延；必要离圈接受清零。 |
| AI-A14 终局/新局/长帧 | 结果边界同时有在途攻击、奖励、选卡、Sell/Buy、复活和 AI 决策；延迟旧通知到下一局；长帧/长期运行 | Close 后新增玩法提交为零；不补跑历史决策；缓存/队列/每 AI 工作有界；仅新 Match 的上下文可继续，旧回包不唤醒策略。 |
| AI-A15 营地反击/Returning | 路过/碰撞/预览/挥空/被拒命令，对照确认合法命中且首个成员当次死亡；遮挡同组/目标；回位途中 Root/伤害/击杀、存活到家 | 只有确认命中触发存活同营反击，各自重查观察不共享坐标。可见 camp_returning 无仇恨/期限/路径，Root 保留、退出或 Conceal 撤销；Returning 可伤控杀不重追，不限制真人追杀。死亡正常恰一次奖励；到家重置不发奖/不复活死者，隐藏重置不进入 AI 输入。 |

运行产物写忽略的 Artifacts。每条证据至少带本次 commit/内容摘要、真实连接拓扑、
Match/Contender/BodyRef、授权 Frame/Map 修订、策略原因/公共目标、
Order/Action/Choice/Shop 请求和回执、稳定死亡奖励键、核心阶段/结果。
权威 observer-checker 可以比较真值以检查泄露，但不得把这份全知检查数据送回策略。
只看屏幕或 Host 日志、仅 SpawnActor 模拟“human”、旧 B92 报告以及模型实验都不足以通过 AI-A02。

当前可复用起点是 RunNavigationCheck、RunCombatNetworkCheck、RunViewNetworkCheck、
RunInventoryCheck、RunVendorCheck、RunLoadoutCheck、RunTests 的有效部分；
必须去掉不参赛 Host/旧 profile 前提并接目标断言，不能直接把旧成功计入本表。
最小相关构建、Bootstrap 与实际场景由后续代码切片执行；本次纯文档只核验事实、链接与范围。

## 9. 专家研究、独立审核与就绪边界

| 实际专家 | 独立职责 | 结论范围 |
| --- | --- | --- |
| `demo-ai-strategy-expert`，agent `efd9bd03-4282-4e47-bfac-739b37e88e7a` | 独立提出单一策略、完整自主闭环、参数需求和正反覆盖，不兼任工程或最终 QA | 策略研究完整交付；不存单位历史、不建通用规划平台，已按全部已确认生命周期/观察规则收敛。 |
| `demo-ai-authority-engineer`，agent `72a21c5b-fcc2-4894-8676-bcd5927d7064` | 独立核对实际生产者/消费者、Controller/导航/领域耦合、精确退役集合和检查入口 | 静态工程研究完整交付；B92 不能充当自主 AI，无连接 adapter 与原域验证是选定最小接线。 |
| `demo-ai-fairness-integration-qa`，agent `c614cdd1-e582-4f12-82d1-5d06ce143477` | 两份研究和整合稿完成后独立只读审核；未参与策略、工程研究或整合写作 | **静态文档审核通过，无剩余必须修正问题。** 覆盖营地反击/Returning、纯值成本估计、到店前规划与开店/成交权限边界；不代表已实施、已运行或数值已批准。 |

模块设计和对接输入已通过独立静态审核，可用于分派后续实施切片。
权限/身份/拒旧基础可以先接线；自主闭环完成仍以批准内容、原生接口、
真实拥有连接和 §8 的实际证据为门槛，不能将审核通过直接升级为运行就绪。

公开工程资料由工程专家通过 Context7 的 Epic 文档集合核对：
[AAIController](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/AIModule/AAIController)
说明 MoveToActor 会持续更新目标位置，不能持隐藏 Actor 用它追踪；
[CharacterMovement](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UCharacterMovementComponent)
与 [SavedMove](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FSavedMove_Character)
支持复用原生运动而非另写积分器；
[Actor Ticking](https://dev.epicgames.com/documentation/unreal-engine/actor-ticking-in-unreal-engine)
只提供执行依赖，不替代本项目权威批次/同刻占领合同。
这些通用页面不证明 Unreal 5.8.4 补丁下已构建或运行。

已确认的生命周期与观察行为不再列为待问用户问题。
首版内容/数值仍需产品审查批准，正式接口、单图/资产引用、生成链与目标场景尚需实施和运行证明。
本报告不声称 AI 已能实战、已完成平衡、代码已裁剪或完整 Demo 已就绪；
原始研究保留于独立 agent 会话，不把聊天全文、废弃方案或逐轮过程写入仓库。

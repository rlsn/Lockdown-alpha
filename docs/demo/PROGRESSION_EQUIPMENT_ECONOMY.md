# Demo 成长、装备与经济

**目标：v0.10.0 · 架构职责：M6 / M7 / M8 · Unreal Engine 5.8.4**

本报告将 [App Spec](../APP_SPEC.md) §4、§5、§7–11 接到
[架构 R08/R09、N03/N04/N05](ARCHITECTURE.md) 和
[共享契约 SC-01–09](SHARED_CONTRACTS.md)。实施依赖见
[实施计划 I4](IMPLEMENTATION_PLAN.md)，当前组件知识见
[战斗与成长](../systems/COMBAT_AND_GROWTH.md)、
[库存与事务](../systems/INVENTORY_AND_TRANSACTIONS.md)。

**这是实施准备设计，不是运行实现。** “现有”证据固定于源码 `fa11d09`；
本文的新增类型、函数、状态机、协议和验收均为待实施接线。
数值、公式和首版目录只由 `CONTENT_AND_BALANCE.md` 维护；本文引用其正式键，
不建立第二份价格、经验、效果或平衡表。跨域身份、时钟、批次和终局由
`SESSION_MATCH_LIFECYCLE.md` 单点定义，战斗投影由 `COMBAT_AND_ATTRIBUTES.md` 定义，
世界查询由 `WORLD_CAMPS_AND_VISION.md` 定义；这些同批领域文件接入前仅用文件名引用。

## 1. 结论、确认规则与实施门槛

**选定方案：保留一个账本、一份参赛者成长、一条 Combat 来源链。**
复用现有 Ledger 的精确条款、预期修订、整数检查和复算机制；改造 Development、
Inventory、Vendor，使死亡奖励、选择和买卖分别进入同一权威批次的复合提交。
不沿用“先改 OwnerState / 物品 Actor，再扫描差额补账”，不把旧账本误称为已经覆盖 XP。

| 已确认的产品约束 | 本域必须落实的结果 |
| --- | --- |
| 所有人初始近战、单体普攻；群攻和攻击距离可经装备或属性成长获得 | 初始构筑不授主动；获准的攻击距离、普攻群攻修正进入同一 Combat 投影，不是只有面板数值。 |
| 1 至 10 级，每升级一次三选一，最多九次 | 累计 XP、等级、已获机会同一提交；连升排队，满级仍能处理已有机会。 |
| 三张不同且合法的卡；技能最高三级 | 未学技能仅提供一级学习，已学未满级仅提供下一阶，满级排除；属性卡不可冒充三个相同选项。 |
| Q/W 两角色主动，满槽需确认替换 | 未确定替换不消费；遗忘旧技能及等级，不退款、不保留备用库；再学一级。被动无独立槽上限。 |
| 两装备、无背包、允许同款双持，满槽先卖后买 | 容量由权威物品位置限制，不由 UI 格子限制；售出和随后买入是两个独立事务。 |
| 同款基础属性相加，同名被动只生效一次 | 属性逐实例汇总；被动按稳定内容键去重，不按本地化名字或实例数触发。 |
| 两份同款主动独立冷却，可以连续使用两次 | 两个持久 `IssuedCooldownId` 各自持有截止时刻，不使用同款共享 `ReadyAt`。 |
| 失去来源期间继续计时，重持有沿用原结束时间 | 售出、遗忘、换槽、死亡不缩短已开始的 `ReadyAtMs`；回购优先承接未结束的离体债务。 |
| 已释放独立效果不因普通死亡、售出或遗忘而取消 | 继续原定期限；依赖持续持有/施法的光环、被动、引导按维持条件失效。正式离局取消未结束独立效果，但不追溯改写既成死亡事实。 |
| 最后一击实际所有者独占 XP 和金币 | 野怪、其他参赛者的合法击杀一次结清；阵亡但仍在本局的来源可以收奖。无主、自杀、回位、离开不产生额外收益。 |
| 死亡不丢成长、金币、装备或待选；新局清局内状态 | 持久状态不放在 Pawn / Widget；死亡取消身体期请求，不重发初始资源。 |
| 死亡等待可以确认选择，只更新保留构筑 | 不提前复活、不治疗尸体；当前 Life 和生/死确认阶段重新校验。 |
| 最大 HP/MP 改变时保持原生命/法力百分比 | 活体只按一次最终属性投影同比换算；不采用保持原绝对当前值的规则，精度由内容/Combat 单点定义。 |
| 商店不安全，本体不可攻击 | 买卖重查存活、比赛阶段和实际范围；受伤本身不附加脱战、安静或无敌政策。1/2 主动不依赖商店。 |

以上双持、叠加、独立主动与失源继续计时已获用户确认，不再保留共享冷却或冻结计时备选。
基础文档中的旧待审措辞由集成负责人同步，不由本报告反向改写这些决定。

本域上述产品选择均已定案，详见 §10；具体内容/数值仍待 PR 审查，
资源同比换算的精度与边界还须与内容/Combat 的唯一公式一致。
这些门槛不阻塞状态所有权、拒旧、删除依赖和事务设计；
本域不能宣称游戏已实现、整体输入已批准或真实联机已通过。

## 2. 已核验实现及不可直接沿用之处

以下是工程专家的实际源码审计，不从旧 SourceData 反推新需求。
`Public` / `Private` 路径均在 `Source\Lockdown`；行号对应上述源码基线。

| 编号 | 真实入口与证据 | 可复用机制 / 必须替换的政策 |
| --- | --- | --- |
| E1 | [LDGameMode.cpp:378–387](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L378-L387)，`StateFor / SyncOwner` | 当前 `Record.State` 可写且直接复制给拥有者。保留参赛记录和定向发布，收紧写入口；OwnerState 不再是各服务随意改钱/XP 的入口。 |
| E2 | [LDCharacterBuild.h:24–64](../../Source/Lockdown/Public/Development/LDCharacterBuild.h#L24-L64)、[LDCharacterBuild.cpp:12–18](../../Source/Lockdown/Private/Development/LDCharacterBuild.cpp#L12-L18)、[162–208](../../Source/Lockdown/Private/Development/LDCharacterBuild.cpp#L162-L208) | 有 Level/Ranks/Revision/来源，但构造器授技能，`AwardXP` 发属性点/技能点，`Prepare` 收整份草稿且禁止降 rank；没有机会队列或持久 Offer。 |
| E3 | [LDEconomyLedger.h:8–62](../../Source/Lockdown/Public/Economy/LDEconomyLedger.h#L8-L62)、[LDEconomyLedger.cpp:36–100](../../Source/Lockdown/Private/Economy/LDEconomyLedger.cpp#L36-L100) | 有账户、物品、事务 Id/Cause、精确重放、预期版本、溢出拒绝和 `Reconcile`；没有 XP、等级、选择，也没有目标装备 DefinitionId。 |
| E4 | [LDVendorDirector.cpp:19–81](../../Source/Lockdown/Private/Inventory/LDVendorDirector.cpp#L19-L81)、[LDVendorTypes.h](../../Source/Lockdown/Public/Inventory/LDVendorTypes.h) | 当前是书店，无出售命令；额外检查安静、受伤、动作、旧 LOS 等。购买先 `Adopt`、改 `O->Gold`，再补账，不是目标复合事务。 |
| E5 | [LDInventoryDirector.cpp:45–76](../../Source/Lockdown/Private/Inventory/LDInventoryDirector.cpp#L45-L76)、[LDGameMode.cpp:538–549](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L538-L549)、[LDEconomyDirector.cpp:111–150](../../Source/Lockdown/Private/Economy/LDEconomyDirector.cpp#L111-L150) | `Changed / Adopt` 可先发布，`RecordEconomyGold` 不把失败传回 Vendor，`Record` 事后扫描差额。必须撤销这条常规玩法写链，而不是再添加第二钱包。 |
| E6 | [LDWorldDirector.cpp:90–110](../../Source/Lockdown/Private/World/LDWorldDirector.cpp#L90-L110)、[LDCombatDirector.cpp:320–375](../../Source/Lockdown/Private/Combat/LDCombatDirector.cpp#L320-L375) | 当前 `Contact` 有历史 LastOwner 残留；野怪钱/XP/Paid 分步且重试新建事务，PvP 只给 XP 并有旧配对/总额限制。不能直接改名为统一奖励。 |
| E7 | [LDInventoryTypes.h:9–65](../../Source/Lockdown/Public/Inventory/LDInventoryTypes.h#L9-L65)、[LDV08Loadout.cpp:22–77](../../Source/Lockdown/Private/Development/LDV08Loadout.cpp#L22-L77) | 有真实实例、来源失效及派生投影；当前容量、背包/Quick、十一绑定/六动作与重复 family 拒绝必须删除。 |
| E8 | [LDContentActions.cpp:82–95](../../Source/Lockdown/Private/Combat/LDContentActions.cpp#L82-L95)、[LDContentRelease.cpp:124–126](../../Source/Lockdown/Private/Combat/LDContentRelease.cpp#L124-L126)、[LDV08Modifiers.cpp:14–19](../../Source/Lockdown/Private/Combat/LDV08Modifiers.cpp#L14-L19) | 装备主动已有 Combat 路径，但旧 `ItemFamilies` 共享锁不适用于独立双主动；主动/被动还改实物和库存修订。目标分离结构修订和 readiness 修订。 |
| E9 | [LDDevelopmentDirector.cpp:43–89](../../Source/Lockdown/Private/Development/LDDevelopmentDirector.cpp#L43-L89)、[121–207](../../Source/Lockdown/Private/Development/LDDevelopmentDirector.cpp#L121-L207) | 现有站点/静止/读书/准备流程不是三选一。有限回执和拒旧可复用，但新请求须保存规范化条款，不能只按 ID 返回结果。 |
| E10 | [LDEconomyCommerce.cpp:25–67](../../Source/Lockdown/Private/Economy/LDEconomyCommerce.cpp#L25-L67)、[LDAcquisitionDirector.cpp:99–130](../../Source/Lockdown/Private/Inventory/LDAcquisitionDirector.cpp#L99-L130) | 可抽取“创建失败前置、输入身份精确、先账后投影”的局部能力；不能顺带保留市场现金、书籍、制作和品质政策。 |

**物品修订不能偷换。** 当前 `Adopt` 递增实物 revision 后产生 owner 条目，
而 Ledger 新物品从自己的 revision 开始；`Record / Audit` 不证明两者相等。
目标以 Ledger 的唯一结构修订生成 ItemRevision 投影，不能把现有网络 ItemRevision
原封不动塞进账本 `FItemDelta.Expected`。施法推进 readiness，不再伪装成物品位置改变。

**现有 Commit 也未证明“后半段不可能分配”。** `Items.Add`、嵌套数组/事务复制、
`Receipts.Add` 仍在 mutation 段，且完整回执历史未设淘汰边界。
§4 的预分配、原子发布和有界拒旧是必须实施的改造，不是已有异常安全保证。

## 3. 唯一状态、正式类型与版本职责

本节及后续签名均是**目标接口**，落在现有组件；不要求新增通用 Manager、
第二套账本、第二份技能状态或世界事件平台。

| 唯一写入者 / 目标类型 | 真相与限制 | 只读消费者 |
| --- | --- | --- |
| `ALDDevelopmentDirector` 修改参赛记录中的 `FLDCharacterBuild` | Level；当前已学技能及 rank；Q/W；永久属性贡献；BuildRevision。奖励/选择可改其获准字段；买卖只能通过 Development prepared patch 推进装备派生的 BuildRevision，不能直接改等级/技能。不得接客户端完整 Build。 | Combat 准备投影、自身 UI、该参赛 AI。 |
| 同一参赛记录的 `FLDProgressionState` | `int64 CumulativeXP`、ProgressionRevision、已获/已消费机会边界、当前一个 `FLDChoiceOffer`、局内私有随机材料、最多九次选择结果。Level 只在 Build 存一次。 | `FLDProgressionView`；不复制给其他参赛者。 |
| 精简后的 `ALDEconomyDirector::Ledger` | 唯一 `int64 Gold`、AccountRevision；物品 `InstanceId / DefinitionId / Owner / EquipmentSlot / ItemRevision`；每 owner 的 InventoryRevision。每活实例只有一个位置，最多两个活装备位置。 | Inventory 查询/准备、自身钱物 DTO；Combat 只消费来源和属性，不改账。 |
| `ALDInventoryDirector` | 两槽资格、实例解析、准备变更；不另存可写 Items/Bindings/Actor Holder 镜像。槽只允许 0/1，无背包、Quick、地面掉落位置。 | `FLDEquipmentView`、Vendor、Combat 准备器。 |
| Combat 写参赛持久 `FLDSourceReadiness` | 实际来源 → `IssuedCooldownId` 绑定、SourceBindingRevision；每个 ID 的 ReadyAtMs 和 ReadinessRevision；被动来源集合及需保留的触发状态。成长只提供 prepared 来源增删。 | Q/W、1/2、属性面板、AI 自身读模型。 |
| Vendor 的 `FLDShopSession` 与领域请求记录 | ShopId、SessionId、Life、QuoteRevision、局内 CatalogDigest、请求序列/条款/结果；不是持有装备或金币的权威。 | 当前拥有者商店页及 AI 购物适配。 |
| `FLDOwnerState` / 各 View | 上述真相的一次一致投影，携带 Match/Life 和发布修订；不得反向写账、成长或 Combat。 | 拥有连接；GameState 不持有全体私有构筑。 |

机会队列采用最多九项的有界表示：`EarnedThrough`、`ConsumedThrough` 表示连续机会序号，
待处理区间是二者之间尚未消费的序号，队首是 `ConsumedThrough + 1`。
每次升级记录对应序号及原奖励关联；选择结果最多九份，不能因回执窗口淘汰而丢失
“这个 Offer 已消费”的证明。`EarnedThrough` 必须等于当前等级减一；
二者在同一奖励 patch 中推进，`ConsumedThrough` 只能随有效选择推进。
PendingChoiceCount 从队列计算，UI 不保存另一可写次数。

`FLDChoiceOffer` 至少含 `OfferId`、OpportunityOrdinal、三个不可变
`FLDChoiceOption{OptionId, Kind, DefinitionId, FromRank, ToRank, EffectReference}`、
生成时 BuildRevision、当前 GuardBuildRevision、OfferRevision、CatalogDigest、状态。
Offer/Option ID 由服务器在相应机会生成时一次建立；OptionId 不等于显示顺序、
翻译名或客户端提交的属性值。属性复合卡的全部分量属于同一个 EffectReference。

| 操作 | 必须推进的修订 / 不得推进的内容 |
| --- | --- |
| XP 增加但未升级 | ProgressionRevision；有钱变化时 AccountRevision。不得因读 XP 重建 Build。 |
| 奖励升级 | ProgressionRevision、BuildRevision；已有 Offer 只更新保护版本，不换卡。 |
| 选择成功 | ProgressionRevision、BuildRevision、选择结果；涉及来源时 SourceBindingRevision。 |
| 买入/卖出 | AccountRevision、InventoryRevision、对应 ItemRevision、BuildRevision、需要的 SourceBindingRevision；同一最终投影。 |
| 结清后的账户退休 | 通过Ledger的AccountRetire提交推进AccountRevision和独立Ledger Serial；金币/XP/物品/Build不改变，不直接修改Retired标志。 |
| 主动释放/被动触发 | Combat 的 readiness / 动作修订；不是库存结构修改，不能使所有选择因每次普攻持续过期。 |
| 秒表流逝、查询、关窗、回执重放 | 不改变构筑、库存、机会或来源绑定版本；UI 从绝对截止推显示。 |

### 3.1 固定标量类型与编码

下表是目标协议的确定类型，不表示现状同宽。现有 Build/Inventory/物理 Item/Ledger Item/
Account revisions 均为 `uint32`，目标统一升为 `uint64`；Combat 已采用同一修订宽度。
新窄协议一次迁移生产者、消费者、UHT/NetSerialize 和测试，不截断回旧字段，不保留旧 width 兼容。

| 字段族 | 唯一类型 / 编码 | 初值、限制与换代 |
| --- | --- | --- |
| Generation / Life | `uint32`，沿核心编码 | 仅核心推进，零值不接受为有效比赛/已建立生命；到上限明确失败，不回绕。LedgerMatch仍是FGuid。 |
| ContenderId / LedgerMatch / OfferId / OptionId / ItemInstance / IssuedCooldownId / ShopSessionId | `FGuid`，UE内置四个uint32的16字节编码 | 有效ID非零；可选项单独带存在标志，不拿零GUID当成功默认。Offer/Option/会话由服务器一次建立；Item按§4发行；CD每定义最多两份。新Match无旧执行权。 |
| RequestId / Ledger Serial / IssueSerial / CommitOrdinal | `uint64` | 请求每Family从1开始，只在新Match重置；死亡不重置。Ledger/发行独立计数见§4，CommitOrdinal由核心给。0不表示一笔成功业务。 |
| Build / Inventory / Item / Account / Progression / Offer / Quote / SourceBinding / Readiness revisions | `uint64`，包括所有Expected/Guard引用 | 匹配初始化或对象建立完成后基准为1，只由相应唯一写入者checked递增；当前倒计时不逐帧增修订。不能截断后比较。 |
| Level / Rank / OpportunityOrdinal / SlotIndex | `uint8` | Level=1..10、已学Rank=1..3、机会=1..9、Slot=0/1；队列计数可为0。未学/空槽用可选存在位，不伪造Rank0技能记录。 |
| Gold / XP / P2 Current与Max | `int64` | Gold/XP非负；资源域依内容/Combat；任何乘加/存储越界在prepare拒绝。 |
| ReadyAtMs / AtMs / 运行时毫秒期限 | `int64` | 当前Match绝对时间；ReadyAtMs=0仅表示未开始CD的初始就绪，无负期限。原始事件秒为核心`double OccurredAtSeconds`，不得用毫秒桶代替先后。 |
| `FLDCatalogDigest` | `uint8 Bytes[32]`，SHA-256，wire固定32原始字节 | 不传FString/变长hex；日志/展示是64个小写十六进制字符，边界验证后转换。本局固定，不因开面板换摘要。 |
| 本域技能/装备 DefinitionId | 服务端`FName`；与Combat同一wire：固定64字节UTF-8缓冲，最多63字节内容，NUL终止且余字节零填充 | 当前批准ID为`[a-z][a-z0-9_]*`ASCII子集；先验证UTF-8、终止/填充、长度、规范与catalog再构造FName。未知/非法拒绝，不截断。效果参数由服务器解析。 |
| Kind / Family / Outcome / Reason / bool | 领域Kind/Family/Outcome为`uint8`枚举，Reason为`uint16`，bool wire仅0/1 | 唯一领域头文件声明明确支持值；decoder逐值白名单，不用历史最大枚举包住废弃值。可选值编码为存在位+存在时的值。Family仅Choice/Shop，不能伪造Reward RPC。 |

ShopId、世界修订/观察句柄、Permit、EventStamp 和 ELDLifePhase 直接引用其拥有域正式类型，
不在本域另声明同名身份。`FLDWorldRevision`沿世界的MapId/LayoutRevision/GeometryRevision组合，
不压成另一个全局修订号。`ExpectedLifePhase`只接受核心Alive/Dead；其余状态明确拒绝。
所有DTO和回执的结构修订使用本表的同一宽度；未涉及的修订不虚增。

现有B84各载荷分别有SHA-256，并非已存在统一根摘要。目标请求的 CatalogDigest
引用内容域已对齐的唯一 Demo `manifest.json` 根：manifest自身不含自身hash，
对规范化manifest树经现有 `CatalogTools.encode` 产生的完整字节计算SHA-256，contract保存结果。
覆盖角色/成长、技能、装备、奖励/价格/政策、稳定ID/schema、handler投影和声明的世界/
显示/payload摘要。
子载荷摘要由唯一Demo compiler生成/核验，但不能将Combat子摘要与Equipment子摘要都当此根比较。
根manifest的实际生成输入/输出由内容域维护，本域不复制生成器、哈希常量或第二份数值。

## 4. 共同身份、提交阶段、原子性与失败恢复

### 4.1 不另造死亡或时间身份

复用生命周期与 Combat 已收敛的以下类型，具体定义只由该域维护：

| 正式类型 | 本域使用方式 |
| --- | --- |
| `FLDMatchContext{uint32 Generation; FGuid LedgerMatch;}` | GameMode 建立本局映射；网络核验 Generation，Ledger 使用 LedgerMatch；新局二者一起换代。 |
| `FLDVictimKey{Kind, EntityId, Life}` / `FLDCombatBodyRef{Victim, BodyIdentity}` | 参赛者 EntityId 映射 ContenderId；野怪由世界营地成员映射。身体是短寿命物理引用，不是奖励账号。 |
| `FLDDeathKey{LedgerMatch, Victim}` | 唯一死亡/奖励键。**不含来源、EffectId、DeathSerial、BodyIdentity 或重试序号。** |
| `FLDCommittedDeath` | 消费 Combat 第一次零 HP 锁定的不可变事实：Key、VictimBody、受害参赛者、实际致死所有者或 NoOwner、FatalEffectId、释放来源/身体、RewardDefinitionId、DeathCause、EventStamp。 |
| `FLDCommittedDeath.RewardBasis` | 核心/Combat唯一冻结：`bool CreditOwnerEligibleAtDeath`、明确NoRewardReason、原CreditOwner；玩家`uint8 VictimLevelAtDeath / uint64 VictimBuildRevisionAtDeath`，营地用成员奖励定义。它们是资格/计算输入，不加入DeathKey。 |
| `FLDAuthorityBoundary{Match, BatchSerial, AtMs}` | 本次不可回拨的权威边界；所有截止统一毫秒，时间取核心 `GetAuthorityNowMs`。 |
| `FLDGameplayWritePermit{Boundary, Phase, GateRevision}` | 核心当前阶段签发；最终应用必须 `GameMode.ValidateWritePermit(Permit, ExpectedPhase)`，不能用一个“仍进行中”布尔值代替。 |

每次 Effects 的致死集合先各自固定完整致死快照，再结该集合奖励；否则先发给受害者的另一份 XP
可能改变随后查询到的等级。后续 Transactions 因果子批若产生新致死集合，取当时真实已提交
等级重新执行该屏障，不要求提前预知整个 Boundary 的未来死亡。
无主死亡不可继承 LastOwner，错误 payload 不可改已锁定收款人。
所有者在**实际致死时点**已阵亡但仍 InMatch 时有效；该时点前 Session 已处理离局则无资格，
不以活 Pawn 判断账号。核心在 `RewardBasis.CreditOwnerEligibleAtDeath` 中认证当时资格，后续退出/清理或奖励重试
不能把此前合法收奖改成 NoReward；不得用重试时已变更的当前成员状态追溯撤销。

EventStamp 采用核心的 Generation、Provider、StreamId、ProducerSequence、
`double OccurredAtSeconds`、OccurredAtMs、RecordedAtMs、CommitOrdinal。
先按原始实际发生时间排序，真正同刻才按 phase/ordinal；毫秒只是向上投影的桶，
不能把同一毫秒中较晚事件提前。重试不改原始死亡戳；
新奖励输出使用 Progression 的正式流，不拿 RequestId 或 Ledger Serial 冒充生产者序列。
世界的 CampId/MemberId/CampGeneration 只映射同一 Victim/Life，不有第二条 MemberLife 或发奖链。

### 4.2 确定批次及 Prepare / Apply

唯一阶段顺序为：

`Session → Motion → Actions → Effects → DeathsAndRewards → RespawnsAndWorld → Transactions → Capture → Close → Publish`

奖励只在 `DeathsAndRewards`；选择与买卖只在 `Transactions`。
同刻死亡、离店、新生命、离局已按前置阶段生效后再重验交易。
同批奖励可以使随后旧余额/BuildRevision 的操作被拒；不能偷偷替调用者改成新版本。
本批合法交易在 Capture 前提交，正常胜利关闭后仅保留结果，不追回已真实成交的交易。
新成长/装备不能反向重算此前 Effects 的伤害。

```cpp
FLDRewardReceipt ALDEconomyDirector::CommitDeathReward(
    const FLDCommittedDeath& Death,
    const FLDGameplayWritePermit& Permit);

FLDPrepareLedgerResult LDEconomy::FLedger::Prepare(const FTransaction& Transaction);
void LDEconomy::FLedger::ApplyPrepared(
    FLDPreparedLedgerCommit&& Prepared, const FLDGameplayWritePermit& Permit);

FLDPrepareGrowthResult ALDDevelopmentDirector::PrepareExperience(
    const FGuid& ContenderId, const FLDRewardGrant& Grant);
void ALDDevelopmentDirector::ApplyPreparedGrowth(
    FLDPreparedGrowthChange&& Prepared, const FLDGameplayWritePermit& Permit);
```

Combat 配合的正式入口为 `PrepareBuildChange(Caller, FLDBuildProjectionInput,
FLDExpectedBuildVersions) -> FLDCombatPrepareResult` 和
`ApplyBuildChange(FLDPreparedCombatProjection, FLDGameplayWritePermit)`。
调用者是服务器解析后的本局参赛者，不是客户端自选账号；原 `FLDCharacterBuild`
仍是唯一构筑。提交许可只有上述核心类型，Apply不对RPC开放。

| 步骤 | 必须执行的操作 | 不得发生 |
| --- | --- | --- |
| 查重/资格 | 对照核心原始死亡或领域请求条款；核验 Match、成员、原因、定义和现有结果。 | 为重试换 DeathKey/事务 Cause；把失败当合法零奖。 |
| Validate | 纯算 Gold/XP、累计阈值、等级/机会、最终 Build、装备位置/容量、被动/来源；检查全部预期版本、整数和修订溢出。 | 先写 XP 再看钱能否入账，或先扣钱再发现来源/效果非法。 |
| Stage | 在原 Ledger 准备 after-image/差量，准备成长字段 patch、Combat 投影/绑定、机会/候选工作区、最终回执和发布 outbox。所有容器与嵌套 payload 复制/容量在此完成。 | 提前发行 IssuedCooldownId、Adopt/Changed/SyncOwner；创建会触发外部 BeginPlay 的权威物品。 |
| 最终重查 | 同一服务器线程重查 Permit、全部 prepared 依赖版本；交易重新采样商店资格。进入不可重入、无等待的连续提交段。 | 保存 Permit 到 Pending/Offer/下批，跨 callback 使用旧许可，允许外部修改 GateRevision。 |
| No-fail Apply | 仅移动已准备数据/交换存储、有界赋值；同时应用 Ledger、成长、Combat、来源失效、机会和终态回执；只替换本域字段，不覆盖新 readiness。 | 分配、加载、RPC/Delegate/OnRep、可失败 Actor 操作或第二次业务校验后部分返回。 |
| Publish | 子结果均封存后生成一致 owner 投影和回执；按共同 Publish 阶段发出。 | 先公开新钱旧物、新等级旧机会，或用客户端 OnRep 到达顺序当原子提交。 |

子 Apply 的 Permit 检查在该连续段内必须保持恒成立；如果 Ledger 已改变而后续子 Apply
还能正常返回版本/许可失败，设计并未满足 no-fail，不能把它记录成成功。
预分配并不等于假定 UE OOM 可捕获；只对项目能够报告的准备失败提供恢复。
引擎级 OOM、进程崩溃或断电的跨进程恢复不属于 Demo；不能新增世界存档系统补这项承诺。

### 4.3 奖励结果、水位与有界故障路径

| `FLDRewardReceipt` 状态 | 含义与记录 | 后续处理 |
| --- | --- | --- |
| `Granted` | 同一死亡的 Gold/XP/等级/新增机会及必要投影全部提交；带原事实指纹、Ledger 关联、实际增量、等级前后和结果修订。 | 可以封已处理事实、推进水位；同条款只读重放。 |
| `NoReward` | 核心合法死亡事实明确无奖：无主、自杀、实际致死时已离局来源或非奖励对象等有类型化原因。不是回位产生的假死亡。 | 同 Key 记已处理，不能后来补来源发奖；可推进水位。 |
| `RetryableNotCommitted` | 必需奖励合法，但准备资源暂不可用；钱、XP、等级、机会、随机状态均未提交。 | 留同一事实，不发正常完成水位，不进入正常 Capture。 |
| `PermanentlyRejected` | 旧局、伪造/未提交事实、同 Key 异条款、非法目录、越界等明确错误。 | 非法输入拒绝；若是核心真正待付事实的不可恢复错误，上报权威故障，不能伪装 NoReward。 |

核心保留已提交死亡 inbox。本域暂失败后释放当前 Permit，只回收可以安全释放的已决记录/
准备资源，由核心重新签发当前合法奖励阶段的 Permit，对同一事实重新准备。
**一次受控恢复仍失败，或错误已不可恢复，则上报 `AuthorityFault`，由生命周期无正常胜者
故障关闭；明确该奖励未支付，关闭后不后台补发。** 不无限 Pending，不跳过必需奖励判胜，
也不回滚已经锁定的死亡来制造另一发奖机会。故障关闭不得伪造成功 fence。

正式调用为 `GameMode::FailAuthorityBoundary(const FLDAuthorityBoundary&,
const FLDAuthorityFault&) -> FLDFaultReceipt`：原子关闭写闸、进入 `Faulted`，
记录未支付/未完成事实和原因，明确反馈后回前端。它不是占领/HostEnd 的正常 ResultReceipt，
不推进已闭合水位；若正常终局已先锁定，返回 `AlreadyTerminal` 并仅记错误，不覆盖胜者。

离局的身体/效果立即清理，但本局参赛记录和账本不能先删除或退休到无法结算：
先前已认证致死事实的奖励必须结清，再越过其 resolved 水位完成相应账务退休。
关闭新命令权限与偿付既成事实是两件事；这不是离局后新发奖资格或断线重连能力。
退休本身也必须是§4.4的可重放Ledger操作，不能在生命周期清理回调中直接置Retired。

`FLDProviderFence{Generation, Provider, StreamId, ThroughMs, ThroughSequence}` 只封完整无缺口前缀：
必需奖励都 Granted/NoReward、本批受理选择/买卖都成功或明确拒绝、因果 outbox 已封存后，
Progression 才发布 Through；空批同样封时间水位，但不制造假交易。
若事务引发新的合法同刻 Combat 工作，交核心排空显式因果子批次后才封水位；
不得在 Publish 回调中再施法/发奖。Capture 不能用“当前队列为空”代替完整 fence。

### 4.4 去重和有限留存

- Choice 与 Shop 分别使用本局参赛者范围的单调 `uint64 RequestId`。
  每条流只允许一个未决变更；未来序号缺口明确 `OutOfOrder`，不缓存无限乱序请求。
  受理即固定规范化 payload；最终成功和拒绝都消费该请求序号。
- 同 ID 同 payload 返回相同 Pending 或最终回执；同 ID 异 payload 为 `RequestConflict`。
  双击 Pending 按钮复用原请求，不新造第二次购买；真正购买第二件需前次终态和当前版本。
- 延续 Vendor 的 **64 份最终表现回执窗口**作为工程限额，另保存已处理序号水位。
  超窗返回 `ReceiptExpired` 与合法当前状态，不重执行、不猜测历史成功。
  窗口不是选择结果历史：九次已消费 Offer 的小型结果索引保留整局。
- 死亡回执使用 DeathKey 与原始事实指纹。完整回包可有界淘汰，但核心对应实体已解决 Life
  水位及当前未决事实不能提前丢弃；旧 Life 无回包时明确拒旧，不能当第一次死亡。
  改 Source/Effect/DeathSerial/BodyIdentity 只会与原事实冲突，不能产生第二份奖励。
- 所有参数/定义集合、活实例、来源 ID 和 pending 容器均有明确上界。
  发布、审计和请求序号溢出也是准备错误，不靠整数回绕或静默饱和维持“无限对局”。

**账本压缩的精确目标合同：**

| 结构 / 字段 | 确定含义 |
| --- | --- |
| Ledger `CommittedSerial / NextCommitSerial` | `uint64`，初始化为0/1；每次真实 Ledger 提交取 Next，再 checked 递增。独立于回执/尾部数组长度，压缩不改变它们；重复与只读查询不占新号。 |
| Ledger `IssuedThrough / NextIssueSerial` | `uint64`，初始化为0/1；每次装备 Create 原子使用下一个发行号，失败不推进。记录和回执保留 IssueSerial，已使用发行号本局不复用。 |
| `FLDLedgerAuditPrefix` | Match、ThroughSerial、该点每个账户的 Id/Gold/Revision/Retired/RetireSerial/RetireLifecycleStamp、每 owner InventoryRevision、活实例的 Id/IssueSerial/Definition/Owner/Slot/ItemRevision、IssuedThrough、InitialGoldTotal、累计 Mint/Burn及审计总量。保留退休账户，不只保留全局金币总数。 |
| `FLDLedgerJournalEntry` | 独立 Serial、业务键/原条款指纹、Cause、Gold/Item/AccountRetire 的完整delta与预期修订、结果修订、核心CommitOrdinal；AccountRetire含生命周期关联，足以独立复算余额与退休状态。 |
| `JournalTail` | 精确为前缀之后连续的 Ledger 提交；容量64条是工程内存限额，不是比赛奖励/交易次数上限。下一条会超过容量前必须压缩或明确准备失败。 |
| `FLDReceiptFrontier` | 每参赛者每 Choice/Shop 流的 FinalizedThrough、当前已受理条款以及独立64份最终回包窗口。压缩 Ledger 不删除该拒旧证据；窗外不再断言条款相同，只返回 ReceiptExpired。 |
| 核心 `ResolvedLifeThrough` | 按稳定 `(Victim.Kind, Victim.EntityId)` 保留最高连续已解决 Life及当前未决原事实。Life推进/释放记录前必须完成该旧生命奖励，不以丢回执当已处理。不是另一 MemberLife 计数。 |

前缀是不可变**审计输入**，不是可写钱包或世界存档。其初始点为 Serial 0 的明确开局状态，
InitialGoldTotal记录获准初始化总量，后续Mint/Burn从零累计，复活绝不重建此基线；
后续前缀由旧前缀加尾部独立重放构造，而不是直接复制可能已算错的当前余额当正确答案。
每个已提交装备 InstanceId 由唯一服务器发行器将本局 IssueSerial 一一编码成 FGuid：
四个字依次为 `LedgerMatch.A`、`LedgerMatch.B`、发行号高32位、低32位。
完整身份始终带当前 Match，禁止跨 Match 只按此 GUID 查找；本局内无哈希截断碰撞或回收复用。
客户端不能提供 Create 的 InstanceId/IssueSerial，不能让未知旧 ID 被 Adopt 成新物品。

`FLedger::TryCompactResolvedPrefix(uint64 ExpectedCommittedSerial) -> FLDLedgerCompactResult`
只在服务器不可重入的维护点执行：无持有旧 Ledger 内存的 prepared plan，全部尾部业务终态
与 outbox 已封存，没有未解决事实/回执准备/消费者 pin 指向待释放记录。
不等待网络 ACK；可查询的完整回包已在独立窗口中，已释放效果持有独立来源快照而非 Ledger 指针。
压缩前必须满足以下顺序：

1. 核对 ExpectedCommittedSerial 和尾部连续性，取 `S = CommittedSerial`，不跳过未决记录。
2. 从原 Prefix 重放全部 Tail，逐账户/修订/实例归属/库存修订/发行号/审计总量复算，
   比较当前唯一 Ledger 状态；任何不一致为 `CorruptHistory`，不能覆盖差异。
3. 预分配由复算结果构成的新 Prefix(S) 和空 Tail；同时确认请求/死亡拒旧水位仍完备。
4. 一次交换 Prefix/Tail 并释放旧记录；NextCommitSerial、NextIssueSerial、钱包/物品/
   业务修订、回包窗口、死亡水位和九次选择结果完全不变。准备失败保留旧 Prefix/Tail。

`FLDLedgerCompactResult` 为 `Compacted / NothingToCompact / Pinned / PrepareUnavailable /
CorruptHistory`。被 pin 或准备失败时不假成功；可选命令明确未提交，必需奖励按 §4.3
一次受控恢复或 Fault 收口。正常情况下每个64条已决前缀可持续折叠，不限制比赛长度。
该过程不提供游戏状态 Restore，不保留旧世界 checkpoint 系统。

**AccountRetire 的唯一提交路径：**
核心已标记Left并确认其先前奖励/相关请求已解决后，由
`ALDEconomyDirector::CommitAccountRetirement(ContenderId, LeaveStamp, Permit)`
在 `Transactions` 内部维护阶段准备提交；不是玩家/AI可调用命令。
业务键固定为本局账户的唯一退休事实，LeaveStamp必须等于核心原始离局戳，不能重试换Cause。
`FAccountRetireDelta` 明确含 AccountId、ExpectedAccountRevision:uint64、
ExpectedRetired=false、NewRetired=true、ResultAccountRevision、RetireLifecycleStamp。
准备检查无未结旧债、当前账户/修订和Permit；Apply占用下一个独立Ledger Serial，
只把Retired从false变true并递增AccountRevision，记录RetireSerial/关联戳，
同时写完整AccountRetire journal/outbox，不改Gold、物品或Build。

退休回执明确 `Retired / AlreadyRetired / AwaitingSettlement / Rejected`；
同一事实重放不新增Serial，未结清只是等待其真实前置，不允许直接退休。
不可恢复或一次受控准备恢复仍失败走§4.3 Fault；正常终局已经锁定则只读拒绝，不在关闸后补提交。
Reconcile必须按AccountRetire delta验证旧修订/false状态、重放true和新修订及关联戳；
压缩后的Prefix继续携带这些结果，不能从当前账户补标志以掩盖缺日志。
退休账户及其冻结物品仍参与本局审计；不出售、转移或重新开放该账户。

**三个准入屏障必须在 Ledger Prepare 之前生效：**
请求序号不大于 FinalizedThrough 时仅查已有回包/拒旧；DeathKey 的 Life 不大于对应
ResolvedLifeThrough 时仅重放/拒旧，大于当前核心已提交生命也拒绝；Create 只接受本次准备器
签出的 `IssueSerial = IssuedThrough + 1` 和对应新 ID。
对已发行号，只有匹配的活实例索引才可 Sell/引用；已消费实例即使历史条目已释放也不存在活身份，
不能复购或用旧条款 Create。退款重放先被请求屏障挡住，不靠无限保存 consumed Actor。
无奖死亡可独立封结果，不强制造一条零金币 Ledger 事务；无钱物变化的选择也不造空账。

验收必须覆盖：先提交64条并折叠该前缀，再提交65时尾部只剩 Serial 65，下一笔仍是66而不是
`Receipts.Num()+1` 得到的2；持续压缩后 Reconcile逐账户/实例仍一致；
加入退休→继续提交→压缩后，Gold不变而Retired/AccountRevision/关联戳能独立重放；
旧请求、旧 Create/已售实例、旧死亡分别被自身屏障拒绝，未决 pin不被提前释放。

## 5. 奖励与经验：一次致死到一组完整结果

`CommitDeathReward` 只从核心 committed-death inbox 接收事实，不作为客户端/AI RPC。
资格先看受害对象与关系，再看实际所有者；另一参赛者或野怪的合法击杀才发奖。
持续效果、装备主动、获准普攻副命中使用 Combat 已锁定的真实所有者；
抑制副命中的吸血/递归触发不抑制其合法死亡奖励。

| 输入 | 期望结果 |
| --- | --- |
| 活参赛者击杀野怪或另一参赛者 | 全部奖励归该所有者，无助攻、距离、伤害比例分配。 |
| 两个参赛者同批互相致死 | 两个不同 Victim DeathKey，各按各自致死快照和仍 InMatch 的所有者收奖。 |
| 已阵亡来源的存续效果合法击杀 | 该实际致死时原参赛者仍在本局则全额收奖，不能因无 Pawn 丢 XP 或改发兜底账号。 |
| 无主致死、自己杀自己 | 合法 NoReward，同一死亡仍封已处理；不退回历史攻击者。 |
| 回位/重置、断线移除、Host 结束清理 | 不制造击杀事实，不给钱/XP，不把原旧 LastOwner 回调送奖励。 |
| 致死与正式离局先后 | 致死在先则资格/归属锁定，后来离局不改既成结果；离局在先则立即移除身体并取消其独立效果，不再产生后续击杀。原始时间先于毫秒桶/phase。 |
| 营地成员死亡/整组清空/刷新 | 每成员同一 Life 只一次；清空/刷新仅世界生命周期，不再发一份清营奖。 |
| 营地回位到家 Reset | 仅仍存活成员满血蓝、清临时状态和冷却就绪；不复活死者、不创建新Death/Life、不发奖。回位途中真实致死仍按原生命一次结算，不是参赛者洗CD入口。 |

经验使用内容的累计阈值 `XP.Threshold[L]`；目标等级是合法范围内已达到阈值的最高等级。
前后等级差逐一授予机会，每个序号只一次，不能把连升压成一张卡或一次性预生成全部卡。
`FORMULA.XPToNext` 从同一阈值表派生，不维护另一份会漂移的升级成本。

内容当前提案是满十级继续累计 `int64` XP，但不再升级/授机会、不转金币；UI 显示 `MAX`，
不读取不存在的十一级阈值。该提案与奖励整数公式一并供内容审批，不把旧有限奖励预算改名沿用。
Gold、XP、阈值运算及审计累计的任何越界必须在整奖准备阶段拒绝，
不得只发金币、只发 XP、截掉溢出再返回 Granted。

## 6. 三选一：可实施状态机与合法池

### 6.1 生成、展示、确认和恢复

| 权威状态 / 本地动作 | 转移与持久内容 | 机会是否消费 |
| --- | --- | --- |
| `NoOpportunity` | 奖励提交推进已获机会；没有未决 Offer 时准备队首。 | 否 |
| `HeadReady` | 当前构筑已生效后，以局内固定目录、队首私有随机材料生成三个不同合法 Option，保存一次。 | 否 |
| `Offered` | HUD 提示待选数；由玩家主动开面板，不强夺战斗输入。不打开、关闭、重开、死亡均读同组。 | 否 |
| 本地卡片/替换草稿 | 预览卡片与 Q/W；返回、Esc、取消替换只清草稿，不改服务器 Offer。 | 否 |
| `RequestPending` | 最终点击提交一个 Option 及必要替换确认；显示等待，不能本地先改 rank 或减机会。关闭面板不声称撤回已受理意图。 | 否 |
| `Committed` | 同一复合提交应用效果、遗忘/装槽、来源失效、最终属性、机会消费和结果；之后才生成下一队首。 | 恰一次 |
| 拒绝 / 版本恢复 | 返回原因和同一个 Offer 的当前保护版本；重新明确确认，不换卡、不替换其他目标。 | 否 |
| `Frozen / InvalidMatch` | 终局只读冻结，离开失去提交资格；新局所有旧身份无执行权。 | 否 |

首个队首在奖励提交后同一调度链立即生成，不等“首次打开页面”才抽；
后续队首只在前一次选择已经应用后生成。连升九次也最多同时一份 Offer，
余下机会保留队列，不预抽九组再修补非法升级。

准备阶段可以对 prospective Build 枚举下一队首的合法池并预留空间，**不能提前生成、
公开或消费下一组随机结果**。实际抽样在本次状态应用后、整体 Publish 前完成；
这是固定目录、预验合法池和已预分配容器上的无失败纯操作。
任何准备失败不推进 RNG；已分配的队首随机材料不因恢复换 seed。
目录损坏不以重抽/属性替换修补：保留机会与原内容，明确完整性错误并走 §4 的有限恢复/故障路径。

### 6.2 满槽替换与版本保护

空槽学习采用 Q 后 W 的确定空槽顺序，UI 明示目标槽。满槽的新主动仍是一张合法卡，
“替换 Q”与“替换 W”是后续参数，不是两张不同候选。
最终确认必须绑定 Slot、旧技能稳定 ID、旧 rank、ExpectedBuildRevision 和 OfferRevision。
只选中新卡但尚未确定替换不消费；确认成功后旧技能/等级真实删除，新技能 rank 1。
旧技能冷却记录不等于备用技能库：再次获得仍从一级开始，仅继承未结束截止。

Offer 将**不可变内容**与**可更新保护版本**分开。
交易或又一次升级推进 BuildRevision 时，重验相同三张卡，更新 GuardBuildRevision/OfferRevision；
原 OptionId、效果引用及承诺 rank 不变。旧确认返回 `OfferGuardStale`，
不能把一次 stale 处理成重抽，也不能要求玩家永远提供生成时的旧 BuildRevision。
金币、HP、MP 或冷却自然流逝不重抽、不单独推进 OfferRevision。

当前候选池无装备/等级门槛，技能只有选择入口可学/升/忘，
且属性卡必须覆盖全部合法装备组合的完整增量。因此购物不应使固定卡语义失效；
若新目录引入这种失效，内容验证须拒绝该目录，不能以运行时悄悄换卡维护流程。
两个请求竞争同一 Offer 时，仅一个可提交；另一个返回原 Offer 已处理，
绝不把其 OptionId 套到下一组。

### 6.3 实际待审目录的候选覆盖

当前内容提案为三主动、三被动、五属性、七装备；只引用以下稳定键：

| 类别 | 本域合法性与效果引用 |
| --- | --- |
| 主动 `active_arc_bolt / active_guard_pulse / active_binding_snare` | 未学提供 rank 1；已学 rank 1/2 提供下一阶；rank 3 排除；满 Q/W 的新主动允许明确替换。效果为 `ABILITY.<id>.Rank[r]`。 |
| 被动 `passive_battle_rhythm / passive_second_wind / passive_precision` | 同样按学习/升阶/满阶处理；不检查独立被动槽容量。 |
| `CHOICE.attr_might / attr_focus / attr_vitality / attr_resolve / attr_reach` | 永久属性；resolve 的两种抗性是同一复合选项；reach 引用 `AttackRangeCmFlat`，仅改变普攻攻击距离，不自动扩大技能/余波范围。 |

按 `CHOICE.BaseWeight` 的统一正权重，在稳定排序的合法**语义定义**中无放回抽三项；
同一技能不能同时是学习与升级候选，同一属性不得换图标/OptionId 重复占两张。
不增加“必出主动”、类别保底、额外技能点、免费重抽或自动选择。

覆盖须以最终目录摘要重算，展开每次合法选项及满槽的两个替换去向；
状态必须包含已消费机会数，不能将遗忘前后的投入损失合并掉。
前九次选择中的任一生成点，至多已有八次投入；当前三级技能最多两个，
六种技能中至少四种仍能学习或升阶。若五属性在全部可达状态仍完整合法，则候选数量
足以产生三张互异卡。**属性前提必须证明，不能仅凭结构计数认定完成。**

内容验证需覆盖五属性各九次重复、所有零/一/两件装备的 **36 种多重集合**、等级成长、
三级被动、公式顺序和取整，拒绝“承诺增加但静默 cap 掉”或复合卡只应用一半。
AP 在尚无 AP 加成来源时可作为前置投资，UI 不应谎称它即时增强无 AP 加成的普攻。
旧四属性/六装备模型的候选最小值、转移数和极值不能作为当前新池通过证据。
这些覆盖证明也不代替选择持久性、来源历史、真实效果、网络或平衡验证。

## 7. 两装备、商店与来源冷却

### 7.1 钱物真相和买卖

最终装备实例存在于原 Ledger，而非另一个可写 `ALDItem` 持有层。
Inventory 从 Ledger 解析两个真实位置，Combat 引用实例/定义，视觉附件只负责表现。
任何物理类删除仍须 §11 的全部消费者和反射/资产门槛，不是本轮立即删除。

| 事务 | 同一次提交的全部后果 |
| --- | --- |
| Buy | 检查目录/明确展示价、余额、空槽；准备唯一 Instance、占用空槽、扣款、属性/被动/主动绑定、修订和回执。满槽 `EquipmentFull`，不自动卖/毁旧物。 |
| Sell | 按精确 Instance/ItemRevision/真实位置验证；计算批准卖价、销毁其权威持有、移除对应贡献/未释放来源、回款、保留冷却债并发布修订。 |
| Sell 后 Buy | 两次独立请求。Sell 成功后重读余额/槽位再发 Buy；后买失败保留真实卖出结果，不恢复已卖物或虚构新物。 |

账务分类只保留所需操作：击杀/开局初始化的明确 Mint，购买 Burn，出售退款的明确 Mint，
以及装备 Create/Consume；不引入商店现金、有限旧库存、银行、玩家交易、制作或掉落循环。
目录固定价和 `FORMULA.SellGold` 必须证明任意卖买循环无正金币收益，且每个精确实例最多售出一次。
开局初始化只在核心新 Match 中执行；复活、打开商店、读取 projection 不初始化钱包。

交易提交直接调用世界域的唯一正式签名：

```cpp
FLDShopEligibilityResult QueryShopEligibility(
    const FLDGameplayWritePermit& Permit,
    const FLDCombatBodyRef& BuyerBody,
    FName ShopId,
    const FLDWorldRevision& ExpectedWorldRevision);
```

Vendor从真实连接/AI意图绑定买家并取得当前核心BuyerBody，不接受调用者指定另一身体。
Permit只借用于当前Transactions调用栈，World先ValidateWritePermit(Transactions)，
再匹配身体/世界修订并只读查询合法脚点/支持面、DistanceXYCm、InsideInteractionRange。
采样取核心GetAuthorityNowSeconds及该真实子时刻的运动证据，
不能拿AtMs封桶上界当实际时刻或用较晚姿态回填。
几何成功不代替交易提交权：最终安装资源前Vendor仍重验Permit、原交易资格与全部预期版本。
只读回执重放不再调用资格查询或新交易，不保存旧Permit。
`SHOP.InteractRadiusCm` 是脚点到固定交互锚点的 XY 中心距，不增减胶囊半径。
FindApproach/曾经开店只提供接近能力，不构成提交期长期授权。

用户已确认商店不额外检查视线；查询不要求动态观察/旧 LDVision 直线：固定店公开，交互盘须属于同一合法可走
接近区，隔墙/分层口袋是布局验证错误。Vendor 不自行复制距离/LOS 算法，不加安静、
未受伤、准备站或“正在普攻就关店”规则。世界错误 DataNotReady、StaleContext、
StaleWorld、UnknownShop、InvalidGround、NotGrounded、WrongSurface、OutsideRange、
InvalidApproachRegion 要映射成明确反馈；失败不返回成功默认采样。

### 7.2 `IssuedCooldownId` 与防洗冷却

持久 CD 键使用共同 Match/Contender，加来源 Kind、技能或**装备定义**及 `IssuedCooldownId`；
不使用 Q/W/1/2 槽号或新 ItemInstance 作为完整冷却身份。
不同装备即使复用一个底层技能 handler，也不能误合并成同一来源定义。
角色技能每定义只有其原持久来源；不能借装备双轨为角色技能多发使用次数。

每个 owner、本局、装备定义最多两条已发行记录，各保存可选绑定 ItemInstance 和 ReadyAtMs。
到期记录仍留本局复用，不能删掉再造第三条；定义集合由批准目录有界。
**只有 Combat 写 ReadyAtMs**，其随发行/绑定计划进入同一复合提交，不由 Vendor 自行设置为 ready。

购买指定定义时，Combat 在 `PreparedSourceBindings` 中确定唯一回绑对象：

1. 未绑定且未结束的旧债优先；按剩余时间降序，同值按 IssuedCooldownId 稳定排序。
2. 没有未绑定债时，按 ID 顺序使用未绑定且已就绪的已发行记录。
3. 该定义从未发行过时创建第一条；若没有任何未绑定旧记录、当前第一条仍绑定在真正持有
   的装备上且此次确实形成同款双持，才允许首次发行第二条。绝不发行第三条。

客户端不能挑 ID、传 ReadyAt、借目标槽位决定回绑。最终准备重验持有、来源绑定修订、
当前 readiness 及债优先排序；失败不留下新 ID、绑定、扣款或实例。
购买预览/回执说明本次继承的截止，显示使用服务器时间，不通过重新购买承诺立即可用。

**必测反例：** 第一条已就绪且未绑定，第二条刚释放后售出且未到期；
买一件必须回绑第二条债，而不是按发行顺序挑第一条 ready。
两件全卖再买两件分别继承两条截止，不合并债务，也不把另一仍持有的 ready 来源连带锁住。
两个实际持有且就绪的同款实例可依正常动作时序连续合法释放两次，
各自消耗应有法力并推进自己的截止；独立 CD 不豁免目标、控制或资源条件。

出售精确解绑所售实例，保留原截止；换定义、遗忘、死亡均不缩短截止。
重学技能从一级开始但继承原期限；到期后自然可用不算洗 CD。
升阶、换装/冷却属性变化不重写已开始的期限，下次合法释放才按当时批准参数计算。
施法先提交则出售留下新债；出售先提交则旧实例/旧绑定的迟到施法失效，
不能因新实例绑定同 ID 而复活旧动作。

### 7.3 被动与攻击形态投影

基础属性逐实例相加，被动按内容稳定 `Passive.Family` / 被动定义键建立一次执行记录，
另保留贡献来源集合。两份减一份不重新注册触发、不重置内部冷却、不重复发一次性收益；
最后一份移除才移除持续贡献。若被动有未结束触发期限，按同一 Combat 来源合同保留必要债务。
不得把防主动洗 CD 的双轨扩展成同名被动触发两次。

`item_echo_maul` 的属性/普攻群攻贡献与 `CHOICE.attr_reach.AttackRangeCmFlat`
经同一最终投影到普攻几何和命中；同款锤属性相加、`passive_cleave` 一次。
余波副命中不吸血/不递归是当前**内容待审**语义，不扩大成全部技能的默认触发政策。
已成功释放的独立效果使用 Combat 释放快照并继续原定期限，普通死亡、出售装备或遗忘技能
均不取消，不再要求当前仍持有原 ItemInstance/技能；目标 Life、终局和正式离局取消仍检查。
维持型光环、装备被动和引导则按持续持有/施法条件解绑失效，不能与独立快照混用。
本域解绑不回滚已提交命中、伤害或奖励，不给资源退款，原 CD 截止照常继续。

装备 1/2 仅走 `FLDCombatSourceRef`、真实 Instance/Definition/ItemRevision、
InventoryRevision/BuildRevision/SourceBindingRevision、CatalogDigest 和 Combat 目标/资源检查。
**不带 ShopSession、报价、购买余额、商店距离或空槽条件。**
两装备已满、商店外、占领圈内必须仍可使用现有主动，释放不卸装备。

## 8. SC-09 命令、回执、读取与生命周期

### 8.1 正式窄入口

以下 `ContenderId` 参数只存在于服务器适配后的领域调用。真人 RPC 从拥有 Controller
解析，AI 从其服务器分配参赛记录解析；网络包不能指定另一账号，AI 不伪 RPC 或直接操作 Ledger。

```cpp
FLDProgressionView ALDDevelopmentDirector::ReadProgression(const FGuid& ContenderId) const;
FLDSubmitResult ALDDevelopmentDirector::QueueChoice(
    const FGuid& ContenderId, const FLDChoiceCommand& Command);
FLDChoiceReceiptQueryResult ALDDevelopmentDirector::QueryChoiceReceipt(
    const FGuid& ContenderId, const FLDReceiptQuery& Query) const;
FLDChoiceReceipt ALDDevelopmentDirector::CommitChoice(
    const FGuid& ContenderId, const FLDChoiceCommand& Command,
    const FLDGameplayWritePermit& Permit);

FLDEquipmentView ALDInventoryDirector::ReadEquipment(const FGuid& ContenderId) const;
FLDShopOpenResult ALDVendorDirector::OpenShop(
    const FGuid& ContenderId, const FLDShopOpenCommand& Command,
    const FLDGameplayWritePermit& Permit);
FLDSubmitResult ALDVendorDirector::QueueShopCommand(
    const FGuid& ContenderId, const FLDShopCommand& Command);
FLDShopReceiptQueryResult ALDVendorDirector::QueryShopReceipt(
    const FGuid& ContenderId, const FLDReceiptQuery& Query) const;
FLDShopReceipt ALDVendorDirector::CommitShopCommand(
    const FGuid& ContenderId, const FLDShopCommand& Command,
    const FLDGameplayWritePermit& Permit);
```

`Queue*` 只规范化、核验受理条件、固定条款并进入本批队列，返回
`AcceptedPending / Final / AdmissionRejected`；不是立即扣钱/成长的函数。
`Commit*` 为相应核心阶段内部入口，不直接暴露成 RPC。
Query 返回 `Pending / Final / NotFound / ReceiptExpired / InvalidContext`，不存在“查不到就再执行”。
OpenShop是Transactions阶段内部的开店处理器：网络/AI入口先把开店意图交现有权威队列，
等待阶段返回FLDShopOpenResult的Pending，不提前同步调用受Permit约束的世界查询。
复用当前交互绑定及取消机制，每名参赛者最多一个未办理开店意图，同一意图重复受理合并；
不另建商店请求流或第二套Pending队列。
核心分派时重新解析当前身体并借用当次Permit，OpenShop通过同一资格查询后才创建/复用
私有会话并返回Opened与合法报价，失败返回Rejected；处理器本身不伪造新的Pending或Permit。
不把Permit放进队列，不新增一套预览资格算法。
关闭/取消开店表现沿操作域现有交互规则，不把迟到开店结果强制弹回已关闭面板，
结果沿原交互绑定和owner发布修订拒旧，也不把开店成功当作买入成功。
`FLDReceiptQuery` 必须携带 `Generation / Family / RequestId`，查找键是
当前已验证 Match、服务器解析的 ContenderId、命令族和请求号。
先拒绝旧 Generation 再查询，不能由当前 Controller 替旧查询补新局上下文。
回执带原 Generation、Family、RequestId 和 OriginalLife；同局旧生命只读结果可查询，
但不可作为当前生命写入。新局 RequestId 重新从1开始也不会与旧局延迟查询串结果。
每个Query函数只接受自身对应Family，不匹配返回InvalidContext，不能返回另一族业务Receipt。

| 正式结果类型 | 必须包含的字段与语义 |
| --- | --- |
| `FLDSubmitResult` | Generation:uint32、Family、RequestId:uint64、上述AdmissionStatus、Reason、SequenceReserved:bool、可选FinalReceipt。AdmissionRejected不保留未来序号；AcceptedPending已固定条款但未成功；Final重放既有终态。 |
| `FLDShopOpenResult` | Generation:uint32、Life:uint32、ShopId、OpenStatus:uint8（Pending/Opened/Rejected）、Reason、可选FLDShopView；Pending由队列受理返回，Opened仅来自阶段成功，Rejected来自受理或阶段拒绝。只有Opened携带会话/报价，另两态View不存在。 |
| `FLDChoiceReceipt / FLDShopReceipt` 的共同头 | Generation:uint32、Family、RequestId:uint64、OriginalLife:uint32、OriginalLifePhase、Outcome（Applied/Rejected）、Reason、可选CommitOrdinal:uint64、结果版本集合。只读重放标志属于传输外壳，不改原业务结果。 |
| 结果版本集合 | Account/Progression/Build/Inventory/SourceBinding/Offer revisions均uint64，按存在位标明本次适用字段；Applied给提交后的版本，Rejected的当前guard只作同步提示、不宣称曾提交。 |
| `FLDChoiceReceipt` 载荷 | 原OfferId/OptionId、OpportunityOrdinal:uint8、消费后ConsumedThrough:uint8、实际新学/升阶/永久属性引用及替换旧技能/rank、可选下一OfferId；Rejected不伪造消费或下一卡。 |
| `FLDShopReceipt` 载荷 | Buy/Sell、精确InstanceId/IssueSerial:uint64/DefinitionId/SlotIndex、ActualGoldDelta:int64、可选LedgerSerial:uint64、来源绑定及继承IssuedCooldownId/ReadyAtMs；不是整个旧OwnerState副本。 |
| `FLDRewardReceipt` | DeathKey、冻结事实指纹（SHA-256原始32字节）、§4四态/Reason、可选LedgerSerial/CommitOrdinal、GoldDelta/XPDelta:int64、LevelBefore/After:uint8、新授机会区间和结果版本。Granted/NoReward是终态；暂失败无提交号，不伪造版本增长。 |
| `FLDChoiceReceiptQueryResult / FLDShopReceiptQueryResult` | 原FLDReceiptQuery（Generation:uint32、Family、RequestId:uint64）、LookupStatus、可选FLDPendingReceiptInfo、可选对应FLDChoiceReceipt或FLDShopReceipt；不是业务终态Receipt本身。 |
| `FLDPendingReceiptInfo` | 已受理请求的OriginalLife:uint32、OriginalLifePhase；没有业务Outcome/CommitOrdinal或成功载荷，不需要伪造原生命。 |

枚举由同一个本域协议声明供Controller/AI/UI/Combat引用，不各写一套数字表；
本表固定宽度，§8.2固定拒绝语义，真正实现的显式枚举编号纳入同一wire schema和拒绝测试。
新局清理请求执行域，查询依旧必须带原Generation；绝不由空参数或当前OwnerState补全身份。
查询包装结果的形状固定：LookupStatus=Pending时仅PendingInfo存在，Final时仅匹配原查询键的
业务Receipt存在；NotFound/ReceiptExpired/InvalidContext时二者均不存在，只回原查询键/状态。
这些结果不伪造OriginalLife、Applied/Rejected或物品/选择载荷，业务终态回执保持不可变。

| 命令 / 类型 | 必要输入与上限 | 提交、拒绝与回执 |
| --- | --- | --- |
| `FLDChoiceCommand` | Generation、当前参赛记录 Life、ExpectedLifePhase（Alive/Dead）、递增 RequestId；OfferId/OfferRevision、OptionId、ExpectedBuildRevision、CatalogDigest；满槽时 Slot 0/1、ExpectedOldSkillId/Rank、明确 ReplaceConfirmed。 | Transactions 阶段查当前成员/生命状态、队首及原卡/版本；成功回旧 Offer 的唯一结果及新状态修订。最多一个 Option，不收任意 Build、rank/属性数组或奖励金额。 |
| `FLDShopOpenCommand` | Generation/Life、公共ShopId、FLDWorldRevision ExpectedWorldRevision；请求不携Permit。 | 先进入权威队列，在Transactions借当次Permit重验存活/进行中及世界资格后给SessionId/QuoteRevision；失败不给默认商店或旧会话执行权。 |
| `FLDShopCommand::Buy` | Generation/Life/RequestId、ShopSession、QuoteRevision、CatalogDigest、ExpectedWorldRevision；ExpectedAccount/Inventory/Build/SourceBindingRevision；商品 DefinitionId、明确展示 BuyPrice。 | 当前会话、实际范围、价格、余额、容量、全部准备合法才 Create+扣款+来源一次提交；回 Instance/Slot/结果修订和继承 CD。一次只买一件。 |
| `FLDShopCommand::Sell` | 上述上下文与预期版本；精确 ItemInstance、ExpectedItemRevision、展示 SellPrice。 | 重查所有权/位置/卖价，Consume+回款+解绑一次提交；回具体已卖实例及修订。一次只卖一件。 |
| `FLDReceiptQuery` | 显式 Generation、Family、RequestId；拥有者由连接/AI适配解析，不接受别人的任意历史查询。 | 先拒旧局再查，只读；回原Generation/Family/RequestId/OriginalLife。本局历史生命与终局已有结果可查，但不覆盖当前身体/新局。 |
| 装备/角色施法 | 按 Combat 正式请求及当前来源版本。 | 不走本表 Shop/Choice 许可；1/2 与 Q/W 分别真实来源，失败不耗资源。 |

ID/枚举由当前批准 catalog 支持集解码；不是接受到历史最大枚举为止。
固定摘要长度、单命令单对象、合法槽/rank范围、非零身份、整数范围均在反序列化/受理入口验证。
客户端显示的价格须与当前服务器报价精确一致，过期不能悄悄按新价成交。
QuoteRevision 不因一毫秒流逝递增；时间投影不等于新报价。

选择的 Life 使用参赛记录当前生命代次；死亡界面无需伪造活 Pawn。
ExpectedLifePhase 只表达该次明确确认所见的生/死状态，不是新增生命计数：
死前 Alive 确认在死亡后即使 Life 尚未递增也拒绝；
死时重新确认允许受理，复活换 Life 后旧死亡确认仍拒旧。
生命阶段拒绝保留原卡，只刷新资格，不退款、不重抽。
使用核心 `ELDLifePhase` 的 Alive/Dead；Unspawned/Left 不可确认，
复活保护是 Alive 下的资格/期限，不另造本域生命计数。

### 8.2 错误与 UI / AI 恢复

| 错误族 | UI / AI 的合法恢复 |
| --- | --- |
| `OldMatch / OldLife / LifePhaseChanged / LeftMatch / MatchClosed` | 清旧身体意图/交易会话；读取当前获准状态或停止本局策略，不重试旧执行权。 |
| `OfferGuardStale / BuildStale / InventoryStale / AccountStale / SourceBindingsStale` | 读取当前状态；Offer 保留同 ID/内容，重新明确确认。不得自动修改替换目标或把后续卡当原卡。 |
| `UnknownOffer / InvalidOption / OfferAlreadyCommitted / ReplacementRequired / ReplacementMismatch` | 给出原结果或实际 Q/W，保留尚未消费机会；不自动代选/重抽。 |
| `QuoteStale / PriceChanged / ItemNotOwned / ItemStale / EquipmentFull / InsufficientGold` | 刷新自己报价/钱物；满槽先明确 Sell，等成功再另 Buy；不忙循环、不给补贴、不能直接改钱。 |
| 世界资格错误 / `Dead` | 没成交就零钱物变化；真导航到店或等复活后重开合法会话。受击本身不作此错误。 |
| `RequestConflict / OutOfOrder / ReceiptExpired` | 不重执行；核对原请求/序列、查询或同步当前状态，不能以超时猜成功。 |
| `NumericUnderflow / NumericDomain` | Combat 最终资源投影未提交；整次可选交易/选择拒绝，源/钱/机会/Current不变，等合法资源状态或修复数据后新请求，不能向上补1或靠换装杀死。必需奖励中的不可恢复数值错误走 §4 故障。 |
| `DataInvalid / ArithmeticOverflow / PrepareUnavailable / AuthorityFault` | 明确未提交和是否可恢复；按 §4 限制恢复，不能返回默认数据、长期 spinner 或静默丢选择。 |

错误详细信息只给有权的自身；不通过未知 Item/技能/账号错误泄露其他参赛者构筑或钱包。
内部断言和账本导出不发送客户端。

### 8.3 私有读模型与回包顺序

`FLDProgressionView` 提供当前生命/选择资格、Level/CumulativeXP、经验阈值的内容引用、
ProgressionRevision/BuildRevision、PendingChoiceCount、当前一组固定 Offer 与必要说明。
`FLDEquipmentView` 提供自身 Gold/AccountRevision、InventoryRevision、两个槽的
Instance/Definition/ItemRevision、自身来源与 IssuedCooldownId/ReadyAtMs、
SourceBindingRevision。`FLDShopView` 提供当前合法商品、价、说明、会话/报价版本和继承债说明。

View 最终组成同一次 `FLDOwnerState` 发布，附 Generation/Life/发布修订及各子域结果修订。
回执可以先于新快照到达：UI 可展示“已提交，等待状态同步”，不能自行扣款/造物补齐；
新快照先到则迟到回执只补结果提示，不回放旧快照。
重放的旧生命成功回执只能作为历史结果查询，不改当前生命资源/来源。
等待期间关闭面板不撤销已受理事实，重开查询真实结果；请求失败不保留假成功图标。

自身被动列表可展开/滚动，不新增玩法槽上限。Q/W 和 1/2 从真实输入映射显示按键，
两个同款主动分别显示自己的截止，选中别人不改变按钮所有者。
UI 手势由输入域捕获，不穿透发世界命令；打开、选择、购物都不暂停/授无敌。
GameState、全场击杀通知不获取本域私有钱物/Offer/来源 ID。

用户已允许**当前可见对手的装备展示**，不等于授权完整 Inventory。
本域提供 `FLDObservedEquipmentView`，只含两个 `Slot{SlotIndex, optional DefinitionId}`；
空槽明确为空，同款两件可各占一槽，名称/图标取公开内容定义。
`ALDInventoryDirector::ProjectObservedEquipment(ContenderId)` 仅供世界/视野发布内部调用，
世界必须先验证观察者的当前句柄/读取权限；UI/AI 不获得任意目标此函数。
DTO 不含 InstanceId、IssuedCooldownId、ItemRevision、InventoryRevision、BuildRevision、
Gold、XP、角色技能或 cooldown。观测外层 Generation/Life/ViewRevision、失视野撤销、
迟到拒绝沿世界唯一协议，不由本域再发隐藏期间增量。
序列化前在同一发布阶段重验 Match、目标 Life、观察 epoch；未知 catalog 明确报错，
不能伪造空装备。NPC 的外层 Equipment 状态为 `NotApplicable`，不调用参赛者查询、
不冒充两个空槽。静态装备 Tooltip 不推算对手最终属性或读取隐藏构筑。
用户允许的名称/等级/HP/MP/状态由各自合法投影提供；不顺带暴露其他属性。
AI 读取自身这些 View 和世界授权的对手装备外观、合法感知/公共圈，不扫描账本或全体 GameMode 表。

正常结算是另一条明确公共投影：核心公布全部本局参赛者名称、击杀、死亡、最终等级，
以及赢家、结束原因和用时。成长只提供冻结的 `Build.Level` 作为 FinalLevel；
K/D 由核心按唯一 DeathKey 归约，不从奖励回执再计一次，也不新增本域统计真相。
不得把整个 Progression/Equipment View 塞进结果，金币、XP、完整构筑和来源 CD 不公开，
统计不参与判胜。离局后的清理不能抹掉既成死亡/击杀或修改已冻结正常结果。

### 8.4 生命周期矩阵

| 对象/动作 | 取消或关窗 | 死亡/复活 | 普通离局 | 终局 | 新局 |
| --- | --- | --- | --- | --- | --- |
| XP/等级/永久属性/技能/金币/装备 | 不变 | 全部保留，不再初始化；复活投影同构筑 | 不转移给替补或新加入者 | 冻结 | 按批准初始输入重建 |
| 队列/固定 Offer | 只清本地草稿，Pending 查真实结果 | 原卡/机会保留；可用当前Dead阶段确认，只更新持久Build；旧阶段拒绝 | 不再提交 | 冻结，不继续选 | 清队列/Offer/结果域 |
| 来源/IssuedCooldownId/ReadyAtMs | 不因图标/草稿清除；售出/遗忘不取消独立已释放效果 | 时间继续；独立效果续原期限，新身体重绑保留来源 | 禁止新命令并取消尚未结束独立效果，不追溯改写既成事实 | 不再释放，展示只读 | 旧 ID/期限全部失效 |
| 商店会话/未提交买卖 | 关闭表现不声称回滚已受理交易 | 死亡失效；复活必须新 Life/会话 | 失效，未提交拒绝 | 新成交拒绝 | 旧报价/会话无效 |
| 致死到奖励 | 不是 UI 可取消意图 | 死者可收合法存续效果奖励 | 按实际致死时资格，后来离局不撤销；离局先则不再产生效果击杀 | 无新提交；必需未决不越过正常闸 | 旧 DeathKey 拒绝 |
| 已提交回执 | 可查，不重新 apply | 历史结果不写新身体 | 仅服务器获准诊断 | 可只读查既有本局结果 | 不写新局；旧投影拒绝 |
| 身体动作/当前血蓝/临时控制 | 归操作/Combat 取消合同 | 不在成长重置；生命域复活新 Life、满血蓝/清临时控制 | 核心移除资格 | 核心清理 | 新身体域 |

Host 离开由会话域结束，不执行普通离局后继续游戏路径；
没有断线重连、席位转 AI、Host 迁移或跨局战力恢复。

## 9. 内容参数、单位、取整与生成合同

数值只引用 `CONTENT_AND_BALANCE.md`，以下是消费者合同而非独立数值表。
结构上限来自 Spec；实际金额、秒数、距离、概率、增量、公式常量及待审效果不在本报告填默认值。

| 正式键 | 类型、单位和转换责任 | 本域消费者 / 必须验证 |
| --- | --- | --- |
| `XP.Start / XP.MaxLevel / XP.Threshold[L]`、`FORMULA.XPToNext` | 累计 XP 的有界整数；严格递增阈值，等级合法；运行 int64，算术检查后再存储。 | PrepareExperience；逐级跨阈值、十级 MAX、无十一阈值。 |
| `REWARD.PlayerXP / PlayerGold` | 受害者致死等级为自变量的整数奖励；只用不可变快照。 | CommitDeathReward；重复/互杀/阵亡来源、整奖越界。 |
| `CAMP.Low/High.RewardXP / RewardGold` | 每成员每 Life 的整数；不兼有一笔清营奖。 | 同一 DeathFact；世界只提供映射/定义，不另发资源。 |
| `CHOICE.MaxCount / BaseWeight / CHOICE.<id>` | 次数/正整数抽样权重；属性增量按内容量纲；复合项全量预检。 | 合法池、九次界限、无放回、五属性重复及36装备组合。 |
| `ABILITY.<id>.Rank[r]` | 三级正式效果、目标、法力/时间/距离、伤害与触发规则。 | Build/Combat；rank 演进、重新学习、未选 handler 删除依据。 |
| `ITEM.<id>.BuyPrice / Stats / Passive / Active` | 整数价格；属性单位沿 Combat；被动稳定键；每装备定义来源关系。 | 两实例属性相加、同名被动一次、独立来源、商店显示与实际能力。 |
| `SHOP.StartingGold / SellRatio`、`FORMULA.SellGold` | 比例按内容固定精度/精确有理值解释，乘法检查溢出，**仅最终卖价向下取整到整金币**；不拿 UI 浮点值结算。 | 初始化/卖出；非负、卖价不超买价、循环无正金币收益。 |
| `SHOP.InteractRadiusCm` | Unreal 厘米；脚点到锚点 XY 距离，世界单点查询；客户端不传可接受半径。 | Open/Buy/Sell 当前资格；边界内外、错误地面/分层。 |
| `CHOICE.attr_reach.AttackRangeCmFlat` / `ITEM.item_echo_maul` | 普攻距离厘米增量与获准普攻群攻内容；不外推技能/区域距离。 | 投影、输入接近/Combat 真实射程、余波去重/来源奖励。 |
| `POLICY.DuplicateEquipmentActive=IndependentSources` / `POLICY.MissingSourceCooldown=ContinueServerTime` | 已确认语义；ReadyAtMs 是本局绝对毫秒，不是可被 UI 重置的剩余时长。 | 两个 IssuedCooldownId、失源/重购/重学/死亡。 |
| `POLICY.DeadChoiceCommit`、`POLICY.ResourceMaxChange`、`FORMULA.ResourceMaxChange` | 死时确认和保持资源百分比已确认；精度/取整/边界由内容公式单点维护，不是运行时多模式开关。 | 选择准入、活体一次最终同比换算、循环无取整获益、死亡不治疗。 |

秒到毫秒使用生命周期的唯一转换合同，作者秒值必须能按批准精度准确表示；
不在每个 Vendor/Widget 分别四舍五入。Ready 判定由服务器当前时刻与截止比较，
UI 到零不授权。属性和派生数值的计算类型、上下界、截断与来源快照均沿 Combat 的单一公式。
金钱、XP、构筑、来源与显示格式化不能相互代算。

作者域和生成终态以 `CONTENT_AND_BALANCE.md` §10.1/§11.2 为唯一口径：
内容获批准后，角色/成长、技能、装备、奖励/价格及政策写入 `SourceData\Demo\content.json`，
由同域严格 `content.schema.json` 和稳定 `native_ids.json` 约束，不继续维护旧V08记录为第二作者源。
唯一Demo Sync/compiler同步生成 `Content\Lockdown\Data\Demo\demo_content.json`、
同目录的 `manifest.json`，以及生成的原生 `LDDemoContentContract.h`；新 `ULDDemoContentCatalog`
统一不可变内容定义，沿原生类型/DataAsset/Bootstrap Validate链替换旧目录入口，
不拥有另一份角色、金币、HP或运行状态。地图坐标仍归世界作者域，本内容仅引用稳定锚点及世界合同。
这些是待实施目标，不是已经存在的目录/命令；未知/缺失ID、非法值、无处理器/图标/触发定义必须拒绝。
数值reader按内容合同保留Decimal/原词法，先检查精度、有限性、范围与量纲，再转规范
定点原生树交encode；不能先转float使非法精度塌缩或大数变inf，encode也不直接接Decimal。
这是共享输入管线的改造前提，不在本域另写校验器或复制数值输入表。
不直接修改生成摘要，不整库替换 V07/V08/Bxx 或产品版本号；游戏不读取 docs。
数据裁剪前要先取得批准定义到 handler/来源/序列化/UI/资产/用例的完整保留集合。

## 10. 已确认的死亡选择与资源同比规则

| 项目 | 用户已确认的行为 | 不可越过的边界 |
| --- | --- | --- |
| 死亡等待中的确认 | `POLICY.DeadChoiceCommit=Allowed`：允许查看、选卡和明确替换，利用等待整理构筑。 | 只改变持久成长，不伪造活 Pawn，不立即治疗/施法/复活。当前 Life+ExpectedLifePhase 重新确认，旧生命/旧阶段拒绝；交易始终要求存活。 |
| 最大 HP/MP 改变当前值 | 用户原文“保持原来的生命／法力百分比”；引用 `POLICY.ResourceMaxChange` 与 `FORMULA.ResourceMaxChange`。 | 升级、属性卡、装备变化均按旧当前值/旧上限和最终新上限一次换算，不逐来源反复移除/添加；不是保持旧绝对当前值。 |

同比换算由 Combat 的 PrepareBuildChange 准备、ApplyBuildChange 一次应用；
经济/成长不另写 HP/MP 算法，UI/AI 不反向代算。死亡时选择只改持久 Build，
不 Apply 活体 Current；生命域复活以最新构筑满血蓝。
内容的当前正式提案采用 `POLICY.ResourceMaxChange=PreserveRatio`：
权威资源为 int64 百分之一单位 P2，计算旧 Current 与最终新 Max 的 checked 乘除，
仅最终向下取整到 P2。此精度属于待审内容公式，不是用户要求精确到某一量化格。
正活 HP 缩小上限若量化为零，Combat Prepare 返回 `NumericUnderflow`，整次变更不发生；
不以 `min 1` 免费抬升或投影杀死；MP 向下量化到零合法，不形成死亡。
旧 Max、新 Max、Current 全为零时保持零；旧 Max 为零而新 Max 为正的普通转换返回
`NumericDomain`，不猜一个百分比。当前玩家目录的 MaxMP 始终为正，此域拒绝不阻塞常规成长。
正常初始化/复活由生命域显式满恢复，不走旧零分母转换。
完整买卖往返不能从量化获得额外百分比，不能通过临时逐件卸装多次取整制造额外损失；
真实伤害、消耗、回复仍按 Combat 唯一规则改变资源，不能拿旧快照抹掉两次交易间的伤害。
首版目录没有强制到期的临时 Max 增益，无 MP 野怪固定零且不参与装备/成长转换。
本拒绝策略仅覆盖当前可拒绝的成长/买卖准备，不能用 `NumericUnderflow` 保留本应到期
或被强制撤销的来源；这类最大值变化不在当前目录支持集，不能静默接入该分支。

保护解除也沿最新已确认 Combat 合同：通过局/生命/调用权限过滤的进攻意图即解除保护，
目标、距离或蓝量校验随后失败也不恢复保护；旧局/旧生命/未授权包先拒绝。
装备1/2采用同规则，选择/购物本身不是进攻意图。失败施法仍不扣蓝或启动技能冷却。

上述行为不再作为待选方案；具体技能/装备/价格/阈值/公式及射程/余波内容继续由内容报告
单点供 PR 审查。工程类型、债优先排序、版本保护、Permit 和有限故障收口不转成用户逐字段选择。

## 11. 新增、复用、改造与精确删除接线

这里细化本域消费者和替代前提，不复制架构整章。
**删除是后续依赖关闭后的真实删除，不是本次执行，也不是放到 Legacy/Backup 或空壳。**
共享核心、地图和反射资产由对应负责人协调。

| 架构映射 / 处置 | 具体落点与真实消费者 | 先替代什么，再删除什么 |
| --- | --- | --- |
| R08 局部原样机制 + 改造，D03 | `Economy\LDEconomyLedger.*` 的条款比较、checked arithmetic、预期修订、Receipt/Reconcile；`LDEconomyDirector.*` | 先 Prepare/Apply、单钱物真相及有界前缀复算；再删 BankTransaction、预算/endowment、quality/transient/parents 等无消费者字段和事后 `Record/GoldChanged/Snapshot` 常规写链。不是整删 Ledger。 |
| N03 新增，D07/D08 旧奖励删除 | `World\LDWorldDirector::Contact/Reward/PvPKill`、`Framework\LDGameMode::AwardExperience/RecordEconomyGold`；Combat `Sample/Die` | 先接共同 DeathFact、不可变致死等级和 CommitDeathReward；再删 LastOwner fallback、AttributedXP、旧 PvP 配对/总额限额、额外探索/任务/机构 XP；不留任意授予入口绕过复合提交。 |
| N04 新增 + D15 改造 | `Development\LDCharacterBuild.* / LDDevelopmentDirector.* / LDV08Loadout.*`，`FLDBuildPlan / FLDOwnedBook / ALDBook` | 先窄 Choice、持久队列/Offer、角色技能/永久属性 patch；再删职业六维/点数/开局技能、读书/准备/Loadout hold、DropBooks、整份草稿与备用布局。必要来源/修订/投影局部抽取。 |
| R09/N05，D15 库存改造 | `Inventory\LDInventoryTypes.* / LDInventoryDirector.* / LDGearTypes.h / LDGearRules.cpp`；Actor、owner Items、Bindings/Quick | 先 Ledger 唯一 Instance/Owner/Slot/ItemRevision、两槽 DTO和 Combat 来源；再删背包/重量/Quick/十一绑定/双手占位/掉落拾取整理/物理 Holder 镜像。不能只缩小 HUD。 |
| N05 + D16 Vendor | `LDVendorDirector.* / LDVendorTypes.h`、`LDPlayerVendor.cpp`、InventoryPanel 的书店页 | 先新会话/报价/Buy/Sell/回执、世界资格、no-fail 来源绑定；再删 Skill/BookPrice、OfferedSkills、ServerVendorBuy 旧形状和受伤/安静/旧 Ray 附加条件。 |
| R07/R09 局部抽取，D14/D15 | `LDV08Loadout::Validate/ItemSource/InvalidateSources/Project`、`LDContentActions/LDContentRelease/LDV08Modifiers` | 先实例→IssuedCooldownId、SourceBindingRevision、被动来源集合；再删重复 family 禁止、ItemFamilies 共享主动 CD、施法伪库存修订和无关 modifier。按 C1 保留获准射程/余波 handler，不留整套旧效果。 |
| B02/D04 玩家交易整链 | `LDTradeDirector.* / LDTradeTypes.h / LDPlayerTrade.cpp / LDInventoryTradePanel.cpp`；Controller TradeView/Pending、GameMode Trade、Combat/Navigation/Development/Vendor 的取消链 | 先把最终容量、精确版本、条款幂等与隐私断言抽回 Ledger/Vendor/Inventory；再断所有 RPC/成员/Delegate/CancelFor/Pending，删除类型、页签和专测。无须等待新价格，但须适用 C3。 |
| D05 Acquisition 整链 | `LDAcquisitionDirector/Catalog`、`LDSupplyRequest`、`LDLoadoutRequest`、生产/采购 RPC | 先抽创建失败前置与先账后投影，将必需ID/投影校验迁入唯一Demo compiler；先断SyncB84对Acquisition adapter的源码读取，再随旧生成链退出。删除书籍/材料/制作/品质/旧请求和缓存报价，不为compiler留空旧文件。 |
| D02/D06 其他经济消费者 | Bank/Commerce/Production/Institution、B92 钱包/Supply/Work/Courier/Extraction/WorldLedger；GameMode 服务接线 | 目标奖励/买卖已回同一 Ledger 后，按架构闭包断消费并删除；不给 AI 保留第二经济栈。各自有效事务断言先迁移。 |
| D16/N07 UI、RPC 与投影 | `LDPlayerController.h`、`LDPlayerDevelopment/Inventory/Vendor.cpp`、`LDDevelopmentPanel/LDInventoryPanel/LDV08HUDModel.cpp`、`LDViewTypes.h` | 先接 §8 的窄 DTO/请求/错误与同次发布；再删整份 Build 草稿、书籍/六动作/四附件/旧服务页、wire/Delegate/Pending 及旧控制映射。不留不可执行空按钮。 |

**关键实际消费证据：** [LDPlayerController.h:87–121](../../Source/Lockdown/Public/Framework/LDPlayerController.h#L87-L121)、
[LDPlayerDevelopment.cpp:25–67](../../Source/Lockdown/Private/Framework/LDPlayerDevelopment.cpp#L25-L67)、
[LDPlayerInventory.cpp:22–64](../../Source/Lockdown/Private/Framework/LDPlayerInventory.cpp#L22-L64)、
[LDDevelopmentPanel.cpp:98–179](../../Source/Lockdown/Private/UI/LDDevelopmentPanel.cpp#L98-L179)、
[LDInventoryPanel.cpp:116–150](../../Source/Lockdown/Private/UI/LDInventoryPanel.cpp#L116-L150)、
[LDV08HUDModel.cpp:11–35](../../Source/Lockdown/Private/UI/LDV08HUDModel.cpp#L11-L35)。

`ALDItem/ALDBook` 还有 Economy、B92、Combat、World/Tools/Forestry/Checkpoint、
Vision、Objective、Population 及 probe 消费，不能仅在 Inventory 内无引用就删类。
沿 D02/D05/D06/D08/D11/D12 先迁存活消费者；失去用途的 Actor 再连同反射/资产依赖真实删除。

### 11.1 数据、adapter、生成与资产退出顺序

| 当前链 / 证据 | 必须一同迁移的边 |
| --- | --- |
| [SyncB84Data.py:32–103](../../Scripts/SyncB84Data.py#L32-L103) | 旧链读取ELDSkill/ELDItemKind、`LDEquipmentCatalog.cpp` adapter，**还读 `LDAcquisitionCatalog.cpp`**；固定旧数量/starter kits，输出combat/equipment/acquisition_b84、Manifest、LDB84Contract。仅抽所需确定性编码/ID/哈希校验机制到Demo compiler；替换消费者与输出合同后删除旧SyncB84及B84产物，不将它作为终态入口。 |
| [LDEquipmentCatalog.cpp:15–54](../../Source/Lockdown/Private/Inventory/LDEquipmentCatalog.cpp#L15-L54)、[LDAcquisitionCatalog.cpp:20–39](../../Source/Lockdown/Private/Inventory/LDAcquisitionCatalog.cpp#L20-L39) | 必要导入/查询能力迁到ULDDemoContentCatalog；旧目录类/loader及按摘要选择RulesVersion、V07 fallback随消费者切换退出。缺新输入明确失败，不回读旧目录。 |
| [LDSupplyRequest.cpp:2–6](../../Source/Lockdown/Private/Inventory/LDSupplyRequest.cpp#L2-L6)、[LDPlayerProduction.cpp:37–40](../../Source/Lockdown/Private/Framework/LDPlayerProduction.cpp#L37-L40) | 旧 serializer 以历史枚举上界截断，不可用于新商店请求。替换生产者/decoder/白名单/测试后删旧供给请求。保留存活技术编号，不机械重编号。 |
| [LDScaffoldBootstrapCommandlet.cpp:240–270](../../Source/LockdownEditor/Private/ScaffoldBootstrap/LDScaffoldBootstrapCommandlet.cpp#L240-L270) | CharacterCatalog 与 V07 Combat/Equipment/Acquisition/World 无条件资产加载/生成边要改，不因前端不再传旧 profile 就盲删载荷/资产。 |
| [DefaultGame.ini](../../Config/DefaultGame.ini)、[Lockdown.Build.cs:10–22](../../Source/Lockdown/Lockdown.Build.cs#L10-L22)、[PackageWin64.ps1:7–13](../../Scripts/PackageWin64.ps1#L7-L13) | 去旧容量/BookPrice、广域 AlwaysCook、显式旧载荷和多图 staging；只保留当前实际消费者，确认有效配置不会回落引擎旧默认。 |

对应D20/D21/D23/D32及内容CB-D01–04：`SourceData\Catalogs\V08` 等旧记录只用于抽取/迁移审计，
必要稳定编号和获审定义迁入§9的唯一Demo作者域；不能在V08中继续编辑一套“存活记录”并与新域并行生成。
新Demo compiler/catalog、typed投影、精度/拒绝夹具及DataAsset/载荷/root manifest/contract先就绪，
再切换运行、Bootstrap和staging/cook消费者，删除旧V08/B84作者输入、载荷/manifest/contract、
旧catalog/importer、SyncB84及其旧数量/schema专测。旧机制可抽取，旧生成器不是最终受支持入口。
保留有依据的稳定技术编号，不以保留编号为理由保留第二作者源或备用旧产物。
V07 删除仍需关闭 Bootstrap、loader fallback、Sync/native contract、staging/cook 四条边。
本轮不修改 SourceData 字节、不运行生成器、不手改任何摘要或二进制。

反射类、WBP/CDO/DataAsset/map、动态加载和 `.uasset/.umap` 删除须 C3：
以当前入口/角色/装备/技能/root widget 为根检查 AssetRegistry 硬/软/管理引用，
另核对 C++/配置拼接加载；旧类可加载时先迁移、编译、重存存活资产。
随后 UHT、Editor/Game 构建、Bootstrap Validate、Cook 和新包 staged/AssetRegistry 核验。
**文本零引用不证明二进制可安全删除。**

### 11.2 测试与文档迁移

| 当前入口 | 保留/改造与删除 |
| --- | --- |
| [LDEconomyRulesTests.cpp:19–77](../../Source/Lockdown/Private/Tests/LDEconomyRulesTests.cpp#L19-L77) | 保留精确条款、原子性、overflow、复算；迁到 Prepare/Apply及奖励关联，删银行/预算/制作政策夹具；同步窄字段白名单。 |
| [LDInventoryRulesTests.cpp:9–16](../../Source/Lockdown/Private/Tests/LDInventoryRulesTests.cpp#L9-L16)、[LDV08LoadoutTests.cpp:25–60](../../Source/Lockdown/Private/Tests/LDV08LoadoutTests.cpp#L25-L60) | 保留来源/旧修订/wire/引用失效；替换背包/Quick/多绑定/六动作/四附件、旧 family 共享与旧属性预算断言。 |
| [LDVendorProbe.cpp:39–130](../../Source/Lockdown/Private/Tests/LDVendorProbe.cpp#L39-L130) | 将快递买书读书改成普攻打野→完整奖励→选择→装备；保留过期/创建失败/满容量/越界目的，删除受伤必须关店等旧玩法断言。 |
| `RunInventoryCheck.ps1 / RunVendorCheck.ps1 / RunLoadoutCheck.ps1` | 迁真实参赛 Listen Host 和拥有客户端、Demo 两装备/选择/1/2；旧 Server+A+B/LDRemoteAuthority 不是目标联机结果。迁移前不声称已有 Demo 参数。 |
| [RunTests.ps1:19–21](../../Scripts/RunTests.ps1#L19-L21)、[CheckEconomyEvidence.py:15–66](../../Scripts/CheckEconomyEvidence.py#L15-L66) | 明确实际必需用例集合，删旧数量/TradePrivacy 强制项但保留 failed/notRun/inProcess/缺报告失败；复算新增钱物/奖励/前缀尾部，删无消费者 budget/quality/parents。 |
| D28–D34 的旧专测/runner/手册 | 必要 Ledger/Vendor/Inventory 不变量先迁，再真实删 Trade/Acquisition/Bank/Production 旧专测、probe、selector 和说明；同步六系统手册对应真实接口，不保存废弃方案。 |

本域建议新增的规则测试职责落在现有 Tests 体系：死亡复合奖励、Offer/替换、两装备事务、
IssuedCooldownId、Permit/fence/故障恢复与 owner 投影。不要求新建测试框架，
也不保留旧领域只是为了旧测试可编译。I4 与相关 I3/I5 接线后，按最小覆盖逐步升级到实际场景。

## 12. 验收设计与审核结论

以下是后续迁移/新增用例，不冒充已经存在的测试名称。
除明确的合成规则夹具外，实际金额、时间、边界与容差取批准内容参数。

| 用例 | 前置、动作与精确断言 |
| --- | --- |
| A01 从零发育 | 正常菜单开局，仅近战单体普攻；真实击杀低档野怪得到一次 Gold+XP，完成首次选择和真实购买，无调试授予/传送。 |
| A02 最后一击/归属 | 两来源命中同一目标，分别测主动、被动/持续和普攻副命中；只有真实致死所有者收全部奖励。无主不能回落上次攻击者。 |
| A03 死者/互杀/离开 | 同Effects集合双死先锁完整致死等级/资格再奖；后续因果集合用当时新等级。先致死后离局/重试仍沿原资格，离局先则效果取消；同ceil桶真实先后不混，清理不产击杀。 |
| A04 DeathKey 去重 | 重放相同事实，继而改变 Source/Effect/DeathSerial/BodyIdentity；最多一次结果，改条款报冲突不改收款人；合法 NoReward 也不能改源重领。 |
| A05 营地生命周期 | 每成员每Life一次；回位途中可真实致死并按旧Life结一次，到家仅存活者Reset满资源/清临时/CD就绪，不复活死者、不增Life/发奖；刷新不二次奖励，旧Life不命中新生体。 |
| A06 奖励边界 | 每级阈值前/恰到/越过、一次多级至10、延迟九次；机会逐级准确、无第十次。满10继续XP提案按内容批准结果验证。 |
| A07 整奖失败 | 分别注入账本、成长、Combat、Offer工作区、回执/outbox准备失败及整数越界；所有钱/XP/等级/机会/RNG/修订不变，无半奖或 NoReward fallback。 |
| A08 水位/故障 | 必需奖励未决时正常 Capture 不推进；同事实有限恢复，失败经核心明确故障关闭且无正常胜者；空批 fence 正确，生产者前缀无缺口。 |
| A09 Permit | 错 Match/Batch/Phase/GateRevision、跨 callback/下一批旧许可在第一次 mutation 前拒绝；子 Apply 不会半途正常失败。 |
| A10 持久三卡 | 连升/关闭重开/死亡/重复查询不改 OfferId/OptionId/内容/RNG；只有本次选择应用后才生成下一组；不打开窗口也已固定队首。 |
| A11 合法池 | 对当前3/3/5池真实可达生成状态及36装备多重集合验证；满 Q/W、满rank、所有替换分支、属性复选、复合增量全可兑现且三卡互异。 |
| A12 替换遗忘 | Q为三级旧技能、W占用；只点新卡/取消不消费，明确替换后旧rank遗忘，再学一级但旧截止保留；旧确认不能替另一技能。 |
| A13 选择竞争 | 两请求同Offer、选择与买卖/升级竞争；最多一次消费；stale返回同卡及新保护，不用旧整Build覆盖新CD。 |
| A14 生死选择 | 死前Alive确认不能借同Life在Dead应用，死时新确认允许且不需要活Pawn、不治疗尸体，复活旧确认拒绝，机会不丢。 |
| A15 两装备/真实例 | 两次显式购买同款产生两个实例，第三件拒绝；满槽先Sell后Buy，后Buy失败不回滚真实出售；无隐藏背包。 |
| A16 原子买卖/新修订 | 钱不足、旧价/旧库存/旧实例、创建/投影/绑定失败均无扣钱/物品/CD席泄漏；统一 ItemRevision 不混用旧物理与Ledger计数。 |
| A17 商店同刻 | 开店/买卖仅Transactions借当次Permit调用正式世界查询，拒跨栈/旧许可；同ceil桶内不同真实子时刻用对应运动证据。移出范围/死亡/复活/终局/离局均重验，几何通过后最终安装仍复验，无迟到成交；受击本身不禁止。 |
| A18 双属性/单被动 | 同款两实例属性相加、同名被动一次；卖一份不断另一份、不重置触发，卖最后一份移除贡献；装备被动不消耗成长选择。 |
| A19 独立双主动 | 两就绪同款在店外/圈内、满两槽时分别合法使用，可连续两次；各自截止不同，不存在family共享锁，不忽略法力/控制/目标。 |
| A20 回绑反例 | ready旧轨与未结债并存先取债；长短债优先最长，平局稳定；售双重买两债分别继承；反复卖买不出第三ID，失败不发行。 |
| A21 CD生命周期 | 换槽/升级/遗忘重学/售出重购/换定义返回/死亡保持截止，一条不改另一条；已释放独立效果仍续原期限且不再查原持有，维持型按条件取消，正式离局取消独立效果。 |
| A22 当前血蓝 | 买/卖/升阶/原子替换后保持原资源百分比，仅按最终投影一次；覆盖往返、精度、零上限/最小正值及真实伤害夹在两次交易之间，不能量化套利或治疗尸体；正常复活满新上限。 |
| A23 射程与群攻 | attr_reach与echo_maul在服务端真实改变普攻范围/获准副命中；不扩大未指定技能距离；双锤不双余波，副命中奖励仍恰一次。 |
| A24 回包/过窗 | 同ID同条款重放、异条款拒绝、64窗外拒旧+同步；先快照后回执/先回执后快照均不回滚当前状态；双击Pending不创建第二业务。 |
| A24b 压缩/退休/查询 | 先折叠64条再提交65/66；退休有独立serial和delta，多轮压缩后余额/Retired/修订/关联戳可复算，旧请求/实例/死亡拒旧、pin不释放。旧局同号query=InvalidContext；各LookupStatus的包装结果不伪造终态Receipt。 |
| A25 终局/新局/隐私 | 关闸后无新奖励/选择/买卖/来源，只读旧回执；新局清局内域/旧ID。可见对手装备定义失视野撤销；结果只含批准名称/K/D/FinalLevel/赢家/原因/用时，无私有Instance/修订/技能/XP/金币/Offer/CD。 |
| A26 删除与生成闭包 | 批准定义→schema/adapter/loader/handler/UI/资产/测试一致；旧RPC/服务/载荷/专测无存活消费者，保留稳定ID有依据；UHT/build/Bootstrap/Cook/包清单无悬空引用。 |

网络、成长、交易和胜负至少使用**正常参赛且可见的 Listen Host + 真实拥有客户端**，
最终整合覆盖 Host + 两拥有客户端 + 三 AI；注入延迟/抖动/重复/乱序时同时核对服务端回执
与客户端投影。直接调用 `_Implementation`、Host 全知调试或旧无渲染 probe 不能替代这些证据。
LAN 与正确配置的公网直连按集成计划分别验证，输出写忽略的 Artifacts。

### 专家身份、依据与本次边界

| 独立角色 | 实际 agent 身份 | 结论与证据范围 |
| --- | --- | --- |
| RPG/肉鸽/经济设计 `rpg-economy-designer` | `4e69c842-6510-4107-afca-caecf1a484ef` | 独立完成有限选择、持久候选/替换、经济取舍与双轨研究；最终报告服从用户已确认的死亡选择和资源同比规则。未审计源码、未复跑内容模型或游戏。 |
| UE权威事务/数据工程 `ue-transaction-auditor` | `1f0538aa-051e-4bdd-b8ea-fd2dddba696c` | 静态核验真实写路径/修订/消费者/裁剪；确认必须改造先改钱物后补账及XP拆分，提出致死等级快照、Prepare/Apply和有限故障收口。 |
| 独立事务/生命周期 QA `progression-transaction-qa` | `49aff7d7-6acb-4a57-824f-a777a46eee26` | 完整审查与定点复核完成，**通过本模块文档设计独立QA，无剩余必修**；确认压缩/退休可复算、完整型别/查询包装、生命周期/双轨、商店阶段及唯一作者域/生成迁移边界。未兼任研究作者或修改报告。 |

工程专家另执行了**合成输入的纯内存协议模型**：复合准备/重放/Offer保护等65条断言，
双轨发行/绑定/截止43个状态、229条断言；不代表 UE 实现或新内容3/3/5/7池已验证。
独立QA使用自己的模型：选择结构249,184个状态/1,251,972条转移，双CD1,168个状态/
4,756条转移/8,620条断言，P2共137,216组三元输入；最终退休/压缩/查询复核140次提交、
3次压缩、1,737条断言通过。候选结构结论以五属性均完整合法为前提；
36装备集合只是覆盖集合，**实际内容数值/帽值未由本工作区模型独立证明**。
最终契约已纳入独立提交号、AccountRetire重放、明确scalar/编码及跨局查询包装，
不保留以缓存长度编号、直接补退休标志或伪造终态查询的实现路径。
玩法专家的一手参考为
[Riot Games：Gizmos & Gadgets Learnings](https://teamfighttactics.leagueoflegends.com/en-us/news/dev/dev-teamfight-tactics-gizmos-gadgets-learnings/)，
仅用于区分“组合规则合法”与“选择真正有取舍”，不照搬其内容/数值。

本次没有实现接口、改代码/配置/数据/资产，没有运行 Unreal、原生构建/UHT、生成器、
Bootstrap、Cook、真实拥有连接或游戏平衡验证。静态研究/模型不能升级为这些通过声明。
本域已确认的玩法选择不再有未决项；仍需跨报告集成审核、首版内容/公式审批、
实际生成一致性与数值覆盖证明，以及实施后的原生/资产/网络验证。
不因模块文档通过独立QA就宣称完整 Demo ready。

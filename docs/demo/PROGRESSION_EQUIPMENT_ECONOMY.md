# Demo 成长、装备与经济

**目标：v0.10.0 · 架构职责：M6 / M7 / M8 · Unreal Engine 5.8.4**

本报告将 [App Spec](../APP_SPEC.md) §4、§5、§7–11 接到
[架构 R08/R09、N03/N04/N05](ARCHITECTURE.md) 和
[共享契约 SC-01–09](SHARED_CONTRACTS.md)。实施依赖见
[实施计划 I4](IMPLEMENTATION_PLAN.md)，当前组件知识见
[战斗与成长](../systems/COMBAT_AND_GROWTH.md)、
[库存与事务](../systems/INVENTORY_AND_TRANSACTIONS.md)。

本文是该模块的设计与实现合同，实现落点见 §2；类型与函数名以当前源码为准，
§12 是验收要求，不是已通过的测试清单。
数值、公式和首版目录只由 [内容与数值](CONTENT_AND_BALANCE.md) 维护；本文引用其正式键，
不建立第二份价格、经验、效果或平衡表。跨域身份、时钟、批次和终局由
[会话与对局生命周期](SESSION_MATCH_LIFECYCLE.md) 单点定义，战斗投影由
[战斗与属性](COMBAT_AND_ATTRIBUTES.md) 定义，世界查询由 [世界、营地与视野](WORLD_CAMPS_AND_VISION.md) 定义。

## 1. 结论与确认规则

**一个账本、一份参赛者成长、一条 Combat 来源链。**
Ledger 只保存金币与装备实例（精确条款、预期修订、整数检查和复算）；XP、等级与机会由
Development 持有。死亡奖励、选择和买卖各自在同一权威批次内复合提交：
先准备 Ledger、成长与 Combat 投影，全部成功才一起应用，不先改 OwnerState 或物品 Actor 再补账。

| 已确认的产品约束 | 本域必须落实的结果 |
| --- | --- |
| 所有人初始近战、单体普攻；群攻和攻击距离可经装备或属性成长获得 | 初始构筑不授主动；获准的攻击距离、普攻群攻修正进入同一 Combat 投影，不是只有面板数值。 |
| 1 至 10 级，开局一次、每升级一次三选一，最多十次 | 开局机会只在新 Match 授予一次，复活不再授予；累计 XP、等级、已获机会同一提交；连升排队，满级仍能处理已有机会。 |
| 三张不同且合法的卡；技能两档（初级、高级） | 未学技能仅提供初级学习，已学初级仅提供高级，高级排除；属性卡不可冒充三个相同选项。 |
| Q/W 两角色主动，满槽需确认替换 | 未确定替换不消费；遗忘旧技能及等级，不退款、不保留备用库；新主动从初级开始。被动无独立槽上限。 |
| 两装备、无背包、允许同款双持，满槽先卖后买 | 容量由权威物品位置限制，不由 UI 格子限制；售出和随后买入是两个独立事务。 |
| 同款基础属性相加，同名被动只生效一次 | 属性逐实例汇总；被动按稳定内容键去重，不按本地化名字或实例数触发。 |
| 装备没有主动技能 | 装备只提供固定属性与被动，不发行冷却记录。 |
| 失去来源期间继续计时，重持有沿用原结束时间 | 遗忘、换槽、死亡不缩短已开始的 `ReadyAtMs`；重学承接原记录的截止时刻。 |
| 已释放独立效果不因普通死亡、售出或遗忘而取消 | 继续原定期限；依赖持续持有/施法的光环、被动、引导按维持条件失效。正式离局取消未结束独立效果，但不追溯改写既成死亡事实。 |
| 野怪的 XP 和金币归最后一击实际所有者；参赛者击杀另有助攻（#112） | 合法击杀一次结清：参赛者受害者的击杀者全额金币，助攻者平分 50% 基础击杀金币，XP 由击杀者与助攻者平分（App Spec §4.1）；阵亡但仍在本局的来源可以收奖或得助攻。无主、自杀、回位、离开不产生额外收益。 |
| 死亡不丢成长、金币、装备或待选；新局清局内状态 | 持久状态不放在 Pawn / Widget；死亡取消身体期请求，不重发初始资源。 |
| 死亡等待可以确认选择，只更新保留构筑 | 不提前复活、不治疗尸体；当前 Life 和生/死确认阶段重新校验。 |
| 最大 HP/MP 改变时保持原生命/法力百分比 | 活体只按一次最终属性投影同比换算；不采用保持原绝对当前值的规则，精度由内容/Combat 单点定义。 |
| 商店不安全，本体不可攻击 | 买卖重查存活、比赛阶段和实际范围；受伤本身不附加脱战、安静或无敌政策。 |

以上双持、叠加与失源继续计时已获用户确认，不保留共享冷却或冻结计时备选。
内容与数值以 2026-11-02 批准的首轮方案（[内容与数值](CONTENT_AND_BALANCE.md)）为准；
资源同比换算的精度与边界只由内容/Combat 的唯一公式定义。批准输入与规则测试都不代表真实联机或平衡已通过。

## 2. 现有实现与已退出的旧链

本域实现于 `Development\LDDevelopmentDirector`、`LDProgressionRules`、`LDGrowthProtocol`、`LDGrowthRequests`，
`Economy\LDEconomyDirector`、`LDEconomyLedger`、`LDGrowthProviders`，`Inventory\LDInventoryDirector`、`LDVendorDirector`
与 `Framework\LDPlayerGrowth.cpp`；DEV 成长夹具为 `Development\LDDevGrowthFixture`。接口、生命周期与检查入口见
[库存与事务](../systems/INVENTORY_AND_TRANSACTIONS.md)，构筑安装与来源冷却见
[战斗、效果与成长](../systems/COMBAT_AND_GROWTH.md)。

旧实现已整体删除，不作为现状、兼容入口或数值来源：旧 R09 构筑 `LDCharacterBuild`
（构造器授技能、`AwardXP` 发点数、整份草稿 `Prepare`）与 `ULDCharacterCatalog`；V08 `LDV08Loadout`/`LDV08Modifiers`、
`LDGearRules`、`LDInventoryTypes`、`LDLoadoutRequest`（背包/Quick/多绑定/六动作/family 共享锁）；
V07/V08 装备、获取与战术目录及 B81–B84 合同；先 `Adopt`、改 OwnerState 金币再补账的旧背包链与书店 Vendor
（同名 `ALDVendorDirector` 已按本文重写）；旧营地 Actor 的分步奖励（N03 在核心 DeathKey 上统一提交）。
对应旧专测已退役，由 `Lockdown.Rules.DemoGrowth*`、Combat 来源/属性规则与 `RunDemoGrowthCheck.ps1` 拥有连接场景承接。

**物品修订不能偷换。** ItemRevision 只来自账本唯一结构修订；施法推进 Combat readiness，
不改变物品位置或修订，也不把网络修订塞回账本期望。

**提交后半段不分配。** 账本 `Prepare` 在副本上构造完整后态，`ApplyPrepared` 只移动；
日志尾容量 64 条（`LDEconomy::JournalTailCapacity`），满时 `Prepare` 以 `JournalFull` 拒绝；
`ALDEconomyDirector` 在 Transactions 开始和每个意图提交后调用 `TryCompactResolvedPrefix` 折叠已解决前缀。
请求与奖励回执窗口均为 64 条有界拒旧（§4）。

## 3. 唯一状态、正式类型与版本职责

本节签名均落在现有组件；不新增通用 Manager、
第二套账本、第二份技能状态或世界事件平台。

| 唯一写入者 / 类型 | 真相与限制 | 只读消费者 |
| --- | --- | --- |
| `ALDDevelopmentDirector` 修改参赛记录中的 `FLDGrowthBuild` | Level；当前已学技能及 rank；Q/W；永久属性贡献；BuildRevision。奖励/选择可改其获准字段；买卖只能通过 Development prepared patch 推进装备派生的 BuildRevision，不能直接改等级/技能。不得接客户端完整 Build。 | Combat 准备投影、自身 UI、该参赛 AI。 |
| 同一参赛记录的 `FLDProgressionState` | `int64 CumulativeXP`、ProgressionRevision、已获/已消费机会边界、当前一个 `FLDChoiceOfferState`、最多十条选择记录 `FLDChoiceRecord`；随机材料由本局私有 `MatchSecret` 按参赛者与机会序号派生（`LDProgression::OfferSeed`）。Level 只在 Build 存一次。 | `FLDProgressionView`；不复制给其他参赛者。 |
| `ALDEconomyDirector` 持有的 `LDEconomy::FLedger` | 唯一 `int64 Gold`、AccountRevision；物品 `InstanceId / IssueSerial / Definition / Owner / Slot / Revision`；每 owner 的 InventoryRevision。每活实例只有一个位置，最多两个活装备位置。 | Inventory 查询/准备、自身钱物 DTO；Combat 只消费来源和属性，不改账。 |
| `ALDInventoryDirector` | 两槽资格、实例解析、准备变更；不另存可写 Items/Bindings/Actor Holder 镜像。槽只允许 0/1，无背包、Quick、地面掉落位置。 | `FLDEquipmentView`、Vendor、Combat 准备器。 |
| Combat 写参赛持久 `FLDCombatEntityRecord` | 已安装构筑投影与 `Installed{BuildRevision, InventoryRevision}`；`Tracks`（`FLDCombatTrack`：`IssuedCooldownId`、来源定义、ReadyAtMs）与 `SourceRevision`；被动来源及需保留的触发状态。成长只经 `PrepareBuildChange` 提交完整构筑输入。 | Q/W、D、属性面板、AI 自身读模型（`FLDCombatOwnerView`）。 |
| Vendor 的私有会话（SessionId、Life、QuoteRevision）与 Shop 请求流 | 当前生命的会话/报价修订、请求序列/条款/结果；报价视图携带本局 CatalogDigest 与世界修订；不是持有装备或金币的权威。 | 当前拥有者商店页及 AI 购物适配。 |
| `FLDOwnerState` / 各 View | 上述真相的一次一致投影，携带 Match/Life 和发布修订；不得反向写账、成长或 Combat。 | 拥有连接；GameState 不持有全体私有构筑。 |

机会队列采用最多十项的有界表示：`EarnedThrough`、`ConsumedThrough` 表示连续机会序号，
待处理区间是二者之间尚未消费的序号，队首是 `ConsumedThrough + 1`。
开局机会与每次升级各占一个序号；`FLDChoiceRecord{Ordinal, OfferId, OptionId, RequestId}` 最多十份整局保留，不能因回执窗口淘汰而丢失
“这个 Offer 已消费”的证明。1 号是开局机会：成长 `BeginMatch`（Progression 提供者 Prepare、发育期开放前）
为每名参赛者（真人与 AI）授予它，并在同一 `BeginMatch` 内用 `DrawHead` 生成并保留其首张 Offer；复活从不再授予，新 Match 整体重建。达到 k 级授予 k 号机会，
`EarnedThrough` 必须等于当前等级（最多 `MaxChoices = MaxLevel = 10`）；
二者在同一奖励 patch 中推进，`ConsumedThrough` 只能随有效选择推进。
PendingChoiceCount 从队列计算，UI 不保存另一可写次数。

服务器 Offer 为 `FLDChoiceOfferState{OfferId, Ordinal, Options, GeneratedBuildRevision, GuardBuildRevision, Revision}`，
三个不可变 `FLDChoiceOption{OptionId, Kind, DefinitionId, FromRank, ToRank}`；效果由服务器按
`DefinitionId` 与 `ToRank` 解析，属性复合卡的全部分量属于同一个定义。
本人读取的 `FLDChoiceOfferView` 另带 OfferRevision/GuardBuildRevision，CatalogDigest 由 `FLDProgressionView` 携带。
Offer/Option ID 由服务器在相应机会生成时一次建立；OptionId 不等于显示顺序、
翻译名或客户端提交的属性值。

| 操作 | 必须推进的修订 / 不得推进的内容 |
| --- | --- |
| XP 增加但未升级 | ProgressionRevision；有钱变化时 AccountRevision。不得因读 XP 重建 Build。 |
| 奖励升级 | ProgressionRevision、BuildRevision；已有 Offer 只更新保护版本，不换卡。 |
| 选择成功 | ProgressionRevision、BuildRevision、选择记录；Combat 随新构筑安装推进自己的来源修订。 |
| 买入/卖出 | AccountRevision、InventoryRevision、对应 ItemRevision、BuildRevision 及 Combat 来源绑定；同一最终投影。 |
| 结清后的账户退休 | 通过Ledger的AccountRetire提交推进AccountRevision和独立Ledger Serial；金币/XP/物品/Build不改变，不直接修改Retired标志。 |
| 主动释放/被动触发 | Combat 的 readiness / 动作修订；不是库存结构修改，不能使所有选择因每次普攻持续过期。 |
| 秒表流逝、查询、关窗、回执重放 | 不改变构筑、库存、机会或来源绑定版本；UI 从绝对截止推显示。 |

### 3.1 固定标量类型与编码

下表是当前协议的确定类型；所有修订均为 `uint64`，不截断回旧字段，不保留旧 width 兼容。

| 字段族 | 唯一类型 / 编码 | 初值、限制与换代 |
| --- | --- | --- |
| Generation / Life | `uint32`，沿核心编码 | 仅核心推进，零值不接受为有效比赛/已建立生命；到上限明确失败，不回绕。LedgerMatch仍是FGuid。 |
| ContenderId / LedgerMatch / OfferId / OptionId / ItemInstance / IssuedCooldownId / ShopSessionId | `FGuid`，UE内置四个uint32的16字节编码 | 有效ID非零；可选项单独带存在标志，不拿零GUID当成功默认。Offer/Option/会话由服务器一次建立；Item按§4发行；CD每技能定义一份。新Match无旧执行权。 |
| RequestId / Ledger Serial / IssueSerial / CommitBatch | `uint64` | 请求每Family从1开始，只在新Match重置；死亡不重置。Ledger/发行独立计数见§4；CommitBatch 是提交所在权威边界的 `BatchSerial`。0不表示一笔成功业务。 |
| Build / Inventory / Item / Account / Progression / Offer / Quote revisions | `uint64`，包括所有Expected/Guard引用 | 匹配初始化或对象建立完成后基准为1，只由相应唯一写入者checked递增；当前倒计时不逐帧增修订。不能截断后比较。 |
| Level / Rank / OpportunityOrdinal / SlotIndex | `uint8` | Level=1..10、已学Rank=1..3、机会=1..10、Slot=0/1；队列计数可为0。未学/空槽用可选存在位，不伪造Rank0技能记录。 |
| Gold / XP / P2 Current与Max | `int64` | Gold/XP非负；资源域依内容/Combat；任何乘加/存储越界在prepare拒绝。 |
| ReadyAtMs / AtMs / 运行时毫秒期限 | `int64` | 当前Match绝对时间；ReadyAtMs=0仅表示未开始CD的初始就绪，无负期限。原始事件秒为核心`double OccurredAtSeconds`，不得用毫秒桶代替先后。 |
| `FLDDemoContentDigest` | `uint8 Bytes[32]`，SHA-256，wire固定32原始字节 | 不传FString/变长hex；日志/展示是64个小写十六进制字符，边界验证后转换。本局固定，不因开面板换摘要。 |
| 本域技能/装备 DefinitionId | 服务端`FName`；wire 为 `FLDDefinitionWire`（与Combat同一编码）：固定64字节UTF-8缓冲，最多63字节内容，NUL终止且余字节零填充 | 当前批准ID为`[a-z][a-z0-9_]*`ASCII子集；先验证UTF-8、终止/填充、长度、规范与catalog再构造FName。未知/非法拒绝，不截断。效果参数由服务器解析。 |
| Kind / Family / Outcome / Reason / bool | 领域Kind/Family/Outcome为`uint8`枚举，Reason为`uint16`，bool wire仅0/1 | 唯一领域头文件声明明确支持值；decoder逐值白名单，不用历史最大枚举包住废弃值。可选值编码为存在位+存在时的值。Family仅Choice/Shop，不能伪造Reward RPC。 |

ShopId、世界修订/观察句柄、Permit 和 ELDLifePhase 直接引用其拥有域正式类型，
不在本域另声明同名身份。命令中的世界修订以 `WorldMapId / WorldLayoutRevision / WorldGeometryRevision`
三个字段传输，服务器组合成世界的 `FLDWorldRevision` 比较，
不压成另一个全局修订号。`ExpectedLifePhase`只接受核心Alive/Dead；其余状态明确拒绝。
所有DTO和回执的结构修订使用本表的同一宽度；未涉及的修订不虚增。

请求与视图的 CatalogDigest 是本局内容快照的根摘要（`FLDDemoContentDefinitions::Identity.Root`，
即 `Scripts\SyncDemoContentData.py` 生成并写入 `LDDemoContentContract.h` 的根 SHA-256）；
其覆盖范围与计算口径由[数据管线](../systems/WORLD_AND_DATA_PIPELINE.md)单点维护，
本域只做逐字节比较，不复制生成器、哈希常量或第二份数值，也不拿子载荷摘要代替根摘要。

## 4. 共同身份、提交阶段、原子性与失败恢复

### 4.1 不另造死亡或时间身份

复用生命周期与 Combat 已收敛的以下类型，具体定义只由该域维护：

| 正式类型 | 本域使用方式 |
| --- | --- |
| `FLDMatchContext{uint32 Generation; FGuid LedgerMatch;}` | GameMode 建立本局映射；网络核验 Generation，Ledger 使用 LedgerMatch；新局二者一起换代。 |
| `FLDVictimKey{Kind, EntityId, Life}` / `FLDCombatBodyRef{Victim, BodyIdentity}` | 参赛者 EntityId 映射 ContenderId；野怪由世界营地成员映射。身体是短寿命物理引用，不是奖励账号。 |
| `FLDDeathKey{LedgerMatch, Victim}` | 唯一死亡/奖励键。**不含来源、EffectId、DeathSerial、BodyIdentity 或重试序号。** |
| `FLDCommittedDeath` | 消费 Combat 第一次零 HP 锁定的不可变事实：Key、VictimBody、VictimContender、实际致死所有者 `CreditOwner`（无效即 NoOwner）、FatalEffectId、ReleasedSource/SourceBodyAtRelease、RewardBasis、Cause、`double OccurredAtSeconds` 与其 ceil 毫秒桶 OccurredAtMs。全部字段进入奖励指纹。 |
| `FLDCommittedDeath.RewardBasis`（`FLDRewardBasis`） | Combat 在致死时唯一冻结：`RewardDefinitionId`（参赛者为角色定义，营地成员为怪物定义）、`bCreditOwnerEligibleAtDeath`、明确 `NoRewardReason`（NoOwner/SelfInflicted/OwnerLeft）；玩家另有 `VictimLevelAtDeath / VictimBuildRevisionAtDeath`。它们是资格/计算输入，不加入DeathKey。 |
| `FLDAuthorityBoundary{Match, BatchSerial, AtMs}` | 本次不可回拨的权威边界；所有截止统一毫秒，时间取核心 `GetAuthorityNowMs`。 |
| `FLDGameplayWritePermit{Boundary, Phase, GateRevision}` | 核心当前阶段签发；最终应用必须 `GameMode.ValidateWritePermit(Permit, ExpectedPhase)`，不能用一个“仍进行中”布尔值代替。 |

每次 Effects 的致死集合先各自固定完整致死快照，再结该集合奖励；否则先发给受害者的另一份 XP
可能改变随后查询到的等级。后续 Transactions 因果子批若产生新致死集合，取当时真实已提交
等级重新执行该屏障，不要求提前预知整个 Boundary 的未来死亡。
无主死亡不可继承 LastOwner，错误 payload 不可改已锁定收款人。
所有者在**实际致死时点**已阵亡但仍 InMatch 时有效；该时点前 Session 已处理离局则无资格，
不以活 Pawn 判断账号。核心在 `RewardBasis.CreditOwnerEligibleAtDeath` 中认证当时资格，后续退出/清理或奖励重试
不能把此前合法收奖改成 NoReward；不得用重试时已变更的当前成员状态追溯撤销。

致死事实按原始 `OccurredAtSeconds` 排序后逐个结算，同刻按受害者 EntityId 的确定顺序；
毫秒只是向上投影的桶，不能把同一毫秒中较晚事件提前。重试不改原始死亡事实；
奖励不拿 RequestId 或 Ledger Serial 冒充生产者序列。
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

LDEconomy::FPrepareResult LDEconomy::FLedger::Prepare(const FTransaction& Transaction) const;
void LDEconomy::FLedger::ApplyPrepared(LDEconomy::FPreparedCommit&& Prepared);

FLDPrepareGrowthResult ALDDevelopmentDirector::PrepareExperience(
    const FGuid& ContenderId, int64 GrantXP) const;
FLDPrepareGrowthResult ALDDevelopmentDirector::PrepareEquipmentChange(const FGuid& ContenderId) const;
void ALDDevelopmentDirector::ApplyPreparedGrowth(
    FLDPreparedGrowthChange&& Prepared, const FLDGameplayWritePermit& Permit);
```

Ledger 是纯值类型，不持有 Permit；调用方在同一提交段内先 `ValidateWritePermit`，
奖励（`ALDEconomyDirector`）与买卖（`ALDVendorDirector`）在首个 `Prepare` 与 `ApplyPrepared` 之间 `Pin` 账本。
Combat 配合的正式入口为 `PrepareBuildChange(Caller, FLDBuildProjectionInput,
FLDExpectedBuildVersions) -> FLDCombatPrepareResult` 和
`ApplyBuildChange(FLDPreparedCombatProjection&&, const FLDGameplayWritePermit&)`。
调用者是服务器解析后的本局参赛者，不是客户端自选账号；`FLDGrowthBuild`
是唯一构筑。提交许可只有上述核心类型，Apply不对RPC开放。

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
| `RetryableNotCommitted` | 必需奖励合法，但准备资源暂不可用（如账本 `JournalFull`、Combat `CapacityExceeded`）；钱、XP、等级、机会、随机状态均未提交。 | Combat 保留同一事实，在下一个权威边界的 DeathsAndRewards 优先重试一次。 |
| `PermanentlyRejected` | 旧局、伪造/未提交事实、同 Key 异条款（`DeathConflict`）、旧 Life 无回包（`StaleDeath`）、非法目录、越界等明确错误。 | 非法输入拒绝；若是真正待付事实的不可恢复错误，作为权威故障上报，不能伪装 NoReward。 |

Combat 持有已提交死亡事实（`ALDCombatDirector` 的待结算与重试列表）。暂失败只回收未提交的准备，
由核心在下一边界签发新的 DeathsAndRewards Permit，对同一事实重新准备。
**重试仍失败，或错误已不可恢复，则该边界返回错误，GameMode 以 `FaultAuthority` 进入 `Faulted`
（无正常胜者）；该奖励明确未支付，关闭后不后台补发。** 不无限 Pending，
也不回滚已经锁定的死亡来制造另一发奖机会。
待重试奖励存在时，任何结果都不能锁定（`TryCloseMatch` 返回 `RewardPending`），Host 结束返回 `RequestInFlight`，
等下一边界的重试结清。待重试奖励只能来自已关闭边界的 DeathsAndRewards，而该边界自身的 Capture 可能恰在同刻完成：
此时 Close 保留完成提案（`LD_CAPTURE_CLOSE_AWAITS_REWARD`），driver 随即开一个同刻的**结算边界**
（新 BatchSerial、相同 AtMs、不推进时间、不执行 Motion/Actions/Effects/Transactions/Capture），
只在其 DeathsAndRewards 用新许可做那唯一一次重试，再由其 Close 以原完成时刻和原涂鸦主人锁定结果。
因此完成时刻仍精确，终局后不再产生战斗、交易或新死亡，奖励先于结果结清；
重试仍失败则照上文权威故障、无正常胜者：故障诊断写明该死亡未支付，保留的完成提案随故障一起放弃。
若写闸被其他故障关闭时仍有奖励留在重试列表，Combat 记录 `LD_REWARD_UNPAID_AT_FAULT` 并丢弃，不后台补发；
新局的 Combat 与完成提案随旧局退休。

离局的身体/效果立即清理，但本局参赛记录和账本不能先删除或退休到无法结算。
关闭新命令权限与偿付既成事实是两件事；这不是离局后新发奖资格或断线重连能力。
退休本身是§4.4的可重放Ledger操作，不在生命周期清理回调中直接置Retired。

奖励只在 DeathsAndRewards、选择与买卖只在 Transactions 内提交；不得在 Publish 中再施法/发奖。

### 4.4 去重和有限留存

- Choice 与 Shop 分别使用本局参赛者范围的单调 `uint64 RequestId`（`TLDGrowthRequestStream`）。
  每条流只允许一个未决变更，另一新序号返回 `RequestInFlight`；未来序号缺口明确 `OutOfOrder`，不缓存无限乱序请求。
  受理即固定规范化 payload；最终成功和拒绝都消费该请求序号。
- 同 ID 同 payload 返回相同 Pending 或最终回执；同 ID 异 payload 为 `RequestConflict`。
  双击 Pending 按钮复用原请求，不新造第二次购买；真正购买第二件需前次终态和当前版本。
- 每条流保留 **64 份最终回执窗口**（`WindowCapacity`）作为工程限额，另保存 FinalizedThrough 水位。
  超窗返回 `ReceiptExpired` 与合法当前状态，不重执行、不猜测历史成功。
  窗口不是选择结果历史：最多十条 `FLDChoiceRecord` 保留整局。
- 死亡回执使用 DeathKey 与原始事实指纹。`ALDEconomyDirector` 按 `(Victim.Kind, Victim.EntityId)` 保存
  `ResolvedLifeThrough` 与最近 64 份奖励回执；同 Key 同指纹重放原回执，异指纹为 `DeathConflict`，
  旧 Life 无回包时为 `StaleDeath`，不能当第一次死亡。
  改 Source/Effect/BodyIdentity 只会与原事实冲突，不能产生第二份奖励。
- 所有参数/定义集合、活实例、来源 ID 和 pending 容器均有明确上界。
  发布、审计和请求序号溢出也是准备错误，不靠整数回绕或静默饱和维持“无限对局”。

**账本压缩合同（`LDEconomy`）：**

| 结构 / 字段 | 确定含义 |
| --- | --- |
| `CommittedSerial / NextCommitSerial` | `uint64`，初始化为0/1；每次真实 Ledger 提交取 Next，再 checked 递增。独立于回执/尾部数组长度，压缩不改变它们；重复与只读查询不占新号。 |
| `IssuedThrough` | `uint64`，初始化为0；每次装备 Create 原子使用 `IssuedThrough + 1`，失败不推进。记录和回执保留 IssueSerial，已使用发行号本局不复用。 |
| `FAuditState`（审计前缀 `AuditPrefix()` 与当前状态共用） | Match、ThroughSerial、每个账户的 Gold/Revision/bRetired/RetireSerial/RetireStamp、每 owner InventoryRevision、活实例的 IssueSerial/Definition/Owner/Slot/Revision、IssuedThrough、InitialGoldTotal、累计 Minted/Burned。保留退休账户，不只保留全局金币总数。 |
| `FJournalEntry` | 独立 Serial、`FTransaction`（Cause、BusinessKey、条款 SHA-256 `Terms`、CommitBatch、Mint/Burn、Gold/Create/Consume/Retire 及预期修订）、结果账户/库存修订、新建实例与发行号；足以独立复算余额与退休状态。 |
| `JournalTail()` | 精确为前缀之后连续的 Ledger 提交；容量64条是工程内存限额，不是比赛奖励/交易次数上限。满时 `Prepare` 返回 `JournalFull`。 |
| 请求流水位 | 每参赛者每 Choice/Shop 流的 FinalizedThrough、当前已受理条款以及独立64份最终回包窗口。压缩 Ledger 不删除该拒旧证据；窗外不再断言条款相同，只返回 ReceiptExpired。 |
| `ResolvedLifeThrough` | `ALDEconomyDirector` 按稳定 `(Victim.Kind, Victim.EntityId)` 保留最高已解决 Life。不是另一 MemberLife 计数。 |

前缀是不可变**审计输入**，不是可写钱包或世界存档。其初始点为 Serial 0 的明确开局状态，
InitialGoldTotal记录获准初始化总量，后续Mint/Burn从零累计，复活绝不重建此基线；
后续前缀由旧前缀加尾部独立重放构造，而不是直接复制可能已算错的当前余额当正确答案。
每个已提交装备 InstanceId 由唯一服务器发行器将本局 IssueSerial 一一编码成 FGuid：
四个字依次为 `LedgerMatch.A`、`LedgerMatch.B`、发行号高32位、低32位。
完整身份始终带当前 Match，禁止跨 Match 只按此 GUID 查找；本局内无哈希截断碰撞或回收复用。
客户端不能提供 Create 的 InstanceId/IssueSerial，不能让未知旧 ID 被 Adopt 成新物品。

`LDEconomy::FLedger::TryCompactResolvedPrefix(uint64 ExpectedCommittedSerial, FString& Error) -> ECompactResult`
由 `ALDEconomyDirector` 在 Transactions 开始、每个意图提交后及退休后调用；账本被复合提交 `Pin` 时返回 `Pinned`，不释放记录。
不等待网络 ACK；可查询的完整回包已在独立窗口中，已释放效果持有独立来源快照而非 Ledger 指针。
压缩按以下顺序：

1. 核对 ExpectedCommittedSerial 和尾部连续性，取 `S = CommittedSerial`，不跳过未决记录。
2. 从原 Prefix 重放全部 Tail，逐账户/修订/实例归属/库存修订/发行号/审计总量复算，
   比较当前唯一 Ledger 状态；任何不一致为 `CorruptHistory`，不能覆盖差异。
3. 预分配由复算结果构成的新 Prefix(S) 和空 Tail。
4. 一次交换 Prefix/Tail 并释放旧记录；NextCommitSerial、IssuedThrough、钱包/物品/
   业务修订、回包窗口、死亡水位和十次选择结果完全不变。准备失败保留旧 Prefix/Tail。

`ECompactResult` 为 `Compacted / NothingToCompact / Pinned / PrepareUnavailable /
CorruptHistory`；`ALDEconomyDirector` 把前三者视为正常，其余使本边界失败（权威故障）。
正常情况下每个64条已决前缀可持续折叠，不限制比赛长度。
该过程不提供游戏状态 Restore，不保留旧世界 checkpoint 系统。

**AccountRetire 的唯一提交路径：**
`ALDEconomyDirector::ProcessTransactions` 在本批意图之后调用私有 `RetireLeftAccounts`：
对核心 `ReadContenderLife` 为 `Left` 且未退休的参赛者，提交一笔 `ECause::AccountRetire` 事务；不是玩家/AI可调用命令。
业务键固定为 `("account-retire", LedgerMatch, ContenderId)` 的唯一退休事实。
`FRetireAccount` 含 Account、ExpectedRevision 与 `FRetireStamp{Life, LifeRevision}`（离局时的核心生命记录）。
Prepare 检查当前账户/修订（已退休为 `AlreadyRetired`）；Apply占用下一个独立Ledger Serial，
只把 bRetired 从false变true并递增AccountRevision，记录 RetireSerial/RetireStamp，
不改Gold、物品或Build。已退休账户再收金币为 `RetiredAccount`。
退休准备失败使本边界失败（权威故障）；正常终局已经锁定则不再提交。
Reconcile 与证据复算按 AccountRetire 条目验证旧修订/false状态、重放true和新修订及关联戳；
压缩后的Prefix继续携带这些结果，不能从当前账户补标志以掩盖缺日志。
退休账户及其冻结物品仍参与本局审计；不出售、转移或重新开放该账户。
离局参赛者仍是某个已锁死亡的收款人（本边界待结算或待重试奖励，`ALDCombatDirector::OwesRewardTo`）时，
`RetireLeftAccounts` 跳过他，账户保持开放；下一边界 DeathsAndRewards 的唯一重试先结清，随后的 Transactions 再退休
（`Lockdown.Rules.DemoGrowthRetireAwaitsReward`）。因此重试不会命中 `RetiredAccount`。

**三个准入屏障必须在 Ledger Prepare 之前生效：**
请求序号不大于 FinalizedThrough 时仅查已有回包/拒旧；DeathKey 的 Life 不大于对应
ResolvedLifeThrough 时仅重放/拒旧；Create 只使用账本签出的
`IssueSerial = IssuedThrough + 1` 和对应新 ID（`FLedger::InstanceFor`）。
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
持续效果、装备被动触发、获准普攻副命中使用 Combat 已锁定的真实所有者；
抑制副命中的吸血/递归触发不抑制其合法死亡奖励。

| 输入 | 期望结果 |
| --- | --- |
| 活参赛者击杀野怪 | 全部奖励归该所有者，无助攻、距离、伤害比例分配。 |
| 参赛者击杀另一参赛者且有助攻者 | 致死瞬间冻结助攻者名单（`FLDCommittedDeath::Assisters`，见下）；同一 `CommitDeathReward` 复合提交里击杀者拿全额击杀金币，助攻者平分 `floor(击杀金币×REWARD.AssistGoldRatio)`，XP 由击杀者与助攻者平分，各自向下取整；全部准备成功才一起安装，同一死亡只结算一次。 |
| 无击杀者的参赛者死亡 | 不发奖励，也没有助攻者。 |
| 两个参赛者同批互相致死 | 两个不同 Victim DeathKey，各按各自致死快照和仍 InMatch 的所有者收奖。 |
| 已阵亡来源的存续效果合法击杀 | 该实际致死时原参赛者仍在本局则全额收奖，不能因无 Pawn 丢 XP 或改发兜底账号。 |
| 无主致死、自己杀自己 | 合法 NoReward，同一死亡仍封已处理；不退回历史攻击者。 |
| 回位/重置、断线移除、Host 结束清理 | 不制造击杀事实，不给钱/XP，不把原旧 LastOwner 回调送奖励。 |
| 致死与正式离局先后 | 致死在先则资格/归属锁定，后来离局不改既成结果；离局在先则立即移除身体并取消其独立效果，不再产生后续击杀。原始时间先于毫秒桶/phase。 |
| 营地成员死亡/整组清空/刷新 | 每成员同一 Life 只一次；清空/刷新仅世界生命周期，不再发一份清营奖。 |

**助攻资格与记录。** 唯一的伤害/状态施加路径（`ALDCombatDirector::ApplyHit` 的已提交正伤害——含被护盾吸收的部分——或定身、缴械、眩晕、减速；
`ApplyTauntEffect` 的嘲讽；击退、沉默、破防与灼烧等在其施加成功处）对参赛者受害者调用
`ALDCombatDirector::RecordAssistContribution`，按效果的实际所有者（CreditOwner）在受害者这条生命的身体上记下最近贡献时刻
（`FLDBodyCombatState::AssistContributions`，纯规则 `LDAssist::RecordContribution`），换命随身体一起消失。
`LockDeath` 在击杀者合格时用 `LDAssist::SelectAssisters` 选出死亡前 `REWARD.AssistWindowSec` 内（含边界）有贡献、仍在本局、
不是击杀者或受害者的参赛者，排序后冻结进死亡事实；死亡中的参赛者仍可得助攻。分配用 `LDAssist::SplitKillReward`。
复合提交里每名收款人各一条单账户 `DeathReward` Mint，后一条以 `FLedger::PrepareAfter` 链在前一条之后准备，全部准备成功后依次安装；
助攻者的 Mint 业务键是 `death-assist`+LedgerMatch+受害者生命+助攻者。回执 `FLDRewardReceipt` 的自身字段是击杀者那一份，
`Assists` 按事实顺序列出每名助攻者的金币、XP、等级与机会变化。助攻者或击杀者离局后、重试结清前，其账户不退役（`OwesRewardTo`）。
| 营地回位到家 Reset | 仅仍存活成员满血蓝、清临时状态和冷却就绪；不复活死者、不创建新Death/Life、不发奖。回位途中真实致死仍按原生命一次结算，不是参赛者洗CD入口。 |

经验使用内容的累计阈值 `XP.Threshold[L]`；目标等级是合法范围内已达到阈值的最高等级。
前后等级差逐一授予机会，每个序号只一次，不能把连升压成一张卡或一次性预生成全部卡。
`FORMULA.XPToNext` 从同一阈值表派生，不维护另一份会漂移的升级成本。

满十级后继续累计 `int64` XP，但不再升级/授机会、不转金币（`LevelForXP` 不超过 MaxLevel）；UI 显示 `MAX`，
不读取不存在的十一级阈值。不把旧有限奖励预算改名沿用。
Gold、XP、阈值运算及审计累计的任何越界必须在整奖准备阶段拒绝，
不得只发金币、只发 XP、截掉溢出再返回 Granted。

## 6. 三选一：可实施状态机与合法池

### 6.1 生成、展示、确认和恢复

| 权威状态 / 本地动作 | 转移与持久内容 | 机会是否消费 |
| --- | --- | --- |
| `NoOpportunity` | 奖励提交推进已获机会；没有未决 Offer 时准备队首。 | 否 |
| `HeadReady` | 当前构筑已生效后，以局内固定目录、队首私有随机材料生成三个不同合法 Option，保存一次。 | 否 |
| `Offered` | HUD 提示待选数；真人的开局 Offer 在本局首次到达本人、且没有其他主面板或输入捕获时由 HUD 自动打开一次面板（不代选；若玩家已在用自己的面板则放弃这次自动打开；几秒内未操作或本人受击时收起回待选提示），其余由玩家主动开面板，不强夺战斗输入。不打开、关闭、重开、死亡均读同组。 | 否 |
| 本地卡片/替换草稿 | 预览卡片与 Q/W；返回、Esc、取消替换只清草稿，不改服务器 Offer。 | 否 |
| `RequestPending` | 最终点击提交一个 Option 及必要替换确认；显示等待，不能本地先改 rank 或减机会。关闭面板不声称撤回已受理意图。 | 否 |
| `Committed` | 同一复合提交应用效果、遗忘/装槽、来源失效、最终属性、机会消费和结果；之后才生成下一队首。 | 恰一次 |
| 拒绝 / 版本恢复 | 返回原因和同一个 Offer 的当前保护版本；重新明确确认，不换卡、不替换其他目标。 | 否 |
| `Rerolled` | 玩家用一次免费重抽（`FLDChoiceCommand.bReroll`）替换当前 Offer：同一机会序号、新 OfferId、`bRerolled`，`RerollSeed` 决定的新随机材料，尽量避开上一组三张；消费一个重抽令牌，不消费机会、不改构筑。 | 否 |
| `Frozen / InvalidMatch` | 终局只读冻结，离开失去提交资格；新局所有旧身份无执行权。 | 否 |

开局队首在 `BeginMatch` 中随 1 号机会生成；队列为空时奖励授予的队首在 `ApplyPreparedGrowth` 中立即生成，不等“首次打开页面”才抽；
后续队首只在前一次选择已经应用后生成。开局机会未用又连升九次（共十次待选）也最多同时一份 Offer，
余下机会保留队列，不预抽十组再修补非法升级。

准备阶段对 prospective Build 枚举下一队首的合法池（`HeadPool`，不足三项即 `DataInvalid` 拒绝整次准备），**不提前生成、
公开或消费下一组随机结果**。实际抽样在本次状态应用后、整体 Publish 前完成；
这是固定目录、预验合法池上的无失败纯操作；随机种子由 `OfferSeed(MatchSecret, Contender, Ordinal)` 决定，
同一序号重试不换 seed。抽样规则见 §6.3，`DrawFeasible` 证明当前合法池在任何种子下都能凑齐三张卡。
目录损坏不以重抽/属性替换修补：保留机会与原内容，以 `DataInvalid` 明确拒绝，必需奖励按 §4.3 走故障路径。

### 6.2 满槽替换与版本保护

空槽学习采用 Q 后 W 的确定空槽顺序，UI 明示目标槽。满槽的新主动仍是一张合法卡，
“替换 Q”与“替换 W”是后续参数，不是两张不同候选。
最终确认必须绑定 Slot、旧技能稳定 ID、旧 rank、ExpectedBuildRevision 和 OfferRevision。
只选中新卡但尚未确定替换不消费；确认成功后旧技能/等级真实删除，新技能从 rank 1（初级）开始（D13），
不返还米或选择次数。旧技能冷却记录不等于备用技能库：被遗忘的技能再次获得仍从初级开始，仅继承未结束截止。

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

### 6.3 抽卡规则与重抽

升级抽卡不按标签、流派或已有构筑加权，唯一的权重见下。技能有 `draftable`：为 false 的技能不出现在任何 Offer（开局与升级），
仍留在 `LegalPool`（DEV 学习与夹具预置可用）。

| 机会 | 组成（`LDProgression::DrawOffer`） |
| --- | --- |
| 1 号（开局） | #153：从 `OpeningCards` 等概率抽三个不同方向，各配一个不同的隐藏初级主动。`ActiveGrowthStats` 从主动实际效果读取成长属性，与方向交集对应 `OpeningMatchedSkillWeight`，否则 `OpeningOtherSkillWeight`，数值见内容 §4.7.1。 |
| 2–10 号 | 三个固定卡位，从左到右：**升级位**从可进卡池的已学初级技能中等概率抽 1 个升为高级，没有时改抽一张新技能；**新技能位**从未学技能中按 `NewSkillWeight` 抽 1 个；**自由位**以 `CHOICE.FreeSlotAttributeChance`（0.4）从属性卡等概率抽，否则再抽一张新技能。没有技能可抽的卡位用属性卡补位。 |

新技能权重（`LDProgression::NewSkillWeight`，基点）：一律 10000；Q/W 都满时未学主动为 `CHOICE.NewActiveFullSlotWeight`（0.5），
且一组抽到一张新主动后，剩余新主动退出本组（每组最多 1 张新主动）。权重只看槽位状态，不看构筑其他内容。
自由位的概率判定总会消耗一次随机数，所以它的来源不改变其后的随机流。所有抽取逐张不放回，同一技能不会同时以学习与升级出现。

重抽：`progression.reroll_levels`（4、7）每达到一个等级获得 1 次，`RerollTokensEarned(Level) − RerollsUsed` 为剩余次数，可留存。
`bReroll` 命令走选择请求流（同一请求窗口、回执与去重、Match/Life/生命阶段/OfferRevision/BuildRevision 保护），在 Transactions 阶段提交：
开局方向卡 `OpeningNoReroll`（即使后来获得令牌），没有次数 `NoReroll`，本组已重抽 `OfferAlreadyRerolled`；成功时以 `RerollSeed(MatchSecret, Contender, Ordinal)` 按同样规则重抽，
上一组的三张卡在每个卡位都优先排除（池中没有别的卡时才允许重复），回执带 `bRerolled`、`NextOfferId` 与剩余次数。重抽不花米、不消费机会、不改构筑修订。

内容验证与 `DrawFeasible` 共同保证：开局方向与可进卡池主动各不少于三个（不足时整次准备以 `DataInvalid` 拒绝，不用别的类别补位）；
升级组按最坏情况（自由位抽到属性卡、无技能可抽的卡位改用属性卡、Q/W 满槽时最多 1 张新主动）计算，属性卡数量足以补齐三张。覆盖证明不代替选择持久性、来源历史、真实效果、网络或平衡验证。

开局主动分配仅在 `FLDChoiceOfferState::HiddenActives` 中，不进入 `FLDChoiceOption` 或本人/对手视图，不在生成日志中记录。
选定时 `ApplyOption` 同时记录 `Build.OpeningCard` 并学会初级主动、装 Q；原子投影按永久选择属性来源处理，
通过主动的初级投入计一次选择，不另计属性包。`FLDChoiceReceipt::OpeningActiveId` 只在成功后揭晓。
延迟选择、死亡或关闭页面都保留原 Offer；后续升级机会按正常卡位逐组生成。

## 7. 两装备、商店与来源冷却

### 7.1 钱物真相和买卖

最终装备实例存在于 Ledger，没有另一个可写 `ALDItem` 持有层（旧物理物品类已删除）。
Inventory 从 Ledger 解析两个真实位置，Combat 引用实例/定义，视觉附件只负责表现。

| 事务 | 同一次提交的全部后果 |
| --- | --- |
| Buy | 检查目录/明确展示价、余额、空槽；准备唯一 Instance、占用空槽、扣款、属性/被动绑定、修订和回执。满槽 `EquipmentFull`，不自动卖/毁旧物。 |
| Sell | 按精确 Instance/ItemRevision/真实位置验证；按目录预算的卖价（`SellGold`，即 `FORMULA.SellGold`）销毁其权威持有、移除对应贡献、回款并发布修订。 |
| Sell 后 Buy | 两次独立请求。Sell 成功后重读余额/槽位再发 Buy；后买失败保留真实卖出结果，不恢复已卖物或虚构新物。 |
| Upgrade | 账本 cause `Upgrade`：Burn `SHOP.UpgradeCost`（240）+ 把一件本人持有、未进阶的实例标为 `bUpgraded`（ItemRevision 与 InventoryRevision 各进一），账户 `bUpgradeUsed` 置位；每个账户整局一次，卖掉进阶装备后不恢复。物品的被动块须有已批准的进阶形式（`LDDemoEquipment::Upgradable`），否则 `UpgradeUnavailable`；已用 `UpgradeUsed`、已进阶 `AlreadyUpgraded`。同一复合提交推进 BuildRevision 与 Combat 投影；进阶不刷新冷却。 |

账务分类只保留所需操作（`LDEconomy::ECause`）：`DeathReward` 的 Mint，`Buy` 的 Burn+Create，
`Sell` 的 Consume+Mint，`AccountRetire`；DEV 构建另有仅限 `-LDDevGrowthFixture` 的 `DevFixture` Mint 与 DEV 测试重置的 `DevReset`，
不属于产品流程。开局金币是 `FLedger::Initialize` 为每个名单账户写入的 `SHOP.StartingGold` 基线（审计记为 InitialGoldTotal），
不是 Mint。不引入商店现金、有限旧库存、银行、玩家交易、制作或掉落循环。
目录固定价和 `FORMULA.SellGold` 必须证明任意卖买循环无正金币收益，且每个精确实例最多售出一次。
开局初始化只在核心新 Match 中执行；复活、打开商店、读取 projection 不初始化钱包。

交易提交直接调用世界域的唯一正式签名（`ULDMapRegistry::QueryShopEligibility`，实现于 `World\LDShopEligibility.cpp`）：

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

持久 CD 键使用共同 Match/Contender，加来源 Kind、技能定义及 `IssuedCooldownId`；
不使用 Q/W 槽号作为完整冷却身份。装备没有主动，不发行冷却记录。
角色技能每定义只有其原持久来源；D 冲刺是唯一的 `Innate` 记录。

到期记录仍留本局复用，不能删掉再造一条；定义集合由批准目录有界。
**只有 Combat 写 ReadyAtMs**，其随发行/绑定计划进入同一复合提交，不由 Vendor 或成长域自行设置为 ready。

学习指定技能时，Combat 在 `PrepareBuildChange` 中回绑该定义已有的记录；本局从未发行过时才创建第一条。
客户端不能挑 ID、传 ReadyAt、借目标槽位决定回绑。最终准备重验来源绑定修订与当前 readiness；
失败不留下新 ID、绑定或机会消耗。

换槽、遗忘、死亡均不缩短截止。重学技能从初级开始但继承原期限；到期后自然可用不算洗 CD。
升阶、换装/冷却属性变化不重写已开始的期限，下次合法释放才按当时批准参数计算。

### 7.3 被动与攻击形态投影

基础属性逐实例相加，被动按内容稳定 `Passive.Family` / 被动定义键建立一次执行记录，
另保留贡献来源集合。两份减一份不重新注册触发、不重置内部冷却、不重复发一次性收益；
最后一份移除才移除持续贡献。若被动有未结束触发期限，按同一 Combat 来源合同保留必要债务。

`item_echo_maul` 的属性/普攻群攻贡献与 `CHOICE.attr_hand_out.AttackRangeCmFlat`
经同一最终投影到普攻几何和命中；同款锤属性相加、`passive_cleave` 一次。
横扫副命中不吸血/不递归是当前首轮内容语义，不扩大成全部技能的默认触发政策。
已成功释放的独立效果使用 Combat 释放快照并继续原定期限，普通死亡、出售装备或遗忘技能
均不取消，不再要求当前仍持有原 ItemInstance/技能；目标 Life、终局和正式离局取消仍检查。
维持型光环、装备被动和引导则按持续持有/施法条件解绑失效，不能与独立快照混用。
本域解绑不回滚已提交命中、伤害或奖励，不给资源退款，原 CD 截止照常继续。

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
FLDChoiceReceipt ALDDevelopmentDirector::CommitPendingChoice(
    const FGuid& ContenderId, const FLDGameplayWritePermit& Permit);

FLDEquipmentView ALDInventoryDirector::ReadEquipment(const FGuid& ContenderId) const;
FLDShopOpenResult ALDVendorDirector::QueueShopOpen(
    const FGuid& ContenderId, const FLDShopOpenCommand& Command);
FLDShopOpenResult ALDVendorDirector::ReadShop(const FGuid& ContenderId) const;
FLDShopOpenResult ALDVendorDirector::OpenPendingShop(
    const FGuid& ContenderId, const FLDGameplayWritePermit& Permit);
FLDSubmitResult ALDVendorDirector::QueueShopCommand(
    const FGuid& ContenderId, const FLDShopCommand& Command);
FLDShopReceiptQueryResult ALDVendorDirector::QueryShopReceipt(
    const FGuid& ContenderId, const FLDReceiptQuery& Query) const;
FLDShopReceipt ALDVendorDirector::CommitPendingShopCommand(
    const FGuid& ContenderId, const FLDGameplayWritePermit& Permit);

FLDRewardReceipt ALDEconomyDirector::CommitDeathReward(
    const FLDCommittedDeath& Death, const FLDGameplayWritePermit& Permit);
bool ALDEconomyDirector::ProcessTransactions(const FLDGameplayWritePermit& Permit, FString& Error);
bool ALDEconomyDirector::PublishGrowth(const FLDAuthorityBoundary& Boundary, FString& Error);
```

`Queue*` 只规范化、核验受理条件、固定条款并进入本批队列，返回
`AcceptedPending / Final / AdmissionRejected`；不是立即扣钱/成长的函数。
`Commit*Pending*` 与 `OpenPendingShop` 只由 `ProcessTransactions` 在 Transactions 阶段按服务器受理顺序分派，
处理的是受理时冻结的条款，不直接暴露成 RPC。写闸已关后新请求以 `MatchClosed` 拒绝受理，已有请求号仍可重放。
`PublishGrowth` 在 Close 之后执行，没有许可，也不写玩法状态。
Query 返回 `Pending / Final / NotFound / ReceiptExpired / InvalidContext`，不存在“查不到就再执行”。
开店处理器是 Transactions 阶段内部入口：`QueueShopOpen` 受理开店意图并返回 Pending，
不提前同步调用受Permit约束的世界查询；终态由 `ReadShop`/本人发布与 `ClientShopOpenResult` 给出。
复用当前交互绑定及取消机制，每名参赛者最多一个未办理开店意图，同一意图重复受理合并；
不另建商店请求流或第二套Pending队列。
真人的开店意图由拥有者客户端在 Navigation 当前 ApproachShop 绑定的 `OnShopArrived(ShopId, Life)`
上发送一次（Life 须仍是当前存活生命，附客户端世界修订）；本地拒绝同样以 Rejected 经 `OnShopOpenResult` 反馈。
本人站在小摊交互半径内、本命还没有会话，且商店页面开着、有预售或有可买预购时，HUD 也经同一入口为本命请求会话（APP_SPEC §5 预购与预售）；服务器照常做资格查询。
AI 由服务器适配器直接调用 `QueueShopOpen`。
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
| `FLDChoiceReceipt / FLDShopReceipt` 的共同头（`FLDGrowthReceiptHeader`） | Generation:uint32、Family、RequestId:uint64、OriginalLife:uint32、OriginalLifePhase、Outcome（Applied/Rejected）、Reason:uint16、可选CommitBatch:uint64（提交或拒绝所在权威边界的 BatchSerial）、结果版本集合。只读重放标志属于传输外壳，不改原业务结果。 |
| 结果版本集合（`FLDResultRevisions`） | Account/Progression/Build/Inventory/Offer revisions均uint64，按存在位标明本次适用字段；Applied给提交后的版本，Rejected的当前guard只作同步提示、不宣称曾提交。Combat 来源修订不在此集合中。 |
| `FLDChoiceReceipt` 载荷 | 原OfferId/OptionId、OpportunityOrdinal:uint8、消费后ConsumedThrough:uint8、Kind/DefinitionId/NewRank、替换时的 ReplacedSlot/ReplacedSkillId/ReplacedRank、可选下一OfferId；Rejected不伪造消费或下一卡。 |
| `FLDShopReceipt` 载荷 | Buy/Sell、精确Instance/IssueSerial:uint64/DefinitionId/SlotIndex、ActualGoldDelta:int64、可选LedgerSerial:uint64；不是整个旧OwnerState副本。 |
| `FLDRewardReceipt`（仅服务器内部） | DeathKey、冻结事实指纹（SHA-256原始32字节）、§4四态/Reason、Recipient、可选LedgerSerial、GoldDelta/XPDelta:int64、LevelBefore/After:uint8、新授机会区间 `OpportunityFirst..OpportunityLast`（First>Last 表示无）和结果版本。Granted/NoReward是终态；暂失败无提交号，不伪造版本增长。 |
| `FLDChoiceReceiptQueryResult / FLDShopReceiptQueryResult` | 原FLDReceiptQuery（Generation:uint32、Family、RequestId:uint64）、LookupStatus、可选FLDPendingReceiptInfo、可选对应FLDChoiceReceipt或FLDShopReceipt；不是业务终态Receipt本身。 |
| `FLDPendingReceiptInfo` | 已受理请求的OriginalLife:uint32、OriginalLifePhase；没有业务Outcome/CommitBatch或成功载荷，不需要伪造原生命。 |

枚举由同一个本域协议声明供Controller/AI/UI/Combat引用，不各写一套数字表；
本表固定宽度，§8.2固定拒绝语义，真正实现的显式枚举编号纳入同一wire schema和拒绝测试。
新局清理请求执行域，查询依旧必须带原Generation；绝不由空参数或当前OwnerState补全身份。
查询包装结果的形状固定：LookupStatus=Pending时仅PendingInfo存在，Final时仅匹配原查询键的
业务Receipt存在；NotFound/ReceiptExpired/InvalidContext时二者均不存在，只回原查询键/状态。
这些结果不伪造OriginalLife、Applied/Rejected或物品/选择载荷，业务终态回执保持不可变。

| 命令 / 类型 | 必要输入与上限 | 提交、拒绝与回执 |
| --- | --- | --- |
| `FLDChoiceCommand` | Generation、当前参赛记录 Life、ExpectedLifePhase（Alive/Dead）、递增 RequestId；OfferId/OfferRevision、OptionId、ExpectedBuildRevision、CatalogDigest；满槽时 Slot 0/1、ExpectedOldSkillId/Rank、明确 ReplaceConfirmed。 | Transactions 阶段查当前成员/生命状态、队首及原卡/版本；成功回旧 Offer 的唯一结果及新状态修订。最多一个 Option，不收任意 Build、rank/属性数组或奖励金额。 |
| `FLDShopOpenCommand` | Generation/Life、公共ShopId（`FLDDefinitionWire`）、客户端所见世界修订 `WorldMapId/WorldLayoutRevision/WorldGeometryRevision`；请求不携Permit。 | 先进入权威队列，在Transactions借当次Permit重验存活/进行中及世界资格后给SessionId/QuoteRevision；失败不给默认商店或旧会话执行权。 |
| `FLDShopCommand::Buy` | Generation/Life/RequestId、SessionId、QuoteRevision、CatalogDigest、世界修订三字段；ExpectedAccount/Inventory/BuildRevision；商品 DefinitionId、明确展示 BuyPrice。来源绑定由 Combat Prepare 按当前状态重验，客户端不传来源修订。 | 当前会话、实际范围、价格、余额、容量、全部准备合法才 Create+扣款+来源一次提交；回 Instance/Slot/结果修订。一次只买一件。 |
| `FLDShopCommand::Sell` | 上述上下文与预期版本；精确 Instance、ExpectedItemRevision、展示 SellPrice。 | 重查所有权/位置/卖价，Consume+回款+解绑一次提交；回具体已卖实例及修订。一次只卖一件。 |
| `FLDReceiptQuery` | 显式 Generation、Family、RequestId；拥有者由连接/AI适配解析，不接受别人的任意历史查询。 | 先拒旧局再查，只读；回原查询键与LookupStatus，OriginalLife仅来自实际存在的PendingInfo或Final业务Receipt。本局历史与终局已有结果可查，不覆盖当前身体/新局。 |
| 角色施法 | 按 Combat 正式请求及当前来源版本。 | 不走本表 Shop/Choice 许可；Q/W 与 D 为真实来源，失败不耗资源。 |

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
| `OldMatch / OldLife / LifePhaseChanged / LeftMatch / MatchClosed / NotContender` | 清旧身体意图/交易会话；读取当前获准状态或停止本局策略，不重试旧执行权。 |
| `OfferGuardStale / BuildStale / InventoryStale / AccountStale / CatalogMismatch` | 读取当前状态；Offer 保留同 ID/内容，重新明确确认。不得自动修改替换目标或把后续卡当原卡。 |
| `NoOffer / UnknownOffer / InvalidOption / OfferAlreadyCommitted / ReplacementRequired / ReplacementMismatch / ReplacementNotAllowed` | 给出原结果或实际 Q/W，保留尚未消费机会；不自动代选/重抽。 |
| `NoShopSession / QuoteStale / PriceChanged / UnknownItem / ItemNotOwned / ItemStale / EquipmentFull / InsufficientGold` | 刷新自己报价/钱物；满槽先明确 Sell，等成功再另 Buy；不忙循环、不给补贴、不能直接改钱。 |
| 世界资格错误（`UnknownShop / StaleWorld / OutsideRange / NotGrounded / InvalidGround / WrongSurface / InvalidApproachRegion / DataNotReady / StaleContext`）/ `Dead` | 没成交就零钱物变化；真导航到店或等复活后重开合法会话。受击本身不作此错误。 |
| `InvalidRequest / RequestConflict / OutOfOrder / RequestInFlight / ReceiptExpired` | 不重执行；核对原请求/序列、查询或同步当前状态，不能以超时猜成功。 |
| `NumericUnderflow / NumericDomain` | Combat 最终资源投影未提交；整次可选交易/选择拒绝，源/钱/机会/Current不变，等合法资源状态或修复数据后新请求，不能向上补1或靠换装杀死。必需奖励中的不可恢复数值错误走 §4 故障。 |
| `DataInvalid / ArithmeticOverflow / PrepareUnavailable / CapacityExceeded / NotAuthority / AuthorityFault` | 明确未提交和是否可恢复；按 §4 限制恢复，不能返回默认数据、长期 spinner 或静默丢选择。 |

全部原因值只在 `ELDGrowthReason`（`LDGrowthProtocol.h`）声明一次；奖励专用的
`NoRewardNoOwner / NoRewardSelf / NoRewardOwnerLeft / DeathConflict / StaleDeath` 只出现在服务器内部奖励回执。

错误详细信息只给有权的自身；不通过未知 Item/技能/账号错误泄露其他参赛者构筑或钱包。
内部断言和账本导出不发送客户端。

### 8.3 私有读模型与回包顺序

`FLDProgressionView` 提供当前生命/选择资格、Level/MaxLevel/CumulativeXP、当前级与下一级阈值、
ProgressionRevision/BuildRevision、EarnedThrough/ConsumedThrough/PendingChoiceCount、当前一组固定 Offer、已学技能与属性卡计数及 CatalogDigest。
`FLDEquipmentView` 提供自身 Gold/AccountRevision、InventoryRevision、两个槽的
Instance/IssueSerial/Definition/ItemRevision；技能冷却只由 Combat 的本人 `FLDCombatOwnerView` 发布。
`FLDShopView` 提供当前合法商品、买价/卖价、会话/报价版本与可卖实例。

View 最终组成同一次 `FLDOwnerState` 发布（`FLDOwnerState.Growth`，`FLDGrowthOwnerView`），
附 Generation/Life/发布修订及各子域结果修订。
回执可以先于新快照到达：UI 可展示“已提交，等待状态同步”，不能自行扣款/造物补齐；
新快照先到则迟到回执只补结果提示，不回放旧快照。
重放的旧生命成功回执只能作为历史结果查询，不改当前生命资源/来源。
等待期间关闭面板不撤销已受理事实，重开查询真实结果；请求失败不保留假成功图标。

自身被动列表可展开/滚动，不新增玩法槽上限。Q/W 和 D 从真实输入映射显示按键，
选中别人不改变按钮所有者。
UI 手势由输入域捕获，不穿透发世界命令；打开、选择、购物都不暂停/授无敌。
GameState、全场击杀通知不获取本域私有钱物/Offer/来源 ID。

用户已允许**当前可见对手的装备展示**，不等于授权完整 Inventory。
本域提供 `FLDObservedEquipmentView`，只含 `uint8 Slots[2]`（每槽为原生 `ELDDemoEquipmentId`，0 表示空槽）；
空槽明确为空，同款两件可各占一槽，名称/图标取公开内容定义。
`ALDInventoryDirector::ProjectObservedEquipment(ContenderId, Out, Error)` 仅供世界/视野发布内部调用，
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
| 队列/固定 Offer | 只清本地草稿，Pending 查真实结果 | 原卡/机会保留，复活不再授开局机会；可用当前Dead阶段确认，只更新持久Build；旧阶段拒绝 | 不再提交 | 冻结，不继续选 | 清队列/Offer/结果域，重新授予开局机会及其首张 Offer |
| 来源/IssuedCooldownId/ReadyAtMs | 不因图标/草稿清除；遗忘不取消独立已释放效果 | 时间继续；独立效果续原期限，新身体重绑保留来源 | 禁止新命令并取消尚未结束独立效果，不追溯改写既成事实 | 不再释放，展示只读 | 旧 ID/期限全部失效 |
| 商店会话/未提交买卖 | 关闭表现不声称回滚已受理交易 | 死亡失效；复活必须新 Life/会话 | 失效，未提交拒绝 | 新成交拒绝 | 旧报价/会话无效 |
| 致死到奖励 | 不是 UI 可取消意图 | 死者可收合法存续效果奖励 | 按实际致死时资格，后来离局不撤销；离局先则不再产生效果击杀 | 无新提交；必需未决不越过正常闸 | 旧 DeathKey 拒绝 |
| 已提交回执 | 可查，不重新 apply | 历史结果不写新身体 | 仅服务器获准诊断 | 可只读查既有本局结果 | 不写新局；旧投影拒绝 |
| 身体动作/当前血蓝/临时控制 | 归操作/Combat 取消合同 | 不在成长重置；生命域复活新 Life、满血蓝/清临时控制 | 核心移除资格 | 核心清理 | 新身体域 |

Host 离开由会话域结束，不执行普通离局后继续游戏路径；
没有断线重连、席位转 AI、Host 迁移或跨局战力恢复。

## 9. 内容参数、单位、取整与生成合同

数值只引用 [内容与数值](CONTENT_AND_BALANCE.md)，以下是消费者合同而非独立数值表。
结构上限来自 Spec；实际金额、秒数、距离、概率、增量与公式常量不在本报告填默认值。
`SourceData\Demo\content.json` 中对应位置：`progression`（`max_level`、`max_choices`、`xp_start`、`xp_thresholds` 等）、
参数 `XP.BaseToNext/IncreasePerLevel`、`REWARD.PlayerXP.Base/PerLevel`、`REWARD.PlayerGold.Base/PerLevel`、
`CHOICE.*` 抽卡参数、`progression.opening_offer/reroll_levels/max_skill_rank`、`SHOP.StartingGold/SellRatio/InteractRadiusCm/UpgradeCost/UpgradeStatBonusRatio`，
`abilities[].draftable`、`attributes`、`equipment[].buy_price`，
营地怪物 `reward_xp/reward_gold`。

| 正式键 | 类型、单位和转换责任 | 本域消费者 / 必须验证 |
| --- | --- | --- |
| `XP.Start / XP.MaxLevel / XP.Threshold[L]`、`FORMULA.XPToNext` | 累计 XP 的有界整数；严格递增阈值，等级合法；运行 int64，算术检查后再存储。 | PrepareExperience；逐级跨阈值、十级 MAX、无十一阈值。 |
| `REWARD.PlayerXP / PlayerGold` | 受害者致死等级为自变量的整数奖励；只用不可变快照。 | CommitDeathReward；重复/互杀/阵亡来源、整奖越界。 |
| `CAMP.Low/High.RewardXP / RewardGold` | 每成员每 Life 的整数；不兼有一笔清营奖。 | 同一 DeathFact；世界只提供映射/定义，不另发资源。 |
| `CHOICE.NewActiveFullSlotWeight / FreeSlotAttributeChance`、`progression.opening_offer / reroll_levels / max_skill_rank`、`CHOICE.<id>` | 满槽新主动权重为基点（0 < 权重 ≤ 1）；自由位属性概率为基点概率；重抽等级严格递增且可达；属性增量按内容量纲；复合项全量预检。 | §6.3 抽卡与重抽、十次界限（含开局一次）、无放回、`DemoGrowthDraft/Reroll/OpeningPool`。 |
| `SHOP.UpgradeCost / UpgradeStatBonusRatio`、`equipment[].passive.upgraded`、生成的 `upgraded_stats` | 整金币；R4 比例；进阶块与原块同一组 handler 同序；固定属性由生成器按 P2/R4/整厘米向下取整算出，原生导入复核。 | Upgrade 事务、`LDDemoEquipment::Effective*`、构筑包络（两件装备至多一件进阶）、`DemoGrowthUpgrade`。 |
| `ABILITY.<id>.Rank[r]`、`SKILL.GrowthPerLevel` | 初级/高级两档的正式效果、目标、法力/时间/距离、伤害与触发规则；固定数值按拥有者当前等级乘 G(L)（`FLDDemoContentSnapshot::FindRank`）。 | Build/Combat；rank 演进、重新学习、等级成长、未选 handler 删除依据。 |
| `ITEM.<id>.BuyPrice / Stats / Passive` | 整数价格；属性单位沿 Combat；被动稳定键；每装备定义来源关系。 | 两实例属性相加、同名被动一次、商店显示与实际能力。 |
| `SHOP.StartingGold / SellRatio`、`FORMULA.SellGold` | 比例按内容固定精度/精确有理值解释，乘法检查溢出，**仅最终卖价向下取整到整金币**；不拿 UI 浮点值结算。 | 初始化/卖出；非负、卖价不超买价、循环无正金币收益。 |
| `SHOP.InteractRadiusCm` | Unreal 厘米；脚点到锚点 XY 距离，世界单点查询；客户端不传可接受半径。 | Open/Buy/Sell 当前资格；边界内外、错误地面/分层。 |
| `CHOICE.attr_hand_out.AttackRangeCmFlat` / `ITEM.item_echo_maul` | 普攻距离厘米增量与获准普攻群攻内容；不外推技能/区域距离。 | 投影、输入接近/Combat 真实射程、横扫去重/来源奖励。 |
| `POLICY.MissingSourceCooldown=ContinueServerTime` | 已确认语义；ReadyAtMs 是本局绝对毫秒，不是可被 UI 重置的剩余时长。 | 失源/重学/死亡。 |
| `POLICY.DeadChoiceCommit`、`POLICY.ResourceMaxChange`、`FORMULA.ResourceMaxChange` | 死时确认和保持资源百分比已确认；精度/取整/边界由内容公式单点维护，不是运行时多模式开关。 | 选择准入、活体一次最终同比换算、循环无取整获益、死亡不治疗。 |

秒到毫秒使用生命周期的唯一转换合同，作者秒值必须能按批准精度准确表示；
不在每个 Vendor/Widget 分别四舍五入。Ready 判定由服务器当前时刻与截止比较，
UI 到零不授权。属性和派生数值的计算类型、上下界、截断与来源快照均沿 Combat 的单一公式。
金钱、XP、构筑、来源与显示格式化不能相互代算。

作者域和生成链以 [内容与数值](CONTENT_AND_BALANCE.md) §10.1 与[数据管线](../systems/WORLD_AND_DATA_PIPELINE.md)为唯一口径：
角色/成长、技能、装备、奖励/价格及政策写在 `SourceData\Demo\content.json`，
由同域严格 `content.schema.json` 和稳定 `native_ids.json` 约束，没有旧V08记录作为第二作者源。
`Scripts\SyncDemoContentData.py` 同步生成 `Content\Lockdown\Data\Demo\demo_content.json`、
同目录的 `manifest.json`，以及原生 `Private\Development\LDDemoContentContract.h`；`ULDDemoContentCatalog`
提供不可变 `FLDDemoContentSnapshot`，经 DataAsset/Bootstrap Validate 链加载，
不拥有另一份角色、金币、HP或运行状态。地图坐标仍归世界作者域，本内容仅引用稳定锚点及世界合同。
未知/缺失ID、非法值、无处理器/图标/触发定义被拒绝。
数值reader（`Scripts\DemoContent.py`）以 Decimal 保留原词法，先检查精度、有限性、范围与量纲，再转规范
定点原生树交 `CatalogTools.encode`；不先转float，encode也不直接接Decimal。
本域不另写校验器或复制数值输入表。
不直接修改生成摘要，不整库替换 V07/V08/Bxx 或产品版本号；游戏不读取 docs。

## 10. 已确认的死亡选择与资源同比规则

| 项目 | 用户已确认的行为 | 不可越过的边界 |
| --- | --- | --- |
| 死亡等待中的确认 | `POLICY.DeadChoiceCommit=Allowed`：允许查看、选卡和明确替换，利用等待整理构筑。 | 只改变持久成长，不伪造活 Pawn，不立即治疗/施法/复活。当前 Life+ExpectedLifePhase 重新确认，旧生命/旧阶段拒绝；交易始终要求存活。 |
| 最大 HP/MP 改变当前值 | 用户原文“保持原来的生命／法力百分比”；引用 `POLICY.ResourceMaxChange` 与 `FORMULA.ResourceMaxChange`。 | 升级、属性卡、装备变化均按旧当前值/旧上限和最终新上限一次换算，不逐来源反复移除/添加；不是保持旧绝对当前值。 |

同比换算由 Combat 的 PrepareBuildChange 准备、ApplyBuildChange 一次应用；
经济/成长不另写 HP/MP 算法，UI/AI 不反向代算。死亡时选择只改持久 Build，
不 Apply 活体 Current；生命域复活以最新构筑满血蓝。
内容采用 `POLICY.ResourceMaxChange=PreserveRatio`（`FORMULA.ResourceMaxChange=FloorFinalRatioRejectAliveUnderflow`）：
权威资源为 int64 百分之一单位 P2，计算旧 Current 与最终新 Max 的 checked 乘除，
仅最终向下取整到 P2。此精度是 2026-11-02 批准的内容公式，不是用户要求精确到某一量化格。
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
选择/购物本身不是进攻意图。失败施法仍不扣蓝或启动技能冷却。

上述行为不再作为待选方案；具体技能/装备/价格/阈值/公式及射程/横扫内容只由内容报告
单点维护。工程类型、版本保护、Permit 和有限故障收口不转成用户逐字段选择。

## 11. 新增、复用、改造与删除结果

本域的替代与删除已完成；迁移过程只在 Git 历史中，本文不保留旧方案档案。

| 架构映射 | 现有落点 | 已真实删除 |
| --- | --- | --- |
| R08 改造，D03 | `Economy\LDEconomyLedger.*`：唯一钱物真相、Prepare/Apply、有界前缀复算与证据导出 | BankTransaction、预算/endowment、quality/transient/parents 与事后 `Record/GoldChanged/Snapshot` 写链 |
| N03 新增 | `ALDEconomyDirector::CommitDeathReward`，Combat 在 DeathsAndRewards 按 DeathKey 调用 | `AwardExperience/RecordEconomyGold`、营地 Contact/Reward/PvPKill、LastOwner 兜底与任意授予入口 |
| N04 新增 | `Development\LDDevelopmentDirector.*`、`LDProgressionRules.*`：持久机会/Offer、窄 Choice、Q/W 替换 | R09 `LDCharacterBuild` 与 `ULDCharacterCatalog`、职业六维/点数/开局技能、整份草稿与站点 hold |
| N05 新增，D15/D16 | `Inventory\LDVendorDirector.*`（会话/报价/Buy/Sell/回执、世界资格）、`LDInventoryDirector.*`（两槽投影） | 书店 Vendor 与 `LDVendorTypes`、背包/重量/Quick/十一绑定/掉落整理、`LDInventoryTypes`/`ALDItem`、`LDGearRules`、`LDLoadoutRequest`、V07/V08 获取/装备/战术目录 |
| R07/R09 来源 | Combat 唯一持有来源冷却轨道与回绑（`PrepareBuildChange/ApplyBuildChange`） | `LDV08Loadout`/`LDV08Modifiers`、ItemFamilies 共享主动 CD、施法伪库存修订 |
| D16/N07 RPC 与投影 | `Framework\LDPlayerGrowth.cpp`、`FLDOwnerState.Growth`；HUD 的 Choices/Shop 面板 | 旧背包请求 RPC、`LDPlayerBank/Commerce/Production/Development/Vendor.cpp`、OwnerState 的 Build/Inventory/Gold/Xp |
### 11.1 数据、adapter、生成与资产退出

旧链已按原子闭包退出：R09 构筑与 `ULDCharacterCatalog`、V08 Loadout/修正器、V07/V08 装备/获取/战术目录
与 B81–B84 合同连同最后使用者同批删除；同一集成批次删除 `DA_{Character,Acquisition,Combat,Equipment}Catalog`、
`Content\Lockdown\Data\V07`/`V08` 载荷、`SourceData\Catalogs\V07`/`V08` 输入、SyncB84/TestB84/SyncV07* 脚本
与 `Lockdown.Build.cs` 的旧 RuntimeDependencies，删除前完成 AssetRegistry 引用审计。
本域内容只经 `SourceData\Demo` → Demo 载荷/root manifest/contract → `ULDDemoContentCatalog` 一条链读取；
缺新输入明确失败，不回读旧目录，不保留 fallback、第二作者源或备用旧产物。
反射类、WBP/CDO/DataAsset/map、动态加载和 `.uasset/.umap` 删除须 C3：
以当前入口/角色/装备/技能/root widget 为根检查 AssetRegistry 硬/软/管理引用，
另核对 C++/配置拼接加载；旧类可加载时先迁移、编译、重存存活资产。
随后 UHT、Editor/Game 构建、Bootstrap Validate、Cook 和新包 staged/AssetRegistry 核验。
**文本零引用不证明二进制可安全删除。**

### 11.2 测试与文档迁移

| 当前入口 | 保留/改造与删除 |
| --- | --- |
| `Lockdown.Rules.DemoGrowth*`（[LDGrowthRulesTests.cpp](../../Source/Lockdown/Private/Tests/LDGrowthRulesTests.cpp)） | 线编码、精确条款、原子拒绝、overflow、账本守恒/压缩/退休与证据导出、请求窗口、等级/候选/替换与本人视图白名单；旧 Economy/Inventory/V08/Loadout/Character/Preparation/Skill 专测已退役。原生创建失败、过期回执、满容量、越界和生死竞争仍须 §12 A16/A17/A24/A25 的 N05 场景。 |
| 拥有连接场景（[RunDemoGrowthCheck.ps1](../../Scripts/RunDemoGrowthCheck.ps1)，`LDDemoGrowthProbe`） | 参与对局的 Listen Host 与一个真实拥有客户端：开局三选一、击杀奖励、选择与商店交易经各自连接完成，并用 `CheckEconomyEvidence.py` 复算账本证据；纯规则和直接调用不代表真实联机结果，不为旧场景保留服务壳。 |
| [RunTests.ps1](../../Scripts/RunTests.ps1)、[CheckRuleReport.py](../../Scripts/CheckRuleReport.py)、[CheckEconomyEvidence.py](../../Scripts/CheckEconomyEvidence.py) | 随提供者迁移维护明确的实际必需用例集合及源码注册一致性，保留 failed/notRun/inProcess/缺报告失败；账本证据 schema 5 由 `LDEconomy::ExportEvidence` 导出，独立复算前缀+尾部的钱物/奖励/退休并核对当前状态，DEV 成长夹具的 `DevFixture` 铸币与 DEV 测试重置的 `DevReset` 条目只在 `--dev-fixture` 时接受；旧 budget/quality/parents 分支已删除。 |
| D29–D34 本域专测/手册 | 必要 Ledger/Inventory/来源不变量已迁入上列规则与 Combat 来源/属性规则，旧生产者与专测已删除；系统手册只描述真实接口，不保存废弃方案。 |

本域规则测试落在现有 Tests 体系，不新建测试框架，也不保留旧领域只是为了旧测试可编译。

Growth 场景的准备行走保留随机出生与正常身体碰撞：对练地点取作者地形高度并校验原生导航起点，
双方站在同层平地、商店和喷漆区外，避开公开 Boss 巡逻点。准备路线使用作者营地家位置选择有界绕行点，
每段仍须通过已注册地面与完整原生路径检查，再由各自拥有连接发送普通移动；不读取隐藏单位实时位置、
不传送、不清除野怪、不改变正式游戏的动态避障规则。绕行失败明确结束场景，不重复同一路线掩盖阻挡。

## 12. 验收要求

下表是本域验收要求，不是测试名称清单；当前已实现的检查入口见 §11.2，
未被这些入口覆盖的条目不能当作已通过。
除明确的合成规则夹具外，实际金额、时间、边界与容差取批准内容参数。

| 用例 | 前置、动作与精确断言 |
| --- | --- |
| A01 从零发育 | 正常菜单开局，仅近战单体普攻；真实击杀低档野怪得到一次 Gold+XP，完成首次选择和真实购买，无调试授予/传送。 |
| A02 最后一击/归属 | 两来源命中同一目标，分别测主动、被动/持续和普攻副命中；只有真实致死所有者收全部奖励。无主不能回落上次攻击者。 |
| A03 死者/互杀/离开 | 同Effects集合双死先锁完整致死等级/资格再奖；后续因果集合用当时新等级。先致死后离局/重试仍沿原资格，离局先则效果取消；同ceil桶真实先后不混，清理不产击杀。 |
| A04 DeathKey 去重 | 重放相同事实，继而改变 Source/Effect/DeathSerial/BodyIdentity；最多一次结果，改条款报冲突不改收款人；合法 NoReward 也不能改源重领。 |
| A05 营地生命周期 | 每成员每Life一次；回位途中可真实致死并按旧Life结一次，到家仅存活者Reset满资源/清临时/CD就绪，不复活死者、不增Life/发奖；刷新不二次奖励，旧Life不命中新生体。 |
| A06 奖励边界 | 每级阈值前/恰到/越过、一次多级至10、延迟十次（开局一次加九次升级）；开局机会与逐级机会准确、无第十一次；满10后XP继续累计但不升级/不授机会。 |
| A07 整奖失败 | 分别注入账本、成长、Combat、Offer工作区、回执/outbox准备失败及整数越界；所有钱/XP/等级/机会/RNG/修订不变，无半奖或 NoReward fallback。 |
| A08 重试/故障 | 必需奖励暂失败时只在下一边界重试一次；仍失败经 `FaultAuthority` 明确故障关闭且无正常胜者；必需奖励未决时 Capture 完成与 Host 结束都不越过它：同刻完成先经结算边界重试再锁定，Host 结束返回 `RequestInFlight`（`Lockdown.Rules.DemoCore.RewardSettlementBeforeClose`）。 |
| A09 Permit | 错 Match/Batch/Phase/GateRevision、跨 callback/下一批旧许可在第一次 mutation 前拒绝；子 Apply 不会半途正常失败。 |
| A10 持久三卡 | 连升/关闭重开/死亡/重复查询不改 OfferId/OptionId/内容/RNG；只有本次选择应用后才生成下一组；不打开窗口也已固定队首。 |
| A11 合法池 | 对当前目录的技能与属性卡池真实可达生成状态及全部两件装备组合验证；满 Q/W、满rank、所有替换分支、属性复选、复合增量全可兑现且三卡互异。 |
| A12 替换遗忘 | Q为高级旧技能、W占用；只点新卡/取消不消费，明确替换后旧rank遗忘、新主动为初级，再学从初级开始但旧截止保留；旧确认不能替另一技能。 |
| A13 选择竞争 | 两请求同Offer、选择与买卖/升级竞争；最多一次消费；stale返回同卡及新保护，不用旧整Build覆盖新CD。 |
| A14 生死选择 | 死前Alive确认不能借同Life在Dead应用，死时新确认允许且不需要活Pawn、不治疗尸体，复活旧确认拒绝，机会不丢。 |
| A15 两装备/真实例 | 两次显式购买同款产生两个实例，第三件拒绝；满槽先Sell后Buy，后Buy失败不回滚真实出售；无隐藏背包。 |
| A16 原子买卖/新修订 | 钱不足、旧价/旧库存/旧实例、创建/投影/绑定失败均无扣钱/物品泄漏；统一 ItemRevision 不混用旧物理与Ledger计数。 |
| A17 商店同刻 | 开店/买卖仅Transactions借当次Permit调用正式世界查询，拒跨栈/旧许可；同ceil桶内不同真实子时刻用对应运动证据。移出范围/死亡/复活/终局/离局均重验，几何通过后最终安装仍复验，无迟到成交；受击本身不禁止。 |
| A18 双属性/单被动 | 同款两实例属性相加、同名被动一次；卖一份不断另一份、不重置触发，卖最后一份移除贡献；装备被动不消耗成长选择。 |
| A21 CD生命周期 | 换槽/升级/遗忘重学/死亡保持截止；已释放独立效果仍续原期限且不再查原持有，维持型按条件取消，正式离局取消独立效果。 |
| A22 当前血蓝 | 买/卖/升阶/原子替换后保持原资源百分比，仅按最终投影一次；覆盖往返、精度、零上限/最小正值及真实伤害夹在两次交易之间，不能量化套利或治疗尸体；正常复活满新上限。 |
| A23 射程与群攻 | attr_hand_out与echo_maul在服务端真实改变普攻范围/获准副命中；不扩大未指定技能距离；双自拍杆不重复触发横扫，副命中奖励仍恰一次。 |
| A24 回包/过窗 | 同ID同条款重放、异条款拒绝、64窗外拒旧+同步；先快照后回执/先回执后快照均不回滚当前状态；双击Pending不创建第二业务。 |
| A24b 压缩/退休/查询 | 先折叠64条再提交65/66；退休有独立serial和 `FRetireAccount` 条目，多轮压缩后余额/Retired/修订/RetireStamp可复算，旧请求/实例/死亡拒旧、pin不释放。旧局同号query=InvalidContext；各LookupStatus的包装结果不伪造终态Receipt。 |
| A25 终局/新局/隐私 | 关闸后无新奖励/选择/买卖/来源，只读旧回执；新局清局内域/旧ID。可见对手装备定义失视野撤销；结果只含批准名称/K/D/FinalLevel/赢家/原因/用时，无私有Instance/修订/技能/XP/金币/Offer/CD。 |
| A26 删除与生成闭包 | 批准定义→schema/adapter/loader/handler/UI/资产/测试一致；旧RPC/服务/载荷/专测无存活消费者，保留稳定ID有依据；UHT/build/Bootstrap/Cook/包清单无悬空引用。 |

网络、成长、交易和胜负至少使用**正常参赛且可见的 Listen Host + 真实拥有客户端**，
最终整合覆盖 Host + 两拥有客户端 + 三 AI；注入延迟/抖动/重复/乱序时同时核对服务端回执
与客户端投影。直接调用 `_Implementation`、Host 全知调试或旧无渲染 probe 不能替代这些证据。
LAN 与正确配置的公网直连分别验证，输出写忽略的 Artifacts。

### 设计参考

玩法取舍参考 [Riot Games：Gizmos & Gadgets Learnings](https://teamfighttactics.leagueoflegends.com/en-us/news/dev/dev-teamfight-tactics-gizmos-gadgets-learnings/)，
仅用于区分“组合规则合法”与“选择真正有取舍”，不照搬其内容/数值。
规则测试与拥有连接场景通过只证明其覆盖的条目；不宣称平衡、完整 §12 或完整 Demo 已验证。

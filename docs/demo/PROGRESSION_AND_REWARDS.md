# Demo 成长与奖励

**目标：v0.11.0 · 架构职责：M6 / M7 · Unreal Engine 5.8.4**

本报告将 [App Spec](../APP_SPEC.md) §4、§6–10 接到
[架构 R09、N03/N04](ARCHITECTURE.md) 和
[共享契约 SC-01–09](SHARED_CONTRACTS.md)。实施依赖见
[实施计划 I4](IMPLEMENTATION_PLAN.md)，当前组件知识见
[战斗与成长](../systems/COMBAT_AND_GROWTH.md)、
[成长事务](../systems/GROWTH_TRANSACTIONS.md)。

本文是该模块的设计与实现合同，实现落点见 §2；类型与函数名以当前源码为准，
§12 是验收要求，不是已通过的测试清单。
数值、公式和首版目录只由 [内容与数值](CONTENT_AND_BALANCE.md) 维护；本文引用其正式键，
不建立第二份经验、效果或平衡表。跨域身份、时钟、批次和终局由
[会话与对局生命周期](SESSION_MATCH_LIFECYCLE.md) 单点定义，战斗投影由
[战斗与属性](COMBAT_AND_ATTRIBUTES.md) 定义，世界查询由 [世界、营地与视野](WORLD_CAMPS_AND_VISION.md) 定义。

## 1. 结论与确认规则

**一份参赛者成长、一条 Combat 来源链。**
XP、等级、属性点、主动机会与待学图纸由 Development 持有；装备、金币与商店已按 [#144](https://github.com/rlsn/Lockdown-alpha/issues/144) 删除。
成长规则按 [#198](https://github.com/rlsn/Lockdown-alpha/issues/198)（2026-11-10 批准）改为五维属性点与单级主动，产品规则见 App Spec §4.2–§4.4；
本域已接入其第一部分、第二部分（B：材料实例、开局与升级的基础构件 B01–B18 及其安装）与休息服务（NPC、洗点、替换、遗忘，见[成长事务](../systems/GROWTH_TRANSACTIONS.md)）；
休息时刻见[会话与生命周期 §3.4](SESSION_MATCH_LIFECYCLE.md#34-休息野外进度与阶段边界198)，改装件/连接件与 PK 由 #198 后续部分接入。
死亡奖励和成长命令各自在同一权威批次内复合提交：
先准备成长与 Combat 投影，全部成功才一起应用，不先改 OwnerState 再补写。

| 已确认的产品约束 | 本域必须落实的结果 |
| --- | --- |
| 所有人初始近战、单体普攻；普攻可成长（距离、元素、强化、反应范围等），不要求群攻（2026-11-10 用户决定） | 初始构筑不授主动；获准的攻击距离、元素与强化进入同一 Combat 投影，不是只有面板数值。当前运行目录只有强化普攻主动。 |
| 1 至 16 级；开局 3 属性点与一次开局主动三选一，每次升级 3 点，3、6 级各额外一次主动三选一；16 级后按 XP 继续给点 | 开局点数与机会只在新 Match 授予一次，复活不再授予；累计 XP、等级、点数与主动机会同一提交；连升排队，满级后继续累计 XP 并按阈值给点。 |
| 开局主动选定后自选一个当前可用基础构件；每次正常升级（2–16 级）给一件普通构件，先按等级档位抽类别再在未持有定义中等概率取，空类别按剩余权重归一化，普通池耗尽改给 3 点（App Spec、AI 规范 §4.1–§4.3） | 开局基础构件整局一次，候选为常驻起手加开局主动解锁的，排除已持有；升级材料与 XP 同一提交、按本局私有种子抽取，满级后的 XP 不给材料；死亡与重连不重发、不洗首次安装历史。 |
| 构件的首次安装、搬动、卸下与重装旧件随时可做（#222 用户决定：“all the time 可以学新技能和加点 all the time”，取代 AI 规范 §5 的脱战安装窗与“旧件只在休息”） | `LDProgression::InstallGate` 与学习同一闸门：对局进行中（野外含交战、休息每个步骤含 PK）、存活或死亡都受理，只有阶段关闭为 `PhaseClosed`；`RestOnly`（63）已退役。 |
| 主动候选互不重复，排除已学与待学图纸；技能只有一级 | 开局取开局池，3/6 级取全部主动；只有 1–2 个合格时如实显示，为零时机会排队；没有重抽。 |
| Q/W/E 三角色主动；选中即成待学图纸，可学习且有空槽时同次学到 | 学习与加点没有构筑安全窗（#222）：有空槽就学到，Q/W/E 已满才存为图纸；满槽在野外不能替换（替换与遗忘只在休息的配装与派奖期间），新主动保留为图纸；遗忘不退款、不保留备用库。 |
| 失去来源期间继续计时，重持有沿用原结束时间 | 遗忘、换槽、死亡不缩短已开始的 `ReadyAtMs`；重学承接原记录的截止时刻。 |
| 已释放独立效果不因普通死亡或遗忘而取消 | 继续原定期限；依赖持续持有/施法的光环、被动、引导按维持条件失效。正式离局取消未结束独立效果，但不追溯改写既成死亡事实。 |
| 野怪的 XP 归最后一击实际所有者；参赛者击杀另有助攻（#112） | 合法击杀一次结清：参赛者受害者的 XP 由击杀者与助攻者平分（App Spec §4.1）；阵亡但仍在本局的来源可以收奖或得助攻。无主、自杀、回位、离开不产生额外收益。 |
| 死亡不丢等级、XP、属性点、构筑、图纸或待选；新局清局内状态 | 持久状态不放在 Pawn / Widget；死亡取消身体期请求，不重发初始资源。 |
| 死亡等待可以选择、学到空槽、加点与安装构件，只更新保留构筑（#222） | 不提前复活、不治疗尸体；复活后学到的技能仍在；当前 Life 和生/死确认阶段重新校验。 |
| 最大 HP/MP 改变时保持原生命/法力百分比 | 活体只按一次最终属性投影同比换算；不采用保持原绝对当前值的规则，精度由内容/Combat 单点定义。 |

以上失源继续计时已获用户确认，不保留共享冷却或冻结计时备选。
内容与数值以 2026-11-02 批准的首轮方案与 2026-11-10 批准的 #198 目录（[内容与数值](CONTENT_AND_BALANCE.md)）为准；
资源同比换算的精度与边界只由内容/Combat 的唯一公式定义。批准输入与规则测试都不代表真实联机或平衡已通过。

## 2. 现有实现与已退出的旧链

本域实现于 `Development\LDDevelopmentDirector`、`LDProgressionRules`、`LDGrowthProtocol`、`LDGrowthRequests`，
`Development\LDGrowthDirector`（死亡奖励提交）、`LDGrowthProviders`
与 `Framework\LDPlayerGrowth.cpp`；DEV 成长夹具为 `Development\LDDevGrowthFixture`。接口、生命周期与检查入口见
[成长事务](../systems/GROWTH_TRANSACTIONS.md)，构筑安装与来源冷却见
[战斗、效果与成长](../systems/COMBAT_AND_GROWTH.md)。

旧实现已整体删除，不作为现状、兼容入口或数值来源：旧 R09 构筑 `LDCharacterBuild`
（构造器授技能、`AwardXP` 发点数、整份草稿 `Prepare`）与 `ULDCharacterCatalog`；V08 `LDV08Loadout`/`LDV08Modifiers`、
`LDGearRules`、`LDInventoryTypes`、`LDLoadoutRequest`（背包/Quick/多绑定/六动作/family 共享锁）；
V07/V08 装备、获取与战术目录及 B81–B84 合同；先 `Adopt`、改 OwnerState 金币再补账的旧背包链与书店 Vendor；
#144 删除的金币账本、两装备槽、装备目录与进阶、商店会话与买卖（§11）；旧营地 Actor 的分步奖励（N03 在核心 DeathKey 上统一提交）。
对应旧专测已退役，由 `Lockdown.Rules.DemoGrowth*`、Combat 来源/属性规则与 `RunDemoGrowthCheck.ps1` 拥有连接场景承接。
#198 第一部分同样真实删除了混合三选一（学习、升级、被动、属性卡与开局方向卡选项）、两档技能、属性卡与品质、重抽、
全部被动目录与抱头装怂，不保留兼容壳或备用模式。

**提交后半段不分配。** 成长与 Combat 的 Prepare 在副本上构造完整后态，Apply 只移动；
请求与奖励回执窗口均为 64 条有界拒旧（§4）。

## 3. 唯一状态、正式类型与版本职责

本节签名均落在现有组件；不新增通用 Manager、
第二份技能状态或世界事件平台。

| 唯一写入者 / 类型 | 真相与限制 | 只读消费者 |
| --- | --- | --- |
| `ALDDevelopmentDirector` 修改参赛记录中的 `FLDGrowthBuild` | Level；五维各自的已分配点数；Q/W/E 已学主动；待学图纸（不含已学定义）；材料实例（每定义至多一件，含来源、安装、首次历史与绑定）；BuildRevision。奖励与成长命令只改其获准字段。不得接客户端完整 Build。 | Combat 准备投影（只含已安装组件）、自身 UI、该参赛 AI。 |
| 同一参赛记录的 `FLDProgressionState` | `int64 CumulativeXP`、ProgressionRevision、主动机会队列（已获/已消费）、当前至多一组主动候选（最多 3 个定义）及每次机会的处理记录、材料池耗尽改给点数的升级次数；随机材料由本局私有 `MatchSecret` 按参赛者与机会序号或等级派生（`LDProgression::OfferSeed`、`ComponentSeed`）。Level 只在 Build 存一次。 | `FLDProgressionView`；不复制给其他参赛者。 |
| Combat 写参赛持久 `FLDCombatEntityRecord` | 已安装构筑投影与 `Installed{BuildRevision}`；`Tracks`（`FLDCombatTrack`：`IssuedCooldownId`、来源定义、ReadyAtMs）与 `SourceRevision`；交战窗口的最近交战时刻（跨生命保留，只供 HUD 交战折叠）。成长只经 `PrepareBuildChange` 提交完整构筑输入。 | Q/W/E、D、属性视图、AI 自身读模型（`FLDCombatOwnerView`）。 |
| `FLDOwnerState` / 各 View | 上述真相的一次一致投影，携带 Match/Life 和发布修订；不得反向写成长或 Combat。 | 拥有连接；GameState 不持有全体私有构筑。 |

主动机会是一条有序队列：1 号是开局机会，由成长 `BeginMatch`（Progression 提供者 Prepare、开局休息 R0 之前）为每名参赛者（真人与 AI）授予，
同时给开局点数；达到 `progression.active_offer_levels` 的每个等级各加一个机会，与该次奖励的等级变化同一提交。复活从不再授予，新 Match 整体重建。
队首有合格主动时生成并保留一组候选，其余机会排队；没有合格主动时机会保留、不生成候选，构筑变化后重新判定。
待处理机会数从队列计算，UI 不保存另一可写次数；每次机会的处理记录整局保留，不能因回执窗口淘汰而丢失“这组候选已处理”的证明。

属性点不单独存储：已得点数由等级与累计 XP 按[内容与数值 §4.1](CONTENT_AND_BALANCE.md#41-五维属性与成长参数)的公式派生，
已用点数是五维点数之和，未分配点数是二者之差；出现负值是权威故障，不能截断成零。

候选由服务器在对应机会生成时一次建立，只包含定义身份；效果由服务器按当前目录解析。
候选身份不等于显示顺序或翻译名，客户端只能引用服务器给出的候选与选项身份。

| 操作 | 必须推进的修订 / 不得推进的内容 |
| --- | --- |
| XP 增加但未升级 | ProgressionRevision（满级后跨过给点阈值也只推进它，点数由 XP 派生）。不得因读 XP 重建 Build。 |
| 奖励升级 | ProgressionRevision、BuildRevision；可能新增主动机会；已有候选不换组。 |
| 成长命令成功（选择、学习、加点） | ProgressionRevision、BuildRevision 与处理记录；学到主动或加点时 Combat 随新构筑安装推进自己的来源修订。 |
| 主动释放/效果触发 | Combat 的 readiness / 动作修订；不是构筑结构修改，不能使成长命令因每次普攻持续过期。 |
| 秒表流逝、查询、关窗、回执重放 | 不改变构筑、机会或来源绑定版本；UI 从绝对截止推显示。 |

### 3.1 固定标量类型与编码

下表是当前协议的确定类型；所有修订均为 `uint64`，不截断回旧字段，不保留旧 width 兼容。

| 字段族 | 唯一类型 / 编码 | 初值、限制与换代 |
| --- | --- | --- |
| Generation / Life | `uint32`，沿核心编码 | 仅核心推进，零值不接受为有效比赛/已建立生命；到上限明确失败，不回绕。LedgerMatch仍是FGuid。 |
| ContenderId / LedgerMatch / OfferId / OptionId / IssuedCooldownId | `FGuid`，UE内置四个uint32的16字节编码 | 有效ID非零；可选项单独带存在标志，不拿零GUID当成功默认。Offer/Option由服务器一次建立；CD每技能定义一份。新Match无旧执行权。 |
| RequestId / CommitBatch | `uint64` | 请求每Family从1开始，只在新Match重置；死亡不重置。CommitBatch 是提交所在权威边界的 `BatchSerial`。0不表示一笔成功业务。 |
| Build / Progression / Offer revisions | `uint64`，包括所有Expected/Guard引用 | 匹配初始化或对象建立完成后基准为1，只由相应唯一写入者checked递增；计时显示不逐帧增修订。不能截断后比较。 |
| Level / OpportunityOrdinal / SlotIndex / 每项加点数 | `uint8` | Level=1..16、机会序号从 1 起、技能槽=0/1/2；队列计数可为0。空槽用可选存在位，不伪造空技能记录。技能没有等级字段。 |
| XP / P2 Current与Max | `int64` | XP非负；资源域依内容/Combat；任何乘加/存储越界在prepare拒绝。 |
| ReadyAtMs / AtMs / 运行时毫秒期限 | `int64` | 当前Match绝对时间；ReadyAtMs=0仅表示未开始CD的初始就绪，无负期限。原始事件秒为核心`double OccurredAtSeconds`，不得用毫秒桶代替先后。 |
| `FLDDemoContentDigest` | `uint8 Bytes[32]`，SHA-256，wire固定32原始字节 | 不传FString/变长hex；日志/展示是64个小写十六进制字符，边界验证后转换。本局固定，不因开面板换摘要。 |
| 本域技能 DefinitionId | 服务端`FName`；wire 为 `FLDDefinitionWire`（与Combat同一编码）：固定64字节UTF-8缓冲，最多63字节内容，NUL终止且余字节零填充 | 当前批准ID为`[a-z][a-z0-9_]*`ASCII子集；先验证UTF-8、终止/填充、长度、规范与catalog再构造FName。未知/非法拒绝，不截断。效果参数由服务器解析。 |
| Kind / Family / Outcome / Reason / bool | 领域Kind/Family/Outcome为`uint8`枚举，Reason为`uint16`，bool wire仅0/1 | 唯一领域头文件声明明确支持值；decoder逐值白名单，不用历史最大枚举包住废弃值。可选值编码为存在位+存在时的值。成长命令只有一个命令族，不能伪造Reward RPC。 |

Permit 和 ELDLifePhase 直接引用其拥有域正式类型，
不在本域另声明同名身份。`ExpectedLifePhase`只接受核心Alive/Dead；其余状态明确拒绝。
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
| `FLDMatchContext{uint32 Generation; FGuid LedgerMatch;}` | GameMode 建立本局映射；网络核验 Generation，DeathKey 使用 LedgerMatch；新局二者一起换代。 |
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
奖励不拿 RequestId 冒充生产者序列。
世界的 CampId/MemberId/CampGeneration 只映射同一 Victim/Life，不有第二条 MemberLife 或发奖链。

### 4.2 确定批次及 Prepare / Apply

唯一阶段顺序为：

`Session → Motion → Actions → Effects → DeathsAndRewards → RespawnsAndWorld → Transactions → Capture → Close → Publish`

奖励只在 `DeathsAndRewards`；成长命令（主动选择、学习、加点）只在 `Transactions`。
同刻死亡、新生命与离局已按前置阶段生效后再重验成长命令。
同批奖励可以使随后旧 BuildRevision 的操作被拒；不能偷偷替调用者改成新版本。
本批合法成长命令在 Capture 前提交，正常胜利关闭后仅保留结果，不追回已提交的命令。
新成长不能反向重算此前 Effects 的伤害。

```cpp
FLDRewardReceipt ALDGrowthDirector::CommitDeathReward(
    const FLDCommittedDeath& Death,
    const FLDGameplayWritePermit& Permit);

FLDPrepareGrowthResult ALDDevelopmentDirector::PrepareExperience(
    const FGuid& ContenderId, int64 GrantXP) const;
void ALDDevelopmentDirector::ApplyPreparedGrowth(
    FLDPreparedGrowthChange&& Prepared, const FLDGameplayWritePermit& Permit);
```

调用方在同一提交段内先 `ValidateWritePermit`，再准备并应用成长与 Combat 投影。
Combat 配合的正式入口为 `PrepareBuildChange(Caller, FLDBuildProjectionInput,
FLDExpectedBuildVersions) -> FLDCombatPrepareResult` 和
`ApplyBuildChange(FLDPreparedCombatProjection&&, const FLDGameplayWritePermit&)`。
调用者是服务器解析后的本局参赛者，不是客户端自选账号；`FLDGrowthBuild`
是唯一构筑。提交许可只有上述核心类型，Apply不对RPC开放。

| 步骤 | 必须执行的操作 | 不得发生 |
| --- | --- | --- |
| 查重/资格 | 对照核心原始死亡或领域请求条款；核验 Match、成员、原因、定义和现有结果。 | 为重试换 DeathKey/事务 Cause；把失败当合法零奖。 |
| Validate | 纯算 XP、累计阈值、等级/点数/机会、最终 Build、来源；检查全部预期版本、整数和修订溢出。 | 先写 XP 再发现来源/效果非法。 |
| Stage | 准备成长字段 patch、Combat 投影/绑定、机会/候选工作区、最终回执和发布 outbox。所有容器与嵌套 payload 复制/容量在此完成。 | 提前发行 IssuedCooldownId、Changed/SyncOwner。 |
| 最终重查 | 同一服务器线程重查 Permit、全部 prepared 依赖版本。进入不可重入、无等待的连续提交段。 | 保存 Permit 到 Pending/Offer/下批，跨 callback 使用旧许可，允许外部修改 GateRevision。 |
| No-fail Apply | 仅移动已准备数据/交换存储、有界赋值；同时应用成长、Combat、来源失效、机会和终态回执；只替换本域字段，不覆盖新 readiness。 | 分配、加载、RPC/Delegate/OnRep、可失败 Actor 操作或第二次业务校验后部分返回。 |
| Publish | 子结果均封存后生成一致 owner 投影和回执；按共同 Publish 阶段发出。 | 先公开新等级旧机会，或用客户端 OnRep 到达顺序当原子提交。 |

子 Apply 的 Permit 检查在该连续段内必须保持恒成立；如果成长已改变而后续子 Apply
还能正常返回版本/许可失败，设计并未满足 no-fail，不能把它记录成成功。
预分配并不等于假定 UE OOM 可捕获；只对项目能够报告的准备失败提供恢复。
引擎级 OOM、进程崩溃或断电的跨进程恢复不属于 Demo；不能新增世界存档系统补这项承诺。

### 4.3 奖励结果、水位与有界故障路径

| `FLDRewardReceipt` 状态 | 含义与记录 | 后续处理 |
| --- | --- | --- |
| `Granted` | 同一死亡的 XP/等级/新增机会及必要投影全部提交；带原事实指纹、实际增量、等级前后和结果修订。 | 可以封已处理事实、推进水位；同条款只读重放。 |
| `NoReward` | 核心合法死亡事实明确无奖：无主、自杀、实际致死时已离局来源或非奖励对象等有类型化原因。不是回位产生的假死亡。 | 同 Key 记已处理，不能后来补来源发奖；可推进水位。 |
| `RetryableNotCommitted` | 必需奖励合法，但准备资源暂不可用（如 Combat `CapacityExceeded`）；XP、等级、机会、随机状态均未提交。 | Combat 保留同一事实，在下一个权威边界的 DeathsAndRewards 优先重试一次。 |
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
因此完成时刻仍精确，终局后不再产生战斗、选择或新死亡，奖励先于结果结清；
重试仍失败则照上文权威故障、无正常胜者：故障诊断写明该死亡未支付，保留的完成提案随故障一起放弃。
若写闸被其他故障关闭时仍有奖励留在重试列表，Combat 记录 `LD_REWARD_UNPAID_AT_FAULT` 并丢弃，不后台补发；
新局的 Combat 与完成提案随旧局退休。

离局的身体/效果立即清理，但本局参赛记录不能先删除到无法结算。
关闭新命令权限与偿付既成事实是两件事；这不是离局后新发奖资格或断线重连能力。

奖励只在 DeathsAndRewards、成长命令只在 Transactions 内提交；不得在 Publish 中再施法/发奖。

### 4.4 去重和有限留存

- 成长命令使用本局参赛者范围的单调 `uint64 RequestId`（`TLDGrowthRequestStream`）。
  每条流只允许一个未决变更，另一新序号返回 `RequestInFlight`；未来序号缺口明确 `OutOfOrder`，不缓存无限乱序请求。
  受理即固定规范化 payload；最终成功和拒绝都消费该请求序号。
- 同 ID 同 payload 返回相同 Pending 或最终回执；同 ID 异 payload 为 `RequestConflict`。
  双击 Pending 按钮复用原请求，不新造第二次提交；再次提交需前次终态和当前版本。
- 每条流保留 **64 份最终回执窗口**（`WindowCapacity`）作为工程限额，另保存 FinalizedThrough 水位。
  超窗返回 `ReceiptExpired` 与合法当前状态，不重执行、不猜测历史成功。
  窗口不是成长结果历史：每次主动机会的处理记录整局保留。
- 死亡回执使用 DeathKey 与原始事实指纹。`ALDGrowthDirector` 按 `(Victim.Kind, Victim.EntityId)` 保存
  `ResolvedLifeThrough` 与最近 64 份奖励回执；同 Key 同指纹重放原回执，异指纹为 `DeathConflict`，
  旧 Life 无回包时为 `StaleDeath`，不能当第一次死亡。
  改 Source/Effect/BodyIdentity 只会与原事实冲突，不能产生第二份奖励。
- 所有参数/定义集合、活实例、来源 ID 和 pending 容器均有明确上界。
  发布、审计和请求序号溢出也是准备错误，不靠整数回绕或静默饱和维持“无限对局”。

**两个准入屏障必须在成长 Prepare 之前生效：**
请求序号不大于 FinalizedThrough 时仅查已有回包/拒旧；DeathKey 的 Life 不大于对应
ResolvedLifeThrough 时仅重放/拒旧。无奖死亡可独立封结果，无构筑变化的请求也不造空提交。

## 5. 奖励与经验：一次致死到一组完整结果

`CommitDeathReward` 只从核心 committed-death inbox 接收事实，不作为客户端/AI RPC。
资格先看受害对象与关系，再看实际所有者；另一参赛者或野怪的合法击杀才发奖。
持续效果、被动触发、获准普攻副命中使用 Combat 已锁定的真实所有者；
抑制副命中的吸血/递归触发不抑制其合法死亡奖励。

| 输入 | 期望结果 |
| --- | --- |
| 活参赛者击杀野怪 | 全部奖励归该所有者，无助攻、距离、伤害比例分配。 |
| 参赛者击杀另一参赛者且有助攻者 | 致死瞬间冻结助攻者名单（`FLDCommittedDeath::Assisters`，见下）；同一 `CommitDeathReward` 复合提交里 XP 由击杀者与助攻者平分，各自向下取整；全部准备成功才一起安装，同一死亡只结算一次。 |
| 无击杀者的参赛者死亡 | 不发奖励，也没有助攻者。 |
| 两个参赛者同批互相致死 | 两个不同 Victim DeathKey，各按各自致死快照和仍 InMatch 的所有者收奖。 |
| 已阵亡来源的存续效果合法击杀 | 该实际致死时原参赛者仍在本局则全额收奖，不能因无 Pawn 丢 XP 或改发兜底账号。 |
| 无主致死、自己杀自己 | 合法 NoReward，同一死亡仍封已处理；不退回历史攻击者。 |
| 回位/重置、断线移除、Host 结束清理 | 不制造击杀事实，不给 XP，不把原旧 LastOwner 回调送奖励。 |
| 致死与正式离局先后 | 致死在先则资格/归属锁定，后来离局不改既成结果；离局在先则立即移除身体并取消其独立效果，不再产生后续击杀。原始时间先于毫秒桶/phase。 |
| 营地成员死亡/整组清空/刷新 | 每成员同一 Life 只一次；清空/刷新仅世界生命周期，不再发一份清营奖。 |

**助攻资格与记录。** 唯一的伤害/状态施加路径（`ALDCombatDirector::ApplyHit` 的已提交正伤害——含被护盾吸收的部分——或定身、缴械、眩晕、减速；
`ApplyTauntEffect` 的嘲讽；击退、沉默、破防与灼烧等在其施加成功处）对参赛者受害者调用
`ALDCombatDirector::RecordAssistContribution`，按效果的实际所有者（CreditOwner）在受害者这条生命的身体上记下最近贡献时刻
（`FLDBodyCombatState::AssistContributions`，纯规则 `LDAssist::RecordContribution`），换命随身体一起消失。
`LockDeath` 在击杀者合格时用 `LDAssist::SelectAssisters` 选出死亡前 `REWARD.AssistWindowSec` 内（含边界）有贡献、仍在本局、
不是击杀者或受害者的参赛者，排序后冻结进死亡事实；死亡中的参赛者仍可得助攻。分配用 `LDAssist::SplitKillReward`。
复合提交为每名收款人准备 XP、等级与机会变化，全部准备成功后一起安装。回执 `FLDRewardReceipt` 的自身字段是击杀者那一份，
`Assists` 按事实顺序列出每名助攻者的 XP、等级与机会变化。
| 营地回位到家 Reset | 仅仍存活成员满血蓝、清临时状态和冷却就绪；不复活死者、不创建新Death/Life、不发奖。回位途中真实致死仍按原生命一次结算，不是参赛者洗CD入口。 |

经验使用内容的累计阈值 `XP.Threshold[L]`；目标等级是合法范围内已达到阈值的最高等级。
前后等级差逐级给点；跨过 `progression.active_offer_levels` 的等级各授予一次主动机会，每个序号只一次，不一次性预生成全部候选。
`FORMULA.XPToNext` 从同一阈值表派生，不维护另一份会漂移的升级成本。

满十六级后继续累计 `int64` XP，但不再升级或授予主动机会（`LevelForXP` 不超过 MaxLevel）；超出 16 级阈值的 XP
每满 `GROWTH.MaxLevelPointsXP` 派生 `GROWTH.MaxLevelPoints` 点。UI 显示 `MAX`，不读取不存在的十七级阈值。不把旧有限奖励预算改名沿用。
XP 与阈值运算的任何越界必须在整奖准备阶段拒绝，
不得只发部分奖励、截掉溢出再返回 Granted。

## 6. 主动机会、学习与加点

产品规则见 App Spec §4.2–§4.3，参数见[内容与数值 §4.1、§4.3](CONTENT_AND_BALANCE.md#43-主动机会学习与加点)。

### 6.1 主动候选：生成、展示、选择与恢复

| 权威状态 / 本地动作 | 转移与持久内容 | 机会是否消费 |
| --- | --- | --- |
| `NoOpportunity` | 奖励提交推进已获机会；没有当前候选时准备队首。 | 否 |
| `Queued` | 队首没有合格主动（全部已学或已在图纸中）：不生成候选，机会保留，构筑变化后重新判定；不阻挡加点。 | 否 |
| `Offered` | 以局内固定目录、队首私有随机材料生成至多 3 个不同的合格主动并保存一次。HUD 提示待选数；真人的开局候选在本局首次到达本人、且没有其他主面板或输入捕获时由 HUD 自动打开一次面板（不代选；若玩家已在用自己的面板则放弃这次自动打开；几秒内未操作或本人受击时收起回待选提示），其余由玩家主动开面板，不强夺战斗输入。不打开、关闭、重开、死亡均读同组。 | 否 |
| 本地草稿 | 预览卡片与首选空槽（加点没有草稿：HUD 属性图标的“+”点一下即提交）；返回、Esc 只清草稿，不改服务器状态。 | 否 |
| `RequestPending` | 提交一次选择（可带“能学就学”与首选空槽）；显示等待，不能本地先改 Q/W/E、图纸或点数。关闭面板不声称撤回已受理意图。 | 否 |
| `Committed` | 同一复合提交：选中项成为待学图纸；能学时同时学到空槽并安装 Combat 投影；消费机会；之后才生成下一队首。回执写明学到的槽，或存为图纸及原因。 | 恰一次 |
| 拒绝 / 版本恢复 | 返回原因和同一组候选的当前版本；重新明确确认，不换组。 | 否 |
| `Frozen / InvalidMatch` | 终局只读冻结，离开失去提交资格；新局所有旧身份无执行权。 | 否 |

开局队首在 `BeginMatch` 中随 1 号机会生成；奖励授予的机会在奖励应用后按队列生成，不等“首次打开页面”才抽；
后续队首只在前一次选择已经应用后生成。连升多级时最多同时一组候选，余下机会保留在队列，不预抽多组再修补。

抽样在本次状态应用后、整体 Publish 前完成；它是固定目录与已验证合格池上的无失败纯操作，
随机种子由 `OfferSeed(MatchSecret, Contender, Ordinal)` 决定，同一序号重试不换 seed。
开局机会取 `opening: true` 的主动，3、6 级机会取全部主动；都排除已学与待学图纸，等概率不放回，没有权重和重抽。
合格池只有 1–2 个时候选如实只含这些。目录损坏不以替换修补：保留机会与原内容，以 `DataInvalid` 明确拒绝，
必需奖励按 §4.3 走故障路径。

两个请求竞争同一组候选时，仅一个可提交；另一个返回该组已处理，绝不把其选项套到下一组。
HP、MP 或冷却自然流逝不换组。

### 6.2 学习、满槽与时机

- **时机**（#222 用户决定：“all the time 可以学新技能和加点 all the time”）：选择、学习、加点与构件安装/拆下/搬动在对局进行中随时受理——
  野外（含交战中）、休息的每个步骤（含抽签、押注、PK 与返回倒计时）、存活或死亡等待中；只有阶段关闭（结算后等）以 `PhaseClosed` 拒绝。
  PK 中不锁定报名者的构筑快照，每次权威构筑变更直接投影到当前 PK 生命。没有构筑安全窗，`NotSafe`（52）已退役。
- **能学就学**：选择时有空槽就学到首选空槽（未指定时按 Q、W、E 顺序第一个空槽）；Q/W/E 已满时存为图纸，回执写明原因（`NoEmptySlot`）。
  决定与选择同一提交，不会只成一半。死亡等待中也同样学到空槽，复活后仍在。
- **学习图纸**：把一份待学图纸学到指定的空槽，时机同上；没有该图纸或槽已占用时明确拒绝，图纸保留。
- **没有野外替换**：满槽时新主动只能保留为图纸。满槽替换与遗忘只在休息的配装与派奖期间（[成长事务](../systems/GROWTH_TRANSACTIONS.md)）；旧主动上的挂件在同一次提交里退回构件库，
  不自动改绑，玩家可随时自行搬到合法对象；
  遗忘不退款、不保留备用技能库，再用需要新的真实来源。
- 学习时 Combat 回绑该定义原有的冷却记录（§7.1）。
- **交战窗口**由 Combat 维护：每名参赛者跨生命保留最近一次交战时刻，触发条件见 App Spec §4.2；本人 Combat 视图发布
  `BuildSafeAtMs`，只供 HUD 在交战中收起野外摘要。成长命令与 AI 都不读它（`FLDProgressionView::BuildSafeAtMs` 已删除）。

### 6.3 加点

- 一次命令给出五维各自加的点数，合计至少 1、不超过未分配点数；整次生效或整次拒绝，不部分应用草稿。
- 合法时机：同 §6.2，对局进行中随时（含交战、死亡等待与休息的每个步骤）；结算后等其他阶段以阶段关闭拒绝。
- 加点永久；洗点只在休息的配装与派奖期间退已分配点（NPC 花费的点数不退）。
- 加点改变最终属性投影：存活时按 §10 同比换算生命/法力；死亡中只改持久 Build，复活按新上限满血蓝。

## 7. 来源冷却与攻击形态

### 7.1 `IssuedCooldownId` 与防洗冷却

持久 CD 键使用共同 Match/Contender，加来源 Kind、技能定义及 `IssuedCooldownId`；
不使用 Q/W/E 槽号作为完整冷却身份。
角色技能每定义只有其原持久来源；D 冲刺是唯一的 `Innate` 记录。

到期记录仍留本局复用，不能删掉再造一条；定义集合由批准目录有界。
**只有 Combat 写 ReadyAtMs**，其随发行/绑定计划进入同一复合提交，不由成长域自行设置为 ready。

学习指定技能时，Combat 在 `PrepareBuildChange` 中回绑该定义已有的记录；本局从未发行过时才创建第一条。
客户端不能挑 ID、传 ReadyAt、借目标槽位决定回绑。最终准备重验来源绑定修订与当前 readiness；
失败不留下新 ID、绑定或机会消耗。

换槽、遗忘、死亡均不缩短截止。重学技能继承原期限；到期后自然可用不算洗 CD。
角色升级、冷却属性变化不重写已开始的期限，下次合法释放才按当时批准参数计算。

### 7.2 属性点与攻击形态投影

五维点数按每点收益作为永久来源进入同一最终属性投影（同类 flat 与加法比例相加，不逐点相乘）。
普攻不要求群攻；#198 构件的持续属性与射程来源按同一 Combat 来源合同投影，
来源移除才移除持续贡献，未结束的触发期限按同一来源合同保留必要债务。
已成功释放的独立效果使用 Combat 释放快照并继续原定期限，普通死亡或遗忘技能
均不取消，不再要求当前仍持有原技能；目标 Life、终局和正式离局取消仍检查。
维持型光环、被动和引导则按持续持有/施法条件解绑失效，不能与独立快照混用。
本域解绑不回滚已提交命中、伤害或奖励，不给资源退款，原 CD 截止照常继续。

## 8. SC-09 命令、回执、读取与生命周期

### 8.1 正式窄入口

以下 `ContenderId` 参数只存在于服务器适配后的领域调用。真人 RPC 从拥有 Controller
解析，AI 从其服务器分配参赛记录解析；网络包不能指定另一账号，AI 不伪 RPC 或直接写成长。

```cpp
FLDRewardReceipt ALDGrowthDirector::CommitDeathReward(
    const FLDCommittedDeath& Death, const FLDGameplayWritePermit& Permit);
bool ALDGrowthDirector::ProcessTransactions(const FLDGameplayWritePermit& Permit, FString& Error);
bool ALDGrowthDirector::PublishGrowth(const FLDAuthorityBoundary& Boundary, FString& Error);
```

成长只有一个窄命令，分三种类别：**主动选择**（候选身份与版本、选项、是否能学就学、可选首选空槽）、
**学习图纸**（图纸定义与目标空槽）、**加点**（五维各自的点数）。每条命令都带共同信封：Generation、当前参赛记录 Life、
ExpectedLifePhase（Alive/Dead）、递增 RequestId、ExpectedBuildRevision 与 CatalogDigest；
不收任意 Build、技能等级、属性数值或奖励金额。类型、签名与编码只由[成长事务](../systems/GROWTH_TRANSACTIONS.md)维护。

受理与提交分两步。受理只规范化、核验受理条件、固定条款并进入本批队列，返回
`AcceptedPending / Final / AdmissionRejected`，不是立即成长的函数。`ProcessTransactions` 在 Transactions 阶段
按服务器受理顺序提交受理时冻结的条款，成长与 Combat 投影经 §4.2 的 Prepare/Apply 在同一许可内一起应用。
写闸已关后新请求以 `MatchClosed` 拒绝受理，已有请求号仍可重放。
`PublishGrowth` 在 Close 之后执行，没有许可，也不写玩法状态。

| 结果 | 必须包含的内容与语义 |
| --- | --- |
| 受理结果 | Generation、命令族、RequestId、受理状态、原因、是否保留了序号、可选的既有终态回执。AdmissionRejected 不保留未来序号；AcceptedPending 已固定条款但未成功；Final 重放既有终态。 |
| 成长回执的共同头 | Generation、命令族、RequestId、原 Life 与生命阶段、结果（Applied/Rejected）、原因、提交或拒绝所在的权威批次、结果版本集合（Progression/Build/候选版本，按存在位标明）。只读重放标志属于传输外壳，不改原业务结果。 |
| 成长回执载荷 | 命令类别与定义；主动选择写明学到的槽，或存为图纸及原因（没有空槽）；学习写明槽；加点写明各项点数与之后的未分配点数；可选的下一组候选。Rejected 不伪造消费、学习、加点或下一组。 |
| 奖励回执（仅服务器内部） | DeathKey、冻结事实指纹（SHA-256 原始 32 字节）、§4 四态与原因、收款人、XP 增量、等级前后、新授主动机会区间与结果版本。Granted/NoReward 是终态；暂失败无提交号，不伪造版本增长。 |
| 查询结果 | 原查询键（Generation、命令族、RequestId）与查找状态；Pending 时只带原生命与阶段，Final 时只带匹配原查询键的业务回执，其余状态二者都不带，不伪造终态。 |

查询返回 `Pending / Final / NotFound / ReceiptExpired / InvalidContext`，不存在“查不到就再执行”。
查找键是当前已验证 Match、服务器解析的参赛者、命令族和请求号；先拒绝旧 Generation 再查询，
不能由当前 Controller 替旧查询补新局上下文。同局旧生命只读结果可查询，但不可作为当前生命写入；
新局 RequestId 重新从 1 开始也不会与旧局延迟查询串结果。查询只接受自身对应的命令族。

命令的 Life 使用参赛记录当前生命代次；死亡界面无需伪造活 Pawn。
ExpectedLifePhase 只表达该次明确确认所见的生/死状态，不是新增生命计数：
死前 Alive 确认在死亡后即使 Life 尚未递增也拒绝；死时重新确认允许受理，复活换 Life 后旧死亡确认仍拒旧。
生命阶段拒绝保留原候选、图纸与点数，只刷新资格，不换组。
使用核心 `ELDLifePhase` 的 Alive/Dead；Unspawned/Left 不可确认，复活保护是 Alive 下的资格/期限，不另造本域生命计数。

ID/枚举由当前批准 catalog 支持集解码，不是接受到历史最大枚举为止。
固定摘要长度、单命令单对象、合法槽、点数范围、非零身份与整数范围均在反序列化/受理入口验证。
角色施法不走成长命令；Q/W/E 与 D 是 Combat 的真实来源，失败不耗资源。

### 8.2 错误与 UI / AI 恢复

| 错误族 | UI / AI 的合法恢复 |
| --- | --- |
| `OldMatch / OldLife / LifePhaseChanged / LeftMatch / MatchClosed / NotContender` | 清旧身体意图；读取当前获准状态或停止本局策略，不重试旧执行权。 |
| 版本或目录过期（如 `BuildStale / CatalogMismatch`） | 读取当前状态；候选保留同 ID/内容，重新明确确认。不得自动修改目标槽或把后续候选当原候选。 |
| 时机不合法：阶段关闭（自己的构筑命令）；NPC 服务不在配装/派奖窗口（`BuildLocked`/`NotRestWindow`）、阵亡或离 NPC 太远 | 显示原因，等合法时机后由玩家或 AI 重新提交；不自动排队重放。 |
| 对象不合法：没有当前候选、未知候选或选项、该组已处理、没有该图纸、没有空槽或槽已占用、点数不足或为零 | 给出原结果或实际 Q/W/E、图纸与点数，保留尚未消费的机会；不自动代选或换组。 |
| `InvalidRequest / RequestConflict / OutOfOrder / RequestInFlight / ReceiptExpired` | 不重执行；核对原请求/序列、查询或同步当前状态，不能以超时猜成功。 |
| `NumericUnderflow / NumericDomain` | Combat 最终资源投影未提交；整次命令拒绝，源/机会/点数/Current不变，等合法资源状态或修复数据后新请求，不能向上补1或靠构筑变化杀死。必需奖励中的不可恢复数值错误走 §4 故障。 |
| `DataInvalid / ArithmeticOverflow / PrepareUnavailable / CapacityExceeded / NotAuthority / AuthorityFault` | 明确未提交和是否可恢复；按 §4 限制恢复，不能返回默认数据、长期 spinner 或静默丢命令。 |

全部原因值只在 `ELDGrowthReason`（`LDGrowthProtocol.h`）声明一次；奖励专用的
`NoRewardNoOwner / NoRewardSelf / NoRewardOwnerLeft / DeathConflict / StaleDeath` 只出现在服务器内部奖励回执。

错误详细信息只给有权的自身；不通过未知技能/账号错误泄露其他参赛者构筑。
内部断言不发送客户端。

### 8.3 私有读模型与回包顺序

`FLDProgressionView` 提供当前生命与成长资格、Level/MaxLevel/CumulativeXP、当前级与下一级阈值、
ProgressionRevision/BuildRevision、未分配点数与五维点数、Q/W/E 已学主动、待学图纸、排队的主动机会数、
当前一组候选及 CatalogDigest（#222 起不再带构筑安全时刻）。
技能冷却只由 Combat 的本人 `FLDCombatOwnerView` 发布，它还带交战窗口 `BuildSafeAtMs`，只供 HUD 交战折叠。

View 最终组成同一次 `FLDOwnerState` 发布（`FLDOwnerState.Growth`，`FLDGrowthOwnerView`），
附 Generation/Life/发布修订及各子域结果修订。
回执可以先于新快照到达：UI 可展示“已提交，等待状态同步”，不能自行补齐结果；
新快照先到则迟到回执只补结果提示，不回放旧快照。
重放的旧生命成功回执只能作为历史结果查询，不改当前生命资源/来源。
等待期间关闭面板不撤销已受理事实，重开查询真实结果；请求失败不保留假成功图标。

自身构件列表（#198 接入后）可展开（不滚动），不新增玩法槽上限。Q/W/E 和 D 从真实输入映射显示按键，
选中别人不改变按钮所有者。
UI 手势由输入域捕获，不穿透发世界命令；打开、选择都不暂停/授无敌。
GameState、全场击杀通知不获取本域私有成长/候选/来源 ID。

用户允许的名称/等级/HP/MP/状态由各自合法投影提供；不顺带暴露其他属性。
AI 读取自身这些 View 和合法感知/公共圈，不扫描全体 GameMode 表。

正常结算是另一条明确公共投影：核心公布全部本局参赛者名称、击杀、死亡、最终等级，
以及赢家、结束原因和用时。成长只提供冻结的 `Build.Level` 作为 FinalLevel；
K/D 由核心按唯一 DeathKey 归约，不从奖励回执再计一次，也不新增本域统计真相。
不得把整个 Progression View 塞进结果，XP、完整构筑和来源 CD 不公开，
统计不参与判胜。离局后的清理不能抹掉既成死亡/击杀或修改已冻结正常结果。

### 8.4 生命周期矩阵

| 对象/动作 | 取消或关窗 | 死亡/复活 | 普通离局 | 终局 | 新局 |
| --- | --- | --- | --- | --- | --- |
| XP/等级/属性点/技能/图纸 | 不变 | 全部保留，不再初始化；复活投影同构筑 | 不转移给替补或新加入者 | 冻结 | 按批准初始输入重建 |
| 主动机会/候选 | 只清本地草稿，Pending 查真实结果 | 候选与机会保留，复活不再授开局点数或机会；Dead 阶段可选择、学到空槽、加点与安装构件，只更新持久Build；旧阶段拒绝 | 不再提交 | 冻结，不继续选 | 清队列/候选/结果域，重新授予开局点数、开局机会及其候选 |
| 来源/IssuedCooldownId/ReadyAtMs | 不因图标/草稿清除；遗忘不取消独立已释放效果 | 时间继续；独立效果续原期限，新身体重绑保留来源 | 禁止新命令并取消尚未结束独立效果，不追溯改写既成事实 | 不再释放，展示只读 | 旧 ID/期限全部失效 |
| 致死到奖励 | 不是 UI 可取消意图 | 死者可收合法存续效果奖励 | 按实际致死时资格，后来离局不撤销；离局先则不再产生效果击杀 | 无新提交；必需未决不越过正常闸 | 旧 DeathKey 拒绝 |
| 已提交回执 | 可查，不重新 apply | 历史结果不写新身体 | 仅服务器获准诊断 | 可只读查既有本局结果 | 不写新局；旧投影拒绝 |
| 身体动作/当前血蓝/临时控制 | 归操作/Combat 取消合同 | 不在成长重置；生命域复活新 Life、满血蓝/清临时控制 | 核心移除资格 | 核心清理 | 新身体域 |

Host 离开由会话域结束，不执行普通离局后继续游戏路径；
没有断线重连、席位转 AI、Host 迁移或跨局战力恢复。

## 9. 内容参数、单位、取整与生成合同

数值只引用 [内容与数值](CONTENT_AND_BALANCE.md)，以下是消费者合同而非独立数值表。
结构上限来自 Spec；实际奖励、秒数、距离、概率、增量与公式常量不在本报告填默认值。
`SourceData\Demo\content.json` 中对应位置：`progression`（`max_level`、`active_slots`、`xp_start`、`xp_thresholds`、`active_offer_levels`）、
参数 `XP.BaseToNext/IncreasePerLevel`、`REWARD.PlayerXP.Base/PerLevel`、`GROWTH.*`、`SKILL.GrowthPerLevel*`，
`abilities[].opening`、`stats`，
营地怪物 `reward_xp`。

| 正式键 | 类型、单位和转换责任 | 本域消费者 / 必须验证 |
| --- | --- | --- |
| `XP.Start / XP.MaxLevel / XP.Threshold[L]`、`FORMULA.XPToNext` | 累计 XP 的有界整数；严格递增阈值，等级合法；运行 int64，算术检查后再存储。 | PrepareExperience；逐级跨阈值、十六级 MAX、无十七阈值。 |
| `REWARD.PlayerXP` | 受害者致死等级为自变量的整数奖励；只用不可变快照。 | CommitDeathReward；重复/互杀/阵亡来源、整奖越界。 |
| `CAMP.Low/High.RewardXP` | 每成员每 Life 的整数；不兼有一笔清营奖。 | 同一 DeathFact；世界只提供映射/定义，不另发资源。 |
| `GROWTH.OpeningPoints / PointsPerLevel / MaxLevelPointsXP / MaxLevelPoints`、`progression.active_offer_levels`、`abilities[].opening` | 点数为非负整数；满级给点的 XP 段为正整数；额外机会等级严格递增且在 2..16 内；开局池为主动子集。 | 点数派生与未分配点数非负、开局与 3/6 级机会、满级后按 XP 给点、无放回抽样与排除。 |
| `GROWTH.BuildSafeWindowSec` | 正秒数，按生命周期的唯一转换得整毫秒。 | Combat 维护交战窗口，只供 HUD 交战折叠；#222 起不限制任何成长命令。 |
| `stats[].per_point` | 每点收益按属性量纲；与其他来源同类相加后统一投影。 | Combat 最终投影；加点后真实属性变化。 |
| `ABILITY.<id>`、`SKILL.GrowthPerLevel / GrowthPerLevelAfter10` | 单级技能的正式效果、目标、法力/时间/距离、伤害与触发规则；固定数值按拥有者当前等级乘 G(L)。 | Build/Combat；学习、重新学习、等级成长、未选 handler 删除依据。 |
| `POLICY.MissingSourceCooldown=ContinueServerTime` | 已确认语义；ReadyAtMs 是本局绝对毫秒，不是可被 UI 重置的剩余时长。 | 失源/重学/死亡。 |
| `POLICY.DeadChoiceCommit`、`POLICY.ResourceMaxChange`、`FORMULA.ResourceMaxChange` | 死时处理成长和保持资源百分比已确认；精度/取整/边界由内容公式单点维护，不是运行时多模式开关。 | 成长命令准入、活体一次最终同比换算、循环无取整获益、死亡不治疗。 |

秒到毫秒使用生命周期的唯一转换合同，作者秒值必须能按批准精度准确表示；
不在每个 Widget 分别四舍五入。Ready 判定由服务器当前时刻与截止比较，
UI 到零不授权。属性和派生数值的计算类型、上下界、截断与来源快照均沿 Combat 的单一公式。
XP、构筑、来源与显示格式化不能相互代算。

作者域和生成链以 [内容与数值](CONTENT_AND_BALANCE.md) §10.1 与[数据管线](../systems/WORLD_AND_DATA_PIPELINE.md)为唯一口径：
角色/成长、技能、奖励及政策写在 `SourceData\Demo\content.json`，
由同域严格 `content.schema.json` 和稳定 `native_ids.json` 约束，没有旧V08记录作为第二作者源。
`Scripts\SyncDemoContentData.py` 同步生成 `Content\Lockdown\Data\Demo\demo_content.json`、
同目录的 `manifest.json`，以及原生 `Private\Development\LDDemoContentContract.h`；`ULDDemoContentCatalog`
提供不可变 `FLDDemoContentSnapshot`，经 DataAsset/Bootstrap Validate 链加载，
不拥有另一份角色、HP或运行状态。地图坐标仍归世界作者域，本内容仅引用稳定锚点及世界合同。
未知/缺失ID、非法值、无处理器/图标/触发定义被拒绝。
数值reader（`Scripts\DemoContent.py`）以 Decimal 保留原词法，先检查精度、有限性、范围与量纲，再转规范
定点原生树交 `CatalogTools.encode`；不先转float，encode也不直接接Decimal。
本域不另写校验器或复制数值输入表。
不直接修改生成摘要，不整库替换 V07/V08/Bxx 或产品版本号；游戏不读取 docs。

## 10. 已确认的死亡选择与资源同比规则

| 项目 | 用户已确认的行为 | 不可越过的边界 |
| --- | --- | --- |
| 死亡等待中的成长 | `POLICY.DeadChoiceCommit=Allowed`：允许查看、选择并学到空槽、加点与安装构件（#222），利用等待整理构筑。 | 只改变持久成长，不伪造活 Pawn，不立即治疗/施法/复活。当前 Life+ExpectedLifePhase 重新确认，旧生命/旧阶段拒绝。 |
| 最大 HP/MP 改变当前值 | 用户原文“保持原来的生命／法力百分比”；引用 `POLICY.ResourceMaxChange` 与 `FORMULA.ResourceMaxChange`。 | 升级、加点与学习均按旧当前值/旧上限和最终新上限一次换算，不逐来源反复移除/添加；不是保持旧绝对当前值。 |

同比换算由 Combat 的 PrepareBuildChange 准备、ApplyBuildChange 一次应用；
成长不另写 HP/MP 算法，UI/AI 不反向代算。死亡时选择只改持久 Build，
不 Apply 活体 Current；生命域复活以最新构筑满血蓝。
内容采用 `POLICY.ResourceMaxChange=PreserveRatio`（`FORMULA.ResourceMaxChange=FloorFinalRatioRejectAliveUnderflow`）：
权威资源为 int64 百分之一单位 P2，计算旧 Current 与最终新 Max 的 checked 乘除，
仅最终向下取整到 P2。此精度是 2026-11-02 批准的内容公式，不是用户要求精确到某一量化格。
正活 HP 缩小上限若量化为零，Combat Prepare 返回 `NumericUnderflow`，整次变更不发生；
不以 `min 1` 免费抬升或投影杀死；MP 向下量化到零合法，不形成死亡。
旧 Max、新 Max、Current 全为零时保持零；旧 Max 为零而新 Max 为正的普通转换返回
`NumericDomain`，不猜一个百分比。当前玩家目录的 MaxMP 始终为正，此域拒绝不阻塞常规成长。
正常初始化/复活由生命域显式满恢复，不走旧零分母转换。
构筑往返不能从量化获得额外百分比；
真实伤害、消耗、回复仍按 Combat 唯一规则改变资源，不能拿旧快照抹掉两次变更间的伤害。
首版目录没有强制到期的临时 Max 增益，无 MP 野怪固定零且不参与成长转换。
本拒绝策略仅覆盖当前可拒绝的成长准备，不能用 `NumericUnderflow` 保留本应到期
或被强制撤销的来源；这类最大值变化不在当前目录支持集，不能静默接入该分支。

保护解除也沿最新已确认 Combat 合同：通过局/生命/调用权限过滤的进攻意图即解除保护，
目标、距离或蓝量校验随后失败也不恢复保护；旧局/旧生命/未授权包先拒绝。
选择本身不是进攻意图。失败施法仍不扣蓝或启动技能冷却。

上述行为不再作为待选方案；具体技能/阈值/公式及射程/横扫内容只由内容报告
单点维护。工程类型、版本保护、Permit 和有限故障收口不转成用户逐字段选择。

## 11. 新增、复用、改造与删除结果

本域的替代与删除已完成；迁移过程只在 Git 历史中，本文不保留旧方案档案。

| 架构映射 | 现有落点 | 已真实删除 |
| --- | --- | --- |
| D03、D35（#144） | — | 金币账本 `LDEconomyLedger` 及其证据导出与账户退休，BankTransaction、预算/endowment、quality/transient/parents 与事后 `Record/GoldChanged/Snapshot` 写链；`LDVendorDirector`（商店会话/报价/买卖）、`LDInventoryDirector`（两槽装备与观测投影）、装备目录与进阶 |
| N03 新增 | `ALDGrowthDirector::CommitDeathReward`，Combat 在 DeathsAndRewards 按 DeathKey 调用 | `AwardExperience/RecordEconomyGold`、营地 Contact/Reward/PvPKill、LastOwner 兜底与任意授予入口 |
| N04 新增 | `Development\LDDevelopmentDirector.*`、`LDProgressionRules.*`：属性点、持久主动机会与候选、窄成长命令（选择、学习、加点） | R09 `LDCharacterBuild` 与 `ULDCharacterCatalog`、职业六维/点数/开局技能、整份草稿与站点 hold |
| D15/D16 | — | 书店 Vendor 与 `LDVendorTypes`、背包/重量/Quick/十一绑定/掉落整理、`LDInventoryTypes`/`ALDItem`、`LDGearRules`、`LDLoadoutRequest`、V07/V08 获取/装备/战术目录 |
| R07/R09 来源 | Combat 唯一持有来源冷却轨道与回绑（`PrepareBuildChange/ApplyBuildChange`） | `LDV08Loadout`/`LDV08Modifiers`、ItemFamilies 共享主动 CD、施法伪库存修订 |
| D16/N07 RPC 与投影 | `Framework\LDPlayerGrowth.cpp`、`FLDOwnerState.Growth`；HUD 的构筑选择卡 | 旧背包请求 RPC、`LDPlayerBank/Commerce/Production/Development/Vendor.cpp`、OwnerState 的 Build/Inventory/Gold/Xp |
### 11.1 数据、adapter、生成与资产退出

旧链已按原子闭包退出：R09 构筑与 `ULDCharacterCatalog`、V08 Loadout/修正器、V07/V08 装备/获取/战术目录
与 B81–B84 合同连同最后使用者同批删除；同一集成批次删除 `DA_{Character,Acquisition,Combat,Equipment}Catalog`、
`Content\Lockdown\Data\V07`/`V08` 载荷、`SourceData\Catalogs\V07`/`V08` 输入、SyncB84/TestB84/SyncV07* 脚本
与 `Lockdown.Build.cs` 的旧 RuntimeDependencies，删除前完成 AssetRegistry 引用审计。
本域内容只经 `SourceData\Demo` → Demo 载荷/root manifest/contract → `ULDDemoContentCatalog` 一条链读取；
缺新输入明确失败，不回读旧目录，不保留 fallback、第二作者源或备用旧产物。
反射类、WBP/CDO/DataAsset/map、动态加载和 `.uasset/.umap` 删除须 C3：
以当前入口/角色/技能/root widget 为根检查 AssetRegistry 硬/软/管理引用，
另核对 C++/配置拼接加载；旧类可加载时先迁移、编译、重存存活资产。
随后 UHT、Editor/Game 构建、Bootstrap Validate、Cook 和新包 staged/AssetRegistry 核验。
**文本零引用不证明二进制可安全删除。**

### 11.2 测试与文档迁移

| 当前入口 | 保留/改造与删除 |
| --- | --- |
| `Lockdown.Rules.DemoGrowth*`（[LDGrowthRulesTests.cpp](../../Source/Lockdown/Private/Tests/LDGrowthRulesTests.cpp)） | 线编码、精确条款、原子拒绝、overflow、请求窗口、等级/点数/主动候选/学习与加点准入与本人视图白名单（按 #198 第一部分改写，当前结果见就绪评估）；旧 Economy/Inventory/V08/Loadout/Character/Preparation/Skill 专测已退役。过期回执、满容量、越界和生死竞争仍须 §12 A24/A25 的场景。 |
| 拥有连接场景（[RunDemoGrowthCheck.ps1](../../Scripts/RunDemoGrowthCheck.ps1)，`LDDemoGrowthProbe`） | 参与对局的 Listen Host 与一个真实拥有客户端：开局主动选择、开局加点、交战窗口内选到的等级候选当场学到 W 并加点、死亡中选择学到 E 并加点（不复活，复活后 E 仍在）、击杀奖励经各自连接完成（按 #198 第一部分改写，当前结果见就绪评估）；纯规则和直接调用不代表真实联机结果，不为旧场景保留服务壳。 |
| [RunTests.ps1](../../Scripts/RunTests.ps1)、[CheckRuleReport.py](../../Scripts/CheckRuleReport.py) | 随提供者迁移维护明确的实际必需用例集合及源码注册一致性，保留 failed/notRun/inProcess/缺报告失败。 |
| D29–D34 本域专测/手册 | 必要来源不变量已迁入上列规则与 Combat 来源/属性规则，旧生产者与专测已删除；系统手册只描述真实接口，不保存废弃方案。 |

本域规则测试落在现有 Tests 体系，不新建测试框架，也不保留旧领域只是为了旧测试可编译。

Growth 场景的准备行走保留随机出生与正常身体碰撞：对练地点取作者地形高度并校验原生导航起点，
双方站在同层平地、喷漆区外，避开公开 Boss 巡逻点。准备路线使用作者营地家位置选择有界绕行点，
每段仍须通过已注册地面与完整原生路径检查，再由各自拥有连接发送普通移动；不读取隐藏单位实时位置、
不传送、不清除野怪、不改变正式游戏的动态避障规则。绕行失败明确结束场景，不重复同一路线掩盖阻挡。
Host 的野怪奖励样本从当前批准的两成员低档营地及作者家位置选择，沿同一原生绕行入口前往；
不使用已删除营地的旧坐标，也不为验收恢复旧地图内容。
Listen Host 的奖励验收读取实际 HUD 的头像等级和经验条（比例与提示），
同时核对本人投影与飘字；不再读取已删除的旧金额/等级文本控件。

## 12. 验收要求

下表是本域验收要求，不是测试名称清单；当前已实现的检查入口见 §11.2，
未被这些入口覆盖的条目不能当作已通过。
除明确的合成规则夹具外，实际奖励、时间、边界与容差取批准内容参数。

| 用例 | 前置、动作与精确断言 |
| --- | --- |
| A01 从零发育 | 正常菜单开局，仅近战单体普攻；真实击杀低档野怪得到一次 XP，完成首次升级与加点，无调试授予/传送。 |
| A02 最后一击/归属 | 两来源命中同一目标，分别测主动、持续效果（灼烧、区域）和普攻；只有真实致死所有者收全部奖励。无主不能回落上次攻击者。 |
| A03 死者/互杀/离开 | 同Effects集合双死先锁完整致死等级/资格再奖；后续因果集合用当时新等级。先致死后离局/重试仍沿原资格，离局先则效果取消；同ceil桶真实先后不混，清理不产击杀。 |
| A04 DeathKey 去重 | 重放相同事实，继而改变 Source/Effect/DeathSerial/BodyIdentity；最多一次结果，改条款报冲突不改收款人；合法 NoReward 也不能改源重领。 |
| A05 营地生命周期 | 每成员每Life一次；回位途中可真实致死并按旧Life结一次，到家仅存活者Reset满资源/清临时/CD就绪，不复活死者、不增Life/发奖；刷新不二次奖励，旧Life不命中新生体。 |
| A06 奖励边界 | 每级阈值前/恰到/越过、一次多级至16；开局点数与开局机会、逐级点数、3/6 级主动机会准确，连升跨过 3、6 级时机会逐一排队；满16后XP继续累计，不升级、不授机会，每满 `GROWTH.MaxLevelPointsXP` 派生点数。 |
| A07 整奖失败 | 分别注入成长、Combat、候选工作区、回执/outbox准备失败及整数越界；所有XP/等级/点数/机会/RNG/修订不变，无半奖或 NoReward fallback。 |
| A08 重试/故障 | 必需奖励暂失败时只在下一边界重试一次；仍失败经 `FaultAuthority` 明确故障关闭且无正常胜者；必需奖励未决时 Capture 完成与 Host 结束都不越过它：同刻完成先经结算边界重试再锁定，Host 结束返回 `RequestInFlight`（`Lockdown.Rules.DemoCore.RewardSettlementBeforeClose`）。 |
| A09 Permit | 错 Match/Batch/Phase/GateRevision、跨 callback/下一批旧许可在第一次 mutation 前拒绝；子 Apply 不会半途正常失败。 |
| A10 持久候选 | 连升/关闭重开/死亡/重复查询不改候选身份/内容/RNG；只有本次选择应用后才生成下一组；不打开窗口也已固定队首；没有合格主动时机会排队、不生成空组。 |
| A11 候选池 | 开局取开局池、3/6 级取全部主动，都排除已学与待学图纸、互不重复；合格只有 1–2 个时如实只给这些。 |
| A12 学习与满槽 | 能学就学时学到首选或第一个空槽，Q/W/E 已满时存为图纸并给出原因；学习图纸只能到空槽；满槽时拒绝学习且图纸保留；重学沿用原冷却截止（休息中的替换与遗忘由 `Lockdown.Rules.DemoRest.Services.ReplaceForget` 覆盖；挂件同次回收由 `Lockdown.Rules.DemoRest.Services.ForgetReclaim` 覆盖）。 |
| A13 命令竞争 | 两请求同一组候选、选择与升级竞争、加点与升级竞争；最多一次消费；版本过期返回同一组候选，不用旧整Build覆盖新CD。 |
| A14 生死成长 | 死前Alive确认不能借同Life在Dead应用；死时可选择、学到空槽与加点，不需要活Pawn、不治疗尸体；复活旧确认拒绝，机会、图纸与点数不丢。 |
| A15 构筑随时准入 | 野外交战中、死亡等待中、休息每个步骤（含 PK 与返回倒计时）的选择、学习、加点与构件安装/拆下/搬动都受理，阶段关闭拒绝；NPC 服务在锁定步骤 `BuildLocked`、野外 `NotRestWindow`/`ReplacementNotAllowed`。交战窗口本身：普攻起手、进攻技能释放、造成与受到敌方正伤害、被施加硬控与击退各自刷新，单独减速、菜单、视野与回复不刷新，死亡不重置。 |
| A16 加点原子性 | 合计为零或超过未分配点数整次拒绝；成功时五维点数与最终属性投影同一提交；点数不能为负。 |
| A21 CD生命周期 | 换槽/升级/遗忘重学/死亡保持截止；已释放独立效果仍续原期限且不再查原持有，维持型按条件取消，正式离局取消独立效果。 |
| A22 当前血蓝 | 升级/加点/学习后保持原资源百分比，仅按最终投影一次；覆盖往返、精度、零上限/最小正值及真实伤害夹在两次变更之间，不能量化套利或治疗尸体；正常复活满新上限。 |
| A23 射程 | 构件射程来源在服务端真实改变普攻范围；不扩大未指定技能距离。 |
| A24 回包/过窗 | 同ID同条款重放、异条款拒绝、64窗外拒旧+同步；先快照后回执/先回执后快照均不回滚当前状态；双击Pending不创建第二业务。 |
| A24b 查询 | 旧请求/死亡拒旧；旧局同号query=InvalidContext；各LookupStatus的包装结果不伪造终态Receipt。 |
| A25 终局/新局/隐私 | 关闸后无新奖励/成长命令/来源，只读旧回执；新局清局内域/旧ID。结果只含批准名称/K/D/FinalLevel/赢家/原因/用时，无私有修订/技能/点数/XP/候选/CD。 |
| A26 删除与生成闭包 | 批准定义→schema/adapter/loader/handler/UI/资产/测试一致；旧RPC/服务/载荷/专测无存活消费者，保留稳定ID有依据；UHT/build/Bootstrap/Cook/包清单无悬空引用。 |

网络、成长和胜负至少使用**正常参赛且可见的 Listen Host + 真实拥有客户端**，
最终整合覆盖 Host + 两拥有客户端 + 三 AI；注入延迟/抖动/重复/乱序时同时核对服务端回执
与客户端投影。直接调用 `_Implementation`、Host 全知调试或旧无渲染 probe 不能替代这些证据。
LAN 与正确配置的公网直连分别验证，输出写忽略的 Artifacts。

### 设计参考

玩法取舍参考 [Riot Games：Gizmos & Gadgets Learnings](https://teamfighttactics.leagueoflegends.com/en-us/news/dev/dev-teamfight-tactics-gizmos-gadgets-learnings/)，
仅用于区分“组合规则合法”与“选择真正有取舍”，不照搬其内容/数值。
规则测试与拥有连接场景通过只证明其覆盖的条目；不宣称平衡、完整 §12 或完整 Demo 已验证。

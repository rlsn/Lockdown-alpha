# 成长事务与回执

可复用的是身份、版本校验、原子提交和反馈机制。Demo 没有装备、金钱与商店（#144 删除）；
#198 第一部分起成长只有经验、等级、派生的五维点数、单级主动、主动机会与待学图纸，第二部分（B）加入材料实例与基础构件
B01–B18 的获取和安装，休息窗口另有 NPC 主动候选、洗点、替换与遗忘主动四项服务，范围见 [App Spec](../APP_SPEC.md)，
目标合同见 [成长与奖励](../demo/PROGRESSION_AND_REWARDS.md)，数值见 [内容与数值](../demo/CONTENT_AND_BALANCE.md)。

## 状态与入口

| 入口 | 责任 |
| --- | --- |
| `Development\LDGrowthDirector` | 成长域枢纽：死亡奖励汇点、地图事件奖励、Transactions 分派与拥有者发布 |
| `Development\LDDevelopmentDirector` | 等级/XP、主动机会队列与唯一持久候选组、Q/W/E、待学图纸、五维点数分配与休息服务 |
| `Development\LDProgressionRules.h` | 纯规则：等级、派生点数、机会、候选抽取、构筑命令的合法性与 Apply（含休息服务） |
| `Development\LDGrowthProtocol.h` | 构筑命令、回执、查询、提交结果与本人视图的线协议 |
| `Development\LDGrowthProviders.cpp` | 向对局门禁注册 Progression 提供者及最终等级读取 |

成长状态是 `FLDGrowthBuild`（等级、按五维设计顺序（体格、脸皮、力道、脑力、身手，即 native id 升序，`FLDDemoContentDefinitions::StatsInDesignOrder`；目录本身按 id 排序）每个五维一格的已分配点数、至多三个 `{Id, Slot}` 主动、按 id 排序的待学图纸、
永久消费点数 `PermanentSpentPoints`、按 id 排序的材料实例、构筑修订）与 `FLDProgressionState`（累计 XP、已得/已用主动机会、其中 NPC 购买的机会数
`NpcPurchased`、唯一队首候选组及其修订、已消费机会记录、材料池耗尽改给点数的升级次数 `EmptyPoolGrants`）。
点数不存储：`LDProgression::EarnedPoints` 由等级、XP 与 `EmptyPoolGrants` 派生（开局 `GROWTH.OpeningPoints`、每升一级
`GROWTH.PointsPerLevel`，满级后每满 `GROWTH.MaxLevelPointsXP` 再加 `GROWTH.MaxLevelPoints`，每次池空升级再加
`GROWTH.EmptyComponentPoolPoints`），未分配点数 = 派生值 − 已分配 − 永久消费；结果为负是
`AuthorityFault`，永久消费为负是 `DataInvalid`。本人视图的 `UnspentPoints` 按此计算，`PermanentSpentPoints` 另列。
主动机会按获得顺序编号 1..`ActiveEarned`：开局一次（`BeginMatch`），之后 `active_offer_levels` 中的等级各加一次（连升逐一排队），
休息 NPC 每次付费再加一次；`ActiveEarned` 恒等于 `ActiveOpportunitiesAt(等级) + NpcPurchased`，`PlanExperience` 只接收等级部分，
`PrepareExperience` 把 NPC 部分原样带过升级。

材料实例 `FLDComponentInstance`（#198 B）是本局真实获得的一件材料：`{Id, Source, SourceLevel, bInstalled, bEverInstalled, Target}`，
每个定义至多一件，库存与已安装合并去重。`ValidateBuild` 要求 id 已知且严格升序、来源合法（`OpeningPick` 为 1 级且整局至多一件，
`LevelUp` 为 2..当前等级且每级至多一件）、已安装必有首次历史、库存实例没有目标；已安装的按定义形状安装，
带 `active_element` 绑定的（B18）绑定其列表内的主动与元素；安装所在的主动、宿主构件与 C12 复用的基础构件都必须仍然有效
（已学会／已安装），否则构筑非法（`DataInvalid`），因为遗忘与卸下会在同一次提交里先回收它们（见下表 `ReplaceActive`/`ForgetActive`）。
已安装实例按 id 排序进入 `FLDBuildProjectionInput::Components`，库存实例不进 Combat。死亡、复活与重连不改实例与首次历史。

- **开局基础构件**：开局主动选定（1 号机会记录的 `Chosen`）后、整局一次。候选是普通池里 `opening: always` 的基础构件加上
  `opening.actives` 含开局主动的基础构件，排除已持有，目录顺序（`LDProgression::OpeningBaseOptions`；本人视图的
  `bOpeningBasePending`/`OpeningBaseOptions`）。倒计时与发育期均可选，活着或死亡都可以。
- **升级材料**：每次正常升级（2..MaxLevel）与 XP 同一提交给一件：按新等级所在 `progression.component_category_weights`
  档位的基础/改装/连接百分比抽类别，再在该类别尚未持有的普通定义中按目录顺序等概率取一件；空类别剔除、其余按原权重归一化；
  全部为空时该级改给 `GROWTH.EmptyComponentPoolPoints` 点（`EmptyPoolGrants` 加一）。随机材料由本局私有 `MatchSecret`
  按参赛者与等级派生（`LDProgression::ComponentSeed`，与同号的 `OfferSeed` 不同）；`DrawLevelComponent` 是单级纯规则，
  `GrantLevelComponents` 逐级串联。满级后的 XP 不给材料。档位表缺级、重叠、百分比和不为 100 或只剩不可抽的类别时是
  `DataInvalid`，不当作池空；`BeginMatch` 预检全部等级，失败拒绝开局。

`ALDGrowthDirector::SpawnDomain` 在核心初始化时创建成长与发育两个 director，`RetireDomain` 随
`ALDGameMode::RetireMatchDomains` 退休；Progression 提供者的 Prepare 以锁定名单（真人加 AI 槽）
初始化成长记录（每人一次开局机会及其已抽好的首组候选，以及开局点数）。复活或读取投影都不会再授予开局机会。

候选组是 1–3 个互异的主动 id，生成后不可变，只有本次机会被选择应用后才抽下一组。开局机会从 `opening: true` 的主动抽，
之后的机会（升级与 NPC）从全部主动抽；都排除已学与待学图纸（被遗忘的主动重新合格），按 Offer 种子无放回均匀抽取。
升级机会合格主动不足三个时如实只给这些；一个合格主动都没有时机会保持排队、不生成空组（`LD_OFFER_WAITING`），
之后图纸被学走或构筑变化时再抽。NPC 机会在付费的同一提交里按新序号的 Offer 种子抽好恰好三个，不足三个直接拒绝。

拥有连接的 RPC 在 `Framework\LDPlayerGrowth.cpp`：`SubmitBuild(FLDBuildCommand, OutLocalReason)` 从已接受的 OwnerState
填 Generation/Life/生死阶段并分配 RequestId（`PeekGrowthRequestId(ELDGrowthFamily::Build)` 可先取下一号），一次只有一条在途；
服务器入口 `ServerGrowthBuild` 只调用 `QueueBuild`，受理结果经 `ClientGrowthSubmit` 立即返回（`OnGrowthSubmit`），
终态回执在 `PublishGrowth` 之后经 `ClientBuildReceipt` 推送（`OnBuildReceipt`）；`QueryGrowthReceipt` 按
Generation/Family/RequestId 重查同一份不可变回执，旧局为 `InvalidContext`。线族只有 `ELDGrowthFamily::Build = 3`
（1 为退役的三选一，2 为 #144 退役的商店）。本人视图 `FLDProgressionView` 随 `SyncOwner` 一起复制在
`FLDOwnerState.Growth`（Listen Host 的本地控制器由 `SyncOwner` 直接写入并调用 `OnRep_OwnerState`），
`ReadOwnProgression` 只返回当前对局的视图。

## 构筑命令

`FLDBuildCommand` 带 Generation、Life、`ExpectedLifePhase`（Alive 或 Dead）、RequestId、`ExpectedBuildRevision`、
目录摘要与 `Kind`：

| Kind | 业务字段 | 提交 |
| --- | --- | --- |
| `ActiveChoice` | `OfferId`、`OfferRevision`、`Option`；`bLearnIfPossible` 时 `Slot` 为首选空槽 0..2 或 `AnySlot` | 消费队首机会；能学就学到首选或最低空槽（`Learned`），否则存为待学图纸（`StoredAsBlueprint`，`StoredReason` 为 `NotSafe`/`Dead`/`NoEmptySlot`）；之后才抽下一组 |
| `LearnBlueprint` | `Blueprint`、空槽 `Slot` 0..2 | 把一张待学图纸学到该空槽；没有该图纸为 `NoBlueprint`，槽已被占（含满槽；替换走 `ReplaceActive`）为 `SlotOccupied`，图纸保留 |
| `AllocatePoints` | `PointCounts`（每个五维一格、五维设计顺序、合计 ≥ 1） | 整次分配；超过未分配点数为 `NotEnoughPoints`，不部分生效 |
| `OpeningBase` = 10 | `Component`；`bInstallIfPossible` 时目标与绑定（`TargetKind`/`TargetDefinition`/`TargetSegment`/`BoundActive`/`Element`） | 授予开局实例（`OpeningPick`，1 级）；能装就同次安装（`bComponentInstalled`），否则留在库存，`InstallStoredReason` 为 `NotSafe`/`Dead`/`InvalidTarget`；开局主动未选为 `OpeningActiveFirst`，已选过为 `OpeningBaseTaken`，不在候选内为 `OpeningBaseUnavailable` |
| `InstallComponent` = 11 | `Component` 与目标、绑定 | 安装一件已持有的库存实例：未持有 `NotOwned`，已安装 `AlreadyInstalled`；目标或绑定不合法 `InvalidTarget`（改装件/连接件：不在该定义的 `targets` 内、主动未学、宿主构件未安装、C12 所选基础构件未安装、C01 装在未绑火的 B18 上）；M08 与 M09 同装一处为 `TargetConflict` = 150。卸下宿主构件时，装在它上面的改装件/连接件一起卸回库存 |
| `UninstallComponent` = 12 | `Component` | 卸回库存，首次历史保留：未持有 `NotOwned`，未安装 `NotInstalled`；只在休息窗口（`RestOnly`） |
| `BuyNpcOffer` = 20 | 无（`Slot` 为 `AnySlot`） | 休息 NPC：`GROWTH.NpcActivePoints` 计入 `PermanentSpentPoints`，同一提交新增一次主动机会并抽好恰好三个互异主动（全部主动池、按新序号的 Offer 种子）；回执 `NpcPointsPaid`、`OpportunityOrdinal` 与 `NextOfferId`。之后经 `ActiveChoice` 选择（不能学就存为图纸）；关闭界面不退款，重发按请求窗口重放原回执。有候选组或任何排队机会为 `OfferOutstanding`，合格主动不足三个为 `NpcPoolTooSmall`，点数不足为 `NotEnoughPoints`，都不收费 |
| `Respec` = 21 | 无（`Slot` 为 `AnySlot`） | 全部已分配点数退回未分配，永久消费不退；回执 `PointsRefunded`；没有已分配点数为 `NothingToRespec`。Combat 按资源比例重投影，不补满 |
| `ReplaceActive` = 22 | `Blueprint`、已占槽 `Slot` 0..2 | 槽中旧主动被遗忘（不退、不变图纸），图纸被消费并学进该槽；旧主动上的挂件同次回收（见 `ForgetActive`），不会自动绑到新主动；回执 `DefinitionId`、`LearnedSlot` 与 `ForgottenId`。空槽为 `SlotEmpty`（空槽用 `LearnBlueprint`），没有该图纸为 `NoBlueprint`，有未完成候选组为 `OfferOutstanding` |
| `ForgetActive` = 23 | 已占槽 `Slot` 0..2 | 遗忘该槽主动，回执 `ForgottenId`；它重新成为候选与 NPC 的合格主动，但只有新的来源才能再得到。同一次提交里，以它为目标的改装／连接、绑定它的 B18，以及（传递地）装在这些构件上或复用它们的构件，全部带首次历史退回库存（`LDProgression::ReclaimAttachments`），不复制、不丢失、不自动改绑；玩家可在同一休息窗口自行搬到合法对象。空槽为 `SlotEmpty`，有未完成候选组为 `OfferOutstanding` |
| `ComponentChoice` = 13 | `OfferId` = 待选的精英营地二选一 `ChoiceId`；`Component` = 所选项，全零 = 改领点数 | 与 `ActiveChoice` 同表（野外存活或死亡、休息窗口都可选）。所选项成为库存实例（`EliteChoice`），该二选一结束；没有该 `ChoiceId` 为 `NoComponentChoice` = 160，不是候选项为 `InvalidOption`，所选项已持有为 `ComponentOwned` = 161。只有全部候选都已持有时才能带全零 `Component` 改领 `GROWTH.EmptyComponentPoolPoints`（计入 `AwardedPoints`），否则也是 `ComponentOwned` |
| `FillGap` = 24 | `Component` = 缺口目标（持有的改装件/连接件），`Filler` = 要拿的普通基础构件 | 休息服务（与 NPC 同门）：目标须从未启用、未补过，且全部持有构件、已学主动与待学图纸里没有它的任何兼容入口（D 冲刺与自身目标恒在；反应连接件 C09 要求这些入口原生施加至少两种不同元素——主动的灼烧/潮湿/感电、B01/B10/B11 与 B18 已绑定的元素，两个火源不算），否则 `NotAGap` = 162，补过为 `GapAlreadyFilled` = 164；`Filler` 须为能给它入口的未持有普通基础构件（它的 `targets.components` 与 C12 的 `bases`；C09 只列能补上一种缺少元素的，未绑定的 B18 按可绑元素计），否则 `NoGapFiller` = 163；`GROWTH.FillGapPoints` 计入 `PermanentSpentPoints`、`Filler` 成为库存实例（`FillGap`）、目标记“已补”同一提交；回执 `ComponentId`、`ServicePointsPaid`。点数不足 `NotEnoughPoints` |
| `Dismantle` = 25 | `Surrendered` = 交出的构件 | 休息服务：恰好 `GROWTH.DismantleInputCount` 种互异、已持有的普通构件（稀缺件、重复、未持有都为 `DismantleInvalid` = 165），换一件未持有普通构件（全部普通池均匀，结果由请求身份固定，重发重放原回执）；装在被交出构件上或以它为 C12 基础的改装件/连接件卸回库存（回执 `UninstalledComponents`），交出项删除（`RemovedComponents`），所得为库存实例（`Dismantle`）。普通池已无可得为 `DismantlePoolEmpty` = 166，不删除任何东西 |

#198 G 野外奖励（`ALDGrowthDirector::CommitFieldReward`）：地图事件按 `Event + Serial` 只付一次（重复为 `FieldRewardDuplicate` = 167），
XP 与构筑部分在同一奖励组合里准备、一次安装。构筑部分（`LDProgression::DrawFieldReward`，按比赛私密种子与事件身份固定）：
`BuildDraw`（空投与 Boss）先公平抽类别，50% 一张未学且非待学的主动图纸、50% 一件未持有的稀缺构件，各自均匀；一类为空用另一类，
两类都空按当前等级的普通升级抽一件普通构件（普通池也空则 `GROWTH.EmptyComponentPoolPoints`，计入 `AwardedPoints`）。
`Blueprint`（高档营地标记）给一张均匀的合格主动图纸，池空则保留为 `HeldBlueprints`，之后任何构筑提交让主动重新合格时逐张发放。
`ComponentChoice`（精英营地标记）无放回抽两件未持有普通构件成为待选二选一（只剩一件就显示一件，全空给点数）。
本人视图 `FLDProgressionView` 带 `ComponentChoices`、`HeldBlueprints` 与 `FieldRewards`（全部野外奖励记录，供 HUD 提示）。

组件命令的 `Component` 必须可解码，目标种类与元素按白名单解码（`LDGrowthWire::IsKnownTargetKind`/`IsKnownElement`），
不需要目标的命令（主动、加点、卸下、不要求安装的 `OpeningBase`）带任何目标字段都在受理时 `InvalidRequest`；
主动与加点命令带组件字段同样拒绝。B18 的绑定须为已学且列在 `binding.actives` 的主动加 `binding.elements` 的一种元素，
否则 `InvalidTarget`；水与电（B18 的绑定、B10/B11 的普攻元素）与火一样可以安装，由 Combat 的元素状态施加（`ApplyComponentElement` → `ApplyElements`）。

客户端发送的 `Kind` 按白名单解码，未知编号（含 4–9、14–19、26 以上）在受理时为 `InvalidRequest`，不映射到相邻含义；
每种 Kind 只接受上表的字段，其余字段必须为空。替换与遗忘之后 Combat 保留被遗忘主动的冷却轨道
（`PlanTrackBindings` 按定义找回原轨道与就绪时刻），重学沿用原冷却债。

合法性矩阵（`LDProgression::CommandGate`/`LearnGate`/`IsRestWindow`，提交时 `ALDDevelopmentDirector` 经
`ALDGameMode::ReadBuildStage` 读阶段、读 Combat 的 `ReadBuildSafeAtMs` 与权威时间）：

| 阶段（`EMatchStage`） | ActiveChoice | LearnBlueprint | AllocatePoints | BuyNpcOffer / Respec | ReplaceActive / ForgetActive |
| --- | --- | --- | --- | --- | --- |
| `Rest`（休息构筑窗口：构筑/训练 45 s/30 s 与 12 s 发放），活着，不看安全窗 | 允许，能学就学 | 允许 | 允许 | 允许 | 允许 |
| `Rest`，死亡（休息中参赛者都是 Alive，仅防御） | 允许，存为图纸（`Dead`） | `Dead` | 允许 | `Dead` | `Dead` |
| `Live`（野外段），活着且安全 | 允许，能学就学 | 允许 | 允许 | `NotRestWindow` | `ReplacementNotAllowed` |
| `Live`，活着但安全窗未过 | 允许，存为图纸（`NotSafe`） | `NotSafe` | `NotSafe` | `NotRestWindow` | `ReplacementNotAllowed` |
| `Live`，死亡 | 允许，存为图纸（`Dead`） | `Dead` | 允许，不复活、不治疗、不改复活时间 | `NotRestWindow` | `ReplacementNotAllowed` |
| `RestLocked`（抽签、下注、PK、结算、返场倒计时） | `BuildLocked` | `BuildLocked` | `BuildLocked` | `BuildLocked` | `BuildLocked` |
| `Closed`（结果等其他阶段） | `PhaseClosed` | `PhaseClosed` | `PhaseClosed` | `PhaseClosed` | `PhaseClosed` |

开局倒计时已退役，开局休息 R0 取代它：开局选择与开局点数在 `Rest` 中提交。野外满槽选择照旧存为图纸，
野外不能遗忘、替换、洗点或办 NPC。`IsRestWindow(Context)` 即 `Stage == Rest`，组件搬动与卸下同样以它为门。

组件命令（`LDProgression::InstallGate`、`CommandGate`）：`OpeningBase` 的选取与 `ActiveChoice` 同表（同次安装部分按首次安装判断）；
首次安装（实例从未安装过）与 `LearnBlueprint` 同表（休息窗口允许，野外存活且安全才允许，否则 `NotSafe`/`Dead`）；
重装曾经安装过的实例与 `UninstallComponent` 只在休息窗口（死亡为 `Dead`），野外为 `RestOnly`，`RestLocked` 为 `BuildLocked`。

构筑安全窗：Combat 在实体记录上保存 `LastBuildUnsafeAtMs`（跨生命，不回退），普攻起手、进攻性主动释放、
造成或受到敌方正伤害（含灼烧与区域跳伤）、被施加硬控（定身、眩晕、缴械、嘲讽、沉默）或击退时刷新；
单独减速、菜单、视野与回复不刷新。`BuildSafeAtMs = LastBuildUnsafeAtMs + GROWTH.BuildSafeWindowSec`（没有记录为 0），
`<= now` 即安全；本人 `FLDCombatOwnerView::BuildSafeAtMs` 与 `FLDProgressionView::BuildSafeAtMs` 都给出这一时刻。

## 复合事务

奖励与构筑命令都按同一形状提交：纯 Prepare 完成全部可失败工作（整数 checked、版本、容量、
Combat 投影与 HP/MP 同比换算），在同一调用栈重验许可后进入不会失败的 Apply，然后标记发布。
拒绝的命令不扣点数、不消费机会、不改图纸。

| 事务 | 同次提交 |
| --- | --- |
| `CommitDeathReward` | DeathKey 去重与事实指纹（含冻结的助攻者名单）；击杀者与每名助攻者各一份累计 XP + 逐级点数、材料与主动机会（`CommitRewardShares`，`LDAssist::SplitKillReward` 平分），升级时各自 Combat 投影，全部准备后一起安装；每级材料记在回执的 `ComponentGrants`（池空为 None）；无主/自杀/离局为 NoReward 且没有助攻 |
| `CommitEventReward` | 地图事件（空投）以 (Event, Serial) 为业务身份，整局只付一次；同一事件的第二次领取以 `DataInvalid` 永久拒绝，不付任何经验 |
| `CommitPendingBuild` | 许可 → 旧局/目录/上下文/离局/旧生命/生死阶段/`BuildStale` 重查 → 按 Kind 校验（`NoActiveOffer`、`OfferAlreadyCommitted`、`UnknownOffer`、`OfferGuardStale`、`InvalidOption`；休息服务的 `OfferOutstanding`、`NpcPoolTooSmall`、`NothingToRespec`、`SlotEmpty`、`NoBlueprint`；组件命令见上）→ 合法性矩阵 → 安装构筑（等级、点数、Q/W/E 或已安装组件变化时才重投影 Combat；只改图纸、库存实例、永久消费或机会时推进构筑修订而不重装 Combat；NPC 的付费与固定候选组同次安装）→ 抽下一组 |

每笔装入的奖励都打一行 `LD_REWARD t=… contender=… cause=death|map_event|dev_fixture role=killer|assist|recipient xp=… xp_after=… level=a->b <来源>`，
升级时另打 `LD_LEVEL_UP`（`components=` 列出每级所得材料，池空的级为 `-`，`empty_pool_grants=` 为其个数）；候选组打 `LD_OFFER`（没有合格主动时 `LD_OFFER_WAITING`；NPC 候选组另带 `source=npc`），主动选择打 `LD_CHOICE`，
其余构筑命令打 `LD_BUILD`（`forgot=… refunded=… paid=… ordinal=…` 记录遗忘、洗点退回、NPC 付费与所购机会；组件命令的 `card=` 为组件 id，另有 `installed=`、`bind=主动:元素` 与 `install_stored=`；`reclaimed=` 为遗忘／替换同次退回库存的构件 id，逗号分隔，没有为 `-`）。`ProcessTransactions` 按服务器受理顺序处理构筑命令（DEV 构建另有测试命令）。
单条请求失败只形成拒绝回执；只有失去写许可等权威故障才返回 false。
`PublishGrowth(Boundary)` 在 Close 之后执行，不需要许可，只对变化者调用 `SyncOwner` 并推送终态回执。

业务去重分层完成：请求序号屏障（64 份回执窗口与终结水位，同号同条款重放原回执、异条款 `RequestConflict`）、
DeathKey 生命水位与事实指纹（最近 64 份奖励回执可原样重放，同键不同事实为 `DeathConflict`，窗口外的旧生命为 `StaleDeath`），
以及地图事件的 (Event, Serial) 集合。窗口外的旧请求返回 `ReceiptExpired`，不重新执行；写闸关闭后的新序号为 `MatchClosed`，
已受理的重放仍回原回执。拒绝原因编号：40、46、47、49–51 与 65（#198 B 元素接入前的 `ElementUnsupported`）已退役，
新增 `ReplacementNotAllowed=48`（野外替换/遗忘）、`NotSafe=52`、`NoEmptySlot=53`、`SlotOccupied=54`、`NoBlueprint=55`、
`NotEnoughPoints=56`、`NoActiveOffer=57`、`PhaseClosed=58`；#198 B 新增 `NotOwned=60`、`AlreadyInstalled=61`、`NotInstalled=62`、
`RestOnly=63`、`InvalidTarget=64`、`OpeningBaseUnavailable=66`、`OpeningBaseTaken=67`、`OpeningActiveFirst=68`；
休息服务 `BuildLocked=70`、`NpcPoolTooSmall=71`、`OfferOutstanding=72`、`NothingToRespec=73`、`SlotEmpty=74`、`NotRestWindow=75`
（76、77 未分配）。构筑命令种类 1–3 之外，组件命令为 10–12，休息服务为 20–23，其余编号未用。

## 对外信息

普通客户端只接收自己的等级、经验、点数、候选组、图纸、材料实例（含安装与绑定）、开局基础构件候选、安全窗与自身回执。
可见对手只经世界/视野发布获得等级与 Q/W/E 主动，不含冷却、XP、点数、候选、图纸或材料。拒绝原因只给有权的本人。

## 检查入口

`RunTests.ps1` 中的 `Lockdown.Rules.DemoGrowth*` 覆盖：线编码与旧族/旧原因的拒绝（`DemoGrowthWire`）、请求窗口
（`DemoGrowthRequests`）、等级/点数派生（含满级后的每 400 XP 点数）/机会/种子（`DemoGrowthProgression`）、
开局机会与开局点数及 3、6 级机会（`DemoGrowthOpening`）、候选池与开局池排除、合格不足与空池排队（`DemoGrowthOpeningPool`）、
本人视图白名单（`DemoGrowthPrivacySchema`）、合法性矩阵与三种命令的原子 Apply（`DemoGrowthBuildRules`）、
director 提交链（受理形状、倒计时学习与加点、重放/冲突/旧修订、安全窗 `NotSafe`、满槽与图纸、死亡时选择与加点、
空池排队后再抽、关局，`DemoGrowthBuildCommands`；其中开局基础构件经拥有命令在开局休息 R0 同次安装进 Combat、重选 `OpeningBaseTaken`、
野外卸下 `RestOnly`，升到 3 级各得一件库存材料）与技能等级成长 G(L)（`DemoGrowthSkillGrowth`）；
`Lockdown.Rules.DemoGrowthComponents.*` 覆盖材料实例去重与形状（`InstanceDedupe`）、固定种子的升级材料、类别归一化与池空给点
（`LevelGrantFixedSeed`）、首次安装/重装/卸下门禁与 B18 绑定、水电拒绝（`InstallGate`）、开局基础构件候选与选取（`OpeningBase`）；
`Lockdown.Rules.DemoRest.Services.*` 覆盖休息服务的纯规则：每种命令在 Live/Rest/RestLocked/Closed 的合法性与 `IsRestWindow`、
休息种类与原因的线编号（`Gates`），NPC 恰好收取 `GROWTH.NpcActivePoints` 并从全部主动池固定三个候选、有候选或排队机会/池不足三项/
点数不足都不收费（`Npc`），洗点只退已分配点数（`Respec`），替换消费图纸并遗忘旧主动、遗忘清空槽位并让该主动重新合格，
以及 `SlotEmpty`/`NoBlueprint`/`OfferOutstanding`/`ReplacementNotAllowed` 拒绝（`ReplaceForget`）；
`Lockdown.Rules.DemoAssist.*` 覆盖助攻资格、经验平分与击杀者/助攻者的复合奖励；安全窗的触发与不触发见
`Lockdown.Rules.DemoBuildPrimitives.BuildSafeWindow`。

DEV 成长夹具（`Development\LDDevGrowthFixture.*`）只在 `WITH_DEV_AUTOMATION_TESTS` 构建中存在，
只在以 `-LDDevGrowthFixture` 启动的权威进程、Lobby 或当前开放对局内生效，没有 RPC，客户端、AI 和正常玩法不会调用。
Host 的 DEV probe 在游戏线程直接调用：
`ALDDevelopmentDirector::DevQueueOffer(Contender, 1–3 个主动)` 为该参赛者之后生成的队首候选组按先进先出各预置一组
（只供开局与升级机会；NPC 机会在付费时按种子抽取，不取预置），
入队时按当前构筑校验（互异、未学、不是待学图纸；预置可以是开局池外的主动），
抽取时再验，失效则打 `LD_DEV_FIXTURE_PRESET_REFUSED` 错误并按正常种子抽取。
开局候选在 `BeginMatch` 中生成、早于任何对局内调用，而成长域 director 直到 Start 时才创建，
因此 Lobby 阶段改用 `LDDevGrowthFixture::StageOffer(World, Contender, 主动)`：由夹具自身暂存给下一局
（按空的开局构筑校验，接收者须是当前 Lobby 成员）；`BeginMatch` 经 `TakeStaged` 把锁定名单内的暂存移入本局队列，
由开局候选先消费，名单外的暂存记错误丢弃，旧局队列从不带入新局。
选择仍经 `SubmitBuild`→`CommitPendingBuild`→Combat 安装。
`ALDGrowthDirector::DevGrantReward(Contender, XP)` 排入下一个 Transactions 阶段，
与死亡奖励共用 `CommitRewardParts` 复合提交（`PrepareExperience` 与 Combat 投影，`cause=dev_fixture`），
不伪造死亡事实；返回票号由 `DevGrantStatus` 轮询 Pending/Granted/Rejected。开局与每次调用都打 `LD_DEV_FIXTURE_ACTIVE`。

DEV 测试命令（`Development\LDDevTestProtocol.*`，客户端入口 `Framework\LDPlayerDevTest.cpp`）是同一开关下给拥有连接用的请求族，
只改调用者自己的参赛者，不能指定别人；两个整局操作（刷新营地、提前开圈）只接受 Listen Host 自己的连接
（服务器上只有它的控制器是本地控制器，`QueueDevTest` 的 `bHostConnection`），其他连接在受理时回 `NotAuthority`。线协议在所有构建里存在，权威只在同一 `LDDevGrowthFixture::Admit`
通过时受理（`WITH_DEV_AUTOMATION_TESTS` 构建、Host 带 `-LDDevGrowthFixture`、Demo 房间权威），否则回 `NotAuthority`。
`SubmitDevTest` 与构筑命令一样从已接受的 OwnerState 填 Generation/Life、分配 RequestId，一次只有一条在途；
服务器 `ALDGrowthDirector::QueueDevTest` 复用 `TLDGrowthRequestStream`（单调序号、64 份回执窗口、冲突/乱序/过期拒绝），
已接受的命令在 Transactions 阶段与构筑命令按服务器到达顺序提交（`CommitPendingDevTest`）。
提交时重查本局与生命：离局为 `LeftMatch`，Life 不等于当前生命为 `OldLife`（旧命令不作用于下一条命）。
操作号 2、6、7（原加金币、买、卖）随 #144 退役，5（原学习候选选项）随 #198 退役，都不再复用。
DEV 构筑操作要求 Alive 或 Dead，不受构筑安全窗限制。

| 命令 | 提交 |
| --- | --- |
| `GrantExperience`（数量 > 0） | 与死亡奖励同一 `CommitRewardParts` 复合提交（`cause=dev_fixture`）；可重试拒绝保留待下一阶段 |
| `RefillResources`（数量 0） | 当前 Alive 身体的 HP/MP 设为已提交上限，不动护盾；非 Alive 为 `Dead` |
| `ResetCooldowns`（数量 0） | 该参赛者全部冷却轨道（技能与 D 冲刺、已绑定或未绑定的债）截止时间提前到本边界，身份与绑定不变 |
| `GrantBlueprint`（主动 id） | `ALDDevelopmentDirector::DevGrantBlueprint`：不消费机会，加一张待学图纸；已学或已是图纸为 `InvalidOption` |
| `LearnActive`（主动 id，槽 0..3） | `ALDDevelopmentDirector::DevLearnActive`：不需要机会、图纸或安全窗；0 为最低空槽（满槽 `NoEmptySlot`），1..3 为 Q/W/E 并遗忘该槽原主动（Combat 保留其冷却轨道供重学）；已学为 `InvalidOption`；同名图纸一并移除 |
| `AllocatePoints`（五维 id，数量 > 0） | `ALDDevelopmentDirector::DevAllocatePoints`：把未分配点数加到该五维；超过未分配为 `NotEnoughPoints`（明细写出请求与剩余） |
| `SetAttribute`（Stat，数量 ≥ 0）/ `ClearAttributes`（数量 0） | `ALDCombatDirector::DevSetAttribute`：改 Combat 已安装构筑的测试层 `FLDBuildProjectionInput::DevBonuses`（每个属性至多一项、按属性排序；点数 P2、比例基点、射程整厘米；不接受只限临时项的 `ADBonusRatio`；0 去掉该项，Clear 全部去掉），经 `PrepareProjected`/`ApplyBuildChange` 重投影安装：资源上限按比例换算，活着的身体保留本命临时项，不推进构筑修订，成长域在途请求不失效；投影拒绝（最终值越界）为 `NumericDomain`、不改任何状态；Alive 或 Dead（首次出生前还没有 Combat 记录，为 `DataInvalid`） |
| `ResetToMatchStart`（什么都不带） | `ALDGrowthDirector::CommitDevReset` 一笔复合提交：`ALDDevelopmentDirector::DevPrepareReset`（1 级、0 经验、无主动、图纸、材料实例、已分配与永久消费点数，没有 NPC 机会，只有开局机会与开局点数、Apply 时与 `BeginMatch` 一样抽开局候选；各修订继续递增，旧状态的在途命令按过期拒绝）与 `ALDCombatDirector::DevPrepareReset(Contender, ResultBuildRevision)`（经 `PrepareProjected` 安装空的 1 级构筑、去掉测试层）都可行才依次安装，随后 `DevFinishReset` 丢弃全部技能冷却轨道、冲刺就绪、构筑安全窗清零、活着的身体补满；Alive 或 Dead |
| `RefreshCamps`（只限 Host） | `ALDCampDirector::DevRefreshCleared` → `FLDCampMatchState::DevRefreshNow`：每个已清空、等待刷新的营地截止时间提前到本边界（不推迟更早的截止），下一个 RespawnsAndWorld 经 `AdvanceCamps` 正常整组刷新（含出生点被占时的等待）；存活营地不动；回执写出等待中的营地数；没有营地 director 为 `DataNotReady` |
| `OpenCapture`（只限 Host） | `ALDGameMode::DevOpenCaptureNow` → `FLDCaptureReducer::DevOpenAt`：只在 Development，把未开放旗杆的开放时刻提前到本边界，同一步随后的 Capture 阶段照常开放并进入 CaptureOpen；喷漆、守护时长与全部规则不变；其他阶段为 `StaleContext` |

终态回执在 `PublishGrowth` 的 `SyncOwner` 之后经 `ClientDevTestResult` 送达，受理结果同一通道立即返回。
普通学习、死亡与复活都不会顺带补满或重置冷却，只有显式命令会。新局重建成长域，命令流与回执不带入下一局。
测试层只由 Combat 持有：成长域的 `PrepareBuildChange` 输入不得带它（`InvalidInput`），Combat 把已安装的测试层带进每次真实构筑变更；
投影把它作为单独的 `FLDAttributeContributions::Dev` 行（本人属性视图的 `Dev`/`DevPercent`、HUD 来源“测试”），
最终值与临时项一样把它计入加和；测试层与真实构筑合计越界时，那次真实命令同样按投影拒绝；新局重建 Combat 记录，测试层不带入下一局。
控制台入口 `ld.Dev.XP <数量>`、`ld.Dev.Refill`、`ld.Dev.ResetCooldowns`、`ld.Dev.Learn <主动> [Q|W|E]`、`ld.Dev.Blueprint <主动>`、
`ld.Dev.Points <五维> <数量>`、`ld.Dev.Attr <属性> <数值>`、`ld.Dev.ClearAttr`、`ld.Dev.Reset`、`ld.Dev.RefreshCamps`、
`ld.Dev.OpenCapture` 与 HUD 的测试面板（`UI/LDDevTestPanel.*`）都只是本地发送者，共用 `LDDevTest::StatFields`、`ParseAmount`、
`ParseScaled` 与 `OpLabel`（`ld.Dev.List` 打印本进程已加载的主动——开局池标注 `(opening)`——与五维），
经验与点数只接受不超过 18 位的正十进制整数，测试属性按显示单位精确换算（点数与百分比至多两位小数，射程整厘米），不四舍五入；
结果打 `LD_DEV_TEST_RESULT` 日志并在 HUD 提示行显示。
`Lockdown.Rules.DemoDevTestCommands` 覆盖形状、开关拒绝、受理流、经验提交、重放不重复提交、旧生命/离局/关局拒绝、
冷却轨道重置、图纸授予、直接学习（最低空槽、指定槽遗忘、已学拒绝、Combat 投影）、点数分配与超额拒绝、补满纯规则，
测试属性（形状、纯层操作、单独的 Dev 投影行与临时项叠加、重复/负值/`ADBonusRatio`/越界拒绝、
死亡时设置不推进任何修订、越界命令不改状态、成长输入夹带测试层被拒、真实学习保留测试层、清除只去掉测试层）、
重置（成长回到 1 级、开局机会与开局点数且修订递增、Combat 安装空构筑与对应修订、技能冷却轨道被丢弃、重置后可再次学习）
以及整局操作（拥有客户端发送被拒、没有营地 director 时拒绝、发育期外提前开圈被拒且不改开放时刻、发育期内开放时刻提前到提交边界）；
`FLDCaptureReducer::DevOpenAt` 的纯规则与 `Lockdown.Rules.DemoCamp.Lifecycle` 的 `DevRefreshNow` 另有覆盖。

`RunDemoGrowthCheck.ps1` 是 DEV 拥有连接场景（`LDDemoGrowthProbe.cpp`）：渲染的参赛 Listen Host 带 `-LDDevGrowthFixture`，
在 Lobby 预置拥有客户端的三组候选。拥有客户端在开局倒计时内把开局主动学到 Q 并分配开局点数；经自身连接击杀得奖励，
到首个机会等级后在自身构筑安全窗内选择被存为图纸、加点与学习被 `NotSafe` 拒绝，窗口过后学到 W 并加点；
被 Host 击杀后在死亡中选择存为图纸、学习被 `Dead` 拒绝、加点生效且不复活不治疗，复活后学到 E；回执查询返回同一份终态回执；
运行 DEV 测试命令（含 DEV 学习、超额加点拒绝与重置），然后正常开第二局并核对新局隔离
（等级/经验/点数回到开局值、只有本局新抽的开局机会、旧局请求被 `OldMatch` 拒绝）。
场景的逐步检查以该 probe 与脚本输出为准；纯规则通过不代表联机场景已经通过。

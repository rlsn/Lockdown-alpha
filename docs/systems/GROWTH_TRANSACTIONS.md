# 成长事务与回执

| 项 | 内容 |
| --- | --- |
| 规则 | [App Spec](../APP_SPEC.md) §4、§6–§10；数值只在[内容与数值](../demo/CONTENT_AND_BALANCE.md)（§4.1、§4.3、§4.4、§6.1）；跨域见[共享契约](../demo/SHARED_CONTRACTS.md) SC-05 |
| 文件 | `Development\LDGrowthDirector`、`LDDevelopmentDirector`、`LDProgressionRules.h`、`LDGrowthProtocol.h`、`LDGrowthRequests`、`LDGrowthProviders.cpp`、`LDDevGrowthFixture`；`Framework\LDPlayerGrowth.cpp` |
| 入口函数 | `ALDGrowthDirector::CommitDeathReward` / `CommitFieldReward` / `ProcessTransactions` / `PublishGrowth`；`ALDDevelopmentDirector::PrepareExperience` / `ApplyPreparedGrowth`；`QueueBuild`（`ServerGrowthBuild`）、`QueryGrowthReceipt`；Combat `PrepareBuildChange` / `ApplyBuildChange` |
| 不变量 | 一份参赛者成长、一条 Combat 来源链；奖励只在 DeathsAndRewards、成长命令只在 Transactions；Prepare 完成全部可失败工作，Apply 不失败；同一 DeathKey 只结算一次；请求窗口 64 条有界拒旧；只有 Combat 写 `ReadyAtMs`；拒绝的命令不扣点数、不消费机会、不改图纸 |
| 检查 | `Lockdown.Rules.DemoGrowth*`、`Growth.Cards.*`、`Growth.Shop.*`、`Growth.Vouchers.Sources`、`Growth.PickTicket`、`Dev.BreakoutPresets`、`DemoRest.Services.*`、`DemoAssist.*`；场景 `RunDemoGrowthCheck.ps1`（见[检查入口](#检查入口)） |

Demo 没有装备与金钱；成长由经验、等级、派生的五维点数、主动技能、主动机会、待学图纸与构筑牌组成，另有本局货币饭票、
中场休息的商店与点名券（App Spec §4.5）；休息窗口的训练师提供洗点、替换与遗忘。
构筑安装与来源冷却见[战斗、效果与成长](COMBAT_AND_GROWTH.md)。

## 状态与入口

| 入口 | 责任 |
| --- | --- |
| `Development\LDGrowthDirector` | 成长域枢纽：死亡奖励汇点、地图事件奖励、Transactions 分派与拥有者发布 |
| `Development\LDDevelopmentDirector` | 等级/XP、主动机会队列与唯一持久候选组、Q/W/E、待学图纸、五维点数分配、牌与牌三选一、休息服务 |
| `Development\LDProgressionRules.h` | 纯规则：等级、派生点数、机会、候选抽取、构筑命令的合法性与 Apply（含休息服务） |
| `Development\LDGrowthProtocol.h` | 构筑命令、回执、查询、提交结果与本人视图的线协议 |
| `Development\LDGrowthProviders.cpp` | 向对局门禁注册 Progression 提供者及最终等级读取 |

成长状态是 `FLDGrowthBuild`（等级、按五维设计顺序（体格、脸皮、力道、脑力、身手，即 native id 升序，`FLDDemoContentDefinitions::StatsInDesignOrder`；目录本身按 id 排序）每个五维一格的已分配点数、至多三个 `{Id, Slot, Branch}` 主动、按 id 排序的待学图纸、
额外奖励点数 `AwardedPoints`、商店买到的技能点 `BoughtSkillPoints`、按序号排序的牌 `Cards` 与下一个序号 `NextCardSerial`、构筑修订、DEV 额外技能点 `DevSkillPoints`）与
`FLDProgressionState`（累计 XP、已得/已用主动机会、唯一队首候选组及其修订、已消费机会记录、饭票 `Vouchers`、点名券 `PickTickets`
与是否已领远古营地的那张 `bAncientTicketTaken`、本局见过的牌 `SeenCards`、商店 `Shop`（`FLDShopState`：货架、进货与刷新次数、修订）、
牌三选一队列 `CardQueue` 与 `CardOffersEarned`、当前牌组 `CardOffer`、重抽 `CardRerollsGranted`/`CardRerollsUsed`）。
基础点数由 `LDProgression::EarnedPoints` 根据等级与 XP 派生（开局 `GROWTH.OpeningPoints`、每升一级 `GROWTH.PointsPerLevel`，
满级后每满 `GROWTH.MaxLevelPointsXP` 再加 `GROWTH.MaxLevelPoints`），未分配点数 = 派生值 + `AwardedPoints` − 已分配；结果为负是
`AuthorityFault`。本人视图的 `UnspentPoints` 按此计算。技能点 = `SKILL.PointLevels` 中已达到的个数 + `BoughtSkillPoints` − Σ（技能等级 − 1）。
主动机会按获得顺序编号 1..`ActiveEarned`：开局一次（`BeginMatch`），之后 `active_offer_levels` 中的等级各加一次（连升逐一排队）；
`ActiveEarned` 恒等于 `ActiveOpportunitiesAt(等级)`。

牌 `FLDCardInstance` 是本局真实获得的一张牌：`{Serial, Id, OfferOrdinal, Target{Kind, Active, Element}}`。序号整局递增、不复用；
同名多份是不同序号的独立牌。`ValidateBuild` 要求 id 已知、序号互异且小于 `NextCardSerial` 并升序、已装的牌位置合法
（`CardTargetReason`：普攻牌在 `Basic`、技能牌在已学的 `Skill` 主动上、冲刺牌在 `Dash`、本体牌在 `Body`；Y25 带它可选的一种元素，其他牌不带元素）、
已装张数不超过本级容量（`LDProgression::CardCapacity` = `CARDS.CapacityBase` + ⌊(等级 − 1) / `CARDS.LevelsPerSlot`⌋）；没有分列上限，也没有形态互斥。
技能牌所在的主动被遗忘或替换时，它们在同一次提交里先回到牌库（见下表 `ReplaceActive`/`ForgetActive`）。
已装的牌按序号进入 `FLDBuildProjectionInput::Cards`，牌库里的不进 Combat。死亡、复活与重连不改牌。

- **牌三选一队列**：开局（`BeginMatch`）入队一次（`ELDCardOfferSource::Opening`），每次升级（2..MaxLevel）入队一次（`LevelUp`），
  空投/Boss 的牌奖励与精英营地标记各入队一次（`FieldReward`）。队首在开局机会处理（选定或放弃）之后才抽出（`RefreshCardOffer`），
  一次只有一组；抽取 `DrawCardOffer`：`CARDS.OfferSize` 张互异的牌，按权重无放回抽取——与本构筑词汇
  （已装的牌与已学技能，`BuildVocab`）共享至少一个键的为 `CARDS.VocabWeight`，否则为 1；种子由本局私有 `MatchSecret`、参赛者、序号与重抽次数派生
  （`CardOfferSeed`）。重抽次数：开局 `CARDS.FreeRerolls`，R1–R3 每次休息加 `CARDS.RerollsPerRest`（`GrantRestRerolls`）。
- **容量**：每张已装的牌占 1 格；`ApplyInstallCard` 在位置合法之后检查容量，满为 `WorkshopFull` = 151，不改构筑；
  可带 `Replacing`（另一张已装的牌）在同一提交里先把它卸回牌库再装。`PickCard` 的“顺手安装”遇满或位置不合法则只进牌库
  （回执 `InstallStoredReason`）。升级只增加格子，从不让已有构筑超限。

`ALDGrowthDirector::SpawnDomain` 在核心初始化时创建成长与发育两个 director，`RetireDomain` 随
`ALDGameMode::RetireMatchDomains` 退休；Progression 提供者的 Prepare 以锁定名单（真人加 AI 槽）
初始化成长记录（每人一次开局机会及其已抽好的首组候选，以及开局点数）。复活或读取投影都不会再授予开局机会。

候选组是 1–3 个互异的主动 id，生成后不可变，只有本次机会被选择或放弃后才抽下一组。开局机会从 `opening: true` 的主动抽，
之后的机会（升级）从全部主动抽；都排除已学与待学图纸（被遗忘的主动重新合格），按 Offer 种子无放回均匀抽取。
升级机会合格主动不足三个时如实只给这些；一个合格主动都没有时机会保持排队、不生成空组（`LD_OFFER_WAITING`），
之后图纸被学走或构筑变化时再抽。

拥有连接的 RPC 在 `Framework\LDPlayerGrowth.cpp`：`SubmitBuild(FLDBuildCommand, OutLocalReason)` 从已接受的 OwnerState
填 Generation/Life/生死阶段并分配 RequestId（`PeekGrowthRequestId(ELDGrowthFamily::Build)` 可先取下一号），一次只有一条在途；
服务器入口 `ServerGrowthBuild` 只调用 `QueueBuild`，受理结果经 `ClientGrowthSubmit` 立即返回（`OnGrowthSubmit`），
终态回执在 `PublishGrowth` 之后经 `ClientBuildReceipt` 推送（`OnBuildReceipt`）；`QueryGrowthReceipt` 按
Generation/Family/RequestId 重查同一份不可变回执，旧局为 `InvalidContext`。线族只有 `ELDGrowthFamily::Build = 3`
（1、2 已退役，不复用）。本人视图 `FLDProgressionView` 随 `SyncOwner` 一起复制在
`FLDOwnerState.Growth`（Listen Host 的本地控制器由 `SyncOwner` 直接写入并调用 `OnRep_OwnerState`），
`ReadOwnProgression` 只返回当前对局的视图。

## 构筑命令

`FLDBuildCommand` 带 Generation、Life、`ExpectedLifePhase`（Alive 或 Dead）、RequestId、`ExpectedBuildRevision`、
目录摘要与 `Kind`：

| Kind | 业务字段 | 提交 |
| --- | --- | --- |
| `ActiveChoice` | `OfferId`、`OfferRevision`、`Option`；`bLearnIfPossible` 时 `Slot` 为首选空槽 0..2 或 `AnySlot` | 消费队首机会；有空槽就学到首选或最低空槽（`Learned`），Q/W/E 已满时存为待学图纸（`StoredAsBlueprint`，`StoredReason` 为 `NoEmptySlot`）；不带 `bLearnIfPossible` 时存为图纸且没有原因；之后才抽下一组 |
| `SkipActiveOffer` = 26 | `OfferId`、`OfferRevision` | 消费队首机会，不授予技能，给 `VOUCHER.ActiveSkip` 饭票，回执 `VouchersDelta`/`VouchersAfter`。按请求窗口重放原回执，不重复发放；过期候选被拒绝 |
| `LearnBlueprint` | `Blueprint`、空槽 `Slot` 0..2 | 把一张待学图纸学到该空槽；没有该图纸为 `NoBlueprint`，槽已被占（含满槽；替换走 `ReplaceActive`）为 `SlotOccupied`，图纸保留 |
| `AllocatePoints` | `PointCounts`（每个五维一格、五维设计顺序、合计 ≥ 1） | 整次分配；超过未分配点数为 `NotEnoughPoints`，不部分生效 |
| `PickCard` = 30 | `OfferId`、`OfferRevision`、`Option` = 当前牌组中的一张；`bInstallIfPossible` 时 `CardTarget`/`TargetDefinition`/`Element` 为位置 | 这张牌进牌库（新序号，`OfferOrdinal` 为本组序号）；带位置时位置合法且有空格就同次装上（`bCardInstalled`），否则留在牌库并写 `InstallStoredReason`；本组结束（锁定的另一张带到下一组）。没有该组为 `NoCardOffer` = 190，不在组内为 `NotInOffer` = 192，修订不符为 `OfferGuardStale` |
| `SkipCards` = 31 | `OfferId`、`OfferRevision` | 本组结束，给 `CARDS.SkipVouchers` 饭票 |
| `RerollCards` = 32 | `OfferId`、`OfferRevision` | 花一次重抽：整组重抽，组换新 `OfferId` 与修订；没有剩余重抽为 `NoReroll` = 191 |
| `InstallCard` = 34 | `CardSerial`、位置（`CardTarget`/`TargetDefinition`/`Element`），可带 `ReplacingSerial` | 把一张库里的牌装到位置上：未持有 `NotOwned`，已装 `AlreadyInstalled`，位置不合法 `InvalidTarget`，满了 `WorkshopFull`；带 `ReplacingSerial` 时它先卸回牌库，容量按卸下后判断，回执 `UninstalledCards` 列出它。搬动是先卸后装两条命令 |
| `UninstallCard` = 35 | `CardSerial` | 卸回牌库：未持有 `NotOwned`，未装 `NotInstalled` |
| `ShopBuy` = 36 | `OfferRevision` = 商店修订、`Slot` = 货架位或 `ShopSkillPointSlot`（0xFE） | 买一个货架位：技能有空槽就学到最低空槽、否则存为图纸，牌进牌库（新序号）；技能点加到 `BoughtSkillPoints`。价格见内容与数值 §4.4.1；回执 `VouchersDelta`（负）、`VouchersAfter`，牌另带 `CardSerial`/`CardId`。修订不符为 `ShopStale` = 202，已卖为 `ShopSoldOut` = 201，饭票不够为 `NotEnoughVouchers` = 200，技能点买满为 `SkillPointLimit` = 203，没有进货为 `NoShop` = 204，都不收费 |
| `ShopRefresh` = 37 | `OfferRevision` | 付刷新价（`SHOP.RefreshPrice` + 本次休息已刷次数 × `SHOP.RefreshStep`）按新种子重抽整排货架，修订前进 |
| `ShopSell` = 38 | `CardSerial` | 卖掉牌库里一张未装的牌，得 `SHOP.SellPrice` 饭票；已装为 `CardInstalled` = 207，未持有为 `NotOwned` |
| `UsePickTicket` = 39 | `Option` = 一个主动或一张牌 | 用掉一张点名券：主动须既未学也非图纸（学到最低空槽或存为图纸），牌须在 `SeenCards` 里（进牌库）；没有券为 `NoPickTicket` = 205，没见过为 `NotSeen` = 206，其他为 `InvalidOption`。与 `RankUpSkill` 同一准入（对局进行中随时） |
| `Respec` = 21 | 无（`Slot` 为 `AnySlot`） | 全部已分配点数退回未分配；回执 `PointsRefunded`；没有已分配点数为 `NothingToRespec`。Combat 按资源比例重投影，不补满 |
| `ReplaceActive` = 22 | `Blueprint`、已占槽 `Slot` 0..2 | 槽中旧主动被遗忘（不退、不变图纸），图纸被消费并学进该槽；旧主动上的技能牌同次回到牌库（见 `ForgetActive`），不会自动绑到新主动；回执 `DefinitionId`、`LearnedSlot` 与 `ForgottenId`。空槽为 `SlotEmpty`（空槽用 `LearnBlueprint`），没有该图纸为 `NoBlueprint`，有未完成候选组为 `OfferOutstanding` |
| `ForgetActive` = 23 | 已占槽 `Slot` 0..2 | 遗忘该槽主动，回执 `ForgottenId`；它重新成为候选与商店的合格主动，但只有新的来源才能再得到。同一次提交里，装在它上面的技能牌全部回到牌库（`LDProgression::ReclaimAttachments`，回执 `UninstalledCards`），不复制、不丢失、不自动改绑；玩家可随时自行装到合法位置。空槽为 `SlotEmpty`，有未完成候选组为 `OfferOutstanding` |
| `RankUpSkill` = 28 | 已学主动的 `Slot` 0..2、`Branch` = 下一步分支 | 花 1 个技能点把该技能升一级：1 级只能选 `A`/`B`，`A` 只能选 `A1`/`A2`，`B` 只能选 `B1`/`B2`（`LDSkillBranch::IsNext`）；技能点 = `SKILL.PointLevels` 中已达到的个数 + 商店买到的 − Σ（技能等级 − 1）（`LDProgression::SkillPoints`）。对局中随时受理（含交战与死亡等待；`CommandGate` 只拒绝 `Closed` 与非 Alive/Dead），成功后重新投影战斗构筑；回执 `Branch` 与 `SkillPointsAfter`，日志 `LD_SKILL_RANK`。空槽为 `SlotEmpty`，没有技能点为 `NoSkillPoint` = 180，分支不是下一步为 `BranchUnavailable` = 181。遗忘或替换主动会清空它的分支，花在它上面的点随之退回 |
| `FinalMod` = 27 | `OfferId` = 本人 `FinalModOfferId`、`Option`、`TargetKind`/`TargetDefinition`；`Slot` 为 `AnySlot` | R3 进入时 `ALDDevelopmentDirector::OpenFinalMods` 按比赛私密种子与参赛者抽取三个互异的独立 FIN（`DrawFinalModOptions`）；候选与合法绑定由本人 `FinalModOptions`/`FinalModSources` 投影。选择在 `Rest` 构筑窗口、活着时受理，不需要 NPC；提交时重验来源，写入唯一的 `FLDGrowthBuild::FinalMod`（定义及绑定），同时置 `FLDFinalModState::bLocked`（这次选择已用掉）。不修改主动、图纸或牌，不占容量；有普通主动候选也可选择。之后普通构筑命令照常受理；绑定的来源被遗忘时改装休眠（`LDProgression::FinalModBound` 为假，`ValidateBuild` 仍通过，战斗投影不带它），本人 `FinalModSources` 改为列出已选卡的合法来源；同一 `OfferId` 与 `Option` 的 `FinalMod` 在 `InstallGate` 开放时（任何阶段）只改绑定。再选或换卡为 `FinalLocked` = 170；没有该选择为 `NoFinalMod` = 171，不是候选为 `InvalidOption`，来源不合法为 `FinalModUnavailable` = 172。揭示后不重抽；最终野外入场边界 `LockFinalMods` 关闭全部未选者的这次选择 |

野外奖励（`ALDGrowthDirector::CommitFieldReward`）：地图事件按 `Event + Serial` 只付一次（重复为 `FieldRewardDuplicate` = 167），
XP 与构筑部分在同一奖励组合里准备、一次安装。构筑部分（`LDProgression::DrawFieldReward`，按比赛私密种子与事件身份固定）：
`BuildDraw`（空投与 Boss）先公平抽类别，50% 一张未学且非待学的主动图纸、50% 一次牌三选一；没有合格主动时总是给牌。
`Blueprint`（高档营地标记）给一张均匀的合格主动图纸，池空则保留为 `HeldBlueprints`，之后任何构筑提交让主动重新合格时逐张发放。
`CardOffer`（精英营地标记）给一次牌三选一。牌三选一入队（`ELDCardOfferSource::FieldReward`），与升级的牌组同一队列处理。
本人视图 `FLDProgressionView` 带 `Cards`、`CardCapacity`、`QueuedCardOffers`、`CardOffer`、`CardRerolls`、`HeldBlueprints` 与 `FieldRewards`（全部野外奖励记录，供 HUD 提示）。

牌命令的位置与元素按白名单解码（`LDGrowthWire::IsKnownCardTarget`/`IsKnownElement`），不需要位置的命令（主动、加点、卸牌、不要求安装的 `PickCard`）
带任何位置字段都在受理时 `InvalidRequest`；主动与加点命令带牌字段同样拒绝。

客户端发送的 `Kind` 按白名单解码，未知编号（含 4–19、24、25、29、36 以上）在受理时为 `InvalidRequest`，不映射到相邻含义；
每种 Kind 只接受上表的字段，其余字段必须为空。替换与遗忘之后 Combat 保留被遗忘主动的冷却轨道
（`PlanTrackBindings` 按定义找回原轨道与就绪时刻），重学沿用原冷却债。

合法性矩阵（`LDProgression::CommandGate`/`LearnGate`/`InstallGate`/`IsRestWindow`，提交时 `ALDDevelopmentDirector` 经
`ALDGameMode::ReadBuildStage` 与时间线读阶段及本人生命阶段）。最终野外之外自己的构筑命令在对局进行中受理：`ActiveChoice`、`SkipActiveOffer`、`LearnBlueprint`、`AllocatePoints`、`PickCard`、`SkipCards`、`RerollCards`、`InstallCard`、
`UninstallCard`（下表“自己的构筑”）；休息服务保持原有阶段、位置与费用约束：

| 阶段（`EMatchStage`） | 自己的构筑（含 `RankUpSkill`、`UsePickTicket`） | ShopBuy / ShopRefresh / ShopSell / Respec | ReplaceActive / ForgetActive |
| --- | --- | --- | --- |
| `Rest`（休息配装步骤：时长见内容与数值 `REST.*`），活着 | 允许 | 允许（商店须本次休息已进货；洗点须靠近训练师） | 靠近训练师时允许 |
| `Rest`，死亡（休息中参赛者都是 Alive，仅防御） | 允许 | `Dead` | `Dead` |
| `Live`（野外段与最终野外），活着（交战中也一样） | 允许 | `NotRestWindow` | `ReplacementNotAllowed` |
| `Live`，死亡 | 允许，不复活、不治疗、不改复活时间 | `NotRestWindow` | `ReplacementNotAllowed` |
| `RestLocked`（返场倒计时），活着或死亡 | 允许 | `BuildLocked` | `BuildLocked` |
| `Closed`（结果等其他阶段） | `PhaseClosed` | `PhaseClosed` | `PhaseClosed` |

生命阶段不是 Alive/Dead（Unspawned、Left）时每种命令都为 `LifePhaseChanged`。没有构筑安全窗：`NotSafe`（52）与 `RestOnly`（63）已退役，
`LDProgression::FBuildContext` 只带阶段与生命阶段。
开局倒计时已退役，开局休息 R0 取代它：开局选择与开局点数在 `Rest` 中提交。野外满槽选择照旧存为图纸，
野外不能遗忘、替换、洗点或用商店。`IsRestWindow(Context)` 即 `Stage == Rest`，只给休息服务作门。

公共休息室唯一的服务 NPC 是 `ELDRestService::Trainer`（Respec/ReplaceActive/ForgetActive），在 `rest_service_3`（`RestServiceAtAnchor`）；
`rest_service_1`、`rest_service_2` 当前不承载服务（地图保留这些定位点）。商店不经 NPC：`ALDGameMode` 在每次中场休息（`RestIndex` > 0）开始时
调 `ALDDevelopmentDirector::StockRestShops` 为每名参赛者按比赛私密种子进货（`LDProgression::StockShop`：技能位均匀抽自 `EligibleBlueprints`，
牌位前 `SHOP.VocabCardSlots` 个按词汇加权，其余均匀；货架上的牌记入 `SeenCards`），刷新次数归零，记 `LD_SHOP_STOCK`。GameMode 的 `NearbyRestService` 用本人当前休息生命的已提交脚点与
这些定位点、复用 `AIRDROP.PickupRadiusCm` 半径判定，只有 Build/Payout 返回服务类型；
`CommitPendingBuild` 经 `CanUseRestService` 在扣费与构筑变更前重验，不在训练师旁为 `RestServiceUnavailable=76`。

**饭票来源**（`ALDDevelopmentDirector::GrantVouchers`，每笔记 `LD_VOUCHER cause=…`）：营地清空（`ALDGameMode::PayCampClear` 在营地清空结算之前，
按档 `VOUCHER.Camp*` 给最后一击者；远古营地另给每人第一次的一张点名券，`TakeAncientTicket`）、Boss（`VOUCHER.Boss`）、
参赛者击杀与助攻（`ALDGrowthDirector` 与经验奖励同次，`VOUCHER.Kill`/`VOUCHER.Assist`）、空投领取（`VOUCHER.Airdrop`）、跳过主动候选与跳过牌组（见上表）。
主动与牌三选一的候选和商店货架都记入 `SeenCards`（点名券的牌池）。
它不改变原有请求/生命/构筑修订和回执去重；重放已完成请求不重新收费。OwnerState 只发布本人的 NearbyRestService，不公开其他人成长。

牌命令（`LDProgression::InstallGate(Context)` 即 `LearnGate`）：选牌、跳过、重抽、装牌、卸牌与搬动都按上表“自己的构筑”判断；
`PickCard` 的同次安装只看位置是否合法与是否有空格，不满足时留在牌库并写 `InstallStoredReason`。

交战窗口（不再是构筑门）：Combat 在实体记录上保存 `LastBuildUnsafeAtMs`（跨生命，不回退），普攻起手、进攻性主动释放、
造成或受到敌方正伤害（含灼烧与区域跳伤）、被施加硬控（定身、眩晕、缴械、嘲讽、沉默）或击退时刷新；
单独减速、菜单、视野与回复不刷新。`BuildSafeAtMs = LastBuildUnsafeAtMs + GROWTH.BuildSafeWindowSec`（没有记录为 0），
只由本人 `FLDCombatOwnerView::BuildSafeAtMs` 发布，供 HUD 在交战中收起野外摘要；成长提交与 AI 都不读它，
`FLDProgressionView::BuildSafeAtMs` 已删除。

## 死亡奖励

`CommitDeathReward(const FLDCommittedDeath&, const FLDGameplayWritePermit&) -> FLDRewardReceipt` 只从 Combat 的已提交死亡接收事实，不是客户端/AI RPC。
死亡身份与奖励基准（`FLDDeathKey`、`FLDCommittedDeath`、`FLDRewardBasis`）由 Combat 冻结，见[战斗、效果与成长](COMBAT_AND_GROWTH.md#身份与接口)；全部字段进入奖励指纹。
致死事实按 `OccurredAtSeconds` 排序逐个结算，同刻按受害者 EntityId 的确定顺序。世界的 CampId/MemberId 只映射同一 Victim/Life，没有第二条发奖链。

| 输入 | 结果 |
| --- | --- |
| 参赛者击杀野怪 | 全部归该所有者，无助攻、距离或伤害比例分配 |
| 参赛者击杀参赛者 | 致死瞬间冻结助攻者（`FLDCommittedDeath::Assisters`）；XP 由击杀者与助攻者平分（`LDAssist::SplitKillReward`，各自向下取整），全部准备成功后一起安装 |
| 同批互相致死 | 两个 DeathKey，各按各自致死快照与仍在局的所有者收奖 |
| 已阵亡来源的存续效果击杀 | 致死时所有者仍在本局则全额收奖 |
| 无主、自杀、实际致死前已离局 | `NoReward`（`NoRewardNoOwner/NoRewardSelf/NoRewardOwnerLeft`），同一死亡仍封已处理 |
| 回位/重置、断线移除、Host 结束 | 不产生击杀事实 |
| 营地成员死亡/清空/刷新 | 每成员每 Life 一次；清空与刷新不另发清营奖 |

**助攻记录**：唯一的伤害/状态施加路径（`ApplyHit` 已提交正伤害含盾吸收，或定身、缴械、眩晕、减速；`ApplyTauntEffect`；其他控制与减益在施加成功处）对参赛者受害者调用
`ALDCombatDirector::RecordAssistContribution`，在受害者这条生命上记最近贡献时刻（`FLDBodyCombatState::AssistContributions`，`LDAssist::RecordContribution`），换命随身体消失。
`LockDeath` 在击杀者合格时用 `LDAssist::SelectAssisters` 选出 `REWARD.AssistWindowSec` 内（含边界）有贡献、仍在本局、不是击杀者或受害者的参赛者；死亡中的参赛者仍可得助攻。
回执自身字段是击杀者那一份，`Assists` 按事实顺序列出每名助攻者的 XP、等级与机会变化。

**经验**：使用累计阈值 `XP.Threshold[L]`，目标等级是已达到阈值的最高等级；逐级给点；跨过 `progression.active_offer_levels` 的等级各授予一次机会，不一次性预生成全部候选。
满 16 级后继续累计 `int64` XP，不再升级或授予机会，超出部分每满 `GROWTH.MaxLevelPointsXP` 派生 `GROWTH.MaxLevelPoints` 点；UI 显示 `MAX`。任何越界在整奖准备阶段拒绝。

**Prepare/Apply**：调用方在同一提交段内先 `ValidateWritePermit`，再 `PrepareExperience(ContenderId, GrantXP)` 与 Combat `PrepareBuildChange`，最终重查许可与全部依赖版本后进入
不可重入、无等待的连续段 `ApplyPreparedGrowth` + `ApplyBuildChange`。Prepare 完成 XP/阈值/等级/点数/机会、最终 Build、来源、容器复制与发布 outbox；
Apply 只移动已准备数据，不分配、不加载、不调 RPC/Delegate/OnRep、不做第二次业务校验。子结果全部封存后才生成一致的 owner 投影与回执。

| `FLDRewardReceipt` | 含义 | 后续 |
| --- | --- | --- |
| `Granted` | XP/等级/机会与投影全部提交，带事实指纹、增量、等级前后与结果修订 | 同条款只读重放 |
| `NoReward` | 合法死亡明确无奖，带类型化原因 | 同 Key 记已处理，不能后来补发 |
| `RetryableNotCommitted` | 准备资源暂不可用（如 `CapacityExceeded`），什么都未提交 | Combat 保留事实，下一边界 DeathsAndRewards 重试一次 |
| `PermanentlyRejected` | 旧局、未提交事实、同 Key 异条款 `DeathConflict`、旧 Life `StaleDeath`、非法目录、越界 | 真正待付事实的不可恢复错误作为权威故障 |

重试仍失败或不可恢复时该边界返回错误，GameMode 以 `FaultAuthority` 进入 `Faulted`（无正常胜者），奖励明确未支付，关闭后不后台补发。待重试奖励存在时 `TryCloseMatch` 返回 `RewardPending`、
Host 结束返回 `RequestInFlight`；同刻 Capture 完成时 Close 保留提案（`LD_CAPTURE_CLOSE_AWAITS_REWARD`），由同刻结算边界结清（见[战斗、效果与成长](COMBAT_AND_GROWTH.md#权威时序)）。
离局清理身体与效果，但参赛记录保留到能结算既成事实；这不是离局后的新发奖资格。

## 复合事务

奖励与构筑命令都按同一形状提交：纯 Prepare 完成全部可失败工作（整数 checked、版本、容量、
Combat 投影与 HP 同比换算），在同一调用栈重验许可后进入不会失败的 Apply，然后标记发布。
拒绝的命令不扣点数、不消费机会、不改图纸。

| 事务 | 同次提交 |
| --- | --- |
| `CommitDeathReward` | DeathKey 去重与事实指纹（含冻结的助攻者名单）；击杀者与每名助攻者各一份累计 XP + 逐级点数、牌三选一与主动机会（`CommitRewardShares`，`LDAssist::SplitKillReward` 平分），升级时各自 Combat 投影，全部准备后一起安装；每级材料记在回执的 `ComponentGrants`（池空为 None）；无主/自杀/离局为 NoReward 且没有助攻。最终野外的死亡记为 NoReward，仍去重但不发成长奖励；其他成长奖励也在最终野外拒绝 |
| `CommitEventReward` | 地图事件（空投）以 (Event, Serial) 为业务身份，整局只付一次；同一事件的第二次领取以 `DataInvalid` 永久拒绝，不付任何经验 |
| `CommitPendingBuild` | 许可 → 旧局/目录/上下文/离局/旧生命/生死阶段/`BuildStale` 重查 → 按 Kind 校验（`NoActiveOffer`、`OfferAlreadyCommitted`、`UnknownOffer`、`OfferGuardStale`、`InvalidOption`；休息服务的 `OfferOutstanding`、`NothingToRespec`、`SlotEmpty`、`NoBlueprint`；商店与点名券见上）→ 合法性矩阵 → 安装构筑（等级、点数、Q/W/E 或已安装组件变化时才重投影 Combat；只改图纸、库存实例、饭票、商店或机会时推进构筑修订而不重装 Combat；商店的扣费与所得同次安装）→ 抽下一组 |

每笔装入的奖励都打一行 `LD_REWARD t=… contender=… cause=death|map_event|dev_fixture role=killer|assist|recipient xp=… xp_after=… level=a->b <来源>`，
升级时另打 `LD_LEVEL_UP`（`card_offers=` 为入队的牌三选一数）；候选组打 `LD_OFFER`（没有合格主动时 `LD_OFFER_WAITING`），主动选择打 `LD_CHOICE`，
其余构筑命令打 `LD_BUILD`（`forgot=… refunded=…` 记录遗忘与洗点退回；商店与点名券另打 `LD_SHOP`（`kind slot item serial vouchers after tickets refreshes stock`）；牌命令另打 `LD_CARD`（`kind card serial installed target stored reclaimed rerolls capacity`），抽出的牌组打 `LD_CARD_OFFER`，重抽次数变化打 `LD_CARD_REROLLS`；`reclaimed=` 为遗忘／替换同次退回牌库的牌 id，逗号分隔，没有为 `-`）。`ProcessTransactions` 按服务器受理顺序处理构筑命令（DEV 构建另有测试命令）。
单条请求失败只形成拒绝回执；只有失去写许可等权威故障才返回 false。
`PublishGrowth(Boundary)` 在 Close 之后执行，不需要许可，只对变化者调用 `SyncOwner` 并推送终态回执。

业务去重分层完成：请求序号屏障（64 份回执窗口与终结水位，同号同条款重放原回执、异条款 `RequestConflict`）、
DeathKey 生命水位与事实指纹（最近 64 份奖励回执可原样重放，同键不同事实为 `DeathConflict`，窗口外的旧生命为 `StaleDeath`），
以及地图事件的 (Event, Serial) 集合。窗口外的旧请求返回 `ReceiptExpired`，不重新执行；写闸关闭后的新序号为 `MatchClosed`，
已受理的重放仍回原回执。拒绝原因编号：40、46、47、49–51 与 65 已退役，
新增 `ReplacementNotAllowed=48`（野外替换/遗忘）、`NoEmptySlot=53`、`SlotOccupied=54`、`NoBlueprint=55`、
`NotEnoughPoints=56`、`NoActiveOffer=57`、`PhaseClosed=58`；`NotOwned=60`、`AlreadyInstalled=61`、`NotInstalled=62`、
`InvalidTarget=64`、`OpeningBaseUnavailable=66`、`OpeningBaseTaken=67`、`OpeningActiveFirst=68`；`NotSafe=52`（构筑安全窗）与
`RestOnly=63` 已退役、编号保留不复用；休息服务 `BuildLocked=70`（只用于休息服务与商店）、`OfferOutstanding=72`、`NothingToRespec=73`、`SlotEmpty=74`、`NotRestWindow=75`
（76、77 未分配；`NpcPoolTooSmall=71` 已退役、编号保留不复用）；商店与点名券 200–207（`NotEnoughVouchers`、`ShopSoldOut`、`ShopStale`、
`SkillPointLimit`、`NoShop`、`NoPickTicket`、`NotSeen`、`CardInstalled`）。构筑命令种类 1–3 之外，组件命令为 10–12，休息服务为 21–23
（20 已退役），商店与点名券为 36–39，其余编号未用。

## 协议、回执与恢复

| 字段族 | 类型 | 限制 |
| --- | --- | --- |
| Generation / Life | `uint32` | 仅核心推进，零值无效，到上限明确失败 |
| ContenderId / LedgerMatch / OfferId / OptionId / IssuedCooldownId | `FGuid` | 有效 ID 非零；可选项单独带存在位 |
| RequestId / CommitBatch | `uint64` | 请求每族从 1 开始，只在新 Match 重置，死亡不重置；CommitBatch 是边界的 `BatchSerial` |
| Build / Progression / Offer 修订 | `uint64` | 建立后基准为 1，只由唯一写入者 checked 递增；计时不增修订 |
| Level / OpportunityOrdinal / SlotIndex / 每项加点数 | `uint8` | Level 1..16、机会从 1 起、槽 0/1/2；技能没有等级字段 |
| XP / P2 | `int64` | 越界在 Prepare 拒绝 |
| `FLDDemoContentDigest` | 32 字节 SHA-256（`FLDDemoContentDefinitions::Identity.Root`） | 只做逐字节比较；日志为 64 位小写 hex |
| 技能 DefinitionId | wire 为 `FLDDefinitionWire`：64 字节 UTF-8 缓冲，最多 63 字节，NUL 终止零填充 | `[a-z][a-z0-9_]*`；先验证再构造 FName，未知/非法拒绝不截断 |
| Kind / Family / Outcome / Reason / bool | 枚举 `uint8`，Reason `uint16`，bool 仅 0/1 | decoder 逐值白名单，不用历史最大枚举包住废弃值 |

| 操作 | 推进的修订 |
| --- | --- |
| XP 增加未升级 | 只 ProgressionRevision |
| 奖励升级 | ProgressionRevision、BuildRevision；可能新增机会，已有候选不换组 |
| 成长命令成功 | ProgressionRevision、BuildRevision 与处理记录；学到主动或加点时 Combat 推进来源修订 |
| 主动释放/效果触发 | 只 Combat readiness/动作修订，不使成长命令过期 |
| 时间流逝、查询、关窗、回执重放 | 不改任何版本 |

- **受理与提交两步**：受理只规范化、核验、固定条款并入队，返回 `AcceptedPending / Final / AdmissionRejected`；`ProcessTransactions` 按受理顺序提交冻结条款。
  每条流（`TLDGrowthRequestStream`）只允许一个未决变更，另一新序号 `RequestInFlight`，未来缺口 `OutOfOrder`；同 ID 同条款返回原 Pending/回执，异条款 `RequestConflict`；
  超出 64 份窗口返回 `ReceiptExpired` 与当前状态，不重执行。请求序号不大于 FinalizedThrough、DeathKey 的 Life 不大于 `ResolvedLifeThrough` 时只查/重放，在成长 Prepare 之前生效。
- **查询**返回 `Pending / Final / NotFound / ReceiptExpired / InvalidContext`，查找键为当前 Match、服务器解析的参赛者、命令族与请求号，先拒旧 Generation；没有“查不到就再执行”。
- **生命阶段**：命令的 Life 是参赛记录当前生命代次；`ExpectedLifePhase` 只接受 Alive/Dead，表达该次确认所见的生死：死前 Alive 确认在死亡后拒绝，死后复活换 Life 后旧死亡确认拒绝；
  拒绝保留候选、图纸与点数。
- **回执**：共同头为 Generation、命令族、RequestId、原 Life 与阶段、Applied/Rejected、原因、所在批次、结果版本集合；载荷写明学到的槽或存为图纸及原因、加点后的未分配点数、可选的下一组候选。
  Rejected 不伪造消费、学习或下一组。

| 错误族 | UI / AI 恢复 |
| --- | --- |
| `OldMatch / OldLife / LifePhaseChanged / LeftMatch / MatchClosed / NotContender` | 清旧意图，读当前状态或停止策略 |
| `BuildStale / CatalogMismatch` | 读当前状态；候选保留，重新明确确认，不自动改槽 |
| 时机：`PhaseClosed`、`BuildLocked`/`NotRestWindow`、阵亡或离训练师远 | 显示原因，等合法时机重新提交，不自动排队 |
| 对象：无候选、已处理、无图纸、槽不合法、点数不足 | 给出实际状态，保留未消费机会 |
| `InvalidRequest / RequestConflict / OutOfOrder / RequestInFlight / ReceiptExpired` | 不重执行；查询或同步，不以超时猜成功 |
| `NumericUnderflow / NumericDomain` | Combat 资源投影未提交，整次拒绝 |
| `DataInvalid / ArithmeticOverflow / PrepareUnavailable / CapacityExceeded / NotAuthority / AuthorityFault` | 明确未提交与是否可恢复，不返回默认数据或长期 spinner |

全部原因只在 `ELDGrowthReason`（`LDGrowthProtocol.h`）声明一次；奖励专用原因只出现在服务器内部。错误详情只给有权的本人。

**本人视图**：`FLDProgressionView`（随 `FLDOwnerState.Growth` 发布）提供生命与资格、Level/MaxLevel/CumulativeXP、当前与下一级阈值、修订、未分配与五维点数、Q/W/E、图纸、
排队机会数、当前候选、牌、牌组与重抽次数及 CatalogDigest。技能冷却与 `BuildSafeAtMs` 只在 Combat 的本人视图。回执可以先于新快照到达：UI 显示“已提交，等待状态同步”，不自行补齐；
新快照先到则迟到回执只补提示。关闭面板不撤销已受理事实。正常结算只取冻结的 `Build.Level` 作为 FinalLevel，K/D 由核心按 DeathKey 归约，XP、构筑与 CD 不公开。

**候选状态**：`Queued`（无合格主动，机会保留，构筑变化后重判，不阻挡加点）→ `Offered`（至多 3 个合格主动，保存一次；关闭、重开、死亡都读同组；真人开局候选在首次到达且无其他主面板或输入捕获时
由 HUD 自动打开一次，几秒未操作或受击时收起）→ `RequestPending` → `Committed`（恰消费一次机会，之后才生成下一队首）。两个请求竞争同一组只有一个提交，另一个得到“该组已处理”。
目录损坏以 `DataInvalid` 拒绝，不以替换修补。

## 对外信息

普通客户端只接收自己的等级、经验、点数、候选组、图纸、牌（含位置与元素）、当前牌组与重抽次数、自身回执（交战窗口只在本人 Combat 视图里）。
可见对手只经世界/视野发布获得等级与 Q/W/E 主动，不含冷却、XP、点数、候选、图纸或牌。拒绝原因只给有权的本人。

## 生命周期矩阵

| 对象 | 取消或关窗 | 死亡/复活 | 普通离局 | 终局 | 新局 |
| --- | --- | --- | --- | --- | --- |
| XP/等级/点数/技能/图纸/牌 | 不变 | 全部保留，不再初始化 | 不转移 | 冻结 | 按初始输入重建 |
| 主动机会/候选 | 只清本地草稿 | 保留；复活不再授开局点数或机会；Dead 可选择、学空槽、加点、选牌与装牌，只更新持久 Build | 不再提交 | 冻结 | 重新授予开局点数、开局机会及候选 |
| 来源/`IssuedCooldownId`/`ReadyAtMs` | 不因图标清除 | 时间继续；新身体重绑保留来源 | 取消未结效果，不改既成事实 | 只读 | 全部失效 |
| 致死到奖励 | 不可取消 | 死者可收存续效果奖励 | 按致死时资格 | 无新提交 | 旧 DeathKey 拒绝 |
| 已提交回执 | 可查不重 apply | 不写新身体 | 仅服务器诊断 | 可只读查 | 旧投影拒绝 |

没有断线重连、席位转 AI、Host 迁移或跨局战力恢复。

## 内容键

数值只在[内容与数值](../demo/CONTENT_AND_BALANCE.md)；作者源 `SourceData\Demo\content.json`（`progression`：`max_level`、`active_slots`、`xp_start`、`xp_thresholds`、`active_offer_levels`、
`cards`；参数 `XP.*`、`REWARD.PlayerXP.*`、`GROWTH.*`、`CARDS.*`、`SKILL.GrowthPerLevel*`；`abilities[].opening`、`stats[].per_point`；营地 `reward_xp`），
由 `Scripts\SyncDemoContentData.py` 生成载荷与 `LDDemoContentContract.h`，经 `ULDDemoContentCatalog` 提供不可变快照（见[世界与数据管线](WORLD_AND_DATA_PIPELINE.md)）。

| 键 | 消费者与必须验证 |
| --- | --- |
| `XP.Start / XP.MaxLevel / XP.Threshold[L]`、`FORMULA.XPToNext` | `PrepareExperience`：严格递增阈值、逐级跨阈值、16 级 MAX、无 17 级阈值 |
| `REWARD.PlayerXP`、`CAMP.*.RewardXP` | `CommitDeathReward`；每成员每 Life 一次 |
| `GROWTH.OpeningPoints / PointsPerLevel / MaxLevelPointsXP / MaxLevelPoints`、`active_offer_levels`、`abilities[].opening` | 点数派生、开局与 2/4 级机会、无放回抽样与排除 |
| `GROWTH.BuildSafeWindowSec` | Combat 交战窗口，只供 HUD 折叠，不限制成长命令 |
| `stats[].per_point` | Combat 最终投影 |
| `POLICY.MissingSourceCooldown=ContinueServerTime`、`POLICY.DeadChoiceCommit=Allowed`、`POLICY.ResourceMaxChange=PreserveRatio` | 失源/重学/死亡计时；死亡中成长只改持久 Build、不治疗；活体一次最终同比换算（`FORMULA.ResourceMaxChange=FloorFinalRatioRejectAliveUnderflow`） |

## 检查入口

`RunTests.ps1` 中的 `Lockdown.Rules.DemoGrowth*` 覆盖：线编码与旧族/旧原因的拒绝（`DemoGrowthWire`）、请求窗口
（`DemoGrowthRequests`）、等级/点数派生（含满级后的每 240 XP 点数）/机会/种子（`DemoGrowthProgression`）、
开局机会与开局点数及 2、4 级机会（`DemoGrowthOpening`）、候选池与开局池排除、合格不足与空池排队（`DemoGrowthOpeningPool`）、
本人视图白名单（`DemoGrowthPrivacySchema`）、合法性矩阵与三种命令的原子 Apply（`DemoGrowthBuildRules`）、
director 提交链（受理形状、倒计时学习与加点、重放/冲突/旧修订、交战后立即选择学习与加点、满槽与图纸、死亡时选择学到空槽与加点、
空池排队后再抽、关局，`DemoGrowthBuildCommands`；其中开局牌三选一在开局技能之后出现（放弃开局技能也出现）、选牌同次装进 Combat、
重选同组 `NoCardOffer`、野外卸下与重装、升到 3 级各入队一组牌三选一）与技能等级成长 G(L)（`DemoGrowthSkillGrowth`）；
`Lockdown.Rules.Growth.Cards.Offer/Reroll/Duplicates/Capacity` 覆盖牌组的抽取与词汇加权、开局机会之后才抽、选牌与跳过、重抽次数、
同名多份各占一格与同位置叠放、容量 6–9 与满时替换；`Lockdown.Rules.Dev.BreakoutPresets` 覆盖九个质变样板都是合法的满级构筑并被 Combat 投影；
`Lockdown.Rules.DemoRest.Services.*` 覆盖休息服务的纯规则：每种命令在 Live/Rest/RestLocked/Closed 的合法性（自己的构筑在三个开放阶段都受理、休息服务与商店在 RestLocked 为 `BuildLocked`）与 `IsRestWindow`、
休息种类与原因的线编号（`Gates`），洗点只退已分配点数（`Respec`），替换消费图纸并遗忘旧主动、遗忘清空槽位并让该主动重新合格，
以及 `SlotEmpty`/`NoBlueprint`/`OfferOutstanding`/`ReplacementNotAllowed` 拒绝（`ReplaceForget`）；
`Lockdown.Rules.Growth.Shop.Stock/Buy/Refresh/Sell` 覆盖进货（技能位不含已学、货架记入见过的牌、同种子同货架、词汇加权）、买技能/牌/技能点与各种拒绝、
刷新涨价与下次休息重置、卖牌与已装拒绝；`Growth.Vouchers.Sources` 覆盖营地档位、远古点名券每人一次与跳过补偿；`Growth.PickTicket` 覆盖主动与见过的牌、
没见过与没有券的拒绝；`Lockdown.Rules.DemoAssist.*` 覆盖助攻资格、经验平分与击杀者/助攻者的复合奖励；交战窗口的触发与不触发见
`Lockdown.Rules.DemoBuildPrimitives.BuildSafeWindow`。

DEV 成长夹具（`Development\LDDevGrowthFixture.*`）只在 `WITH_DEV_AUTOMATION_TESTS` 构建中存在，
只在以 `-LDDevGrowthFixture` 启动的权威进程、Lobby 或当前开放对局内生效，没有 RPC，客户端、AI 和正常玩法不会调用。
Host 的 DEV probe 在游戏线程直接调用：
`ALDDevelopmentDirector::DevQueueOffer(Contender, 1–3 个主动)` 为该参赛者之后生成的队首候选组按先进先出各预置一组
（只供开局与升级机会），
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
操作号 2、5、6、7 已退役，不再复用。
DEV 构筑操作要求 Alive 或 Dead。

| 命令 | 提交 |
| --- | --- |
| `GrantExperience`（数量 > 0） | 与死亡奖励同一 `CommitRewardParts` 复合提交（`cause=dev_fixture`）；可重试拒绝保留待下一阶段 |
| `RefillResources`（数量 0） | 当前 Alive 身体的 HP 设为已提交上限、劲设为上限，不动护盾；非 Alive 为 `Dead` |
| `ResetCooldowns`（数量 0） | 该参赛者全部冷却轨道（技能与 D 冲刺、已绑定或未绑定的债）截止时间提前到本边界，身份与绑定不变 |
| `GrantBlueprint`（主动 id） | `ALDDevelopmentDirector::DevGrantBlueprint`：不消费机会，加一张待学图纸；已学或已是图纸为 `InvalidOption` |
| `LearnActive`（主动 id，槽 0..3） | `ALDDevelopmentDirector::DevLearnActive`：不需要机会或图纸；0 为最低空槽（满槽 `NoEmptySlot`），1..3 为 Q/W/E 并遗忘该槽原主动（Combat 保留其冷却轨道供重学）；已学为 `InvalidOption`；同名图纸一并移除 |
| `AllocatePoints`（五维 id，数量 > 0） | `ALDDevelopmentDirector::DevAllocatePoints`：把未分配点数加到该五维；超过未分配为 `NotEnoughPoints`（明细写出请求与剩余） |
| `SetAttribute`（Stat，数量 ≥ 0）/ `ClearAttributes`（数量 0） | `ALDCombatDirector::DevSetAttribute`：改 Combat 已安装构筑的测试层 `FLDBuildProjectionInput::DevBonuses`（每个属性至多一项、按属性排序；点数 P2、比例基点、射程整厘米；不接受只限临时项的 `ADBonusRatio`；0 去掉该项，Clear 全部去掉），经 `PrepareProjected`/`ApplyBuildChange` 重投影安装：资源上限按比例换算，活着的身体保留本命临时项，不推进构筑修订，成长域在途请求不失效；投影拒绝（最终值越界）为 `NumericDomain`、不改任何状态；Alive 或 Dead（首次出生前还没有 Combat 记录，为 `DataInvalid`） |
| `ResetToMatchStart`（什么都不带） | `ALDGrowthDirector::CommitDevReset` 一笔复合提交：`ALDDevelopmentDirector::DevPrepareReset`（1 级、0 经验、无主动、图纸、牌、已分配与永久消费点数，没有 NPC 机会，只有开局机会与开局点数、Apply 时与 `BeginMatch` 一样抽开局候选；各修订继续递增，旧状态的在途命令按过期拒绝）与 `ALDCombatDirector::DevPrepareReset(Contender, ResultBuildRevision)`（经 `PrepareProjected` 安装空的 1 级构筑、去掉测试层）都可行才依次安装，随后 `DevFinishReset` 丢弃全部技能冷却轨道、冲刺就绪、交战窗口清零、活着的身体补满；Alive 或 Dead |
| `ApplyBreakoutPreset`（样板 id） | 一笔复合提交：先同 `ResetToMatchStart` 重置，再 `ALDDevelopmentDirector::DevApplyBreakoutPreset` 把 `LDBreakoutPresets::Build` 的满级构筑装上（技能与分支、五维点数、全部牌与位置，DEV 额外技能点 `DevSkillPoints` 补足等级之外的技能等级），清空测试层后按样板写技能急速（技能永动机 100），补满资源；未知样板为 `InvalidOption`。DEV 面板与 `ld.Dev.Breakout <样板>` 一键配置 spec §7.6 的九个 P2 样板 |
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
到首个机会等级后在交战窗口仍未结束时选择即学到 W 并加点；
被 Host 击杀后在死亡中选择即学到 E、加点生效且不复活不治疗，复活后 E 仍在；回执查询返回同一份终态回执；
运行 DEV 测试命令（含 DEV 学习、超额加点拒绝与重置），然后正常开第二局并核对新局隔离
（等级/经验/点数回到开局值、只有本局新抽的开局机会、旧局请求被 `OldMatch` 拒绝）。
场景的逐步检查以该 probe 与脚本输出为准；纯规则通过不代表联机场景已经通过。

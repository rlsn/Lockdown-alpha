# 库存、商店与事务

可复用的是身份、版本校验、原子提交和反馈机制。
Demo 只有两件装备与一个地图商店，范围见 [App Spec](../APP_SPEC.md)；
目标合同见 [成长、装备与经济](../demo/PROGRESSION_EQUIPMENT_ECONOMY.md)。

## 状态与入口

| 入口 | 责任 |
| --- | --- |
| `Economy\LDEconomyLedger` | 纯权威账本：每参赛者一份 Gold、两槽装备实例、独立提交/发行序号、审计前缀与有界尾部 |
| `Economy\LDEconomyDirector` | 成长/商店域枢纽：唯一账本、死亡奖励汇点、Transactions 分派、账户退休与拥有者发布 |
| `Development\LDDevelopmentDirector` | 等级/XP、机会队列与唯一持久三选一、Q/W/E 与被动、属性卡 |
| `Inventory\LDInventoryDirector` | 从账本读取自身两槽装备与可见对手装备外观；不持有第二份库存 |
| `Inventory\LDVendorDirector` | 商店会话、报价、单件 Buy/Sell/Upgrade 复合事务与回执 |
| `Development\LDGrowthProtocol.h` | 命令、回执、查询、提交结果与三类本人视图的线协议 |
| `Economy\LDGrowthProviders.cpp` | 向对局门禁注册 Progression/Shop 提供者及最终等级读取 |

`ALDEconomyDirector::SpawnDomain` 在核心初始化时创建四个 director，`RetireDomain` 随
`ALDGameMode::RetireMatchDomains` 退休；Progression 提供者的 Prepare 以锁定名单（真人加 AI 槽）
初始化账本账户（`SHOP.StartingGold`）与成长记录（每人一次开局机会及其已抽好的首组 Offer）。
复活、开店或读取投影都不会初始化钱包，也不会再授予开局机会。

拥有连接的 RPC 在 `Framework\LDPlayerGrowth.cpp`：`SubmitChoice/SubmitShopCommand`
分配本族 RequestId 并在 RPC 前记入在途，服务器入口只调用对应 `Queue*`。
开店意图没有请求号：`BeginPlay` 绑定 `Navigation->OnShopArrived`，拥有者客户端在当前 ApproachShop
绑定到达时经 `RequestShopSession(ShopId, Life)` 以该 Life 和本地 `ULDMapRegistry::Revision()` 发送一次 `SubmitShopOpen`；
本地拒绝与服务器结果都经 `OnShopOpenResult` 通知，HUD 用 `ReadShopInteraction` 判断结果是否仍属当前绑定。
第二个开店时机：本人身体站在小摊交互半径内（`ULDNavigationComponent::OwnBodyAtShop`）、本命还没有会话，
且商店页面开着或预购中有按目录价米够且有空槽的一件时，HUD 经同一 `RequestShopSession` 请求会话（约 3 s 才重试一次）；
这类结果不属于任何交互绑定，不弹出页面，会话随下一次成长发布进入 `ReadOwnShop` 后由面板自动买入。
本人视图随 `SyncOwner` 一起复制在 `FLDOwnerState.Growth`（Listen Host 的本地控制器由 `SyncOwner` 直接写入并调用
`OnRep_OwnerState`）；`ReadOwnShop` 只在当前生命已 Opened 时有效。
砍一刀小摊页面是 HUD 停靠的
`ULDShopPanel`，只格式化 `ReadOwnShop/ReadOwnEquipment`（没有本命会话时列已批准目录，只能浏览与预购），
买卖与预购的自动买入都经 `SubmitShopCommand` 提交所见报价与修订；
三选一页面 `ULDChoicesPanel` 同样只读 `ReadOwnProgression`，经 `SubmitChoice` 提交；开局 Offer 首次到达本人、
尚未消费且没有其他主面板或输入占用时，HUD 自动打开它一次（界面未就绪时最多重试约 5 秒），不替玩家选择；
玩家 6 s 内未点选卡片时面板自动收起为待选提示（`LDHUD::FChoicesFold`），收起不改变机会与 Offer。

## 物品身份不是格子

每件装备实例由账本发行：`InstanceId` 一一编码本局 `LedgerMatch` 与 `IssueSerial`，
记录定义、拥有者、槽位（0/1）、ItemRevision 与是否已进阶（`bUpgraded`）。一个实例只有一个位置，两槽之外没有背包或地面位置。
进阶状态属于这件实例：出售时随实例消失，出售价仍按定义买价计算；账户另记整局只有一次的 `bUpgradeUsed`，卖出后也不恢复。
同款两件是两个实例；第三件在准备阶段以 `EquipmentFull` 拒绝（先于余额检查，满槽时先卖是唯一出路），
不自动出售或替换。
来源冷却身份（`IssuedCooldownId`）与回绑由 Combat 持有，买卖只把变更后的完整装备列表交给 Combat 准备。

## 复合事务

奖励、选择和买卖都按同一形状提交：纯 Prepare 完成全部可失败工作（整数 checked、版本、容量、
Combat 投影与 HP/MP 同比换算），在同一调用栈重验许可后进入不会失败的 Apply，然后标记发布。

| 事务 | 同次提交 |
| --- | --- |
| `CommitDeathReward` | DeathKey 去重与事实指纹（含冻结的助攻者名单）；击杀者与每名助攻者各一份 Mint 金币 + 累计 XP + 逐级机会（`CommitRewardShares`，`LDAssist::SplitKillReward` 分配），升级时各自 Combat 投影，全部准备后一起安装；无主/自杀/离局为 NoReward 且没有助攻 |
| `CommitPendingChoice` | 原卡/版本/生死阶段重查；学习、升阶、属性或确认替换；消费一次机会；之后才抽下一组。重抽（`bReroll`，`OptionId` 为空）只核对队头 Offer/修订与剩余令牌，原地换成同规则新抽的一组（尽量避开上一组三张）并标 `bRerolled`，不消费机会 |
| `CommitPendingShopCommand` | 会话/报价/世界资格/余额/槽位重查；Burn+Create、Consume+Mint 或 Upgrade（Burn `SHOP.UpgradeCost` + 标记实例进阶 + 账户用掉唯一一次）；BuildRevision 与 Combat 投影 |

`ProcessTransactions` 按服务器受理顺序处理开店意图、选择与买卖，之后退休已离局账户并压缩账本。
单条请求失败只形成拒绝回执；只有失去写许可等权威故障才返回 false。
`PublishGrowth(Boundary)` 在 Close 之后执行，不需要许可，只对变化者调用 `SyncOwner` 并推送终态回执。

## 账本接口

`LDEconomy::FLedger` 不参与 UObject 复制。`Initialize` 只在新局建立一次；`Prepare(FTransaction)`
按 Cause 固定形状（DeathReward、Buy、Sell、Upgrade、AccountRetire；DEV 构建另有只供夹具使用的 DevFixture 铸币，
以及 DEV 测试重置的 DevReset：只 Consume 一件不退款，或只一次非零金币变动回到开局金币，增为 Mint、减为 Burn）在完整后像上校验，返回可移动的
`FPreparedCommit`；`ApplyPrepared` 只交换已准备的状态并追加日志尾部。一笔复合提交要动多个账户时（参赛者击杀的击杀者与助攻者），
每个账户仍是一条单账户条目：`PrepareAfter(Previous, FTransaction)` 在尚未应用的前一份计划之后准备下一条（连续序号，
尾部容量与业务键按整条链检查，基准被其他提交抢先时以 `InvalidTransaction` 拒绝），全部准备成功后按顺序 `ApplyPrepared`。
提交序号与发行序号独立于尾部长度，失败不推进任何计数。

尾部容量为 64 条工程限额，不是对局次数上限。`TryCompactResolvedPrefix` 在维护点从旧前缀独立重放
全部尾部，与当前状态逐账户、实例、库存修订与总量比对一致后折叠成新前缀；被 pin、序号不符或历史不一致时
不覆盖。`Reconcile` 以同一重放核对 `初始总量 + Mint - Burn`。
账户退休是带核心离局戳的独立提交：金币和冻结装备仍参与审计，之后不再接受金钱或物品变化。
仍有已锁死亡奖励欠给该离局参赛者（作为击杀者或助攻者，本边界待结算或待重试，`ALDCombatDirector::OwesRewardTo`）时暂不退休，
待下一边界的唯一重试结清后再退休，重试不会命中 `RetiredAccount`。

业务去重分层完成：请求序号屏障（每族 64 份回执窗口与终结水位）、DeathKey 生命水位与事实指纹、
以及尾部内的业务键/条款冲突检查。窗口外的旧请求返回 `ReceiptExpired`，不重新执行。

## 对外信息

普通客户端只接收自己的 Gold、装备、等级、Offer、商店报价与自身回执。
可见对手只经世界/视野发布获得两槽装备定义（`ProjectObservedEquipment`），
不含实例、修订、冷却、金币、XP 或技能。拒绝原因只给有权的本人。

## 检查入口

`RunTests.ps1` 中的 `Lockdown.Rules.DemoGrowthWire`、`DemoGrowthLedger`、`DemoGrowthRequests`、
`DemoGrowthProgression`、`DemoGrowthOpening`、`DemoGrowthOpeningPool`、`DemoGrowthDraft`、`DemoGrowthReroll`、`DemoGrowthSkillGrowth`、`DemoGrowthUpgrade`、`DemoGrowthPrivacySchema` 与 `DemoGrowthRetireAwaitsReward` 覆盖线编码、账本守恒/压缩/退休（含欠奖时推迟退休）、请求窗口、
等级/候选/替换纯规则（替换的新主动从初级开始、升级位/新技能位/自由位与满槽新主动权重、`draftable` 排除）、技能等级成长 G(L)、开局机会（每名参赛者恰好一次且随首组 Offer 生成，只有升级再加机会，新局重建）、
免费重抽的令牌/去重/每组一次、装备进阶事务（整局一次、出售失去、属性/被动生效值）与本人视图隐私白名单。
账本另有独立复算：DEV 构建的 `LDEconomy::ExportEvidence`（场景经 `ALDEconomyDirector::ExportLedgerEvidence`）
导出 schema 5 证据（审计前缀、其后日志尾、当前状态与 `devFixture` 区块：DevFixture 铸币 `grants` 与 DevReset 条目 `resets`），`python -B Scripts\CheckEconomyEvidence.py <证据>`
逐条按 `ApplyEntry` 规则重放并要求等于当前状态；`DemoGrowthLedger` 带 `-LDGrowthLedgerEvidence=<文件>`
时写出一份覆盖全部 cause 的原生样本，`TestEconomyEvidence.py` 用合成证据覆盖拒绝分支。

DEV 成长夹具（`Development\LDDevGrowthFixture.*`）只在 `WITH_DEV_AUTOMATION_TESTS` 构建中存在，
只在以 `-LDDevGrowthFixture` 启动的权威进程、Lobby 或当前开放对局内生效，没有 RPC，客户端、AI 和正常玩法不会调用。
Host 的 DEV probe 在游戏线程直接调用：
`ALDDevelopmentDirector::DevQueueOffer(Contender, 三个定义)` 为该参赛者之后生成的队首 Offer 按先进先出各预置一组，
入队时按当前构筑的 `LegalPool` 校验（恰好三张互异、种类由构筑推出、不超过剩余可抽的 Offer 数），
抽取时按当次合法池再验，失效则打 `LD_DEV_FIXTURE_PRESET_REFUSED` 错误并按正常种子抽取。
开局 Offer 在 `BeginMatch` 中生成、早于任何对局内调用，而成长域四个 director 直到 Start 时才创建，
因此 Lobby 阶段改用 `LDDevGrowthFixture::StageOffer(World, Contender, 三个定义)`：由夹具自身暂存给下一局
（按空的开局构筑校验，接收者须是当前 Lobby 成员）；`BeginMatch` 经 `TakeStaged` 把锁定名单内的暂存移入本局队列，
由开局 Offer 先消费，名单外的暂存记错误丢弃，旧局队列从不带入新局。
选择仍经 `SubmitChoice`→`CommitPendingChoice`→Combat 安装。
`ALDEconomyDirector::DevGrantReward(Contender, XP, Gold)` 排入下一个 Transactions 阶段，
与死亡奖励共用 `CommitRewardParts` 复合提交（账本 cause `DevFixture` 铸币、`PrepareExperience` 与 Combat 投影），
不伪造死亡事实；返回票号由 `DevGrantStatus` 轮询 Pending/Granted/Rejected。
开局与每次调用都打 `LD_DEV_FIXTURE_ACTIVE`。证据的 `devFixture` 记录进程是否带开关及整局全部 DevFixture 铸币与 DevReset 条目；
只有给 `CheckEconomyEvidence.py` 加 `--dev-fixture` 时才接受这些条目，且须与日志尾逐条一致，
未加时任何夹具证据都判失败。

DEV 测试命令（`Development\LDDevTestProtocol.*`，客户端入口 `Framework\LDPlayerDevTest.cpp`）是同一开关下给拥有连接用的请求族，
只改调用者自己的参赛者，不能指定别人；两个整局操作（刷新营地、提前开圈）只接受 Listen Host 自己的连接
（服务器上只有它的控制器是本地控制器，`QueueDevTest` 的 `bHostConnection`），其他连接在受理时回 `NotAuthority`。线协议在所有构建里存在，权威只在同一 `LDDevGrowthFixture::Admit`
通过时受理（`WITH_DEV_AUTOMATION_TESTS` 构建、Host 带 `-LDDevGrowthFixture`、Demo 房间权威），否则回 `NotAuthority`。
`SubmitDevTest` 与选择一样从已接受的 OwnerState 填 Generation/Life、分配 RequestId，一次只有一条在途；
服务器 `ALDEconomyDirector::QueueDevTest` 复用 `TLDGrowthRequestStream`（单调序号、64 份回执窗口、冲突/乱序/过期拒绝），
已接受的命令在 Transactions 阶段与选择、开店、买卖按服务器到达顺序提交（`CommitPendingDevTest`）。
提交时重查本局与生命：离局为 `LeftMatch`，Life 不等于当前生命为 `OldLife`（旧命令不作用于下一条命）。

| 命令 | 提交 |
| --- | --- |
| `GrantExperience` / `GrantGold`（数量 > 0） | 与死亡奖励同一 `CommitRewardParts` 复合提交，账本 cause `DevFixture`、业务键含 LedgerMatch/参赛者/RequestId；可重试拒绝保留待下一阶段 |
| `RefillResources`（数量 0） | 当前 Alive 身体的 HP/MP 设为已提交上限，不动护盾；非 Alive 为 `Dead` |
| `ResetCooldowns`（数量 0） | 该参赛者全部冷却轨道（技能与 D 冲刺、已绑定或未绑定的债）截止时间提前到本边界，身份与绑定不变 |
| `LearnOption`（定义 id，槽 0/1/2） | `ALDDevelopmentDirector::DevChooseOption`：定义须在当前构筑的 `LegalPool` 中，作为队首机会的结果消费它，与 `CommitPendingChoice` 共用 `InstallOption`（ApplyOption、修订、下一组的池、Combat 投影、抽下一组）；槽 1/2 以该槽当前技能确认替换；没有机会为 `NoOffer`；Alive 或 Dead |
| `BuyItem`（定义 id）/ `SellItem`（槽 1/2） | `ALDVendorDirector::DevTrade`：以当前版本与目录价格合成一条商店命令，与 `CommitPendingShopCommand` 共用 `CommitTrade`（账本 Buy/Sell、构筑修订、Combat 投影），只跳过商店会话、报价与距离；满槽、余额不足照常拒绝；业务键带 `dev-test-trade` 标签；只限 Alive |
| `SetAttribute`（Stat，数量 ≥ 0）/ `ClearAttributes`（数量 0） | `ALDCombatDirector::DevSetAttribute`：改 Combat 已安装构筑的测试层 `FLDBuildProjectionInput::DevBonuses`（每个属性至多一项、按属性排序；点数 P2、比例基点、射程整厘米；不接受只限临时项的 `ADBonusRatio`；0 去掉该项，Clear 全部去掉），经 `PrepareProjected`/`ApplyBuildChange` 重投影安装：资源上限按比例换算，活着的身体保留本命临时项，不推进构筑或库存修订，成长域在途请求不失效；投影拒绝（最终值越界）为 `NumericDomain`、不改任何状态；Alive 或 Dead（首次出生前还没有 Combat 记录，为 `DataInvalid`） |
| `ResetToMatchStart`（什么都不带） | `ALDEconomyDirector::CommitDevReset` 一笔复合提交：先在账本副本上演练 DevReset 条目（按槽序每件装备一条不退款的 Consume，再一条金币变动回到 `SHOP.StartingGold`），再按其结果准备 `ALDDevelopmentDirector::DevPrepareReset`（1 级、0 经验、无技能与属性卡、只有开局机会、Apply 时与 `BeginMatch` 一样抽开局 Offer；各修订继续递增，旧状态的在途选择按过期拒绝）与 `ALDCombatDirector::DevPrepareReset`（经 `PrepareProjected` 安装空的 1 级构筑、去掉测试层），全部可行才依次安装，随后 `DevFinishReset` 丢弃全部技能冷却轨道、冲刺就绪、活着的身体补满；日志尾已满时保留待下一阶段；Alive 或 Dead |
| `RefreshCamps`（只限 Host） | `ALDCampDirector::DevRefreshCleared` → `FLDCampMatchState::DevRefreshNow`：每个已清空、等待刷新的营地截止时间提前到本边界（不推迟更早的截止），下一个 RespawnsAndWorld 经 `AdvanceCamps` 正常整组刷新（含出生点被占时的等待）；存活营地不动；回执写出等待中的营地数；没有营地 director 为 `DataNotReady` |
| `OpenCapture`（只限 Host） | `ALDGameMode::DevOpenCaptureNow` → `FLDCaptureReducer::DevOpenAt`：只在 Development，把未开放旗杆的开放时刻提前到本边界，同一步随后的 Capture 阶段照常开放并进入 CaptureOpen；喷漆、守护时长与全部规则不变；其他阶段为 `StaleContext` |

终态回执在 `PublishGrowth` 的 `SyncOwner` 之后经 `ClientDevTestResult` 送达，受理结果同一通道立即返回。
普通学习、换装、死亡与复活都不会顺带补满或重置冷却，只有显式命令会。新局重建成长域，命令流与回执不带入下一局。
测试层只由 Combat 持有：成长域的 `PrepareBuildChange` 输入不得带它（`InvalidInput`），Combat 把已安装的测试层带进每次真实构筑变更；
投影把它作为单独的 `FLDAttributeContributions::Dev` 行（本人属性视图的 `Dev`/`DevPercent`、HUD 来源“测试”），
最终值与临时项一样把它计入加和；测试层与真实构筑合计越界时，那次真实选择或买卖同样按投影拒绝；新局重建 Combat 记录，测试层不带入下一局。
控制台入口 `ld.Dev.XP <数量>`、`ld.Dev.Gold <数量>`、`ld.Dev.Refill`、`ld.Dev.ResetCooldowns`、`ld.Dev.Learn <id> [Q|W]`、
`ld.Dev.Buy <id>`、`ld.Dev.Sell <1|2>`、`ld.Dev.Attr <属性> <数值>`、`ld.Dev.ClearAttr`、`ld.Dev.Reset`、`ld.Dev.RefreshCamps`、`ld.Dev.OpenCapture` 与 HUD 的测试面板（`UI/LDDevTestPanel.*`）
都只是本地发送者，共用 `LDDevTest::StatFields`、`ParseAmount`、`ParseScaled` 与 `OpLabel`（`ld.Dev.List` 打印本进程已加载的批准目录），
经验与金币只接受不超过 18 位的正十进制整数，测试属性按显示单位精确换算（点数与百分比至多两位小数，射程整厘米），不四舍五入；结果打 `LD_DEV_TEST_RESULT` 日志并在 HUD 提示行显示。
`Lockdown.Rules.DemoDevTestCommands` 覆盖形状、开关拒绝、受理流、金币/经验提交、重放不重复提交、旧生命/离局/关局拒绝、
冷却轨道重置、学习（阶段、非法 id、空槽替换、成功安装与 Combat 投影、无机会拒绝）、死亡时买入被拒与补满纯规则，
以及测试属性：形状、纯层操作、单独的 Dev 投影行与临时项叠加、重复/负值/`ADBonusRatio`/越界拒绝、
死亡时设置不推进任何修订、越界命令不改状态、成长输入夹带测试层被拒、真实学习保留测试层、清除只去掉测试层；
以及重置：持有一件装备时死亡中重置，账本恰好两条 DevReset（不退款的 Consume 与回到开局金币的 Burn）且可复算并导出、
成长回到 1 级与开局机会且修订递增、Combat 安装空构筑与对应修订、技能冷却轨道被丢弃、重置后可再次学习；
以及整局操作：拥有客户端发送被拒、没有营地 director 时拒绝、发育期外提前开圈被拒且不改开放时刻、发育期内开放时刻提前到提交边界，
`FLDCaptureReducer::DevOpenAt` 的纯规则（未启用、不更早、已开放拒绝，提前后由 `CloseBoundary` 开放）；
`Lockdown.Rules.DemoCamp.Lifecycle` 覆盖 `DevRefreshNow` 只计已清空营地、截止提前到边界且不推迟更早的截止。
`RunDemoGrowthCheck.ps1` 在第二局导出干净账本之后再跑 DEV 段：买方经远程拥有连接发送旧局（`OldMatch`）、
重放（原回执、不重复提交）、同号异条款（`RequestConflict`）、加金币、离店买入与卖出、重置冷却、补满、
在开局机会上学习、无机会学习（`NoOffer`）、设置测试 AD（本人视图的 Dev 行与最终值）与清除、再买一件后重置到开局
（本人视图回到 1 级、0 经验、无卡、空槽、开局金币与一组开局 Offer）、整局的刷新营地被拒（`NotAuthority`），Listen Host 经本地拥有 RPC 给自己加金币，买方完成后刷新营地并提前开圈
（仍在发育期时必须开放并看到 CaptureOpen；若已过发育期则必须以 `StaleContext` 拒绝）；
随后导出的第三份账本以 `--dev-fixture` 复算，必须恰好列出两人各自的 DEV 铸币与买方重置的两条 DevReset，并与买方本人投影一致。
其中 Listen Host 的加金币与刷新营地、买方最后一步的清除测试属性都经 HUD“测试”入口与 `ULDDevTestPanel` 的按钮发送
（买方先确认面板不显示房主专用操作、格式错误的经验数量在本地拒绝且不发送；Host 截一张打开面板的 `HostDevPanel.png` 供审阅）。
面板的纯格式化（目录行、已有等级、替换条件、属性行、结果行）与输入解析由 `Lockdown.Rules.HUDDevTestPanel` 覆盖。
刷新营地只在规则测试中验证到截止提前，场景只验证受理与回执，未在场景中观察营地实际重生。
`RunDemoGrowthCheck.ps1` 是 DEV 拥有连接场景：渲染的参赛 Listen Host 作受害者并带 `-LDDevGrowthFixture`，
在 Lobby 按 Start 前为买方暂存全部七组 Offer（先拒绝一组重复卡的预置），第一组即买方的开局 Offer；
双方在每局首帧都只有恰好一次待选的开局机会（1 号、未消费、三张互异卡），死亡复活后机会数仍等于等级，
不重发开局机会；一个真实拥有客户端
经自身连接真实击杀得奖励、完成一次选择与回执查询，再经 `ApproachShop`→`OnShopArrived` 开店，
买两件最便宜的同款（`item_iron_plate`）、满槽 `EquipmentFull`、同号重放，出售其中一件，最后离店 `OutsideRange`。之后买方把 Q 的技能升到 2 级、
在 W 和 E 分别学习第二、第三个主动并使用 Q；Q/W/E 已满时只点新主动不确认替换被 `ReplacementRequired` 拒绝且不消费，
确认替换 Q 后旧技能及等级从构筑消失、新技能 1 级绑定自己的就绪轨道，被遗忘的技能只以 1 级学习卡返回，
重学到 W 后绑定原 `IssuedCooldownId` 与原截止时间。随后 Host 经夹具给自己发同等级 XP 与金币（账本 `DevFixture`），
用自己的攻击命令击杀买方；买方死亡等待中确认一次选择：按 Dead 阶段受理并应用，仍为同一 Life 与复活时间、
无身体、HP 为零，只有下一条命的最大 HP 增长，正常复活后满血满蓝。Host 随后走到低档营地 L1 补掉两只野怪；
作为 Listen Host 的击杀者，它对买方和两只野怪的每笔奖励都要在本人 `OwnerState.Growth`（本地控制器由 `SyncOwner`
直接写入并调用 `OnRep_OwnerState`，不走复制）与渲染 HUD 的米/等级文本里达到精确的新值，
渲染的画面回馈（`ULDWorldFeedback`，DEV 构建记录本局呈现过的奖励行）也必须以“+N 米”“+N 流量”收尾且合计等于该笔奖励。Host 导出账本证据由
`CheckEconomyEvidence.py --dev-fixture` 复算并与双方本人投影比对，且只列出 Host 自己那笔夹具铸币。
随后 Host 无胜者结束本局并回到新一代 Lobby，
两人 Ready 后正常开第二局：双方按本人投影核对等级/经验/金币回到开局值、无技能/属性卡/装备、
只有本局新抽的一次开局机会（不是第一局的任何 Offer），Combat 无来源与冷却债；第一局的选择与 Buy 原样重放被 `OldMatch` 拒绝、旧局回执查询为 `InvalidContext`
且不改任何状态；第二局账本在新 LedgerMatch 下重新初始化并经复算。
开局走正常 Lobby Start（全部开局提供者已注册），不覆盖营地反击/回位/刷新、AI、喷漆守护或终局。
真实拥有连接下的奖励、选择、买卖、死亡/终局与新局隔离由 N05 与集成场景验证；
纯规则通过不代表联机场景已经通过。

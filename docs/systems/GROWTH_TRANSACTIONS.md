# 成长事务与回执

可复用的是身份、版本校验、原子提交和反馈机制。Demo 没有装备、金钱与商店（#144 删除）；
成长只有经验、等级、机会与三选一，范围见 [App Spec](../APP_SPEC.md)，
目标合同见 [成长与奖励](../demo/PROGRESSION_AND_REWARDS.md)。

## 状态与入口

| 入口 | 责任 |
| --- | --- |
| `Development\LDGrowthDirector` | 成长域枢纽：死亡奖励汇点、地图事件奖励、Transactions 分派与拥有者发布 |
| `Development\LDDevelopmentDirector` | 等级/XP、机会队列与唯一持久三选一、Q/W/E 与被动、属性卡 |
| `Development\LDGrowthProtocol.h` | 命令、回执、查询、提交结果与本人视图的线协议 |
| `Development\LDGrowthProviders.cpp` | 向对局门禁注册 Progression 提供者及最终等级读取 |

`ALDGrowthDirector::SpawnDomain` 在核心初始化时创建成长与发育两个 director，`RetireDomain` 随
`ALDGameMode::RetireMatchDomains` 退休；Progression 提供者的 Prepare 以锁定名单（真人加 AI 槽）
初始化成长记录（每人一次开局机会及其已抽好的首组 Offer）。复活或读取投影都不会再授予开局机会。

拥有连接的 RPC 在 `Framework\LDPlayerGrowth.cpp`：`SubmitChoice` 分配 RequestId 并在 RPC 前记入在途，
服务器入口只调用 `QueueChoice`。本人视图随 `SyncOwner` 一起复制在 `FLDOwnerState.Growth`
（Listen Host 的本地控制器由 `SyncOwner` 直接写入并调用 `OnRep_OwnerState`）。
三选一页面 `ULDChoicesPanel` 只读 `ReadOwnProgression`，经 `SubmitChoice` 提交；开局 Offer 首次到达本人、
尚未消费且没有其他主面板或输入占用时，HUD 自动打开它一次（界面未就绪时最多重试约 5 秒），不替玩家选择；
玩家 6 s 内未点选卡片时面板自动收起为待选提示（`LDHUD::FChoicesFold`），收起不改变机会与 Offer。

## 复合事务

奖励与选择都按同一形状提交：纯 Prepare 完成全部可失败工作（整数 checked、版本、容量、
Combat 投影与 HP/MP 同比换算），在同一调用栈重验许可后进入不会失败的 Apply，然后标记发布。

| 事务 | 同次提交 |
| --- | --- |
| `CommitDeathReward` | DeathKey 去重与事实指纹（含冻结的助攻者名单）；击杀者与每名助攻者各一份累计 XP + 逐级机会（`CommitRewardShares`，`LDAssist::SplitKillReward` 平分），升级时各自 Combat 投影，全部准备后一起安装；无主/自杀/离局为 NoReward 且没有助攻 |
| `CommitEventReward` | 地图事件（空投）以 (Event, Serial) 为业务身份，整局只付一次；同一事件的第二次领取以 `DataInvalid` 永久拒绝，不付任何经验 |
| `CommitPendingChoice` | 原卡/版本/生死阶段重查；学习、升阶、属性或确认替换；消费一次机会；之后才抽下一组。重抽（`bReroll`，`OptionId` 为空）只核对队头 Offer/修订与剩余令牌，原地换成同规则新抽的一组（尽量避开上一组三张）并标 `bRerolled`，不消费机会 |

每笔装入的奖励都打一行 `LD_REWARD t=… contender=… cause=death|map_event|dev_fixture role=killer|assist|recipient xp=… xp_after=… level=a->b <来源>`，
升级时另打 `LD_LEVEL_UP`。`ProcessTransactions` 按服务器受理顺序处理选择（DEV 构建另有测试命令）。
单条请求失败只形成拒绝回执；只有失去写许可等权威故障才返回 false。
`PublishGrowth(Boundary)` 在 Close 之后执行，不需要许可，只对变化者调用 `SyncOwner` 并推送终态回执。

业务去重分层完成：请求序号屏障（每族 64 份回执窗口与终结水位）、DeathKey 生命水位与事实指纹（最近 64 份奖励回执可原样重放，
同键不同事实为 `DeathConflict`，窗口外的旧生命为 `StaleDeath`），以及地图事件的 (Event, Serial) 集合。
窗口外的旧请求返回 `ReceiptExpired`，不重新执行。

## 对外信息

普通客户端只接收自己的等级、经验、Offer 与自身回执。可见对手只经世界/视野发布获得等级与 Q/W/E 主动，
不含冷却、XP、被动或属性卡。拒绝原因只给有权的本人。

## 检查入口

`RunTests.ps1` 中的 `Lockdown.Rules.DemoGrowth*` 覆盖线编码、请求窗口、等级/候选/替换纯规则（替换的新主动从初级开始、
升级位/新技能位/自由位与满槽新主动权重、`draftable` 排除）、技能等级成长 G(L)、开局机会（每名参赛者恰好一次且随首组 Offer 生成，
只有升级再加机会，新局重建）、免费重抽的令牌/去重/每组一次与本人视图隐私白名单；
`Lockdown.Rules.DemoAssist.*` 覆盖助攻资格、经验平分与击杀者/助攻者的复合奖励。

DEV 成长夹具（`Development\LDDevGrowthFixture.*`）只在 `WITH_DEV_AUTOMATION_TESTS` 构建中存在，
只在以 `-LDDevGrowthFixture` 启动的权威进程、Lobby 或当前开放对局内生效，没有 RPC，客户端、AI 和正常玩法不会调用。
Host 的 DEV probe 在游戏线程直接调用：
`ALDDevelopmentDirector::DevQueueOffer(Contender, 三个定义)` 为该参赛者之后生成的队首 Offer 按先进先出各预置一组，
入队时按当前构筑的 `LegalPool` 校验（恰好三张互异、种类由构筑推出、不超过剩余可抽的 Offer 数），
抽取时按当次合法池再验，失效则打 `LD_DEV_FIXTURE_PRESET_REFUSED` 错误并按正常种子抽取。
开局 Offer 在 `BeginMatch` 中生成、早于任何对局内调用，而成长域 director 直到 Start 时才创建，
因此 Lobby 阶段改用 `LDDevGrowthFixture::StageOffer(World, Contender, 三个定义)`：由夹具自身暂存给下一局
（按空的开局构筑校验，接收者须是当前 Lobby 成员）；`BeginMatch` 经 `TakeStaged` 把锁定名单内的暂存移入本局队列，
由开局 Offer 先消费，名单外的暂存记错误丢弃，旧局队列从不带入新局。
选择仍经 `SubmitChoice`→`CommitPendingChoice`→Combat 安装。
`ALDGrowthDirector::DevGrantReward(Contender, XP)` 排入下一个 Transactions 阶段，
与死亡奖励共用 `CommitRewardParts` 复合提交（`PrepareExperience` 与 Combat 投影，`cause=dev_fixture`），
不伪造死亡事实；返回票号由 `DevGrantStatus` 轮询 Pending/Granted/Rejected。开局与每次调用都打 `LD_DEV_FIXTURE_ACTIVE`。

DEV 测试命令（`Development\LDDevTestProtocol.*`，客户端入口 `Framework\LDPlayerDevTest.cpp`）是同一开关下给拥有连接用的请求族，
只改调用者自己的参赛者，不能指定别人；两个整局操作（刷新营地、提前开圈）只接受 Listen Host 自己的连接
（服务器上只有它的控制器是本地控制器，`QueueDevTest` 的 `bHostConnection`），其他连接在受理时回 `NotAuthority`。线协议在所有构建里存在，权威只在同一 `LDDevGrowthFixture::Admit`
通过时受理（`WITH_DEV_AUTOMATION_TESTS` 构建、Host 带 `-LDDevGrowthFixture`、Demo 房间权威），否则回 `NotAuthority`。
`SubmitDevTest` 与选择一样从已接受的 OwnerState 填 Generation/Life、分配 RequestId，一次只有一条在途；
服务器 `ALDGrowthDirector::QueueDevTest` 复用 `TLDGrowthRequestStream`（单调序号、64 份回执窗口、冲突/乱序/过期拒绝），
已接受的命令在 Transactions 阶段与选择按服务器到达顺序提交（`CommitPendingDevTest`）。
提交时重查本局与生命：离局为 `LeftMatch`，Life 不等于当前生命为 `OldLife`（旧命令不作用于下一条命）。
操作号 2、6、7（原加金币、买、卖）随 #144 退役，不再复用。

| 命令 | 提交 |
| --- | --- |
| `GrantExperience`（数量 > 0） | 与死亡奖励同一 `CommitRewardParts` 复合提交（`cause=dev_fixture`）；可重试拒绝保留待下一阶段 |
| `RefillResources`（数量 0） | 当前 Alive 身体的 HP/MP 设为已提交上限，不动护盾；非 Alive 为 `Dead` |
| `ResetCooldowns`（数量 0） | 该参赛者全部冷却轨道（技能与 D 冲刺、已绑定或未绑定的债）截止时间提前到本边界，身份与绑定不变 |
| `LearnOption`（定义 id，槽 0..3） | `ALDDevelopmentDirector::DevChooseOption`：定义须在当前构筑的 `LegalPool` 中，作为队首机会的结果消费它，与 `CommitPendingChoice` 共用 `InstallOption`（ApplyOption、修订、下一组的池、Combat 投影、抽下一组）；带槽时以该槽当前技能确认替换；没有机会为 `NoOffer`；Alive 或 Dead |
| `SetAttribute`（Stat，数量 ≥ 0）/ `ClearAttributes`（数量 0） | `ALDCombatDirector::DevSetAttribute`：改 Combat 已安装构筑的测试层 `FLDBuildProjectionInput::DevBonuses`（每个属性至多一项、按属性排序；点数 P2、比例基点、射程整厘米；不接受只限临时项的 `ADBonusRatio`；0 去掉该项，Clear 全部去掉），经 `PrepareProjected`/`ApplyBuildChange` 重投影安装：资源上限按比例换算，活着的身体保留本命临时项，不推进构筑修订，成长域在途请求不失效；投影拒绝（最终值越界）为 `NumericDomain`、不改任何状态；Alive 或 Dead（首次出生前还没有 Combat 记录，为 `DataInvalid`） |
| `ResetToMatchStart`（什么都不带） | `ALDGrowthDirector::CommitDevReset` 一笔复合提交：`ALDDevelopmentDirector::DevPrepareReset`（1 级、0 经验、无技能与属性卡、只有开局机会、Apply 时与 `BeginMatch` 一样抽开局 Offer；各修订继续递增，旧状态的在途选择按过期拒绝）与 `ALDCombatDirector::DevPrepareReset(Contender, ResultBuildRevision)`（经 `PrepareProjected` 安装空的 1 级构筑、去掉测试层）都可行才依次安装，随后 `DevFinishReset` 丢弃全部技能冷却轨道、冲刺就绪、活着的身体补满；Alive 或 Dead |
| `RefreshCamps`（只限 Host） | `ALDCampDirector::DevRefreshCleared` → `FLDCampMatchState::DevRefreshNow`：每个已清空、等待刷新的营地截止时间提前到本边界（不推迟更早的截止），下一个 RespawnsAndWorld 经 `AdvanceCamps` 正常整组刷新（含出生点被占时的等待）；存活营地不动；回执写出等待中的营地数；没有营地 director 为 `DataNotReady` |
| `OpenCapture`（只限 Host） | `ALDGameMode::DevOpenCaptureNow` → `FLDCaptureReducer::DevOpenAt`：只在 Development，把未开放旗杆的开放时刻提前到本边界，同一步随后的 Capture 阶段照常开放并进入 CaptureOpen；喷漆、守护时长与全部规则不变；其他阶段为 `StaleContext` |

终态回执在 `PublishGrowth` 的 `SyncOwner` 之后经 `ClientDevTestResult` 送达，受理结果同一通道立即返回。
普通学习、死亡与复活都不会顺带补满或重置冷却，只有显式命令会。新局重建成长域，命令流与回执不带入下一局。
测试层只由 Combat 持有：成长域的 `PrepareBuildChange` 输入不得带它（`InvalidInput`），Combat 把已安装的测试层带进每次真实构筑变更；
投影把它作为单独的 `FLDAttributeContributions::Dev` 行（本人属性视图的 `Dev`/`DevPercent`、HUD 来源“测试”），
最终值与临时项一样把它计入加和；测试层与真实构筑合计越界时，那次真实选择同样按投影拒绝；新局重建 Combat 记录，测试层不带入下一局。
控制台入口 `ld.Dev.XP <数量>`、`ld.Dev.Refill`、`ld.Dev.ResetCooldowns`、`ld.Dev.Learn <id> [Q|W|E]`、
`ld.Dev.Attr <属性> <数值>`、`ld.Dev.ClearAttr`、`ld.Dev.Reset`、`ld.Dev.RefreshCamps`、`ld.Dev.OpenCapture` 与 HUD 的测试面板（`UI/LDDevTestPanel.*`）
都只是本地发送者，共用 `LDDevTest::StatFields`、`ParseAmount`、`ParseScaled` 与 `OpLabel`（`ld.Dev.List` 打印本进程已加载的批准目录），
经验只接受不超过 18 位的正十进制整数，测试属性按显示单位精确换算（点数与百分比至多两位小数，射程整厘米），不四舍五入；结果打 `LD_DEV_TEST_RESULT` 日志并在 HUD 提示行显示。
`Lockdown.Rules.DemoDevTestCommands` 覆盖形状、开关拒绝、受理流、经验提交、重放不重复提交、旧生命/离局/关局拒绝、
冷却轨道重置、学习（阶段、非法 id、空槽替换、成功安装与 Combat 投影、无机会拒绝）与补满纯规则，
测试属性（形状、纯层操作、单独的 Dev 投影行与临时项叠加、重复/负值/`ADBonusRatio`/越界拒绝、
死亡时设置不推进任何修订、越界命令不改状态、成长输入夹带测试层被拒、真实学习保留测试层、清除只去掉测试层）、
重置（成长回到 1 级与开局机会且修订递增、Combat 安装空构筑与对应修订、技能冷却轨道被丢弃、重置后可再次学习）
以及整局操作（拥有客户端发送被拒、没有营地 director 时拒绝、发育期外提前开圈被拒且不改开放时刻、发育期内开放时刻提前到提交边界）；
`FLDCaptureReducer::DevOpenAt` 的纯规则与 `Lockdown.Rules.DemoCamp.Lifecycle` 的 `DevRefreshNow` 另有覆盖。
面板的纯格式化（目录行、已有等级、替换条件、属性行、结果行）与输入解析由 `Lockdown.Rules.HUDDevTestPanel` 覆盖。

`RunDemoGrowthCheck.ps1` 是 DEV 拥有连接场景（`LDDemoGrowthProbe.cpp`）：渲染的参赛 Listen Host 带 `-LDDevGrowthFixture`，
一个真实拥有客户端经自身连接得奖励、完成选择与回执查询、死亡等待中确认选择、运行 DEV 测试命令，
然后正常开第二局并核对新局隔离（等级/经验回到开局值、只有本局新抽的开局机会、旧局请求被 `OldMatch` 拒绝）。
场景的逐步检查以该 probe 与脚本输出为准；纯规则通过不代表联机场景已经通过。

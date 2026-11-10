# 架构、会话与网络权威

本文描述当前原生组件，供复用和维护；产品范围以 [App Spec](../APP_SPEC.md) 为准。
工程入口与完整命令见 [开发指南](../DEVELOPMENT.md)。

## 模块与状态归属

| 边界 | 当前入口 | 职责 |
| --- | --- | --- |
| 运行模块 | `Source\Lockdown\Lockdown.Build.cs` | CharacterMovement、Enhanced Input、UMG、导航、AI、网络与规则 |
| 编辑器模块 | `Source\LockdownEditor` | 导入、Bootstrap、生成资产与保存包；不进入运行模块依赖 |
| 进程内会话 | `Framework\LDGameInstance.h/.cpp` | 网络/旅行失败回到前端，保存本地连接反馈 |
| 正常游戏旅行 | `Framework\LDGameEngine.h/.cpp` | 在创建世界之前拒绝旧本地 URL/模式或缺输入；GameInstance 单独执行带错误反馈的前端恢复 |
| 世界规则 | `Framework\LDGameMode.h/.cpp`、`LDGameModeSession.cpp`、`LDGameModeLifecycle.cpp` | 唯一 Demo 准入、Match 映射、Lobby、生命绑定、窄时序/写许可和会话回执；旧玩法提供者不自动创建 |
| 公共状态 | `ALDGameState`，定义在 `LDGameMode.h` | 明确公开的 Lobby、目标、时间及终局状态 |
| 玩家意图 | `Framework\LDPlayerController.h` 及各 `LDPlayer*.cpp` | 自身状态、RPC、输入路由和回执 |
| 身体 | `Character\LDCharacter.h/.cpp` | 原生运动、相机和可辨识表现，不自行结算经济或胜利 |

表中模块内路径分别对应 `Source\Lockdown\Public` 和 `Private`。
运行代码不依赖 UnrealEd；Game 与 Editor 目标都需要能独立构建。

## 进入一局

`Config\DefaultEngine.ini` 指定 `L_Frontend`、`ALDGameMode` 与 `ULDGameInstance`。
正常游戏引擎设置为 `ULDGameEngine`。仅设置 `InitGame` 的 ErrorMessage 不会阻止 UE 5.8.4
继续初始化世界；仅返回空 GameMode 也会使正常游戏的本地玩家创建触发引擎 fatal。
因此正式本地旅行先在 `Browse` 拒绝，返回 Failure 并保留原 URL/原因，
`StartGameInstance` 再发起独立的干净前端旅行；这不是把拒绝的请求标为成功。
GameInstance 的 GameMode factory 仍在允许的世界上核对完整包路径与保存的模式，作为第二层防护。
`ULDUIRoot::Host` 只请求 `L_Demo?listen?LDName=<本人名称>`，Join 在 wire/产品/目录参数后同样附 `?LDName=`；
引擎会用在线子系统昵称覆盖保留键 `?Name=`（`UPendingNetGame` 与本地玩家 `SpawnPlayActor`），所以名称不走该键。
名称先经本地 `ValidatePlayerName` 校验且必填；服务器在 PreLogin 与 `InitNewPlayer` 用同一规则解析 `LDName`
（`LDMatchLifecycle::ResolveContenderName`），非法即明确拒绝（`InvalidName:`），缺省时取明确默认名“玩家”，
并以 `ChangeName` 同步 PlayerState；入房时 PlayerState 名称再按同一规则核对，在线子系统昵称不会成为参赛名。
Host/Join 均先核对 Demo 载荷与编入程序的 SHA-256。
读取的是原始字节，不规范化换行，也不回退旧目录。`LDDemoEntry::ValidateSelection` 只接受
完整的前端/Demo 包路径，剥离 PIE 前缀，但不接受同短名的其他包、旧 Profile 或 Humans 覆盖项。
旧保存模式和地图的物理移除须与资产引用审计同批完成，不保留可启动的旧预览分支。

`ALDGameMode::InitGame` 为有效 Demo 房间一次创建 `FLDMatchContext`；
网络 Generation 通过 GameInstance 的有界单调分配器取得，LedgerMatch 单独生成并显式绑定。
`PreLogin` 检查阶段、人数、wire/product/content 兼容性，最终 `PostLogin` 再检查阶段与三真人容量；
不会先创建可用 Pawn、旧职业构筑或奖励再发现房间已满。两处拒绝都以 `<原因码>: <英文说明>` 送出稳定原因码
（`LDDemoEntry::JoinRefusal`：MissingDemoDefinition、MatchAlreadyStarted、RoomFull、ProtocolMismatch、
ProductMismatch、ContentMismatch、InvalidName）；`PreLogin` 的这些检查先于引擎会话的人数检查，否则满员只得到不带原因码的
"Server full."。客户端只把已知原因码映射为中文原因并附上该码，不显示英文说明或引擎文本：`PreLogin` 拒绝随待连接失败送达；
`PostLogin` 拒绝时客户端已载入房间，原因经 `ClientWasKicked` 送达后返回前端。拒绝或网络、旅行失败使客户端离开房间世界时，
第一个原因保留到前端，随后同一世界的断线不再覆盖它；正在离开的房间世界也不再建立房间界面，以免界面把该原因记为已显示。
前端不创建参赛记录，Lobby 中的身体阶段为 Unspawned。正常开局由 `LDMatchProviders` 门禁：
Combat、Progression（含最终等级读取）、Camps 均已注册，且有 AI 名额时 OpponentAI 已注册，
Demo 世界注册表已加载，才允许 Host 开始；缺失项逐个写进 Lobby 的 StartBlocker 与 Start 回执。
提供者以各自翻译单元中的静态 `FRegistrar` 登记钩子（Prepare/MatchClosed/MatchReset/LifeCommitted/
ReadFinalLevel），GameMode 不包含提供者头文件；注册本身不等于玩法已正确接通。

`ServerDemoSessionCommand` 传入网络 Generation、单连接序号、命令、结构名单版本及其必要字段，
发送者由拥有 Controller 决定；只有 Host 可以改变 AI 数量、开始、结束本局或带房间回 Lobby。
`EndMatch` 只在 Rest/Development/CaptureOpen 于两步之间受理，以最后已关闭边界无胜者锁定结果（其他阶段以 `WrongPhase` 拒绝）；
有待重试的死亡奖励（或因此等待结算边界的守护完成）时返回 `RequestInFlight`，下一边界结清后才可结束。
`ReturnToLobby` 只在 Results 受理并须携带当前 ResultId。离开房间沿用断开连接/`Logout`，没有另一条 RPC。
Listen Host 离开（菜单离开房间或对局）时，引擎换图先关闭网络驱动、后销毁 Host 的控制器，`Logout` 已无法通知成员；
因此 GameMode 在本世界的 `PreLoadMapWithContext`（早于关闭网络驱动）关闭房间并向每名成员发出带原因的
`ClientDemoRoomClosed`，这些通知随引擎关闭各连接前的最后一次发送先于断线到达，客户端以该原因返回前端。
Host 进程被结束或窗口被直接关闭时没有这一步，客户端显示会话中断。
Ready 是设置布尔值，不是服务器 Toggle。准备变化只推进公共发布版本；
成员/AI 数量改变才推进结构版本并清除所有真人 Ready。
`FLDSessionRequestWindow` 只保存当前在途请求、最近终态及处理水位：
同键同条款重放，改条款拒绝，过窗拒旧，同域重入不能开始第二次写入。
这是会话窗口，不代替成长、战斗或导航各自的去重机制。

Host 同时运行权威世界和自己的客户端表现。IP 直连不依赖匹配服务，也不会自动解决 NAT。
必须分别验证独立拥有客户端和可见的 Listen Host；拥有 Host 进程的人本来就能访问权威状态，
视野过滤不等于能防范恶意 Host。

## 私有、公共与观测状态

- `FLDOwnerState` 属于具体连接的自身投影；`ALDGameMode::StateFor` 是服务器访问参赛状态的入口。
- 玩家身体和 PlayerState 的网络归属不能泄露其他人的私有资源。
- `ALDPlayerState` 只对拥有者相关，并按引擎默认每秒更新一次。Listen Host 默认每秒只 tick
  `NetClientTicksPerSecond` 个客户端连接，帧率高于 100 fps 时每帧只轮到一个；这与 1 Hz 更新锁相后，
  会把本人 PlayerState 的首次复制推迟数秒到近 20 秒。因此 `Config\DefaultEngine.ini` 的 `[ConsoleVariables]`
  设 `net.MaxConnectionsToTickPerServerFrame=8`，每个复制帧都 tick 全部客户端连接（最多两名远端真人）。
- 其他身体的可见表现主要通过观测中继，而不是把所有权威 Actor 广播出去后在 UI 上隐藏。
- GameState 只放明确公开的事实，不应成为全部玩家位置、背包和构筑的总表。
- `ALDGameState::DemoLobby` 是名单、Host/Ready/AI 名额及公开阻塞原因的只读投影，
  不包含 LedgerMatch、身体坐标或私有构筑。每个 AI 名额在开局时绑定一个服务器 `ALDOpponentController`。
- `OnRep_DemoLobby` 通过同一有界读模型检查再发布给 UI：成员身份不得重复或超容量，
  Host/AI/Ready 形状必须一致；旧 Generation、重复/旧发布版本及结构版本回退不能覆盖已接受状态。
- `ALDGameState::PublicMatch` 是 UI/AI 的唯一阶段与旗杆状态来源：阶段、开放时刻、喷漆区状态
  （未开放/无人/独自喷漆/争夺/主人独守，即 `NotOpen/Empty/Solo/Contested/Guarded`）、唯一喷漆进度持有者
  （独自喷漆者，或争夺中被暂停的喷漆者及其 `PausedProgressMs`）、CaptureEpoch 及喷漆起点/喷上时刻，
  涂鸦主人 `GraffitiOwner` 与其身份色槽位 `GraffitiPresentation`、运行中守护的获胜时刻 `GuardDueAtMs`
  或被冻结的守护进度 `GuardKeptMs`；不列喷漆区内人数、
  保护者或隐藏坐标。实时显示时间取同代 `DemoClock.NowMs`。`MatchResult` 仅在 Results 发布一次，
  按开局锁定名单列出全部参赛者（含离局者）的名称、击杀、死亡与最终等级，以及胜者、原因和用时；
  `KillFeed` 是最近 8 条参赛者死亡的公共环形缓冲（击杀者仅限参赛者，营地按档位），不带技能、构筑或位置；
  它的 `Scores` 按名单顺序列出每名参赛者至今的击杀/死亡/助攻（按住 Tab 的排行榜），只随死亡变化，所以与通知一起按最后序号拒旧。
  联机协议版本（`LDDemoEntry::WireProtocol`）当前为 18：#198 公共休息室增加真人准备 RPC、OwnerState 服务/准备字段与时间线准备名单，训练重置绑定当前生命和休息步骤。PK 使用站圈报名（用户选择 A），没有报名开关 RPC，
  世界多出报名圈锚点（布局修订 9）；双方须使用同一构建。
  16 是 #198 规则工坊整合后，公开对局视图新增时间线、符咒与 PK/观众数据，
  并新增蓝图、PK 与休息阶段的 RPC（观察 schema 仍为 8）时的版本。
  15 是 #198 以构建命令（选主动技能、学图纸、分配属性点）取代三选一卡牌，
  成长本人视图带属性点、待学图纸与构建安全窗口，观察 schema 8 的公开 Q/W/E 不再带技能等级时的版本。
  14 是 #144 删除装备、金钱与商店（观察单位不再带装备槽，成长本人视图与回执不再带账户、装备与商店字段）时的版本；13 是观察 schema 7 公开 Q/W/E 三个主动槽与扩展的技能表现、
  每个单位最多 14 个公开状态时的版本；12 是观察 schema 6 使用有界无损覆盖游程编码时的版本；
  11 是 #153 新增开局方向选项、本人已选方向与选择后的技能揭晓回执时的版本；
  隐藏分配不在复制结构中。10 是 #147 第 2 层删除旧目录专用状态（狂暴、伏击就绪、重伤、格挡、易伤）后，
  每个单位的公开状态上限（`LDObservation::MaxStatuses`）降为全部公开状态同时存在的 11 个；
  9 是新增公开状态沉默、灼烧并把上限提高到 12 个时的版本；8 是 `ELDMatchPhase` 在 Starting 与 Development 之间插入 Countdown（#153）时的版本，
  7 是去掉装备主动入口（`ELDCombatEntry` 只剩 Q/W/D）与公开施法的装备来源时的版本。
  三者都经各自 OnRep 的形状/代次/版本检查，旧代次或重复发布不能覆盖已接受值。
- 世界观察只使用 Demo 的单一稀疏协议；旧 B91/B92 控制器与独立观测/服务格式不作兼容入口。
- 局末构筑蓝图（#198 §10）不进 GameState：结果发布后由 Host 只经各真人自己的 `ULDBuildBlueprintComponent`
  （`ALDPlayerController` 默认子对象）发出 Offer，拥有者按顺序拉取分块并在本机 `Saved/Blueprints` 写文件；
  见[视野与 UI](VISION_AND_UI.md#构筑蓝图文件与查看)。

详细可见性机制见 [视野与 UI](VISION_AND_UI.md)。

## 请求、代次与回执

所有权由拥有连接决定，不能相信客户端传入的另一名玩家标识。
命令携带其所需的 match、life、sequence/order、build/object revision，
目标使用当前获准的观察句柄或公开地点 ID，不使用隐藏 Actor 指针。

先验证代次、身份、目标和资源，再提交一次结果。重复的已接受请求返回原回执，
不能重复发钱、创建物品或施放效果。旧 life 的投射物、输入、菜单报价和移动确认不能作用于新身体。
UI 的 Pending 不代表已经提交，客户端不能自行生成成功回执。

`ULDNavigationComponent` 的取消 epoch 与成长的 request 回执
是不同领域的版本，不要用一个全局“最新版本号”替代全部语义。

## 生命周期与失败

Demo Lobby 不通过 `RestartPlayer` 自动生成旧身体。普通离开使当前成员失效；
Host 离开关闭房间并通知其余连接，不把 Host 移交给第一个剩余玩家。
死亡、复活与保护由 `Framework\LDGameModeLife.cpp` 提交：锁定的 Combat 致死事实结束当前生命
（撤销 Combat 身体、取消本生命订单与未释放动作、销毁 Pawn，等级/构筑/来源冷却保留），
复活等待按死亡时的野外进度 T 取 `LIFE.RespawnSec.Field1/Field2/Field3/Final`，到 `RespawnAtMs` 时从全部出生锚点等概率抽样一次、局部预留，再原子提交新 Life、满 HP/MP 与保护截止。
`SpawnBlocked` 时保持 Dead，后续边界以同一选中锚点重试，不重抽“更安全”的点。
成长、营地与终局按各自提供者接入。
`IsGameplayWriteOpen` 只是当前 Match/阶段的快速检查，不是正式提交 permit；它在 Rest/Development/CaptureOpen 为真。

外层流程（#198，用户 2026-11-10 批准[外层参数](https://github.com/rlsn/Lockdown-alpha/issues/198#issuecomment-6752601175)，原话：“批准整套方案，按票内参数实施”）：
#153 的开局倒计时已删除（`ELDMatchPhase` 数值 2 退役）。`StartDemoMatch` 把每名参赛者的第一条生命放在公共休息室按名单槽位固定的 `rest_spawn_<槽位>` 后进入 `Rest`（开局休息 R0），
`InitializeCaptureMatch` 把 `MatchStartedAtMs`（`PublicMatch.StartedAtMs` 与结算的 `StartedAtMs`）设为此刻，以 `MAX_int64` 开始喷漆归约器，
并由 `FLDMatchTimeline` 推进 R0 → 野外 1 → R1 → 野外 2 → R2 → 野外 3 → R3 → 最终野外。时钟只有一条：野外进度 T 只在野外段推进、休息与 PK 中冻结，
外卖、上热搜、画饼老板、营地刷新、营地成长阶段与营地标记都按 T 以 0 为起点计时（`GetFieldNowMs`、`FieldDueAuthorityMs`）；
#198 外层参数 §5 的地图事件（外卖固定落地表与 40 s 消失、老板 T=240 s 出现并在进入第三次休息的 `FreezeForRest` 中退场、营地标记在各段
T+15 s 公开并在 `EnterRestWorld` 过期）由 `ALDGameMode::AdvanceAirdrop`/`AdvanceFieldMarkers`（RespawnsAndWorld）与
`ALDCampDirector::AdvanceCamps`/`FreezeForRest` 驱动；标记由营地死亡（`ConsumeMemberDeath` 的 CampCleared）领取，奖励经
`ALDGameMode::PayFieldReward` 进入 `ALDGrowthDirector::CommitFieldReward`，可重试的拒绝在之后每个 Transactions 由 `RetryFieldRewards` 重付；
最终野外开始的边界由 `FLDCaptureReducer::ScheduleOpen` 定为开放时刻，同一边界的 Capture 阶段从 Development 切到 CaptureOpen。
`NextTimelineDueMs` 让每个休息步骤与野外段终点成为精确步边界；driver 在 RespawnsAndWorld 开头调用 `AdvanceTimeline`，
在 Transactions 末尾调用 `LockTimelineBuild`（截止时刻提交的构筑命令先结算再锁定）。休息中 driver 跳过营地运动、营地意图与世界推进，
Combat 挂起营地成员与 Boss；阶段边界的生命退休/放置、效果取消与资源补满见[会话、对局与生命周期 §3.4](../demo/SESSION_MATCH_LIFECYCLE.md#34-休息野外进度与阶段边界198)。
写闸在休息中保持打开；构筑阶段由 `ReadBuildStage` 给出（Build/Payout 步骤为 Rest、其余休息步骤为 RestLocked、野外为 Live）。
返回倒计时（Return 步骤）中 `IsInputLocked` 为真：导航订单（含 Shift 队列步骤）回 `ELDOrderFailure::NotStarted`，Combat 的普攻、Q/W/E 与冲刺 D 回 `ELDCombatReject::NotStarted`，
这些拒绝记入各自的回执窗口，进入野外后重放仍得到同一拒绝。对手 AI 的身体决策只在 Development/CaptureOpen 运行，休息中只在构筑开放的步骤处理成长。
PK 报名是实体的站圈报名（用户选择 A）：没有报名 RPC，`ALDGameMode::RefreshRestSignups` 在报名窗口内每拍按已提交脚点是否在本人
`pk_signup_<槽位>` 圆盘（`SignupDiskFor`，半径 `AIRDROP.PickupRadiusCm`）内重算公开名单，截止时刻再算一次并锁定。
会话 D 注册可选提供者 `ELDMatchProvider::PK`（`ALDPKDirector`）：竞猜与投掷经 `ALDPlayerController::ServerPKBet`/`ServerPKThrow` 交 `ALDPKDirector::SubmitBet`/`SubmitThrow`
判定，回 `ClientPKBet`/`ClientPKThrow`（投掷另带下次可投时刻）；AI 走同一入口。观众与被淘汰者由观察所有者显式授权看见全部场内选手、场区覆盖
与场内技能投射物和预警，同队 PK 生命共享各自合法视线的并集（投射物与预警同样按队友的脚点与视线判定），被暴露的选手只对本场 PK 生命额外可见。
`LDMatchLifecycleTypes` 的 VictimKey/BodyRef/DeathKey 区分稳定主体、一次生命和短寿命身体。
GameMode 的 `Lives` 保存唯一当前生命阶段/绑定及退休水位；当前 HP/MP/盾仍只在
`Combat.Bodies.Resources`，OwnerState 是 P2 到显示点数的投影，不从旧 double Hp/Tactical 状态补值。
初次身体绑定只接受已登记的 Unspawned 主体、当前 Starting permit 和重新核验的真实合法位置，
重复调用不再初始化资源。初次出生（放在休息锚点）、阶段放置与复活共用同一落地/准备/提交路径（`PlaceContenderBody` 的目标为抽点、指定锚点或就近搜索）；初次出生不附加保护。

GameMode 用同一比赛原点把原生游戏时间变为可执行预算，Combat driver 按真实可达整数上界
推进同一 CMC 区间；不写 `UWorld::TimeSeconds`。`AuthoritySecondsToNowMs` 使用受检 floor，
事件/接收审计桶使用受检 ceil，整数期限直接受检相加，不经 double 往返。
迟收地面意图携核心生成的 NotBefore 时间，不能作用于更早原生区间。
`FLDGameplayWritePermit` 仅由核心在当前调用栈构造，禁止复制/移动；
最终写入重查 Owner、Match、边界、阶段、GateRevision 和当前借用地址，重入不能再发许可。
阶段按 Motion → Session → Actions → Effects → DeathsAndRewards → RespawnsAndWorld → Transactions → Capture → Close
的契约顺序每步各执行一次，不为未接通阶段签发成功证明。Close 锁定唯一结果后，driver 仍完成该边界并调用
无 permit 的发布（成长 `PublishGrowth`、`ExecuteCapturePublish`），然后停止推进。
唯一例外是同刻结算边界：守护完成的边界仍欠一笔待重试奖励时，Close 保留提案，`BeginSettlementStep`
以相同 AtMs 和新 BatchSerial 开一步，只依次执行 DeathsAndRewards 与 Close，不要求原生 Motion。
当前 Motion 批次包含不可变的实际子段/原生支持样本，按 Body 的单一 stream
和连续记录前缀保留；回滚试探不进入提交批次。核心检查相邻批次的起点/支持与上次已接纳终点一致，
不允许两次记录之间的无凭据位置写悄悄成为新起点。退休后的已接纳批次不因弱 Pawn 指针失效而丢弃。
精确前缀确认、原生记录和完成计数都不是提供者 Fence 或可关闭比赛的凭据；
任一阶段缺少 Combat/圈事件消费或 Sealing 时明确故障，不能让缺失服务自动给出空成功。

`RunDemoEntryCheck.ps1` 使用真实游戏进程验证旧保存模式与显式 `?game=` 请求。
开发测试观察器区分“创建世界前拒绝”和“实际观察到被拒绝世界”；
后者从世界初始化/Actor 生成及子系统创建/激活处累计记录，销毁和回菜单不清除计数。
最后的前端 live census 只是补充，不能替代累计的“未激活旧玩法”证据。

`ULDGameInstance::NetworkFailed` 处理 pending connection 的 WorldContext 归属，
避免把一次连接失败写入另一个 PIE 实例。错误信息到前端，不无限停留在失效世界中。

每个对手 AI 槽由一个服务器 `ALDOpponentController` 驱动，经同一核心注册绑定、受限观察与权威命令接入，
不沿用旧 B92 席位接管或控制器交接；策略、请求与回执见[对手 AI](../demo/OPPONENT_AI.md)。
本局不重连，人数和 AI 行为按 App Spec。

喷漆、守护与唯一结果归 GameMode 核心；旧 Objective Actor、Seal/路线/站桩/HeartSensor 已退出。
`FLDCaptureReducer` 是唯一喷漆与守护归约器（`Begin(OpenAtMs, SprayMs, GuardMs)`）：生命提供者在提交点调用 `PublishCaptureEligibility`
（Joined/Respawned 带保护截止与初始支持样本、ProtectionEnded、Died 带 DeathKey 与击杀者），
Combat 在 Actions/Effects 许可内对存活参赛者身体提交定身、眩晕、嘲讽（及击退）时经 `ALDCombatDirector::InterruptSpray`
调用 `PublishCaptureInterrupt`（事实时刻为该边界 AtMs，种类 `Interrupted`；缴械、减速和伤害不调用），
Motion 阶段每个已接纳原生批次经 `PublishCaptureMotion` 由 `LDCaptureCircle::Extract` 求出带真实时刻的
进出（连续子段求交、支持面变化与不连续修正只取前后态；脚点在闭圆盘内且站在注册地面才算在喷漆区内）。
事实按提交序入队，到其整数桶封闭时才按原始秒、阶段、提交序归约，同一边界内的进出/死亡/保护变化
全部保留；保护者既不喷也不争，开放前不计时，Solo 起点取向上取整桶。区内只有一名有效参赛者且他不是涂鸦主人时他喷漆，
争夺冻结喷漆者的进度（其他人不累积），他再次独自在区内时 `SoloSinceMs` 回拨暂停值续计；他离区、死亡、离局或被打断即清零，
别人不继承。喷漆到期严格早于某条事实时先喷上；与到期同刻的死亡、离局、控制、离区先处理。
喷上涂鸦不结束对局：它覆盖原涂鸦、主人换人、守护从 0 计；守护只在 Guarded（主人独自在区内）运行，Empty（主人在区外、区内无人）
时暂停、Solo/Contested（区内有主人以外的有效参赛者）时冻结于 `GuardKept`，回到 Guarded 时从保留值续计（2026-11-07 用户决定，原话：“胜利判断好像还是有问题 - 当旗杆被占领但无人在圈内的时候不计算时间，时间停止，现在好像是时间继续计算是吗”）；
冻结期间主人从区内走出、且该原始秒的事实全部归约后区内仍有非主人，`GuardKept` 清零而涂鸦保留（2026-11-07 用户决定，同刻离开不清零、只暂停）；主人死亡或离局清除涂鸦。只有守护满时长给出结束对局的
`FLDCaptureProposal{Epoch, Winner, GuardSinceMs, DueAtMs}`。`NextDueMs` 给驱动的停靠边界：未开放时为开放时刻、
独自喷漆时为喷上时刻、守护运行时为守护获胜时刻。争夺暂停与守护暂停、冻结不给驱动额外停靠边界：步内能让保留的守护恢复的
只有 Motion 的进出区事实（离局在步前入队，死亡、控制、保护到期、复活都落在步的边界时刻）。驱动在 Motion 之后调用
`CaptureCompletesInsideStep`，由 `FLDCaptureReducer::CompletionInsideMs` 在归约器副本上预演已入队事实；
若恢复后的守护获胜时刻早于本步边界，本步跳过 Session～Transactions，只做欠奖重试，`CloseThroughCompletion`
在该时刻锁定结果（其后的进出丢弃），边界时刻的行动、效果与交易都不发生；步内喷上的涂鸦由 `CloseBoundary` 在其真实时刻归约。
结束原因为 `ELDMatchEndReason::GuardSucceeded`（“守护成功”）或 `HostEnded`。
驱动在 Transactions 后依次调用 `ExecuteCapturePhase`（核对每个存活身体本步运动证据、归约并给出提案）、
`ExecuteClosePhase`（先备好全部结果行，再转 Results 并推进 GateRevision）与无 permit 的
`ExecuteCapturePublish`（提供者 MatchClosed 清理、停身体、发布结果/击杀/公共旗杆状态）。
漏切喷漆或守护完成边界、保护到期未收到 ProtectionEnded、缺运动证据或结果行无法冻结都是明确故障，不默认判胜。
世界里的旗杆表现是本地 `ALDCaptureMarker`：各端在接受的 `PublicMatch` 处于发育/开放/结算时，
按本机注册的 CAPTURE 锚点与 `CAPTURE.RadiusCm` 在地面铺一圈 2cm 平线（复用已烘焙的引擎方块与
`M_Character` 的 `Tint`，无碰撞、不影响寻路/视线/高度），颜色跟随公开喷漆区状态（未开灰、开放黄、独自喷漆绿、
争夺红、主人独守蓝）；旗杆约 210cm 高处另有一块涂鸦板，有涂鸦时按主人的身份色
（`ULDHumanoidVisualComponent::ContenderTint(GraffitiPresentation)`）显示、无主时隐藏，同样无碰撞、不进导航和视线。
换代、回 Lobby 或房间停止即移除；它不复制、不参与判定，只在 Demo 地图生成，缺数据时记 `LD_CAPTURE_MARKER_UNAVAILABLE`。
普通离局在受理时刻向归约器提交 Left，不发奖、不判胜，结果行保留。
`ReturnToLobby` 与开局失败共用 `RetireMatch`：先关闸，再按注册逆序调用 MatchReset，
`RetireAuthorityCore` 拆除身体、`RetireMatchDomains` 中的每局领域 Actor 与时钟/许可状态，
丢弃 AI 与已离局记录后为在线真人换新 Generation/LedgerMatch 并清 Ready 与会话窗口。
本人视图发布（`SyncOwner`）先于身体和资源记录停止：世界拆除（`bIsTearingDown` 早于网络驱动销毁
远端 Pawn）、Host 关房（`Closing`）与 `RetireMatch`（`Resetting`）期间不发布、不判投影故障，
新 Lobby 按新代重新发布；正常对局阶段仍做完整投影校验，真故障照常进入 `Faulted`。
规则 `Lockdown.Rules.DemoOwnerViewTeardown` 固定这三种停发条件与对局中真实不一致仍判故障；
`RunDemoSessionFaultCheck.ps1` 的 HostQuit 段让 Host 在客户端仍连接时正常退出，要求 Host 日志无权威故障。

## 运行日志与试玩记录

运行时的每一行都是 `LD_<TAG> key=value ...`：标签与字段是契约，`Scripts\SummarizePlaytestLog.py` 和各
`RunDemo*Check.ps1` 按它解析；新增字段只追加在已有字段之后，不改名、不复用标签。值含空格、引号或 `=` 时加引号并转义
（`LDLog::Quote`），资源为两位小数的点数（P2），身份为 32 位十六进制，时间 `t=` 为同局权威毫秒。
日志分类在 `Source\Lockdown\Public\Core\LDLog.h`，编辑器 commandlet 用 `LogLDEditor`；
分类只用于分组与 `-LogCmds="LogLDNav Verbose"` 这类开关，不参与解析。

| 分类 | 内容 |
| --- | --- |
| `LogLDMatch` | 入口、房间、开局头与名单、生命与死亡、喷漆守护、结果与对局汇总、标记 |
| `LogLDCombat` | 战斗驱动、Host 命中行、本人战斗输入与回执、客户端本人受伤/状态/所见施法 |
| `LogLDGrowth` | 奖励、升级、三选一、DEV 成长夹具，客户端本人成长 |
| `LogLDNav` / `LogLDAI` / `LogLDWorld` | 订单与移动 / 对手 AI / 营地、Boss、空投、地图注册与视野中继 |
| `LogLDNet` | 10 秒帧耗时与连接汇总、卡顿行 |
| `LogLDUI` / `LogLDContent` / `LogLDTest` | 界面与本地设置 / 内容、美术与音效目录 / DEV 场景探针 |
| `LogLDTrace` | 每秒一行的 Host 心跳与客户端本人状态；`-LogCmds="LogLDTrace off"` 关掉它时连采样也跳过 |

Host（Listen Host 的权威代码）在试玩期可以记录完整权威信息（#134 用户决定“试玩期 Host 日志记完整权威信息”）：

| 标签 | 写入位置 | 字段要点 |
| --- | --- | --- |
| `LD_MATCH_HEADER` / `LD_MATCH_ROSTER` | `LDGameModeTrace.cpp`，开局时 | 产品版本、wire、源码提交（打包版读包旁 `PackageManifest.json`，编辑器读 git HEAD）、内容摘要、网络地址；每名参赛者的槽位、名字、真人/AI、房主、角色、地址 |
| `LD_HEARTBEAT` | `LDGameModeTrace.cpp`，权威时间每秒一次（`LDLog::FPeriodicGate`） | 每名参赛者的生命阶段、脚点、生命/法力/护盾、等级、经验、待选、当前订单与目标点、正在前摇的动作、公开状态 |
| `LD_HIT` | `LDCombatEffects.cpp` `ApplyHit`，每次已提交命中 | 来源/受害者及营地类型、来源技能、命中类别、伤害类型、伤害、护盾吸收、实际掉血、扣血前后与生命上限、护盾前后、增幅（`amp_bp`，加成池与倍率合计）与抗性（已计破防）、暴击/保护/周期（区域脉冲与灼烧）/反伤/附加伤害、控制（`root`/`stun`/`disarm`/`taunt`/`knockback`/`slow`/`silence`）、吸血（来源实际回复的生命，已计生命上限）、是否致死，末尾为加成池 `pool_bp` |
| `LD_DEATH_RECEIPT` / `LD_CAMP_DEATH` | `LDGameModeLife.cpp` | 原字段后追加凶手（CreditOwner）、击杀类别、致命来源、来源身体、助攻者、无奖励原因、等级 |
| `LD_REWARD` / `LD_LEVEL_UP` | `LDGrowthDirector.cpp` 复合奖励安装后 | 每份奖励的领取人、原因（`cause=death`/`map_event`/`dev_fixture`，`role=killer`/`assist`/`recipient`）、经验及其结余、等级变化、获得的三选一次数 |
| `LD_OFFER` / `LD_CHOICE` | `LDDevelopmentDirector.cpp` | 每次出卡（含重抽）的三张卡；确认的卡、等级、替换 |
| `LD_PLAYER_MARK source=host_view` | `ALDGameMode::LogPlaytestMark` | 任一玩家按标记键时，每名参赛者每秒最多一条。标记带发送时的局次，只在该局仍是当前局且处于休息（Rest）、野外（Development）或旗杆开放（CaptureOpen）时记录；上一局迟到的或在大厅、结算时到达的标记写 `LD_PLAYER_MARK_REJECTED`（局次、当前局次、阶段），同样限频 |
| `LD_MATCH_SUMMARY` / `_ROW` | `LDGameModeTrace.cpp`，结果锁定时 | 结束原因、胜者；每人等级、经验、K/D/A、技能及等级、属性卡 |
| `LD_NET role=host` | `LDPlaytestTrace`，每 10 秒 | 每个客户端连接的 RTT、抖动、收发丢包、未收包时长、本窗口服务器发出的移动校正（`ULDCharacterMovementComponent::ServerCorrectionCount`） |

远端客户端只记录该玩家本来就能看到的信息（`FLDPlaytestTrace`，读法与 [视野与 UI](VISION_AND_UI.md#ui-只读模型) 的允许来源一致）：
`LD_SELF_MATCH`/`LD_SELF_ROSTER`（本人身份与公开名单）、`LD_SELF_STATE`（每秒一行本人投影）、`LD_SELF_LIFE`、
`LD_SELF_HP`/`LD_SELF_STATUS`（本人受伤、护盾吸收、治疗与状态，由与 HUD 飘字同一 `LDWorldFeedback::FTracker` 得出）、
`LD_SELF_REWARD`/`LD_SELF_LEVEL`/`LD_SELF_OFFER`/`LD_SELF_BUILD`（本人成长投影）、
`LD_SELF_KILLFEED`/`LD_SELF_DEATH`/`LD_SELF_KILL`/`LD_SELF_RESULT`（公开击杀通知与结果）、`LD_SELF_SEEN_CAST`（当前授权帧内可见单位的施法）、
`LD_SELF_ORDER` 与已有的 `LD_COMBAT_INPUT`（本人发出的订单与战斗输入，按住右键的重复订单最多每 0.25 秒一行）
和 `LD_PLAYER_MARK source=self`。客户端日志不含隐藏单位的位置、他人的经济或伤害公式拆解；本人视角各行都在该局的
`LD_SELF_MATCH` 之后写出，便于按局切分。`SummarizePlaytestLog.py --check-client` 检查每份日志在不作 Host 的时段
（`LD_DEMO_LOBBY` 之前或 `LD_DEMO_FRONTEND` 之后）里没有 Host 专有标签。

每个进程都写 `LD_PERF`（10 秒窗口的 FPS、平均与峰值帧耗时、帧数与卡顿数，复用 HUD 的 `LDHUD::FFrameDiagnostics`）、
客户端的 `LD_NET role=client`（RTT、抖动、丢包、本人移动校正次数与误差）和单帧 ≥ 100 ms 时的 `LD_PERF_HITCH`
（`LDLog::FRateLimit` 每秒最多一行，被省略的条数随下一行报告）。它们与 HUD 的诊断开关无关。
没有逐帧日志：周期行都经 `FPeriodicGate`/`FRateLimit` 限频，事件行只随已提交的权威事实或本人投影的变化写出。

## 扩展与检查

新增规则先找 GameMode 所拥有的现有领域服务，保持一个权威状态和一条提交路径。
修改序列化时同步生产者、消费者、上限检查、代次拒绝和客户端显示，不能只改结构体尺寸。
新的公开信息必须显式决定，不通过日志、提示或失败原因意外公开。

当前入口：`RunDemoCaptureCheck.ps1`（终局与回 Lobby）、`RunDemoSessionFaultCheck.ps1`（中途离开、Host 离开、正常退出与失联、加入失败与拒绝）、`RunDemoCombatCheck.ps1`、`RunDemoVisionCheck.ps1`，
以及相关 `Source\Lockdown\Private\Tests` 中的生命周期、网络与序列化检查。
准备对局、调试授予或无玩家的监听服务不能代替真实客户端完成一局的证据。
Demo 新接缝使用 `Lockdown.Rules.DemoCore`、`Lockdown.Rules.DemoCombatEvaluation`、
`Lockdown.Rules.DemoCapture`（喷漆与守护 `.SprayAndGuard`、主人须在区内守护 `.GuardNeedsOwner`、硬控打断 `.SprayInterrupt`、主人死亡/离局清除涂鸦 `.GraffitiCleared`、
争夺暂停/续计/清零、保护与契约故障、喷漆区几何、公共读模型、提供者门禁、Host 结束/回 Lobby 与换代隔离）和
`RunDemoCoreCheck.ps1`；后者以 DEV 自动化入口在真实三拥有连接上调用同一核心提交路径，
固定选取已编译锚点只作身体/运动机制夹具，不代替普通开局、随机出生、完整战斗或 3+3 验收。
三名拥有者并发验证走动、停止与旧订单后，轮流验证各自的地面终点，避免他人途经目标时按合法的占点规则提前结束订单；
轮转及占点几何由 `Lockdown.Rules.DemoCoreProbe.ArrivalTurn` 覆盖。
`RunDemoCaptureCheck.ps1` 在普通开局门禁打开后经正常大厅开局，验证真实拥有连接上的喷漆、争夺暂停与续计、
喷上涂鸦、主人离区使守护暂停、他人独自改漆使守护保持冻结且改漆失败后守护仍暂停、主人独自回区后续计、冻结期间主人走出使守护清零而涂鸦保留、
主人独自在区内站满守护时长以 `GuardSucceeded` 守护成功的
唯一结果与同世界新局，并由 Host 正常开出第二局：各角色只读本人视图核对首个存活帧的开局等级/XP、
空构筑、无状态与保护、冷却全就绪，Host 再核对五营全新满血、旗杆与涂鸦复位与旧代请求以 StaleMatch 拒绝；
它同样不代替 3+3、AI 或网络异常验收。

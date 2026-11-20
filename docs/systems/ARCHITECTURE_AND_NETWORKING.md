# 架构、会话与网络权威

| 项 | 内容 |
| --- | --- |
| 规则 | [App Spec](../APP_SPEC.md) §2、§6.1、§8；数值只在[内容与数值 §3.4](../demo/CONTENT_AND_BALANCE.md#34-空间与对局节奏参数)；跨域见[共享契约](../demo/SHARED_CONTRACTS.md) SC-01–04 |
| 文件 | `Framework\LDGameInstance`、`LDGameEngine`、`LDGameMode.*`（`LDGameModeSession/Match/Lifecycle/Life/Rest/Capture/Pacing/Showdown/Trace.cpp`）、`LDMatchLifecycleTypes.*`、`LDMatchTimeline.*`、`LDCaptureReducer.*`、`LDCaptureTypes.*`、`LDMatchProviders.cpp`、`LDPlayerSession.cpp`、`LDPlayerRest.cpp`；`World\LDCaptureCircle`、`LDCaptureMarker`、`LDCaptureFence`、`LDSpawnPlacement.cpp`、`LDFieldPacing.*`；`Framework\LDPacingTypes.*`、`LDShowdownRing.*`、`LDPlayerPacing.cpp` |
| 入口函数 | `InitGame` / `PreLogin` / `PostLogin`、`ServerDemoSessionCommand` → `ProcessSessionCommand`、`StartDemoMatch`、`RetireMatch`；`BeginAuthorityStep` / `ValidateWritePermit` / `GetAuthorityNowMs`；`EndContenderLife`、`EndProtectionForAggressiveIntent`；`PublishCaptureMotion/Eligibility/Interrupt`、`ExecuteCapturePhase` / `ExecuteClosePhase` / `ExecuteCapturePublish`、`TryCloseMatch`、`FailAuthorityBoundary`；`AdvanceTimeline` / `LockTimelineBuild` |
| 不变量 | 同一 Listen 世界内单一会话/对局流程；GameMode 是比赛身份、时钟、批次、生命边界、写闸、喷漆与守护归约和唯一结果的唯一提供者；一条单调权威时钟，野外进度 T 只在野外推进；写许可只由核心在当前调用栈构造；唯一结果只提交一次；必需工作失败进入 `Faulted`，不默认判胜；没有 Host 迁移或中途加入 |
| 检查 | `Lockdown.Rules.DemoCapture.*`、`DemoCore.*`、`DemoRest.*`、`DemoShowdown.*`、`DemoSessionReceipts`、`ViewPrivacySchema`、`DemoOwnerViewTeardown`；场景 `RunDemoEntryCheck.ps1`、`RunDemoCaptureCheck.ps1`、`RunDemoSessionFaultCheck.ps1`、`RunDemoCoreCheck.ps1`（见[扩展与检查](#扩展与检查)） |

工程入口与完整命令见[开发指南](../DEVELOPMENT.md)。

## 模块与状态归属

| 边界 | 当前入口 | 职责 |
| --- | --- | --- |
| 运行模块 | `Source\Lockdown\Lockdown.Build.cs` | CharacterMovement、Enhanced Input、UMG、导航、AI、网络与规则 |
| 编辑器模块 | `Source\LockdownEditor` | 导入、Bootstrap、生成资产与保存包；不进入运行模块依赖 |
| 规则测试模块 | `Source\LockdownTests` | `Lockdown.Rules` 自动化测试与夹具；只由编辑器目标构建和加载，Game 目标与打包版不含规则测试 |
| DEV 场景探针 | `Probes\` | 命令行启动的 DEV 场景、性能与美术采集探针；留在运行模块，打包 Development 版可用 |
| 进程内会话 | `Framework\LDGameInstance.h/.cpp` | 网络/旅行失败回到前端，保存本地连接反馈 |
| 正常游戏旅行 | `Framework\LDGameEngine.h/.cpp` | 在创建世界之前拒绝旧本地 URL/模式或缺输入；GameInstance 单独执行带错误反馈的前端恢复 |
| 世界规则 | `Framework\LDGameMode.h/.cpp`、`LDGameModeSession.cpp`、`LDGameModeLifecycle.cpp` | 唯一 Demo 准入、Match 映射、Lobby、生命绑定、窄时序/写许可和会话回执；旧玩法提供者不自动创建 |
| 权威切片 | `Framework\LDMatchAuthority.h`、`LDLifeAuthority.h` | `ALDGameMode` 实现的窄接口：比赛上下文、权威时钟、写许可与故障报告；生命/身体引用、保护与视野导演。只需这些的域经自己的导演（如 `ALDCombatDirector::Authority()`）或 `LDAuthority::Find` 读取，不包含整个 GameMode |
| 公共状态 | `ALDGameState`，定义在 `Framework\LDGameState.h` | 明确公开的 Lobby、目标、时间及终局状态 |
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
读取的是原始字节，不规范化换行，也不回退旧目录。
入口检查（Host/Join、地图旅行、`InitGame`、开局前的 `CheckDemoStartInputs`）每次都重算三个载荷文件的 SHA-256 并重新核对文本表；
游戏进程里已为同一目录根验证过的内容快照直接沿用，不再重新投影与校验整份目录（`LDDemoEntry::ReusesApprovedContent`）。
编辑器进程仍每次完整加载，因为编辑器工具可能在内存中改写目录资产。`LDDemoEntry::ValidateSelection` 只接受
完整的前端/Demo 包路径，剥离 PIE 前缀，但不接受同短名的其他包、旧 Profile 或 Humans 覆盖项。
旧保存模式和地图的物理移除须与资产引用审计同批完成，不保留可启动的旧预览分支。

`ALDGameMode::InitGame` 为有效 Demo 房间一次创建 `FLDMatchContext`；
网络 Generation 通过 GameInstance 的有界单调分配器取得，LedgerMatch 单独生成并显式绑定。
`PreLogin` 检查阶段、人数、wire/product/content 兼容性，最终 `PostLogin` 再检查阶段与真人和 AI 共用的六人容量；
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
发送者由拥有 Controller 决定；只有 Host 可以改变 AI 数量、开始、结束本局或带房间回 Lobby。玩家菜单不提供结束本局入口；
服务器仍保留 `EndMatch` 命令供开发与规则测试使用，只在 Rest/Development/CaptureOpen 于两步之间受理，以最后已关闭边界无胜者锁定结果（其他阶段以 `WrongPhase` 拒绝）；
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
  设 `net.MaxConnectionsToTickPerServerFrame=8`，每个复制帧都 tick 全部客户端连接（最多五名远端真人）。
- 其他身体的可见表现主要通过观测中继，而不是把所有权威 Actor 广播出去后在 UI 上隐藏。
- GameState 只放明确公开的事实，不应成为全部玩家位置、背包和构筑的总表。
- `ALDGameState::DemoLobby` 是名单、Host/Ready/AI 名额及公开阻塞原因的只读投影，
  不包含 LedgerMatch、身体坐标或当前私有构筑。Lobby 另带上局公开结果身份及保留 AI 席位的旧参赛者身份；
  AI 名额删除后重加不继承旧映射。每个 AI 名额在开局时绑定一个服务器 `ALDOpponentController`。
- `OnRep_DemoLobby` 通过同一有界读模型检查再发布给 UI：成员身份不得重复或超容量，
  Host/AI/Ready 形状必须一致；旧 Generation、重复/旧发布版本及结构版本回退不能覆盖已接受状态。
- `ALDGameState::PublicMatch` 是 UI/AI 的唯一阶段与旗杆状态来源：阶段、开放时刻、喷漆区状态
  （未开放/无人/独自喷漆/争夺/主人独守，即 `NotOpen/Empty/Solo/Contested/Guarded`）、唯一喷漆进度持有者
  （独自喷漆者，或争夺中被暂停的喷漆者及其 `PausedProgressMs`）、CaptureEpoch 及喷漆起点/喷上时刻，
  涂鸦主人 `GraffitiOwner` 与其身份色槽位 `GraffitiPresentation`、运行中守护的获胜时刻 `GuardDueAtMs`
  或被冻结的守护进度 `GuardKeptMs`；不列喷漆区内人数、
  保护者或隐藏坐标。实时显示时间取同代 `DemoClock.NowMs`。`MatchResult` 仅在 Results 发布一次，
  按开局锁定名单列出全部参赛者（含离局者）的名称、击杀、死亡与最终等级，以及胜者、原因、用时和只读规则板摘要；
  摘要只在写闸关闭后从成长权威投影已学技能、已装的牌与安装位置，附目录摘要，不包含库存、属性点或冷却。
  投影失败记日志并标记不可用，不影响结果锁定；结果保留供返回同房间的 Lobby 按身份显示，不能作为新局状态。
  `KillFeed` 是最近 8 条参赛者死亡的公共环形缓冲（击杀者仅限参赛者，营地按档位），不带技能、构筑或位置；
  它的 `Scores` 按名单顺序列出每名参赛者至今的击杀/死亡/助攻（按住 Tab 的排行榜），只随死亡变化，所以与通知一起按最后序号拒旧。
  联机协议版本是 `LDDemoEntry::WireProtocol`（当前 35），双方须使用同一构建；复制结构、RPC、观察编码或不兼容的房间契约变动时加一。
  三者都经各自 OnRep 的形状/代次/版本检查，旧代次或重复发布不能覆盖已接受值。
- 世界观察只使用 Demo 的单一稀疏协议。
- 完整局末构筑蓝图不进 GameState：结果发布后由 Host 只经各真人自己的 `ULDBuildBlueprintComponent`
  （`ALDPlayerController` 默认子对象）发出 Offer，拥有者按顺序拉取分块并在本机 `Saved/Blueprints` 写文件；
  见[视野与 UI](VISION_AND_UI.md#构筑蓝图文件与查看)。

详细可见性机制见 [视野与 UI](VISION_AND_UI.md)。

## 阶段与会话命令

`ELDMatchPhase`（GameMode）：`Lobby / Starting / Rest / Development / CaptureOpen / Results / Resetting / Closing / Faulted`。数值 2 空缺不复用，`Rest` 为 9 并覆盖每次休息的全部步骤；
最终野外开放前为 `Development`，开放后为 `CaptureOpen`。`Faulted` 是停止玩法提交的工程故障状态。“连接中/加入失败”属于 GameInstance/UI，不复制为比赛状态。

| 阶段 / 输入 | 权威行为 | 失败与并发 |
| --- | --- | --- |
| 前端创建 / IP 加入 | `OpenLevel(...listen)` 进入 `Lobby`；加入时握手与准入都通过才显示已加入 | 地址不可达、超时、房间满、已开始、版本/内容不兼容分别反馈；错误只作用于所属 WorldContext/连接尝试 |
| `Lobby` / Ready、改 AI 数、真人进出 | Ready 只改本人；AI 数与成员进出推进结构名单版本并清全部真人 Ready | 过期名单返回当前名单与拒绝；最终入房提交点重查容量与阶段 |
| `Lobby` / Start | 重查 Host、名单版本与全部真人 Ready，关闭准入进入 `Starting`，冻结参赛快照 | 重复 Start 重放原结果；离开先提交则拒绝旧 Start |
| `Starting` | 验证内容、地图与各域依赖，初次身体全部站到休息锚点后进入 `Rest`（R0 配装；`PublicMatch.StartedAtMs` 为此刻） | 失败清理未发布对象、退休该次 Match，发布新一代 Lobby 与明确失败；提交前断线使启动失败 |
| `Rest` / 返场 | 见[休息、野外进度与阶段边界](#休息野外进度与阶段边界) | 全员共同提交，超时 `LD_REST_RETURN_UNREADY` 权威故障 |
| `Development` / 野外段到期 | T 到达 `MATCH.FieldSegmentSec` 整数倍进入下一次休息 | 进入休息不是死亡 |
| `Development` / 最终野外开放 | `FLDCaptureReducer::ScheduleOpen` 定开放时刻，同边界 Capture 进入 `CaptureOpen` | 前三段的区内停留不计 |
| `CaptureOpen` / 守护完成 | 闭合边界、重判主人与连续守护，先锁结果与写闸，进入 `Results` | 旧局/旧生命/旧 CaptureEpoch 的提案拒绝 |
| 测试用 Host 结束 | 以最后已关闭边界无胜者 `HostEnded` 锁定 | 与自然胜利按批次顺序裁定 |
| `Results` / 回 Lobby | 保持旧闸关闭进入 `Resetting`，全域失效、拆除、换 Match 后发布 `Lobby` | 保留连接，清 Ready，不自动重开 |
| 普通成员离开 | 立即撤资格/命令、移除身体与碰撞、取消未结效果 | 不是死亡、不发离开奖励、不接管 AI；已锁事实的结算记录保留 |
| Host 离开或丢失 | 关闭会话，客户端回前端显示原因 | 不迁移 Host |
| 必需权威工作失败 | 停止新玩法写入，进入 `Faulted` | 不用漏发奖励或默认数据继续判胜 |

**身份与准入**：真人身份从拥有的 PlayerController 解析，服务端创建 `FLDContenderRecord.Session`；显示名、数组下标与 Pawn 都不是身份。AI 由服务器分配同类 `ContenderId`。
未通过准入的连接不获得 Pawn、成长记录或观测权限；比赛中不补位，只在回到 Lobby 后接受新加入。兼容性分三层：Unreal 网络版本、项目 wire 协议、产品版本与内容摘要。
同世界回 Lobby 不复原旧世界快照：`Resetting` 等每个领域确认旧对象、回调、回执执行权与观察代理失效后才发布新 Lobby；只保留连接、Host 身份、名称与本地设置。

**会话命令**：请求头 `FLDSessionCommandKey { uint32 Generation; uint64 RequestSequence; }`，发送者来自拥有连接。

| 命令 | 额外字段 | 接受条件 |
| --- | --- | --- |
| SetReady | `bool Ready; uint64 ExpectedRosterRevision` | Lobby、已准入、结构版本相符；设置布尔值而非 Toggle |
| SetAICount | `uint8 Count; ExpectedRosterRevision` | Lobby、Host、0..3 |
| StartMatch | `ExpectedRosterRevision` | Host、全部真人 Ready、容量合法；先 Pending，初始化整体提交才 Committed |
| EndMatch | — | Host、Rest/Development/CaptureOpen 两步之间；有待重试奖励时 `RequestInFlight` |
| ReturnToLobby | `FGuid ExpectedResultReceipt` | Host、Results、结果身份相符 |
| 离开 / `Logout` | — | 断开连接；两条路径消费同一次成员移除 |

`FLDSessionReceipt` 含请求键、`Pending / Committed / Rejected`、`ELDSessionReject`、当前阶段与结构名单版本。单连接只允许一个在途请求；`FLDSessionRequestWindow` 保存最后终态与 `ProcessedThrough`：
同键同条款重放，改条款 `RequestConflict`，过窗 `ReceiptExpired`。拒绝枚举：`NotOwner / NotHost / WrongPhase / RoomFull / MatchAlreadyStarted / NotAllReady / StaleRoster / StaleMatch / StaleLife /
RequestConflict / RequestInFlight / ReceiptExpired / ProtocolMismatch / ProductMismatch / ContentMismatch / MissingDemoDefinition / SpawnBlocked / MatchClosed / AuthorityFault`。
地址格式、端口、不可达与旅行失败归 GameInstance 本地错误。本地连接使用独立 `ConnectionAttemptSerial`：取消后迟到的成功立即离开清理，迟到失败不关闭下一次成功连接。

**公共读模型**：`FLDPublicMatchView` 时间字段为 `ServerNowMs / OpenAtMs / SoloSinceMs / DueAtMs / PausedProgressMs / GuardDueAtMs / GuardKeptMs`，另带 `PublicRevision`、
`CircleState / Occupier / GraffitiOwner / GraffitiPresentation / CaptureEpoch / Result`、`Timeline`、`DailyModifiers`、`Objective`。`SoloSinceMs / DueAtMs` 只在 `Solo` 时有值；`GuardDueAtMs` 只在 `Guarded`；
`GuardKeptMs` 在 `Empty / Solo / Contested` 表示保留进度；无主时用明确空值。字段组合由 `LDCaptureTypes.cpp` 校验。时间锚带 `double ServerSampleWorldSeconds`，
客户端相对该锚估算显示，本地估计到零不触发服务器动作。`FLDResultView { Generation; ResultId; ResultSerial; Reason; TOptional<FGuid> Winner; StartedAtMs; EndedAtMs; DurationMs; Rows }`
不发送 LedgerMatch；`FLDResultRow` 为 `ContenderId / Name / Kills / Deaths / FinalLevel / Assists`，按开局锁定名单顺序冻结（离局者有行，离局不加 Deaths）。
公开喷漆者或涂鸦主人不授予其坐标或视野。Owner 的时间字段为 `RespawnAtMs / ProtectedUntilMs`。

## 请求、代次与回执

所有权由拥有连接决定，不能相信客户端传入的另一名玩家标识。
命令携带其所需的 match、life、sequence/order、build/object revision，
目标使用当前获准的观察句柄或公开地点 ID，不使用隐藏 Actor 指针。

先验证代次、身份、目标和资源，再提交一次结果。重复的已接受请求返回原回执，
不能重复发钱、创建物品或施放效果。旧 life 的投射物、输入、菜单报价和移动确认不能作用于新身体。
UI 的 Pending 不代表已经提交，客户端不能自行生成成功回执。

`ULDNavigationComponent` 的取消 epoch 与成长的 request 回执
是不同领域的版本，不要用一个全局“最新版本号”替代全部语义。

## 核心身份与生命

`LDMatchLifecycleTypes.h` 定义 `FLDMatchContext{uint32 Generation; FGuid LedgerMatch}`、`ELDVictimKind{Contender, CampMember}`、`ELDContenderKind{Human, OpponentAI}`、
`ELDLifePhase{Unspawned, Alive, Dead, Left}`、`FLDVictimKey{Kind, EntityId, uint32 Life}`、`FLDCombatBodyRef{Victim, BodyIdentity}`、`FLDDeathKey{LedgerMatch, Victim}`。

| 身份 | 唯一分配者 | 约束 |
| --- | --- | --- |
| `Generation` | GameInstance 的有界单调分配器，经 GameMode 绑定 | 初入房间与每次新局换代；零无效；到上界明确拒绝，不环绕 |
| `LedgerMatch` | GameMode 每局一次创建的非零 `FGuid` | 与 Generation 显式一一映射；不接受客户端传入 |
| `ContenderId` | `FLDContenderRecord.Session` | 生死与换 Pawn 不变；不是连接、UI 槽位或观察句柄 |
| `Victim.EntityId` / `Life` | 参赛者为 ContenderId；营地成员由 World 映射 CampId/MemberId；Life 单调 | 死亡不多增一次，成功复活/营地新生命才增；回位不是新生命 |
| `BodyIdentity` | Combat 在新生命注册时生成 | 同一生命至多一个绑定；重绑同 Pawn 不获第二次死亡资格 |
| Order / Epoch / revisions | 各域自有 | 不合成全局“最新版本” |

`GetMatchContext()` 是唯一映射入口；旧 Generation 即使带当前 ContenderId 或新请求号也不执行。`AliveProtected` 是 Alive 与保护期限的派生资格，不另存可写标志。

**生命提交链**：Combat 首次 0 HP 锁存 → 同一 `FLDCommittedDeath`/DeathKey → `EndContenderLife(Fact, Permit) -> FLDLifeReceipt`（资格只消费一次，切 `DeadWaiting`，取消旧身体动作，设复活截止）
→ 成长复合奖励（含零奖励）→ 抽点与同点预留 → 核心/Combat 提交新 Life、BodyIdentity、满 HP、起始劲与保护截止 → World Finalize + Owner/Vision 投影 → `AliveProtected` → 到期或主动解除 → `Alive`。
`DeathSerial` 只是派生显示计数。离局后保留 settlement-only 参赛/奖励接收记录直到既成事实处理完，不恢复在局资格。

**出生与复活**：首次进入野外、野外复活与返场找不到位置时，从同一组全部出生锚点独立等概率、有放回抽一次（不按危险、视野或敌人距离挑点）；重复请求与同点暂占复用已选 SpawnId；
世界局部预留，失败为 `SpawnBlocked` 保持 Dead，后续边界以同一锚点重试。只有参数、当前 Match、预期 Life、合法落地、身体与属性准备都成功才提交；失败不推进 Life、不消耗第二次抽样。
Finalize 不再做可能失败的分配或换点；提交后不变式破坏即 AuthorityFault。Respawned 资格事件原子携带 Alive 与保护期限，不先发布无保护 Alive。初次出生走同一落地机制但不附加保护。
对手 AI 同样抽点；野怪使用营地出生机制。

**保护**：免伤免控、可移动、不计入喷漆区人数是生命域资格，Combat 读同一状态。保护是半开区间：到期时刻本身已不受保护，同刻来袭伤害可命中、同刻区内到期可形成争夺；
新保护从成功生命提交时开始。`EndProtectionForAggressiveIntent` 的语义见[战斗、效果与成长](COMBAT_AND_GROWTH.md#从意图到结果)；`FLDProtectionReceipt` 含 Generation、Body、原 Request、Outcome、
ProtectionRemoved、`uint64 LifeRevision`、可选 EventStamp 与 Reason；Life 自己从当前 Actions 边界生成 Stamp，只有保护真正变化才发布资格事件。

**AI 绑定**：`RegisterAIContender(AController& ServerController, uint8 RosterSlot, const FLDMatchContext&) -> FLDAIRegistrationResult` 只在 Starting 接受冻结的 AI 名额，同槽重复调用重放同一绑定，
失败返回 WrongPhase/InvalidRosterSlot/AlreadyBound/StaleMatch。签发的 `FLDAIContenderBinding{Match, ContenderId}` 构造受限；`ReadAISelf(Binding)` 校验注册 Controller 与当前 Match，只返回该 AI 的 Owner 投影；
公共状态用 `ReadPublicMatch()`。真人/AI 按 `ELDContenderKind` 计数。

**提交后通知**：`FLDLifeNotice{Match; BodyRef; ELDLifeChange Change; uint64 LifeRevision; FLDAuthorityEventStamp Stamp}`，Death/Respawn/ProtectionEnded/Left 各推进 LifeRevision；
`NotifyLifeCommitted` 让 Navigation/目标/AI/表现清理旧身体工作。`NotifyMatchClosed`（带不可变 ResultReceipt）在写闸关闭后通知取消；`NotifyMatchReset` 带 Old/NewContext 与新 Lobby 修订。
AI 控制器在 MatchReset 里只停止活动，身体由 `RetireAuthorityCore` 退役，控制器在下一帧确认换代后自毁，并覆盖 `ClientTravelInternal` 不执行 travel。AI 的策略结果只提交给下一边界的领域意图队列。

## 生命周期与失败

Demo Lobby 不通过 `RestartPlayer` 自动生成旧身体。普通离开使当前成员失效；
Host 离开关闭房间并通知其余连接，不把 Host 移交给第一个剩余玩家。
死亡、复活与保护由 `Framework\LDGameModeLife.cpp` 提交：锁定的 Combat 致死事实结束当前生命
（撤销 Combat 身体、取消本生命订单与未释放动作、销毁 Pawn，等级/构筑/来源冷却保留），
野外复活等待为 `LIFE.RespawnBaseSec` + `LIFE.RespawnPerLevelSec` × 死亡时等级（考试周另加 `DAILY.ExamRespawnBonusSec`）；死亡时 `OpenSpawnChoice` 从全部出生锚点不放回抽 `LIFE.SpawnChoiceCount` 个候选，到 `RespawnAtMs` 时在选中（未选则第一个）的锚点局部预留，再原子提交新 Life、满 HP、起始劲与保护截止。已确定的等待总时长经 `CombatView.RespawnDurationMs` 仅发布给本人供界面使用，复活、离局或休息终止等待时清除。
`SpawnBlocked` 时保持 Dead，后续边界以同一选中锚点重试，不重抽“更安全”的点。
成长、营地与终局按各自提供者接入。
`IsGameplayWriteOpen` 只是当前 Match/阶段的快速检查，不是正式提交 permit；它在 Rest/Development/CaptureOpen 为真。

外层流程：
`StartDemoMatch` 把每名参赛者的第一条生命放在公共休息室按名单槽位固定的 `rest_spawn_<槽位>` 后进入 `Rest`（开局休息 R0），
`InitializeCaptureMatch` 把 `MatchStartedAtMs`（`PublicMatch.StartedAtMs` 与结算的 `StartedAtMs`）设为此刻，以 `MAX_int64` 开始喷漆归约器，
并由 `FLDMatchTimeline` 推进 R0 → 野外 1 → R1 → 野外 2 → R2 → 野外 3 → R3 → 最终野外。时钟只有一条：野外进度 T 只在野外段推进、休息中冻结，
外卖、上热搜、画饼老板、营地刷新、营地成长阶段与营地标记都按 T 以 0 为起点计时（`GetFieldNowMs`、`FieldDueAuthorityMs`）；
外层参数 §5 的地图事件（外卖固定落地表与消失、老板按批准 T 出现并在第三段野外边界退场、营地标记在各段
T+11 s 公开并在 `EnterRestWorld` 过期）由 `ALDGameMode::AdvanceAirdrop`/`AdvanceFieldMarkers`（RespawnsAndWorld）与
`ALDCampDirector::AdvanceCamps`/`FreezeForRest` 驱动；标记由营地死亡（`ConsumeMemberDeath` 的 CampCleared）领取，奖励经
`ALDGameMode::PayFieldReward` 进入 `ALDGrowthDirector::CommitFieldReward`，可重试的拒绝在之后每个 Transactions 由 `RetryFieldRewards` 重付；
最终野外开始的边界由 `FLDCaptureReducer::ScheduleOpen` 定为开放时刻，同一边界的 Capture 阶段从 Development 切到 CaptureOpen。
`NextTimelineDueMs` 让每个休息步骤与野外段终点成为精确步边界；driver 在 RespawnsAndWorld 开头调用 `AdvanceTimeline`，
在 Transactions 末尾调用 `LockTimelineBuild`（截止时刻提交的构筑命令先结算，再进入返回倒计时；不锁构筑）。休息中 driver 跳过营地运动、营地意图与世界推进，
Combat 挂起营地成员与 Boss；阶段边界的生命退休/放置、效果取消与资源补满见[休息、野外进度与阶段边界](#休息野外进度与阶段边界)。
写闸在休息中保持打开；构筑阶段由 `ReadBuildStage` 给出（Build 与 Payout 步骤为 Rest、其余休息步骤为 RestLocked、野外为 Live）：自己的构筑命令在三者都受理，RestLocked 只关闭 NPC 服务。
返回倒计时（Return 步骤）中 `IsInputLocked` 为真：导航订单（含 Shift 队列步骤）回 `ELDOrderFailure::NotStarted`，Combat 的普攻、Q/W/E 与冲刺 D 回 `ELDCombatReject::NotStarted`，
这些拒绝记入各自的回执窗口，进入野外后重放仍得到同一拒绝。对手 AI 的身体决策只在 Development/CaptureOpen 运行；成长命令与真人同一闸门，但 AI 策略在休息中只在配装步骤做构筑决策。
P6 野外节奏（`LDGameModePacing.cpp`）：`InitializePacing` 在开局抽今日热点（`LDPacing::DrawDailyModifiers`，本局私有种子）、
准备野外目标（`FLDObjectiveState`）、外卖柜（`FLDLockerState`）与二手贩子计划（`FLDPeddlerPlan`），全部按 T 计时；
`AdvanceFieldPacing`（RespawnsAndWorld）推进目标的预告/开放/关闭、擂台结算、贩子出现与离开、下雨与台风；
`ClaimFieldPacing`（Transactions）结算小迷你旗胜者、擂台奖励、外卖柜的看见（`Vision->TraceSight`）与开启、售货机购买。
奖励都经 `PayFieldReward` 与 `GrantVouchers` 进入成长复合事务。死亡选点经 `ALDPlayerController::ServerChooseSpawn` 交 `SubmitSpawnChoice`，
回 `ClientChooseSpawn`；售货机经 `ServerVending` 交 `SubmitVending` 入队，回 `ClientVending`（`Queued` 只是受理），在下一 Transactions 边界付款并生效，再回 `Delivered` 或具体拒绝（`NotAlive`/`NotAtMachine`/`NotEnoughVouchers`，进入休息或终局时 `Closed`），不扣饭票的拒绝不会显示为已购买。
两者都绑定 match/life，旧生命与旧局的请求明确拒绝；AI 死亡时 `ChooseSpawnForAI` 立即选离公开目标更近的候选。
本人投影 `FLDOwnerState.Pacing`（`FLDOwnerPacingView`）只发给本人：候选出生点与选择、身边售货机、饭票、饮料剩余次数与看见过的外卖柜。
`LDMatchLifecycleTypes` 的 VictimKey/BodyRef/DeathKey 区分稳定主体、一次生命和短寿命身体。
GameMode 的 `Lives` 保存唯一当前生命阶段/绑定及退休水位；当前 HP/劲/盾仍只在
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
不沿用旧 B92 席位接管或控制器交接；策略、请求与回执见[对手 AI](OPPONENT_AI.md)。
意外断线的非 Host 真人由 `ParkDisconnectedMember` 转给无 UI、无策略输入的同类被动控制器，保留同一参赛者、身体、构筑、资源与复活队列；服务器时钟不暂停，不补位或新增生命。
被动控制器仍占核心参赛名额，但 `GetNumPlayers` 不把它算作第二条真人连接；原生 `GameSession` 保留六连接上限，恢复连接仍须通过私有认证。
恢复连接先经 `LDReconnect` 标记的私有 challenge/proof 校验，不走新成员准入，不在认证前发布本人观察或构筑。
票据只保存在 `GameInstance` 内存，不写入 URL、日志或文件；`ResumeRoomMember` 迁移原导航提供者和账本，恢复 Build/DEV/Direct/Session 请求水位及移动 order/epoch，再重建本人投影并轮换票据。
Build 的待定请求只查询原回执，DEV 的待定请求等待原结算；不重发旧选择或旧生命动作。主动离开清票据，Host 离开结束房间，不支持 Host 迁移或进程退出后的恢复。

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
时暂停、Solo/Contested（区内有主人以外的有效参赛者）时冻结于 `GuardKept`，回到 Guarded 时从保留值续计；
主人离区、死亡或离局只暂停，保留涂鸦与当前守护；对手成功喷成才清原主人的当前守护，`BankedGuard` 历史贡献不清。只有守护满时长给出结束对局的
`FLDCaptureProposal{Epoch, Winner, GuardSinceMs, DueAtMs}`。`NextDueMs` 给驱动的停靠边界：未开放时为开放时刻、
独自喷漆时为喷上时刻、守护运行时为守护获胜时刻。争夺暂停与守护暂停、冻结不给驱动额外停靠边界：步内能让保留的守护恢复的
只有 Motion 的进出区事实（离局在步前入队，死亡、控制、保护到期、复活都落在步的边界时刻）。驱动在 Motion 之后调用
`CaptureCompletesInsideStep`，由 `FLDCaptureReducer::CompletionInsideMs` 在归约器副本上预演已入队事实；
若恢复后的守护获胜时刻早于本步边界，本步跳过 Session～Transactions，只做欠奖重试，`CloseThroughCompletion`
在该时刻锁定结果（其后的进出丢弃），边界时刻的行动、效果与交易都不发生；步内喷上的涂鸦由 `CloseBoundary` 在其真实时刻归约。
结束原因为 `ELDMatchEndReason::GuardSucceeded`（“守护成功”）、`SprayCount`、`ClearOut` 或 `HostEnded`。

终局改装（App Spec §2.2）也在这条归约链上判定，没有第二套结算。`BeginShowdown` 在锁定构筑后经
`ALDDevelopmentDirector::ReadFinalMod` 读取每人所选，写入公开的 `FLDPublicMatchView::FinalMods`，并以
`FLDCaptureReducer::SetFinalRules`（开放前一次）把持卡表与 `final_mods` 参数交给主旗杆归约器。归约器只多三类状态：
打一枪换个地方的褪色时刻 `FadeAt`（褪色是 `NextDueMs` 的停靠点；褪色中的涂鸦没有守护）与须先离区的 `MustLeave`；
`Died` 事实携带 `Killer`，在死亡归约后、`Evaluate` 前由 `ApplyKill` 处理人头换秒（历史守护 +，当前主人则前移
`GuardDue` 或增加 `GuardKept`，满时长即获胜）与篡位（先结算原主人的守护，再改主并继承运行或保留的进度）；
每个同刻事实组结束后 `CheckClearOut` 判定清场。替代胜利与击杀补满的守护都记为候选，`FLDCaptureProposal::Kind`
写明 Guard/Sprays/ClearOut；一次归约只继续处理与最早候选同刻的事实，`FLDCaptureReducer::Settle` 按“最早时刻，再按
守护 > 喷漆次数 > 清场”裁决，同一条件不同人并列时 `Winner` 为空（`NoChampion`）。
私人旗杆各有一个 `BeginPrivate` 建立的独立归约器（从主归约器的已提交名册起步，`OnlySprayer` 只许持有者喷），圆心取
持有者入场口锚点 `final_spawn_<roster slot>`，任何资格、打断与离局事实经 `EnqueueCaptureFact` 同时进入主旗杆与每根私人旗杆，
运动穿越逐圈提取。`ExecuteCapturePhase` 汇总全部归约器的候选同样用 `Settle` 裁决；`CaptureCompletesInsideStep`
取最早的步内完成者。无结果时 `UpdateCaptureGeometry` 经纯函数 `LDCaptureCircle::FinalZone` 更新主喷漆区：
扛旗跑路把圆心移到存活主人的脚点（铁笼站立时不动），双黄线换半径；圆心离开锚点后 `FLDCaptureCircle::bAnyGround`
接受任意原生落地面。几何变化递增 `CaptureCircleSerial`，下一步 `PublishCaptureMotion` 先按批次起点补一条存在事实，
再从新圆提取穿越。铁笼由同一函数在持有者新涂鸦出现时写入公开的 `CageUntilMs/CageCentreCm/CageRadiusCm`；
`ALDGameState::SyncCaptureFence` 在每台机器（含权威端）按公开视图生成 `ALDCaptureFence`，只阻挡 Pawn 通道。
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
公开地图事件同样由 `ALDGameState` 在本地画出（Host 发布后直接调用 `OnRep_PublicMatch`，加入者在复制时调用，两端同一路径）：
当前外卖的 `ALDAirdropMarker` 使用 `AIRDROP.PickupRadiusCm` 橙色圈，落地后显示美术目录的箱子；效果直接在权威领取流程中发放，不生成独立拾取物。
普通离局在受理时刻向归约器提交 Left，不发奖、不判胜，结果行保留。
`ReturnToLobby` 与开局失败共用 `RetireMatch`：先关闸，再按注册逆序调用 MatchReset，
`RetireAuthorityCore` 拆除身体、`RetireMatchDomains` 中的每局领域 Actor 与时钟/许可状态，
丢弃 AI 与已离局记录后为在线真人换新 Generation/LedgerMatch 并清 Ready 与会话窗口。
本人视图发布（`SyncOwner`）先于身体和资源记录停止：世界拆除（`bIsTearingDown` 早于网络驱动销毁
远端 Pawn）、Host 关房（`Closing`）与 `RetireMatch`（`Resetting`）期间不发布、不判投影故障，
新 Lobby 按新代重新发布；正常对局阶段仍做完整投影校验，真故障照常进入 `Faulted`。
规则 `Lockdown.Rules.DemoOwnerViewTeardown` 固定这三种停发条件与对局中真实不一致仍判故障；
`RunDemoSessionFaultCheck.ps1` 的 HostQuit 段让 Host 在客户端仍连接时正常退出，要求 Host 日志无权威故障。

## 休息、野外进度与阶段边界

数值只在[内容与数值 §3.4](../demo/CONTENT_AND_BALANCE.md#34-空间与对局节奏参数)。

- **时钟**：`FLDMatchTimeline`（`LDMatchTimeline.h/.cpp`）是 GameMode 拥有的纯归约器，从不读世界时钟，每次调用传入权威时刻，转换都在精确边界上（`NextTimelineDueMs` 交给 driver 作停靠边界）。
  野外进度 T 是派生累计值，只在三段定时野外（各 `MATCH.FieldSegmentSec`）推进，休息中冻结。公开读模型 `FLDPublicMatchView.Timeline`（`FLDTimelineView`：阶段、休息序号、野外段、步骤与起止、
  T 基准、本段 T 终点、准备名单），客户端与 AI 用 `LDMatchTimeline::FieldNowFromView` 推导 T。`MATCH.CountdownSec`、`MATCH.DevelopmentSec` 不是合法字段。
- **公共休息室**：`RestAnchorFor` 返回 `rest_spawn_<名单槽位>`；所有休息身体属于 `ALDGameMode::RestIsolationGroup()`。`IsHiddenFrom` 按群组隔离野外与休息室；Vision 向休息室连接发布全部活体训练目标的白名单状态；
  `IsIsolated` 在该组禁止参赛者互相伤害/控制，但允许攻击共享训练目标。
- **真人准备**：`ServerRestReady(Generation, RestIndex, StartedAtMs, true)` 绑定当前配装窗口，只可准备不可撤销；OwnerState 发布 `bRestReady`/`bCanRestReady`，`ReadyContenders` 只含真人。
  全部仍在局真人准备后 `FinishBuildEarly` 把配装截止改为当前时刻，同一边界锁定并进入返回倒计时。新休息/新局清空准备。
- **步骤**：Build（R0 `REST.OpeningBuildSec`；R1–R3 `REST.BuildSec`；R1、R2 的最后 `REST.SignupSec` 兼作 PK 站圈报名）→ R1/R2 锁定报名者不少于 2 人时
  Draw / Betting / PKReady / PK（至多 `REST.PKMaxSec`）/ Settlement / Payout → Return `REST.ReturnSec`；R0、R3 与报名不足时直接进入 Return。锁定在 Transactions 末尾（`LockTimelineBuild`，截止时刻提交的构筑命令有效），
  其余转换在 RespawnsAndWorld 开头（`AdvanceTimeline`）。`ReadBuildStage`：Build 与 Payout 为 Rest，其余休息步骤为 RestLocked，野外为 Live；自己的构筑命令在三者都受理，RestLocked 只关闭 NPC 服务与商店（`BuildLocked`）。
  Return 步骤 `IsInputLocked` 为真：订单（含 Shift 步骤）回 `ELDOrderFailure::NotStarted`，普攻、Q/W/E 与 D 回 `ELDCombatReject::NotStarted`，进入野外后重放仍得同一拒绝。AI 身体决策只在 Development/CaptureOpen 运行。
- **PK**：可选提供者 `ELDMatchProvider::PK`（`ALDPKDirector`），经 `FLDMatchProviderHooks::RestStep` 收到每次步骤进入与报名锁定；没有该提供者时跳过 PK。
  报名没有 RPC：`RefreshRestSignups` 在报名窗口内每拍按休息生命的已提交脚点是否在公共圆盘内（圆心为六个 `pk_signup_<槽位>` 的平均位置，
  半径 `AIRDROP.PickupRadiusCm`，边界算在内）重算 `Timeline.Signups`，截止时再算一次并锁定；全员准备（`FinishBuildEarly`）在 PK 休息里只把配装缩短到剩 `REST.SignupSec`。
  Draw 用本局私有随机流抽赛制、阵营与六个 `PKSpawn` 定位点的旋转；PKReady 经 `EnterPKLives` 共同结束休息生命并放置 PK 生命（独立隔离组与 `PKSide`，无保护、无喷漆资格）；
  PK 步骤里落投掷、每 `PK.RingDamageTickSec` 结算圈伤，判定出结果即提前结束；Settlement 把 PK 生命送回休息位，并经 `ALDDevelopmentDirector::GrantPKPoints` 发属性点
  （每场每人一个业务身份，重复为 `PKAwardDuplicate`）。PK 死亡记 `NoRewardPKArena`，不计击杀、死亡与奖励。押注与投掷经 `ServerPKBet`/`ServerPKThrow` 交
  `ALDPKDirector::SubmitBet`/`SubmitThrow`，回 `ClientPKBet`/`ClientPKThrow`；AI 走同一入口。公开读模型是 `FLDPublicMatchView::PK`（`FLDPKView`）。
  入场超过 `REST.EntryReadyTimeoutSec` 仍放不下时本场记无效（`LD_PK_ARENA_BLOCKED`）。
- **共同入场**：返回野外由 `PrepareJointPhase` 先为全部目标准备位置与战斗状态，全部成功才 `CommitPhaseBody` 一起提交；有人放不下就全部释放并撤回出生请求（`ULDMapRegistry::WithdrawSpawnRequest`），
  下次以新请求身份重试。未就绪时 `FLDMatchTimeline::Advance` 的 `bBodiesReady` 为假，时间线停在边界（`IsEntryHeld`），T、旗杆与复活都不开始；等待记 `LD_REST_RETURN_HELD`，
  超过 `REST.EntryReadyTimeoutSec` 为 `LD_REST_RETURN_UNREADY` 权威故障。
- **野外 → 休息**：存活野外生命退休而不死亡（`PhaseRetired`，无 K/D、播报、奖励或复活等待），剩余复活等待结束；`EnterRestWorld` 中 `ALDCombatDirector::CancelAllForPhase` 取消全部进行中效果
  （`LD_PHASE_EFFECTS_CANCELLED reason=CancelledByPhase`）并结束控制、减速、限时加成、标记、灼烧、破防、护盾、嘲讽、强化与硬控递减；每人得到新的休息生命（满 HP/MP），`ReadyAllForPhase` 让全部冷却与普攻节奏就绪。
- **休息 → 野外**：`ExitRestWorld` 同样清理并解冻世界、退休休息生命；进入休息时存活者放回记住的野外脚点，或经 `ReserveSpawnNear`（3 圈 × 8 方向）在 `REST.ReturnSearchRadiusCm` 内找可达地面，仍不行正常抽点；
  已死亡者正常抽点；存活者先放。所有人获得 `LIFE.ProtectionSec`；R0 返场是喷漆名单上的 `Joined`（可带保护截止）。休息生命若死亡在休息锚点重放（`PlaceRestLife`，被阻挡记入 `PendingPhasePlacement` 每拍重试）。
- **世界冻结**：营地、老板、外卖与上热搜按 T 运行；休息中 driver 跳过营地运动、营地意图与世界推进；`FreezeForRest` 让 Combat 挂起营地成员与 Boss（保持 HP/MP、不回复、不能被命中或行动）；
  返场 `ThawAfterRest` 在冻结处解冻并重新选择目标。冻结成员仍每步以零输入推进运动，以满足运动证据覆盖。
- **训练区**：`ALDTrainingRoomDisplay` 只呈现地面边界与区域名；`SpawnTrainingDummy` 在各槽位锚点建立真实共享目标（该槽位公开等级的无构筑底盘），各休息室连接经观察代理呈现；
  归零回满、不发奖励、不进名单或喷漆资格；锚点被占时复用七个合法偏移，全被占用时待入者等待重试。`ServerTrainingReset` 带 match/life/rest/步骤起点，只重置本人与对应假人。训练资源与效果在阶段边界清理。
- 其他世界拥有者在 `EnterRestWorld/ExitRestWorld` 接入；查询入口为 `IsRestPhase`、`IsInputLocked`、`ReadBuildStage`、`GetFieldNowMs`、`IsFieldRunning`、`FieldDueAuthorityMs`、`NextTimelineDueMs`。

## 时间、事件与窄接口

时间结构：`FLDAuthorityStep{Match, StepSerial, Start/EndServerSeconds}`、`FLDAuthorityBoundary{Match, BatchSerial, AtMs}`、
`FLDAuthorityEventStamp{Generation, Provider, StreamId, ProducerSequence, OccurredAtSeconds, OccurredAtMs, RecordedAtMs, CommitOrdinal}`、`FLDProviderFence{Generation, Provider, StreamId, ThroughMs, ThroughSequence}`；
`ELDProviderId`：Session、Motion、Combat、Life、World、Progression。

- 跨流先比较未取整的 `OccurredAtSeconds`，真正同刻才比较因果、阶段与 `CommitOrdinal`；`OccurredAtMs` 是 checked ceil 分桶，`RecordedAtMs` 是记录审计桶，效果 `DueAtMs` 不等于已发生。
  可用性用 checked floor 的 NowMs（`GetAuthorityNowMs`），不能用 ceil 桶提前开放冷却、攻击、保护或复活；例如实际只到 1000.2 ms 时 1001 ms 的能力不可用。
- 内容 `*Sec` 是时长，生成时必须能精确投影成有界整数毫秒，否则拒绝。同世界换局只换比赛时间原点，不清零 `UWorld::TimeSeconds`。
- AtMs 是整数封桶上界，不是把桶内事实改成同一时间；driver 在原始发生时刻有序停住处理，再推进剩余部分。先于 Close 发现的同刻 Combat 因果在 Capture 前排空为子批；循环或无进展显式故障。
- 原生运动的 segment 序与圈事件序分开：一段穿圈产生入/出两个事件；`ExtractCircleCrossings`/`LDCaptureCircle::Extract` 用合法落地脚点平面中心与 `CAPTURE.RadiusCm`（边界算在内）。
  已确认的 `NotGrounded` 是确定的不在区内，重新取得支持面时重新查询，即使 XY 没变。缺覆盖所需时刻的运动证据返回 `GeometryUnavailable` 并阻断边界，不默认 Miss。

| GameMode 窄接口 | 契约 |
| --- | --- |
| `BeginAuthorityStep(Match, EndAtMs)` / `GetAuthorityStep` / `ValidateMotionStep(Step, Body)` | 仅 driver 调用；发放上界内的原生区间；远程 packed/saved moves 不能绕开 |
| `GetAuthorityNowSeconds` / `GetAuthorityNowMs` | 同一时域；后者 checked floor，不回拨 |
| `PublishCaptureMotion` / `PublishCaptureEligibility` / `PublishCaptureInterrupt(Permit, Body)` | 位移进出 / 仅 Session 与 Life 发布 Joined、Left、Died、Respawned、ProtectionEnded、PhaseRetired / 仅 Combat 的硬控提交 |
| `EndContenderLife(Fact, Permit)` | 消费一次 Combat 死亡事实，不改 XP |
| `AdvanceTimeline` / `LockTimelineBuild` / `EnterRestWorld` / `ExitRestWorld` | 阶段转换；失败即边界故障 |
| `EndProtectionForAggressiveIntent` | Navigation/Combat 共享 |
| `IsGameplayWriteOpen` / `ValidateWritePermit(Permit, Phase)` | 前者只是快速检查；后者在最终提交点核对 Match、批次、阶段、GateRevision 与闸门 |
| `TryCloseMatch` | 唯一结果 CAS：守护提案须带当前 CaptureEpoch、主人与已封闭到 DueAtMs 的边界，且没有未处理的必需奖励 |
| `FailAuthorityBoundary(Boundary, Fault) -> FLDFaultReceipt` | 一次切 `Faulted`、关闸、旧 permit 失效；含原因、服务键与未支付 DeathKey；不带胜者；已有结果时返回 AlreadyTerminal |

事件与水位回执区分 Accepted、Duplicate、StaleMatch、StaleLife、UnknownStream、SequenceGap、TimeRegression、AlreadyClosed 与故障。当前实现是同步驱动：Motion 流的 Fence 等价物是 Capture 阶段逐身体本步证据检查
（本步新登记的生命除外）。故障后沿 GameInstance 失败路径回前端并退休该局，不计为正常完成；普通玩家命令的可理解拒绝不触发 Faulted。

**结果与统计**：`FLDResultReceipt{Match, ResultId, ResultSerial, Reason, OptionalWinner, EndedAtMs}` 只提交一次，GameState 与归约器读同一结果。结束原因为 `GuardSucceeded`、`ObjectiveTimeout`
（终局期限到达时在存活、在局、非保护且有贡献者中按历史守护、成功喷漆排序）、`NoChampion`（并列、无人有贡献，或无人存活且没有合法排队复活）与 `HostEnded`。HostEnd 按实际受理时刻关闭。
K/D/A 由 GameMode 从同一 DeathKey 派生一次：真实死亡加 Deaths，另一参赛者的合法击杀加 Kills，冻结的每名助攻者加 Assists（经 `FLDCaptureEligibilityEvent::Assisters` 进入核心，同时写入
`FLDKillNotice::Assisters`）；无主/自杀没有击杀者与助攻，离开/重置不加死亡，野怪不进统计；同键重放不重计。成长只提供 FinalLevel。DEV 构建的 `FLDCaptureDevTally` 只是遥测，不参与规则。
关闭后延迟伤害、控制、回复、奖励、选卡、营地刷新、复活、喷漆与守护都没有新提交权。

**生命周期矩阵**

| 状态 | 局部取消（S、Esc、关面板） | 死亡 / 复活 | 普通离局 | 终局 | 新局 |
| --- | --- | --- | --- | --- | --- |
| 订单、追击、可中断动作 | 按操作域取消 | 取消旧生命工作，新生命无继承 | 撤销命令权，不转 AI | 关闸后取消 | 旧 saved move、order、token 拒绝 |
| 已释放效果 | 不回滚、不退款 | 独立效果继续原期限 | 立即取消，不回滚既成事实 | 不再结算 | 清空 |
| 等级、XP、技能 | 不变 | 保留 | 仅 settlement-only 接收 | 冻结 | 全新建立 |
| 来源冷却 | 不清零 | 按服务器时间继续 | 不再提交动作 | 不再释放 | 新时间域 |
| HP、劲、控制、保护 | 按真实结果 | 复活满 HP、起始劲、清旧控制、原子建立保护 | 立即移除身体与碰撞 | 不再变化 | 全新身体 |
| 喷漆、涂鸦、守护 | 停止/面板/战斗不清零 | 死亡或硬控清未完成喷漆；守护、涂鸦与历史贡献保留 | 清未完成喷漆、失去冠军资格、保留贡献 | 不再推进 | 全部清空，从 R0 开始 |
| 观察与私有回执 | 不改服务器可见集合 | 旧生命观测撤销 | 关闭通道 | 保留获准结果 | 代次全换 |
| 本地名称与设置 | 保留 | 保留 | 保留 | 保留 | 保留，不夹带局内战力 |

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
| `LogLDTrace` | 每秒一行的 Host 心跳、Host 的施法计数行与客户端本人状态；`-LogCmds="LogLDTrace off"` 关掉它时连采样也跳过 |

Host（Listen Host 的权威代码）在试玩期记录完整权威信息：

| 标签 | 写入位置 | 字段要点 |
| --- | --- | --- |
| `LD_MATCH_HEADER` / `LD_MATCH_ROSTER` | `LDGameModeTrace.cpp`，开局时 | 产品版本、wire、源码提交（打包版读包旁 `PackageManifest.json`，编辑器读 git HEAD）、内容摘要、网络地址；每名参赛者的槽位、名字、真人/AI、房主、角色、地址 |
| `LD_HEARTBEAT` | `LDGameModeTrace.cpp`，权威时间每秒一次（`LDLog::FPeriodicGate`） | 每名参赛者的生命阶段、脚点、生命/劲/护盾、等级、经验、待选、当前订单与目标点、正在前摇的动作、公开状态 |
| `LD_CAST` | `LDGameModeTrace.cpp`，每次权威推进后与结果锁定时 | 读 Combat 构筑台账的 `ability_casts`（每次成功释放 Q/W/E 记一次，二次施放也算一次；内置冲刺与私人训练不计），有新增时每名参赛者一行：权威时间、生命、本局累计次数 `casts` 与本次新增 `added`；没命中的释放也在内。`t` 与 `life` 是提交该释放的那次推进结束时的值，最多晚一次推进（固定 60 FPS 约 17 ms），同一推进里先释放后阵亡的技能也可能记在死亡时刻之后 |
| `LD_HIT` | `LDCombatEffects.cpp` `ApplyHit`，每次已提交命中 | 来源/受害者及营地类型、来源技能、命中类别、伤害类型、伤害、护盾吸收、实际掉血、扣血前后与生命上限、护盾前后、增幅（`amp_bp`，加成池与倍率合计）与抗性（已计破防）、暴击/保护/周期（区域脉冲与灼烧）/反伤/附加伤害、控制（`root`/`stun`/`disarm`/`taunt`/`knockback`/`slow`/`silence`）、吸血（来源实际回复的生命，已计生命上限）、是否致死，末尾为加成池 `pool_bp` |
| `LD_DEATH_RECEIPT` / `LD_CAMP_DEATH` | `LDGameModeLife.cpp` | 原字段后追加凶手（CreditOwner）、击杀类别、致命来源、来源身体、助攻者、无奖励原因、等级 |
| `LD_REWARD` / `LD_LEVEL_UP` | `LDGrowthDirector.cpp` 复合奖励安装后 | 每份奖励的领取人、原因（`cause=death`/`map_event`/`dev_fixture`，`role=killer`/`assist`/`recipient`）、经验及其结余、等级变化、获得的三选一次数 |
| `LD_OFFER` / `LD_CHOICE` | `LDDevelopmentDirector.cpp` | 每次出卡的候选（含 NPC 出卡）；确认的卡、结果、槽位、是否存为图纸 |
| `LD_PLAYER_MARK source=host_view` | `ALDGameMode::LogPlaytestMark` | 任一玩家按标记键时，每名参赛者每秒最多一条。标记带发送时的局次，只在该局仍是当前局且处于休息（Rest）、野外（Development）或旗杆开放（CaptureOpen）时记录；上一局迟到的或在大厅、结算时到达的标记写 `LD_PLAYER_MARK_REJECTED`（局次、当前局次、阶段），同样限频 |
| `LD_MATCH_SUMMARY` / `_ROW` | `LDGameModeTrace.cpp`，结果锁定时 | 结束原因、胜者；每人等级、经验、K/D/A、选择次数、主动、图纸与五维点数 |
| `LD_NET role=host` | `LDPlaytestTrace`，每 10 秒 | 每个客户端连接的 RTT、抖动、收发丢包、未收包时长、引擎上一统计周期的每秒收发字节（`in_bytes_s`/`out_bytes_s`）、本窗口服务器发出的移动校正（`ULDCharacterMovementComponent::ServerCorrectionCount`） |

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
客户端的 `LD_NET role=client`（RTT、抖动、丢包、每秒收发字节、本人移动校正次数与误差，以及观察流的当前呈现延迟
`obs_delay_ms`、到达迟滞包络 `obs_late_ms`、接收超时 `obs_timeout_ms` 与累计超时清除次数 `obs_timeouts`）和单帧 ≥ 100 ms 时的 `LD_PERF_HITCH`
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

| 规则测试 | 覆盖 |
| --- | --- |
| `DemoCapture.*`（含 `.Geometry`、`.PublicViews`、`.ProviderGate`、`.SessionEndgame`、`.CombatHitInterrupt`） | 喷漆暂停/续计与打断、涂鸦与守护保留、历史贡献、期限资格与并列无冠军、复活次数与截止、同刻顺序与步内守护完成、几何、公共 DTO、提供者门禁、Host 结束/回 Lobby/换代隔离、名称解析 |
| `DemoCore.*`（含 `.CaptureCompletesInsideStep`）、`DemoSessionReceipts`、`ViewPrivacySchema`、`DemoOwnerViewTeardown` | 核心时间与许可、步内守护完成、会话窗口重放/拒旧、公开字段白名单、本人视图停发条件 |
| `DemoRest.Timeline/.ReadyEarly/.PhasePolicy/.EntryHold/.SpawnIdentity`、`DemoRest.Objective.*/.Respawn.Choice/.Bounty/.DailyModifiers/.CampAffixes/.Explore.*`、`DemoShowdown.*`、`DemoVision.EffectAccess` | 时间线时长与提前结束配装、T 冻结与推进、隔离组与阶段清理、共同入场等待、撤回出生请求重试、野外目标、死亡选点、悬赏、今日热点、营地词条与隐藏物、封街圈伤切分、隔离组飞行物权限 |

各角色 runner 要求恰好报告其稳定检查 id，缺失、多出或重复都失败。`RunDemoSessionFaultCheck.ps1` 覆盖客户端离局、Host 进程丢失、加入失败、版本/满员/已开局拒绝、开局竞态与 Host 经菜单离开；
`RunDemoOpponentCheck.ps1` 覆盖 AI 绑定、自主循环、AI 以 `GuardSucceeded` 获胜与回 Lobby 后退役。

证据限制：规则测试与场景只在同一台机器的回环连接上运行，跨机器局域网与公网直连没有证据；3+3 满员、打包版与网络异常在里程碑与发布验收时运行，结论只认当次 `Artifacts`；
外层休息流程、野外目标、死亡选点、售货机与训练假人只有规则测试，没有拥有连接场景；视野过滤不能防范恶意 Host；Host 断线时客户端按引擎默认约 60 秒连接超时后才回到菜单。

**内容键**（数值只在[内容与数值](../demo/CONTENT_AND_BALANCE.md)；作者秒经生成链变为严格检查的毫秒时长，绝对截止只在本局服务器建立）：

| 键 | 消费者 |
| --- | --- |
| `MATCH.FieldSegmentSec` | `FLDMatchTimeline` 的三段定时野外 |
| `REST.OpeningBuildSec/BuildSec/ReturnSec` | 休息步骤（`FLDTimelineParameters::Validate` 与导入校验） |
| `REST.SignupSec/DrawSec/BettingSec/PKReadySec/PKMaxSec/SettlementSec/PayoutSec` | R1、R2 的 PK 步骤（`ELDRestStep::Draw..Payout`；`LDMatchTimeline::IsPKRest`）。可选提供者 `ELDMatchProvider::PK` 经 `FLDMatchProviderHooks::RestStep` 收到每个休息步骤与配装锁定，`ALDPKDirector` 据此抽签、放置 PK 生命、判定与派奖；没有该提供者时报名被忽略、每次休息跳过 PK |
| `OBJECTIVE.*`、`DAILY.*`、`AFFIX.*`、`EXPLORE.*`、`LIFE.RespawnBaseSec/RespawnPerLevelSec/SpawnChoiceCount/SpawnChoiceSec` | `FLDObjectiveState`、`LDPacing`、`FLDLockerState`、`FLDPeddlerPlan` 与 `LDGameModePacing.cpp`、营地与 Combat 的热点/词条调整 |
| `REST.ReturnSearchRadiusCm`、`REST.EntryReadyTimeoutSec` | 返场就近搜索；共同入场最长等待 |
| `CAPTURE.SpraySec`、`CAPTURE.GuardSec`、`CAPTURE.RadiusCm` | 喷漆与守护归约、世界查询、UI/AI |
| `SHOWDOWN.*` | `FLDShowdownParameters::FromContent`：解锁、期限、封街与复活截止的相对关系 |
| `LIFE.RespawnBaseSec/RespawnPerLevelSec/SpawnChoiceCount/SpawnChoiceSec`、`LIFE.ProtectionSec` | 按死亡时等级取复活等待、候选出生点与选择期限；保护期限 |
| `UNIT.CollisionRadiusCm` | 只服务合法落地与碰撞，不扩大喷漆区或重抽锚点 |
| `POLICY.InvalidAggressionProtection=BreakOnAggressiveIntent`、`POLICY.ReleasedEffectSourceLoss=ContinueForRemainingContender`、`POLICY.SourceLeave=CancelUnsettledNoRefund`、`POLICY.DeadChoiceCommit`、`POLICY.MissingSourceCooldown`、`POLICY.ResourceMaxChange` | 生命、Combat 与成长的已确认政策 |

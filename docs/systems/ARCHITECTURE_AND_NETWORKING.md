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
`ULDUIRoot::Host` 只请求 `L_Demo?listen`；Host/Join 均先核对 Demo 载荷与编入程序的 SHA-256。
读取的是原始字节，不规范化换行，也不回退旧目录。`LDDemoEntry::ValidateSelection` 只接受
完整的前端/Demo 包路径，剥离 PIE 前缀，但不接受同短名的其他包、旧 Profile 或 Humans 覆盖项。
旧保存模式和地图的物理移除须与资产引用审计同批完成，不保留可启动的旧预览分支。

`ALDGameMode::InitGame` 为有效 Demo 房间一次创建 `FLDMatchContext`；
网络 Generation 通过 GameInstance 的有界单调分配器取得，LedgerMatch 单独生成并显式绑定。
`PreLogin` 检查阶段、人数、wire/product/content 兼容性，最终 `PostLogin` 再检查阶段与三真人容量；
不会先创建可用 Pawn、旧职业构筑或奖励再发现房间已满。
前端不创建参赛记录，Lobby 中的身体阶段为 Unspawned。正常开局由 `LDMatchProviders` 门禁：
Combat、Progression（含最终等级读取）、Shop、Camps 均已注册，且有 AI 名额时 OpponentAI 已注册，
Demo 世界注册表已加载，才允许 Host 开始；缺失项逐个写进 Lobby 的 StartBlocker 与 Start 回执。
提供者以各自翻译单元中的静态 `FRegistrar` 登记钩子（Prepare/MatchClosed/MatchReset/LifeCommitted/
ReadFinalLevel），GameMode 不包含提供者头文件；注册本身不等于玩法已正确接通。

`ServerDemoSessionCommand` 传入网络 Generation、单连接序号、命令、结构名单版本及其必要字段，
发送者由拥有 Controller 决定；只有 Host 可以改变 AI 数量、开始、结束本局或带房间回 Lobby。
`EndMatch` 只在 Development/CaptureOpen 于两步之间受理，以最后已关闭边界无胜者锁定结果；
`ReturnToLobby` 只在 Results 受理并须携带当前 ResultId。离开房间沿用断开连接/`Logout`，没有另一条 RPC。
Ready 是设置布尔值，不是服务器 Toggle。准备变化只推进公共发布版本；
成员/AI 数量改变才推进结构版本并清除所有真人 Ready。
`FLDSessionRequestWindow` 只保存当前在途请求、最近终态及处理水位：
同键同条款重放，改条款拒绝，过窗拒旧，同域重入不能开始第二次写入。
这是会话窗口，不代替经济账本、战斗或导航各自的去重机制。

Host 同时运行权威世界和自己的客户端表现。IP 直连不依赖匹配服务，也不会自动解决 NAT。
必须分别验证独立拥有客户端和可见的 Listen Host；拥有 Host 进程的人本来就能访问权威状态，
视野过滤不等于能防范恶意 Host。

## 私有、公共与观测状态

- `FLDOwnerState` 属于具体连接的自身投影；`ALDGameMode::StateFor` 是服务器访问参赛状态的入口。
- 玩家身体和 PlayerState 的网络归属不能泄露其他人的私有资源。
- 其他身体的可见表现主要通过观测中继，而不是把所有权威 Actor 广播出去后在 UI 上隐藏。
- GameState 只放明确公开的事实，不应成为全部玩家位置、背包和构筑的总表。
- `ALDGameState::DemoLobby` 是名单、Host/Ready/AI 名额及公开阻塞原因的只读投影，
  不包含 LedgerMatch、身体坐标或私有构筑。AI 名额尚不是实际 AI 控制器。
- `OnRep_DemoLobby` 通过同一有界读模型检查再发布给 UI：成员身份不得重复或超容量，
  Host/AI/Ready 形状必须一致；旧 Generation、重复/旧发布版本及结构版本回退不能覆盖已接受状态。
- `ALDGameState::PublicMatch` 是 UI/AI 的唯一阶段与圈状态来源：阶段、开放时刻、圈状态
  （未开放/无人/独占/争夺）、唯一占领者、CaptureEpoch 及独占起点/完成时刻；不列圈内人数、
  保护者或隐藏坐标。实时显示时间取同代 `DemoClock.NowMs`。`MatchResult` 仅在 Results 发布一次，
  按开局锁定名单列出全部参赛者（含离局者）的名称、击杀、死亡与最终等级，以及胜者、原因和用时；
  `KillFeed` 是最近 8 条参赛者死亡的公共环形缓冲（击杀者仅限参赛者，营地按档位），不带技能、构筑或位置。
  三者都经各自 OnRep 的形状/代次/版本检查，旧代次或重复发布不能覆盖已接受值。
- 世界观察只使用 Demo 的单一稀疏协议；旧 B91/B92 控制器与独立观测/服务格式不作兼容入口。

详细可见性机制见 [视野与 UI](VISION_AND_UI.md)。

## 请求、代次与回执

所有权由拥有连接决定，不能相信客户端传入的另一名玩家标识。
命令携带其所需的 match、life、sequence/order、build/inventory/object revision，
目标使用当前获准的观察句柄或公开地点 ID，不使用隐藏 Actor 指针。

先验证代次、身份、目标和资源，再提交一次结果。重复的已接受请求返回原回执，
不能重复发钱、创建物品或施放效果。旧 life 的投射物、输入、菜单报价和移动确认不能作用于新身体。
UI 的 Pending 不代表已经提交，客户端不能自行生成成功回执。

`ULDNavigationComponent` 的取消 epoch 与库存/商店的 request 回执
是不同领域的版本，不要用一个全局“最新版本号”替代全部语义。

## 生命周期与失败

Demo Lobby 不通过 `RestartPlayer` 自动生成旧身体。普通离开使当前成员失效；
Host 离开关闭房间并通知其余连接，不把 Host 移交给第一个剩余玩家。
旧 `EndContenderLife`/`ReturnDueContenders`、成长和场景组件仍有待各自闭包迁移，
不代表 Demo 死亡保留、十点复活、终局或同世界新局已经接通。
`IsGameplayWriteOpen` 只是当前 Match/阶段的快速检查，不是正式提交 permit。
`LDMatchLifecycleTypes` 的 VictimKey/BodyRef/DeathKey 区分稳定主体、一次生命和短寿命身体。
GameMode 的 `Lives` 保存唯一当前生命阶段/绑定及退休水位；当前 HP/MP/盾仍只在
`Combat.Bodies.Resources`，OwnerState 是 P2 到显示点数的投影，不从旧 double Hp/Tactical 状态补值。
初次身体绑定只接受已登记的 Unspawned 主体、当前 Starting permit 和重新核验的真实合法位置，
重复调用不再初始化资源。完整十点抽样/预留、死亡事实及复活链仍待对应切片。

GameMode 用同一比赛原点把原生游戏时间变为可执行预算，Combat driver 按真实可达整数上界
推进同一 CMC 区间；不写 `UWorld::TimeSeconds`。`AuthoritySecondsToNowMs` 使用受检 floor，
事件/接收审计桶使用受检 ceil，整数期限直接受检相加，不经 double 往返。
迟收地面意图携核心生成的 NotBefore 时间，不能作用于更早原生区间。
`FLDGameplayWritePermit` 仅由核心在当前调用栈构造，禁止复制/移动；
最终写入重查 Owner、Match、边界、阶段、GateRevision 和当前借用地址，重入不能再发许可。
当前执行切片仅接 Starting 身体、Motion、地面 Actions，不为未实现的 Effects/奖励/占领等阶段
签发成功证明。当前 Motion 批次包含不可变的实际子段/原生支持样本，按 Body 的单一 stream
和连续记录前缀保留；回滚试探不进入提交批次。核心检查相邻批次的起点/支持与上次已接纳终点一致，
不允许两次记录之间的无凭据位置写悄悄成为新起点。退休后的已接纳批次不因弱 Pawn 指针失效而丢弃。
精确前缀确认、原生记录和完成计数都不是提供者 Fence 或可关闭比赛的凭据；
真实 Combat/圈事件消费与 Sealing 尚未齐备时，不能让缺失服务自动给出空成功。

`RunDemoEntryCheck.ps1` 使用真实游戏进程验证旧保存模式与显式 `?game=` 请求。
开发测试观察器区分“创建世界前拒绝”和“实际观察到被拒绝世界”；
后者从世界初始化/Actor 生成及子系统创建/激活处累计记录，销毁和回菜单不清除计数。
最后的前端 live census 只是补充，不能替代累计的“未激活旧玩法”证据。

`ULDGameInstance::NetworkFailed` 处理 pending connection 的 WorldContext 归属，
避免把一次连接失败写入另一个 PIE 实例。错误信息到前端，不无限停留在失效世界中。

对手 AI 应通过同一核心注册、受限观察与权威命令接入，不沿用旧 B92 席位接管或控制器交接。
本局不重连，人数和 AI 行为按 App Spec；纯控制接口不等于已实现自主核心循环。

占领与唯一结果归 GameMode 核心；旧 Objective Actor、Seal/路线/站桩/HeartSensor 已退出。
`FLDCaptureReducer` 是唯一连续独占归约器：生命提供者在提交点调用 `PublishCaptureEligibility`
（Joined/Respawned 带保护截止与初始支持样本、ProtectionEnded、Died 带 DeathKey 与击杀者），
Motion 阶段每个已接纳原生批次经 `PublishCaptureMotion` 由 `LDCaptureCircle::Extract` 求出带真实时刻的
进出（连续子段求交、支持面变化与不连续修正只取前后态；脚点在闭圆盘内且站在注册地面才算占用）。
事实按提交序入队，到其整数桶封闭时才按原始秒、阶段、提交序归约，同一边界内的进出/死亡/保护变化
全部保留；保护者既不占也不争，开放前不计时，Solo 起点取向上取整桶，完成时刻 `DueAtMs` 由驱动切步。
驱动在 Transactions 后依次调用 `ExecuteCapturePhase`（核对每个存活身体本步运动证据、归约并给出提案）、
`ExecuteClosePhase`（先备好全部结果行，再转 Results 并推进 GateRevision）与无 permit 的
`ExecuteCapturePublish`（提供者 MatchClosed 清理、停身体、发布结果/击杀/公共圈）。
漏切完成边界、保护到期未收到 ProtectionEnded、缺运动证据或结果行无法冻结都是明确故障，不默认判胜。
普通离局在受理时刻向归约器提交 Left，不发奖、不判胜，结果行保留。
`ReturnToLobby` 与开局失败共用 `RetireMatch`：先关闸，再按注册逆序调用 MatchReset，
`RetireAuthorityCore` 拆除身体、`RetireMatchDomains` 中的每局领域 Actor 与时钟/许可状态，
丢弃 AI 与已离局记录后为在线真人换新 Generation/LedgerMatch 并清 Ready 与会话窗口。

## 扩展与检查

新增规则先找 GameMode 所拥有的现有领域服务，保持一个权威状态和一条提交路径。
修改序列化时同步生产者、消费者、上限检查、代次拒绝和客户端显示，不能只改结构体尺寸。
新的公开信息必须显式决定，不通过日志、提示或失败原因意外公开。

当前入口：`RunResetCheck.ps1`、`RunCombatNetworkCheck.ps1`、`RunDemoVisionCheck.ps1`，
以及相关 `Source\Lockdown\Private\Tests` 中的生命周期、网络与序列化检查。
准备对局、调试授予或无玩家的监听服务不能代替真实客户端完成一局的证据。
Demo 新接缝使用 `Lockdown.Rules.DemoCore`、`Lockdown.Rules.DemoCombatEvaluation`、
`Lockdown.Rules.DemoCapture`（归约器连续性/保护与契约故障、圈几何、公共读模型、提供者门禁）和
`RunDemoCoreCheck.ps1`；后者以 DEV 自动化入口在真实三拥有连接上调用同一核心提交路径，
固定选取已编译锚点只作身体/运动机制夹具，不代替普通开局、随机出生、完整战斗或 3+3 验收。

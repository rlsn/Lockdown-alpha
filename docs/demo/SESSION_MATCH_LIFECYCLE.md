# Demo 会话、对局与生命周期

**目标：v0.10.0 · 架构职责：M1 / M2 / M5 · Unreal Engine 5.8.4**

产品规则以 [App Spec](../APP_SPEC.md) 为准；跨模块接口以
[架构审计](ARCHITECTURE.md) 和 [共享契约](SHARED_CONTRACTS.md) 为准。
本文规定会话、阶段、生命边界、批次/写闸、喷漆与守护归约与唯一结果必须保持的契约，
并在 §9 列出实现这些契约的源码与检查入口。§3–§6 中的类型与签名描述契约形状；
与源码命名不同时以 §9 所列源码为准，但契约本身不能因此放宽。
本文不是运行通过声明；当前自动化覆盖与证据限制见 §10、§12。

## 1. 结论与责任边界

选择**同一 Listen 世界内的单一会话/对局流程**：现有会话组件改接 M9G 的唯一 `L_Demo` 世界，
从前端创建或 IP 加入后进入 Lobby；
Host 在真人全部准备后开始；开局休息 R0 后三段定时野外各接一次休息（R1–R3），最终野外开放唯一旗杆；喷上涂鸦后守护计时连续满规定时长锁定唯一结果；
Host 带仍连接的房间回 Lobby，再完整初始化新局。Lobby 和结果是该世界的阶段，
不是另外保留旧 profile、额外玩法地图或世界检查点恢复模式。

现有 GameInstance 的连接失败处理、GameMode 的私有参赛记录、GameState 公共投影、
Combat 的身体/命中时序、Navigation/CharacterMovement 的生命和取消代次可以收敛复用。
不能原样保留旧人数/职业开始条件、死亡惩罚、安全最远出生、Heart/Claim/deadline 胜负，
也不能将旧本地重载测试视为同房间多人新局的证明。

本域是共享比赛身份、比赛时钟、实际事件批次及消费水位、生命边界和终局写闸的
**唯一正式提供者**。这些职责接回现有 GameMode/Combat 调度，喷漆与守护归约也归 GameMode 核心，
不新增通用事件总线、第二套身体表、移动积分器、第二套奖励结算或独立胜负 Manager。

| 所有者 | 本域规定的职责 | 不越过的边界 |
| --- | --- | --- |
| `ULDGameInstance` | 本进程连接尝试、失败归属、本地反馈、网络比赛代次分配 | 不保存跨局战力，不替服务器判定成功，不迁移 Host |
| `ALDGameMode` | 房间身份/准入/准备/名单；Match 映射；参赛资格/生命；批次和写闸；唯一喷漆区的喷漆与守护归约（独自喷漆、争夺暂停、失效/硬控清零、涂鸦覆盖与清除、守护计时、冻结与清零）、守护完成提案与唯一结果（N02 归核心 GameMode，由 `FLDCaptureReducer` 实现）；生命侧（`EndContenderLife`、复活、保护解除）在 `LDGameModeLife.cpp`，由 Combat 维护 | 不自行实现命中、发奖励、生成三选一或积分移动；归约器不读客户端位置或借迷雾判断，不另交第二个结果 |
| `ALDCombatDirector` | 既有权威推进入口；身体、动作、伤害/控制；首次致死事实；已提交的定身/眩晕/嘲讽（及击退）经 `InterruptSpray` 通知喷漆区 | 死亡只生产一次；生命域消费同一事实，不再认定另一死亡 |
| 操作域 / CharacterMovement | 实际执行的移动、碰撞、位移分段与运动完成水位 | 不由圈 Tick 推导轨迹；不把当前姿态回填过去 |
| 世界域 | 十点抽样与同点合法落地；喷漆区几何/交点；营地实体生命映射 | 不再维护一份喷漆/涂鸦状态或参赛者生命计数 |
| 成长域 | 致死事实的经验+等级+机会复合提交、Offer、构筑和回执 | 只接受本域当前批次许可；不另外生成 Match、DeathKey 或结束时钟 |
| 视野 / UI / AI | 获准的自身、观测和公共投影；同一权威命令 | 不回写真相；Host 普通主画面和 AI 不因在服务器进程而获得全知权限 |

## 2. 固定规则与实施边界

| 规则 | 本模块必须兑现的含义 |
| --- | --- |
| 房间规模 | 最多三名真人，包含 Host；Host 选择零至三个独立参赛 AI。没有队伍，不补满空位；Host 独自练习仍走同一流程。 |
| 开始条件 | 仅 Host 可开始；全部已连接真人含 Host 都准备；AI 不伪造玩家 Ready RPC；开始时锁定真实参赛名单。 |
| 联机边界 | IP 直连；局域网和正确配置防火墙/手动端口转发的公网可达地址；无自动 NAT 穿透、中继、中途加入、重连或 Host 迁移。 |
| 唯一目标 | 前三段定时野外可以战斗/成长而不能喷漆或获胜；R3 返场、最终野外开始时同一根旗杆开放并一直开放，不重新关闭、轮换或因无人守护成功而超时结算。 |
| 外层流程（#198） | R0 → 野外 1 → R1 → 野外 2 → R2 → 野外 3 → R3 → 最终野外 → Results；一条单调权威时钟，野外进度 T 只在野外推进、休息与 PK 中冻结。休息时每名参赛者在按名单槽位固定的私人休息位有一条休息生命，世界冻结；阶段边界（野外↔休息）结束生命而不是死亡，清除全部进行中效果并补满资源与冷却。用户 2026-11-10 批准（[方案](https://github.com/rlsn/Lockdown-alpha/issues/198#issuecomment-6752601175)，[审批](https://github.com/rlsn/Lockdown-alpha/issues/198#issuecomment-6752628806)，原话：“批准整套方案，按票内参数实施”）；私人休息空间是协调者在用户授权（“你做最终审查和决策”）下的决定。详见 §3.4。 |
| 喷漆与暂停 | 只有仍在本局、存活、非复活保护的参赛真人/AI 计数；野怪、尸体、技能表现不计数。喷漆区内只有一名有效参赛者且他不是涂鸦主人时，他的喷漆进度累积，满 `CAPTURE.SpraySec` 喷上涂鸦；两名以上时所有人的喷漆进度暂停并保留，争夺结束仍由他独自在区内时从暂停值续计，其他人既不累积也不继承。涂鸦主人在区内不喷漆。 |
| 清零条件 | 喷漆者离区/死亡/离局（含争夺进行中），或在施加瞬间受到定身、眩晕、嘲讽、击退，即清零其进度；之后若仍独自在区内，从 0 重新累积。中间进出即使发生在同一帧也不能遗漏。 |
| 不清零条件 | 区内移动、攻击、施法、普通伤害、减速、缴械不影响喷漆；由此实际离区或死亡才按相应事件处理。保护者既不喷漆，也不暂停别人的喷漆或冻结守护。 |
| 涂鸦与守护 | 喷上涂鸦即覆盖原涂鸦，守护计时从 0 开始（喷上时区内只有主人，守护随即计时）。守护只在涂鸦主人独自在区内时计时，受伤、受控不影响守护；主人离区、区内无人时守护暂停在当前值、不清零，主人重新独自回区后从该值继续（2026-11-07 用户决定，原话：“胜利判断好像还是有问题 - 当旗杆被占领但无人在圈内的时候不计算时间，时间停止，现在好像是时间继续计算是吗”）。主人以外任一有效参赛者在区内（不论人数、是否在喷漆、主人在不在区内）守护即冻结，区内非主人全部离开（含死亡、离局）后，主人独自在区内时从冻结值继续、不在区内时保持暂停；冻结期间主人从区内走出、区内仍有非主人，守护清零、涂鸦保留，之后主人独自在区内时从 0 重新累积（2026-11-07 用户决定；对方进圈时主人已在区外只冻结，主人走出与最后一名非主人离开或与非主人进圈同一时刻时不清零）。涂鸦主人死亡或离局，涂鸦立即清除、旗杆回到无主。 |
| 唯一结果 | 守护计时累积满 `CAPTURE.GuardSec`（暂停、冻结期间不计）时涂鸦主人获胜，结束原因 `GuardSucceeded`（“守护成功”）。到期时先处理相关真实事件并证明提供者水位闭合，再重查；成功只锁一个胜者。Host 主动结束无胜者；没有击杀/财富/等级/时间积分胜利。 |
| 出生与复活 | 休息生命站在按开局名单槽位固定的休息锚点；返场时进入休息前存活者回到原脚点或其附近（§3.4）。首次进入野外、野外复活与返场找不到位置时，每次从同一组全部出生锚点独立等概率、有放回抽取；可抽回同点。碰撞只在选中点作必要合法落地/防重叠，不改按危险、视野或敌人距离挑点。 |
| 死亡代价 | 只失去等待时间与场上位置。等级、XP、技能、永久属性加成、未用机会和已生成候选全部保留；可确认三选一，但只更新保留构筑，不提前复活或治疗尸体。 |
| 已释放效果 | 释放者死亡不取消已经成功释放的独立效果，继续到原定结束，后续合法击杀归仍在本局的实际所有者；未完成释放的动作因死亡取消。 |
| 成功复活 | 满 HP/MP，清除死亡前临时控制，获得短暂免伤免控保护；原有冷却按服务器时间继续，不因新 Pawn/新 Life 刷新。 |
| 主动解除保护 | 本人当前 Match/Body/Life、请求新鲜性、包形状及写闸验证后，可识别的进攻意图就先解除保护；即使目标/距离/蓝量等使动作失败也解除。移动不解除。 |
| 离开与新局 | 正式离局立即移除身体，不留可攻击/阻路对象，并取消其未结束独立效果；不回滚此前事实、不发离开奖励、不补AI/自动胜利。Host离开终止会话；新局清全部局内域。 |

已确认补充规则与最终 App Spec 同步：同款两件的基础属性相加、同名被动一次；
遗忘期间原冷却继续，重学沿用原结束时间，不能洗冷却。
初始为近战单体物理普攻，属性/技能/被动路径可提供群攻或增加攻击距离，不是永久近战单体限制。
死亡可确认选择、进攻意图解除保护、上限变化保持 HP/MP 原百分比也已确认；
生命域不因此治疗尸体、提前复活或改动复活满最终上限的规则。
公式和数值仍由内容与战斗单点维护，不在这里复制来源/叠加公式。
时长与距离只引用 `CONTENT_AND_BALANCE.md` 的正式参数键，不在本报告复制数值。

## 3. 状态机与完整会话路径

### 3.1 一条权威阶段链

目标枚举 `ELDMatchPhase` 归 GameMode：
`Lobby / Starting / Rest / Development / CaptureOpen / Results / Resetting / Closing / Faulted`。
数值 2 已退役（原 #153 开局倒计时 `Countdown`），`Rest` 为 9 并覆盖每次休息的全部步骤；前三段野外与最终野外开放前为 `Development`，最终野外旗杆开放后为 `CaptureOpen`。
`Faulted` 是明确停止玩法提交的工程故障状态，不是第二种胜负规则。
本地的“连接中”“加入失败”属于 GameInstance/UI，不复制成第二套比赛状态。

| 当前阶段 / 输入 | 权威行为及目标阶段 | 失败、并发和取消 |
| --- | --- | --- |
| 前端创建 | 校验本地端口/构建可用性，使用当前唯一玩法图 `OpenLevel(...listen)`；服务端初始化后进入 `Lobby` | 加载/监听失败回前端并显示原因；禁止回落 V05 或其他旧图。 |
| 前端 IP 加入 | 本地检查地址与端口格式，`ClientTravel`；服务端握手和准入都通过后才显示已加入 `Lobby` | “正在连接”不是成功。地址不可达、超时、房间满、已开始、版本/内容不兼容分别反馈；错误只作用于所属 WorldContext/连接尝试。 |
| `Lobby` / Ready | 只改变拥有连接本人的 Ready；记录其确认的结构名单版本 | 不接受客户端替别人准备；过期名单返回当前名单和明确拒绝。 |
| `Lobby` / Host 改 AI 数 | 校验 Host 和范围，变更结构名单版本，清真人 Ready 后重新公布 | 空位不自动补角色。准备位变化不再递增结构名单版本，避免最后一人 Ready 使前面的人永远过期。 |
| `Lobby` / 真人进出 | 原子预留/释放真人容量；变更结构名单版本并使旧 Ready 失效 | PreLogin 检查之外，在最终入房提交点重查容量和阶段，覆盖同时到达的两个连接。 |
| `Lobby` / Host Start | 重查 Host、名单版本和所有真人 Ready，关闭准入，进入 `Starting`，冻结参赛快照 | 重复 Start 重放原结果；同 ID 改条款拒绝。连接离开若先提交则改变名单并拒绝旧 Start。 |
| `Starting` / 初始化 | 验证已批准内容、地图及各域依赖，预留出生/必要对象与初始记录；初次身体全部站到各自的休息锚点（休息生命、隔离组、世界冻结）后统一进入 `Rest`（R0 的配装步骤；`PublicMatch.StartedAtMs` 为此刻） | 缺输入/落地/初始化失败不得发布可玩成功；清理未发布对象并退休失败尝试的 Match，发布新一代 Lobby 和明确失败。开始提交前断线使该次启动失败并重准备；提交后按普通离局处理。 |
| `Rest` / 休息步骤 | 时间线按精确边界推进：配装（R0 `REST.OpeningBuildSec`，R1–R3 `REST.BuildSec`，最后 `REST.SignupSec` 兼作 PK 报名，截止时刻含在内）→ 锁定 → 锁定报名者 ≥2 时抽签/押注/PK 准备/PK/结算/派奖，否则直接返回倒计时 `REST.ReturnSec`。构筑命令只在配装与派奖受理（不需安全窗），其他步骤以 `BuildLocked` 拒绝；返回倒计时内订单、攻击、施法与冲刺以 `NotStarted` 拒绝。营地意图、营地运动与世界推进跳过 | 锁定在该边界成长命令之后的 Transactions 阶段执行（截止时刻提交的命令有效），其余转换在 RespawnsAndWorld 开始时执行。被拒请求记入回执窗口，进入野外后不重新生效；房主可结束本局；未选的开局机会保留、不代选 |
| `Rest` / 返场 | 返回倒计时结束：清除休息效果、退休全部休息生命、按 §3.4 放置下一条野外生命并给 `LIFE.ProtectionSec`；进入 `Development`。R0 返场是每名参赛者在喷漆名单上的 `Joined` | 放置被阻挡的参赛者记入 `PendingPhasePlacement`，之后每个边界重试；不跳过、不默认成功 |
| `Development` / 野外段到期 | 定时野外段的 T 到达 `MATCH.FieldSegmentSec` 的整数倍时进入下一次休息（`Rest`），见 §3.4 | 进入休息不是死亡：无 K/D、击杀播报、奖励或复活等待 |
| `Development` / 最终野外开放 | R3 返场即最终野外开始，`FLDCaptureReducer::ScheduleOpen` 把开放时刻定为该边界，同一边界的 Capture 阶段进入 `CaptureOpen`，只在开放后开始计喷漆进度 | 仍可升级、战斗；不能把前三段野外的区内停留时间算入喷漆。 |
| `CaptureOpen` / 守护完成提案 | 闭合真实事件边界、重新判定涂鸦主人与连续守护；GameMode 先锁结果和写闸，进入 `Results` | 多个提案不能互相覆盖；未闭合水位不是成功，旧局/旧生命/旧 CaptureEpoch 的提案拒绝。 |
| 活跃阶段 / Host 结束 | 在权威边界提交 `HostEnded`，无胜者，进入 `Results` | 不借终止挑选最高分；与自然胜利的竞争按批次顺序裁定，不能先发布两个结果。 |
| `Results` / Host 返回 Lobby | 先保持旧闸关闭，进入 `Resetting`；全域失效、拆除身体和局内对象、换 Match 后发布 `Lobby` | 保留已连接房间，不要求客户端伪造一次重连；全真人 Ready 清除，不能自动重开。 |
| 任意房间阶段 / 普通成员离开 | 立即撤销资格/命令、移除身体及碰撞、取消未结束独立效果；其余人继续正常规则 | 不是死亡、不发离开奖励、不接管AI；已发生命中/致死不回滚，已锁事实的结算接收记录不因身体移除而删除。 |
| 任意阶段 / Host 离开或丢失 | 关闭服务器会话；所有客户端撤销输入/观测和 Pending，回前端显示原因 | 不停在失效地图，不迁移 Host，不自动恢复旧房间。 |
| 必需权威准备/提交失败 | 停止受影响边界和新玩法写入，发布工程故障；进入 `Faulted` 或明确中止连接流程 | 不用漏发奖励、跳过事件或默认数据继续判胜。故障退出不是正常完成对局。 |

### 3.2 身份与准入不能由界面代替

真人身份从拥有的 PlayerController/连接解析，服务端创建 `FLDContenderRecord.Session`；
显示名称、头像、数组下标和 Pawn 指针都不是身份。AI 由服务器为其参赛记录分配同类
`ContenderId`，直接调用相同领域验证，不伪造网络连接或客户端 RPC。

未通过准入的连接不能获得可用战斗 Pawn、成长记录或观测权限。
匹配中的名单不允许补位：原连接断开后再连，即使名称/IP 相同也不能接回旧参赛者。
只在明确回到 Lobby 后接受新的加入；这不等于中途重连。

兼容性分三层检查：Unreal 既有网络版本、项目 wire 协议版本、产品版本与内容摘要。
项目须增加自身可解释的版本/内容准入，不声称 Unreal 没有网络版本检查；
`0.10.0` 不是目录 schema、枚举值或协议号。

### 3.3 同世界回 Lobby，不复原旧世界快照

初次建房从 `L_Frontend` 进入唯一 `L_Demo` Listen 世界，由其 `DA_DemoWorld` 和
MapRegistry/Anchor 提供世界定义；在房间存在期间，Lobby、比赛和结果转换留在
这个世界内，由 GameMode 阶段驱动身体/服务的创建和清理，连接不换归属。
不把 `OpenLevel` 本地重载当成保持其他玩家连接的返回方案，也不引入
checkpoint/restore、席位交接或第二套跨旅行的战力保存对象。

`Resetting` 必须等每个领域确认旧局对象、回调、回执执行权和观察代理已失效，
然后才发布新一代 Lobby。只能保留房间连接、Host 身份、供展示的名称和本地设置；
不能从旧 OwnerState 或 Pawn 拷贝成长、候选、冷却或期限作为新局初值。
AI 数量可以作为房间配置重新展示，AI 的局内记录必须重新初始化。

### 3.4 休息、野外进度与阶段边界（#198）

用户 2026-11-10 批准外层参数（[方案](https://github.com/rlsn/Lockdown-alpha/issues/198#issuecomment-6752601175)，
[审批](https://github.com/rlsn/Lockdown-alpha/issues/198#issuecomment-6752628806)，原话：“批准整套方案，按票内参数实施”），
取代 #153 的开局倒计时（`MATCH.CountdownSec`、`MATCH.DevelopmentSec` 与全程单值 `LIFE.RespawnSec` 已删除）。
数值只在[内容与数值 §3.4](CONTENT_AND_BALANCE.md#34-空间与对局节奏参数)维护。

- **时钟**：只有一条单调权威时钟。`FLDMatchTimeline`（`LDMatchTimeline.h/.cpp`）是 GameMode 拥有的纯归约器，从不读世界时钟，
  每次调用传入权威时刻，转换都发生在精确边界上（`ALDGameMode::NextTimelineDueMs` 交给 Combat driver 作停靠边界）。
  野外进度 T 是派生累计值，只在野外段推进（三段定时野外各 `MATCH.FieldSegmentSec`），休息与 PK 中冻结；它不是第二个世界 Tick。
  公开读模型为 `FLDPublicMatchView.Timeline`（`FLDTimelineView`：阶段、休息序号、野外段、步骤与起止、报名开放时刻、T 基准、
  本段 T 终点、锁定人数、PK 是否跳过、报名名单），客户端与 AI 用 `LDMatchTimeline::FieldNowFromView` 按同一规则推导 T。
  `PublicMatch.StartedAtMs` 是开局（R0 开始）时刻；`OpenAtMs` 在最终野外开始前为 `MAX_int64`，由 `FLDCaptureReducer::ScheduleOpen` 定为最终野外开始的边界。
- **私人休息空间**（协调者在用户授权“你做最终审查和决策”下的决定）：每名参赛者的休息生命站在按锁定名单槽位选出的已批准出生锚点
  （`ALDGameMode::RestAnchorFor`：目录出生点序号 `slot*N/6`，当前 12 个锚点即 s00、s02、…、s10），同一世界、连接、参赛身份与成长记录；
  身体在 Combat 中属于自己的隔离组，休息视野过滤让休息中的参赛者只看见本组，任何人都看不见冻结的世界。不新增几何或锚点。
- **休息步骤**：Build（R0 `REST.OpeningBuildSec`、R1–R3 `REST.BuildSec`；最后 `REST.SignupSec` 为 PK 报名，截止时刻含在内）→
  锁定报名者 ≥2 时 Draw `REST.DrawSec` / Betting `REST.BettingSec` / PKReady `REST.PKReadySec` / PK（至多 `REST.PKMaxSec`）/
  Settlement `REST.SettlementSec` / Payout `REST.PayoutSec`，否则直接 → Return `REST.ReturnSec`。
  构筑锁定在该边界成长命令之后的 Transactions 阶段执行（`LockTimelineBuild`，截止时刻提交的命令有效），其余转换在 RespawnsAndWorld 开始时执行（`AdvanceTimeline`）。
  构筑命令在 Build/Payout 受理且不需构筑安全窗（`ReadBuildStage` = Rest），其他步骤以 `BuildLocked` 拒绝（RestLocked）；
  Return 步骤 `IsInputLocked` 为真，订单、攻击、施法与冲刺都以 `NotStarted` 拒绝。休息服务（NPC、洗点、替换、遗忘）见[成长事务](../systems/GROWTH_TRANSACTIONS.md)。
- **PK 接口**：PK 是 #198 会话 D 的可选提供者 `ELDMatchProvider::PK`，`FLDMatchProviderHooks::RestStep` 收到每次步骤进入与锁定（`FLDRestStepNotice`）；
  报名经 `ALDPlayerController::SubmitRestSignup` → `ServerRestSignup` → `ALDGameMode::SubmitRestSignup` → `ClientRestSignup`，
  PK 提前分出结果时提供者调用 `ALDGameMode::FinishRestPK`。没有该提供者时报名以 `ELDRestSignupResult::NoPKProvider` 明确拒绝，每次休息跳过 PK。
- **野外 → 休息**：每个存活的野外生命退休而不死亡（喷漆资格变化 `PhaseRetired`：离开喷漆名单，没有 K/D、击杀播报、奖励或复活等待；
  生命提供者收到 `ELDLifeChange::PhaseRetired`），剩余的野外复活等待结束；`ALDGameMode::EnterRestWorld` 中
  `ALDCombatDirector::CancelAllForPhase` 取消全部进行中效果（投射物、待结算命中、区域、引导、地面效果；日志 `LD_PHASE_EFFECTS_CANCELLED reason=CancelledByPhase`，
  不虚构命中、打破或死亡），并结束每个身体的控制、减速、限时加成、标记、灼烧、破防、护盾、嘲讽、强化与硬控递减；
  然后每人得到一条新的休息生命（构造即满 HP/MP），`ReadyAllForPhase` 让全部冷却（含已遗忘主动保留的冷却记录）与普攻节奏就绪。
- **休息 → 野外**：`ExitRestWorld` 做同样清理并解冻世界，退休全部休息生命；进入休息时存活者的下一条野外生命放在记住的野外脚点，
  或经 `ULDMapRegistry::ReserveSpawnNear`（脚点，再 3 圈 × 8 方向）在 `REST.ReturnSearchRadiusCm` 内、从脚点可达的地面上找位置，仍不行则正常抽点；
  进入休息时已死亡者正常抽点；存活者先放置。所有人获得 `LIFE.ProtectionSec` 保护；R0 返场是每个人在喷漆名单上的 `Joined`（`Joined` 现在可以带保护截止）。
  被阻挡的放置记入 `PendingPhasePlacement`，之后每个边界重试。休息生命若死亡（不应发生）在休息锚点重新放置，不产生喷漆资格。
- **野外复活等待**按死亡时的 T 固定：`LIFE.RespawnSec.Field1/Field2/Field3/Final`（`RespawnWaitAtFieldMs`）。
- **世界冻结**：营地、画饼老板、天降外卖与上热搜的时间表按 T 运行（以 0 为起点解析；营地刷新与营地成长阶段也按 T），
  driver 在休息中跳过营地运动、营地意图与世界推进；`ALDCampDirector::FreezeForRest` 让 Combat 挂起每个营地成员与 Boss
  （保持 HP/MP、不回复、不能被命中或行动）。返场时 `ThawAfterRest` 让成员在冻结处解冻，忘掉目标与敌对来源后重新选择，HP 不变；
  刷新截止按 T 计算，休息既不执行也不跳过它们。这就是批准的规则：野怪与 Boss 随 T 冻结并保留剩余 HP，休息中不回复、不刷新，不搬回营地。
  冻结的成员仍每步以零输入推进运动（原地停住），以满足运动栅栏对全部已注册身体的覆盖。
- **私人训练假人**：私人训练假人（协调者决定，按用户授权“你做最终审查和决策”：现有参赛者身体、按主人当前等级、没有构筑、生命归零即回满不会死亡、命中与结果只给主人、不给经验或奖励；“重置训练”让本人与假人回满并冷却就绪，训练中消耗的资源与冷却不带回野外）。休息生命放置后由 `SpawnTrainingDummy` 在本人休息锚点的空闲出生偏移处生成（与出生同样的合法地面与占位规则，无空位时记录 `LD_TRAINING_DUMMY_BLOCKED` 且本次没有假人）；它是参赛者类、隔离组为主人的身体，不进名单、不产生喷漆资格，Combat 在致死时回满（`LockDeath`）。`ServerTrainingReset` 只排队（`ELDTrainingResetResult`），在 RespawnsAndWorld 由 `Combat::ResetTraining` 清除双方效果并回满、冷却就绪；休息生命退休、离局或对局退休时 `RetireTrainingDummy` 移除它；阶段边界的补满保证训练资源不带回野外。
- 其他世界拥有者（如后续的符咒）在 `EnterRestWorld/ExitRestWorld` 接入；查询入口为 `IsRestPhase`、`IsInputLocked`、`ReadBuildStage`、
  `GetFieldNowMs`、`IsFieldRunning`、`FieldDueAuthorityMs`、`NextTimelineDueMs`。

## 4. 唯一核心身份与写权限

以下类型由本报告单点定义，统一归 `Framework` 的窄共享头 `LDMatchLifecycleTypes.h`。
该文件已有 Match 上下文、阶段/参赛种类及房间回执基础；下列完整 Victim/Body/DeathKey、
实际时序/许可和相关消费者仍是后续核心实施，不把已有头文件当作全部类型已接通。
不将这些内部上下文全部机械序列化进每个 RPC。

```cpp
struct FLDMatchContext
{
    uint32 Generation;
    FGuid LedgerMatch;
};

enum class ELDVictimKind : uint8 { Contender, CampMember };
enum class ELDContenderKind : uint8 { Human, OpponentAI };
enum class ELDLifePhase : uint8 { Unspawned, Alive, Dead, Left };

struct FLDVictimKey
{
    ELDVictimKind Kind;
    FGuid EntityId;
    uint32 Life;
};

struct FLDCombatBodyRef
{
    FLDVictimKey Victim;
    FGuid BodyIdentity;
};

struct FLDDeathKey
{
    FGuid LedgerMatch;
    FLDVictimKey Victim;
};
```

| 身份 | 正式含义 / 唯一分配者 | 跨生命周期约束 |
| --- | --- | --- |
| `Generation` | GameInstance 的单调 `uint32` 网络局代次，经 GameMode 绑定当前上下文 | 初入房间和每次新局换代；零值无效；达到上界必须明确拒绝继续分配，不能环绕复用。 |
| `LedgerMatch` | GameMode 为相同新局一次创建的非零 `FGuid`，作为 DeathKey 等本局业务键 | 与 Generation 显式一一映射；不强转、不从 Generation 伪造 Guid、不接受客户端传入 Guid。 |
| `ContenderId` | 现有 `FLDContenderRecord.Session` 的职责；在当前 Match 内稳定的参赛者 | 生死和换 Pawn 不变；不等于连接、UI 槽位或观察句柄。若房间下一局复用该 Guid，仍必须有新的 Match 才具有意义。 |
| `Victim.EntityId` | 参赛者等于 ContenderId；营地成员由 World 将当前 Match 下的 CampId/MemberId 显式映射一次 | 野怪也只有一个战斗主体身份，不借 Controller 或最后一次攻击者补身份。 |
| `Victim.Life` | 该主体单调生命代次；参赛者由生命域分配，营地成员通过相同核心注册边界分配 | 死亡只将当前生命置无效，不提前多增一次；成功复活/实际营地新生命才增加。回位不是新生命。 |
| `BodyIdentity` | 复用 `FLDBodyCombatState.Identity` 的角色，由 Combat 在新生命注册时生成 | 稳定主体和短寿命身体分开。同一主体生命至多一个绑定；重绑同一 Pawn 不获得第二次死亡/奖励资格。 |
| `DeathKey` | LedgerMatch + Victim(kind/entity/life) | 不含来源、FatalEffectId、DeathSerial 或可变时间；这些只能是事实审计字段，不能扩大去重域。 |
| Order / Epoch / revisions | Navigation 的订单/取消域、Combat token、Build 等各有原本所有者 | 不是 Life 的替代，也不合成一个全局“最新版本”；保留各域分别拒旧的目的。 |

GameMode 的 `GetMatchContext()` 是唯一当前映射入口；拥有请求中的网络 Generation
先与当前连接/房间上下文核对，服务器再补齐 LedgerMatch。
旧 Generation 即使携带当前 ContenderId、Life 或新 request ID 也不得执行。
死亡记录和奖励回执使用同一 DeathKey；没有奖励的合法死亡也消费该键，
不能以后换来源、换效果或重绑身体重试一次奖励。
`ELDLifePhase` 是唯一生命阶段；`AliveProtected` 是 Alive 与保护期限的派生资格，
不另存第二份生命计数或可写 Alive 标志。死亡中选卡已允许，成长命令必须同时带当前
Life 和 ExpectedLifePhase，不能让死前发出的 Alive 确认借同一 Life 在 Dead 阶段执行；
复活又必须通过新的 Life 检查。死亡确认只改持久构筑和派生定义，不改尸体当前 HP/MP、
ReturnAt 或存活资格；新生命再按当时最终上限满 HP/MP 初始化。

### 4.1 会话命令、回执与拒绝

新增的会话命令仍通过现有 PlayerController RPC 到 GameMode，不加另一套网络服务。
公共请求头仅为 `FLDSessionCommandKey { uint32 Generation; uint64 RequestSequence; }`；
发送者来自拥有连接，不能自行填 `ContenderId`、Host 标志、LedgerMatch 或服务器时间。
结构名单版本与一般界面发布版本分开；`SetReady` 是设置布尔值，不是可被重放翻转的 Toggle。

| 拟改造/新增入口 | 额外请求字段 | 接受条件 / 权威结果 |
| --- | --- | --- |
| `ServerSetReady` → `ALDGameMode::SetReady` | `bool Ready; uint64 ExpectedRosterRevision` | Lobby、本连接已准入、结构版本相符；只改变本人 Ready，返回新 Lobby 投影。 |
| `ServerSetAICount` → `SetAICount` | `uint8 Count; uint64 ExpectedRosterRevision` | Lobby、Host、Count 在零至三内；只变房间配置和名单版本，不凭客户端数组生成 AI。 |
| `ServerStartMatch` → `StartMatch` | `uint64 ExpectedRosterRevision` | Host、Lobby、当前全部真人 Ready、容量合法；先 Pending，初始化整体提交才 Committed。 |
| `ServerEndMatch` → `RequestHostEnd` | 无身体字段 | Host、Development/CaptureOpen；排入当前权威边界，无胜者结束。不能由普通客户端、Widget 或 AI 调用调试胜利。 |
| `ServerReturnToLobby` → `ReturnRoomToLobby` | `FGuid ExpectedResultReceipt` | Host、Results、结果身份相符；全域失效及新上下文发布后才完成。 |
| `ServerLeaveRoom` / `Logout` → `RemoveRoomMember` | 用户意图无身体字段；网络断开由引擎入口提供 | 准入的本连接或可信断开回调；两条路径消费同一次成员移除，不伪造 DeathFact。 |
| `ServerQuerySessionReceipt` | 请求头及被查询的 Sequence | 只读本人已处理结果或明确过窗；查询不改变 Ready、阶段或玩法状态。 |

`FLDSessionReceipt` 包含请求键、`Pending / Committed / Rejected`、`ELDSessionReject`、
当前阶段、结构名单版本及必要的结果/新局身份投影。Pending 不是已开始或已返回。
单连接会话控制命令只允许一个在途请求；下一命令须在前一终态后提交，UI 可合并尚未发送
的 Ready 意图。服务器保留最后一个终态请求的规范化条款及回执和 `ProcessedThrough`；
相同键同条款重放，改条款返回 `RequestConflict`，低于水位且回执已过窗返回
`ReceiptExpired` 和当前获准状态，绝不重新执行。该小窗口只约束会话命令，
不替代各玩法域的业务去重或现有请求窗口。

目标拒绝枚举至少区分：
`NotOwner / NotHost / WrongPhase / RoomFull / MatchAlreadyStarted / NotAllReady /
StaleRoster / StaleMatch / StaleLife / RequestConflict / RequestInFlight /
ReceiptExpired / ProtocolMismatch / ProductMismatch / ContentMismatch /
MissingDemoDefinition / SpawnBlocked / MatchClosed / AuthorityFault`。
地址格式、端口范围、网络不可达和旅行失败归 GameInstance 本地连接错误，
不能伪装成服务器已经接受再拒绝的玩法事务。

网络版本、摘要、数组/字符串长度按对应 wire/schema 的有限类型严格解码；
端口为合法非零 16 位端口，人数为 Spec 上限，未知枚举拒绝。
请求 Sequence、名单修订和代次溢出时明确故障/拒绝，不环绕到旧请求的身份。
错误文案映射给 UI/AI，内部日志可以记录完整上下文，但不向对手公开隐藏实体或私有字段。

本地连接使用独立 `ConnectionAttemptSerial`：取消后若服务器已经接纳，必须立即完成
对应离开清理；迟到成功不能重新打开取消的房间，迟到失败不能关闭下一次成功连接。
开始失败/返回 Lobby 的回执可以引用原请求键并携带新 Generation，但只能完成本连接
仍在等待的控制请求，不能以旧 Gameplay 回包覆盖新局状态。

### 4.2 读模型及权限

| 投影 | 生产者 / 允许字段 | 版本与清理 |
| --- | --- | --- |
| `FLDLobbyView` → GameState | GameMode：本局网络 Generation、阶段、RosterRevision、真人/AI 名单、名称、Host、Ready、AI 数量、可开始状态及公开拒绝原因 | 房间公开；Ready 变化有单独发布 revision。离局移除名单项；新局更新代次，旧 OnRep 不能覆盖。 |
| `FLDPublicMatchView` → GameState | GameMode（含喷漆与守护归约）：阶段、服务器时间同步锚、外层时间线 `Timeline`（§3.4，含公开的 PK 报名名单）、开放截止、喷漆区状态、喷漆进度持有者（独自喷漆者或争夺中被暂停的喷漆者）、喷漆段起点/截止、暂停进度、涂鸦主人及其公开身份槽、守护获胜时刻或被冻结的守护进度、结果身份/原因/唯一胜者 | 喷漆、涂鸦、守护和结果全场公开，不包含隐藏坐标、生命表、MP或敌方构筑。客户端只估算显示进度。 |
| `FLDOwnerState` 目标字段 | 本人的 ContenderId、BodyRef/Life、存活/等待/保护、复活及保护截止、已授权的成长/冷却投影 | 只给拥有连接或对应 AI；死亡清理旧身体引用，候选和来源冷却仍来自其持久所有者；换 Match 全部换代。 |
| 观测 DTO | Vision 按当前观察权限发布身体及已批准表现字段；BodyRef 不直接充当永久可用的目标句柄 | 隐藏/死亡/离开/终局撤销；旧句柄不能解析新生命。正常 Host 的原生 Mesh/附属表现与代理一起遵守授权。 |
| 连接错误 / 会话回执 | GameInstance 的当前尝试、拥有者命令回执 | 只回所属本地玩家；取消连接后迟到的旧错误不覆盖新尝试，不清另一个 PIE 世界。 |

“全场公开的喷漆者或涂鸦主人”不自动授予该角色坐标或视野。公共 DTO 只需喷漆区为空、独自喷漆、争夺或主人独守的状态，
争夺中被暂停的喷漆者和其暂停进度，以及当前涂鸦主人和守护进度，
不发布完整区内实体名单来暴露保护者或隐藏身体。
结算已确认公开所有参赛者的名称、击杀、死亡、最终等级，并标明赢家、结束原因和本局用时；
只在终局冻结后公开，不变成局内实时全员属性页，也不按统计决定胜负。
可见对手的名称/等级/HP/MP/状态只通过当前观察权限显示，
技能/CD/其他属性不在白名单；失视野停止追击，不留 LastSeen 单位标记。
死亡可自由拖看静态地图，但不生成新的活体视野源，不借相机获得隐藏单位。

公共结果使用 `FLDResultView { uint32 Generation; FGuid ResultId; uint64 ResultSerial;
ELDMatchEndReason Reason; TOptional<FGuid> Winner; int64 StartedAtMs; int64 EndedAtMs;
int64 DurationMs; TArray<FLDResultRow> Rows; }`，
由内部 ResultReceipt 投影，**不发送 LedgerMatch**。`FLDPublicMatchView` 的正式时间字段
为 `ServerNowMs / OpenAtMs / SoloSinceMs / DueAtMs / PausedProgressMs / GuardDueAtMs / GuardKeptMs`，另带 `PublicRevision`、
`CircleState / Occupier / GraffitiOwner / GraffitiPresentation / CaptureEpoch / Result`。
`CircleState` 为 `NotOpen / Empty / Solo / Contested / Guarded`：`Solo` 是区内恰有一名有效参赛者且他不是涂鸦主人（他在喷漆），
`Contested` 是两名以上，`Guarded` 是区内只有涂鸦主人本人（他不喷漆）。
`SoloSinceMs / DueAtMs` 只在 `Solo` 时有值，续计时 `SoloSinceMs` 为恢复时刻减暂停值，`DueAtMs` 即真实喷上涂鸦的时刻；
争夺中两者为 0，被暂停的喷漆者由 `Occupier` 与 `PausedProgressMs` 表示。
`GraffitiOwner` 是旗杆上涂鸦的主人，`GraffitiPresentation` 是他的开局名单槽位（即身体染色键），无主时分别为空 Guid 与 0；
有主时 `GuardDueAtMs` 只在 `Guarded`（主人独自在区内）下表示运行中守护的真实获胜时刻；`GuardKeptMs` 在 `Empty`（主人在区外、区内无人，守护暂停）与 `Solo / Contested`（区内有主人以外的有效参赛者，守护冻结）时表示保留的守护进度（2026-11-07 用户决定），无主时两者为 0。
没有进度持有者或涂鸦主人的时刻使用明确空值，
不是让零 Guid/默认时间冒充一名参赛者。各字段组合由 `LDCaptureTypes.cpp` 的校验拒绝畸形。名单条目身份、真人/AI、Host、Ready 的字段
由 `FLDLobbyView` 发布；Owner 的时间字段为 `RespawnAtMs / ProtectedUntilMs`。
时间同步锚同时携带 `double ServerSampleWorldSeconds`，来自服务器同一游戏时间采样；
客户端用 GameState 同步的服务器 World 时间相对该锚估算显示，不混用新局零点和世界绝对时间。
Generation/PublicRevision 拒绝旧锚；非活跃阶段按明确阶段停止玩法倒计时估算，
不能由本地估计达到零触发任何服务器动作。

`FLDResultRow` 为 `ContenderId / Name / uint64 Kills / uint64 Deaths / uint8 FinalLevel / uint64 Assists`，
Winner 的 ContenderId 是唯一赢家标记来源。按开局锁定名单顺序冻结所有行，
不按击杀造排名；离局者仍有行，leave 不加 Deaths。
StartedAtMs 来自成功开局（R0 开始，结算用时包含休息），不以 OpenAtMs 推算；OpenAtMs 在最终野外开始前为 `MAX_int64`；DurationMs 由核心实际开始/结束时刻计算，
UI 不从收到结果的本地时刻或 killfeed 重建。

### 4.3 AI 绑定与提交后通知

目标 `FLDAIContenderBinding { FLDMatchContext Match; FGuid ContenderId; }` 是核心签发的
服务器绑定，构造/持有受限，不是 AI 可任填其他参赛者身份的查询凭据。
GameMode 的 `RegisterAIContender(AController& ServerController, uint8 RosterSlot,
const FLDMatchContext&) -> FLDAIRegistrationResult` 只在 Starting 接受已冻结的合法 AI
名额，为 Lobby 已由服务器预留标识的同类参赛记录绑定 Controller；同槽重复调用只能重放同一绑定，
不能多建角色。它不生成额外人数、免费资源或独立生命表。
失败明确返回 WrongPhase/InvalidRosterSlot/AlreadyBound/StaleMatch，不由 AI 补建一个“临时角色”。
真人/AI 按显式 ELDContenderKind 计数，不按 IsPlayerController、NetConnection 是否为空
或 Pawn 数猜测。AI 域的 `ALDOpponentController : ALDPlayerController` 由 OpponentAI 提供者在 Prepare 中按冻结名额生成，
直接复用基类的 StateFor/BuildFor/Holder 路径而不经过 B91/B92；没有 LocalPlayer/UI/网络
连接，不伪造 RPC，也不能落入 NPCBuild。核心注册后才调用其 `BindRegisteredSelf`，
AI 不 friend 直写 Contenders；ReadAISelf 必须验证注册的 adapter 与 Match。

`ReadAISelf(const FLDAIContenderBinding&) const -> FLDAISelfReadResult` 校验该绑定所属的
注册 Controller 和当前 Match，只返回该 AI 的同一 Owner 投影；
公共阶段/圈用 `ReadPublicMatch() const -> FLDPublicMatchView`。
AI 自己的 `ALDOpponentController::BuildFrame` 只是组合这两个只读入口和获准
Observer 的适配，不另加一个 GameMode 查询真相或在 AI 中复制生命状态。
观察权限与目标解析由世界提供的 ObserverContext 接入，不将 GameMode 全表作为 AI 感知。
操作域的服务器 `FLDOrderCaller` 使用 MatchContext+BodyRef，
ContenderId 从 Kind=Contender 的 Victim.EntityId 派生，不另养一套 Match/Life。

生命通知为 `FLDLifeNotice { Match; BodyRef; ELDLifeChange Change;
uint64 LifeRevision; FLDAuthorityEventStamp Stamp; }`；
Death/Respawn/ProtectionEnded/Left 的提交分别推进该参赛实体 LifeRevision。
通知只描述已提交的生命事实，不与实际致死用的 DeathKey 混同。
`NotifyLifeCommitted(const FLDLifeNotice&)` 使 Navigation/目标/AI/表现清理旧身体工作；
旧通知不覆盖新的 LifeRevision，保留中的候选/构筑不因通知被清空。

`NotifyMatchClosed(const FLDMatchClosed&)` 带当前 Context 和不可变 ResultReceipt，
在写闸关闭后通知取消；`NotifyMatchReset(const FLDMatchReset&)` 明确
OldContext/NewContext 和新的 Lobby 修订。通知均由 GameMode 产生，
消费者不能靠回调重新打开闸门。AI 控制器在 MatchReset 回调里只停止全部活动，不当场销毁：
其身体由 `RetireAuthorityCore` 按真人同一路径退役、记录随后丢弃，控制器在下一帧确认 Match 已换代后自毁，
此时 `Logout` 找不到记录，不会走离局路径；它也覆盖 `ClientTravelInternal` 不执行 travel，
以免无连接控制器的 Client RPC 在 Host 进程本地执行。AI 在 Publish 后的获准快照上进行短寿命策略调度，
其结果只提交给下一实际边界的领域意图队列；不在本批后半部回插 Actions，
也不直接获得 `FLDGameplayWritePermit` 改战斗/成长。

## 5. 生命、出生和持久状态

### 5.1 单一生命提交链

```text
Combat 首次 0 HP 锁存 PendingDeath
  -> 同一 FLDCommittedDeath / DeathKey
  -> GameMode.EndContenderLife（生命和资格只消费一次）
  -> 成长域复合奖励（也记录零奖励处理）
  -> DeadWaiting
  -> World 固定十点抽取 + 同点 Reserve
  -> 核心/Combat 成功提交新 Life 和 BodyIdentity
  -> World Finalize + Owner/Vision 新生命投影
  -> AliveProtected
  -> 保护到期或按获准规则主动解除
  -> Alive
```

`FLDCommittedDeath` 使用上述 DeathKey、VictimBody、可选的 VictimContender、
FatalEffectId、明确的实际 CreditOwner 或 NoOwner、释放来源引用、
SourceBodyAtRelease、不可变 RewardBasis、DeathCause 和共同 EventStamp。
RewardBasis 由 Combat 定义，包含 RewardDefinitionId，以及参赛者分支的
VictimLevelAtDeath/VictimBuildRevisionAtDeath，另固定
`bool CreditOwnerEligibleAtDeath` 和明确的 NoRewardReason；野怪分支使用相应营地奖励定义，
不伪造一个玩家等级。第一次 PendingDeath 就锁定全部基准；**当前 Effects 致死集合**
中的快照全部完成后，才允许该集合派奖改变任一参赛者等级。
后续 Transactions 触发的因果 Effects 子批形成自己的新致死集合，使用其实际发生时
已经提交的等级，各自在派奖前锁定；不要求预知整个 Boundary 未来尚未发生的死亡。
Growth 不得在派奖时重读受害当前/复活后的 Build，
这些基准也不加入 DeathKey 来扩大去重域。
来源/效果类型及其内容由 Combat 定义，生命域不得用“最近打过它的人”补全缺失所有者。
零 HP 首次锁存后本生命不再接受治疗起死；重复命中不再制造第二事实。

`EndContenderLife(const FLDCommittedDeath&, const FLDGameplayWritePermit&)`
只接收当前上下文已提交的 Combat 事实，返回同一 `FLDLifeReceipt` 或明确拒绝。
生命记录切换到 `DeadWaiting`，资格立即无效，取消适用的旧身体动作，
设置复活截止；不直接改 XP，也不把旧 DeathCost 或掉落调用带进来。
`DeathSerial` 如仍有显示/审计用途，只是派生计数，不参与决定能否再次死亡或发奖。

死亡所有者仍在本局时，独立已释放效果继续至原结束，合法击杀仍可收奖，即使双方同批死亡。
CreditOwnerEligibleAtDeath 在首次锁存时按核心实际时间/因果序认证，死亡不取消在局资格；
Growth 只消费这一冻结资格和关系，不在重试时用更晚的 CurrentInMatch=false 撤销既成奖励。
无主、自杀、离开清理、营地回位不发奖；离局先发生则取消未结效果，不再新产致死。
致死先发生则后来的离局不改 Owner/DeathKey/RewardBasis，也不回滚已经应用的伤害。
因此离局后保留必要的 settlement-only 参赛/奖励接收记录直到此前事实处理完成：
只处理已锁事实，不创建身体、被动或新动作，不恢复在局资格。
真正终局闸关闭后仍不补付；必需支付无法完成时走明确 Unpaid/Fault 路径。
XP、跨级升级和新增机会先全量预检/预留再一起提交，不能先成功改 XP 再调用可能失败
的机会/候选分支。必需准备失败时事实保持待处理并阻断该权威边界，显式报错，
不能漏发半份奖励后继续宣布守护成功。
只有明确 `RetryableNotCommitted` 的准备失败可释放原 permit 和预留后，
进行一次受控回收/重新准备并取得新许可；不能重放已经应用过一部分的提交。
再次失败、非法定义或溢出直接调用 §6.6 的 FailAuthorityBoundary，
结束等待并明确未支付；不永久 Pending，不伪造成合法 NoReward。

### 5.2 抽点、落地和新生命提交

本域调用世界的 `DrawSpawnAnchor`、`ReserveSpawnPlacement`、`FinalizeSpawnPlacement`
职责，具体参数容器以 `WORLD_CAMPS_AND_VISION.md` 为准；共同身份只使用 §4。
请求包含当前 Match、参赛者和拟议新 Life 及稳定 SpawnRequest 身份。

抽样只在新的出生/复活请求上发生一次，输入是固定的完整十点集合；没有敌人距离、视野、
危险权重或排除上次落点。重复请求和同点暂时被占用都复用已经选定的 SpawnId。
世界在该点局部预留合法落地；失败返回 `SpawnBlocked` 或明确几何错误，
不偷偷抽另一个点，不将失败位置当成成功。

只有参数/当前 Match/预期死亡生命、合法落地、身体和派生属性准备都成功后，核心才提交
新 `Life`、新 BodyIdentity、完整 HP/MP 和保护截止，再让世界完成该预留。
失败不推进 Life，不消耗第二次抽样，不留下半复活身体；清理尚未提交的预留不发任何奖励。
同一生命重绑、Actor 池重用或 Pawn 创建重试不能重复初始化局内成长。
Respawned 资格事件原子携带新生命的最终 Alive+保护期限；不得先发布无保护 Alive，
再另发 ProtectionStarted，制造保护者落入喷漆区瞬间争夺。位置事件若先被归约，
未激活的新 Body 仍不具资格，必须等这一原子生命提交。

这里的正常失败必须前置到 Reserve/准备阶段：Finalize 只完成已经预留的提交，
不得再进行可能失败的分配、换点或碰撞挑选。若提交后的内部不变式破坏，显式进入
AuthorityFault，禁止发布复活成功或靠回退 Life/再抽点掩盖；它不是可静默重试的普通落地失败。

初次出生走同一抽点/落地机制，但只进行开局初始化，不借用死亡扣费、返还奖励或刷新旧来源
的路径。Spec 只规定复活保护，不能因为调用了共用 Spawn helper 就无意附加开局无敌政策。
对手 AI 同样抽样并走同一生命规则；野怪使用其营地出生机制，不参加玩家十点抽样。

### 5.3 进攻意图、保护解除和动作边界

`AliveProtected` 和一般战斗 Guard/护盾不是同一状态。免伤免控、可移动、不计入喷漆区人数
是生命域资格；Combat 读取同一状态，不另存一个可能漂移的“是否无敌”开关。
保护结束必须产生一次资格事件，喷漆计时从恢复资格后的真实时间开始，不追溯保护期间。

进攻意图先通过外层本人/注册身份、当前 Match/Body/Life、Alive、写闸/permit、
原请求新鲜性及取消水位、合法包形状和精确条款重放检查。
之后只要是可识别的 AttackUnit、AttackMove、普攻或角色主动意图，
**立即提交保护解除及喷漆区资格事件，再验证路径/目标/来源/观察/距离/蓝量/冷却/控制等可执行性**。
不能等追到目标、成功 release 或命中才解除。

合法但语义不可执行的 EmptySource、过期 binding、显式 Missing 目标、超距/无路、
冷却/蓝量/控制失败仍解除保护；真正畸形包、未知命令编码、旧 Match/Life、旧 Epoch、
重放/冲突请求和已关闭闸门不改变当前生命。
本地 Armed 预览、普通 Move、Stop/取消不构成此入口的进攻意图；
客户端预测不可用也不能吞掉已经确认发送的合法进攻意图。

唯一入口归 GameMode：

```cpp
FLDProtectionReceipt EndProtectionForAggressiveIntent(
    AController& Caller,
    const FLDMatchContext& Match,
    const FLDCombatBodyRef& Body,
    const FLDAggressiveIntent& Intent,
    const FLDGameplayWritePermit& Permit);
```

`FLDAggressiveIntent { ELDAggressiveIntentKind Kind; FLDAggressionRequestKey Request; }`；
Kind 只含 `AttackUnit / AttackMove / BasicAttack / ActivateCharacter`。
Request 为 `Origin(Navigation/Combat) / uint64 Sequence / uint32 CancelEpoch`：
Sequence 在 Navigation 复用 Order，在 Combat 使用收件 `DirectSequence`，
不是语义准入成功后才分配的 authority CombatToken；Navigation 使用原 Epoch，
Combat 无适用 Epoch 时为零，不新发另一份失保动作序。
派生 RepeatAttack 沿原导航意图，不伪造新玩家意图。
各域保留自身精确条款/重放水位，调用此入口前已完成外层验证；
核心再次核对 Caller 注册、当前身体/阶段和许可。

`FLDProtectionReceipt` 包含 Generation、Body、原 Request、Outcome、ProtectionRemoved、
`uint64 LifeRevision`、可选的本次 Life EventStamp 和 Reason。
Life 自己从当前 Actions 边界生成 Stamp，不接收调用者伪造的时间；
只有保护状态真正变化才发布一次资格事件并推进 LifeRevision。
动作后续可能 Rejected，但必须携带已经提交的 ProtectionRemoved/LifeRevision，
资源和冷却不变，也不替换未成功接受的旧订单。重复回包只重放该结果，不再解除一次。

### 5.4 取消、死亡、离局、终局和新局矩阵

| 状态 / 工作 | S、Esc、面板关闭等局部取消 | 死亡 / 复活 | 普通离局 | 终局 | 新局 |
| --- | --- | --- | --- | --- | --- |
| 导航、追击、可中断动作、预选目标 | 按操作域边界取消，推进相应 Epoch；不重启旧确认 | 取消旧 Body/Life 的订单和未释放动作，新生命无继承订单 | 撤销该连接所有命令权，不转 AI | 关闸后取消，迟到回调只作拒绝/清理 | 新 Match；旧 saved move、order、target 和 token 拒绝 |
| 已释放效果 | 不把关UI当回滚/退款 | 成功释放的独立效果继续原期限，保留所有者，旧目标Life不伤新生命；未完成release取消 | 立即取消未结束独立效果；不回滚既成命中/死亡、不产生离开奖励 | 不再首次结算命中、触发、奖励 | 清旧效果及执行权 |
| 等级、XP、技能、永久加成 | 不变 | 全部保留 | 撤销玩法权限；仅为已锁事实保留settlement-only接收，不能接管/新发动作 | 冻结；已提交事务只读重放 | 按批准初始输入全新建立 |
| 选择机会和已生成三张候选 | 关闭只隐藏，不重抽、不自动选 | 保留并可确认；重验当前Life+Phase，只改持久构筑，不治尸体/提前复活 | 清理未决确认，不转给新连接 | 拒绝新确认；不消耗机会 | 全部清空，旧 Offer/确认不能操作新局 |
| 来源冷却 | 不退款、不清零 | 同一来源 readiness 随服务器时间继续；不放在易被销毁的 Pawn 独占存储 | 不再可提交动作；失源规则由内容/战斗明确 | 不再释放；显示冻结/结束由结果 UI 决定 | 新时间域；不继承旧锁，也不接受旧回执 |
| HP/MP、临时控制、保护 | 按真实动作结果，不给UI无敌 | 死亡不战斗；复活满HP/MP清旧控制、原子建立保护 | 立即移除身体及可攻击/阻路碰撞，不计战斗/喷漆区 | 不再治疗、伤害、控制或复活 | 全新身体和生命边界 |
| 喷漆进度、涂鸦与守护 | 单纯停止/面板/战斗不清零 | 死亡立即清零喷漆进度，涂鸦主人死亡即清除涂鸦；硬控在施加瞬间清零喷漆进度；保护既不喷也不争 | 离局事件清零喷漆进度并清除其涂鸦，不按人数直接胜利 | 一次结果后不再推进 | 新局从开局休息 R0 重新开始，旧进度/涂鸦/结果不能复用 |
| 观察句柄、代理、私有回执 | 取消选择不改变服务器可见集合 | 旧生命观测撤销；自身持久信息保留 | 关闭所属通道，撤销旧目标授权 | 清理实时世界更新，保留获准结果 | 代次全换；旧投影/ACK 不覆盖 |
| 本地名称、按键、画质、音量、镜头偏好 | 按本地设置保存 | 保留 | 保留 | 保留 | 保留；不夹带任何局内战力 |

普通离局本身不制造 Combat 死亡事实；移除身体/取消未结束效果不能倒改更早的事实，
也不能通过销毁 Controller 抹掉已认证的奖励接收者。已离局的旧身体和新请求立即失效，
历史已接受前缀和 settlement-only 记录是结清既成事实，不是重连或恢复玩法。
Host 离开不走“其他人继续”的普通离局分支。

## 6. 实际时间、运动证据和唯一批次

### 6.1 共同时间与提供者类型

采用一份服务器单调比赛时间。GameMode 的窄时序状态给现有 Combat
`AdvanceAuthority` 调度和原生运动适配提供步长；不再允许临时回拨 World 时间
改变其他生产者所见的“现在”。UI、AI 和客户端只读同步投影，不能推进此时钟。

```cpp
enum class ELDProviderId : uint8
{
    Session, Motion, Combat, Life, World, Progression
};

struct FLDAuthorityStep
{
    FLDMatchContext Match;
    uint64 StepSerial;
    double StartServerSeconds;
    double EndServerSeconds;
};

struct FLDAuthorityEventStamp
{
    uint32 Generation;
    ELDProviderId Provider;
    FGuid StreamId;
    uint64 ProducerSequence;
    double OccurredAtSeconds;
    int64 OccurredAtMs;
    int64 RecordedAtMs;
    uint64 CommitOrdinal;
};

struct FLDProviderFence
{
    uint32 Generation;
    ELDProviderId Provider;
    FGuid StreamId;
    int64 ThroughMs;
    uint64 ThroughSequence;
};

struct FLDAuthorityBoundary
{
    FLDMatchContext Match;
    uint64 BatchSerial;
    int64 AtMs;
};
```

`GetAuthorityStep()` 的秒是同一比赛时钟的高精度表示，供实际原生子段几何使用，
不是操作域自己维护的第二时钟。`GetAuthorityNowMs()` 返回同一时域的非回拨时间，
不读为旧 Sample 临时改写的 World 时间，也不接受客户端发送时间。
可用性使用的 NowMs 必须是当前实际游戏时间的 checked floor（或已经真实到达的整数边界），
不能用事件的 ceil 桶提前开放冷却/攻击/保护/复活期限；
`GetAuthorityNowSeconds()` 提供同一时刻的原始秒，不是另一份时钟。
这里的 Actual 是服务器实际执行的**游戏/模拟时间**，不是新增的墙钟玩法；
同世界换局只换比赛时间原点，不把 `UWorld::TimeSeconds` 清零。已是整数毫秒的
期限直接参与比较/安全加法，不能经过 double 往返后再 ceil 成下一毫秒。

内容的 `*Sec` 是**时长**，生成边界要求可精确投影成有界整数毫秒；
非法、非有限、越界或不能满足所需精度的输入明确拒绝，不能运行时悄悄取整成另一数值。
真实运动的秒级时刻保留在 OccurredAtSeconds；唯一事件分桶转换
`AuthoritySecondsToEventMs` 使用检查溢出的向上取整。这样实际在截止之后的事件
不会因向下取整而倒灌到截止之前。相同毫秒内仍保留全部事件及真实子段顺序，
不将它们压成一个最终 Inside 值。显示转换只除以单位，不参与裁定。
**跨流先比较未取整 OccurredAtSeconds，真正同刻才比较因果、阶段和 CommitOrdinal**；
OccurredAtMs 是边界分桶，不足以替代真实先后。
定时器/准确原生端点的整数边界标签由核心保留，对应秒只是同一时刻的表示，
不能经浮点往返重新归桶；字段由核心时间构造入口一起生成，不允许分别任填。

| 字段 | 必须解释的含义 |
| --- | --- |
| `OccurredAtSeconds` | 同一非回拨游戏时域中可跨提供者比较的原始实际发生时间；亚毫秒顺序也必须保留，非第二套时钟。 |
| `OccurredAtMs` | 实际已提交的运动/资格/命中所处的比赛时刻；非客户端愿望时间。定时器只能在正确的权威边界真实执行后产生事实。 |
| `RecordedAtMs` | 处理/收录该事实时的同一单调时钟，不能早于发生时刻；补算延迟必须可诊断。 |
| 效果 `DueAtMs` | Combat 拥有的最早到期时间，**不等于已经发生**。迟处理不能简单用 DueAtMs 覆盖真实发生时间。 |
| `ProducerSequence` | GameMode 注册的单个提供者流内单调序号；零序号用于尚无事件的初始水位，不作为事件。 |
| `CommitOrdinal` | 核心在固定阶段内分配的稳定实际提交序，不由客户端或 Actor 地址决定；同毫秒的因果顺序必须保留。 |
| `ThroughMs / ThroughSequence` | 该流确认所有发生时刻不晚于核心选定的整数边界 ThroughMs 的事件都已发布，且指定连续序号前缀无空洞；包括无事件的时间段。水位不使用事件的 ceil 转换。 |
| `AtMs / BatchSerial` | 此次正在关闭的权威时间边界及批次；不是允许任意旧请求“以这个时间重试”的凭据。 |

RecordedAtMs 是记录时刻的 ceil **审计桶**，不从用于可用性的 floor NowMs 直接复制；
比较真实先后仍使用 OccurredAtSeconds。新 ReadyAt/NextAttackAt 等截止由原始实际
release 起点生成，按内容公式给出的毫秒时长安全相加/上取整最终截止，不能先 floor 起点
而提前可用。整数截止与 raw Now 或 safe-floor Now 比较；例如实际只到 1000.2ms，
绝不能因为 ceil 桶为 1001 就使 1001ms 的能力可用。这些转换都在同一核心时间边界。

### 6.2 原生运动的真实生产点

操作域以 `FLDNativeMotionBatch`（`LDNativeMotionTypes.h`，按 §4 的 `FLDCombatBodyRef` 归属）发布原生运动，
契约字段包括 `ProviderStreamId / SegmentSequence / WorldRevision / FromFootCm / ToFootCm /
StartServerSeconds / EndServerSeconds / MotionKind`。所有秒字段来自核心 Step。
`MotionKind` 区分实际自愿移动、root motion、击退/位移和落地；不连续落地不沿一条
虚构的瞬移连线制造圈内穿越。具体几何查询只归世界域。

`SegmentSequence` 是实际原生段序，`Stamp.ProducerSequence` 是发布圈事件的序号，
二者不能混用：一段从圈外穿圈再到圈外，会生成入/出两个事件，事件序必须分别递增，
否则按段序去重会丢掉退出。事件保留原段序及 `CrossingOrdinal` 作出处；
CompleteBodyMotion 的 LastSegmentSequence 指原生段，Fence 的 ThroughSequence 指
事件前缀。两套序号属于同一 Body Motion 流的证据和交付层，不新增 force/self 两条流。

CharacterMovement 的 `RecordAuthoritySegment` 只记录**实际成功提交**的子段，
保留滑移、绕角和子步；`FScopedMovementUpdate` 回滚的试探段必须丢弃。
客户端 correction、预测预览、寻路规划、`OnMovementUpdated` 的首末快照都不能
代替服务器的真实分段。Combat 造成的位移仍通过同一运动生产路径，不另做积分。

世界的 `ExtractCircleCrossings` 根据这些已接受子段和唯一圈几何输出按实际时间排序的
中间进出；`QueryCircleMembership` 使用合法落地脚点的平面中心与
`CAPTURE.RadiusCm`，边界包含关系由世界单点定义。喷漆归约器不重算另一套胶囊半径规则，
也不根据可见集合筛人。

身体支持面只从权威 CMC 当前真实有效的 `CurrentFloor` 映射到注册 SurfaceId，
不能用历史缓存、瞄准点或空 SurfaceHint 向下投影来补接地。
已确认的 `NotGrounded` 是几何占用的确定否定，不是缺少证据：
Motion 发布 `PublishCapturePresence(..., Inside=false)` 移出有效集合；
重新取得合法支持面时重新查询并发布真实占用变化，即使 XY 没变也不能漏报。
这些是实际几何/支持面变化，不是新增“控制状态本身清零”规则，也不改变 Alive/Life。
同一原始时刻、事件序和原生 scope 提交约束仍适用，回滚的试探 floor 变化不作事实。
目标的 InvalidSurface/WrongSurface 语义拒绝与身体占用分开，不能用无效瞄准目标
推断本人离圈；可识别进攻仍按 §5.3 先解除保护。

操作域在初次原生推进结束后只调用 `CompleteBodyMotion` 交付基础位置证据；
本批命中仍可能造成位移，因此这不是最终 Motion Fence。全部子段、后续位移和圈查询
完成且结果已发布，核心进入封闭阶段后才调用 `PublishThrough` 推进该 Body 的 Motion 流；
已处理的 NotGrounded 可以完成边界，不能让确定的无占用状态冻结全局水位。
静止身体也封空流；真正缺数据、查询未完成或系统错误不能伪造成功结果或推进缺证据的水位。
出生/复活提交时为新 Body 注册流并提供初始合法位置及资格；死亡/离局时旧流以明确的
最终事件/终止水位退休，不能简单从等待集合删除而遗漏最后一次离圈/死亡。

只保留尚未消费边界所需的原生分段和紧凑圈事件，不做永久日志或网络回滚世界。
Combat 在一个已记录时间边界读取位置时只能使用该真实子段的几何证据；
禁止用当前 Actor 姿态外推过去。没有覆盖所需时刻的运动证据时明确返回
`GeometryUnavailable` 并阻断边界，不能默认 Miss 或当作无人争夺。

### 6.3 发布和水位接口不是通用事件平台

以下入口归 `ALDGameMode`，由当前已注册服务调用；不是 RPC，也不开放给 UI。
类型是有界的单圈/生命周期交接，不能向其中添加任意脚本回调或其他业务事件。

| 目标函数 | 提供者 / 契约 |
| --- | --- |
| `GetMatchContext() const -> const FLDMatchContext&` | 读取唯一上下文；不能独立生成 LedgerMatch。 |
| `BeginAuthorityStep(const FLDMatchContext&, int64 EndAtMs) -> FLDStepPermitResult` | 仅Combat driver调用，发放以EndAtMs为上界的原生区间；driver必须在实际事件子时刻停住/结算再续未执行部分，不能把上界当全部事实发生时刻。 |
| `GetAuthorityStep() const -> const FLDAuthorityStep&` | 原生运动读取单调 step 的实际时间区间。 |
| `ValidateMotionStep(const FLDAuthorityStep&, const FLDCombatBodyRef&) -> ELDCommitReject` | 检查当前上下文、StepSerial、该身体注册和未关闭边界；远程 packed/saved moves 不能绕开。 |
| `CompleteBodyMotion(const FLDAuthorityStep&, const FLDCombatBodyRef&, uint64 LastSegmentSequence) -> FLDMotionReceipt` | 收集该 Body 的基础运动完成证明；不是最终 Fence，允许本批效果通过同一流再提交实际位移。 |
| `GetAuthorityNowSeconds() const -> double` | 同一时域的原始实际服务器游戏时间，供精确顺序/起点使用。 |
| `GetAuthorityNowMs() const -> int64` | 当前服务器时间的 checked floor/已到达整数边界，绝不回拨，也不把ceil桶当成已到期。 |
| `PublishCapturePresence(const FLDCapturePresenceEvent&) -> FLDEventReceipt` | Motion 发布 Body、Stamp、Inside 的实际变更；世界只提供计算结果。 |
| `PublishCaptureEligibility(const FLDCaptureEligibilityEvent&) -> FLDEventReceipt` | 仅 Session/Life 发布 Joined、Left、Died、Respawned、ProtectionEnded、PhaseRetired（阶段边界退休，§3.4）之一；Joined 可带保护截止（返场）；Respawned原子含保护期限，Combat经生命入口触发，不重复发死亡或中间无保护Alive。 |
| `PublishCaptureInterrupt(const FLDGameplayWritePermit&, const FLDCombatBodyRef&) -> ELDCommitReject` | 仅 Combat 在 Actions/Effects 许可内、对存活参赛者身体提交定身、眩晕、嘲讽或击退时调用（`ALDCombatDirector::InterruptSpray`），事实时间为该边界 AtMs；清零其喷漆进度。缴械、减速和伤害不调用。 |
| `PublishThrough(const FLDProviderFence&) -> FLDFenceReceipt` | 事件先于水位；检查注册流、当前 Match、连续序列和单调时间。重复完全相同水位可幂等，矛盾/退回/越过未发布事件拒绝。 |
| `AckConsumed(const FLDAuthorityBoundary&)` | 只有 Combat 的边界位置读取和 GameMode 喷漆与守护归约均完成后通知提供者释放已消费前缀；保留下一段所需末端锚点。 |
| `EndContenderLife(const FLDCommittedDeath&, const FLDGameplayWritePermit&) -> FLDLifeReceipt` | 消费一次 Combat 死亡事实，提交生命/资格和复活等待；不在此改 XP。 |
| `AdvanceTimeline(Permit)` / `LockTimelineBuild(Permit)` | 仅 driver 在 RespawnsAndWorld 开始 / Transactions 末尾调用；应用到期的休息与野外转换及构筑锁定（§3.4），失败即边界故障。`EnterRestWorld/ExitRestWorld` 是阶段边界的世界清理与冻结/解冻，其他世界拥有者在此接入。 |
| `EndProtectionForAggressiveIntent(Caller, Match, Body, Intent, Permit) -> FLDProtectionReceipt` | 完整签名见 §5.3；Navigation/Combat 共享，识别新进攻意图即解保护，不等待语义动作成功。 |
| `IsGameplayWriteOpen(const FLDMatchContext&) const -> bool` | 只读快速检查，不是足以绕过正式提交许可的证明。 |
| `ValidateWritePermit(const FLDGameplayWritePermit&, ELDCommitPhase) -> ELDCommitReject` | 最终提交点核对当前 Match、批次、阶段、许可代次和闸门。 |
| `TryCloseMatch(const FLDResultProposal&, const FLDAuthorityBoundary&) -> FLDResultReceipt` | 唯一结果 CAS 语义；守护提案须带当前 CaptureEpoch、涂鸦主人及完整边界证明，HostEnd 须来自已验证权限。 |
| `FailAuthorityBoundary(const FLDAuthorityBoundary&, const FLDAuthorityFault&) -> FLDFaultReceipt` | 必需工作无法完成时锁故障和写闸，明确未支付/未完成；不是正常 Result 或成功 Fence，不补写旧局。 |

`FLDCapturePresenceEvent` 和 `FLDCaptureEligibilityEvent` 都包含同一 BodyRef/Stamp；
后者只允许上表的有限资格变更，不允许调用者自由覆盖整份 Alive/InMatch 状态。
`FLDEventReceipt / FLDFenceReceipt` 明确区分 Accepted、Duplicate、StaleMatch、
StaleLife、UnknownStream、SequenceGap、TimeRegression、AlreadyClosed 和故障；
拒绝不能制造“已发布到此”的成功形状。

提供者注册集合由当前 Match 和身体注册决定：Session、Combat、Life、World、
Progression 各一个服务流，Motion 按当前身体各一个流。没有工作的服务仍封闭空批次。
World/Progression 水位分别证明本边界营地生命/刷新及致死复合奖励/事务已经处理，
不是让喷漆归约读取它们的私有业务表。
同一 Body 的强制位移和自愿运动不能各发一个互不知情的 Motion 完成水位。

核心只关闭所有相关流已覆盖的边界。只有 ThroughMs 足够而序号缺失仍不能通过；
未来事件已入队也不能让当前缺口自动补齐。
边界内部明确区分 `Collecting / Sealing / Closed`：Sealing 前排空本批全部合法因果工作，
最终 Fence 只在 Sealing 接受；Closed 后不存在可以再提交发生时刻不晚于该边界的入口。
仅因当前时钟读数也是某毫秒，不能证明该毫秒桶已闭合。
最终 ThroughMs 只能取核心已经完成的**精确整数毫秒边界** `Boundary.AtMs`；
原生覆盖必须实际达到该边界，不能 ceil 任意小数秒完成点。比如只覆盖到 1.0002 秒，
不能发布 ThroughMs=1001，因为 1.0008 秒发生的事件仍会归入 1001 桶。
退休流的最后事件也必须等其所属桶真实闭合后才完成退休，不能提前移出 required 集合。
封闭后才收到的意图等下一次真实获准 step 执行，不能通过 `last + 1` 伪造水位，
也不能把迟收命令倒填已封闭的边界。
旧 Body 的事件只要此前合法接受且仍处于未消费前缀，就必须按其实际生命区间归约；
不能因阶段 5 已经提交新 Life 而丢掉该前缀中的旧生命争夺/死亡。
退休后新到的旧 Body 请求则明确拒旧；两种情况不能共用一个“不是当前 Life 就删除”的过滤器。
消费者完成后回收前缀，保留序列拒旧水位和退休生命信息，避免无限对局永久保留全部表现事件。
积压达到实现容量时背压并显式停止该边界；不能丢掉中间进出、伪造水位或静默继续游戏。
退休信息按稳定 Victim.EntityId 压缩为 `RetiredThroughLife` 及当前/仍未消费流，
而不是每次复活永久追加一个 StreamId 墓碑。LifeReceipt 只保留最近终态和处理水位；
过窗返回 ReceiptExpired 与获准当前状态，旧 Life 仍由水位永久拒绝，不能再造死亡/奖励。
待处理 DeathFact 不得被压缩掉，领域业务回执按成长域自己的保留规则处理，
核心不复制另一份无限账史。长期循环验收须证明退休缓存不随累计死亡次数线性增长。

**当前实现的落地形式**（同步驱动，不另设事件平台）：圈的 Motion 事实不由运动提供者自行求交，
而是核心在 `CompleteNativeMotion` 接受批次后经 `PublishCaptureMotion` 交给 `LDCaptureCircle::Extract`
求出带原始秒的进出；生命侧事实经 `PublishCaptureEligibility(Permit, FLDCaptureEligibilityEvent)` 提交，
硬控事实经 `PublishCaptureInterrupt(Permit, Body)` 提交，
Left 只由核心离局路径产生。Motion 流的 Fence 等价物是 Capture 阶段的逐身体本步证据检查
（本步新登记的生命除外）；`ExecuteCapturePhase / ExecuteClosePhase / ExecuteCapturePublish`
分别承担归约、先锁闸后冻结结果、无 permit 清理发布，`TryCloseMatch` 为其内部唯一结果入口。
提供者 MatchClosed/MatchReset/Prepare 钩子与正常开局门禁由 `LDMatchProviders` 静态注册；
关闭先停本人视图再释放身体与资源记录：世界拆除（`bIsTearingDown`，引擎在网络驱动销毁远端 Pawn 前置位）、
Host 关房（`Closing`）与 `RetireMatch`（`Resetting`）期间 `SyncOwner` 不发布、也不判投影故障，
进入新 Lobby 后按新代重新发布；其余阶段照常做完整投影校验。
详见[架构与网络](../systems/ARCHITECTURE_AND_NETWORKING.md)。

### 6.4 固定批次顺序及提交许可

现有 `ALDCombatDirector::AdvanceAuthority / Sample` 收敛为唯一驱动入口，
由 GameMode 核心状态规定下面的顺序；领域不能通过独立 Actor Tick 越过阶段提交。
批次按最早尚未关闭的时间边界前进，必须包括已知旗杆开放、保护/复活、效果到期、
喷漆完成、守护完成及营地到期边界。不能先提交截止之后的奖励/伤害，再回头检查较早的胜利。

目标签名为 `AdvanceAuthority(const FLDMatchContext&, int64 ThroughMs)` 和
`SampleBoundary(const FLDAuthorityBoundary&)`。主路线是**按边界推进原生 step**：
到期控制/击退影响未来运动时先在对应边界提交效果，再让 CMC 推进后续时间；
完整留存的子段只能用作真实位置证据，不能补造本来没有执行过的受控运动。
现有 `TG_PostPhysics` 或给 Actor 增加 Tick prerequisite 本身不足以证明这个顺序。
必须同时接住远程 MoveAutonomous/packed moves、PerformMovement、root motion、
强制位移和落地，证明没有“原生 Tick 一次、手动 driver 再一次”的双重积分。
这些是后续 UE 5.8.4 实现/原生验证门槛，不是本轮已经验证的引擎能力。

AtMs 是整数封桶/消费上界，不是把桶内全部事实改成同一实际时间。
同一 Step 票据内的实际子段只执行一次；driver 在原始发生时刻有序停住并处理，
再推进尚未执行的后续部分，取得该点的真实运动证据后按该真实时刻的阶段0–6提交。
不能先把整桶较晚的 Session 离局处理完，再处理整桶较早的 Effects 致死；
已经提交的死亡/命中也不能因后来移除身体而被抹掉。
最终真实到达整数上界且全部提供者工作完成，才执行 Sealing/Capture。
真正同刻的新工作按阶段/因果顺序分配 CommitOrdinal；消费者比较原始时间之后
只按已分配的 Ordinal，不再按 Provider/Phase 把已提交事实重新排序。
当前原始时刻来自 GetAuthorityNowSeconds，不拿 Step.End/Boundary.AtMs 提前给事件打戳。

`FLDGameplayWritePermit { FLDAuthorityBoundary Boundary; ELDCommitPhase Phase;
uint64 GateRevision; }` 只由核心为当前调用栈发放，不发给网络、不跨回调保存。
它不是一把永久可用的锁；每个领域在**最终提交点**重新验证，结束/换局推进 GateRevision，
过期 permit 即使先前通过 bool 检查也不能使用。
这些是只读语义字段：实现须限制构造并禁止复制/持久保存，采用核心发放的借用提交作用域。
同域重入请求排队，不能嵌套开始另一份事务；不是把三个可自由填写的字段当成安全许可。

| 顺序 / `ELDCommitPhase` | 本边界完成的工作 | 关键禁止项 |
| --- | --- | --- |
| 0 `Session` | 处理当前真实子时刻的离开/准入失效/HostEnd；保护到期和旗杆开放条件在此刻动作前生效 | 不把桶内较晚离局提前，不回滚已提交事实；不用客户端拔线时间。HostEnd先锁则其后玩法不执行，既有结果不覆盖。 |
| 1 `Motion` | 排空原生实际运动/位移证据，取得本边界合法位置和全部圈穿越；处理此前阶段安排的位移 | 不将帧末位置当成整个时间段的位置；未有证据就不能进入需要它的命中/选择。 |
| 2 `Actions` | 先外层身份/shape/请求/permit，再识别进攻意图并经Life解除保护；之后动作可执行性准入、可到期release和成功消耗/CD | 语义失败仍可有ProtectionRemoved；旧/畸形包不解，失败不付资源；不能等有效目标/成功release才解。 |
| 3 `Effects` | 先按同一边界姿态收集到期命中，再提交伤害/控制和首次零 HP 锁存；位移通过原生运动路径补充实际事件 | 不因处理第一个死者而抹去另一份同刻已到期命中；不跳过必需工作；普通非致命伤害、减速、缴械不清喷漆进度，定身/眩晕/嘲讽/击退在提交时经 `PublishCaptureInterrupt` 清零被控者的喷漆进度。 |
| 4 `DeathsAndRewards` | 当前Effects致死集合全锁RewardBasis后，消费各DeathKey生命失效并完成该集合复合奖励或明确零奖 | 同集合互杀不因先派奖改变另一死亡基准；必需准备失败不封水位，不越过它宣布胜利。 |
| 5 `RespawnsAndWorld` | 先推进外层时间线（休息进出与阶段生命，§3.4），再到期复活/新身体保护；只在野外段运行时推进营地实际新生命与刷新、外卖与上热搜；为新身体建立位置/资格，退休旧流 | 死亡/离局/闸关闭重查；失败不安全重抽、不产生半个生命或离开奖励。 |
| 6 `Transactions` | 当前选择/替换；按当前生命/阶段/版本重查，一次原子提交派生属性与来源；之后执行到期的休息构筑锁定（§3.4） | 死亡后不能使用阶段前的快照提交；旧生命确认不能自动作用复活身体。 |
| 7 `Capture` | 确认所有提供者前缀及本批工作闭合，依真实发生次序归约全部中间事件，重查完成资格 | 不只看最终人数；不能略过先进入后死亡的争夺事实。 |
| 8 `Close` | 先以唯一提案锁结果、关闭全域写闸、推进许可代次 | 禁止先调用取消服务再设置 Finished；已锁结果不再覆盖。 |
| 9 `Publish` | 清理/取消、最终授权投影、回执及结果；确认消费并释放证据 | 清理不得提交伤害/奖励/选择，不把只读回执重放当作新提交。 |

阶段 0 的保护到期使用半开保护区间：到期时刻本身已经不受保护，
因此同刻来袭伤害可以命中，同刻喷漆区内保护到期可以形成争夺。
新复活的保护从**成功生命提交**时开始，不能由客户端倒计时或排队预约时刻提前计入。

同一毫秒的先后仍先按原始发生时间，再按真正同刻的因果/CommitOrdinal 保留。
例如保护者在 1000.8ms 已离圈、1001ms 才保护到期，两事件虽同在 1001 桶，
仍必须先离圈再解除保护，不能因 Session 排在 Motion 前就虚构短暂争夺。
又如运动中有效竞争者先进入、
本批后被击杀，这次争夺必须纳入（暂停喷漆者进度）；若其死亡在更早事实中已经提交，后面的尸体位置
不再计入。后于完成时刻的事件不能回填过去；迟到网络命令只在实际服务器接受时间排队。

在阶段 6 更新来源/属性所需的同步派生变更仍须在本批内完成并更新相应资格事件；
不允许直接从事务回调偷偷结算 Combat。若获准内容确实产生本时刻的新增战斗工作，
核心必须在进入 Capture 前排空其明确的因果子批次（Effects → DeathsAndRewards →
必要的生命/来源投影），更新同一流序号，最后才封闭水位；不是先封水位后补发事件。
每个 Effects 子批形成独立的不可变致死集合，按该集合“先锁全部基准，再派奖”；
后续子批可以合法读取前一集合已经提交的等级，但不得改写前一集合的快照或 DeathKey。
发现循环触发、无进展或无法完成的子批次要显式故障，不任意截断一次算“已处理”。

到期采样与当前时间不同不是给旧时钟回拨的许可：只有记录齐全且尚未关闭的边界
能按上述顺序推进；当前姿态、后来的网络意图不能当作旧时刻证据。
过期效果无法在真实证据下完整处理时进入边界故障，不伪造 OccurredAtMs、
不把遗漏视为无伤害、更不能给圈一个默认成功。

### 6.5 喷漆与守护归约与唯一结果

`FLDCaptureReducer` 是唯一的喷漆与守护归约器，`Begin(OpenAtMs, SprayMs, GuardMs)` 以开放时刻（开局时为 `MAX_int64`，最终野外开始时由 `ScheduleOpen` 定下）、
`CAPTURE.SpraySec` 与 `CAPTURE.GuardSec` 建立本局。它持有当前有效集合、`CaptureEpoch`、
喷漆进度持有者、`SoloSinceMs`、`DueAtMs`、争夺中的暂停进度，以及涂鸦主人、守护截止 `GuardDue`
与被冻结的守护进度 `GuardKept`；它们是同一归约器状态，至多一名喷漆进度持有者和一名涂鸦主人，
不是每个角色一条可累计的进度。喷漆区状态、持有者或主人每次变化都推进 Epoch；
结果提案 `FLDCaptureProposal { Epoch, Winner, GuardSinceMs, DueAtMs }` 连同 Match 与已闭合的 Boundary 提交，
`GuardSinceMs` 为守护恢复时刻减已守进度。旧 Epoch 的 Timer 回调永远不能为新一段计时完成。

事实种类为 `Open / Joined / Respawned / ProtectionEnded / Presence / Died / Left / Interrupted`，
按整数边界闭合后依真实时间（原始秒、提交阶段、序号）归约。
开放前归约位置/资格，但不累计；到开放时刻建立新的计时段。
区内恰有一名有效参赛者且他不是涂鸦主人时，他从该次变更开始喷漆（`Solo`）；若他就是争夺暂停的持有者，
`SoloSinceMs` 回拨为恢复时刻减暂停值，`DueAtMs` 为其加 `SprayMs`，即从暂停值续计。
至少两名有效者时（`Contested`），持有者喷漆进度冻结为暂停值，其他人不累积；
持有者离区、死亡、离局或收到 `Interrupted` 即清零，即使争夺仍在继续也无人继承。
与独自喷漆同一毫秒桶内开始的争夺暂停值为零，按无持有者处理。
区内只有涂鸦主人本人时为 `Guarded`，他不喷漆；区内无有效者时为 `Empty`。
`Interrupted` 只清零被控者在施加那一刻的喷漆进度；他若仍独自留在区内，从同一时刻起由 0 重新累积。
持有者不变的区内移动/战斗不重置；同一帧中先争夺再恢复独自喷漆按暂停/续计归约，
争夺事件不丢失、进度也不丢失。

喷漆到期严格早于某条事实时先应用喷漆；与到期同一时刻的死亡、离局、控制和离区先于喷漆处理。
喷上涂鸦即覆盖原涂鸦，主人改为喷漆者，守护从该时刻由 0 开始；这一步不结束对局，
步内完成的喷漆由 `CloseBoundary` 在其真实时刻归约。守护只在 `Guarded`（主人独自在区内，是否受伤受控都不影响）时运行；
`Empty` 时主人在区外、区内无人，守护暂停于 `GuardKept`（2026-11-07 用户决定，原话：“胜利判断好像还是有问题 - 当旗杆被占领但无人在圈内的时候不计算时间，时间停止，现在好像是时间继续计算是吗”）；
`Solo / Contested` 时区内有主人以外的有效参赛者，守护冻结于 `GuardKept`；
回到 `Guarded` 时守护从保留值继续，`GuardDue` 为恢复时刻加剩余时长（2026-11-07 用户决定）。
冻结期间主人从区内走出、且同一时刻的事实全部归约后区内仍有非主人时，`GuardKept` 清零，涂鸦与主人不变；
清零按同一原始秒的事实整体判定：该时刻开始时主人在区内且守护已冻结、结束时主人在区外且区内仍有非主人（`Solo / Contested`），
因此主人与最后一名非主人同时离开（区内无人，只暂停）、或主人离开与非主人进圈同时发生都不清零。涂鸦主人死亡或离局，涂鸦立即清除、旗杆无主。
只有守护满 `GuardMs` 才产生结束对局的提案。

`NextDueMs` 给驱动的停靠边界：未开放时为开放时刻，独自喷漆时为喷漆到期，守护运行时为守护获胜时刻；
争夺暂停与守护暂停、冻结不给驱动额外的停靠边界：恢复只能来自步内运动的进出区事实
（离局在步前入队，死亡、控制、保护到期、复活都在步边界时刻），因此驱动在 Motion 之后预演已入队事实
（`CompletionInsideMs`）；被暂停或冻结的守护在步内恢复（主人在步内重新独自在区内）、且获胜时刻早于本步边界时，
本步不再执行该边界时刻的行动、效果、复活与选择，只重试欠奖，
随即由 `CloseThroughCompletion` 在获胜时刻归约并锁定结果，之后的进出不再改写它。
普通边界上守护截止仍被越过即为“漏切完成边界”故障。
保护者进出不会改变有效集合；其保护结束则是独立资格事件，不能等到下一次碰撞回调。

DEV 构建的归约器另记 `FLDCaptureDevTally` 遥测：每名持有者的开始、续计、暂停、丢失、被打断与喷成次数，
以及全局争夺、喷成、覆盖（喷在别人涂鸦上）、因主人死亡/离局清除的次数和当前涂鸦的喷上时刻。
它只供遥测读取，不参与任何规则。

`TryCloseMatch` 的守护成功条件必须同时包括当前上下文、写闸开放、
已封闭至 DueAtMs 的相关事件前缀、当前未过期的 CaptureEpoch、
该时间边界该参赛者仍是涂鸦主人且守护连续满时长，以及没有未处理的必需死亡奖励/派生工作。
结束原因为 `ELDMatchEndReason::GuardSucceeded`（“守护成功”）；Host 主动结束为 `HostEnded`。
只通过 Timer 或 UI 倒计时不是完成证明。提交一次
`FLDResultReceipt { Match, ResultId, ResultSerial, Reason, OptionalWinner, EndedAtMs }`，
GameState 与归约器只读同一结果，不各写一个赢家。

HostEnd 按实际受理时刻关闭，不宣称已经封闭其后尚未执行的毫秒桶；
只能在更早已提交的必需工作全部结清后受理，无赢家且不等待未来输入。
结果内部保留准确开始/结束时刻，EndedAtMs/DurationMs 为对外安全毫秒投影，
不借较晚整数上界延长对局或继续提交玩法。

结果统计归 GameMode 私有参赛记录，从同一已提交 DeathKey 派生一次：
参赛者真实死亡增加 Deaths；具有合法实际归属的另一参赛者击杀增加 Kills，
死亡事实冻结的每名助攻者增加 Assists（经 `FLDCaptureEligibilityEvent::Assisters` 交给核心，同时写入公开击杀通知
`FLDKillNotice::Assisters`，排序、不含击杀者与受害者，只随参赛者击杀出现），
无主/自杀不指定击杀者也没有助攻，离开/重置不加死亡，野怪不进入该参赛者 K/D/A 统计。
同一键重放不重计。Growth 只提供最终已提交 FinalLevel，不再维护第二份 K/D/A。
锁结果前准备完整 Rows/时间，锁后只发布该冻结快照；离局者的合法结算完成后仍有结果行，
不能从已销毁 Pawn、killfeed 到达次数、当前公开名单或客户端镜头推算。

关闭后延迟伤害、控制、回复、奖励、选卡、营地刷新、复活、喷漆和守护一律没有新提交权；
已完成事务的只读回执可按其窗口重放。无胜者 HostEnd 也使用相同写闸。
Host 连接消失是会话故障，不由客户端生成一份假的权威 HostEnd receipt。

### 6.6 故障关闭与无永久 Pending

`FailAuthorityBoundary` 只属于 GameMode，输入当前 Boundary 及服务提供的明确故障事实。
`FLDAuthorityFault` 至少有具体原因、所属服务/操作键和仍未支付的 DeathKey；
内部细节只供审计和相应拥有者回执，不广播隐藏对手资料。
`FLDFaultReceipt` 包含 Match、FaultId、失败 Boundary、原因和关闭后的 GateRevision，
不带默认胜者、不伪造“正常完成”的 ResultReceipt。

对仍活跃的当前局，入口一次性切换 `Faulted`、关闭全域玩法写闸并使旧 permit 失效，
保留哪些事实已提交/哪些奖励未支付的真实区别，拒绝后续补奖或新玩法提交。
该失败边界不推进成功消费水位，不写 NoReward 来掩盖支付失败，
也不让喷漆与守护归约跳过该故障获得胜利。已支付的事务仍只能只读重放。
若正常结果先已锁定，返回 AlreadyTerminal 并记录错误，不能用故障覆盖已存在的胜者。

随后以明确的权威故障通知结束 Pending，沿 GameInstance 的失败路径回前端并清理退休局；
不是无限停在死局等待，也不将该局计为正常完成。旧局整体退休时允许释放其缓冲/对象，
但不能冒充正常 AckConsumed。重新建房使用新上下文，不恢复或偷偷补结旧故障局。
这是一条工程失败路径，不是超时胜利、分数结算或另一种可玩的模式。
普通玩家命令的目标/蓝量/价格/范围等可理解拒绝不属于必需权威工作失败，
不能用任意非法输入触发整局 Faulted；这些请求只返回其正常拒绝及已生效的保护变化。

## 7. 内容键、单位和真正未决项

所有数值/公式及建议审核状态由 `CONTENT_AND_BALANCE.md` 单点维护；
本报告不提供备用值、临时默认值或第二份可编辑数值表。
内容作者使用秒，编译产物使用经严格检查的毫秒时长；绝对截止只在本局服务器建立。
距离使用 Unreal 厘米。完整 Demo 必须使用批准内容，不仅让参数字段非空。

| 正式参数 / 政策键 | 类型、单位和消费者 | 接入与正反验收 |
| --- | --- | --- |
| `MATCH.FieldSegmentSec` | 正时长，作者秒；`FLDMatchTimeline` | 三段定时野外各一段，只按野外进度 T 计；R3 返场后的最终野外开放旗杆，开放后不再关闭。已删除的 `MATCH.CountdownSec`、`MATCH.DevelopmentSec` 不能作缺值 fallback（导入以 UnknownField 拒绝）。 |
| `REST.OpeningBuildSec`、`REST.BuildSec`、`REST.SignupSec`、`REST.DrawSec`、`REST.BettingSec`、`REST.PKReadySec`、`REST.PKMaxSec`、`REST.SettlementSec`、`REST.PayoutSec`、`REST.ReturnSec` | 正时长，作者秒；`FLDMatchTimeline` | 休息各步骤时长（§3.4）；报名窗不得长于配装窗，`FLDTimelineParameters::Validate` 与导入校验拒绝。 |
| `REST.ReturnSearchRadiusCm` | 厘米；生命域/World | 返场原脚点被占用时的就近搜索半径（`ReserveSpawnNear`），仍失败则正常抽点。 |
| `CAPTURE.SpraySec` | 有界时长，作者秒；GameMode 喷漆归约/UI/AI | 从独自喷漆开始（续计时回拨暂停值）建立喷上涂鸦的截止；争夺暂停所有人进度，离区/死亡/离局/硬控施加清零，普通伤害、减速、缴械不影响。 |
| `CAPTURE.GuardSec` | 有界时长，作者秒；GameMode 守护归约/UI/AI | 喷上涂鸦起守护从 0 计；只有他人独自喷漆时暂停，改漆失败从暂停处续计；主人死亡/离局清除涂鸦；连续满时长即守护成功。 |
| `LIFE.RespawnSec.Field1/Field2/Field3/Final` | 有界时长，作者秒；生命域 | 按死亡时的野外进度 T 所在野外段选一个（#198 批准为试玩输入），不按等级/人数/死亡次数变化，不自行加入缩放公式。死亡事实建立期限一次，重复死亡不再延长或缩短；进入休息时剩余等待结束。 |
| `LIFE.ProtectionSec` | 有界时长，作者秒；生命/Combat/喷漆归约/UI | 成功复活建立保护期限；到期等号、合法主动解除、保护期间不计入喷漆区人数均按 §5/6。 |
| `CAPTURE.RadiusCm` | 厘米，脚点 XY 中心的喷漆区半径（边界算在内）；World 查询 | 使用世界统一边界包含规则，不在喷漆归约/Combat/UI 分别加减碰撞半径或容差。 |
| `UNIT.CollisionRadiusCm` | 厘米，参与者碰撞尺度；World/原生身体 | 只服务必要合法落地/防重叠及碰撞，不据此扩大喷漆区、加危险评分或重抽锚点。 |
| `POLICY.InvalidAggressionProtection` | 已确认 `BreakOnAggressiveIntent`；Combat/生命 | 外层身份/当前生命/shape/请求/闸门验证后，进攻意图即解保护；路径/目标/来源/资源等语义失败也解除，具体入口见§5.3。 |
| `POLICY.DeadChoiceCommit` | 已确认 `Allowed`；成长/生命/UI | 死亡可确认，只改持久构筑；重验Life+ExpectedLifePhase，不能提前复活/治疗尸体，不能执行死前Alive确认。 |
| `POLICY.ReleasedEffectSourceLoss` | 已确认独立已释放效果的死亡存续；Combat/内容 | `ContinueForRemainingContender`：释放者死亡仍继续原期限，合法信用归仍在局者；未完成release取消，非独立附属效果按内容定义，不扩大规则。 |
| `POLICY.SourceLeave` | 已确认离局行为；Combat/世界/生命 | `CancelUnsettledNoRefund`：立即移体/碰撞、取消未结束独立效果；不回滚已发生事实，先前认证奖励仅settlement-only结清，不发离开奖励。 |
| `POLICY.MissingSourceCooldown` | 已确认 `ContinueServerTime`；Combat/成长 | 遗忘期间也继续原冷却，重学沿用原结束时间，不能因来源重建清零。 |
| `POLICY.ResourceMaxChange` | 已确认 `PreserveRatio`；Combat/成长 | 存活者构筑改变上限按原百分比投影；死亡选择不治疗尸体/提前复活，正常复活仍满最终HP/MP。精确取整/定义域/下溢拒绝按内容公式，不在此复制。 |

时长的可接受范围、资源公式、叠加及初始值由内容定义；本域只规定有限值、单位、
精度和安全加法必须验证，溢出不能产生“立即到期”或无限保护。
内容输入沿批准定义 → `SourceData\Demo` 唯一作者域 → 统一内容 compiler →
`Data\Demo` 内容载荷/根 manifest/contract → `ULDDemoContentCatalog` 导入与开局校验。
世界几何使用独立的 `demo_world` / `SyncDemoWorldData` 链（见世界报告）；
不让游戏读取本文，不手填生成哈希，也不回落旧 prototype/V08 目录。

死亡静态自由镜头、不新增活体视野、无LastSeen标记、可见对手字段及终局统计
已确认，按§4白名单接线，不能继续列为未定。
基础普攻近战单体及经属性/技能/被动获得群攻、射程成长已确认；具体数值由 D01 批准为试玩输入，
批准不等于已平衡。喷漆 + 守护规则（#112）的 `CAPTURE.RadiusCm`、`CAPTURE.SpraySec`、`CAPTURE.GuardSec`
已批准为试玩输入，与 `LIFE.RespawnSec.*` 的配合待试玩复核（见内容报告）。本模块没有另需用户选择的生命周期分支；
同世界阶段选择、毫秒转换、HostEnd 的工程先后、类型命名、请求窗口及水位证明属于工程设计。

## 8. 具体正反验收

**本节是验收设计；已自动化的部分及其范围见 §10，未列入 §10 的条目没有自动化证据。**
`H` 表示有正常主画面的参赛 Listen Host；`C1/C2` 是独立进程、真实拥有连接的客户端；
`A1–A3` 是实际参赛 AI。真实连接不是调用一下服务器函数或只有 Host 的调试世界。
完整场景另外分别记录 LAN 与正确配置后的公网结果，同机回环不能代替它们。

纯规则夹具可以注入确定的时间/随机流/故障，验证精确边界；集成验收必须让事件从
原生运动、拥有者 RPC、实际命中和真实断开入口产生，不能只往归约器填一张最终人数表。
完整 Demo 场景必须用批准内容正常普攻发育、成长、喷漆和守护，
不使用调试授予、传送、固定胜者或强制完成按钮。

### 8.1 房间、开始和失败

| ID | 前置和动作 | 精确正反断言 |
| --- | --- | --- |
| SM-01 | H+C1+C2，选择三 AI，所有真人 Ready 后 H Start | 六名独立参赛者、含 Host 的三真人上限；单次初始化；AI 不是队友。再加入第四真人明确 RoomFull；三 AI 不挤占真人计数。 |
| SM-02 | H 单人、零 AI；另覆盖未用满的真人和 AI 名额 | 不补满，不要求两真人；练习仍须完成发育、完整喷漆与守护，不自动判 H 胜。 |
| SM-03 | 一真人未准备；C1 发 Start/改 AI 数/结束；伪造别人 Ready | 分别 NotAllReady/NotHost/NotOwner，无状态副作用；Host 自己也必须 Ready。 |
| SM-04 | 同一结构版本 Ready 并发；Ready 撤销/AI 改数/最终 Join 与 Start 竞争 | Ready 位更新不会无限使其他 Ready 过期；结构变更清准备；Start 的原子锁定点决定唯一名单，PreLogin 先通过也不能晚入局。 |
| SM-05 | 两连接同时争最后名额；重复 Start；同请求 ID 改条款；回执过窗 | 不超员、不双启动；同键重放一次，改条款拒绝，过窗不再执行且能查询当前状态。 |
| SM-06 | 取消加入，立即重试；旧尝试迟到成功/失败 | 无幽灵席位，无迟到成功重新开房，无旧失败清掉新连接；错误归属当前 WorldContext/Attempt。 |
| SM-07 | 非法 IP/端口、不可达目标、wire/产品/内容分别不兼容、已开始后加入 | 明确区分对应错误；回菜单可重试；不笼统断言超时一定是 NAT，不静默回落旧 profile。 |
| SM-08 | Starting 中注入必要输入/身体/落地准备失败或成员离开 | 没有局部玩家可战斗、部分初始资源、旧营地和计时残留；失败 Match 退休，回新 Lobby 重准备；旧启动回调不能污染重试。 |
| SM-09 | C1 在 Lobby/活跃局分别离开，再用同名同 IP 加入 | Lobby 可为下一局新准入；活跃局拒绝晚加入/重连，无席位接管或 AI 补人；离开不发奖、不按剩余人数判胜。 |
| SM-10 | 对照 H 正常死亡、H 主动结束、H 离开、H 进程丢失 | 死亡不关闭服务器；主动结束无赢家且可回 Lobby；连接丢失时 C1/C2 回前端并获原因，不移交 Host。 |
| SM-11 | 默认开发入口和包内入口从Frontend建房；Bootstrap Generate/Validate与新隔离Cook/Stage | 都进入L_Demo并绑定DA_DemoWorld；项目终态地图根仅Frontend与唯一Demo，旧DevelopmentRegion/CivicRegion不作fallback，不再生成或进入最终包；缺新根明确失败。 |

### 8.2 生命和保护

| ID | 前置和动作 | 精确正反断言 |
| --- | --- | --- |
| LF-01 | C1 经正常成长获得技能/被动/永久属性及当前待选候选后死亡 | 全部持久状态保留；无死亡税/掉落/初始奖励重发；复活派生不重复加一次永久属性。 |
| LF-02 | 同 VictimLife 重复致死；之后复活再次死亡 | 前者一个 DeathKey、一份等待和复合奖励；后者新 Life 产生另一合法死亡，不能被只按参赛者去重吞掉。 |
| LF-03 | H/C1/AI 带未结束来源冷却死亡和复活；raw Now=1000.2ms、ReadyAt=1001ms | 剩余时间继续且只在真实到期可用；不能用ceil Now提前，也不能因新Pawn/槽位/BodyIdentity清零；失源后重学同样不洗。 |
| LF-04 | 固定无偏 RNG 夹具覆盖十个输出及拒绝区间；相同随机流下改变敌人距离/视野；强制连续相同索引 | 仍使用完整十点均匀采样；改变威胁状态不改变抽点；允许同人连续同点和多人同点。不能以小样本次数恰好相等作为均匀性证明。 |
| LF-05 | 同点多 H/C/AI，注入 Reserve 失败/重试、重复 Finalize | 在选中锚点局部合法落地；重试不增加抽样次数、不多增 Life；Finalize 无第二次初始化。准备失败明确报错，内部提交不变式错误走 AuthorityFault。 |
| LF-06 | 复活后立即读取身体、Owner/UI 和喷漆区状态 | 同一新 Body/Life，满最终上限 HP/MP、无旧临时控制；保护期限来自成功提交，保护者不喷不争。 |
| LF-07 | 保护中移动/受击/控制；确认AttackUnit/AttackMove/QW/12且目标缺失/超距/缺蓝/过期binding；对照旧包/畸形包；到期同刻受击 | 移动/预览不解；可识别新意图在语义验证前解并使其计入喷漆区，Rejected仍带LifeRevision而无资源/CD；旧/畸形包不解；自然到期等号不免疫。 |
| LF-08 | 延迟死亡前移动/施法/确认到复活之后；同刻死亡+复活 Timer/离局/结束 | 旧生命请求无效，候选保留不赋予旧请求执行权；终局/离局后不创建新身体。 |
| LF-09 | 同Effects集合互杀且会升级；死后独立效果命中；致死1000.2ms/离局1000.8ms及相反顺序 | 基准全先锁、死者合法收奖；致死在前则EligibleAtDeath不被后离局撤销，settlement-only结清；离局在前取消效果无新死，不按同桶阶段倒排。 |
| LF-10 | 死亡确认/替换、迟到Alive确认、上限成长；离局有身体/独立效果 | Dead确认只改保留构筑，旧Phase拒绝，尸体不治；复活满新上限；离局立即移体/碰撞、取消未结效果，不回滚先前事实。 |

### 8.3 喷漆、守护与实际事件水位

| ID | 前置和动作 | 精确正反断言 |
| --- | --- | --- |
| CP-01 | R3 前站在喷漆区，返场回到原脚点跨过最终野外的开放边界 | 开放前无进度/胜利；从开放边界才开始新段，期间仍能成长。 |
| CP-02 | 空区→独自喷漆→两有效者争夺→原喷漆者独自；对照争夺中原喷漆者离区/死亡/离局 | 争夺暂停喷漆进度、他人不累积，恢复独自喷漆从暂停值续计；原喷漆者失效即清零，下一喷漆者从 0 开始。 |
| CP-03 | 区内走动、普攻、施法、非致命伤害、减速和缴械；对照定身/眩晕/嘲讽/击退、击退出区、致死 | 前组不改变喷漆进度；后组在施加或离区/死亡时刻清零，仍独自在区内的被控者从 0 重新累积。 |
| CP-04 | 唯一保护者；喷漆者旁保护者；受保护新生命落区；区内野怪/尸体/特效 | 无效实体不喷不争；新生命不短暂暴露无保护Alive；代理不双计数；保护结束才具资格。 |
| CP-05 | 同一原生步中喷漆者离开再返回；竞争者进入再离开；滑移/绕角产生多次穿越 | 最终位置相同也保留中间变更：喷漆者离开即清零，竞争者进出按暂停/续计；不能只用步首/末快照。 |
| CP-06 | 同刻竞争者先有效进入再死亡；对照先死亡再经过 | 前者是一次真实争夺，期间喷漆者不累积；后者不计尸体。旧生命事件不能因本批新 Life 已登记而被抹掉。 |
| CP-07 | 在喷漆 `DueAtMs` 与守护获胜时刻的 `-1`、`0`、`+1` 分别发生进入/离区/控制/死亡/离局 | 喷漆截止同刻的死亡、离局、控制、离区先于喷漆处理；守护截止前及同刻的独自喷漆或主人死亡/离局阻止原胜利；之后事件不得回填推翻已合法锁定的结果。使用实际服务器发生时间，不用客户端报告。 |
| CP-08 | 区内保护者恰在喷漆完成时刻到期；对照在完成之后到期；本时刻只有非致命伤害 | 同刻保护到期形成争夺；后到期不倒灌；单纯非致命伤害仍可完成。 |
| CP-09 | 非整数毫秒交点略早/正好/略晚于截止；同毫秒两次穿越；保护者1000.8ms离区、1001ms到期 | ceil不把截止后事件归前；先原始时间再真正同时因果序；最后一例不虚构争夺，不能按Session/Motion阶段逆序。 |
| CP-10 | Motion静止、查询未完成/系统错误、序号缺口、重复水位、旧Match/Body、未来事件；一段穿区入/出 | 静止封空流；缺证据不判胜，不与已确认NotGrounded混同；段序不作两个事件共同去重键；未来事件不补当前缺口。 |
| CP-11 | FScopedMovementUpdate 试探后回滚、客户端 correction、真实 root motion/击退 | 前两者不发布权威穿越；后两者经同一真实原生生产链发布，不另积分。 |
| CP-12 | 延长帧/延迟效果/关闭边界前缺少相应位置证据 | 未闭合前不宣布胜利；不拿当前姿态补历史；明确 GeometryUnavailable/边界故障，而非默认为无人或 Miss。 |
| CP-13 | C2 看不到喷漆者或涂鸦主人；AI 根据公共喷漆/守护进度移动、改漆或围攻；长期无人守住或所有人暂死 | 喷漆、涂鸦与守护状态仍公开，但不泄露隐藏坐标/构筑；AI 可同规则胜；无击杀/财富/时间/人数胜利。 |
| CP-14 | 权威CurrentFloor失效但XY仍在区内，再在原XY合法落地；对照缺数据/试探floor回滚/无效目标SurfaceHint | 确定NotGrounded发布Inside=false且可闭合水位，落地重查恢复占用；未知缺证据不封水位，试探不发事件；错误瞄准不改本人占用，语义进攻拒绝仍按规则解保护。 |
| CP-15 | 喷上涂鸦后主人离区、受伤、受控；他人独自改漆或与主人同在区内后离区；冻结期间主人走出喷漆区；他人改漆成功；主人死亡/离局 | 区内无他人时主人离区、受伤、受控不影响守护；主人以外有人在区内即冻结守护，全部离开后从冻结值续计；冻结期间主人从区内走出则守护清零、涂鸦保留（同时离开不清零）；改漆成功覆盖涂鸦并从 0 守护；主人死亡/离局立即清除涂鸦；守护满时长唯一胜利，原因 `GuardSucceeded`。 |

### 8.4 终局、故障和下一局

| ID | 前置和动作 | 精确正反断言 |
| --- | --- | --- |
| FN-01 | 同截止边界有动作、到期致死、XP、选择 | 严格执行 §6 单序；完整闸前提交保留一次真实回执，闸后首次提交拒绝；不会边出结果边发经验或换构筑。 |
| FN-02 | 奖励准备的一侧失败、事务派生触发同刻战斗、未处理因果子批次 | 无半奖/默认成功；Progression 不提前封水位；一次受控准备重试仍失败则 Faulted/明确未支付/回前端，不正常判胜、不伪造NoReward、不关后补奖。 |
| FN-03 | 预先取得 permit，随后终局/新局，再触发异步回调；在 CloseCombat/取消中尝试发奖 | 最终提交 ValidateWritePermit 拒绝；清理没有玩法写权，不依赖早先 bool 检查。 |
| FN-04 | 重复/冲突守护提案、旧 CaptureEpoch 回调、HostEnd 与完成同刻、完成后再 HostEnd | 只一个结果和 ResultId；阶段 0 接受 HostEnd 时无胜者，已锁胜者不能被后续 HostEnd 覆盖。 |
| FN-05 | H 带 C1/C2 回同世界 Lobby，再重新准备开始 | 拥有连接保持；Match Guid 换代，全部局内状态、营地/AI/效果/候选/来源清空；Lobby 期间不继续结算旧游戏。 |
| FN-06 | 在新局重放旧移动、伤害、Offer、事务、观察、复活、喷漆/涂鸦及结果消息 | 全部失去新提交/覆盖权；同 ContenderId 或数值 Life 也不能跨 Match 混用；名称和本地设置仍保留。 |
| FN-07 | 必需工作发生精度/计数溢出、积压或不变式失败；另作固定实体反复复活长测、普通非法请求对照 | 无环绕/默认/假胜者；退休缓存按实体和未消费窗口有界；普通请求拒绝不终止全局；真正故障退出不记正常完成。 |
| FN-08 | H+C1+C2+A1–A3完整对局后再开；LAN/公网；途中离局、killfeed重放 | 正常循环无调试捷径；结果含锁定名单全部行/唯一赢家/原因/真实用时，leave不增Deaths，重放不重计K/D；不公开完整Build。 |
| FN-09 | 工程夹具中首个Effects集合派奖令A升级；Transactions合法因果效果随后杀A | 后续集合锁当时新等级，各集合分别先锁后奖；首个集合基准不被回读修改，不要求预知后续死亡；没有同DeathKey重复奖。 |

场景输出应记录本次输入摘要、连接归属/拓扑、Match 映射、相关 Body/Life/DeathKey、
时间/流序/水位、提交阶段及回执，不向玩家错误信息泄露这些内部审计字段。
输出写忽略的 `Artifacts`；本报告不引用旧输出作为当前通过证据。

## 9. 实现位置

旧 Objective/Claim/路线/站桩、旧 profile、死亡税、安全最远出生、checkpoint/restore 与旧时钟分派已删除；
现行退役范围与不得恢复的内容以[架构 §3.2](ARCHITECTURE.md#32-删除清单与防回归约束)为准。
下列源码均在 `Source\Lockdown` 下，是本文契约的唯一实现；不另设管理器或第二套状态。

| 职责 | 源码 | 要点 |
| --- | --- | --- |
| 房间、准入、名称、命令回执 | `Private\Framework\LDGameModeSession.cpp`、`LDPlayerSession.cpp`；`Public/Private\Framework\LDMatchLifecycleTypes.*` | PreLogin/最终入房两次准入，`?LDName=` 经 `ResolveContenderName` 解析（缺省“玩家”，非法明确拒绝）；`ProcessSessionCommand` 每次可能改动 `Contenders` 后重新查找记录。拒绝与回执文案为中文。 |
| 开局、Starting、Prepare、出生 | `Private\Framework\LDGameModeMatch.cpp`、`LDMatchProviders.cpp` | `LDMatchProviders` 门禁：Combat、Progression、Camps 及（有 AI 名额时）OpponentAI 都注册才可开始；Prepare 失败走同一 `RetireMatch`，发布新一代 Lobby 与明确失败。 |
| 写闸、阶段、回 Lobby、关房、本人视图发布 | `Private\Framework\LDGameMode.cpp`、`LDGameModeLifecycle.cpp` | `RetireMatch` 先关闸，再按注册逆序调 MatchReset，`RetireAuthorityCore`/`RetireMatchDomains` 拆除，丢弃 AI 与离局记录后换代。`SyncOwner` 在世界拆除、`Closing`、`Resetting` 时不发布、不判投影故障；Host 换图前在 `PreLoadMapWithContext` 关闭房间并通知成员。 |
| 生命、复活、保护解除 | `Private\Framework\LDGameModeLife.cpp` | 由 Combat 维护；向本模块发布 Joined/Respawned/ProtectionEnded/Died/Left 资格事件；野外死亡的复活等待按死亡时 T 取值。 |
| 外层时间线、休息与阶段边界（#198） | `Public/Private\Framework\LDMatchTimeline.*`、`Private\Framework\LDGameModeRest.cpp`、`LDPlayerRest.cpp`、`Private\Combat\LDCombatRest.cpp`、`Private\World\LDCampDirector.cpp`（`FreezeForRest/ThawAfterRest`）、`Private\World\LDSpawnPlacement.cpp`（`ReserveSpawnNear`） | 见 §3.4：纯时间线归约与公开视图、休息锚点与隔离组、阶段生命退休与放置（`PhaseRetired`、`PendingPhasePlacement`）、`CancelAllForPhase`/`ReadyAllForPhase`、世界冻结与解冻、PK 报名与可选 PK 提供者钩子。 |
| 权威推进与阶段调用点 | `Private\Combat\LDCombatDriver.cpp`、`Private\World\LDWorldClock.cpp` | 由 Combat 维护；RespawnsAndWorld 先 `AdvanceTimeline`，Transactions 末尾 `LockTimelineBuild`，之后依次调用 `ExecuteCapturePhase` → `ExecuteClosePhase` → `ExecuteCapturePublish`，以 `NextTimelineDueMs`、`NextCaptureDueMs` 等切步。 |
| 喷漆与守护归约、结果、公共投影 | `Public/Private\Framework\LDCaptureReducer.*`、`LDCaptureTypes.*`、`Private\Framework\LDGameModeCapture.cpp` | 见 §6.5；`PublishCaptureInterrupt` 接收 Combat 的硬控事实；`FLDPublicMatchView`/`FLDResultView`/`FLDKillFeedView` 经各自 Validate/Accept 拒旧与拒畸形。 |
| 喷漆区几何与世界表现 | `Public/Private\World\LDCaptureCircle.*`、`LDCaptureMarker.*` | `LDCaptureCircle::Extract` 求真实进出；`ALDCaptureMarker` 是各端本地、无碰撞的边界环（颜色随公开状态，`Guarded` 为蓝色），并在旗杆约 210 cm 高处挂一块按涂鸦主人身份色染色的涂鸦板，无主时隐藏；不参与判定、不进导航或视线。 |

## 10. 检查入口

| 入口 | 覆盖 | 不覆盖 |
| --- | --- | --- |
| `Lockdown.Rules.DemoCapture.SprayAndGuard`、`.GuardFreeze`、`.GuardNeedsOwner`、`.SprayInterrupt`、`.GraffitiCleared`、`.ContestPause`、`.ProtectionAndContracts`、`.PausedContestSteps` | 开放边界、同桶中间进出、独自喷漆与喷上涂鸦、争夺暂停/续计、喷漆者离区/死亡/离局/硬控清零且不继承、被控仍在区内从 0 重喷、喷漆截止同刻的事实先处理、涂鸦覆盖与守护从 0 开始、主人离区且区内无人时守护暂停在当前值（越过原获胜时刻也不获胜）、主人独自回区从保留值续计并须在区内站满剩余时长才获胜（`.GuardNeedsOwner`）、主人以外有人在区内（独自改漆、与主人争夺、多名挑战者）冻结守护与全部离开后主人独自在区内时续计、冻结期间主人走出清零而涂鸦保留、主人先在区外只冻结、主人与挑战者同刻离开或同刻一出一进不清零、保护者不冻结、冻结或暂停的守护在步内恢复并完成、主人死亡/离局清除涂鸦、守护满时长唯一胜利、步内续计完成（`CompletionInsideMs`/`CloseThroughCompletion`）、暂停争夺不缩短驱动步长（2 s 争夺不超过 40 步）、漏切边界与契约故障、保护者不喷不争 | 原生运动与真实控制产生的事件序列（由场景覆盖一部分） |
| `Lockdown.Rules.DemoCore.CaptureCompletesInsideStep` | 核心步序：Motion 后判定步内被冻结的守护恢复并完成，跳过边界时刻玩法阶段，Capture/Close 在守护获胜时刻锁定唯一结果 | 真实驱动与原生运动（步内守护完成在场景中不一定出现） |
| `.Geometry`、`.PublicViews`、`.ProviderGate`、`.SessionEndgame` | 喷漆区求交、公共 DTO（喷漆、涂鸦主人、守护字段组合）校验、提供者门禁、Host 结束/回 Lobby/换代隔离、名称解析 | 真实连接 |
| `Lockdown.Rules.DemoSessionReceipts`、`Lockdown.Rules.DemoCore.*`、`Lockdown.Rules.ViewPrivacySchema` | 会话窗口重放/拒旧、核心时间与许可、公开字段白名单 | 真实连接 |
| `Lockdown.Rules.DemoRest.Timeline`、`.PhasePolicy` | 纯时间线：批准时长、报名窗口（截止含在内）、锁定保持、PK 跳过、T 在休息冻结与野外推进、完整中场休息 126 s、PK 提前结束、最终野外、按 T 的复活等待、客户端 T 推导；隔离组、冻结身体、阶段清理与补满 | 真实驱动下的阶段生命放置、返场就近搜索与世界冻结（场景探针已改写，尚未运行）；PK 本身 |
| `Scripts\RunDemoCaptureCheck.ps1` | Listen Host + 两个拥有客户端经正常大厅开局：名单与结算行显示所选名称；Client1 喷漆，Client2 进区争夺（暂停）后离开（续计），涂鸦喷上（C07）；主人走出喷漆区使守护暂停在当前值，Client2 独自改漆使守护保持冻结，3.5 s 前走出区（改漆进度清零、主人仍在区外所以守护仍暂停），主人独自回区后守护从保留值续计（C18）；Client2 进区与主人同在（守护冻结），主人再走出（守护清零、涂鸦保留），Client2 在改漆喷上前走出（区内无人，守护停在 0），主人独自回区后守护从 0 计时（C19）；主人独自在区内站满守护时长唯一胜利、原因 `GuardSucceeded`（C06）与计时（C08）；唯一结果与写闸关闭；边界环与涂鸦板在各端出现/变色/移除（C14）；同世界回新一代 Lobby 并正常开出第二局、核对干净开局与旧代请求拒绝 | 喷漆者被硬控或死亡、主人死亡清除涂鸦、3+3、AI、网络异常 |
| `Scripts\RunDemoSessionFaultCheck.ps1` | 客户端离局、Host 进程丢失、加入失败、版本/满员/已开局拒绝、开局竞态、Host 经菜单离开 | 跨机器局域网与公网 |
| `Scripts\RunDemoOpponentCheck.ps1` | AI 绑定、自主循环、喷漆/守护/围攻行为、AI 以 `GuardSucceeded` 获胜且是公开涂鸦主人、回 Lobby 后退役 | 真人离开、网络异常 |

各角色 runner 要求恰好报告其稳定检查 id，缺失、多出或重复都失败。

## 11. SC-09 对接交付表

| 必需项 | 本模块的正式交付 | 生产者 / 消费者与失效规则 |
| --- | --- | --- |
| 状态所有者 / 函数 | §1、§3、§4：GameMode 阶段/参赛资格/结果；Combat 唯一权威推进；GameMode 唯一喷漆与守护归约 | GameState/OwnerState/AI 都是获准投影。没有 GameMode/Combat 两个相互推进的调度器或重复可写结果。 |
| Match 双表示 | §4 的 `FLDMatchContext{Generation,LedgerMatch}`；GetMatchContext；GI 只分配不回退 Generation | 网络 Generation 与 LedgerMatch Guid 显式映射；成功新局/失败启动换代；旧匹配请求拒绝，不能强转/猜 Guid。 |
| Contender / Body / Life / Death | §4/5 的 VictimKey、BodyRef、LifePhase、DeathKey、EndContenderLife、AI 绑定 | 每个 Effects 集合先全锁再奖；死亡可选但重查 Phase/不治尸体，成功复活才增 Life。 |
| 命令、结果、权限 | §4.1 的 Ready/SetAICount/StartMatch/EndMatch/ReturnToLobby；离开沿用断开/`Logout`；SessionReceipt | 拥有连接与 Host 服务端解析；单在途、精确条款/过窗拒绝。AI 不伪造 RPC。 |
| 时间 / 序号 / 水位 | §6 的 Step、事件时间、提供者前缀、Boundary、运动与资格发布 | 原始秒跨流排序、毫秒只 ceil 桶；只封闭已完成整数边界。 |
| 终局与非重入提交 | §6 的借用 GameplayWritePermit、ValidateWritePermit、TryCloseMatch、ResultView、故障关闭 | 先锁闸再清理/发布；闸后不首次提交；必需失败明确故障关闭，不造胜者。 |
| 取消/死亡/离开/新局 | §5.4 矩阵；§3.3 同世界 Reset；§4.3 生命周期通知 | 保留候选/财产/持续冷却，清短寿命动作；退休后的新请求拒绝；Host 故障不转移。 |
| 参数/单位/政策 | §7 的 MATCH/REST/CAPTURE/LIFE/UNIT/POLICY 键，数值单点在内容报告 | D01 批准为试玩输入，不等于已平衡；无默认值。 |
| 公共/私有读权限 | §4.2/4.3：Lobby/PublicMatch/ResultView/Owner/AI self；LifeNotice/MatchClosed/MatchReset | 全场只公开喷漆区状态、喷漆进度持有者与进度、涂鸦主人与守护进度，无隐藏坐标/构筑；ResultView 不发 LedgerMatch；Host 同样受限。 |

## 12. 证据限制

- 上表规则测试与场景只在同一台机器的回环连接上运行；跨机器局域网与公网直连没有证据。
- 3+3 满员、打包版与网络异常场景在里程碑检查和发布验收时运行；某次运行的结论以当次 `Artifacts` 报告为准，本文不引用旧报告作为当前通过的依据。
- 争夺中喷漆者死亡后清零、硬控打断喷漆、主人死亡/离局清除涂鸦只有规则测试证据；喷漆 + 守护数值与 `LIFE.RespawnSec.*` 的配合未经试玩复核。
- #198 外层流程只有规则测试（`DemoRest.*`）证据；改写到休息流程的拥有连接场景探针尚未运行。
- 未实现：PK/竞猜/观众投掷（#198 会话 D）；AI 不使用 NPC、洗点、替换、遗忘与训练。训练假人只有规则测试（`DemoRest.PhasePolicy`），没有拥有连接场景。
- 视野过滤不能防范恶意 Host；Host 断线时客户端按引擎默认约 60 秒连接超时后才回到菜单。
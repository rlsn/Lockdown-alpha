# Demo 会话、对局与生命生命周期

**目标：v0.10.0 · 架构职责：M1 / M2 / M5 · Unreal Engine 5.8.4**

产品规则以 [App Spec](../APP_SPEC.md) 为准；实施依赖以
[架构审计](ARCHITECTURE.md)、[共享契约](SHARED_CONTRACTS.md) 和
[实施计划](IMPLEMENTATION_PLAN.md) 为准。本报告只提供实施设计，不实现游戏或裁剪工程。
下文的正式目标类型、函数、状态和验收场景均为**拟建或拟改造**，不是现有 API 或运行通过声明。
运行源码证据固定于 `fa11d09a762bccadf495191072e222dd158fc014`；
当前分支上的准备文档提交不改变这一运行基线。

## 1. 结论与责任边界

选择**现有 Listen 世界内的单一会话/对局流程**：前端创建或 IP 加入，进入 Lobby；
Host 在真人全部准备后开始；发育期结束开放唯一圈；连续独占成功锁定唯一结果；
Host 带仍连接的房间回 Lobby，再完整初始化新局。Lobby 和结果是该世界的阶段，
不是另外保留旧 profile、额外玩法地图或世界检查点恢复模式。

现有 GameInstance 的连接失败处理、GameMode 的私有参赛记录、GameState 公共投影、
Combat 的身体/命中时序、Navigation/CharacterMovement 的生命和取消代次可以收敛复用。
不能原样保留旧人数/职业开始条件、死亡扣钱、安全最远出生、Heart/Claim/deadline 胜负，
也不能将旧本地重载测试视为同房间多人新局的证明。

本域是共享比赛身份、比赛时钟、实际事件批次及消费水位、生命边界和终局写闸的
**唯一正式提供者**。这些职责接回现有 GameMode/Combat 调度和 Objective，
不新增通用事件总线、第二套身体表、移动积分器、经济账本或独立胜负 Manager。

| 所有者 | 本域规定的职责 | 不越过的边界 |
| --- | --- | --- |
| `ULDGameInstance` | 本进程连接尝试、失败归属、本地反馈、网络比赛代次分配 | 不保存跨局战力，不替服务器判定成功，不迁移 Host |
| `ALDGameMode` | 房间身份/准入/准备/名单；Match 映射；参赛资格/生命；批次和写闸；唯一结果 | 不自行实现命中、发金币、生成三选一或积分移动 |
| `ALDCombatDirector` | 既有权威推进入口；身体、动作、伤害/控制；首次致死事实 | 死亡只生产一次；生命域消费同一事实，不再认定另一死亡 |
| `ALDObjectiveDirector` | 唯一圈的连续独占归约、完成提案 | 不读客户端位置或借迷雾判断；不直接提交第二个结果 |
| 操作域 / CharacterMovement | 实际执行的移动、碰撞、位移分段与运动完成水位 | 不由圈 Tick 推导轨迹；不把当前姿态回填过去 |
| 世界域 | 十点抽样与同点合法落地；圈几何/交点；营地实体生命映射 | 不再维护一份占领状态或参赛者生命计数 |
| 成长/装备/经济域 | 致死事实的金币+经验+等级+机会复合提交、Offer、构筑、装备和回执 | 只接受本域当前批次许可；不另外生成 Match、DeathKey 或结束时钟 |
| 视野 / UI / AI | 获准的自身、观测和公共投影；同一权威命令 | 不回写真相；Host 普通主画面和 AI 不因在服务器进程而获得全知权限 |

## 2. 固定规则与实施边界

| 规则 | 本模块必须兑现的含义 |
| --- | --- |
| 房间规模 | 最多三名真人，包含 Host；Host 选择零至三个独立参赛 AI。没有队伍，不补满空位；Host 独自练习仍走同一流程。 |
| 开始条件 | 仅 Host 可开始；全部已连接真人含 Host 都准备；AI 不伪造玩家 Ready RPC；开始时锁定真实参赛名单。 |
| 联机边界 | IP 直连；局域网和正确配置防火墙/手动端口转发的公网可达地址；无自动 NAT 穿透、中继、中途加入、重连或 Host 迁移。 |
| 唯一目标 | 发育期可以战斗/成长/购物而不能占领获胜；之后同一个圈一直开放，不重新关闭、轮换或因无人占领而超时结算。 |
| 连续独占 | 只有仍在本局、存活、非复活保护的参赛真人/AI 计数；野怪、尸体、商店、技能表现不计数。独占必须是同一参赛者连续的一整段，不累计。 |
| 清零条件 | 另一有效参赛者进入、占领者离圈/死亡/离局即破坏连续性；争夺结束后重新计完整一段。中间进出即使发生在同一帧也不能遗漏。 |
| 不清零条件 | 圈内移动、攻击、施法、受伤、控制本身不清零；由此实际离圈或死亡才按相应事件处理。保护者不推进也不争夺。 |
| 唯一结果 | 到期时先处理相关真实事件并证明提供者水位闭合，再重查；成功只锁一个胜者。Host 主动结束无胜者；没有击杀/财富/等级/时间积分胜利。 |
| 出生与复活 | 每次从同一组全部十点独立等概率、有放回抽取；可抽回同点。碰撞只在选中点作必要合法落地/防重叠，不改按危险、视野或敌人距离挑点。 |
| 死亡代价 | 只失去等待时间与场上位置。等级、XP、金币、装备、技能、永久属性加成、未用机会和已生成候选全部保留；可确认三选一，但只更新保留构筑，不提前复活或治疗尸体。 |
| 已释放效果 | 释放者死亡不取消已经成功释放的独立效果，继续到原定结束，后续合法击杀归仍在本局的实际所有者；未完成释放的动作因死亡取消。 |
| 成功复活 | 满 HP/MP，清除死亡前临时控制，获得短暂免伤免控保护；原有冷却按服务器时间继续，不因新 Pawn/新 Life 刷新。 |
| 主动解除保护 | 本人当前 Match/Body/Life、请求新鲜性、包形状及写闸验证后，可识别的进攻意图就先解除保护；即使目标/距离/蓝量等使动作失败也解除。移动不解除。 |
| 离开与新局 | 正式离局立即移除身体，不留可攻击/阻路对象，并取消其未结束独立效果；不回滚此前事实、不发离开奖励、不补AI/自动胜利。Host离开终止会话；新局清全部局内域。 |

已确认补充规则与最终 App Spec 同步：同款两件的基础属性相加、同名被动一次、
两份主动独立冷却；出售/遗忘期间原冷却继续，重购/重学沿用原结束时间，不能洗冷却。
初始为近战单体物理普攻，装备/属性路径可提供群攻或增加攻击距离，不是永久近战单体限制。
死亡可确认选择、进攻意图解除保护、上限变化保持 HP/MP 原百分比也已确认；
生命域不因此治疗尸体、提前复活或改动复活满最终上限的规则。
公式和数值仍由内容与战斗单点维护，不在这里复制来源/叠加公式。
时长与距离只引用 `CONTENT_AND_BALANCE.md` 的正式参数键，不在本报告复制数值。

## 3. 状态机与完整会话路径

### 3.1 一条权威阶段链

目标枚举 `ELDMatchPhase` 归 GameMode：
`Lobby / Starting / Development / CaptureOpen / Results / Resetting / Closing / Faulted`。
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
| `Starting` / 初始化 | 验证已批准内容、地图及各域依赖，预留出生/必要对象与初始记录；全部可提交后统一开始 `Development` | 缺输入/落地/初始化失败不得发布可玩成功；清理未发布对象并退休失败尝试的 Match，发布新一代 Lobby 和明确失败。开始提交前断线使该次启动失败并重准备；提交后按普通离局处理。 |
| `Development` / 开放到期 | 使用服务器截止时刻进入 `CaptureOpen`，只在开放后建立占领连续段 | 仍可升级、购物、战斗；不能把发育期圈内停留时间算入独占。 |
| `CaptureOpen` / 完成提案 | 闭合真实事件边界、重新判定连续性；GameMode 先锁结果和写闸，进入 `Results` | 多个提案不能互相覆盖；未闭合水位不是成功，旧局/旧生命/旧连续段提案拒绝。 |
| 活跃阶段 / Host 结束 | 在权威边界提交 `HostEnded`，无胜者，进入 `Results` | 不借终止挑选最高分；与自然胜利的竞争按批次顺序裁定，不能先发布两个结果。 |
| `Results` / Host 返回 Lobby | 先保持旧闸关闭，进入 `Resetting`；全域失效、拆除身体和局内对象、换 Match 后发布 `Lobby` | 保留已连接房间，不要求客户端伪造一次重连；全真人 Ready 清除，不能自动重开。 |
| 任意房间阶段 / 普通成员离开 | 立即撤销资格/命令、移除身体及碰撞、取消未结束独立效果；其余人继续正常规则 | 不是死亡、不发离开奖励、不接管AI；已发生命中/致死不回滚，已锁事实的结算接收记录不因身体移除而删除。 |
| 任意阶段 / Host 离开或丢失 | 关闭服务器会话；所有客户端撤销输入/观测和 Pending，回前端显示原因 | 不停在失效地图，不迁移 Host，不自动恢复旧房间。 |
| 必需权威准备/提交失败 | 停止受影响边界和新玩法写入，发布工程故障；进入 `Faulted` 或明确中止连接流程 | 不用漏发奖励、跳过事件或默认数据继续判胜。故障退出不是正常完成对局。 |

### 3.2 身份与准入不能由界面代替

真人身份从拥有的 PlayerController/连接解析，服务端创建 `FLDContenderRecord.Session`；
显示名称、头像、数组下标和 Pawn 指针都不是身份。AI 由服务器为其参赛记录分配同类
`ContenderId`，直接调用相同领域验证，不伪造网络连接或客户端 RPC。

未通过准入的连接不能获得可用战斗 Pawn、经济记录或观测权限。
匹配中的名单不允许补位：原连接断开后再连，即使名称/IP 相同也不能接回旧参赛者。
只在明确回到 Lobby 后接受新的加入；这不等于中途重连。

兼容性分三层检查：Unreal 既有网络版本、项目 wire 协议版本、产品版本与内容摘要。
项目须增加自身可解释的版本/内容准入，不声称 Unreal 没有网络版本检查；
`0.10.0` 不是目录 schema、枚举值或协议号。

### 3.3 同世界回 Lobby，不复原旧世界快照

初次建房需要从前端进入 Listen 世界；在房间存在期间，Lobby、比赛和结果转换留在
这个世界内，由 GameMode 阶段驱动身体/服务的创建和清理，连接不换归属。
不把 `OpenLevel` 本地重载当成保持其他玩家连接的返回方案，也不引入
checkpoint/restore、席位交接或第二套跨旅行的战力保存对象。

`Resetting` 必须等每个领域确认旧局对象、回调、回执执行权和观察代理已失效，
然后才发布新一代 Lobby。只能保留房间连接、Host 身份、供展示的名称和本地设置；
不能从旧 OwnerState、Ledger 或 Pawn 拷贝成长、装备、候选、冷却或期限作为新局初值。
AI 数量可以作为房间配置重新展示，AI 的局内记录必须重新初始化。

## 4. 唯一核心身份与写权限

以下类型由本报告单点定义，实施时放在现有 `Framework` 的窄共享头
`LDMatchLifecycleTypes.h`；这是**拟新增文件**，本轮不创建。
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
| `LedgerMatch` | GameMode 为相同新局一次创建的非零 `FGuid`，传给现有 Ledger 初始化 | 与 Generation 显式一一映射；不强转、不从 Generation 伪造 Guid、不接受客户端传入 Guid 改账。 |
| `ContenderId` | 现有 `FLDContenderRecord.Session` 的职责；在当前 Match 内稳定的参赛者 | 生死和换 Pawn 不变；不等于连接、UI 槽位或观察句柄。若房间下一局复用该 Guid，仍必须有新的 Match 才具有意义。 |
| `Victim.EntityId` | 参赛者等于 ContenderId；营地成员由 World 将当前 Match 下的 CampId/MemberId 显式映射一次 | 野怪也只有一个战斗主体身份，不借 Controller 或最后一次攻击者补身份。 |
| `Victim.Life` | 该主体单调生命代次；参赛者由生命域分配，营地成员通过相同核心注册边界分配 | 死亡只将当前生命置无效，不提前多增一次；成功复活/实际营地新生命才增加。回位不是新生命。 |
| `BodyIdentity` | 复用 `FLDBodyCombatState.Identity` 的角色，由 Combat 在新生命注册时生成 | 稳定主体和短寿命身体分开。同一主体生命至多一个绑定；重绑同一 Pawn 不获得第二次死亡/奖励资格。 |
| `DeathKey` | LedgerMatch + Victim(kind/entity/life) | 不含来源、FatalEffectId、DeathSerial 或可变时间；这些只能是事实审计字段，不能扩大去重域。 |
| Order / Epoch / revisions | Navigation 的订单/取消域、Combat token、Build/Inventory/Item 等各有原本所有者 | 不是 Life 的替代，也不合成一个全局“最新版本”；保留各域分别拒旧的目的。 |

GameMode 的 `GetMatchContext()` 是唯一当前映射入口；拥有请求中的网络 Generation
先与当前连接/房间上下文核对，服务器再补齐 LedgerMatch。
旧 Generation 即使携带当前 ContenderId、Life 或新 request ID 也不得执行。
死亡账本和奖励账本使用同一 DeathKey；没有奖励的合法死亡也消费该键，
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
不替代 Ledger 的业务去重或各玩法域的现有请求窗口。

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

| 拟建/改造投影 | 生产者 / 允许字段 | 版本与清理 |
| --- | --- | --- |
| `FLDLobbyView` → GameState | GameMode：本局网络 Generation、阶段、RosterRevision、真人/AI 名单、名称、Host、Ready、AI 数量、可开始状态及公开拒绝原因 | 房间公开；Ready 变化有单独发布 revision。离局移除名单项；新局更新代次，旧 OnRep 不能覆盖。 |
| `FLDPublicMatchView` → GameState | GameMode/Objective：阶段、服务器时间同步锚、开放截止、圈状态、当前占领者、连续段起点/截止、结果身份/原因/唯一胜者 | 圈和结果全场公开，不包含隐藏坐标、生命表、MP、钱包、装备或敌方构筑。客户端只估算显示进度。 |
| `FLDOwnerState` 目标字段 | 本人的 ContenderId、BodyRef/Life、存活/等待/保护、复活及保护截止、已授权的成长/装备/冷却投影 | 只给拥有连接或对应 AI；死亡清理旧身体引用，候选和来源冷却仍来自其持久所有者；换 Match 全部换代。 |
| 观测 DTO | Vision 按当前观察权限发布身体及已批准表现字段；BodyRef 不直接充当永久可用的目标句柄 | 隐藏/死亡/离开/终局撤销；旧句柄不能解析新生命。正常 Host 的原生 Mesh/附属表现与代理一起遵守授权。 |
| 连接错误 / 会话回执 | GameInstance 的当前尝试、拥有者命令回执 | 只回所属本地玩家；取消连接后迟到的旧错误不覆盖新尝试，不清另一个 PIE 世界。 |

“全场占领者”不自动授予该角色坐标或视野。公共圈 DTO 只需无占领者、独占者或争夺状态，
不发布完整圈内实体名单来暴露保护者或隐藏身体。
结算已确认公开所有参赛者的名称、击杀、死亡、最终等级，并标明赢家、结束原因和本局用时；
只在终局冻结后公开，不变成局内实时全员属性页，也不按统计决定胜负。
可见对手的名称/等级/HP/MP/状态/装备只通过当前观察权限显示，
技能/CD/其他属性/金币不在白名单；失视野停止追击，不留 LastSeen 单位标记。
死亡可自由拖看静态地图，但不生成新的活体视野源，不借相机获得隐藏单位。

公共结果使用 `FLDResultView { uint32 Generation; FGuid ResultId; uint64 ResultSerial;
ELDMatchEndReason Reason; TOptional<FGuid> Winner; int64 StartedAtMs; int64 EndedAtMs;
int64 DurationMs; TArray<FLDResultRow> Rows; }`，
由内部 ResultReceipt 投影，**不发送 LedgerMatch**。`FLDPublicMatchView` 的正式时间字段
为 `ServerNowMs / OpenAtMs / SoloSinceMs / DueAtMs`，另带 `PublicRevision`、
`CircleState / Occupier / CaptureEpoch / Result`；没有独占者的时刻使用明确空值，
不是让零 Guid/默认时间冒充一名占领者。名单条目身份、真人/AI、Host、Ready 的字段
由 `FLDLobbyView` 发布；Owner 的时间字段为 `RespawnAtMs / ProtectedUntilMs`。
时间同步锚同时携带 `double ServerSampleWorldSeconds`，来自服务器同一游戏时间采样；
客户端用 GameState 同步的服务器 World 时间相对该锚估算显示，不混用新局零点和世界绝对时间。
Generation/PublicRevision 拒绝旧锚；非活跃阶段按明确阶段停止玩法倒计时估算，
不能由本地估计达到零触发任何服务器动作。

`FLDResultRow` 为 `ContenderId / Name / uint64 Kills / uint64 Deaths / uint8 FinalLevel`，
Winner 的 ContenderId 是唯一赢家标记来源。按开局锁定名单顺序冻结所有行，
不按击杀造排名；离局者仍有行，leave 不加 Deaths。
StartedAtMs 来自成功开局，不以 OpenAtMs 推算；DurationMs 由核心实际开始/结束时刻计算，
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
或 Pawn 数猜测。AI 域拟建 `ALDOpponentController : ALDPlayerController`，
直接复用基类的 StateFor/BuildFor/Holder 路径而不经过 B91/B92；没有 LocalPlayer/UI/网络
连接，不伪造 RPC，也不能落入 NPCBuild。核心注册后才调用其 `BindRegisteredSelf`，
AI 不 friend 直写 Contenders；ReadAISelf/RequestAISnapshot 必须验证注册的 adapter 与 Match。

`ReadAISelf(const FLDAIContenderBinding&) const -> FLDAISelfReadResult` 校验该绑定所属的
注册 Controller 和当前 Match，只返回该 AI 的同一 Owner 投影；
公共阶段/圈用 `ReadPublicMatch() const -> FLDPublicMatchView`。
AI 自己的 `ALDOpponentController::RequestAISnapshot` 只是组合这两个只读入口和获准
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
消费者不能靠回调重新打开闸门。AI 在 Publish 后的获准快照上进行短寿命策略调度，
其结果只提交给下一实际边界的领域意图队列；不在本批后半部回插 Actions，
也不直接获得 `FLDGameplayWritePermit` 改战斗/经济。

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
生命记录切换到 `DeadWaiting`，资格立即无效，取消适用的旧身体动作和交易会话，
设置复活截止；不直接改金币/XP，也不把旧 DeathCost 或掉落调用带进来。
`DeathSerial` 如仍有显示/审计用途，只是派生计数，不参与决定能否再次死亡或发奖。

死亡所有者仍在本局时，独立已释放效果继续至原结束，合法击杀仍可收奖，即使双方同批死亡。
CreditOwnerEligibleAtDeath 在首次锁存时按核心实际时间/因果序认证，死亡不取消在局资格；
Growth 只消费这一冻结资格和关系，不在重试时用更晚的 CurrentInMatch=false 撤销既成奖励。
无主、自杀、离开清理、营地回位不发奖；离局先发生则取消未结效果，不再新产致死。
致死先发生则后来的离局不改 Owner/DeathKey/RewardBasis，也不回滚已经应用的伤害。
因此离局后保留必要的 settlement-only 参赛/账本接收记录直到此前事实处理完成：
只处理已锁事实，不创建身体、被动或新动作，不恢复在局资格。
真正终局闸关闭后仍不补付；必需支付无法完成时走明确 Unpaid/Fault 路径。
金币、XP、跨级升级和新增机会先全量预检/预留再一起提交，不能先成功改账再调用可能失败
的 XP 分支。必需准备失败时事实保持待处理并阻断该权威边界，显式报错，
不能漏发半份奖励后继续宣布占领成功。
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
再另发 ProtectionStarted，制造保护者落圈瞬间争夺。位置事件若先被归约，
未激活的新 Body 仍不具资格，必须等这一原子生命提交。

这里的正常失败必须前置到 Reserve/准备阶段：Finalize 只完成已经预留的提交，
不得再进行可能失败的分配、换点或碰撞挑选。若提交后的内部不变式破坏，显式进入
AuthorityFault，禁止发布复活成功或靠回退 Life/再抽点掩盖；它不是可静默重试的普通落地失败。

初次出生走同一抽点/落地机制，但只进行开局初始化，不借用死亡扣费、返还奖励或刷新旧来源
的路径。Spec 只规定复活保护，不能因为调用了共用 Spawn helper 就无意附加开局无敌政策。
对手 AI 同样抽样并走同一生命规则；野怪使用其营地出生机制，不参加玩家十点抽样。

### 5.3 进攻意图、保护解除和动作边界

`AliveProtected` 和一般战斗 Guard/护盾不是同一状态。免伤免控、可移动、不参与占领
是生命域资格；Combat 读取同一状态，不另存一个可能漂移的“是否无敌”开关。
保护结束必须产生一次资格事件，圈计时从恢复资格后的真实时间开始，不追溯保护期间。

进攻意图先通过外层本人/注册身份、当前 Match/Body/Life、Alive、写闸/permit、
原请求新鲜性及取消水位、合法包形状和精确条款重放检查。
之后只要是可识别的 AttackUnit、AttackMove、普攻、角色主动或装备主动意图，
**立即提交保护解除及圈资格事件，再验证路径/目标/来源/观察/距离/蓝量/冷却/控制等可执行性**。
不能等追到目标、成功 release 或命中才解除。

合法但语义不可执行的 EmptySource、过期 binding、显式 Missing 目标、超距/无路、
冷却/蓝量/控制失败仍解除保护；真正畸形包、未知命令编码、旧 Match/Life、旧 Epoch、
重放/冲突请求和已关闭闸门不改变当前生命。
本地 Armed 预览、普通 Move、Shop、Stop/取消不构成此入口的进攻意图；
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
Kind 只含 `AttackUnit / AttackMove / BasicAttack / ActivateCharacter / ActivateEquipment`。
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
| 等级、XP、金币、装备、技能、永久加成 | 不变 | 全部保留 | 撤销玩法权限；仅为已锁事实保留settlement-only接收，不能接管/新发动作 | 冻结；已提交事务只读重放 | 按批准初始输入全新建立 |
| 选择机会和已生成三张候选 | 关闭只隐藏，不重抽、不自动选 | 保留并可确认；重验当前Life+Phase，只改持久构筑，不治尸体/提前复活 | 清理未决确认，不转给新连接 | 拒绝新确认；不消耗机会 | 全部清空，旧 Offer/确认不能操作新局 |
| 来源冷却 | 不退款、不清零 | 同一来源 readiness 随服务器时间继续；不放在易被销毁的 Pawn 独占存储 | 不再可提交动作；失源规则由内容/战斗明确 | 不再释放；显示冻结/结束由结果 UI 决定 | 新时间域；不继承旧锁，也不接受旧回执 |
| HP/MP、临时控制、保护 | 按真实动作结果，不给UI无敌 | 死亡不战斗；复活满HP/MP清旧控制、原子建立保护 | 立即移除身体及可攻击/阻路碰撞，不计战斗/圈 | 不再治疗、伤害、控制或复活 | 全新身体和生命边界 |
| 商店会话、报价、未提交买卖 | 关闭/停止由商店域取消未提交工作；不撤销已成交事务 | 旧会话失效；死亡不能买卖，复活重新校验范围 | 会话/报价失效 | 拒绝首次提交，无部分改钱/物 | 新会话/报价版本，旧请求明确拒绝 |
| 占领连续段 | 单纯停止/面板/战斗不清零 | 死亡立即清零；保护既不占也不争 | 离局事件清零，不按人数直接胜利 | 一次结果后不再推进 | 回发育期，旧进度/结果不能复用 |
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

操作域的拟建 `FLDAuthorityMotionSegment` 使用 §4 的 `FLDCombatBodyRef`，
以及 `ProviderStreamId / SegmentSequence / WorldRevision / FromFootCm / ToFootCm /
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
`CAPTURE.RadiusCm`，边界包含关系由世界单点定义。圈归约器不重算另一套胶囊半径规则，
也不根据可见集合筛人。

操作域在初次原生推进结束后只调用 `CompleteBodyMotion` 交付基础位置证据；
本批命中仍可能造成位移，因此这不是最终 Motion Fence。全部子段、后续位移和圈查询
成功，且核心进入本边界封闭阶段后，才调用 `PublishThrough` 推进该 Body 的 Motion 流。
静止身体也发布“已处理至此、无新事件”的水位。查询失败不能发布成功水位。
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
| `PublishCaptureEligibility(const FLDCaptureEligibilityEvent&) -> FLDEventReceipt` | 仅 Session/Life 发布 Joined、Left、Died、Respawned、ProtectionEnded 之一；Respawned原子含保护期限，Combat经生命入口触发，不重复发死亡或中间无保护Alive。 |
| `PublishThrough(const FLDProviderFence&) -> FLDFenceReceipt` | 事件先于水位；检查注册流、当前 Match、连续序列和单调时间。重复完全相同水位可幂等，矛盾/退回/越过未发布事件拒绝。 |
| `AckConsumed(const FLDAuthorityBoundary&)` | 只有 Combat 的边界位置读取和 Objective 的归约均完成后通知提供者释放已消费前缀；保留下一段所需末端锚点。 |
| `EndContenderLife(const FLDCommittedDeath&, const FLDGameplayWritePermit&) -> FLDLifeReceipt` | 消费一次 Combat 死亡事实，提交生命/资格和复活等待；不在此改金币。 |
| `EndProtectionForAggressiveIntent(Caller, Match, Body, Intent, Permit) -> FLDProtectionReceipt` | 完整签名见 §5.3；Navigation/Combat 共享，识别新进攻意图即解保护，不等待语义动作成功。 |
| `IsGameplayWriteOpen(const FLDMatchContext&) const -> bool` | 只读快速检查，不是足以绕过正式提交许可的证明。 |
| `ValidateWritePermit(const FLDGameplayWritePermit&, ELDCommitPhase) -> ELDCommitReject` | 最终提交点核对当前 Match、批次、阶段、许可代次和闸门。 |
| `TryCloseMatch(const FLDResultProposal&, const FLDAuthorityBoundary&) -> FLDResultReceipt` | 唯一结果 CAS 语义；占领提案须带当前连续段身份及完整边界证明，HostEnd 须来自已验证权限。 |
| `FailAuthorityBoundary(const FLDAuthorityBoundary&, const FLDAuthorityFault&) -> FLDFaultReceipt` | 必需工作无法完成时锁故障和写闸，明确未支付/未完成；不是正常 Result 或成功 Fence，不补写旧局。 |

`FLDCapturePresenceEvent` 和 `FLDCaptureEligibilityEvent` 都包含同一 BodyRef/Stamp；
后者只允许上表的有限资格变更，不允许调用者自由覆盖整份 Alive/InMatch 状态。
`FLDEventReceipt / FLDFenceReceipt` 明确区分 Accepted、Duplicate、StaleMatch、
StaleLife、UnknownStream、SequenceGap、TimeRegression、AlreadyClosed 和故障；
拒绝不能制造“已发布到此”的成功形状。

提供者注册集合由当前 Match 和身体注册决定：Session、Combat、Life、World、
Progression 各一个服务流，Motion 按当前身体各一个流。没有工作的服务仍封闭空批次。
World/Progression 水位分别证明本边界营地生命/刷新及致死复合奖励/事务已经处理，
不是让 Objective 读取它们的私有业务表。
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
待处理 DeathFact 不得被压缩掉，领域业务回执按成长/账本自己的保留规则处理，
核心不复制另一份无限账史。长期循环验收须证明退休缓存不随累计死亡次数线性增长。

### 6.4 固定批次顺序及提交许可

现有 `ALDCombatDirector::AdvanceAuthority / Sample` 收敛为唯一驱动入口，
由 GameMode 核心状态规定下面的顺序；领域不能通过独立 Actor Tick 越过阶段提交。
批次按最早尚未关闭的时间边界前进，必须包括已知开圈、保护/复活、效果到期、
圈完成及营地到期边界。不能先提交截止之后的金币/伤害，再回头检查较早的胜利。

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
| 0 `Session` | 处理当前真实子时刻的离开/准入失效/HostEnd；保护到期和开圈条件在此刻动作前生效 | 不把桶内较晚离局提前，不回滚已提交事实；不用客户端拔线时间。HostEnd先锁则其后玩法不执行，既有结果不覆盖。 |
| 1 `Motion` | 排空原生实际运动/位移证据，取得本边界合法位置和全部圈穿越；处理此前阶段安排的位移 | 不将帧末位置当成整个时间段的位置；未有证据就不能进入需要它的命中/交易。 |
| 2 `Actions` | 先外层身份/shape/请求/permit，再识别进攻意图并经Life解除保护；之后动作可执行性准入、可到期release和成功消耗/CD | 语义失败仍可有ProtectionRemoved；旧/畸形包不解，失败不付资源；不能等有效目标/成功release才解。 |
| 3 `Effects` | 先按同一边界姿态收集到期命中，再提交伤害/控制和首次零 HP 锁存；位移通过原生运动路径补充实际事件 | 不因处理第一个死者而抹去另一份同刻已到期命中；不跳过必需工作；普通非致命伤害/原地控制不清圈。 |
| 4 `DeathsAndRewards` | 当前Effects致死集合全锁RewardBasis后，消费各DeathKey生命失效并完成该集合复合奖励或明确零奖 | 同集合互杀不因先派奖改变另一死亡基准；必需准备失败不封水位，不越过它宣布胜利。 |
| 5 `RespawnsAndWorld` | 到期复活/新身体保护，营地实际新生命与刷新；为新身体建立位置/资格，退休旧流 | 死亡/离局/闸关闭重查；失败不安全重抽、不产生半个生命或离开奖励。 |
| 6 `Transactions` | 当前选择/替换与买卖；按当前生命/阶段/版本/位置重查，一次原子提交派生属性与来源 | 死亡或离店不能使用阶段前的快照成交；旧生命确认不能自动作用复活身体。 |
| 7 `Capture` | 确认所有提供者前缀及本批工作闭合，依真实发生次序归约全部中间事件，重查完成资格 | 不只看最终人数；不能略过先进入后死亡的争夺事实。 |
| 8 `Close` | 先以唯一提案锁结果、关闭全域写闸、推进许可代次 | 禁止先调用取消服务再设置 Finished；已锁结果不再覆盖。 |
| 9 `Publish` | 清理/取消、最终授权投影、回执及结果；确认消费并释放证据 | 清理不得提交伤害/奖励/买卖/选择，不把只读回执重放当作新交易。 |

阶段 0 的保护到期使用半开保护区间：到期时刻本身已经不受保护，
因此同刻来袭伤害可以命中，同刻圈内保护到期可以形成争夺。
新复活的保护从**成功生命提交**时开始，不能由客户端倒计时或排队预约时刻提前计入。

同一毫秒的先后仍先按原始发生时间，再按真正同刻的因果/CommitOrdinal 保留。
例如保护者在 1000.8ms 已离圈、1001ms 才保护到期，两事件虽同在 1001 桶，
仍必须先离圈再解除保护，不能因 Session 排在 Motion 前就虚构短暂争夺。
又如运动中有效竞争者先进入、
本批后被击杀，已有的争夺必须清零；若其死亡在更早事实中已经提交，后面的尸体位置
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

### 6.5 连续占领归约与唯一结果

Objective 持有当前有效集合、`CaptureEpoch`、独占者、`SoloSinceMs` 和 `DueAtMs`；
它们是同一归约器状态，不是每个角色一条可累计的占领进度。
每次破坏连续性都推进 Epoch；结果提案包含 Match、CaptureEpoch、占领者、
DueAtMs 和已闭合的 Boundary。旧 Epoch 的 Timer 回调永远不能为新一段计时完成。

开放前归约位置/资格，但不累计；到开放时刻建立新的计时段。
开放后空集合或至少两名有效者使进度归零；从这些状态变为唯一有效者，从该次变更开始。
唯一占领者不变的圈内移动/战斗不重置；同一帧中先争夺再恢复独占也必须新开一段。
保护者进出不会改变有效集合；其保护结束则是独立资格事件，不能等到下一次碰撞回调。

`TryCloseMatch` 的占领成功条件必须同时包括当前上下文、写闸开放、
已封闭至 DueAtMs 的相关事件前缀、当前未过期的 CaptureEpoch、
该时间边界仍连续独占，以及没有未处理的必需死亡奖励/派生工作。
只通过 Timer 或 UI 倒计时不是完成证明。提交一次
`FLDResultReceipt { Match, ResultId, ResultSerial, Reason, OptionalWinner, EndedAtMs }`，
GameState 与 Objective 只读同一结果，不各写一个赢家。

HostEnd 按实际受理时刻关闭，不宣称已经封闭其后尚未执行的毫秒桶；
只能在更早已提交的必需工作全部结清后受理，无赢家且不等待未来输入。
结果内部保留准确开始/结束时刻，EndedAtMs/DurationMs 为对外安全毫秒投影，
不借较晚整数上界延长对局或继续提交玩法。

结果统计归 GameMode 私有参赛记录，从同一已提交 DeathKey 派生一次：
参赛者真实死亡增加 Deaths；具有合法实际归属的另一参赛者击杀增加 Kills，
无主/自杀不指定击杀者，离开/重置不加死亡，野怪不进入该参赛者 K/D 统计。
同一键重放不重计。Growth 只提供最终已提交 FinalLevel，不再维护第二份 K/D。
锁结果前准备完整 Rows/时间，锁后只发布该冻结快照；离局者的合法结算完成后仍有结果行，
不能从已销毁 Pawn、killfeed 到达次数、当前公开名单或客户端镜头推算。

关闭后延迟伤害、控制、回复、奖励、选卡、交易、营地刷新、复活和占点一律没有新提交权；
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
也不让 Objective 跳过该故障获得胜利。已支付的事务仍只能只读重放。
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
| `MATCH.DevelopmentSec` | 有界时长，作者秒；GameMode/Objective/UI | 开始提交后建立开放截止；到期前不能获胜，开放后不再关闭。旧 SourceData 发育值不能作缺值 fallback。 |
| `CAPTURE.HoldSec` | 有界时长，作者秒；Objective | 从当前连续段开始建立完成截止；争夺/离圈重开完整一段，非致命战斗不重开。 |
| `LIFE.RespawnSec` | 有界时长，作者秒；生命域 | 当前内容候选为常量，不按等级/人数变化；这是待审内容方案，不自行加入缩放公式。死亡事实建立期限一次，重复死亡不再延长或缩短。 |
| `LIFE.ProtectionSec` | 有界时长，作者秒；生命/Combat/Objective/UI | 成功复活建立保护期限；到期等号、合法主动解除、不能占圈均按 §5/6。 |
| `CAPTURE.RadiusCm` | 厘米，脚点 XY 中心的圆半径；World 查询 | 使用世界统一边界包含规则，不在 Objective/Combat/UI 分别加减碰撞半径或容差。 |
| `UNIT.CollisionRadiusCm` | 厘米，参与者碰撞尺度；World/原生身体 | 只服务必要合法落地/防重叠及碰撞，不据此扩大圈、加危险评分或重抽锚点。 |
| `POLICY.InvalidAggressionProtection` | 已确认 `BreakOnAggressiveIntent`；Combat/生命 | 外层身份/当前生命/shape/请求/闸门验证后，进攻意图即解保护；路径/目标/来源/资源等语义失败也解除，具体入口见§5.3。 |
| `POLICY.DeadChoiceCommit` | 已确认 `Allowed`；成长/生命/UI | 死亡可确认，只改持久构筑；重验Life+ExpectedLifePhase，不能提前复活/治疗尸体，不能执行死前Alive确认。 |
| `POLICY.ReleasedEffectSourceLoss` | 已确认独立已释放效果的死亡存续；Combat/内容 | `ContinueForRemainingContender`：释放者死亡仍继续原期限，合法信用归仍在局者；未完成release取消，非独立附属效果按内容定义，不扩大规则。 |
| `POLICY.SourceLeave` | 已确认离局行为；Combat/世界/生命 | `CancelUnsettledNoRefund`：立即移体/碰撞、取消未结束独立效果；不回滚已发生事实，先前认证奖励仅settlement-only结清，不发离开奖励。 |
| `POLICY.MissingSourceCooldown` | 已确认 `ContinueServerTime`；Combat/成长 | 出售/遗忘期间也继续原冷却，重购/重学沿用原结束时间；两同款主动仍各自独立，不能因来源重建清零。 |
| `POLICY.ResourceMaxChange` | 已确认 `PreserveRatio`；Combat/成长 | 存活者构筑/装备改变上限按原百分比投影；死亡选择不治疗尸体/提前复活，正常复活仍满最终HP/MP。精确取整/定义域/下溢拒绝按内容公式，不在此复制。 |

时长的可接受范围、资源公式、叠加及初始值由内容定义；本域只规定有限值、单位、
精度和安全加法必须验证，溢出不能产生“立即到期”或无限保护。
从 docs 到运行输入必须沿批准定义 → SourceData → 对应 Sync → 载荷/contract →
原生导入和开局校验的同一链路；不让游戏读取本文，不手填生成哈希。

死亡静态自由镜头、不新增活体视野、无LastSeen标记、可见对手字段及终局统计
已确认，按§4白名单接线，不能继续列为未定。
基础普攻近战单体及经装备/属性获得群攻、射程成长已确认，
具体内容/数值仍需批准，不从这项形态规则擅定伤害或距离。
目前本模块没有另需用户选择的生命周期分支；仍待审的是中央具体数值/目录及相应内容定义，
只阻塞其消费切片，不阻塞 B01 准入/代次/失败合同或独立 B02/B03 闭包。
同世界阶段选择、毫秒转换、HostEnd 的工程先后、类型命名、请求窗口及水位证明
已经是本报告的工程设计，不再当作需要用户拍板的玩法问题。

## 8. 具体正反验收

**本节是后续实施的场景设计，均未在本轮运行。**
`H` 表示有正常主画面的参赛 Listen Host；`C1/C2` 是独立进程、真实拥有连接的客户端；
`A1–A3` 是实际参赛 AI。真实连接不是调用一下服务器函数或只有 Host 的调试世界。
完整场景另外分别记录 LAN 与正确配置后的公网结果，同机回环不能代替它们。

纯规则夹具可以注入确定的时间/随机流/故障，验证精确边界；集成验收必须让事件从
原生运动、拥有者 RPC、实际命中和真实断开入口产生，不能只往 Objective 填一张最终人数表。
完整 Demo 场景必须用批准内容正常普攻发育、成长、购物和争圈，
不使用调试授予、传送、固定胜者或强制完成按钮。

### 8.1 房间、开始和失败

| ID | 前置和动作 | 精确正反断言 |
| --- | --- | --- |
| SM-01 | H+C1+C2，选择三 AI，所有真人 Ready 后 H Start | 六名独立参赛者、含 Host 的三真人上限；单次初始化；AI 不是队友。再加入第四真人明确 RoomFull；三 AI 不挤占真人计数。 |
| SM-02 | H 单人、零 AI；另覆盖未用满的真人和 AI 名额 | 不补满，不要求两真人；练习仍须完成发育与完整独占，不自动判 H 胜。 |
| SM-03 | 一真人未准备；C1 发 Start/改 AI 数/结束；伪造别人 Ready | 分别 NotAllReady/NotHost/NotOwner，无状态副作用；Host 自己也必须 Ready。 |
| SM-04 | 同一结构版本 Ready 并发；Ready 撤销/AI 改数/最终 Join 与 Start 竞争 | Ready 位更新不会无限使其他 Ready 过期；结构变更清准备；Start 的原子锁定点决定唯一名单，PreLogin 先通过也不能晚入局。 |
| SM-05 | 两连接同时争最后名额；重复 Start；同请求 ID 改条款；回执过窗 | 不超员、不双启动；同键重放一次，改条款拒绝，过窗不再执行且能查询当前状态。 |
| SM-06 | 取消加入，立即重试；旧尝试迟到成功/失败 | 无幽灵席位，无迟到成功重新开房，无旧失败清掉新连接；错误归属当前 WorldContext/Attempt。 |
| SM-07 | 非法 IP/端口、不可达目标、wire/产品/内容分别不兼容、已开始后加入 | 明确区分对应错误；回菜单可重试；不笼统断言超时一定是 NAT，不静默回落旧 profile。 |
| SM-08 | Starting 中注入必要输入/身体/落地准备失败或成员离开 | 没有局部玩家可战斗、部分初始资源、旧营地和计时残留；失败 Match 退休，回新 Lobby 重准备；旧启动回调不能污染重试。 |
| SM-09 | C1 在 Lobby/活跃局分别离开，再用同名同 IP 加入 | Lobby 可为下一局新准入；活跃局拒绝晚加入/重连，无席位接管或 AI 补人；离开不发奖、不按剩余人数判胜。 |
| SM-10 | 对照 H 正常死亡、H 主动结束、H 离开、H 进程丢失 | 死亡不关闭服务器；主动结束无赢家且可回 Lobby；连接丢失时 C1/C2 回前端并获原因，不移交 Host。 |

### 8.2 生命和保护

| ID | 前置和动作 | 精确正反断言 |
| --- | --- | --- |
| LF-01 | C1 经正常成长获得技能/被动/永久属性、同款两装备、金币及当前待选候选后死亡 | 全部持久状态保留；无死亡税/掉落/初始奖励重发；复活派生不重复加一次装备或永久属性。 |
| LF-02 | 同 VictimLife 重复致死；之后复活再次死亡 | 前者一个 DeathKey、一份等待和复合奖励；后者新 Life 产生另一合法死亡，不能被只按参赛者去重吞掉。 |
| LF-03 | H/C1/AI 带未结束来源冷却死亡和复活；raw Now=1000.2ms、ReadyAt=1001ms | 剩余时间继续且只在真实到期可用；不能用ceil Now提前，也不能因新Pawn/槽位/BodyIdentity清零；失源后重购/重学同样不洗。 |
| LF-04 | 固定无偏 RNG 夹具覆盖十个输出及拒绝区间；相同随机流下改变敌人距离/视野；强制连续相同索引 | 仍使用完整十点均匀采样；改变威胁状态不改变抽点；允许同人连续同点和多人同点。不能以小样本次数恰好相等作为均匀性证明。 |
| LF-05 | 同点多 H/C/AI，注入 Reserve 失败/重试、重复 Finalize | 在选中锚点局部合法落地；重试不增加抽样次数、不多增 Life；Finalize 无第二次初始化。准备失败明确报错，内部提交不变式错误走 AuthorityFault。 |
| LF-06 | 复活后立即读取身体、Owner/UI 和圈状态 | 同一新 Body/Life，满最终上限 HP/MP、无旧临时控制；保护期限来自成功提交，保护者不占不争。 |
| LF-07 | 保护中移动/受击/控制；确认AttackUnit/AttackMove/QW/12且目标缺失/超距/缺蓝/过期binding；对照旧包/畸形包；到期同刻受击 | 移动/预览不解；可识别新意图在语义验证前解并使圈有效，Rejected仍带LifeRevision而无资源/CD；旧/畸形包不解；自然到期等号不免疫。 |
| LF-08 | 延迟死亡前移动/施法/交易/确认到复活之后；同刻死亡+复活 Timer/离局/结束 | 旧生命请求无效，候选保留不赋予旧请求执行权；终局/离局后不创建新身体。 |
| LF-09 | 同Effects集合互杀且会升级；死后独立效果命中；致死1000.2ms/离局1000.8ms及相反顺序 | 基准全先锁、死者合法收奖；致死在前则EligibleAtDeath不被后离局撤销，settlement-only结清；离局在前取消效果无新死，不按同桶阶段倒排。 |
| LF-10 | 死亡确认/替换、迟到Alive确认、上限成长；离局有身体/独立效果 | Dead确认只改保留构筑，旧Phase拒绝，尸体不治；复活满新上限；离局立即移体/碰撞、取消未结效果，不回滚先前事实。 |

### 8.3 连续占领与实际事件水位

| ID | 前置和动作 | 精确正反断言 |
| --- | --- | --- |
| CP-01 | 发育期一直站圈，跨开放时刻 | 开放前无进度/胜利；从开放边界才开始新段，期间仍能成长/购物。 |
| CP-02 | 空圈→独占→两有效者争夺→原占领者独占 | 无人/争夺归零；原占领者也重新完整计时，不保留累计时间。 |
| CP-03 | 圈内走动、普攻、施法、装备主动、非致命伤害和原地控制；对照击退出圈/致死 | 前组不改变连续段；后组必须清零。 |
| CP-04 | 唯一保护者；独占者旁保护者；受保护新生命落圈；圈内野怪/尸体/商店/特效 | 无效实体不占不争；新生命不短暂暴露无保护Alive；代理不双计数；保护结束才具资格。 |
| CP-05 | 同一原生步中占领者离开再返回；竞争者进入再离开；滑移/绕角产生多次穿越 | 最终位置相同也保留中间变更，旧段作废；不能只用步首/末快照。 |
| CP-06 | 同刻竞争者先有效进入再死亡；对照先死亡再经过 | 前者已破坏连续性，即使末态只剩原占领者也不赢；后者不计尸体。旧生命事件不能因本批新 Life 已登记而被抹掉。 |
| CP-07 | 在 `DueAtMs - 1`、`DueAtMs`、`DueAtMs + 1` 分别发生进入/离圈/死亡/离局 | 前两组阻止原胜利；之后事件不得回填推翻已合法锁定的结果。使用实际服务器发生时间，不用客户端报告。 |
| CP-08 | 圈内保护者恰在完成时刻到期；对照在完成之后到期；本时刻只有非致命伤害 | 同刻保护到期形成争夺；后到期不倒灌；单纯非致命伤害仍可完成。 |
| CP-09 | 非整数毫秒交点略早/正好/略晚于截止；同毫秒两次穿越；保护者1000.8ms离圈、1001ms到期 | ceil不把截止后事件归前；先原始时间再真正同时因果序；最后一例不虚构争夺，不能按Session/Motion阶段逆序。 |
| CP-10 | Motion 静止、查询失败、序号缺口、重复水位、旧 Match/Body、晚到未来事件；一段穿圈产生入/出两事件 | 静止能封空水位；失败/缺口不能判胜；几何段序不作两个事件的共同去重键；幂等重放不清当前段，未来事件不补当前缺口。 |
| CP-11 | FScopedMovementUpdate 试探后回滚、客户端 correction、真实 root motion/击退 | 前两者不发布权威穿越；后两者经同一真实原生生产链发布，不另积分。 |
| CP-12 | 延长帧/延迟效果/关闭边界前缺少相应位置证据 | 未闭合前不宣布胜利；不拿当前姿态补历史；明确 GeometryUnavailable/边界故障，而非默认为无人或 Miss。 |
| CP-13 | C2 看不到占领者；AI 根据公共进度移动争夺；长期无人完成或所有人暂死 | 圈状态仍公开，但不泄露隐藏坐标/构筑；AI 可同规则胜；无击杀/财富/时间/人数胜利。 |

### 8.4 终局、故障和下一局

| ID | 前置和动作 | 精确正反断言 |
| --- | --- | --- |
| FN-01 | 同截止边界有动作、到期致死、金币+XP、选择、购买/出售 | 严格执行 §6 单序；完整闸前提交保留一次真实回执，闸后首次提交拒绝；不会边出结果边发钱或换装。 |
| FN-02 | 奖励准备的一侧失败、事务派生触发同刻战斗、未处理因果子批次 | 无半奖/默认成功；Progression 不提前封水位；一次受控准备重试仍失败则 Faulted/明确未支付/回前端，不正常判胜、不伪造NoReward、不关后补奖。 |
| FN-03 | 预先取得 permit，随后终局/新局，再触发异步回调；在 CloseCombat/取消中尝试发奖 | 最终提交 ValidateWritePermit 拒绝；清理没有玩法写权，不依赖早先 bool 检查。 |
| FN-04 | 重复/冲突占领提案、旧 CaptureEpoch 回调、HostEnd 与完成同刻、完成后再 HostEnd | 只一个结果和 ResultId；阶段 0 接受 HostEnd 时无胜者，已锁胜者不能被后续 HostEnd 覆盖。 |
| FN-05 | H 带 C1/C2 回同世界 Lobby，再重新准备开始 | 拥有连接保持；Match/账本 Guid 换代，全部局内状态、营地/AI/效果/候选/来源清空；Lobby 期间不继续结算旧游戏。 |
| FN-06 | 在新局重放旧移动、伤害、Offer、报价、事务、观察、复活、圈及结果消息 | 全部失去新提交/覆盖权；同 ContenderId 或数值 Life 也不能跨 Match 混用；名称和本地设置仍保留。 |
| FN-07 | 必需工作发生精度/计数溢出、积压或不变式失败；另作固定实体反复复活长测、普通非法请求对照 | 无环绕/默认/假胜者；退休缓存按实体和未消费窗口有界；普通请求拒绝不终止全局；真正故障退出不记正常完成。 |
| FN-08 | H+C1+C2+A1–A3完整对局后再开；LAN/公网；途中离局、killfeed重放 | 正常循环无调试捷径；结果含锁定名单全部行/唯一赢家/原因/真实用时，leave不增Deaths，重放不重计K/D；不公开金币/完整Build。 |
| FN-09 | 工程夹具中首个Effects集合派奖令A升级；Transactions合法因果效果随后杀A | 后续集合锁当时新等级，各集合分别先锁后奖；首个集合基准不被回读修改，不要求预知后续死亡；没有同DeathKey重复奖。 |

场景输出应记录本次输入摘要、连接归属/拓扑、Match 映射、相关 Body/Life/DeathKey、
时间/流序/水位、提交阶段及回执，不向玩家错误信息泄露这些内部审计字段。
输出写忽略的 `Artifacts`；本报告不引用旧输出作为当前通过证据。

## 9. 源码证据与删除、改造、抽取闭包

独立工程研究比较了基线与当前文档 HEAD：`Source`、`Config`、`SourceData`、`Content`、
`Scripts` 和 `Lockdown.uproject` 没有运行差异。下列行号属于 `fa11d09`。
“原样复用”仅指列出的局部机制，不授权保留其整个旧模块/政策。

### 9.1 现有事实和目标替换点

| 编号 / 当前真实入口 | 已核验事实 | 改造/抽取/删除与前置 |
| --- | --- | --- |
| E01 [LDUIRoot.cpp:368–396](../../Source/Lockdown/Private/UI/LDUIRoot.cpp#L368-L396)、[DefaultEngine.ini:3–7](../../Config/DefaultEngine.ini#L3-L7) | Host 固定旧 `L_DevelopmentRegion?listen?Profile=V05`；Join 走 ClientTravel；Leave 本地 OpenLevel 前端，没有同房间新局入口 | 改造 R13/N01/D01/D16。首次建房 travel 保留，旧 Profile 选择真删；回 Lobby 改同世界权威重置，不保留本地重载兜底。 |
| E02 [LDGameInstance.cpp:8–45](../../Source/Lockdown/Private/Framework/LDGameInstance.cpp#L8-L45) | 有网络/旅行失败委托及 pending-driver 的 GameInstance 归属核对；NetworkFailed 丢弃传入 Error，反馈仍粗略 | 保留归属/弱回调，改造连接 Attempt、错误分类和旧回调隔离。不能把一次失败归到其他 PIE 世界。 |
| E03 [LDGameMode.cpp:144–179](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L144-L179)、[315–359](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L315-L359) | 多 profile、旧四人上限、PreLogin/PostLogin 二次检查，项目未覆写 Login；PostLogin 初始化职业 Build | R06/N01：三真人准入的共用检查、Starting 锁、最终注册前复查。职业初始化和旧容量删除，AI 独立计数，不从已有 Pawn 数推席位。 |
| E04 [LDGameMode.cpp:211–310](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L211-L310)、[490–519](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L490-L519) | 世界启动生成旧服务/居民；开始启用旧经济/银行/制作和 MatchDeadline，没有为全体重新随机初生 | D01/N01：明确 Starting 预备/全量提交；不再次调用 StartPlay 冒充 Reset，不启旧世界来获得圈时钟。 |
| E05 [LDGameMode.cpp:223–226](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L223-L226)、[378–386](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L378-L386)、[LDEconomyDirector.cpp:19–45](../../Source/Lockdown/Private/Economy/LDEconomyDirector.cpp#L19-L45) | Vision 从 GI 增 Generation，Owner 从 Vision 抄 Match；Economy 自行 NewGuid 初始化 Ledger | R06/R08：反转为 GameMode 单点上下文向各域下发。删除 Economy 自行选局 Guid，不把投影当身份来源。 |
| E06 [LDGameMode.h:78–91](../../Source/Lockdown/Public/Framework/LDGameMode.h#L78-L91)、[LDViewTypes.h:67–165](../../Source/Lockdown/Public/Vision/LDViewTypes.h#L67-L165) | 私有参赛记录/OwnerState 混有成长、库存、生命和旧任务；没有目标待选队列 | 保留唯一私有状态和 SyncOwner 投影，去 Controller/Pawn 生命周期耦合；成长域接自己的实际存储，禁止第二钱包/候选表。 |
| E07 [LDCharacterMovementComponent.cpp:22–101](../../Source/Lockdown/Private/Character/LDCharacterMovementComponent.cpp#L22-L101)、[LDNavigationComponent.cpp:96–150](../../Source/Lockdown/Private/Navigation/LDNavigationComponent.cpp#L96-L150)、[237–319](../../Source/Lockdown/Private/Navigation/LDNavigationComponent.cpp#L237-L319) | 有 saved move 身份、禁止跨代合并、Order/Epoch/确认/心跳；导航仍有 local-controller 假设，OnMovementUpdated 不记录完整轨迹 | 原样复用 R01/R02 的身份/预测机制，改造 R05 的服务端适配与真实子段。删除旧分派，不增加第二移动积分器。 |
| E08 [LDCombatDirector.cpp:206–224](../../Source/Lockdown/Private/Combat/LDCombatDirector.cpp#L206-L224)、[254–388](../../Source/Lockdown/Private/Combat/LDCombatDirector.cpp#L254-L388)、[LDWorldClock.cpp:9–28](../../Source/Lockdown/Private/World/LDWorldClock.cpp#L9-L28) | Finished 前仍采环境，Finished 分支仍调用服务；旧 WorldRuntime 才驱动时钟，临时改写 World 时间，缺流/水位 | R07/D10/N02：保留到期推进/重入目的，按 §6 替换 driver 与全部写入口；旧 hold/回拨/依赖整个 WorldRuntime 的政策真删。 |
| E09 [LDTacticalRules.cpp:4](../../Source/Lockdown/Private/Combat/LDTacticalRules.cpp#L4) | 现有 Milliseconds 使用 floor | 事件转换不能原样使用；集中 checked ceil，精确整数期限不经浮点往返。存活消费者统一迁移，不能保留两种圈时间解释。 |
| E10 [LDObjectiveDirector.cpp:93–171](../../Source/Lockdown/Private/Objectives/LDObjectiveDirector.cpp#L93-L171)、[234–360](../../Source/Lockdown/Private/Objectives/LDObjectiveDirector.cpp#L234-L360)、[LDWorldClaims.cpp:8–28](../../Source/Lockdown/Private/World/LDWorldClaims.cpp#L8-L28) | Claim 有授权/路线/静止/hold/deadline，技能/控制可打断；Finish 先取消再写 EndReason | D08/D09/N02：Objective 原位替换为唯一圈 reducer 和提案，结果所有权接 GameMode；删 Claim/路线/RPC/旧 UI，不留旧 fallback。 |
| E11 [LDGameMode.cpp:181–194](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L181-L194)、[413–440](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L413-L440)、[LDRegionDefinition.cpp:47–63](../../Source/Lockdown/Private/Region/LDRegionDefinition.cpp#L47-L63) | 初生 TakeSpawn 抽后 RemoveAtSwap，无放回且有硬编码兜底；复活筛 Safe/Farthest；布局带旧 City/Heart 条件 | R06/R11/C2：改接世界十点统一采样/Reserve；删随机池移除、安全最远策略和旧缺图默认位置。 |
| E12 [LDGameMode.cpp:388–411](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L388-L411)、[441–458](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L441-L458)、[LDInventoryDirector.cpp:78–103](../../Source/Lockdown/Private/Inventory/LDInventoryDirector.cpp#L78-L103) | 死亡先回调后 inactive，并有 DeathTax/掉落；复活先增 Life 后失败回退；库存死亡仍解绑/掉落/消耗 | M5 先失效旧身体再取消，改为准备后生命原子提交；删死亡税/掉落/生命回退。成长装备所有者同步保留，不能只删一条扣钱语句。 |
| E13 [LDCharacter.cpp:63–74](../../Source/Lockdown/Private/Character/LDCharacter.cpp#L63-L74)、[25–30](../../Source/Lockdown/Private/Character/LDCharacter.cpp#L25-L30) | BeginPlay 自动注册 Combat，Pawn 对 Pawn Ignore | 准备中的身体不得自动参战；接预备/激活边界。同点防重叠需真实实现，不能宣称现有碰撞已解决。 |
| E14 [LDSkillExecution.cpp:17–33](../../Source/Lockdown/Private/Combat/LDSkillExecution.cpp#L17-L33)、[LDGameMode.cpp:522–544](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L522-L544) | 有持久 Build/readiness 基础，但身体 BasicReady 等并未完整跨死恢复；AwardExperience 缺阶段检查 | 抽取必要来源冷却到仍在局内的持久所有者；新身体只投影。同一奖励入口必须验证 permit，不能继续直写 XP。 |
| E15 [LDTacticalExecution.cpp:31–59](../../Source/Lockdown/Private/Combat/LDTacticalExecution.cpp#L31-L59)、[LDContentActions.cpp:45](../../Source/Lockdown/Private/Combat/LDContentActions.cpp#L45)、[LDContentGeometry.cpp:57–62](../../Source/Lockdown/Private/Combat/LDContentGeometry.cpp#L57-L62) | ApplyStatus 缺统一复活保护检查，主动入口反而拒绝保护者；CancelTactical/DestroyField 可改 readiness/库存/账务 | C1/R07：进攻意图外层验证后即解保护，语义失败也生效；取消拆成 cancel-only，所有副作用经最终闸验证。不能仅给 Finish 补 bool。 |
| E16 [LDWorldDirector.cpp:90–110](../../Source/Lockdown/Private/World/LDWorldDirector.cpp#L90-L110)、[LDEconomyLedger.cpp:36–100](../../Source/Lockdown/Private/Economy/LDEconomyLedger.cpp#L36-L100) | 旧 LastOwner/LastDamageOwner 信用、Reward 另造事务 ID；Ledger 有精确条款重放/原子性但无终局概念 | N03/R08/R10：Combat 固定 DeathKey/所有者，成长复合提交；删旧信用兜底/限额。Ledger 机制可复用，不承担第二生命/胜负。 |
| E17 [LDVisionDirector.cpp:150–158](../../Source/Lockdown/Private/Vision/LDVisionDirector.cpp#L150-L158)、[LDWorldViewRelay.cpp:100–126](../../Source/Lockdown/Private/Vision/LDWorldViewRelay.cpp#L100-L126) | Register 发现旧观察者直接返回；relay 能换代清代理 | 新局须显式清服务器探索/句柄表，不只改 Generation；复用客户端换代清理，不开放全 PlayerState/身体复制绕过隐私。 |
| E18 [LDB92Roster.cpp:32–33](../../Source/Lockdown/Private/Population/LDB92Roster.cpp#L32-L33)、[117–199](../../Source/Lockdown/Private/Population/LDB92Roster.cpp#L117-L199)、[LDGameMode.cpp:373](../../Source/Lockdown/Private/Framework/LDGameMode.cpp#L373) | B92 是持久席位/人接管/离线转 AI；旧 Logout 可重选 LobbyHost | D06：删旧接管闭包；M12 接真实参赛 AI；本流程不移交 Host，不拿 seat 当自主 AI。 |

### 9.2 本模块触及的有限文件组

表中 `Public/Private` 是 `Source\Lockdown` 下的实际头/实现配对，并非对目录执行通配删除。
共享文件由对应所有者协调改造；本轮只列计划，不执行任何一项源码/资产删除。

| 类别 / 架构编号 | 文件、符号与调用者 | 替换和删除顺序 |
| --- | --- | --- |
| 原样局部复用 R01/R02/R08 | `Public\Core\LDRules.h::FViewCursor`、`Public\Framework\LDCommandLiveness.h::Fresh`、`Private\Character\LDCharacterMovementComponent.cpp` saved move/serialize、`Private\Economy\LDEconomyLedger.cpp::Commit/Receipt/Reconcile` | 保留当前拒旧/预测/原子性；同文件旧 DeathCost/Heart/ForProfile 和无消费者字段另删，不整包原样保留。 |
| 改造 R06/R13/N01 | `Public/Private\Framework\LDGameMode.h/.cpp`、`LDGameInstance.h/.cpp`、`LDPlayerController.h/.cpp`；`Public/Private\UI\LDUIRoot.h/.cpp`；`Public\Vision\LDViewTypes.h` | 先连接命令/DTO/唯一 Context/阶段/错误与 Reset，再断旧职业/容量/profile/主机重选；UI 和生产者同批更新。 |
| 改造/抽取 R05/R07/R10/N02 | `Public/Private\Combat\LDCombatDirector.h/.cpp`、`Private\World\LDWorldClock.cpp`、`Public/Private\Objectives\LDObjectiveDirector.h/.cpp`、`Public/Private\Navigation\LDNavigationComponent.h/.cpp`、`Public/Private\Character\LDCharacterMovementComponent.h/.cpp` | 先接完整 step/Body/流/permit/单圈生产消费者，再删旧时钟分派、hold/deadline；不能先删时钟后由 UI 接管。 |
| 本域拟新增 | `Public\Framework\LDMatchLifecycleTypes.h`；`Private\Framework\LDMatchLifecycle.cpp`；现有 Tests 下 `LDMatchLifecycleTests.cpp` | 前者仅共享窄类型，后者是现有 GameMode 的实现拆分，不新增 Manager；测试验证真实契约。只有实施任务才创建。 |
| 协同改造 C1/M4/M9G | `Private\Combat\LDTacticalExecution.cpp`、`LDTacticalFields.cpp`、`LDContentActions.cpp`、`LDContentRelease.cpp`、`LDContentLifecycle.cpp`、`LDContentGeometry.cpp`、`LDSkillExecution.cpp` | 核对所有状态/击退/落地/效果销毁和取消回调；接同一运动/生命/闸门。保留效果集合按批准目录，不整包默认保留。 |
| 协同改造 M6–M9 | `Public/Private\World\LDWorldDirector.h/.cpp`、`Economy\LDEconomyDirector.h/.cpp`、`Inventory\LDInventoryDirector.h/.cpp`、`LDVendorDirector.h/.cpp`、`Development\LDCharacterBuild.h/.cpp`、`LDDevelopmentDirector.h/.cpp` | 先切到相同 DeathKey/Match、预备提交及全保留/Reset，再删除旧 LastOwner、PvP 限额、税/掉落和各自另造局 Guid。 |
| 真删 D08/D09/D10 | `Private\World\LDWorldClaims.cpp`、`LDWorldLeases.cpp`；`Public\Objectives\LDObjectiveRules.h` 的旧 Claim/授权/deadline；GameMode 旧安全出生/死亡税/profile 分支 | 先替换 Objective/World/Controller/UI/clock 消费者和场景，再删符号/调用/旧条件；不能留恒真旧 predicate。 |
| 真删 D06 | `Public/Private\Population\LDB92Roster.h/.cpp` 及架构 D06 明列的 Controller、数据、UI、Transfer/Release 消费闭包 | 真实 Demo AI 通过现有参赛记录/领域入口接通；解除跨域引用后再删，不从类名判断全部人口依赖已消失。 |
| 真删 D12/B03 | `Public/Private\World\LDB91Checkpoint.h/.cpp`、`Private\World\LDWorldCheckpoint.cpp`、`LDCheckpointGeometry.cpp`、`Private\Combat\LDCombatCheckpoint.cpp`；WorldObjects checkpoint 成员/命令 | 先断 console/flags/probe/serializer/EnvironmentSettled 独占边，再删；不动本地设置 Save，同世界 Reset 不调用这些恢复函数。 |

### 9.3 配置、生成输入和资产根

当前三个配置文件有四份区域定义：`Config\RegionLayout.json` 的 Civic Reach、
Reflected Reach，`Config\V07RegionLayout.json` 的 B76 Civic Reach，
`Config\V08RegionLayout.json` 的 B82 River Reach，均为六个 spawn。
实际出生消费 DataAsset 的 Spawns，并非已经有十个可直接使用的 PlayerStart；
`LDRegionBootstrap.cpp:56` 的单个 PlayerStart 也不能作十点验收。

| 处置 / 有限路径 | 当前生产者/消费者与前提 |
| --- | --- |
| 改造 `Config\DefaultEngine.ini`、`DefaultGame.ini`、`RegionLayout.json`、`BootstrapLayout.json` | 默认 map/mode、旧容量、广域 AlwaysCook 和地图生成一起改；C4 核对合并后有效值，保留真实所需导航/碰撞与平台锁。 |
| 裁剪 `Config\V07RegionLayout.json`、`V08RegionLayout.json`；`SourceData\Catalogs\V07\world_and_routes.json`、`v07_rules.json` | `Scripts\SyncV07WorldData.py` 同时生成旧 payload/contract/布局；先改 producer/loader，再删除闭包，不能先断其中一端。 |
| 改造 `SourceData\Character\prototype_rules.json` 与架构 D21 的 V08 有限目录文件 | Scaffold 导入 CharacterCatalog；`SyncB84Data.py` 还读取 `LDAcquisitionCatalog.cpp`。C1 定保留集并沿 Sync 生成，不把旧参数认定批准。 |
| 保留资产身份并改造 `Content\Lockdown\Generated\Maps\L_Frontend.umap`、`Maps\L_DevelopmentRegion.umap`、`Data\DA_CivicRegion.uasset`、`UI\WBP_UIRoot.uasset` | 它们是已有入口根；“保留身份”不等于保留旧布局/规则。C2/C3 后修改生成器并重存/加载。 |
| 必查当前引用 `Content\Lockdown\Generated\Data\DA_PrototypeTuning.uasset`、`Data\DA_CharacterCatalog.uasset`、`Input\IMC_Gameplay.uasset`、`Materials\M_Character.uasset`、`Materials\M_Fog.uasset`、`Materials\T_FogClosed.uasset` | Character、UI、Relay 有消费者；`T_FogClosed` 实际在 Materials。先确认 Demo 当前依赖，不因占位或技术旧名盲删。 |
| 退役候选 `Content\Lockdown\Generated\Maps\L_MigrationSandbox.umap`、`L_FunctionalTests.umap`、`L_RegionAlternate.umap`、`L_V07Region.umap`、`L_V08Region.umap`；`Data\DA_AlternateRegion.uasset`、`DA_TacticalRegion.uasset`、`DA_ElementalRegion.uasset` | 当前磁盘存在，Scaffold/RegionBootstrap 仍生产；按 D18/C2/C3 先迁存活根/WorldSettings/WBP/反射引用和生成清单，再删除旧包，不能靠文本无命中宣称安全。 |
| 改造 `Scripts\OpenStandalone.ps1`、`OpenPackaged.ps1`、`PackageWin64.ps1`、`BootstrapAssets.ps1`；两个 `Lockdown.Build.cs` / `LockdownEditor.Build.cs` | 旧 profile、多图 UAT 列表、RuntimeDependencies/AlwaysCook 和 Bootstrap 目录须同批收敛；开发与包内入口同一套规则，不保留旧启动参数暗门。 |

缩写路径沿同一 `Generated` 子目录展开；上述十八个资产只确认文件存在和静态引用，
未验证 LFS 实体完整性、AssetRegistry、Blueprint/CDO、WorldSettings、动态加载或 cook。
最终删除必须按架构 P0–P5：展开引用 → 接回必需组件 → 改生成/加载和迁移存活资产
→ 真删 → 同步门禁/手册 → 实际加载/构建/cook/场景证明。
不以本报告授权无条件删除旧 V07 loader、完整 B84、整图、材质、IMC 或插件。

### 9.4 runner 与断言迁移

| 当前真实入口 / 证据 | 保留目的 | 目标迁移和明确不能声称 |
| --- | --- | --- |
| [LDRulesTests.cpp:64–79](../../Source/Lockdown/Private/Tests/LDRulesTests.cpp#L64-L79)，`Lockdown.Rules.ViewOrdering` | Generation/revision 拒旧 | 增同世界换局/旧回包，不能只改 OwnerState 不清 Vision 表。 |
| [LDInputRulesTests.cpp:6–38](../../Source/Lockdown/Private/Tests/LDInputRulesTests.cpp#L6-L38)，`ControlMappings` / `IssuedCommandLiveness` | 本地 Save、非未来时间、严格失活边界 | 迁目标输入/Actual 时钟；删旧 profile hold 断言，不把 Save 删进 B03。 |
| `Private\Tests\LDNavigationProbe.cpp`；`Scripts\RunNavigationCheck.ps1` | Stop、旧 Life ACK、saved move、观察失效的夹具目的 | 加原生 step、完整子段、拥有连接和不双积分；当前导航检查不是完整圈时序证明。 |
| `Private\Tests\LDDevelopmentProbe.cpp` / `Scripts\RunDevelopmentCheck.ps1` | Lobby/Ready、重复死亡、身体/Build cooldown | 删除职业/人数/死亡税断言，补所有来源冷却和新局初始化；不保留旧玩法运行入口。 |
| [LDResetProbe.cpp:33–89](../../Source/Lockdown/Private/Tests/LDResetProbe.cpp#L33-L89) / `Scripts\RunResetCheck.ps1` | 脏状态、旧代次 RPC/Actor/回包的夹具目的 | 当前是多次 OpenLevel 重开 World，不能证明连接保留；迁为同 World 周期验证，更新真实用例集合。 |
| `Private\Tests\LDObjectiveProbe.cpp`、`LDObjectiveNetworkProbe.cpp` / `Scripts\RunObjectiveCheck.ps1`、`RunObjectiveNetworkCheck.ps1` | 阶段驱动、真实拥有交互、单结果的夹具结构 | 删 Main/ExactDeadline/LateClaim/NoClaim/Withdraw/Assault/Seal 的旧语义；新增 §8，不以改测试名替代。 |
| `Private\Tests\LDWorldBoundary.cpp:38–47,124–145` / `Scripts\RunWorldBoundaryCheck.ps1` | 迟到采样、明确边界、结果恰一次的检查目的 | 当前直接改 World 时间/位置并用旧 Claim；删 Stun/Interrupt/Release 清 Claim 断言，不能冒充真实运动证据。文件不是 `LDWorldBoundaryProbe.cpp`。 |
| [LDEconomyRulesTests.cpp:15–75](../../Source/Lockdown/Private/Tests/LDEconomyRulesTests.cpp#L15-L75)，`EconomyAccounting`；[LDViewTests.cpp:39–64](../../Source/Lockdown/Private/Tests/LDViewTests.cpp#L39-L64)，`ViewPrivacySchema` | 全量失败/重复/溢出/复算、反射隐私白名单 | 迁当前实际操作/字段集合并补 DeathKey/闸门；不关闭断言或用旧数量充数。 |
| `Scripts\RunCombatNetworkCheck.ps1`、`RunViewNetworkCheck.ps1` | 多进程、拥有客户端和网络模拟的启动基础 | 当前 packaged authority 可为不参赛 LDRemoteAuthority；必须增加正常可见且参赛 Host，不能替代公网验证。 |
| [RunTests.ps1:2–21](../../Scripts/RunTests.ps1#L2-L21) | 报告缺失/失败/未完成均失败 | 当前整个 Lockdown.Rules 和旧数量/TradePrivacy 强制项须迁明确用例集合；不能仅降低数量门槛。 |

工程实施先运行本批最小规则/输入拒绝检查，再在修改原生/反射/资产后做
Editor/Game 构建、Bootstrap Validate 及所需真实场景。
当前 runner 的现有参数以其声明为准，不在此编造已经可用的 Demo selector。
本轮仅文档，不执行这些昂贵命令，也不把旧报告写成当前通过。

## 10. 可分派实施切片及门槛

以下是后续代码任务，不是本次已执行进度；各切片仍在同一 Demo 链路内替换旧入口，
不并排新增一个可选旧模式。

| 任务 | 责任 / 精确交付 | 真正前置 | 退出证据 |
| --- | --- | --- | --- |
| SM-T01 单入口、房间和准入 | M1/M11：E01–E04 的 Host/Join/错误、Ready/AI/HostStart、名单锁、Starting 失败；架构 B01 对应入口/参数部分 | 已定 Spec/本报告会话契约；本批 C3/C4 核验 | 旧选择明确拒绝、无缺输入 fallback；SM-01–09 的本批场景、UHT/构建和默认根有效。缺内容时正确拒绝不算可玩 Demo。 |
| SM-T02 核心上下文、资格与写闸 | M1/M5：§4 的 Generation/Guid 映射、Contender/Body/Life/DeathKey、借用 permit、结果单写者、公开/私有投影 | SM-T01 的拥有者/名单入口；Combat/成长/世界共同类型采用 | Economy/Vision 不再独立造局，旧 Match/Life/permit 拒绝，结果 CAS/取消重入 FN-03/04。 |
| SM-T03 原生 step 和完整时间边界 | M2，操作/Combat/世界协作：§6 的 BeginAuthorityStep、CMC 子段、边界姿态、空流/退休/Sealing/Ack | SM-T02；M3 原生运动适配和 M9G 几何；相关 C2 输入 | CP-05–12，远程 packed moves/root motion/击退/回滚 scope 与不双积分；不能仅有纯 DTO/假事件测试。 |
| SM-T04 致死、保留、复活和保护 | M5，Combat/成长/世界协作：首次事实、复合奖励、死者资格、同点 Reserve/提交/Finalize、全部冷却保留 | SM-T02/03；Combat 致死/成长准备提交/World 预留；批准的相关 LIFE 参数及受影响政策 | LF-01–09；金币/XP 不半奖，成功新生命不回退；真实拥有连接下的到期/重放/故障。政策未定分支单列阻塞。 |
| SM-T05 单圈归约和唯一结果 | M2：Objective 替换 Claim/deadline；事件序列/连续段、完成提案和先锁闸 | SM-T02/03；SM-T04 的生命事件；批准的发育/圈参数 | CP 全组和 FN-01–04；接入成长后补奖励/交易竞争，不能只用 Host 调试强制赢。 |
| SM-T06 同房间回 Lobby/新局 | M1/M5，各状态所有者共同 Reset：不重进 StartPlay、不 travel、不 restore checkpoint | SM-T01/02；各域明确 retire/reset/新初始化；完整验收依赖 SM-T04/05 | FN-05/06，连接保持、旧服务不在 Lobby 偷跑，Vision 服务器表与客户端代理都清，旧 Guid/Generation/回执无执行权。 |
| SM-T07 生产链与裁剪闭合 | 地图/资产负责人及各域：§9 精确源调用/数据/生成/资产/runner 闭包；更新真实系统说明 | 本闭包 P0–P4 和适用 C1–C4，不等无关模块参数 | 资产引用、加载、构建、Bootstrap/cook 和当前用例集合；旧专属符号/输入/图/runner 无存活消费者，正常完整 FN-08。 |

**B01–B03 与 C1–C4 的界线：**

| 架构门槛 | 本模块的具体影响 |
| --- | --- |
| B01 | 先做入口/身份/阶段和旧输入/缺输入拒绝，不等待完整技能目录；不能把拒绝开局包装成正常 Demo。 |
| B02 | 玩家交易 RPC/服务/面板/Cancel/Pending/TradePrivacy 闭包同步真删；保留 Ledger/Vendor/Inventory 的有效原子性和来源测试，不依赖新价格。 |
| B03 | checkpoint/restore 的 console/参数/序列化/probe/EnvironmentSettled 独占依赖删除；本地设置 Save 不动，多人新局由 SM-T06 提供。 |
| C1 | 只阻塞相关实际效果/属性/冷却/内容保留集及相应数值验收；未选目录不能作为全部保留的理由。 |
| C2 | 阻塞实际唯一地图/十点/圈几何和其资产迁移，结构数量本身已定；不阻塞基础身份/准入或 B02/B03。 |
| C3 | 反射类型与二进制资产物理删除前查 Blueprint/CDO/WorldSettings/硬软及动态引用，迁移重存存活根后再删。 |
| C4 | 模块/插件/ini 的真实消费者和合并后有效配置必须核实，不修改共享引擎/插件安装掩盖依赖错误。 |

基础实施可以开始，但完整 M1/M2/M5 仍受实际原生 driver 接线、各领域合约落实、
批准参数/受影响政策和真实场景门槛约束。该结论不声称一个报告已经让 Demo 可运行，
也不把本模块之外的内容/AI/视野/成长完成情况推导为通过。

## 11. SC-09 对接交付表

| 必需项 | 本模块的正式交付 | 生产者 / 消费者与失效规则 |
| --- | --- | --- |
| 状态所有者 / 函数 | §1、§3、§4：GameMode 阶段/参赛资格/结果；Combat 唯一 AdvanceAuthority/SampleBoundary；Objective 单圈归约 | GameState/OwnerState/AI 都是获准投影。没有 GameMode/Combat 两个相互推进的调度器或重复可写结果。 |
| Match 双表示 | §4 的 `FLDMatchContext{Generation,LedgerMatch}`；GetMatchContext；GI 只分配不回退 Generation | 网络 Generation 与 Ledger Guid 显式映射；成功新局/失败启动换代；旧匹配请求拒绝，不能强转/猜 Guid。 |
| Contender / Body / Life / Death | §4/5 的VictimKey、BodyRef、LifePhase、DeathKey、CommittedDeath/RewardBasis含EligibleAtDeath、EndContenderLife、AI绑定 | 每个Effects集合先全锁再奖；不以更晚离局撤销资格；死亡可选但重查Phase/不治尸体，成功复活才增Life。 |
| 命令、结果、权限 | §4.1 的 ServerSetReady/SetAICount/StartMatch/EndMatch/ReturnToLobby/LeaveRoom；SessionReceipt | 拥有连接与 Host 服务端解析；按阶段/结构版本接受；单在途、精确条款/ProcessedThrough/过窗拒绝。AI 不伪造 RPC。 |
| 时间 / 序号 / 水位 | §6 的 Step、EventStamp、ProviderFence、Boundary、Begin/Validate/CompleteBodyMotion、PublishPresence/Eligibility/Through、AckConsumed | 原始OccurredAtSeconds跨流排序、Ms只ceil桶；段序不同于事件序；Fence只取已完成整数边界，空流/退休/因果子批次闭合。 |
| 终局与非重入提交 | §6 的借用 GameplayWritePermit、ValidateWritePermit、TryCloseMatch、ResultReceipt/ResultView、FailAuthorityBoundary/FaultReceipt | 最终点核许可；先锁闸再取消；闸后不首次提交。必需失败有界重试后明确故障关闭，不造成功Fence/NoReward/胜者。 |
| 取消/死亡/离开/新局 | §5.4 矩阵；§3.3 同世界 Reset；§4.3 生命周期通知 | 保留候选/财产/持续冷却，清短寿命动作；旧主体事件按合法已接受前缀消费，退休后的新请求拒绝；Host 故障不转移。 |
| 参数/单位/政策 | §7 正式 MATCH/CAPTURE/LIFE/UNIT/POLICY 键及唯一内容所有者 | 死亡选择/意图解保护/比例/来源冷却/死亡独立效果继续/离局取消均已确认；具体数值/内容仍待审，无默认值。 |
| 公共/私有读权限 | §4.2/4.3：Lobby/PublicMatch/ResultView/Owner/AI self；LifeNotice/MatchClosed/MatchReset | 全场圈例外无隐藏坐标/构筑，ResultView 不发 LedgerMatch；旧 Generation/revision 清理；Host 同样受限。 |
| 接线、真实删除和验证 | §8–10 的 E01–18、有限文件/资产/runner、SM-T01–07、B/C/P 门槛 | 生产者、消费者、UHT/序列化、生成/cook、测试和手册同闭包；不凭文本研究盲删二进制，不把旧调试 runner 当新 Demo 通过。 |

## 12. 独立研究、审核和证据限制

| 实际专家 | 独立职责 / 本模块采用的结果 | 证据级别 |
| --- | --- | --- |
| `lifecycle-rules-researcher`，agent `64686d59-3f9d-44f5-8505-c25e52a8ef63` | 独立产品/状态机研究；确认同世界方案符合 Spec，覆盖连接取消竞争、失败开局、十点重试、死亡保留、中间争夺和新局隔离 | 已完成研究；没有写本模块整合稿，没有运行游戏，也不担任最终 QA。 |
| `lifecycle-ue-auditor`，agent `5c59b29b-ebcb-4af4-ae02-e81aaffb96ae` | 独立源码/UE 时序审计；定位真实调用/资产/runner，提出原生 step、边界姿态、毫秒桶闭合、Finalize 前置准备与同世界 Reset 的约束 | 已完成源码研究及有限路径补核；未修改文件，未构建/加载二进制/联机，也不担任最终 QA。 |
| `lifecycle-multiplayer-qa`，agent `d1ed9b2d-1b3d-404f-a7a9-eb8eb4fa1a83` | 未参与前两份研究或整合稿写作；独立核对流程、真实源码、同刻竞态、失保/死亡/离局/新局和跨域提交完整性 | 独立复核完成：SM-QA-01 原始亚毫秒跨流排序、SM-QA-02 每个Effects致死集合快照屏障均已解决；当前模块无进一步高置信文档阻塞。不是原生运行通过。 |

审核还确认退休流/回执压缩、过窗拒旧和长期容量场景已补齐；
子时刻阶段推进、整数封桶、致死资格冻结、settlement-only、原子复活、
结果统计与故障关闭在文档层面自洽。源代码、资产和实际连接的后续证据门槛仍按 §8–10 执行。

普通事务固定在 Capture 之前，采用 §6 的唯一完整批次；本报告不保留第二种排序方案。
研究、整合与独立审核不代替原生实现验证；本文只对已列静态证据和目标设计负责。

工程专家经 Context7 核验的公开语义包括
[Game Mode and Game State](https://dev.epicgames.com/documentation/unreal-engine/game-mode-and-game-state-in-unreal-engine)、
[Actor Ticking](https://dev.epicgames.com/documentation/unreal-engine/actor-ticking-in-unreal-engine)、
[FScopedMovementUpdate](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FScopedMovementUpdate) 和
[Travelling in Multiplayer](https://dev.epicgames.com/documentation/en-us/unreal-engine/travelling-in-multiplayer-in-unreal-engine)。
它们支持 PreLogin 与最终注册之间重查、Tick 依赖不等于完整历史、移动 scope 可能撤销、
以及本方案不需要跨世界重连；不是对项目锁定 UE 5.8.4 原生执行路径的实机证明。

**本轮未运行** Unreal、Editor/Game 构建、Bootstrap/生成器、原生自动化、LAN/公网、
发布包或完整对局；未实施任何代码/配置/SourceData/Content/Scripts 裁剪。
本报告可以作为已独立审核的文档模块交付；具体参数/内容审核、原生接线及资产证明
仍分别约束相应实施切片，不等于完整 Demo 已经实现或运行通过。

# 视野过滤与 UI 状态

战争迷雾首先是服务器的读取权限，不是给已经收到的敌人信息盖一层黑色材质。
本文说明当前观测与表现组件；产品显示内容以 [App Spec](../APP_SPEC.md) 为准。

## 权威观测链

| 入口 | 责任 |
| --- | --- |
| `Vision\LDVisionDirector` | 服务器侧。`FLDSightGeometry` 做静态视线；`FLDObserverLedger` 管逐观察者的句柄、epoch、revision、ObservedDeath 事实和 Nav pin；导演按核心时钟采样并发布，处理死亡记录、命令解析和视线查询 |
| `Vision\LDViewTypes` | 冻结的 `FLDWorldView` 观测帧、隐藏通知与终结通知的线缆格式和校验；保存与加载都会校验 |
| `Vision\LDWorldViewRelay` | 每个拥有连接一个 relay：接收游标、超时清除、代理与迷雾表现 |
| `ALDObservedEntityProxy` | 客户端非复制的人形表现，无碰撞，不参与权威战斗或资源所有权；参赛者按名册座位着色，营地 NPC 用独立外观；动作与状态经共用的 cue 字节呈现，动作年龄取样本自带年龄加上呈现时钟越过该样本的时间；授权的观察死亡（`ObservedDeath`）先留下 Combat 的客户端倒地副本 `ALDBodyPuppet::SpawnFall` 再移除代理，离开视野、超时与复位只移除 |
| `ALDObservedProjectileProxy` | 客户端非复制的公开技能表现：粉笔、臭袜飞行物，以及高数书影、小广告武装/已武装圈、开麦扇形。只传不含施法者/等级/冷却的公开位置、朝向、look与半径/角度；无碰撞、不可选为目标。飞行物带 `ULDFlightTrailComponent` 并按呈现时钟走完最后样本（最多0.4s）；地面/扇形效果从授权帧消失时立即隐藏销毁；stale、复位或他局视图立即销毁 |
| `LDPropFade`（relay 内） | 客户端每帧检查带 `LDFadeable` 组件标签的道具（美术目录 `fadeable`：旗杆、两种树、集装箱与废车）是否挡在镜头与本人存活身体或当前呈现代理的胸口/头部之间，或穿过站在其中或紧贴它的身体（水平距包围盒 ≤ `PierceRadiusCm` 45 cm 且在其高度内）；挡住时把材质参数 `LDFade` 在约 1/6 s 内调到 1，不挡回到 0。relay 给淡化材质设 `LDFadeStrength` = `FadedStrength`（0.3），完全淡化时仍保留约 70% 像素可见度，接近障碍时不会消失；普通组件淡化期间关闭投影，恢复不透明后恢复投影。正式地图中的每棵树和每个静态道具都是独立保存的 Actor，只淡化实际挡住身体的组件。只影响显示，不改碰撞、视线或导航；材质缺该参数时退回遮挡期间隐藏并记录 `LD_FADEABLE_PARAMETER_MISSING`。只测已授权呈现的单位，不因隐藏单位而淡化。 |
| `ALDOwnBodyMarker` | 只在本机生成、挂在本机存活身体脚下的光圈，用于一眼认出自己；死亡或无身体时隐藏 |

导演只读核心的身体、生命和资源，不另存 HP、Life 或身体表。
核心负责调用：生成导演，开局 `BeginMatch`，为每个有 Controller 的参赛者 `RegisterObserver(Session, PC)`，
在驱动推进完最后一步之后调用 `PublishAuthority`，离开时 `UnregisterObserver`，局终 `EndMatch`。
只有拥有真实 `ULocalPlayer` 或网络连接的 Controller 才生成 relay，客户端表现也只在拥有真实 `ULocalPlayer` 的机器上进行。
`IsLocalController()` 在不同网络模式下不能可靠区分服务器侧对手，因此用 `GetLocalPlayer()`/`GetNetConnection()` 判断；
对手只能通过 `EnumerateVisibleUnits` 读取，否则其视野会在 Host 上生成代理，造成泄露。

## 谁可以看到什么

- 观察者唯一的来源是自己的存活身体。半径取批准内容的 `VISION.RadiusCm`（含边界）。
- 遮挡只来自注册世界中 `BlocksSight` 的凸多边形：线段触到边或角即算遮挡，世界只有一层。
- 镜头位置、缩放和小地图手势都不授予视野。
- 观察者没有存活身体时，帧为显式 `NoLivingBody`：没有覆盖、单位或终结记录。死亡镜头只看静态和公开信息。
- 参赛者单位只携带 SC-07 白名单：参赛者 Id（客户端经公开名册换成名称）、等级、HP/MP、公开状态，
  以及 Q/W/E 三个主动的原生 id 与等级（`Actives`，字段位 `LDObservation::Actives`）。
  `Actives` 由 Combat 的 `PublicActivesOf` 在构筑安装和身体准备时写进同一公开事实，只随当前可见单位下发、失视即撤销；
  codec 要求每槽“id 与等级同为 0 或同为非 0”、等级不超过 `LDObservation::MaxActiveRank`（2）、非空槽的 id 两两不同，营地成员三个槽均为 0，接收端只接受批准目录里的主动技能。
  目标卡（`LDHUD::ObservedCard`）据此显示 Q/W/E 小卡（键位与技能名，空槽单独标出）。不含冷却、完整属性或规范身体身份。
- 营地成员只携带生物 Id、HP 和公开状态；内容定义了 MP 时才带 MP。营地成员的 `Presentation` 恒为 0。
  `CampReturning`（3）只出现在营地成员身上，不叠层、无期限：codec 只接受 `Stacks` 为 1、`ExpiresAtServerMs` 为 0 的该状态。
  它由 `ALDCampDirector::AppendObservedFacts` 在核心 `ReadObservedPublicFacts` 之后追加（只在成员处于 Returning 时）；
  营地拒绝或缺失时本次发布失败关闭（`LD_OBSERVATION_CAMP_FACTS_UNAVAILABLE`），不发布缺少营地事实的单位。
- 复活保护的唯一真值是核心的 `ProtectedUntilMs`：当前身体、`Alive` 且 `Now < ProtectedUntilMs`（半开区间）时生效。
  本人通过 `OwnerState` 显示；他人只在当前可见时，从同一 `Statuses` 列表看到一个不叠加、带 `ExpiresAtServerMs` 的
  `RespawnProtection`（4），由核心的公开事实提供方派生，codec 只接受参赛者身上、`Stacks` 为 1 且有到期时间的该状态。
  不公开复活时刻、Life、BodyIdentity、来源或技能冷却；失去视野、死亡、离局时随单位一起清除。
- 每个观察者每次看到一个单位，都分配新的 opaque `Handle` 和 `ObservationEpoch`；
  展示字段变化只推进该单位的 `StateRevision`，姿态变化不推进。
- 参赛者的 `Presentation` 是锁定名册座位 0..5（由 Combat 写入身体，跨命不变），codec 拒绝 6 及以上；
  六个座位色由 `ULDHumanoidVisualComponent::ContenderTint` 唯一定义，身体、头像和小地图共用。
  营地 NPC 不用座位色：低档已读不回的 HR 带袖章和夹板，高档追着办卡的私教带帽子和哨子（`ApplyMonsterLook`）。
- 动作提示 `ActionCue` 为 `Attack/Cast/Hit`：Combat 在核心 `ReadObservedPublicFacts` 里给出当前提示、开始及释放的权威时刻；
  线缆只带提示、`ActionCueAgeMs` 与 `ActionCueImpactDelayMs`。后者对 Attack/Cast 是从提示开始到服务器释放的毫秒数，
  对 Hit/None 为 0；超出 `uint16` 或提示开始晚于当前时刻时发布失败关闭（`LD_OBSERVATION_ACTION_CUE_INVALID`）。
  代理与本人身体经 `ALDCharacter::PresentationCue` 得到同一个 cue 字节：低 4 位是动作姿势，
  定身、回位、护盾、复活保护各有独立形状（脚下圆环并锁腿、背后标牌、头顶光环、竖直光柱），不只靠颜色区分。
- `Cast` 提示可带该次施法的技能原生 id（`ActionAbility`；其他提示与 D 冲刺的 `Cast` 提示为 0），
  由 Combat 在同一公开事实里给出；接收端只接受批准内容中的 id。这是已批准的 CB-P10/SC-07 例外：
  只用于在施法那一刻于可见施法者旁显示技能名（`ULDWorldFeedback`），不进目标卡，不保留技能列表，不含等级或冷却。
- 飞行物（`Projectiles`）使用与单位相同的视线规则：观察者存活时，从 Combat 的 `ReadObservedProjectiles`
  选出在视线内的飞行，近的优先，每帧最多 16 个；超出注册地面的飞行只是不可见，非有限位置是故障。
  每次可见区间分配新的 opaque 句柄（与单位、终结共用句柄空间），只带地面点、朝向与外观，
  不带施放者、效果、来源或目标，不能被选中或下令；离开视线或飞行结束时随隐藏通知立即移除。
  飞行的可见权限与其施放者身体一致（`LDObservation::ClassifyFlight` 按 `FLDObservedProjectileFact.SourceEntity` 的
  `ALDCombatDirector::ActingGroup` 判定）：冻结的野外身体与其他隔离组的飞行隐藏；PK 观众与被淘汰者看见竞技场组的全部飞行（不需视线）；
  PK 生命对本场飞行取自己与存活队友视线的并集（`SelectVisibleProjectiles` 的 `SharedFeet`），其余仍只看自己的视线。
- 帧中的单位和隐藏通知中的句柄都按观察者本地句柄排序；同一次采样中新开的区间也按句柄顺序分配 epoch。
  帧内位置和 epoch 大小都不反映规范身体顺序，客户端无法借此跨区间追踪单位。
- `ResolveObservedUnit` 和 `BindObservedOrder` 在命令处理的那个权威时刻重新判断视线，并核对注册世界的修订；`Purpose` 不扩大授权。
  正常失败对外只合并为 `TargetUnavailable`（超出半径、被遮挡、句柄已失效）或 `ObserverUnavailable`，不泄露目标是否存在。
  视线 `InvalidPoint`/`StaleWorld`、观察者的核心生命或身体读取失败属于不变式破坏，返回 `AuthorityFault`，
  调用方按故障处理，不能当作目标消失或失视。`BindObservedOrder` 以 `AuthorityFault:` 开头的 Error 表示同类故障，
  其他 `false` 都是普通拒绝。采样和死亡见证使用同一分类。
- 终结只来自授权的观察死亡：需要 `DeathsAndRewards` 许可、仍注册的身体、核心当前时刻，并且观察者当时看得见。
  不能由 Conceal 推断死亡。历史时刻的死亡会明确失败，等 CT06 提供子段姿态。
- 每个观察者只保留一个当前 Nav pin。Bind 和 Release 校验完整的 Match/Life/Order/Epoch，旧订单不能覆盖或清除新订单。
  最多保留 16 条终结事实，只有未被订单绑定的才可被替换；全部被绑定时明确背压。
- `BeginMatch` 会拒绝“参赛者数 + 营地成员数 − 1”超过 16 的配置；codec 从不截断。

## 全量快照、隐藏与恢复

- `ClientFullView`（不可靠）发送完整帧；`ClientConceal`（可靠）的 `Removed` 为空时表示代次复位；`ClientTermination`（可靠）发送终结通知。
- 三者共用一个游标（match, generation, revision），旧消息被拒绝。
- 收到 full 帧时先应用其中的终结，再替换单位集合；不在新帧里的单位以 `FullSetRemoved` 清除。
- 隐藏通知不能被迟到的旧快照撤销；旧隐藏也不能擦掉更新的重现，因为重现带新的句柄和 epoch。
- 0.25 s 收不到 full 帧就清除全部单位与飞行物并标记 stale：不外推位置，不保留 LastSeen。
- 换局（`OwnerState.Match`）、观察者生命变化、断线、局终和非法载荷（`ProtocolRejected`）都会清除状态。
  属于其他世界修订的帧会被拒绝。
- 清除时销毁对应代理及其表现。
- 拾取只对当前 full 帧中的同句柄/epoch 代理进行，使用其呈现位置、骨骼摆姿与可见材质 section 的三角面；
  不读取隐藏权威角色，不增加物理碰撞或 LastSeen。模型 LOD 须保留 CPU 数据（美术导入器生成，catalog 校验）。
  每个网格 LOD 在美术子系统加载时建一份共享拾取模型（`LDModelPicking::SharedSkinnedModel`）：可用 section 的三角面按材质与主导骨骼
  分成小块，每块记录各影响骨骼的锚点与半径。每个模型每帧最多摆姿一次：只由骨骼矩阵算出各块的保守包围盒，
  射线进入的块才按渲染器 CPU 蒙皮的同一公式蒙皮并测试，由近到远直到不可能有更近表面；
  不对整网格蒙皮，也不调用会刷新渲染线程的 `GetCPUSkinnedVertices`。
  静态 Sight 或本人模型遮挡之前的最近表面才可选，模型包围盒仅作宽相拒绝，不能当作模型命中；本人模型是第一遮挡面时不做宽容回退。没有表面命中时，
  才按 `LDModelPicking::NearVolume` 取射线在 12 px 角容差内最接近的竖直拾取体（脚底到头顶、根环足迹半径），
  同样不越过静态 Sight 或本人模型遮挡；遮挡距离在角容差判断前生效。隐藏或倒地的模型没有拾取体。
- 受击闪白、胸口火花与轻微后仰独立于攻击/施法动作。本人用合法 owner cue 的 `LastHitAtMs`；
  可见代理仅用同一授权区间、相同血量上限的相邻样本下降，在呈现时钟到达样本时触发，不推断施害者、暴击或被治疗抵消的隐藏伤害。
  受击时仍在攻击/施法的模型有 45 ms 局部姿势停顿，随后追上原动作时间；不暂停输入、位移或服务器判定。
- 代理与飞行物由 relay 用同一个呈现时钟摆姿（`LDObservedPresentation`）：呈现时刻是权威时间轴上的
  “本机时间 − 跟踪的时钟偏移 − 0.13 s”（一个发布间隔加抖动余量），偏移由每帧的到达时间与 `SampledAtServerSec` 得出：
  更早的到达立即下调偏移，迟到只能缓慢上调，跳变超过 0.5 s 视为换了时钟并重新起算。位置只在两个已收样本之间线性插值，超过最新样本就停在最新样本，不外推；
  相邻样本相距超过 300 cm（首次出现、复活、传送）直接跳到位。每个样本的动作提示与状态在时钟到达该样本时才呈现，
  因此出手、飞行物到达与受击在同一时间轴上。单位的隐藏、死亡和超时仍立即销毁代理，不按延迟时钟保留。
- 迷雾是覆盖在注册地面边界上的 128² 纹理（双线性采样）：当前覆盖亮（255），其余调暗到 `LDFogHidden`（190）以便看清静态地形；没有探索层。
  每收到新的覆盖，各格在约 0.1 s 内从屏幕上的当前值过渡到新覆盖，只在两次已授权覆盖之间过渡。
- 观察 schema 7（联机 wire 13）新增上述公开技能几何；覆盖位图仍做有界字节游程编码，解码后仍是精确的2048字节，
  不缩小覆盖、不增加探索层、不依赖上一帧。不可压缩时发送原始位图；错误长度、越界、未填满或截断直接拒绝。
  这减少普通覆盖快照的拆包与带宽开销，不把不可靠快照改成会积压的可靠队列，也不延长 0.25 s 的权限清除窗口。
- 采样节奏和帧格式在代码中维护，不在这里设定 Demo 平衡值。
  采样按核心时钟排程：普通迟帧保持原相位，停顿后跳过错过的间隔，只采当前姿态一次，不补发历史批次。
  单位、飞行物和句柄仍在每次采样更新；128² 覆盖只在有拥有连接 relay 的发布样本计算，
  观察者脚点与世界修订完全相同时复用上次覆盖，生命/观察代次复位时清掉缓存。
  AI 与未发布的内部存活帧保留精确 2048 字节的零位图以满足帧校验；它们的可见单位来自各自逐目标视线，
  不读取覆盖，也不借用真人视图。没有存活身体的帧仍是空覆盖。
  静态地形 tier 与高层区域候选由空间网格筛选，仍以原闭凸包/开内部线段判定作最终裁决；
  覆盖格中心的 tier 在构建世界视线时预计算，不把格中心的覆盖当成单位可见性。

## UI 只读模型

玩家可见的术语与带梗短句（资源与属性名、流量、普攻、冲刺、旗杆与阶段文案、击杀与结算文案等）只在
`Source\Lockdown\Public\UI\LDTerms.h` 定义一次，HUD、前端、面板、飘字、订单提示与测试都引用它；技能、野怪与状态的条目名
仍由内容显示行提供。系统文案直接点名的几个状态名（护盾、定身、减速、嘲讽、缴械、眩晕、复活保护）
在术语表里另有一份，由 `Lockdown.Rules.HUDTermsGlossary` 核对与批准目录的显示名一致。

| 信息 | 允许来源 |
| --- | --- |
| 网络与帧诊断（右上角，可分别关闭） | 本地设置 `bShowNetworkDiagnostics`、`bShowFPS`，默认关闭。FPS/平均帧耗时/窗口峰值使用实际本机帧间隔（至少 0.5 s 窗口，不裁掉长帧）；RTT、抖动、收/发丢包、距上次收包时间直接读取本人 `UNetConnection`，丢包比例转换为百分比，不新增探测 RPC。远端本人还显示 CMC 校正频率与最近误差（匹配 ACK saved move 的位置差）。房主显示本机房主，不伪造远端 0 ms；无连接/无样本明确显示状态。面板不拦输入，不参与玩法判定 |
| 自身血蓝、护盾与临时状态 | Combat 的仅本人复制 `ALDPlayerController::CombatView`（同 Match/Life 才使用；没有身体时退回 Lives 投影的血蓝）；`LDHUD::SelfIcons` 只整理（不设上限）：小地图列与底部带之间的状态条，右边贴着底部带左缘、底边与底部带底边齐平，每行 6 格（`ULDHUD` 的 `HudStatusPerRow`，48 单位见方，随底部带同比缩放），第一行在最下、更多的行向上叠，格子按需创建（`EnsureStatusCells`），先控制状态、再其他临时状态与正在生效的回复（`bOutOfCombatRegenActive` 为“脱战回复中”、`bDormRegenActive` 为“躺平角回血中”，无剩余时间圈，说明层按 REGEN/DORM 参数写出延迟、比例与中断条件）；光环与复活保护（生命行）不进格；每格是该状态或回复的图标（`FSelfIconView::IconId` 经 `LDUIIcons`；纹理缺失时显示名称首字 `IconGlyph`），按正负面着色（`FSelfIconView::bPositive`）：对承受者有利的状态（`LDHUD::StatusIsPositive`：护盾、增益、强化普攻、复活保护、回位）、两种回复绿边，所有控制与减速、破防、灼烧红边（未知状态按负面），剩余时间圈同色，叠层数在右下角；悬停时用与 Q/W/E/D 格同一个说明层（停在方格正上方），由 `LDHUD::SelfIconDetail` 只为悬停的方格生成：首行是名称与层数/护盾/剩余时间，次行是类别（控制状态/临时状态后接“ · 正面”或“ · 负面”），正文是内容目录里的完整描述：状态读描述文本，回复按 REGEN/DORM 参数写出规则；本人投影缺失时显示“状态同步中”。状态条格子算 HUD 命中区，条内空白不挡世界点击。有截止时刻的临时状态由 `ULDHUD::NativePaint` 在方格上画剩余时间圈（暗色底圈 + 从 12 点顺时针收缩的亮弧），比例来自 `LDHUD::FStatusRings`：投影只带截止时刻，首次看到某截止时刻时的剩余时间算满圈，截止时刻延后（刷新）重新满圈，状态消失后再出现重新计；中途首次看到（如重连）从满圈开始；读不到权威时刻时不画圈 |
| 自身资源条每秒回复 | Combat 仅本人投影 `HPRegenPerSec/MPRegenPerSec`，由 `LDTactical::RegenRates` 从当前属性及参赛者脱战回复的同一条件推导；只用于显示，不参与资源结算。`PublishOwnerViews` 在满资源下的回复率变化也推进修订；HUD 经 `LDHUD::RegenText` 拒绝别局/别命、死亡、无身体及无效资源投影，条内右侧显示 `+X.X/s`，中央保留资源/护盾读数。满资源显示回复能力，不估计资源差值或展示他人私有属性 |
| 自身等级、经验与待选 | Growth 的 `ReadOwnProgression`（`FLDOwnerState.Growth`）；满级显示 MAX，缺失显示“同步中” |
| 自身生命、保护与复活倒计时 | Lives 投影的 `LifePhase`、`ProtectedUntilMs`、`RespawnAtMs`，以及 `ReadDemoAuthorityTimeMs` 给出的同局权威时刻；读不到时显示“同步中”，不用本地时间或 0 代替；倒计时在该时刻到达截止前不显示 0，显示本身也不解除保护 |
| Q/W/E 与 D | 底部动作栏只有 Q/W/E 与 D 两组共四格（没有装备格），已绑定技能的 Q/W/E 格可拖到另一 Q/W/E 格交换本地槽位顺序。`CombatView.Entries` 经 `LDHUD::ActionInput/Action` 把意图可提交性（本人当前 Match/Life、对局进行中、存活、`IsWorldInputSuppressed()` 为假即未被菜单/设置、子确认或文字编辑占用且无残留按键）与执行限制（空槽、冷却、法力、控制、服务器原因）分开；图标与热键同走 `ActivateCombatEntry`，限制只置灰说明，不吞掉显式激活；本人战斗投影缺失时显示“未同步”且不发送。准备态读 `ReadArmedCombatEntry/IsAttackMoveArmed`，回执日志 `CombatReceipts`（`FLDCombatReceiptLog`，按到达序号保留最近 32 条）由 HUD 用自己的游标逐条读取，同一帧到达的多条回执各自提示一次：`LDHUD::CombatReceiptsNotice` 按到达顺序把不同的“未执行”与“复活保护没了”合成一行提示条，`CombatReceiptSlotNotice` 非空的槽位外圈变红约 2 s。格面只画热键、图标（`FActionView::IconId`，D 为 `icon_dash`；受控、法力不足与不可用时图标变灰或偏蓝，纹理缺失时显示 `Glyph` 首字）、等级（`Rank`）与法力数字（`Cost`），状态只由 `LDHUD::ActionLook` 的外圈与图标颜色表示，文字在悬停说明层里：`LDHUD::ActionDetail` 给出名称、“类别 [键] · 状态”与描述/目标/当前等级效果，HUD 按格子的布局框判断指针所在（禁用的格子也能看说明）并把说明层居中放在格子上方；准备态不另出说明行 |
| 射程与目标预览 | `ULDHUD::NativePaint`：本人同局同命存活且未武装时，在脚下画一圈淡白的普攻射程（`CombatView.AttackRangeCm`，射程成长立即反映）；武装单位技能或攻击移动时，射程圈改为强调色，并对指针下的单位（`ObservedUnderPointer` + `ReadUnit`，与点击同一选取、只取当前授权帧）按脚点 2D 距离给射程圈着色：不超过射程为绿、否则为红，单位身上不画框；方向技能另画指示线：方向与确认时提交的一致（`GroundUnderPointer` 地面命中点减胶囊脚点），从本人显示位置画出，长度为射程（落点技能取到落点为止）；指针下没有地面时不画。射程圈画在显示位置，它的距离颜色以胶囊脚点判定。只是预览，是否命中由服务器裁决 |
| 时间线横幅与休息返回倒计时 | 只读同代 `PublicMatch.Timeline` 与 `ReadDemoAuthorityTimeMs`。`TimelineBanner` 显示休息编号、步骤、剩余时间、站圈报名窗口与人数（锁定步骤不再写“配装已锁定”，构筑照常可做）；野外显示段数与下次休息，最终野外显示旗杆开放。`SyncSignupMarker` 为各玩家画相同公共圆心的报名圈（`AIRDROP.PickupRadiusCm`），站圈而非按钮报名。“准备完毕”只在 Build 显示，只读 `OwnerState.bCanRestReady/bRestReady` 并发 `SubmitRestReady(true)`；只有 Accepted 显示已准备，拒绝逐项明确，不在 UI 提前缩短计时。所有真人准备后由权威进入最后报名窗口，不能撤回准备；公共名单在 `Timeline.ReadyContenders`。Build/Payout 的“重置训练”及附近 NPC 按钮固定在横幅下方，锁定/PK/返回时隐藏；训练重置沿用 `SubmitTrainingReset/OnTrainingReset`。NPC 仅在 `NearbyRestService` 非空时给出交互入口，选择后打开独立服务框，离开现有150 cm范围关闭；替换及确认只在显式训练师服务框显示，回执与在途状态不因隐藏 UI 重发。野外顶部保留时间线和必要旗杆状态；空投、Boss、标记营地、符咒与上热搜的公开信息由小地图上的标记本身表达（小地图上方只留对局用时），外卖、Boss 与标记营地的出现与结束走事件提示条（符咒不提示），场景中另有外卖与符咒的地面圈。野外文字摘要的交战折叠取本人实际受击与 Combat 交战窗口（`CombatView.BuildSafeAtMs`），不以野外阶段代替交战；构筑面板不因交战或 PK 收起，只有自动打开的面板在本人受击或几秒未操作时收起，脱战不自动重开。休息室角色与全部共享真实训练目标只经获准观察帧呈现，不直接读取隐藏身体或所属玩家私有成长；`ObservedContenderName` 将名单外真实假人标作“训练假人”。三个公开 NPC 与训练区中文名走 CJK Slate 投影、描边和统一避让，不用默认字体斜放 TextRender，也不创建静态假人替身。观战者看到的场内目标只标队伍，“观战中”只属于本人状态。`RestReturnCountdown` 在返回步骤居中显示向上取整倒数，本地先到保持 1 直到权威返回野外；每段野外开始显示“开淫！”。 |
| PK 面板、竞技场圈与观战镜头 | 只读 `PublicMatch.PK`（`FLDPKView`，`LDHUD::HasPK`：比赛、时间线与 PK 同代、休息阶段、`Serial != 0`）、时间线、房间名单与 `ReadDemoAuthorityTimeMs`。`LDHUD::PKPanel` 给出休息按钮下方独立命中面板的全部内容，从 Draw 到 Payout 显示：`LDHUD::PKFormatText`（单挑/混战/按 `Format` 与 `SideCount` 等分的 2v2、3v3）与人数组成的标题；每方一行（`LDHUD::PKSideNames`，名单外为“未列名参赛者”；`FightStartedAtMs` 之后 `!bAlive` 记“（出局）”，`bForfeited` 记“（离场）”；公开押注人数、“你在这方”“你押了这方”）；PKReady 到 `StepEndsAtMs` 的整秒倒数（本地时钟先到保持 1）与 PK 步骤的“最长还剩”；PK 运行中的安全圈行（`FLDPKParameters::FromContent` 读本机已批准内容快照，`LDPKRules::RingRadiusCm`/`RingPreview` 在 `LDHUD::PKRingElapsedMs`＝权威时刻减 `FightStartedAtMs` 上求值）。押注只在 Betting 步骤给 `PK.Bettors` 里的非选手：每方一个“押 X、Y”按钮，本人 `PK.Bets` 里的那方接“ · 你押了这方”并高亮金色，按钮只发 `ALDPlayerController::SubmitPKBet(Side)`，等待回答期间禁用；投掷只在 PK 步骤且 `Outcome == Running` 时给同样的观众：“投掷”按钮武装瞄准（`ULDHUD::IsPKThrowArmed`），之后左键由 `LDPlayerInput` 交给 `ULDHUD::ThrowPKAtPointer`，用移动命令同一条 `ULDNavigationComponent::GroundUnderPointer` 地面射线发 `SubmitPKThrow(ImpactPoint)`，右键、Back 或再按按钮只取消（这次点击不产生查看、移动或攻击）；冷却按最近一次 `OnPKThrow` 的 `ReadyAtMs` 对权威时刻显示 `LDHUD::PKThrowCooldown`（同 `CooldownText` 取整，无时钟为同步中）。`OnPKBet`/`OnPKThrow` 的结果经 `LDHUD::PKBetText`/`PKThrowText` 显示在面板底部并进提示条：只有 Placed/Thrown 读作完成，其余各写原因。Settlement 与 Payout 显示 `LDHUD::PKResultText`（胜方名单、平局或本场无效，本人获胜另加一句）与 `LDHUD::PKAwardRows`（参与/胜者/竞猜/合计，跳过零分，本人一行高亮）。所有本地状态（在途、瞄准、冷却、回答）只属于一场 PK（代次＋`Serial`），换场即清空；HUD 被菜单遮住时取消瞄准。世界表现由 HUD 持有的 `ALDPKRingMarker`（不复制、无碰撞/导航/视野影响，`LDGroundRing` 画法）在 Demo 世界的 PKReady 与 PK 步骤画出：紫色当前安全圈、浅色预告圈（`RingPreview` 为真时）与每个 `PK.Throws` 落点的粉色 `ThrowRadiusCm` 预警圈（到 `LandsAtMs` 为止）；离开这两步、换代或换场、`NoContest` 时销毁，HUD 析构时也销毁。观战镜头：`LDHUD::IsSpectatingPK`（同上 `HasPK`、PKReady 或 PK 步骤、不是 `NoContest`、本人不是 `bAlive` 的选手）成立且公开视图是本人对局时，本地控制的 `ALDCharacter::CalcCamera` 用同一固定臂角把中心换成 `PK.ArenaCentreCm`、臂长取 `LDCamera::MaxArmLength`；场内单位与视野仍由服务器授权 |
| 对局计时 | `LDHUD::MatchTimer` 在小地图正上方居中显示自 `PublicMatch.StartedAtMs`（开局休息 R0 开始）起已过的整秒（向下取整），格式 `m:ss`，分钟不补零（如 `0:12`、`12:05`）；读不到权威时刻或开局时刻未发布时不显示；只随小地图在对局进行中显示 |
| 构筑按钮 | 右下角常驻按钮，只写“构筑”，切换它正上方的选择卡。`LDHUD::GrowthPending(...).Any()`（显示中的主动候选、有空槽可学的图纸、开局构件自选或精英二选一）为真时高亮，否则禁用变灰；`Offers`（排队的主动机会数）或待选构件增加时脉动几秒。没有卡时已展开的选择卡在没有在途请求时自动收起。小地图上方除对局用时外不显示文字行，HUD 也没有“下一步”提示（见 App Spec §7 界面原则）。 |
| 被选中的其他单位 | 当前帧中 Handle 与 Epoch 都匹配的单位，经 `LDHUD::ObservedCard/ObservedUnit` 只显示白名单 |
| 可见单位头顶的名字、等级、血条与状态 | `ULDWorldLabels`（`LDUIRoot` 世界层，最底层、不命中测试、不取焦点）：只在本人存活、帧为同局同命的当前帧且 HUD 显示时，为 `ReadPresentedLocation` 返回的单位绘制；参赛者名来自公开名册（查不到就不显示名字），野怪名来自内容显示文本，状态行与检视卡片同源（复活保护剩余时间向上取整到 0.1 s），各状态按身体标记的颜色书写（定身品红、护盾亮青、复活保护白金，其余用普通状态色），文字本身仍写出状态名。白名单子集为名字、等级（参赛者，画在座位色徽章里）、HP 比例条（参赛者红、营地橙，最大值未知不画）、是否有护盾（条上方浅蓝线）与公开状态；不显示法力、技能或冷却。字号：参赛者名 16 粗体、野怪名 16、状态 14，随文字缩放 |
| 本人头顶血蓝 | 同一层：本人同局同命存活且有身体、`CombatView` 同代同命且生命上限已知时，在本人身体上方画生命条（护盾接在其后，合计超出时按比例收进整条）与法力条，条的底边就落在锚点上；不画技能就绪点，技能状态只在底部动作栏。不画本人名字 |
| 画面回馈（浮动数字、奖励、击败、升级） | `ULDWorldFeedback`（与头顶标签同层同显隐）由纯值 `LDWorldFeedback::FTracker` 比较相邻样本得出事件，第一次样本、换局、换命或视图失效只重设基线，不补发历史。本人：`CombatView` 同代同命相邻修订的生命差（回血按 25 点/秒 × 间隔 + 1 的容差不飘字，生命上限变化不算伤害或治疗）与护盾差（增加、被打掉；自然到期不飘）；可见单位：相邻当前帧中同一 Handle/Epoch、同一最大值的生命差（营地无回血，容差 1 点），按帧的采样时刻等呈现时钟到达再显示，单位不再呈现即丢弃；施法名：可见施法者的新 `Cast` 提示带 `ActionAbility` 时，在其身旁飘一次技能显示名（`LDHUD::AbilityName`/内容表），不含等级、冷却，首见样本只建基线、无来源不显示（CB-P10/SC-07 白名单例外）；奖励：同一次成长发布里 `CumulativeXP` 增加才飘“+流量”；击败：公开击杀通知中击杀者是本人且新鲜（6 s 规则）才出“淫了 某某！”；升级：等级上升出“涨粉了！LvN”横幅与本人脚下光环、光柱，新获得三选一时加副行“获得一次开盲盒机会”。不读其他身体 Actor 或他人投影 |
| 小地图 | 注册地面边界、静态阻挡（区分阻走/阻视线）、旗杆喷漆区、三档营地标记（精英为最大的紫色菱形）；当前覆盖与当前可见单位（参赛者用座位色圆点，位置取代理的呈现位置，不可呈现时退回帧内脚点）；不画探索层或出生点。朝向按本地设置 `MinimapOrientation`（跟随镜头/跟随角色朝向/固定正北），三种都是真实俯视的旋转加统一缩放，不镜像；`LDMinimap::MakeView/ToFrame/ToWorld` 是绘制、镜头框与拖图/右键移动共用的唯一换算，一次只显示一个区域：本人存活时按身体所在、否则按镜头中心落点，在休息室里显示休息室，其余显示野外；区域是该区域锚点（休息室：休息出生点、NPC、训练假人与报名圈；野外：其余锚点）的外接框加上距它 1000 cm 内的已画区域（围墙、树、喷漆区等），裁到注册地面内，两区域重叠时整层视为无效并记录。地图本身就是随视角旋转的当前区域（跟随镜头时是一个菱形），整块画进正方形框内、不裁切，另一区域的任何内容都不画，区域外的框角透明，按下时交给世界（`LDMinimap::OnGround`），拖图与右键移动钳在当前区域内；不画方位文字 |
| 属性视图（P） | 按住可改绑的 `Details`（默认 C）暂显加点图标与已装构筑，松开恢复。`ULDStatBar::ToggleView` 在加点图标与属性视图之间切换（`ULDHUD::ToggleAttributeView`，HUD 按钮 `HUDAttributes` 与 Attributes 键共用；按钮写出切换后的视图名）。属性视图只格式化 `CombatView.Attributes`：`LDHUD::AttributeViewOrder` 九项，每项 `FAttributeRowView::Glyph` 图标与 `Value`（`LDHUD::AttributeRow` 把攻速换成取整的次/分钟、移速不带单位、急速换成冷却缩减百分比，规则值不变），三列网格与加点行同高；悬停交给 HUD 弹层名称、`Final` 与 `Breakdown`；投影缺失时显示“属性同步中”。每局从加点图标开始 |
| 构筑选择卡 | `ULDChoicesPanel` 只读本人 `FLDProgressionView` 与卡面数值所需的属性，由右下角“构筑”按钮切换（HUD 用 `SetTrayInset` 让它贴在按钮正上方）。没有面板、标题、说明文字或滚动：每个待做决定一行卡，自下而上依次是主动候选（`GrowthOffer0..2`）及 `GrowthOfferSkip` 不选卡（同组带框并显示当前机会序号/总数）、开局基础构件（`GrowthBase<n>`）、精英二选一（`GrowthComponentChoice<r>_<o>`，都已持有时同一行多一张 `GrowthComponentChoicePoints<r>` 点数卡）与有空槽时的待学图纸（`GrowthBlueprint<n>`）。卡上只有图标（缺图时为名称首字）与名称；`LDHUD::ChoiceTrayTiles` 为真（构件卡合计多于 `ChoiceTrayCardLimit`=6）时构件各行改为只有图标的网格。悬停时 `Hovered(Pointer,…)` 把该卡的 `LDHUD::ActiveChoiceDetail` / `ComponentChoiceDetail`（名称、种类与目标/消耗、完整规则、关键词解释、现在拿不了的原因或点击去向）交给 HUD 的槽位弹层；`PreviewOffer` 只钉住某张主动卡的弹层供评审截图。点卡即提交原命令：主动带 `bLearnIfPossible` 学进最低空槽，普通开局基础构件同一提交装到本体、B18 只领取进构件库，图纸卡学进最低空槽。拿不了的卡禁用并把图标调暗，卡面只在定义、结果说明或拥有者数值变化时重新渲染。结果与拒绝都经 `Notify` 进 HUD 提示行。每局首次主动机会或开局构件仅非野外阶段自动展开一次；不因交战或 PK 收起：只有自动展开的在本人受击或几秒未操作时收起（`UpdateFold(bOwnHit)`）；死亡等待者同样使用；收起不丢已提交请求。休息 NPC 服务是同一面板的独立窗口，只有在 `OwnerState.NearbyRestService` 指向的现有150 cm上下文内主动交互时才显示；技能商人抽主动、构件工匠补缺/拆解、训练师洗点/替换/遗忘（训练师窗口另列待学图纸的“学到/替换 Q/W/E”行）。窗口不滚动，放不下时整体按比例缩小。价格、候选、准入、事务及拒绝反馈仍来自现有目录、`BuildGate`、`MaterialServices` 与服务器回执，不由 UI 授予物品或改数值。训练师与遗忘按钮、确认行、卡片去向与回执里的槽位都写 HUD 上该槽当前显示的按键（`LDHUD::FSlotKeys`，由本人改键与 HUD 拖动换位决定），控件名仍用服务器槽位 Q/W/E。打开只读蓝图时收起选择卡，反向打开时收起蓝图。 |
| 属性与加点 | `ULDStatBar`（`ULDHUD` 放在底部带左块名称下方）只读本人 `FLDProgressionView` 与内容目录：按 `StatsInDesignOrder` 每一维一个图标（`LDHUD::StatRow` 的 `IconId`，缺图显示首字），角上写已分配点数（`StatPointsOf`），有排队或在途的点时写“K+S”；悬停的图标正上方的预留条里出现“+”（只在还有未分配点数时出现，`LDHUD::BuildGate` 不允许加点时置灰），悬停说明用 Q/W/E 格同一个说明层：名称、“已加 K 点 · 未分配点数 N”，再按该属性的每点加成顺序每项一行“属性名 当前值（每点 +收益）”（`LDHUD::StatEffects`，当前值取本人 `CombatView.Attributes` 的最终值，读不到时写“?”），以及不能加点的原因。点“+”即发送一点 `AllocatePoints`，无暂存与确认；一次只有一个请求，回执生效后要等本人视图带上回执的构筑版本（最多 5 s）才发下一次，期间的点击在本地排队后合并发送；控制器的唯一构筑在途槽被构筑选择卡或构件库占用时同样等待。被拒、回执过期或本地拒绝都丢弃排队的点并在提示条写原因；换局清空。头像左下角黄色圆圈写本人视图的未分配点数（0 时隐藏），等级仍在右下角。只是本地呈现，不预判结果 |
| 构件库 | `ULDComponentStorage`（`ULDHUD` 放在画布上、在主面板弹层之下，不是主面板）只读本人 `FLDProgressionView` 与内容目录，模型在 `LDHUDModel.h`：`LDHUD::ComponentStorage` 把已持有构件分成库存与五列已装（`EStorageColumn` 为 Body/SlotQ/SlotW/SlotE/Dash）：本体放基础构件与以自身为目标的改装件，绑定技能或以技能为目标的进对应服务器槽位的列，以冲刺为目标的进冲刺(D)，装在构件上的改装件/连接件沿宿主链归到宿主所在列并嵌在宿主下面；`ComponentDrops` 给出拖动中某件的合法落点，`FindDrop` 查指针下的落点，`DetachedWith` 列出拆下宿主时一起退下的件；`FStorageView::Installed/Capacity` 是已装件数与本级工坊容量（`LDProgression::WorkshopCapacity`），`WorkshopFull` 为真时库存件没有落点（已装件的搬动仍有落点，先卸下再装），`StoredPlaceText` 给库存件悬停的位置行（满了写“工坊已满 y/y，先拆下一件”）。牌堆在底部 HUD 横条右侧，显示件数与从未悬停查看且从未安装的“新 n”，牌堆名下面一行“已装 x/y”（满时为强调色）；悬停向上展开成图标网格（每行 4 个，最新的离牌堆最近，卡面只有图标与从未悬停查看且从未安装的“新”角标；不写标题、件数或操作说明，库空时不展开网格），同时升起一排各列：本体在第一个 Q/W/E 格左边一个格距处，Q/W/E 在本地槽位顺序下显示该服务器槽位的格上方，冲刺在 D 格上方，各自居中；只有本体列写列名，其余列不写键位字母（`FStorageColumnView::Title` 对技能列是该技能名或“空技能槽”、对冲刺列是“冲刺”，只用于提示条里的落点名）；悬停任一图标显示完整说明，位置行之上先写 `FComponentCardView::KindLine`（类型“基础规则/技能改装/联动规则”加内容目录 `tags` 的用途标签，如“技能改装 · 弹道 · 法术”；构筑选择卡的构件弹层摘要也用这一行）。拖动库存卡到合法列或宿主上发 `InstallComponent`（合法落点绿边、其余变暗、“放这里”占位）；B18 落在技能列上先弹火/水/电选择，C12 先弹可重复的已安装基础构件，取消不发送；已装件拖回牌堆或列表发 `UninstallComponent`（提示条写出一起退下的件），拖到另一处为搬动：先卸下，视图确认后再安装。与构筑面板各自持有在途请求、经同一 `SubmitBuild` 与回执；交战中、死亡等待与 PK 中都可用，阶段关闭由服务器拒绝并显示原因；Back（Esc）先关闭选择或放弃拖动（`ULDHUD::CancelStorage`）。只是本地呈现，不授予或预判结果 |
| 旗杆喷漆与守护、击杀通知与结果 | `ALDGameState::PublicMatch`（唯一阶段来源，校验代次）经 `LDHUD::Circle(Public, SprayerName, OwnerName, bClock, NowMs, SprayMs, GuardMs)` 显示喷漆行与涂鸦/守护行：发育期固定为“猥琐发育中，别浪 · 旗杆在最后一次休息（R3）后开放”（最终野外开始前 `OpenAtMs` 未排定，不打印开放倒计时）；独自喷漆“X 正在旗杆上喷漆 · 剩 <秒>s”；争夺“旗杆底下打成一锅粥 · X 的喷漆进度先存着 <已存>/<所需> s”（十分位，无持有者时“谁也没存下进度”）；守护运行（主人独自在区内）“旗杆上是 X 的涂鸦 · 再守 <秒>s”，主人以外有人在区内而冻结时“旗杆上是 X 的涂鸦 · 有人进圈，守护冻结（再守 <秒>s）”，主人不在区内、区内无人而暂停时“旗杆上是 X 的涂鸦 · 守擂者不在圈内，守护暂停（再守 <秒>s）”；无主且无人时“旗杆空着，没人敢上？”；两部分用“ · ”连接。进度条独自喷漆时为喷漆进度、争夺时为暂存喷漆进度，其余为守护进度。`KillFeed`（按序号去重，只含参赛者死亡）、`MatchResult`（与 `PublicMatch.ResultId` 一致才显示，缺失时“结算同步中”而非零表；守护胜利标题“守护成功 · X 的涂鸦守住了，恭喜成为今晚的淫人王！”，房主结束“房主结束了本局 · 没人守住涂鸦”）；倒计时到零只等待权威状态，不宣布开放、喷上涂鸦或胜利 |
| 命中、失败与进度 | 经裁剪的自身反馈，而非完整服务器日志 |
| 客户端试玩日志（`FLDPlaytestTrace`，只在远端客户端） | 与本表相同的来源：本人 `OwnerState`/`CombatView`/成长投影、房间公开的 `DemoLobby`/`KillFeed`/`MatchResult`、本人当前授权帧中的可见单位（所见施法与其位置），以及本人发出的订单与输入；伤害/护盾/状态事件取自同一 `LDWorldFeedback::FTracker`。不读其他身体 Actor、他人投影或权威记录；完整权威记录只在 Host 日志。行契约见[架构与网络](ARCHITECTURE_AND_NETWORKING.md#运行日志与试玩记录) |

成长请求一个家族同时只有一个在途：面板在提交前先用 `PeekGrowthRequestId` 记下将分配的序号，
因为 Listen Host 会在提交调用内回答；只在 Controller 也结束该请求时（Final、未保留序号的拒绝、
Final/过期回执）解除等待。

名称来自复制的 `ALDGameState::DemoLobby` 名册，查不到时显示“未列名参赛者”。
技能、野怪和状态的中文名经 `LDHUD::ContentText` 从 `DemoContent` 字符串表读取；
表或键缺失时显示稳定 Id 并记录 `LD_HUD_DISPLAY_TEXT_MISSING`，不猜名称。

**技能卡面**：主动显示行的 `description_text` 是带 `[op.field]` 占位的模板（如 `[damage.amount]`、`[root.duration]`）。
`LDDemoCardText::Parse` 把模板拆成文字与占位，占位必须指向该主动唯一一组效果值里的一个 typed 字段；导入器与 `DemoContent.py` 的 `card_template` 检查都拒绝指不到效果的模板。
`LDHUD::CardText` 按本人当前等级成长这组值，再以本人投影的攻击力/法强/生命上限/护甲（`LDHUD::CardStatsOf`，无投影时为 0）算出数字；
读不到的占位显示“?”并标记不完整，解析或成长失败整句显示“（卡面数据缺失）”。五维属性卡用 `LDHUD::StatCardText`，占位只读该属性的每点收益。
`LDTerms::CardKeywords` 的关键词在富文本里加粗（同一位置取最长匹配），`LDHUD::KeywordLines` 按术语表顺序给出“关键词：解释”行。
构筑选择卡、Q/W/E 动作格（`LDHUD::ActionDetail`）与状态方格的悬停说明使用同一个说明层。
`LDHUDModel`、`LDHUD`、`LDMinimap`、`LDUIRoot` 及各 Panel 是主要 UMG/Slate 入口。

小地图的地面底色/当前覆盖与静态地形分开合成，保持“底色 → 覆盖 → 半透明区域填充 → 轮廓”的层次。
`LDMinimap` 在 tick 中将静态填充与轮廓用原来的 Slate 扫描线、实线/虚线及抗锯齿栅格化为透明 render target；
只栅格化当前显示的区域；仅世界身份/布局/几何 revision、显示区域、view scale、控件尺寸或显示 DPI 变化时重建（进出休息室各重建一次），固定缩放下转向不重建。
纹理按显示 DPI 生成并留出轮廓抗锯齿边缘，经现有 north-up `Plane` 几何旋转后只提交一个 box；
使用与 Slate retainer 相同的 gamma-space、premultiplied-alpha 合成，避免半透明填充重复乘 alpha 或改变颜色。
视野覆盖使用独立 128×128 最近邻纹理，逐格保留授权 `CurrentBits`，以接收端观察 serial 与本人 Match/Life 去重上传；
paint 再次检查当前帧状态及 Match/Life，死亡、过期或他局帧立即不画旧纹理，不保留探索层。
固定地点图标保持直立；自身、可见单位、镜头框及公开事件标记仍逐帧经同一 view 绘制。
`Lockdown.Rules.Minimap.StaticInvalidation/CoverageTexture` 覆盖静态失效键、覆盖逐格颜色/位序、上传去重键与权限清除。
属性、三选一两个普通主面板（以及 DEV 测试房间的“测试”面板）由 `LDHUD` 创建并停靠在自身画布上（命中测试只算面板本体），
并向 `ULDUIRoot::RegisterMainPanel` 登记；同一时刻只开一个，权威事件用 `ShowMainPanel` 打开，
不会顶掉玩家随后打开的菜单或设置。

受控角色、检查对象、准备中的动作、指针捕获、镜头模式和打开面板分别管理。
选中另一角色并不改变技能按钮的所有者；禁用按钮只是反馈，不能代替服务器检查。
Widgets 不得遍历所有 Pawn/PlayerState 来补全 Tooltip 或小地图。

## 构筑蓝图文件与查看

局末构筑蓝图是本人的只读记录，只保存配方，不授予能力、不恢复遗忘的技能，也不写任何运行状态；
台账与归属规则见[战斗与成长](COMBAT_AND_GROWTH.md#局末构筑蓝图)。

- **只给本人**：结果发布时 Host 为每名在场真人组好记录，经 `ALDPlayerController::BuildBlueprints`（`ULDBuildBlueprintComponent`）
  先发 `ClientBlueprintOffer`（总字节或明确的失败原因），客户端按顺序用 `ServerBlueprintChunk` 拉取 16 KiB 分块，服务器只回应当前结果
  的下一个偏移，重放、跳块或旧结果都被拒绝；Listen Host 本人直接交付字节。传输中断或校验失败的部分记录不保存、不显示为蓝图。
  AI 不生成记录；房主断线或故障结束的对局没有锁定结果，因此也没有蓝图。
- **格式**：UTF-8 JSON（规范键序、整数数字），`schema=lockdown.build-blueprint`、`schema_version=1`，扩展名 `.ldbuild.json`。
  顶层为 `origin`（`auto`/`export`）、`complete`、`product_version`、`recorded_at_utc`（本机收到时的 UTC，保留按它排序）、
  `rules`（协议版本、目录根摘要与 schema/handler/projection 修订）、`match`（公开 ResultId、模式、结束原因、胜负、毫秒时刻）、
  `player`（最终等级与 K/D/A）、`final_build`（最后版本的结构与修订）和 `versions`（每个版本的修订、时间段、等级、`dev_layer`、
  结构与 `facts`）。结构的 `components` 与每个事实都带 `collected`；当前写入者全部采集（构件为已安装构件及其 `host` 与按键排序的
  安装参数 `target_kind`/`element`/`bound_component`，没有安装时为空列表），早期写入者未采集的项值为 `null`，读取时照原样保留。不含参赛者 ID、账号、地址、原始 trace、
  未来种子或别人的构筑。
- **保存与保留**（`LDBuildBlueprintStore`）：拥有者在本机 `FPaths::ProjectSavedDir()/Blueprints/match-<ResultId>.ldbuild.json` 写入，
  先写同目录临时文件并回读，再移动到位并回读；成功后才清理：只处理该目录内、文件名就是自身 Match ID、通过严格校验且 `origin=auto`
  的记录，按 `recorded_at_utc` 删最旧的，直到最多 50 份、总计不超过 100 MiB（常量在 `LDBuildBlueprint`）。导出、导入目录、
  其他文件与无效文件都不动；删除失败会明确提示。完整记录超过 2 MiB 时不截断，自动保存失败并说明原因，本局记录仍可在蓝图页只读查看
  （传输上限 8 MiB）。写入失败时旧蓝图保留。
- **页面**（`ULDBlueprintPanel`，结果页与大厅的“构筑蓝图”按钮）：显示来源、保存状态（`Saved/Blueprints/...` 相对路径或失败原因）、
  目录不一致/产品版本不同/失效安装/不完整等提示。默认展示最终 Q/W/E 槽位、属性分配与构件图标；
  有有效本人局内投影时标注“本局已有/本局缺少”，没有投影则明确持有状态不可用，未采集不读作没有。
  悬停看独立右列详情，点击固定；说明按本机目录和记录的最终等级解析，依赖未记录属性的数值显示加成公式，不猜测历史属性；
  安装目标与绑定参数使用可读名称。历史结构、时间段与事实收进“展开对局数据与版本记录”，不再默认平铺全文。
  构筑与蓝图面板使用不透明背景；蓝图外框与左右滚动区有固定边界（蓝图查看不算游戏界面，可以滚动）。页面明确“只读 · 不授予物品或技能”，没有整套保存/应用运行命令。
  “导出文件”把本局记录以 `origin=export` 写到 `Blueprints/Exports/build-<id>-<时间>.ldbuild.json`（不覆盖已有文件，不会被清理）；
  “打开所在文件夹”建好 `Exports`、`Imports` 后用系统文件管理器打开 `Blueprints`；“导入查看”列出 `Blueprints`、`Exports`、`Imports`
  里的文件，选中后先查大小（≤2 MiB），再做严格 UTF-8、JSON、schema/版本、字段类型、未知字段与版本衔接校验，失败时显示具体原因
  （未知版本明确说明无法读取），通过的只读展示。分享由玩家自行发送文件；不建账号、不上传、不发 Discord 或网页链接。

## UI 交互与错误

输入捕获、焦点、Esc、菜单与镜头的共同约束见 [移动与输入](MOVEMENT_INPUT_AND_CAMERA.md)。
界面使用当前映射注册表显示按键，不能写另一份易失效的按键表。
等待、成功、失败、失去权限和过期状态需区别显示，不以本地超时虚构成功。
本地设置的类型化加载/保存、显式恢复与固定无边框窗口见
[移动与输入](MOVEMENT_INPUT_AND_CAMERA.md#本地设置与显示)。
设置页的显示草稿与已应用偏好分离，终局强制关闭调用 `ForceClose`，
不能把关闭或销毁当成应用显示草稿。

数据更新应围绕已有快照/回执和局部变化，不为每个 Widget 新增一次全世界扫描。
增加 HUD 字段时同时检查服务器是否授权它、离开视野如何清理、life 改变如何失效，
以及对应提示、特效和辅助显示是否绕过了同一限制。

## 音效与音量

`ULDAudioSubsystem`（GameInstance 子系统）在启动时一次性加载 `/Game/Lockdown/Audio/DA_DemoAudio`
（`ULDAudioCatalog`），按 `ELDAudioCue` 解析全部音效、按 `ELDMusicTrack` 解析全部背景音乐。目录缺失、条目缺失或乱序、声音未归入
`SC_DemoMaster`（音乐未归入 `SC_DemoMusic`）、音效循环、音乐循环标志与曲目不符或没有时长都记录 `LD_AUDIO_CATALOG_INVALID` 错误，不静默降级；
没有音频设备（如 `-nosound` 自动化）只记录一次 Display 级 `LD_AUDIO_DEVICE_UNAVAILABLE`；
目录解析失败后，首次请求播放还会记录一次 Warning 级 `LD_AUDIO_CUE_UNAVAILABLE`。
所有音效都以非空间化 2D UI 声音（`PlaySound2D`）播放，不带世界位置，不能借声音暴露隐藏坐标。

`ULDHUD` 每帧把下表的只读输入交给 `FLDAudioCueTracker`，再由子系统播放。
跟踪器没有观察单位、Actor 或他人私有状态输入；新代次的首个样本只建立基线，不补放之前的变化。

| 音效 | 唯一来源 | 只响一次的规则 |
| --- | --- | --- |
| 击杀通知 | `KillFeed` 新序号，与横幅相同的 6 秒新鲜度；本人是受害者时由本人死亡音效代替 | 按代次与序号 |
| 旗杆开放 / 新喷上涂鸦 / 开始喷漆或换人 / 争夺 | `PublicMatch` 的开放状态、`CaptureEpoch` 与 `GraffitiOwner`（只在 CaptureOpen 阶段）；出现新的有效涂鸦主人时播放开放同款旗杆号角（`CaptureOpened`），优先于本次喷漆区变化的提示音；否则 Solo 为 `CaptureStarted`、Contested 为 `CaptureContested` | 每个 epoch 一次；喷漆区类提示 1 秒内最多一次，新喷上的涂鸦不受这一限频 |
| 本人胜 / 负 / 无胜者结束 | 与 `PublicMatch.ResultId` 一致的 `MatchResult` | 每个 ResultId 一次；冻结 10 秒后才首次看到则不响 |
| 本人受击 | 同 Match/Life 的 `CombatView` 新 Revision 中生命下降且上限未降；致死一击交给本人死亡音效 | 0.2 秒内最多一次 |
| 本人护盾吸收（`SelfShieldHit`） | 同一投影中生命未降而护盾减少，且护盾状态仍在或在其列出的到期前消失（与护盾吸收飘字同一规则）；护盾自然到期不响 | 与本人受击共用 0.2 秒限频 |
| 本人普攻出手（`SelfAttack`，不是命中确认） | 同 Match/Life 的 `CombatView` 新 Revision 中 `Cues.ReleasedAtMs` 晚于上次所见且 `bReleasedBasic`（近战出手或飞行物发射的时刻，Nav 与 Direct 来源相同）；技能的释放交给下一行的回执 | 每次出手一次；新 Life 的首个投影只建立基线，死亡、旧生命与重放不响 |
| 本人技能或 D 冲刺释放成功 | 本人回执日志 `CombatReceipts` 中音效游标之后的每条回执：Q/W/E/D，Finished/Released 且无拒绝 | 每个请求一次（同一帧到达的多条回执逐条判断，前一条的释放不会被后一条覆盖；同一帧多次释放合成一次）；别局或别条命的回执不响 |
| 升级或出现新的三选一 | 本人 `Progression` 的 Level 或 EarnedThrough 增加 | 选卡后显示排队中的下一组不算新机会 |
| 本人死亡 / 复活 | `OwnerState` 同一 Life 由 Alive 变 Dead / 新 Life 为 Alive | 开局首次出生不响 |
| 界面点击 | 设置页主音量滑条松手或键盘调节时试听 | 每次调节一次 |

主音量是本地设置 `MasterVolume`（无量纲 0..1，默认 1 即不衰减），
经 `SMix_DemoUser` 对 `SC_DemoMaster` 的类覆盖生效，每个新世界重新应用，不修改共享资产或 CDO。
覆盖实际交给音频设备时（首次推送和每次音量变化）记录 Display 级 `LD_AUDIO_MIX_APPLIED device=<id> volume=<v>`。
新增的任何 Demo 音效都必须归入 `SC_DemoMaster`、音乐归入 `SC_DemoMusic`，否则目录校验失败、音量也控制不到它。

### 背景音乐

背景音乐有三首，枚举 `ELDMusicTrack` 的编号是稳定技术身份：

| 曲目 | 何时播放 | 播放方式 |
| --- | --- | --- |
| `Menu` | 主菜单，以及房间的 Lobby、Starting、Countdown、Resetting | 循环；主菜单与大厅共用，进出大厅不重启 |
| `Farm` | 发育期（Development） | 从 T=0 起只播一次。曲长约 181 s，对齐 `MATCH.DevelopmentSec` 的 180 s 旗杆开放；中途开始时从 T=0 起已过的秒数接着播，超过曲长则不播 |
| `Escalation` | 旗杆开放（CaptureOpen）到本局结束 | 从头循环 |
| 无 | Results、Closing、Faulted | 淡出，让胜负短曲单独响；回到 Lobby 时 `Menu` 重新开始 |

`ALDPlayerController::UpdateMusic` 每帧把“本地世界是否为前端”、房间公开的 `PublicMatch` 与共享权威时钟交给纯函数
`LDAudio::SelectMusic`（`Audio/LDMusicSelection.*`），再由 `ULDAudioSubsystem::SetMusic` 播放。它不读本人或他人的私有状态、单位或 Actor。
房间状态尚未到达（没有 GameState 或 generation 为 0），或发育期没有时钟、没有 T=0 时，请求为“未决定”，保持当前曲目，
所以主菜单到大厅的旅行不会打断 `Menu`。请求的曲目与当前相同则什么都不做；换曲时新曲立即开始，
旧曲在 `LDAudio::MusicFadeOutSec`（1.5 s）内淡出。音乐组件跨关卡保留（`CreateSound2D` 的 persist），子系统关闭时停止。
每次换曲记录 Display 级 `LD_AUDIO_MUSIC from=<id> to=<id> start=<秒>`，一次性曲目超过曲长时附 `past-end`。

音乐走 `SC_DemoMusic`，它是 `SC_DemoMaster` 唯一的子类。`SMix_DemoUser` 上有两条类覆盖：主类（含子类）乘主音量，
音乐类再乘本地设置 `MusicVolume`（无量纲 0..1，默认 0.6），所以音乐的实际音量是主音量 × 音乐音量；
`LD_AUDIO_MIX_APPLIED` 同时记录 `volume=` 与 `music=`。

三首曲目是项目所有者为本项目提供的原创作品，不是第三方素材包，因此不走 CC0 包与许可原文的检查；
清单 `music` 段逐首记录 `track`、`source`、`sha256`、`loop`、用途与来源说明（`provenance`）。源文件是
`SourceData\Audio\Music\*.mp3`（Git LFS），以源电平导入（SoundWave `Volume` = 1），不参与音效的响度归一，导入时字节不变。

音效素材全部来自 Kenney（kenney.nl）的 CC0 1.0 音效包，只提交实际选用的 `.ogg`（Git LFS）。
逐文件的包名、下载地址、下载日期、许可、用途与 SHA-256 记录在 `SourceData\Audio\audio_cues.json`（schema 2，音乐见上节）；
一条音效可以复用另一条的源文件（各自导入独立的 `SW_<Cue>`），例如护盾吸收复用受击的 `impactGeneric_light_000.ogg`；
本人普攻出手使用独立的 `SW_SelfAttack`，只表示释放，不作为命中确认。
释放事实只保留最后一次动作；同一采样间隔内被后续技能释放覆盖的普攻不能补放，不从其他单位血量推断。
`ELDAudioCue` 的编号是稳定的技术身份：删除的音效只留下编号空位，不复用、不重排其他音效；
编号 10、11、18、19 空缺；清单与目录按其余音效的枚举顺序排列，下一条新音效取 20。
kenney.nl 的下载地址是会过期的签名链接，包以 `zip_sha256` 识别。
各包的许可原文 `License.txt` 与文件放在同一目录。
每条音效的播放音量（SoundWave `Volume`）由创作脚本 `Scripts\MeasureDemoAudioLoudness.py --write` 测得：
BS.1770 K 加权、400 ms 窗口的最大瞬时响度统一到全部音效的中位数，提升以样本峰值 0.95 为上限，
结果连同 `loudness_lufs` 写入清单的 `volume`，导入时应用，源文件字节不变。
复用文件的条目可带 `level_db`，在归一与峰值上限之后再叠加（护盾吸收为 -6 dB，音量约为受击的一半）；不复用文件的条目也可带它，本人普攻 `impactPunch_medium_000.ogg` 为 -6 dB，连续普攻不刺耳；被复用的文件只计一次中位数。
该脚本需要 `numpy`、`scipy`、`soundfile`，导入与校验都不运行它；
很短的本人反馈音（受击、施法、点击、交易）受峰值上限约束，按同一测量仍比中位数低约 5–9 LU。
`Scripts\ImportDemoAudio.ps1 -Mode Import` 由该清单重建 `Content\Lockdown\Audio` 下的资产；
`-Mode Validate`（Bootstrap Validate 也会执行）拒绝清单外的文件、许可原文不是 CC0 的包、没有来源说明或循环标志与曲目不符的音乐、
源文件哈希变化、`SC_DemoMusic` 不是 `SC_DemoMaster` 的唯一子类，以及不是由当前源字节导入、或播放音量与清单不符的资产。资产经 `DirectoriesToAlwaysCook` 进入包。

## 检查入口

- `Lockdown.Rules.DemoVision.{Codec,Privacy,Ordering,Handles,Termination,Sight}` 覆盖线缆、白名单、游标、句柄与 pin、终结和视线几何。
- `Lockdown.Rules.DemoVision.ActionCues` 覆盖动作提示、提示龄与释放延迟的线缆规则、六个名册座位与身体色（座位互不相同、营地 NPC 不用座位色）；
  `Lockdown.Rules.DemoVision.Projectiles` 覆盖飞行物线缆上限与句柄冲突、同一视线规则的近者优先选择、逐观察者句柄寿命，以及接收端隐藏与超时清除。
- `HUDBasicAttackTerm`、`HUDIdentityPalette`（`LDHUDRulesTests`）覆盖“普攻”文案与头像/身体共用的六色；
  `HUDTermsGlossary` 核对术语表里重复的状态名与批准目录一致、系统术语不为空不重复且不与条目名相同。
  HUD 与头顶标签测试的条目名从 `LDDemoContentFixtures` 的 approved_base 读取（`LDTestApprovedDisplay`），世界观术语引用 `LDTerms`，不逐字重复。
- `Lockdown.Rules.WorldLabels.{ContenderName,CampMemberName,StatusLine,Whitelist,HealthBar,OwnBars}`（`LDWorldLabelsTests`）
  覆盖头顶名字、状态行、只显示白名单（法力、姿态与观察身份不进入标签）、血条比例与未知、本人血蓝条的门控与护盾收比。
- `Lockdown.Rules.DemoVision.PropFade` 覆盖可淡化道具的遮挡判断（镜头与身体之间、线旁、身体在前、身体站在旗杆里或紧贴旗杆、高于道具）与淡入淡出；`DemoVision.ActionCues` 另覆盖施法来源只在 Cast 时、最多一个；`WorldFeedback.Casts` 覆盖施法名只在新施法时出现一次、首见只建基线、无来源不显示；`LocalSettingsQuickCast` 覆盖快速施法默认关闭、保存回读、schema 9 迁移与非法值；`LocalSettingsCombatFeedback` 覆盖特效降低/镜头震动的默认值、保存回读与 schema 20 迁移。
- `Lockdown.Rules.DemoVision.Presentation` 覆盖呈现时钟：样本间线性插值、最新样本之后保持不外推、跳变样本不扫过、朝向走短弧，以及时钟偏移的下调、限速上调与重设。
- `Lockdown.Rules.WorldFeedback.{Priming,OwnDamageAndHeal,Shield,VisibleUnits,RewardsAndLevel,Kill,Text}`（`LDWorldFeedbackTests`）
  覆盖画面回馈事件：首样本与换代只重设基线、回血容差与最大值变化不飘字、护盾增减与自然到期、可见单位相邻帧差与重现不补发、
  奖励只在经验增加时出现、本人新鲜击败与他人击杀/过期通知不出、以及显示文本。
- `HUDObservationBoundary`（`LDHUDRulesTests`）覆盖检视器白名单和地图换算。
- `HUDLifeStatus`（`LDHUDRulesTests`）覆盖自身生命行：权威时刻缺失时显示不可用，倒计时在截止前不显示 0，死亡不显示成长损失。
- `HUDRegenDisplay`（`LDHUDRulesTests`）覆盖每秒回复格式、零回复、满资源、失效/死亡投影；`DemoCombat.RegenRates` 对照既有积分器验证条件回复率。Combat 拥有连接场景还检查满资源时脱战边界后的回复率在 Host 与真实客户端发布。
- `HUDMatchTimer`（`LDHUDRulesTests`）覆盖小地图上方的对局计时：开局时刻起算、向下取整、分钟不补零、时钟落后为 0:00、无权威时刻不显示。
- `HUDRestTimeline`（`LDHUDRulesTests`）覆盖时间线横幅（休息编号与步骤、剩余整秒、PK 报名窗口开放前/中/截止时刻、报名人数与本人报名、PK 跳过与参赛人数、锁定步骤、野外 n/3 与按 T 计的下次休息、最终野外、别代时间线不显示）、休息返回倒计时（向上取整、到点保持 1、野外开始的“开淫！”）与报名圈提示（只在报名窗口显示，没有报名按钮）、重置训练结果文案（排队成功与没有假人的原话、其余拒绝不读作成功）以及休息中名单外参赛者写作“训练假人”；`HUDBuildGate` 另覆盖休息/锁定/野外/关闭各构筑阶段的命令与休息服务门控、`StageForPublic` 的映射、四种休息服务回执与新增原因文案；`HUDPublicMatch` 覆盖发育期旗杆行不打印开放倒计时。
- `HUDPKView`（`LDHUDRulesTests`）覆盖赛制文案（单挑、混战、2v2、3v3，不等分时为空）、押注步骤只给有资格的观众每方一个按钮且本人押注取自公开押注、选手与名单外参赛者不能押注、观战判定（押注步骤不观战，PKReady/PK 中观众与出局选手观战、存活选手与空 id 不观战、`NoContest` 不观战）、PKReady 整秒倒数与到点保持 1、无时钟不猜、PK 最长剩余与安全圈两种预告行、投掷按钮的就绪/瞄准/冷却/在途文案、投掷冷却取整与无时钟、全部押注与投掷结果文案（只有成功读作完成、每个拒绝各有原话）、结算的胜方与奖励行（跳过零分、本人高亮、获胜提示）、平局与本场无效文案，以及返回步骤、别代 PK 与没有 PK 时不显示。
- `HUDFieldRewardNotices`（`LDHUDRulesTests`）覆盖野外奖励提示（四种事件名、流量、图纸/稀缺/普通构件、池空改得点数、二选一就绪、图纸保留，缺定义不冒名、未知事件不冒充、无内容不提示）、`NewFieldRewards` 的已知条数切分、`FFieldRewardWatch` 首份视图建基线、每条只报一次、别代不报与换局重建基线，以及待选二选一点亮构筑按钮与“全部已持有”判定。
- `HUDMaterialServices`（`LDHUDRulesTests`）覆盖二选一、补缺、拆解在休息/野外/阵亡/锁定步骤的门控原因，私有夹具上的缺口与补缺基础件、普通构件拆解候选、点数不足与候选不足的原因、拆解输入合法性、`DismantleDetached` 的传递退回，三种回执与野外奖励原因文案。
- `HUDCardText`（`LDHUDRulesTests`）覆盖卡面模板：每个数字读取拥有者在当前等级使用的那一档、成长与属性加成、升级卡的“A → B”与关键词加粗；
  `HUDCardSelfCheck`（`LDCardSelfCheckTests`）对批准目录检查每个主动、五维与构件都有唯一显示行和描述；主动卡模板可解析，每个占位读到非零数值且卡面至少显示一个数字；
  五维卡的占位只读非零的每点收益；文字不出现“喷漆”“守护”“未知效果”“?”或“内容目录中找不到”。
- `HUDOwnResources`、`HUDActionView`、`HUDPublicMatch`、`HUDContentParameters`、`HUDCombatOwnerView`（`LDHUDRulesTests`）
  覆盖本人资源与等级、Q/W/E/D 意图与限制分离、旗杆喷漆/涂鸦/守护行与进度条、击杀/结果（含守护成功标题）、内容参数行与原因文案、本人状态方格的提示文字（含脱战回复与躺平角回血方格及其说明）与战斗回执提示。
- `HUDIcons`（`LDHUDRulesTests`）用批准目录核对每个主动、五维属性与状态的 IconId 以及冲刺、两种回复的图标都有已导入的纹理，缺失的 ID 记录 `LD_UI_ICON_MISSING`。
- `HUDSelfIcons`（`LDHUDRulesTests`）覆盖状态方格的缺图首字、不设上限（控制状态在前、回复在后），说明层的首行与类别（含正负面），每个状态编号的正负面分类，以及剩余时间圈的满圈、刷新重满、提前结束与消失后重计。
- `HUDCooldownText`、`HUDActionStates`、`HUDDeathCause`、`HUDKillFeedSelf`（`LDHUDRulesTests`）覆盖冷却秒数文字（不提前显示 0）、
  动作格的冷却/法力不足/受控/空槽/准备态分类、法力消耗/等级数字与图标首字、本命之后的死因取值，以及击杀通知里本人写作“你”。
- `HUDScoreboard`（`LDHUDRulesTests`）覆盖按住 Tab 的排行榜：击杀多在前、相同时死亡少在前、再相同保持名单顺序，本人/AI/已离开的名字标注，
  别的代次或没有对局时为空。`DemoCapture` 规则测试另拒绝无效 ID、重复参赛者或超过 6 行的 `KillFeed.Scores`。
- `Lockdown.Rules.BuildBlueprint.{Serialize,Validate,Prune,Components}`（`LDBuildBlueprintTests`）覆盖构筑蓝图的规范序列化与往返（未采集为 `null`、
  构件为空列表、不含参赛者身份）、读取拒绝（超 2 MiB、非 UTF-8 与超长编码、非 JSON、错误 schema、未知版本、缺字段、非法槽位、
  未采集填 0、未知 origin 与未知字段、未知安装参数或元素）、目录与失效安装提示（含构件、宿主与复用基础件），以及只清理本 schema 的自动记录（按时间最旧、50 份与总字节上限，
  导出、未知版本、损坏与他人文件不动）和超过 2 MiB 时不截断、不写入、旧蓝图保留。
- `HUDEventFeed`（`LDHUDRulesTests`）覆盖左下（小地图上方）事件播报：中途进入不补播、每个公开变化只播一次且最新在下、本人行高亮、
  6 条上限与 5 s 离开、外卖被抢行在本人领取序号到达后改写、老板击败者署名、同一主人被冻结的守护归零只播一次
  （暂停、冻结与恢复本身不播），以及新一局从空列表开始；另覆盖外卖无人领取消失、老板退场、
  营地标记公开（高档/精英各一行）、被领走（本人野外奖励记录到达后改写为“你领到了…”）与过期各只播一次。
- `HUDChoiceTray`（`LDHUDRulesTests`）覆盖构筑选择卡的弹层（种类与目标/消耗、规则、关键词转为纯文本、结果说明行，未知技能不编造目标与消耗）与 `ChoiceTrayTiles` 的网格阈值（开局候选、未持有与都已持有的二选一各自计数）；
  `HUDBuildGate` 另覆盖 `GrowthPending`：只有显示中的候选、有空槽的图纸、开局自选与精英二选一点亮构筑按钮，排队机会数只用于脉动，未分配点数与别局投影不点亮。
- `AudioCuePublic`、`AudioCueOwn`、`AudioCueBasicAttack`、`AudioCatalog`、`AudioCatalogAsset`（`LDAudioRulesTests`）覆盖公开与本人事件到音效的映射、
  首样本基线与去重、别局/别命/迟到回执不响、目录完整性（含音乐类的父子关系与每首的循环标志），以及实际 `DA_DemoAudio` 资产可解析、
  `Farm` 曲长不短于且不超过发育期 2 s；`AudioMusicSelection` 覆盖各阶段到曲目的映射、未决定的情形与 `Farm` 的起点；
  `LocalSettingsMusicVolume` 覆盖音乐音量的默认 60%、保存回读、拒绝与 schema 19 迁移。
  它们不证明实际听感、换曲淡出或打包后的设备输出。
- `ViewPrivacySchema`（`LDViewTests`）覆盖公共 GameState（只有 `DemoLobby`、`DemoClock`）以及 Lobby、DemoClock 的结构。
- `RunDemoVisionCheck.ps1`（`Tests\LDDemoVisionProbe`）：参赛且渲染的 Listen Host 与两个拥有客户端经正常大厅开局，
  三具身体走到从公共旗杆附近有界选择、互不占点且相互可见的合法目标（保持 150 cm 到达阈值；三目标验证原生地面，每人实际路线与绕行点验证原生完整路径）。仅 Vision 在原有作者营地家位置/Boss waypoint 排除区外，追加同 surface、按 id 循环连接四个公开 Boss waypoint 的原生完整路径走廊；膨胀半径为 AggroRadius＋本人身体半径＋150 cm 到达盘＋12 cm waypoint 接触余量，目标与路线均避让，不读实时 Boss 状态，Growth 保持原行为。路线候选以 5 ms 预算逐帧推进准备，原生单次查询不可中断，结果分别记录 tick 工作耗时、最慢查询与路线墙钟时间；12×3 目标/路线选择另由 callback seam 确定性规则覆盖，不宣称规则或单次场景证明所有出生点的原生导航。到达后检查拓扑、客户端受限注册表、Host 只经自身投影呈现他人（含生成当帧隐藏）、
  单连接丢帧（引擎网络模拟，非 Shipping）后的超时清除与无 LastSeen，以及客户端断开后 Host 不再呈现离开者。
  正常开局门禁打开前它无法运行；未实际运行不算通过。
- 必须观察真正受限的客户端及其可访问记录，包括 Listen Host 上非本地原生身体是否隐藏；不能只看服务器全知视图或一张截图。

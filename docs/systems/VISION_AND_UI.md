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
| `ALDObservedProjectileProxy` | 客户端非复制的可见飞行物（阴阳怪气 `active_snark_bolt`，美术是现有粉笔；+X 朝飞行方向，带 Combat 的 `ULDFlightTrailComponent` 粉笔灰拖尾：从粉笔尾端起 14 个白色粉团由约 20 cm 渐细到 8 cm，间距随近期速度、最多 11 cm，1600 cm/s 时约 1.5 m 长、前半段连成一条实线，静止时不显示；布局为纯函数 `LDFlightTrail::Layout`，由 `Lockdown.Rules.DemoVision.FlightTrail` 覆盖，`RunDemoCombatCheck.ps1` 的 `HostSees-ChalkFlight.png` 只在拖尾已绘制时截取），无碰撞、不可选为目标；帧不再列出后只按呈现时钟走完已收到的最后样本（最多 0.4 s）再销毁，stale、复位或他局视图立即销毁 |
| `LDPropFade`（relay 内） | 客户端每帧检查带 `LDFadeable` 组件标签的道具（美术目录 `fadeable`：旗杆，以及 #97 的两种树、集装箱与废车）是否挡在镜头与本人存活身体或当前呈现代理的胸口/头部之间，或因装饰无碰撞而穿过站在其中或紧贴它的身体（水平距包围盒 ≤ `PierceRadiusCm` 45 cm 且在其高度内）；挡住时把材质参数 `LDFade` 在约 1/6 s 内调到 1，不挡回到 0。relay 给每个淡化材质设 `LDFadeStrength` = `FadedStrength`（1），所以完全淡化时旗杆消失，而不是留下一条抖动网点柱；淡化期间关闭投影。只影响显示，不改碰撞、视线或导航；材质缺该参数时退回遮挡期间隐藏并记录 `LD_FADEABLE_PARAMETER_MISSING`。只测已授权呈现的单位，不因隐藏单位而淡化。#97：实例化道具（每种道具一个 ISM，带一个每实例自定义浮点）逐实例判断与淡化，只淡化挡住的那一棵树或那一个集装箱；`LDFadeStrength` 设为 `InstanceFadedStrength`（0.6），完全淡化时仍留约四成像素，身体透出而方块仍可辨认；逐实例淡化不关投影 |
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
- 参赛者单位只携带 SC-07 白名单：参赛者 Id（客户端经公开名册换成名称）、等级、HP/MP、公开状态、两件装备，
  以及 Q/W 两个主动的原生 id 与等级（`Actives`，字段位 `LDObservation::Actives`，观察 schema 5；#112 §10）。
  `Actives` 由 Combat 的 `PublicActivesOf` 在构筑安装和身体准备时写进同一公开事实，与装备一样只随当前可见单位下发、失视即撤销；
  codec 要求每槽“id 与等级同为 0 或同为非 0”、等级不超过 `LDObservation::MaxActiveRank`（2）、两槽不同 id，营地成员两槽为 0，接收端只接受批准目录里的主动技能。
  目标卡（`LDHUD::ObservedCard`）据此显示 Q/W 小卡与“初级/高级”，达到目录最高档（`progression.max_skill_rank`）的一槽用强调色描边。不含被动、冷却、完整属性、金币或规范身体身份。
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
- 动作提示 `ActionCue` 为 `Attack/Cast/Hit`：Combat 在核心 `ReadObservedPublicFacts` 里给出当前提示及其开始的权威时刻，
  线缆只带提示与 `ActionCueAgeMs`（无提示时为 0），开始时刻晚于当前时刻时发布失败关闭（`LD_OBSERVATION_ACTION_CUE_INVALID`）。
  代理与本人身体经 `ALDCharacter::PresentationCue` 得到同一个 cue 字节：低 4 位是动作姿势，
  定身、回位、护盾、复活保护各有独立形状（脚下圆环并锁腿、背后标牌、头顶光环、竖直光柱），不只靠颜色区分。
- `Cast` 提示可带该次施法的技能原生 id（`ActionAbility`；其他提示与 D 冲刺的 `Cast` 提示为 0），
  由 Combat 在同一公开事实里给出；接收端只接受批准内容中的 id。这是用户批准的 CB-P10/SC-07 例外：
  只用于在施法那一刻于可见施法者旁显示技能名（`ULDWorldFeedback`），不进目标卡，不保留技能列表，不含等级或冷却。
- 飞行物（`Projectiles`）使用与单位相同的视线规则：观察者存活时，从 Combat 的 `ReadObservedProjectiles`
  选出在视线内的飞行，近的优先，每帧最多 16 个；超出注册地面的飞行只是不可见，非有限位置是故障。
  每次可见区间分配新的 opaque 句柄（与单位、终结共用句柄空间），只带地面点、朝向与外观，
  不带施放者、效果、来源或目标，不能被选中或下令；离开视线或飞行结束时随隐藏通知立即移除。
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

开局方向卡（#153）的技能分配不属于任何复制视图。`FLDChoiceOption` 只带方向 ID 和选项身份，
`ULDChoicesPanel` 的卡面完整显示方向、三项属性与“？随机主动（选了才揭晓）”；不从 UI 推算或提前读取技能。
成功的选择回执才带 `OpeningActiveId`，面板显示“开出了：X！”并从新的本人构筑显示初级 Q。
开局重抽按钮置灰并说明不能重抽，服务器仍独立拒绝绕过按钮的重抽请求。

- `ClientFullView`（不可靠）发送完整帧；`ClientConceal`（可靠）的 `Removed` 为空时表示代次复位；`ClientTermination`（可靠）发送终结通知。
- 三者共用一个游标（match, generation, revision），旧消息被拒绝。
- 收到 full 帧时先应用其中的终结，再替换单位集合；不在新帧里的单位以 `FullSetRemoved` 清除。
- 隐藏通知不能被迟到的旧快照撤销；旧隐藏也不能擦掉更新的重现，因为重现带新的句柄和 epoch。
- 0.25 s 收不到 full 帧就清除全部单位与飞行物并标记 stale：不外推位置，不保留 LastSeen。
- 换局（`OwnerState.Match`）、观察者生命变化、断线、局终和非法载荷（`ProtocolRejected`）都会清除状态。
  属于其他世界修订的帧会被拒绝。
- 清除时销毁对应代理及其表现。
- 拾取只对当前 full 帧中的同句柄/epoch 代理进行，使用其呈现位置、骨骼摆姿与可见材质 section 的三角面；
  不读取隐藏权威角色，不增加物理碰撞或 LastSeen。模型 LOD 须保留 CPU 数据（美术导入器生成，catalog 校验），
  每个模型每帧最多计算一次 CPU 蒙皮，不调用会刷新渲染线程的 `GetCPUSkinnedVertices`。
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
- 迷雾是覆盖在注册地面边界上的 128² 纹理（双线性采样）：当前覆盖亮（255），其余调暗到 `LDFogHidden`（190，#97 由 64 调亮到 140 再到 190，用户原话：“war fog color is too dark, make it lighter”，随后“further lighten fog of war color”）以便看清静态地形；没有探索层。
  每收到新的覆盖，各格在约 0.1 s 内从屏幕上的当前值过渡到新覆盖，只在两次已授权覆盖之间过渡。
- 采样节奏和帧格式（schema 3、覆盖位图 2048 字节）在代码中维护，不在这里设定 Demo 平衡值。
  采样按核心时钟排程：普通迟帧保持原相位，停顿后跳过错过的间隔，只采当前姿态一次，不补发历史批次。

## UI 只读模型

玩家可见的术语与带梗短句（资源与属性名、米/流量、普攻、冲刺、旗杆与阶段文案、击杀与结算文案等）只在
`Source\Lockdown\Public\UI\LDTerms.h` 定义一次，HUD、前端、面板、飘字、订单提示与测试都引用它；技能、装备、野怪与状态的条目名
仍由内容显示行提供。系统文案直接点名的几个状态名（护盾、定身、减速、嘲讽、缴械、眩晕、复活保护）
在术语表里另有一份，由 `Lockdown.Rules.HUDTermsGlossary` 核对与批准目录的显示名一致。

| 信息 | 允许来源 |
| --- | --- |
| 网络与帧诊断（右上角，可分别关闭） | 本地设置 `bShowNetworkDiagnostics`、`bShowFPS`，默认关闭。FPS/平均帧耗时/窗口峰值使用实际本机帧间隔（至少 0.5 s 窗口，不裁掉长帧）；RTT、抖动、收/发丢包、距上次收包时间直接读取本人 `UNetConnection`，丢包比例转换为百分比，不新增探测 RPC。远端本人还显示 CMC 校正频率与最近误差（匹配 ACK saved move 的位置差）。房主显示本机房主，不伪造远端 0 ms；无连接/无样本明确显示状态。面板不拦输入，不参与玩法判定 |
| 自身血蓝、护盾、临时状态与被动 | Combat 的仅本人复制 `ALDPlayerController::CombatView`（同 Match/Life 才使用；没有身体时退回 Lives 投影的血蓝）；`LDHUD::SelfIcons` 只整理：属性按钮下方的 6×2 方格（`SelfIconColumns/Rows`），先控制状态、再其他临时状态与正在生效的回复（`bOutOfCombatRegenActive` 为“脱战回复中”、`bDormRegenActive` 为“躺平角回血中”，无剩余时间圈，说明层按 REGEN/DORM 参数写出延迟、比例与中断条件）、再角色被动与装备被动（按装备来源各一次），被动光环与复活保护（生命行）不进格；每格是名称首字的占位图标（`IconGlyph`），按正负面着色（`FSelfIconView::bPositive`）：对承受者有利的状态（`LDHUD::StatusIsPositive`：护盾、增益、强化普攻、复活保护、回位）、两种回复与全部被动绿边，所有控制与减速、破防、灼烧红边（未知状态按负面），剩余时间圈同色，叠层数在右下角；悬停时用与 Q/W/装备格同一个说明层（停在方格正上方），由 `LDHUD::SelfIconDetail` 只为悬停的方格生成：首行是名称与层数/护盾/剩余时间（角色被动带“初级/高级”），次行是类别（控制状态/临时状态后接“ · 正面”或“ · 负面”；角色被动/装备被动，进阶时注明），正文是内容目录里的完整描述：状态与装备读描述文本（装备另附被动效果行），角色被动用与属性面板、开盲盒同一卡面（`LearnedCardLines`，按本人等级与属性填数，高级时附“高级：”行）；超过 12 项时格下显示“…”，完整列表在属性面板；本人投影缺失时显示“状态同步中”。有截止时刻的临时状态由 `ULDHUD::NativePaint` 在方格上画剩余时间圈（暗色底圈 + 从 12 点顺时针收缩的亮弧），比例来自 `LDHUD::FStatusRings`：投影只带截止时刻，首次看到某截止时刻时的剩余时间算满圈，截止时刻延后（刷新）重新满圈，状态消失后再出现重新计；中途首次看到（如重连）从满圈开始；读不到权威时刻时不画圈 |
| 自身资源条每秒回复 | Combat 仅本人投影 `HPRegenPerSec/MPRegenPerSec`，由 `LDTactical::RegenRates` 从当前属性、被动及参赛者脱战回复的同一条件推导；只用于显示，不参与资源结算。`PublishOwnerViews` 在满资源下的回复率变化也推进修订；HUD 经 `LDHUD::RegenText` 拒绝别局/别命、死亡、无身体及无效资源投影，条内右侧显示 `+X.X/s`，中央保留资源/护盾读数。满资源显示回复能力，不估计资源差值或展示他人私有属性 |
| 自身等级、经验、米、装备与待选 | Growth 的 `ReadOwnProgression/ReadOwnEquipment`（`FLDOwnerState.Growth`）；满级显示 MAX，缺失显示“同步中” |
| 自身生命、保护与复活倒计时 | Lives 投影的 `LifePhase`、`ProtectedUntilMs`、`RespawnAtMs`，以及 `ReadDemoAuthorityTimeMs` 给出的同局权威时刻；读不到时显示“同步中”，不用本地时间或 0 代替；倒计时在该时刻到达截止前不显示 0，显示本身也不解除保护 |
| Q/W 与 D | `CombatView.Entries` 经 `LDHUD::ActionInput/Action` 把意图可提交性（本人当前 Match/Life、对局进行中、存活、`IsWorldInputSuppressed()` 为假即未被菜单/设置、子确认或文字编辑占用且无残留按键）与执行限制（空槽、冷却、法力、控制、服务器原因）分开；图标与热键同走 `ActivateCombatEntry`，限制只置灰说明，不吞掉显式激活；本人战斗投影缺失时显示“未同步”且不发送。准备态读 `ReadArmedCombatEntry/IsAttackMoveArmed`，回执日志 `CombatReceipts`（`FLDCombatReceiptLog`，按到达序号保留最近 32 条）由 HUD 用自己的游标逐条读取，同一帧到达的多条回执各自提示一次：`LDHUD::CombatReceiptsNotice` 按到达顺序把不同的“未执行”与“复活保护没了”合成一行提示条，`CombatReceiptSlotNotice` 非空的槽位外圈变红约 2 s。格面只画热键、占位图标（`FActionView::Glyph`）、等级（`Rank`）与法力数字（`Cost`），状态只由 `LDHUD::ActionLook` 的外圈与图标颜色表示，文字在悬停说明层里：`LDHUD::ActionDetail` 给出名称、“类别 [键] · 状态”与描述/目标/当前等级效果，HUD 按格子的布局框判断指针所在（禁用的格子也能看说明）并把说明层居中放在格子上方；准备态不另出说明行 |
| 射程与目标预览 | `ULDHUD::NativePaint`：本人同局同命存活且未武装时，在脚下画一圈淡白的普攻射程（`CombatView.AttackRangeCm`，射程成长立即反映）；武装单位技能或攻击移动时，射程圈改为强调色，并对指针下的单位（`ObservedUnderPointer` + `ReadUnit`，与点击同一选取、只取当前授权帧）按脚点 2D 距离给射程圈着色：不超过射程为绿、否则为红，单位身上不画框；方向技能另画指示线：方向与确认时提交的一致（`GroundUnderPointer` 地面命中点减胶囊脚点），从本人显示位置画出，长度为射程（落点技能取到落点为止）；指针下没有地面时不画。射程圈画在显示位置，它的距离颜色以胶囊脚点判定。只是预览，是否命中由服务器裁决 |
| 开局倒计时 | `LDHUD::OpeningCountdown` 在屏幕中央显示 `PublicMatch.StartedAtMs`（T=0）前的整秒倒数 5..1（向上取整，本地时钟先到 T=0 时保持 1 直到权威进入 Development），进入 Development 后 1 秒内显示 `LDTerms::CountdownGo`“开淫！”；倒计时内阶段行为 `LDTerms::OpeningCountdown`，HUD、面板、排行榜与镜头照常可用，被拒的攻击、施法与交易提示 `LDTerms::NotStartedYet`“还没开始呢，急什么”；房主菜单在倒计时内不显示结束本局 |
| 对局计时 | `LDHUD::MatchTimer` 在小地图正上方居中显示自 `PublicMatch.StartedAtMs`（T=0）起已过的整秒（向下取整），格式 `m:ss`，分钟不补零（如 `0:12`、`12:05`）；开局倒计时内为 `0:00`；读不到权威时刻或 T=0 未发布时不显示；只随小地图在对局进行中显示 |
| 下一步提示 | `LDHUD::NextStep` 在旗杆状态下方给一行强调色文字，只在开局倒计时/发育期/开放期、同代次投影上计算（倒计时内只提示待选）；优先级：有待选 → “开盲盒 ×N（点右下角的开盲盒按钮）”；开放期 → 本人独自喷漆时“稳住：你正在旗杆上喷漆”，旗杆上是本人涂鸦时“守住：旗杆上是你的涂鸦，别寄（寄了涂鸦就没了）”（`LDTerms::GuardOwn`），旗杆上是别人涂鸦时“下一步：去围攻 X，或者去旗杆改漆”（`LDTerms::Besiege`），否则“下一步：旗杆开放了，冲！去中心喷漆”；有空装备槽且米够买时 → “米够买「X」了，去砍一刀小摊”（X 为买得起的最贵装备）；1 级 → “去打附近的野怪搞米、涨流量（右键攻击）”；都不适用时不显示。右下角商店按钮左侧另有“开盲盒+N”按钮，次数增加后轻微脉动 |
| 被选中的其他单位 | 当前帧中 Handle 与 Epoch 都匹配的单位，经 `LDHUD::ObservedCard/ObservedUnit` 只显示白名单 |
| 可见单位头顶的名字、等级、血条与状态 | `ULDWorldLabels`（`LDUIRoot` 世界层，最底层、不命中测试、不取焦点）：只在本人存活、帧为同局同命的当前帧且 HUD 显示时，为 `ReadPresentedLocation` 返回的单位绘制；参赛者名来自公开名册（查不到就不显示名字），野怪名来自内容显示文本，状态行与检视卡片同源（复活保护剩余时间向上取整到 0.1 s），各状态按身体标记的颜色书写（定身品红、护盾亮青、复活保护白金，其余用普通状态色），文字本身仍写出状态名。白名单子集为名字、等级（参赛者，画在座位色徽章里）、HP 比例条（参赛者红、营地橙，最大值未知不画）、是否有护盾（条上方浅蓝线）与公开状态；不显示法力、装备、技能、冷却或金币。字号：参赛者名 16 粗体、野怪名 16、状态 14，随文字缩放 |
| 本人头顶血蓝 | 同一层：本人同局同命存活且有身体、`CombatView` 同代同命且生命上限已知时，在本人身体上方画生命条（护盾接在其后，合计超出时按比例收进整条）与法力条，条的底边就落在锚点上；不画技能就绪点，技能状态只在底部动作栏。不画本人名字 |
| 画面回馈（浮动数字、奖励、击败、升级） | `ULDWorldFeedback`（与头顶标签同层同显隐）由纯值 `LDWorldFeedback::FTracker` 比较相邻样本得出事件，第一次样本、换局、换命或视图失效只重设基线，不补发历史。本人：`CombatView` 同代同命相邻修订的生命差（回血按 25 点/秒 × 间隔 + 1 的容差不飘字，生命上限变化不算伤害或治疗）与护盾差（增加、被打掉；自然到期不飘）；可见单位：相邻当前帧中同一 Handle/Epoch、同一最大值的生命差（营地无回血，容差 1 点），按帧的采样时刻等呈现时钟到达再显示，单位不再呈现即丢弃；施法名：可见施法者的新 `Cast` 提示带 `ActionAbility` 时，在其身旁飘一次技能显示名（`LDHUD::AbilityName`/内容表），不含等级、冷却，首见样本只建基线、无来源不显示（D16，CB-P10/SC-07 白名单例外）；奖励：同一次成长发布里 `CumulativeXP` 增加才飘“+流量”，金币同时增加才飘“+米”，买卖只改金币不飘；击败：公开击杀通知中击杀者是本人且新鲜（6 s 规则）才出“淫了 某某！”；升级：等级上升出“涨粉了！LvN”横幅与本人脚下光环、光柱，新获得三选一时加副行“获得一次开盲盒机会”。不读其他身体 Actor 或他人投影 |
| 小地图 | 注册地面边界、静态阻挡（区分阻走/阻视线）、砍一刀小摊、旗杆喷漆区、两档营地标记；当前覆盖与当前可见单位（参赛者用座位色圆点，位置取代理的呈现位置，不可呈现时退回帧内脚点）；不画探索层或出生点。朝向按本地设置 `MinimapOrientation`（跟随镜头/跟随角色朝向/固定正北），三种都是真实俯视的旋转加统一缩放，不镜像；`LDMinimap::MakeView/ToFrame/ToWorld` 是绘制、镜头框与拖图/右键移动共用的唯一换算，地图本身就是随视角旋转的整块地面（跟随镜头时是一个菱形），整块画进框内、不裁切，地面外的框角透明，按下时交给世界（`LDMinimap::OnGround`）；不画方位文字 |
| 属性 P | `ULDAttributesPanel` 只格式化 `CombatView.Attributes` 的各来源与最终值、已学被动、装备被动和临时状态；行固定，刷新不重置滚动 |
| 三选一 | `ULDChoicesPanel` 读当前 Offer 的三张固定卡，命令带 OfferId/OptionId/各修订；Q/W 已满时进入替换子确认，不默认槽位；同一机会的修订刷新保留原选项。面板停靠在右下角、HUD 商店按钮正上方的窄列（`SetBottomDock` 跟随按钮高度；宽 340u，半透明底板，三张最低 64u 的等高紧凑卡纵排，替换子确认在卡列下方展开，不滚动）；卡片显示按类别区分形状/颜色的占位图标、名称、“类别 · 档位”一行（升级卡为“初级 → 高级”）和规则主句（`URichTextBlock`，关键词加粗；属性卡为属性行），悬停或键盘聚焦时在卡列左侧与该卡平齐处弹出宽 300u 的详情层（主句与“高级：”行、主动的目标与法力/冷却行、卡面每个加粗关键词的解释，固定换行宽度；放在覆盖整个面板的独立 Canvas 层上，尺寸不影响卡列布局；HitTestInvisible，不挡世界点击）；点卡即提交该卡（`ChooseCard`），没有单独的确认按钮，满槽新主动卡点击后进入替换子确认，子确认的槽按钮只写 Q/W，点 HUD 上亮红的 Q/W 槽即提交替换（`ChooseReplacement`）；替换子确认不捕获输入（`ULDPanel::ChildConfirmationYields`，`ULDUIRoot::YieldingConfirmation`）：等待期间照常移动、攻击，Q/W 键照常施法；HUD 提示条一直显示“选择要替换的技能”（期间到达的其他提示照常显示约 5 s 后让回），替换完成或撤销后消失；Back 先取消已武装的技能，再撤销替换选择；点别的卡从那张卡重来，关闭面板也撤销；对局不暂停，不遮挡画面中央的本人及周围。开局 Offer（第 1 次机会、尚未消费）为真人自动打开一次：等 Growth 发布该 Offer 后用 `ShowMainPanel` 打开，Root 尚在离开 Lobby 时最多重试 5 s，超时记录 `LD_HUD_OPENING_CHOICE_NOT_SHOWN`；玩家已开自己的面板或输入被捕获时放弃这一次，不覆盖玩家的 UI，也不替玩家选择。面板仍盖住右侧约 1/5 的世界，由 `LDHUD::FChoicesFold` 决定自动收起回右下角的“开盲盒+N”按钮（只隐藏，机会、Offer、选中卡与在途请求不变）：自动打开的面板 6 s（`AutoOpenSeconds`）内玩家没有悬停/聚焦或点选卡片、进入替换子确认或提交请求则收起，操作过即不再计时；本人受击（HUD 用与世界飘字同一 `LDWorldFeedback::FTracker` 的本人生命下降/护盾被吸收事件，只读本人投影）时，未在操作的打开面板收起，受击收起后玩家下一次手动打开不再因受击收起直到关闭 |
| 砍一刀小摊 | HUD 右下角商店按钮或左键小摊模型（`ULDNavigationComponent::ShopUnderPointer`：指针射线与美术目录里小摊装饰的网格包围盒相交，不是周围地面）打开；`ULDShopPanel` 读本命 Opened 会话报价（`ReadOwnShop`，没有时列已批准目录，只能浏览与预购）与本人两槽装备，商品是图标网格，悬停弹出说明（与开盲盒卡同一画法），右键图标经 `QuickBuy` 在小摊范围内且有会话时直接买，否则加入预购（`NativeOnPreviewMouseButtonDown` 先于按钮收到右键并消费，世界收不到）；右键小摊只是普通移动；AI 与测试用的 `ApproachShop` 到达只开会话不开页，`OnShopOpenResult` 的 Opened 只在与 `ReadShopInteraction` 同代次、同生命、同砍一刀小摊时记下报价；页面打开且本人在小摊范围内而本命没有会话时，HUD 也请求会话；远端客户端在下一次成长发布前先用 Opened 自带的报价。买卖命令带所显示的会话、报价与修订，没有确认；已拥有且报价 `bCanUpgrade` 的同款再买发 Upgrade（价格显示为进阶价）。出售由 HUD 装备格 1/2 的右键菜单（`ULDHUD::NativeOnPreviewMouseButtonDown` 先于格子按钮收到右键）调 `TogglePresell` 标记实例，格子右上角黄色“S”；最多 2 件预购，本人站在小摊范围内时先逐件卖出预售，再按 `LDHUD::NextPreorder` 自动买入或进阶；换局关闭并清空预购，死亡、换命或会话结束只回到目录并禁止交易，受伤不关闭 |
| 旗杆喷漆与守护、击杀通知与结果 | `ALDGameState::PublicMatch`（唯一阶段来源，校验代次）经 `LDHUD::Circle(Public, SprayerName, OwnerName, bClock, NowMs, SprayMs, GuardMs)` 显示喷漆行与涂鸦/守护行：独自喷漆“X 正在旗杆上喷漆 · 剩 <秒>s”；争夺“旗杆底下打成一锅粥 · X 的喷漆进度先存着 <已存>/<所需> s”（十分位，无持有者时“谁也没存下进度”）；守护运行（主人独自在区内）“旗杆上是 X 的涂鸦 · 再守 <秒>s”，主人以外有人在区内而冻结时“旗杆上是 X 的涂鸦 · 有人进圈，守护冻结（再守 <秒>s）”，主人不在区内、区内无人而暂停时“旗杆上是 X 的涂鸦 · 守擂者不在圈内，守护暂停（再守 <秒>s）”（2026-11-07 用户决定）；无主且无人时“旗杆空着，没人敢上？”；两部分用“ · ”连接。进度条独自喷漆时为喷漆进度、争夺时为暂存喷漆进度，其余为守护进度。`KillFeed`（按序号去重，只含参赛者死亡）、`MatchResult`（与 `PublicMatch.ResultId` 一致才显示，缺失时“结算同步中”而非零表；守护胜利标题“守护成功 · X 的涂鸦守住了，恭喜成为今晚的淫人王！”，房主结束“房主结束了本局 · 没人守住涂鸦”）；倒计时到零只等待权威状态，不宣布开放、喷上涂鸦或胜利 |
| 命中、失败与进度 | 经裁剪的自身反馈，而非完整服务器日志 |
| 客户端试玩日志（`FLDPlaytestTrace`，只在远端客户端） | 与本表相同的来源：本人 `OwnerState`/`CombatView`/成长投影、房间公开的 `DemoLobby`/`KillFeed`/`MatchResult`、本人当前授权帧中的可见单位（所见施法与其位置），以及本人发出的订单与输入；伤害/护盾/状态事件取自同一 `LDWorldFeedback::FTracker`。不读其他身体 Actor、他人投影或权威记录；完整权威记录只在 Host 日志。行契约见[架构与网络](ARCHITECTURE_AND_NETWORKING.md#运行日志与试玩记录) |

成长请求一个家族同时只有一个在途：面板在提交前先用 `PeekGrowthRequestId` 记下将分配的序号，
因为 Listen Host 会在提交调用内回答；只在 Controller 也结束该请求时（Final、未保留序号的拒绝、
Final/过期回执）解除等待。

名称来自复制的 `ALDGameState::DemoLobby` 名册，查不到时显示“未列名参赛者”。
技能、装备、野怪和状态的中文名经 `LDHUD::ContentText` 从 `DemoContent` 字符串表读取；
表或键缺失时显示稳定 Id 并记录 `LD_HUD_DISPLAY_TEXT_MISSING`，不猜名称。

**技能卡面**（#147）：技能显示行的 `description_text`（主句）与可选的 `advanced_text`（高级行，UI 加“高级：”前缀）是带 `[op.field]`
占位的模板（如 `[damage.amount]`、`[root.duration]`、`[cast_frenzy.MoveSpeedBonusRatio]`）。`LDDemoCardText::Parse` 把模板拆成文字与占位，
占位必须指向该技能某档里的一个效果和一个 typed 字段；导入器与 `DemoContent.py` 的 `card_template` 检查都拒绝指不到效果的模板。
`LDHUD::CardText` 用本人当前等级把 rank 按 `LDSkillGrowth::GrowRank` 成长，再以本人投影的攻击力/法强/生命上限/护甲
（`LDHUD::CardStatsOf`，无投影时为 0）算出数字；升级卡传入当前档，变化的值写成“A → B”；读不到的占位显示“?”并标记不完整，
解析或成长失败整句显示“（卡面数据缺失）”。`LDTerms::CardKeywords` 的 13 个关键词在富文本里加粗（同一位置取最长匹配），
`LDHUD::KeywordLines` 按术语表顺序给出“关键词：解释”行。三选一卡片、Q/W 动作格的悬停详情（`LDHUD::ActionDetail`：名称后标“（初级/高级）”，
高级行在已学高级时写“高级（已生效）：”）与属性面板的被动行使用同一渲染；属性卡只显示属性行。
`LDHUDModel`、`LDHUD`、`LDMinimap`、`LDUIRoot` 及各 Panel 是主要 UMG/Slate 入口。
属性、三选一、砍一刀小摊三个普通主面板由 `LDHUD` 创建并停靠在自身画布上（命中测试只算面板本体），
并向 `ULDUIRoot::RegisterMainPanel` 登记；同一时刻只开一个，权威事件用 `ShowMainPanel` 打开，
不会顶掉玩家随后打开的菜单或设置。

受控角色、检查对象、准备中的动作、指针捕获、镜头模式和打开面板分别管理。
选中另一角色并不改变技能/装备按钮的所有者；禁用按钮只是反馈，不能代替服务器检查。
Widgets 不得遍历所有 Pawn/PlayerState 来补全 Tooltip 或小地图。

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
（`ULDAudioCatalog`），按 `ELDAudioCue` 解析全部音效。目录缺失、条目缺失或乱序、声音未归入
`SC_DemoMaster`、循环或没有时长都记录 `LD_AUDIO_CATALOG_INVALID` 错误，不静默降级；
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
| 本人普攻出手（`SelfAttack`，不是命中确认） | 同 Match/Life 的 `CombatView` 新 Revision 中 `Cues.ReleasedAtMs` 晚于上次所见且 `bReleasedBasic`（近战出手或飞行物发射的时刻，Nav 与 Direct 来源相同）；技能与装备的释放交给下一行的回执 | 每次出手一次；新 Life 的首个投影只建立基线，死亡、旧生命与重放不响 |
| 本人技能或 D 冲刺释放成功 | 本人回执日志 `CombatReceipts` 中音效游标之后的每条回执：Q/W/D，Finished/Released 且无拒绝 | 每个请求一次（同一帧到达的多条回执逐条判断，前一条的释放不会被后一条覆盖；同一帧多次释放合成一次）；别局或别条命的回执不响 |
| 购买或出售成功 / 交易被拒 | 本人砍一刀小摊提交结果与回执的最终 Applied/Rejected，及未保留序号的受理拒绝 | 每个请求的最终结果一次；重复或迟到回执不响 |
| 升级或出现新的三选一 | 本人 `Progression` 的 Level 或 EarnedThrough 增加 | 选卡后显示排队中的下一组不算新机会 |
| 本人死亡 / 复活 | `OwnerState` 同一 Life 由 Alive 变 Dead / 新 Life 为 Alive | 开局首次出生不响 |
| 界面点击 | 设置页主音量滑条松手或键盘调节时试听 | 每次调节一次 |
| 预购/预售标记 / 取消 | 本地 `ULDShopPanel`：加入预购或标记预售响 `ShopMarked`（复用复活音 `confirmation_002.ogg`，低 6 dB），取消预购或取消预售响 `ShopUnmarked`（复用界面点击音 `select_002.ogg`）；之后真实成交或被拒仍按上一行的回执音 | 每次操作一次（2026-11-06 用户决定） |

主音量是本地设置 `MasterVolume`（无量纲 0..1，默认 1 即不衰减），
经 `SMix_DemoUser` 对 `SC_DemoMaster` 的类覆盖生效，每个新世界重新应用，不修改共享资产或 CDO。
覆盖实际交给音频设备时（首次推送和每次音量变化）记录 Display 级 `LD_AUDIO_MIX_APPLIED device=<id> volume=<v>`。
新增的任何 Demo 声音都必须归入 `SC_DemoMaster`，否则目录校验失败、音量也控制不到它。

素材全部来自 Kenney（kenney.nl）的 CC0 1.0 音效包，只提交实际选用的 `.ogg`（Git LFS）。
逐文件的包名、下载地址、下载日期、许可、用途与 SHA-256 记录在 `SourceData\Audio\audio_cues.json`；
一条音效可以复用另一条的源文件（各自导入独立的 `SW_<Cue>`），例如护盾吸收复用受击的 `impactGeneric_light_000.ogg`；
本人普攻出手使用独立的 `SW_SelfAttack`，只表示释放，不作为命中确认。
释放事实只保留最后一次动作；同一采样间隔内被后续技能释放覆盖的普攻不能补放，不从其他单位血量推断。
kenney.nl 的下载地址是会过期的签名链接，包以 `zip_sha256` 识别。
各包的许可原文 `License.txt` 与文件放在同一目录。
每条音效的播放音量（SoundWave `Volume`）由创作脚本 `Scripts\MeasureDemoAudioLoudness.py --write` 测得：
BS.1770 K 加权、400 ms 窗口的最大瞬时响度统一到全部音效的中位数，提升以样本峰值 0.95 为上限，
结果连同 `loudness_lufs` 写入清单的 `volume`，导入时应用，源文件字节不变。
复用文件的条目可带 `level_db`，在归一与峰值上限之后再叠加（护盾吸收为 -6 dB，音量约为受击的一半）；不复用文件的条目也可带它，本人普攻 `impactPunch_medium_000.ogg` 为 -6 dB，连续普攻不刺耳；被复用的文件只计一次中位数。
该脚本需要 `numpy`、`scipy`、`soundfile`，导入与校验都不运行它；
很短的本人反馈音（受击、施法、点击、交易）受峰值上限约束，按同一测量仍比中位数低约 5–9 LU。
`Scripts\ImportDemoAudio.ps1 -Mode Import` 由该清单重建 `Content\Lockdown\Audio` 下的资产；
`-Mode Validate`（Bootstrap Validate 也会执行）拒绝清单外的文件、许可原文不是 CC0 的包、
源文件哈希变化，以及不是由当前源字节导入、或播放音量与清单不符的资产。资产经 `DirectoriesToAlwaysCook` 进入包。

## 检查入口

- `Lockdown.Rules.DemoVision.{Codec,Privacy,Ordering,Handles,Termination,Sight}` 覆盖线缆、白名单、游标、句柄与 pin、终结和视线几何。
- `Lockdown.Rules.DemoVision.ActionCues` 覆盖动作提示与提示龄的线缆规则、六个名册座位与身体色（座位互不相同、营地 NPC 不用座位色）；
  `Lockdown.Rules.DemoVision.Projectiles` 覆盖飞行物线缆上限与句柄冲突、同一视线规则的近者优先选择、逐观察者句柄寿命，以及接收端隐藏与超时清除。
- `HUDBasicAttackTerm`、`HUDIdentityPalette`（`LDHUDRulesTests`）覆盖“普攻”文案与头像/身体共用的六色；
  `HUDTermsGlossary` 核对术语表里重复的状态名与批准目录一致、系统术语不为空不重复且不与条目名相同。
  HUD 与头顶标签测试的条目名从 `LDDemoContentFixtures` 的 approved_base 读取（`LDTestApprovedDisplay`），世界观术语引用 `LDTerms`，不逐字重复。
- `Lockdown.Rules.WorldLabels.{ContenderName,CampMemberName,StatusLine,Whitelist,HealthBar,OwnBars}`（`LDWorldLabelsTests`）
  覆盖头顶名字、状态行、只显示白名单（法力、装备、姿态与观察身份不进入标签）、血条比例与未知、本人血蓝条的门控与护盾收比。
- `Lockdown.Rules.DemoVision.PropFade` 覆盖可淡化道具的遮挡判断（镜头与身体之间、线旁、身体在前、身体站在旗杆里或紧贴旗杆、高于道具）与淡入淡出；`DemoVision.ActionCues` 另覆盖施法来源只在 Cast 时、最多一个；`WorldFeedback.Casts` 覆盖施法名只在新施法时出现一次、首见只建基线、无来源不显示；`HUDNextStep` 覆盖下一步提示的优先级与别局投影不提示；`LocalSettingsQuickCast` 覆盖快速施法默认关闭、保存回读、schema 9 迁移与非法值。
- `Lockdown.Rules.DemoVision.Presentation` 覆盖呈现时钟：样本间线性插值、最新样本之后保持不外推、跳变样本不扫过、朝向走短弧，以及时钟偏移的下调、限速上调与重设。
- `Lockdown.Rules.WorldFeedback.{Priming,OwnDamageAndHeal,Shield,VisibleUnits,RewardsAndLevel,Kill,Text}`（`LDWorldFeedbackTests`）
  覆盖画面回馈事件：首样本与换代只重设基线、回血容差与最大值变化不飘字、护盾增减与自然到期、可见单位相邻帧差与重现不补发、
  奖励只在经验增加时出现且买卖不飘、本人新鲜击败与他人击杀/过期通知不出、以及显示文本。
- `HUDObservationBoundary`（`LDHUDRulesTests`）覆盖检视器白名单和地图换算。
- `HUDLifeStatus`（`LDHUDRulesTests`）覆盖自身生命行：权威时刻缺失时显示不可用，倒计时在截止前不显示 0，死亡不显示金币损失。
- `HUDRegenDisplay`（`LDHUDRulesTests`）覆盖每秒回复格式、零回复、满资源、失效/死亡投影；`DemoCombat.RegenRates` 对照既有积分器验证条件回复率。Combat 拥有连接场景还检查满资源时脱战边界后的回复率在 Host 与真实客户端发布。
- `HUDMatchTimer`（`LDHUDRulesTests`）覆盖小地图上方的对局计时：T=0 起算、向下取整、分钟不补零、倒计时内 0:00、无权威时刻不显示。
- `HUDCardText`（`LDHUDRulesTests`）覆盖卡面模板：每个数字读取拥有者在当前等级使用的那一档、成长与属性加成、升级卡的“A → B”与关键词加粗；
  `HUDCardSelfCheck`（`LDCardSelfCheckTests`）对批准目录的每张技能卡检查 1 级主句不超过 26 个可读字、高级行不超过 18 个、每行最多 2 个关键词、
  不出现“喷漆”“守护”或“未知效果”。
- `HUDOwnResources`、`HUDActionView`、`HUDPublicMatch`、`HUDContentParameters`、`HUDCombatOwnerView`（`LDHUDRulesTests`）
  覆盖本人资源与等级、Q/W/D 意图与限制分离、旗杆喷漆/涂鸦/守护行与进度条、击杀/结果（含守护成功标题）、内容参数行与原因文案、本人状态/被动方格的提示文字（含脱战回复与躺平角回血方格及其说明）与战斗回执提示。
- `HUDSelfIcons`（`LDHUDRulesTests`）覆盖状态方格的占位首字、12 格上限与“…”计数（临时状态不被挤掉），说明层的首行与类别（含正负面），每个状态编号的正负面分类，以及剩余时间圈的满圈、刷新重满、提前结束与消失后重计。
- `HUDCooldownText`、`HUDActionStates`、`HUDDeathCause`、`HUDKillFeedSelf`（`LDHUDRulesTests`）覆盖冷却秒数文字（不提前显示 0）、
  动作格的冷却/法力不足/受控/空槽/准备态分类、法力消耗/等级数字与图标首字、本命之后的死因取值，以及击杀通知里本人写作“你”。
- `HUDShopPreorder`（`LDHUDRulesTests`）覆盖预购目录行与自动买入的选择：队列顺序中第一件报价米够且有空槽的，
  报价未列或报价更贵时不按目录价买，没有空槽或队列为空时不买；已拥有可进阶的同款按进阶价进阶、不需空槽，
  进阶价不够或没有进阶价时不改买第二件。
- `HUDScoreboard`（`LDHUDRulesTests`）覆盖按住 Tab 的排行榜：击杀多在前、相同时死亡少在前、再相同保持名单顺序，本人/AI/已离开的名字标注，
  别的代次或没有对局时为空。`DemoCapture` 规则测试另拒绝无效 ID、重复参赛者或超过 6 行的 `KillFeed.Scores`。
- `HUDEventFeed`（`LDHUDRulesTests`）覆盖左下（小地图上方）事件播报：中途进入不补播、每个公开变化只播一次且最新在下、本人行高亮、
  6 条上限与 5 s 离开、外卖被抢行在本人领取序号到达后改写、老板击败者署名、同一主人被冻结的守护归零只播一次
  （暂停、冻结与恢复本身不播），以及新一局从空列表开始。
- `AudioCuePublic`、`AudioCueOwn`、`AudioCueBasicAttack`、`AudioCatalog`、`AudioCatalogAsset`（`LDAudioRulesTests`）覆盖公开与本人事件到音效的映射、
  首样本基线与去重、别局/别命/迟到回执不响、目录完整性，以及实际 `DA_DemoAudio` 资产可解析；
  它们不证明实际听感或打包后的设备输出。
- `ViewPrivacySchema`（`LDViewTests`）覆盖公共 GameState（只有 `DemoLobby`、`DemoClock`）以及 Lobby、DemoClock 的结构。
- `RunDemoVisionCheck.ps1`（`Tests\LDDemoVisionProbe`）：参赛且渲染的 Listen Host 与两个拥有客户端经正常大厅开局，
  三具身体走到公共旗杆锚点后，检查拓扑、客户端受限注册表、Host 只经自身投影呈现他人（含生成当帧隐藏）、
  单连接丢帧（引擎网络模拟，非 Shipping）后的超时清除与无 LastSeen，以及客户端断开后 Host 不再呈现离开者。
  正常开局门禁打开前它无法运行；未实际运行不算通过。
- 必须观察真正受限的客户端及其可访问记录，包括 Listen Host 上非本地原生身体是否隐藏；不能只看服务器全知视图或一张截图。

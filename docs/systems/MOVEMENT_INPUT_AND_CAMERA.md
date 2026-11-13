# 移动、输入与镜头

产品按键和体验目标见 [App Spec](../APP_SPEC.md)。本文保留当前运动和输入基础的工程约束，
不把某个 profile 的旧按键、槽位数或交互方式当作 Demo 规则。

## 入口

| 入口 | 作用 |
| --- | --- |
| `Character\LDCharacter` | 原生身体、SpringArm/Camera、朝向与表现 |
| `Character\LDCharacterMovementComponent` | CharacterMovement 扩展及 saved move 状态 |
| `Navigation\LDNavigationComponent` | 路径、移动命令、确认、取消、心跳和镜头 |
| `Navigation\LDDemoNavigationSystem`、`Region\LDMapRegistry` | 唯一 Demo 的已批准 capsule/NavAgent 配置、当前世界修订、局部地面与完整路径查询 |
| `Framework\LDPlayerInput.cpp` | 输入动作、指向、菜单路由 |
| `UI\LDControlSettings`、`LDControlsPanel` | 映射注册、冲突检测、保存与本地偏好 |

头文件在 `Source\Lockdown\Public`，实现在 `Private`。

## 一个身体只有一套运动

普通移动由 CharacterMovement 执行并参与原生预测/校正。
导航组件在运动前提供方向，不另写一个位置积分器，不沿路径直接 SetActorLocation，
也不为方便寻路把玩家临时交给另一个 AI Controller。

运动上限、碰撞、斜坡和批准的位移能力仍需通过该身体的合法运动路径。
镜头平移或预览不能改变人物位置、攻击距离或服务器判断。

Demo 身体的 `ConfigureDemoMovement` 从已验证内容快照读取碰撞半径与基础移速，
沿用现有身体 half-height 并更新同一 CMC/NavAgent；不把旧 PrototypeTuning 当作新 Demo 默认值。
导航系统在原生数据注册前安装同一 agent，生成器用带明确配置的创建入口，
不能先注册默认 agent 再改半径，否则注册索引与保存的数据会不一致。
Demo 权威身体关闭 CMC 自身 component tick，由现有 Combat driver 发出的当前
`FLDAuthorityStep`/Motion permit 调用 `AdvanceAuthorityMovement`，沿原生
`ControlledCharacterMove/PerformMovement` 执行一次；同一 step/body 完成后不能再积分。
普通客户端仍使用原生预测、saved moves、packed moves 和校正。
服务器的 packed 处理保留原生时间戳验证及 ACK/error 路径，但其中的 `MoveAutonomous`
不另做第二次权威积分；`ServerShouldUseAuthoritativePosition` 禁止以客户端位置覆盖服务器结果。
这不是第二个位置积分器或 AI Controller 接管。

营地成员在已完成原生落地、精确零速度/输入、walking 且静态支持面稳定时可跳过
`PerformMovement` 的物理积分与 floor sweep。待处理外力、launch、root motion、跳跃、
地面穿透、强制 floor 检查、身体生命/胶囊/支持面变化或世界修订变化均走完整 CMC；
跳过前还用当前胶囊的 blocking overlap 排除外部身体（如刚结束冲刺）新造成的穿透，
不能只相信本 CMC 的缓存 floor 标志；
刚停下也须先完成一次原生更新。跳过仍进入相同 native capture、integration 计数和
`CompleteNativeMotion`，同 step/body 恰好提交一次静止批次，不另建休眠运动流。
参赛者不使用该分支：其 packed move 的 ACK/error 和 `ServerCheckClientError` 所依赖的
原生预测簿记保持完整 CMC 更新。`AuthorityStationarySteps` 单独统计省略的营地积分次数。

拥有者的校正仍立即修改 CMC 胶囊并重放合法 saved moves。`FLDOwnerCorrectionSmoothing`
只保留重放前后显示位置的差，按原生 `NetworkSimulatedSmoothLocationTime` 衰减；
偏移受原生平滑距离限制，超过 snap 距离直接跳到位。本人模型、脚下标记、头顶标签、
射程圈与跟随镜头使用同一显示位置；碰撞、距离验证、订单与权威记录仍使用真实胶囊。
saved move 的身份取正在预测的已接受订单；新单尚未激活时不提前换成新单，
停止及没有活动订单时仍保留当前 Local 的 Match/Life/Order/Epoch，不能退回全零身份；
Stop 之后、停止回执到达之前发出的新单不再沿用 Stop 前的订单（取消 epoch 不同），与 `ApplyPrediction` 的判断一致。

`LDCharacterMotion.cpp` 在同一 CMC 的原生 walking/falling、sweep、StepUp、接地和模式入口
记录 `FLDNativeMotionBatch`。移动片段保留实际首末脚点和原生 hit-time 分数；
floor 高度调整不被分摊成自愿移动时间。StepUp 的失败事务撤销整个试探前缀；
walking 的 ledge 分支通过原生 `GetLedgeMove/CheckFall` 结果见证非 virtual `RevertMove`，
核对实际恢复的姿态/支持面后撤销记录，不仅按帧末位置猜测。
`CurrentFloor` 和实际模式更新产生独立支持样本，失去支持、原地重新落地不能漏报。
出现未记录的位置写、缺时间区间、未解回滚或容量耗尽时明确失败，不补造一条首末直线。

核心按当前 Body/stream/Step/世界修订接纳不可变批次，检查记录序列、时间及空间连续性；
每个身体最多保留 256 个未消费批次，每批最多 128 条记录，超限在推进前背压或显式故障，
不静默覆盖前一帧。精确前缀确认只释放已读记录，错误身体/未来前缀不能删除历史；
身体退休后已接纳的证据仍保留在核心记录中，直到消费。
参赛者批次在 `CompleteNativeMotion` 接纳前同步交给喷漆与守护归约器（`PublishCaptureMotion` 提取喷漆区进出）；
driver 在下一步开始时释放已被消费的前缀，营地成员批次没有其他消费者，同样在下一步释放。
`RunDemoCoreCheck` 的 DEV 审计在两步之间读取并确认仍保留的前缀。
native record sequence 与最终圈事件/提供者序列分开。`CompleteNativeMotion` 和这类内存回收
都不是最终 Motion Fence、Sealing 或喷漆/守护完成证明。骨骼动画没有根运动（都原地播放）。
没有按住加速或地表减速的独立运动分派；D 冲刺与技能冲刺只在 Combat 成功释放后、击退只在
`CommitControl` 提交成功后经 `StartDemoDash` 写入原生 CMC 的服务器位移，沿地面积分、遇阻挡停在最远合法位置，不瞬移；
击退同时递增 `KnockbackSerial`，导航据此以 `ELDOrderFailure::KnockedBack` 放弃当前订单。冲刺
由同一原生运动记录链积分；Root 期间由 Combat 准入/释放拒绝，不付费。

## 命令与取消

`FLDMoveIdentity` 包含 `Match`、`Life`、`Order`、`Epoch`；
拥有连接隐含在组件的 Controller 中，不接受客户端指定另一个所有者。

1. `NavigateGround`、`AttackUnit`、`AttackMove` 各建立新 order，发送一份
   `FLDSpatialOrderRequest`（Kind 为 MoveGround/AttackUnit/AttackMove，带脚点与注册
   SurfaceHint 或观察句柄+Epoch）。右键按“可见单位 → 地面”的顺序只取一类。
2. `ServerSubmitOrder` 与对手 AI 的服务器入口 `SubmitOrder(FLDOrderCaller, Request)` 共用同一订单簿：
   先用核心当前 BodyRef/拥有 Controller 核对身份、顺序与形状，再进入单一待处理槽；
   进攻订单在获准 Actions 阶段先经生命域解除保护，再验路径/目标，失败不替换已在执行的订单。
   在 Actions 前被更新请求取代的进攻意图仍补做一次保护解除。
   带 `bQueue`（Shift）的订单和经 `ServerQueueCombatAction` 的 Q/W/D 不进待处理槽，而是排进最多 8 步的
   Shift 队列，轮到时才作为普通待处理请求或 Combat Direct 请求执行（[操作 §4.4](../demo/CONTROLS_AND_TARGETING.md#44-shift-指令队列)）。
3. `ClientOrderReceipt` 只接受当前身份的结果，避免迟到确认重启已取消路线。
4. `Stop` 提升 order 和取消 epoch，清本地预测跟随；服务器在获准 Actions 阶段停止，
   不让菜单回调直接修改权威速度。较旧 order/epoch 不能重启。
5. `ResetLife` 不倒退序列号；新的 Owner 生命投影先发布再清理导航，不发送一条伪装成用户意图的
   新生命 Stop，也不等下一次 Tick 清掉已经收到的新生命首单。死亡由 `EndLifeOrders` 结束全部订单。
6. 服务器订单簿 `FLDOrderBook`（`LDOrderRules.h`）绑定一个（match, life）：请求水位、幂等窗口与激活节流
   只对该局的权威时钟（每局从 0 起算）和该命身份有效，而 Controller 跨局存活。新身份换绑时整体重建；
   Combat 的 MatchReset 经 `ResetMatch` 结束旧局订单并整体丢弃订单簿与内容缓存。
   回执序号是拥有连接的单调序，两端都不随局复位，客户端另按 Match/Life 过滤。
7. 订单的发送、进行中与到达只写入组件自己的 `Status`；HUD 唯一的提示条只经 `PostIntentNotice`
   收到真正的失败（无法到达、路线受阻、被拒、心跳耗尽等）与“复活保护没了”，
   玩家自己的新订单替换旧单或按 S 停止不算失败；同一失败再次发生时会再次提示。
8. 指针与操作手感（拥有客户端本地表现，`ALDPointerFeedback`，不复制、无碰撞、不影响寻路）：
   - 自己的地面订单在发出当帧于落点显示收缩的绿色虚线环；本地路线检查（与预测同一 `PlanRoute`）已失败时
     改为红叉，服务器拒绝仍以提示条给出原因。A 攻击移动的地面点显示同一收缩虚线环的红色版本（路线已失败时同样是红叉）。自己的 AttackUnit 在目标呈现位置显示收拢的红色虚线环。
   - 悬停：指针下当前帧获准可见的单位（`ObservedUnderPointer`，与右键攻击同一选取）脚下显示与攻击标记同色的红色虚线环（近黑底衬），
     光标不变。`ReadHovered` 供 HUD 的技能合法性显示使用。
   - 光标：`ULDGameInstance::OnStart` 用 `LDCursor::Install` 把游戏视口的每种光标类型都换成软件箭头（白面深色描边，尖端在热点上），
     系统光标不再出现；攻击移动待确认时 `TickPointer` 设 `LDCursor::AttackAim`（红光描边），技能待确认时设 `LDCursor::CastAim`（蓝光描边），其余为普通箭头。
   - 选中：左键检视的单位（`ReadInspected`，当前帧仍列出时）脚下显示一圈不转动、短而密的浅色点线环（`SetSelected`，半径 64 cm，
     在 76 cm 的红色悬停环之内），HUD 不再在其身上画方框；失视野或换帧不再列出即消失。
   - 按住移动键（默认右键）时，地面右键之后按指针地面点继续下普通 MoveGround 订单：
     每秒至多 8 次，且地面点相对上一单移动不少于 40 cm（`LDOrderRules::ShouldRepeatHeldMove`），
     本地路线检查不通过的点静默跳过；每单都是新 identity，经同一订单簿与 0.1 s 激活节流，最新待处理单替换旧单。
     按住 Shift 的右键只发一条排队订单，不按住重复。
   - Shift 队列里自己的地面步骤在落点显示小圈（攻击移动为红色），并有一条虚线从身体经当前目的地连到各点
     （`ALDPointerFeedback::SetQueue`）；步骤开始或结束、Stop、新生命时移除。
   - 世界点击、悬停与武装确认共用 `PointerPosition`/`GroundUnderPointer`；DEV 场景可用 `DevSetPointer`
     代替鼠标（仅 `WITH_DEV_AUTOMATION_TESTS` 生效）。
   - 预输入（`LDCombatPreInput`）：自己的 Q/W 技能前摇期间再按的 Direct 键在拥有客户端暂存一个（新按的替换旧的），
     前摇结束（已到 `ActionReleaseAtMs` 或视图不再显示前摇）时作为普通 Direct 请求发出（新序号、当前来源修订）；
     S、新生命/新对局、输入被抑制即丢弃，暂存超过 1 s 丢弃并提示。普攻无需暂存：攻击订单本就在服务器等身体空闲。
   - 快速施法（本地设置 `QuickCast`，默认关）：打开后，对单位技能在指针下已有获准可见单位、方向技能在指针下有地面时，
     按键即按同一确认路径朝指针施放；对单位技能指针下无单位时仍进入普通武装、左键确认。
   - 施法后自动攻击（本地设置 `bAutoAttackAfterCast`，默认开）：不带 Shift 的施放订单完成后，客户端对单位技能的目标、
     或地面/方向技能施法前正在攻击且仍可见的单位发出普通 `AttackUnit` 订单；规则见 `LDOrderRules::CastFollowUpTarget`
     与 `CastFollowUpStep`，细节见 [操作与选择](../demo/CONTROLS_AND_TARGETING.md)。
   - CharacterMovement：`MaxAcceleration` 与 `BrakingDecelerationWalking` 均为 8000，
     300 cm/s 基础移速约 38 ms 达到全速；朝向随转向输入即时设置（`SetFacing`）。

AttackUnit 接近并持续普攻；AttackMove 在合法可见候选中选离自身最近者（同距取较小观察句柄），
搜敌半径取 `TARGET.AttackMoveAcquireRadiusCm`；目标被观察到死亡后重新选敌，无目标时走向原地面目的地，
到点且无目标即结束。失去视野终止订单并解除锁定，不恢复原目的地。
真人订单需要拥有连接心跳续租；没有 LocalPlayer 的服务器 AI 控制器以注册绑定为租约。

被嘲讽（Combat 的 `ReadTaunter`）时，`ProcessAuthorityOrders` 按 `LDOrderRules::ForcedOrderStep` 强制订单：
当前订单以 `Taunted` 取消，待处理的自身订单（含 Stop）以 `Taunted` 拒绝；改为服务器生成的强制追击
（`Order` 为 0、不进订单簿、无心跳租约、不发回执），只经本观察者当前区间的嘲讽者句柄
（Vision `FindObservedHandle` + `ResolveObservedUnit`）接近并经同一 `StartNavAttack` 普攻；尚未进入本观察者帧时原地等待。
嘲讽结束后强制追击结束、控制权交还，不恢复被替换的订单；强制追击自行结束（无路径/无进展）时 0.1 s 后重建；这个重建时刻按比赛时钟记录（`LDOrderRules::FForcedRetry`），`ResetMatch` 清零，不带入下一局。
对手 AI 被嘲讽时暂停身体意图（`LDOpponentRequests::BodyIntentsHeld`），被嘲讽打断的订单按 Dropped 结算，不计失败。

已收请求的完整条款参与重复/冲突判断；更新条款不能复用原 identity。
接收时间由核心按同一比赛原点生成 NotBefore 审计桶，不能把帧间迟到命令放进更早补进的运动区间。
心跳只刷新当前已激活 identity，不创建订单；序列与取消计数耗尽明确失败，不回绕。

目标句柄由相应世界的观察服务重新解析。地面点击可以使用静态地图信息，
但动态目标必须当前获准可见；不能以寻路预览、自动追踪或避障反馈泄露隐藏单位。
公共休息室的配装和结算配装阶段，地面移动会绕开当前观察帧里的可见角色与全部共享真实训练目标；
没有静态假人外观替身。拥有客户端用自己的当前观察帧预测同一转向，服务器仍执行原有碰撞、路线与失败判定。
其他阶段不使用这项绕行，观察帧不可用时也不读取隐藏身体。

## 路径与物理边界

接近目标要找碰撞外的合法位置，不直接寻路到物体被阻挡的中心。
不可走、层高有歧义、无完整路径或长期无进展，都应停止并给出明确失败，
不能把 partial path 当作成功到达，也不能无限重算或穿过已生效的阻挡。

服务器沿路线进展和重算在 `AdvanceAuthorityMotion/ProcessAuthorityOrders`，
`TickComponent` 只承担拥有者预测、心跳和指针输入，镜头在本地相机求值前更新。当前真实 CMC `CurrentFloor` 必须对应注册地面；
无有效支持面不借瞄准 SurfaceHint 向下补接地。服务器保留独立的命令身份和合法性校验；
注册地面检查以胶囊的局部向下 sweep 核对真实支持点的作者高度与法线，最低只到脚点下一个原生
`MAX_FLOOR_DIST` 间隙；接触法线可沿作者允许的最大坡度变化，以覆盖坡顶/坡脚与相邻平面的胶囊局部接触，
但不借向下兜底或允许更陡的支撑面。坡道下沿允许胶囊由脚点旁的坡面支撑，不再把脚点正下方是平地误判为悬空。
路线投影仍保留脚点的 XY 和该处作者地形高度，不移到胶囊的偏心接触点。
出生预约、营地身体和训练假人则使用物理 sweep 首次接触时的胶囊中心，加原生稳定贴地间隙，
并在该实际原点重新检查静态障碍，避免坡缝处构造后被 CMC 移位而破坏预约。
原生 correction 不是可以跳过命令验证的理由。环境碰撞更新与路径重算的关系见
[世界与数据管线](WORLD_AND_DATA_PIPELINE.md)。
所有路线规划的起点由共享的 `LDPathSteering::PlanStart` 给出（参赛者经 `QueryPlanStart`/`PlanRoute`，营地成员同样复用）：
先确认身体在注册支持面上的合法地面，若身体因贴墙、贴身或转角切内侧停在 Recast 保守收缩的边缘外，
只把平面起点移到一个胶囊半径内最近的 navmesh 点，该点须在同一支持面上重新通过 `QueryGround`；
更远则保持原起点，由注册表的完整路径查询明确拒绝。
终点按目标类型区分（`LDPathSteering::PlanRoute` 的 `EGoal`）：玩家点选的地面（MoveGround、AttackMove，
含按住右键连续移动）先按点击处严格规划；点击处不是合法可达站位（墙、箱体、贴障碍过近或不连通区域）时，
在身体所在地面高度投影到最近 navmesh 点，并用允许 partial path 的查询把候选限制在起点连通区域；
按离点击点的距离尝试候选，经 `QueryGround` 与完整路径查询重新验证后作为终点（已批准“everywhere targetable despite non-traversable”）。落点标记仍显示在实际点击处；接受后的续行/重算沿用投影终点；
世界/修订过期、缺少支持面或胶囊不符不投影，仅在没有合法可达候选时明确拒绝。被追击身体的脚点（AttackUnit 的准入与追击
重算、AttackMove 锁定目标后的追击，以及拥有者对攻击订单的预测）与起点同样经 `PlanStart` 落到一个胶囊半径内的
navmesh 点。注册表的拒绝原因写明被调整的是路线起点还是目的地。

## 输入所有权

输入依次由文本框、模态界面、拖拽/捕获、面板、待确认动作、小地图和世界处理。
同一次手势只能有一个消费者；按下时获得的所有权必须维持到释放。
打开窗口的点击不能继续落到新窗口内的按钮，关闭面板也不能重放仍按住的输入。

使用 `CanWorldCommand`、`V05Back`、`SetMenuOpen`、`ClearControls`
等现有控制器边界，不在每个 Widget 中重新判断一套角色可行动状态。
`CanWorldCommand` 是两半之积（实现在 `LDPlayerInput.cpp`）：
`IsWorldLifeValid()` 只看生命与阶段——`PublicMatch`（唯一的代次/阶段来源，不读 DemoLobby 镜像）的代次等于
本人 Core 生命投影的 Match、阶段为 Rest/Development/CaptureOpen（休息中在公共休息室照常移动与操作；返回倒计时内命令照常发出，由服务器以 `NotStarted` 拒绝订单、攻击、施法与冲刺）、本人 Alive 且持有身体；
`IsWorldInputSuppressed()` 只看本地输入——菜单/设置（`SetMenuOpen`）、文本编辑或子确认（`IsWorldInputCaptured()`），
以及菜单、焦点或映射变化后仍未释放的按键。新的世界命令（订单、Q/W/D、A、S、世界与小地图点击）两半都要满足；
Navigation 只在生命/阶段一半失效时停单。打开菜单或设置、窗口内焦点移入面板控件（引擎经 `FlushPressedKeys` 清键）
和重建映射都只经 `ReleaseLocalInput` 取消本地准备态与回中锁存并等待按键释放：
不 Stop 已接受订单、不停心跳、不阻断身体沿路径前进，关闭后也不重放仍按住的输入。
只有操作系统失焦由 Navigation 一次 Stop 并停发心跳，回焦后需新手势，不自动续走。
暂停菜单不暂停共享世界；死亡、离开和终局清除适用的移动/指向/交互状态。

`ULDUIRoot` 是唯一的本地界面层来源：`TopInputLayer()` 报告文本编辑、子确认或主面板；
同一时刻只开一个主面板（`ToggleMainPanel`：菜单、设置，以及经 `RegisterMainPanel` 登记的属性/三选一，DEV 房间另有测试），
未登记的面板明确提示尚未接入。Esc 经 `V05Back` 每次只处理一层：
`CloseTopModal`（结束文本编辑或关闭子确认）→ `CancelArmedCombat`（取消战斗准备态）→ `CloseTopPanel`（关闭主面板）→ 打开菜单。
关闭面板不停止已接受订单、不提交或回滚领域请求。
顶层为文本编辑或子确认时，`IsWorldInputCaptured()` 拦下全部新输入：Q/W/D 与 A 不进入准备态也不发送
（HUD 图标同样经 `ActivateCombatEntry`），S、世界点击、镜头键与缩放都不生效；子确认在已有准备态之上打开时，
控制器在下一帧前取消该准备态。它属于输入抑制一半，只拦新输入，已接受订单与其心跳继续。
P 在死亡等待期间也能切换属性视图；Lobby 与前端不响应。

动作入口只来自 `ULDControlSettings::Definitions()` 这一份 Demo 注册表，不按 profile 分派。
修改按键要同步 `LDControlSettings`、控制器路由、帮助文本、HUD 标记与相应输入检查；
不得只修改图标标签，也不能让消费者继续查询已退役动作名而静默得到无效键。

## 相机与小地图

镜头使用固定方向的俯视斜角，可跟随、回中、平移和缩放。
`Character\LDCameraSettings.h` 单点定义默认臂长 2250 cm、范围 1300–2400 cm、步长 80 cm、
pitch −60°、yaw −90°（世界 −Y 在屏幕上方）、FOV 55°和目标偏移 Z=20 cm；身体、等待相机、设置与美术镜头检查共用。
新安装和恢复默认使用 2250；设置页“采用推荐镜头”只改 Zoom，其他本地偏好和姓名保持不变。
`PanCamera`、`Recenter`、`ToggleFollow`、`PanTo` 与 `UpdateLocalCamera` 共享镜头状态。
镜臂的位置与旋转都使用世界绝对坐标，人物转身不旋转已平移的镜头中心。
`ALDPlayerController::UpdateCameraManager` 在本帧运动/校正重放后先更新本人显示偏移，
再更新镜头中心；`ALDCharacter::CalcCamera` 当次计算固定镜臂几何，不读取较早 tick 缓存的 socket。
本地控制器在同一次相机求值前显式绑定实际 Pawn，不依赖引擎自动选镜头或 OwnerState 回执先后顺序。
Pawn 复制暂时缺失（包括本地仍显示 Alive）时保留最后渲染的相机位置、方向和 FOV；
死亡等待复用该镜头，Pawn 到达后立即重新绑定，迟到的引擎视点复位不能让它退回控制器原点。
仅 DEV 美术截图探针可临时接管本地相机；接管期间不更新游戏镜头或缓存截图姿态，
释放接管或截图相机销毁后恢复正常的 Pawn / 等待镜头选择。
键盘与边缘平移最多消费当前帧的 50 ms 输入，长帧不把当前指针位置解释成整段卡顿期间都在平移；
人物运动、权威时钟与网络超时不采用此显示输入限幅。
边缘平移需尊重应用焦点、UI 遮挡和操作模式：失焦、指针在 UI 上、小地图拖动或战斗准备态期间不边缘平移；
滚轮在面板上滚动面板，不同时缩放世界。

死亡等待期间（`IsAwaitingRespawn`，输入门为 `CanCameraCommand`），`UpdateLocalCamera` 用同一镜头状态驱动
控制器的等待镜头，按身体默认镜臂（`ALDCharacter::DefaultArmCameraPose`）摆放：键盘/边缘平移与小地图拖动
移动静态地图镜头，缩放直接改写已应用的镜头距离偏好，新身体沿用该值；回中与跟随键提示“无存活角色”。
等待镜头从这一生最后的镜头中心起步，只夹在静态地图边界内，不跟随任何单位、不产生游戏移动，
也不改变视野来源；复活后由新身体接回镜头并恢复已保存的跟随偏好。

小地图的坐标转换使用当前地图定义与本地 `MinimapOrientation`：跟随镜头（默认，地图上方即屏幕上方）、跟随角色朝向、固定正北（世界 −Y 朝上）。
三种都是真实俯视的旋转加统一缩放，绘制、镜头框和拖图/右键移动共用 `LDMinimap::MakeView/ToFrame/ToWorld`，不镜像。
点击静态地图不授予对隐藏单位的锁定或远程交互能力，
远处镜头也不会把身体的攻击距离一起移动。
小地图左键拖动只移动镜头，死亡等待期间也可用；右键移动仍要求 `CanWorldCommand`。

本地按键、文字缩放与镜头偏好通过 `LDControlSettings` 保存，不能和对局经验、技能冷却等混为一份状态。
保存失败需反馈，配置迁移不能默默给多个动作绑定同一个按键。

## 本地设置与显示

`ULDGameInstance::GetLocalControlSettings()` 持有应用中的本地偏好，Controller 引用同一对象。
按键只有 `ULDControlSettings::Definitions()` 这一份 Demo 注册表：`Select`、`Navigate`、`AttackMove`、
`Skill1`/`Skill2`/`SkillE`（Q/W/E 角色主动）、`Dash`（D 通用冲刺）、`Stop`、`Attributes`、`Scoreboard`（按住 Tab 看排行榜）、
`Mark`（F9 试玩标记，见[开发指南](../DEVELOPMENT.md#试玩问题排查)）、
`PanUp/PanDown/PanLeft/PanRight`、`Recenter`、`Follow`、`Zoom` 与保留给 Esc 的 `Back`。
默认键取自 App Spec §3.1 与 [操作与目标选择报告](../demo/CONTROLS_AND_TARGETING.md) §2/§3.2，不是新批准的镜头或玩法数值；
没有 profile/registry 参数、第二份默认表或旧 Sprint/快捷位/多技能槽/Help/Inventory 动作，
不自动迁移注册表、调换冲突键或从另一个文件回落。

`LoadLocalSettings` 区分 `Loaded`、`Migrated`、`Missing`、`UnsupportedSchema`、`Corrupt`、
`ReadFailed` 和 `InvalidStorage`。缺文件可使用当前默认值，但不会称为已保存；
损坏或不支持的文件保持原样，菜单与设置页提示显式恢复。
只有绑定不合法而镜头/文字字段合法时，恢复保留这些无关偏好，当前绑定仅用于本次会话；
确认替换之前，通用 flush 不能覆盖原文件。`Key`/`Rebind` 只接受该注册表中的动作，
已退役动作名得到无效键并被拒绝，草稿或文件中的此类绑定会被逐名指出。

本地文件使用 `ULDControlSettings` 内的有限字段序列化，而非反射遍历：
schema、edge/follow、文字比例、zoom、`PlayerName`、`MasterVolume`、`CameraPanCmPerSec`、`MinimapOrientation`、`bQuickCast`、`bShowNetworkDiagnostics`、`bShowFPS`、`bAutoAttackAfterCast` 和绑定是完整正向清单。
当前文件 schema 是 19，与产品/网络版本无关；schema 5–18 显式迁移，旧通用 SaveGame 格式及不支持的 schema 不静默覆盖。
schema 14 迁移保留全部有效偏好及绑定，两个诊断开关默认关闭；加载本身不改写旧文件。
schema 15 的按键表早于试玩标记：schema 5–15 迁移时若缺 `Mark` 且默认键 F9 未被其他动作占用，
补上默认 F9 并在提示中说明；F9 已被占用时按 `Corrupt` 走恢复；当前 schema 缺它不补。
schema 16 的按键表仍有已退役的装备主动键 `Item1`/`Item2`：schema 5–16 迁移时只删除这两项并在提示中说明，
其余绑定原样保留；当前 schema 的文件含有它们时按未知动作走恢复。
`MasterVolume` 是无量纲 0..1 的主音量，默认 1（不衰减），越界或非有限值使文件按 `Corrupt` 处理；
设置页滑条经 `ULDGameInstance::SetAppliedMasterVolume` 直接改写已应用对象并请求延迟保存，
恢复默认会把它还原为 1。音量的实际生效链见 [视野与 UI](VISION_AND_UI.md#音效与音量)。
`CameraPanCmPerSec` 是键盘/边缘平移的镜头速度（cm/s），默认 1800，合法域 900..3600（只作用镜头，
不影响视野、世界距离或角色速度）；越界或非有限值使文件按 `Corrupt` 处理。设置页“镜头”分类以 300 cm/s 步进，
经草稿与 `ULDGameInstance::ApplyLocalSettings` 应用并保存（`SetAppliedCameraPanSpeed` 供直接改写已应用对象的调用方），
导航经 `GetCameraPanCmPerSec()` 读取已应用值，恢复默认还原为 1800。
`MinimapOrientation` 是小地图朝向（0 跟随镜头、1 跟随角色朝向、2 固定正北），默认跟随镜头；
越界值使文件按 `Corrupt` 处理。设置页“界面”分类的三选一同样经草稿应用，恢复默认还原为跟随镜头。
设置页按分类“恢复本页默认”（UI_AND_SETTINGS §8）：确认卡先列出本页将恢复的默认值（其他页、名称与本局进度不变），确认后
音频/操作/镜头/界面经 `ResetPreferenceGroup` 只还原对应偏好组并立即应用、保存，四组合起来正好是 `ResetLocalDefaults`；
画面页的显示/画质取产品默认（临时 `UGameUserSettings::SetToDefaults`，不是 `ResetToCurrentSettings`），
VSync 与画质随该次显示应用生效并保存；与当前显示相同时不应用。
默认画质的渲染比例 `ResolutionQuality` 为 0（沿用项目的 `r.ScreenPercentage`），引擎对 0 不做任何事；
`ULDGameInstance` 每次应用画质后经 `LDLocalSettings::ApplyRenderScale` 撤销 Scalability 优先级的旧比例，渲染比例立即回到项目默认，无需重启。
`PlayerName` 为空表示尚未设置，绝不填入默认名；它不是比赛数据，恢复默认（`ResetLocalDefaults`/`ResetBindings`）也不删除它。
调用方用 `SetAppliedPlayerName` 修改已应用对象，并通过 `ULDGameInstance::RequestLocalSettingsFlush()` 请求保存；草稿不能改名称。
`ValidatePlayerName` 只做本地格式检查：非空、首尾无空白、最多 16 个字符（UTF-16 代理对算一个）、
无控制字符或残缺代理，且不含经 URL 选项 `?LDName=` 传递时有歧义的 `? = # / \ : % & " ' < > |`；
空名称可以保存，但建房/加入前必须先设置。服务器会话域以同一规则再次校验，非法即拒绝入房。
文件中的非空非法名称使整个文件按 `Corrupt` 拒绝，且不提供恢复偏好。
schema 5 是不含名称的同一正向清单，按显式迁移读取：校验通过的绑定和镜头/文字偏好原样保留，名称为空，
返回 `Migrated` 及“先设置名称再建房或加入”的提示；对象处于 `ConfirmedApplied` 而非 `PersistenceVerified`，
立即生效，下一次 flush 写成当前 schema，加载本身不改写旧文件。
schema 6 是含名称但不含主音量的同一清单，同样显式迁移：名称、绑定和镜头/文字偏好保留，主音量取默认 1。
schema 7 是不含镜头平移速度的同一清单，显式迁移时平移速度取默认 1800；schema 5、6 迁移同样取 1800。
schema 8 是不含小地图朝向的同一清单，显式迁移时朝向取跟随镜头；schema 5–7 迁移同样取跟随镜头。
schema 9 是不含快速施法的同一清单，显式迁移时快速施法取默认关闭；schema 5–8 迁移同样关闭。
schema 17 不含施法后自动攻击，显式迁移时该开关取默认开启；schema 5–16 迁移同样开启。
schema 18 保留已存的施法后自动攻击偏好。schema 5–18 迁移时若缺 `SkillE`，先移除已退役装备键，再在 E 未被其他动作占用时补默认 E；
E 已被占用时不抢占原绑定，按 `Corrupt` 走恢复。已有 `SkillE` 绑定保持原样，当前 schema 缺它不补。
schema 10 的按键表早于 D 冲刺：schema 5–10 迁移时若缺 `Dash` 且默认键 D 未被其他动作占用，
补上默认 D 并在迁移提示中说明；D 已被占用时不改键、不挪键，缺动作的文件按 `Corrupt` 走恢复。
schema 11、12 已有 Dash，迁移时不补该键，但仍按上述规则补入后来新增的动作。schema 5–11 必须先按旧 1100–1700 cm、
schema 12 按旧 1100–1900 cm 范围校验 Zoom；在当前 1300–2400 cm 内原样保留（包括旧默认 1400、1850），
低于 1300 cm 的调整为 1300 cm 并在提示中说明；超出各自旧范围的值仍按损坏拒绝，不因新范围扩大而变合法。
迁移提示说明保留或调整后的距离及“采用推荐镜头”入口；加载不写盘，下次核验保存写成当前 schema。
schema 13 已使用当前镜头范围，但按键表早于排行榜：schema 5–13 迁移时若缺 `Scoreboard` 且默认键 Tab 未被其他动作占用，
补上默认 Tab 并在提示中说明（schema 13 不再附镜头距离说明）；Tab 已被占用时按 `Corrupt` 走恢复；schema 14 缺它不补。
schema 5–13 中的损坏、非法偏好或注册表外动作
仍按当前 schema 的 `Corrupt` 与恢复流程处理，不返回 `Migrated`。
schema 4 是已退役的多注册表格式：读取时只校验完整性并列出其中的退役动作，返回
`UnsupportedSchema`，不加载也不迁移任何旧绑定（包括 Demo 仍在使用的动作名）；
合法的镜头/文字偏好走既有恢复流程，Demo 默认绑定只在本会话生效，
原文件在设置页确认替换前保持不变。当前 schema 的文件若含注册表外动作，同样明确拒绝并走恢复。
写入先验证字段，再写同目录临时文件并实际回读，使用 Windows 原子同卷替换且不预删目标，
最后再次回读；替换前显式 flush 文件数据，替换失败保留原先已核验文件，只清理失败候选。
读取元数据时只有明确的文件/路径不存在才视为 `Missing`，权限、共享或 I/O 错误均进入
`ReadFailed`/恢复保护，不用不确定的存在性布尔值生成可自动保存的默认值。
只在核验后清 dirty。
`SaveLocalSettings` 返回类型化结果，`EditingDraft` 不可保存；写失败保留已应用值与重试入口。
`FLDLocalSettingsStorage::User()` 只选择唯一的用户文件 `Saved/SaveGames/LockdownLocalControlsV1.sav`，
沿用原主文件名，使旧格式得到明确提示；已退役注册表的 V07/V08 槽文件不再读取，也不会被删除。
测试必须使用 `Automation(FGuid)`，文件位于忽略的 `Artifacts/Automation/LocalSettings`，
无效夹具不会回落到真实用户文件。无人值守/commandlet 的 GameInstance 未显式指定夹具时禁止用户文件写入。

显示值仍只由引擎 `UGameUserSettings` 管理。游戏只有一种窗口：无边框全屏、桌面分辨率。
`ULDGameInstance::Init` 在 `UGameEngine::Init` 创建窗口之前经 `LDLocalSettings::HoldBorderlessDesktop`
把旧存档中的窗口模式与分辨率改为无边框桌面分辨率并确认，再以 `ApplyResolutionSettings(true)` 请求窗口，
所以第一帧就是无边框；编辑器 PIE 窗口归编辑器管理，不受影响。引擎的启动覆盖（`-windowed`、`-ResX/-ResY`）
仍作用于这次请求，只给自动化场景和同机多开使用，存档中保持无边框。`DefaultInput.ini` 关闭 Alt+Enter/F11 切换全屏。
设置页没有窗口模式、分辨率或保留/回滚试用。

`LDControlsPanel` 的显示草稿只有 VSync 与整体画质（设置页“画面”分类），草稿不写配置；草稿与已核验的生效值不同时才算未应用
（应用失败后设置字段里的请求值不算生效值，直到再次应用或重试保存成功，见 UI_AND_SETTINGS §8），
画面页底部的应用栏列出未应用项，“应用”经 `ApplyDisplaySettings` 立即应用并保存，“撤销更改”丢弃草稿。`SaveSettings` 的 void 返回不代表成功，
核验从实际配置文件读入新的 `FConfigFile`，不从 GConfig 缓存读取；
UE 5.8 的 `GGameUserSettingsIni` 等常量是缓存标识，不保证是物理路径。
`ResolveConfigDestination` 从当前 `FConfigBranch::IniPath` 取得真实目标，缺失即失败；
NoSave、mutator 和 Flush 继续使用原缓存标识，不能把标识拼到工作目录后假装完成磁盘回读。
Editor 的独立质量配置必须在原生保存阶段显式 flush 后核对；
`Scalability::SaveState` 仅暂存该文件，不能依赖 `SaveSettings` 对另一个目标的 flush。
已有 `NoSave` 禁令不被解除，失败保留 dirty 与重试入口。
VSync/画质应用失败与持久化未核验分开反馈。被拒绝的同一份草稿不能再次应用，直到玩家修改草稿；
提示同时说明可以撤销更改，不让玩家反复重试同一个失败。

Esc、强制关页、Widget 销毁、网络失败、旅行和退出前保存已确认的本地值，显示 dirty 时重试显示保存。
`ForceClose` 用于终局或面板切换，普通 `Close` 处理当前捕获及未应用显示草稿：
有未应用草稿时第一次返回切到画面页，确认卡显示“应用并返回 / 放弃更改并返回 / 继续编辑”，
确认卡显示时再按“返回”或 Esc 即放弃草稿并返回（#99），不会在提示的显示与隐藏之间循环。
“应用并返回”失败时留在确认卡并写明失败原因，默认焦点移到“放弃更改并返回”。
`Close`/`ClosePanel` 只在面板真正关闭时返回 true；供 Root 路由 Back 的 `HasChildConfirmation`/`CloseChildConfirmation`
先结束按键捕获；未应用草稿确认卡与“放弃更改并返回”相同，其他确认按“保持不变”关闭，不应用也不丢弃任何值。
按键捕获在该绑定行内进行，被拒的键在行内说明且捕获继续，Esc 只结束捕获；切换分类页签（含捕获中直接点页签）也按“保持不变”结束捕获与确认。
设置页的交互控件（含页签 `Tab*`、每个动作的按键框 `Bind<Action>`）只构造一次并带稳定控件名，自动化按名称查找，不依赖中文文案；
隐藏分类页折叠，不在键盘焦点顺序里，自动化先点该页签再操作页内控件。
最后一次实际 zoom/follow 在 GameInstance 中标记 dirty，旅行/退出不等待延迟 tick 才 flush。
主音量是设置的唯一音频消费链：GameInstance 每次加载或应用本地偏好后把 `MasterVolume` 交给 `ULDAudioSubsystem`；
设置只维护唯一注册表与本地偏好，动作分派和 HUD 标签由各自消费者负责，
不能以本地设置控件代替全界面验收。

## 维护检查

Demo 基础身体/地面链使用 `RunDemoCoreCheck.ps1` 的 DEV-only 三拥有连接夹具，
结合 `Lockdown.Rules.DemoCore` 和命令存活性规则；该夹具不开放普通 Start，也不证明完整 CT06。
`Lockdown.Rules.DemoMotionProbe` 在隔离原生物理世界验证滑移、非空 StepUp 回滚、
ledge 撤销、失去支持/同 XY 落地，以及拒绝缺失的原生位置记录；不改生产地图或共享引擎。
拥有连接下的订单、S 取消与输入路由由 `RunDemoCombatCheck.ps1` 覆盖；
该场景还在 Host 上经 DEV 指针走普通 `ClickMove` 截取落点标记、悬停环与攻击标记，并报告操作手感实测；
`Lockdown.Rules.DemoControlFeel.*` 覆盖按住右键的重发节流与预输入判定。
重点覆盖：新命令取代旧命令、S 后迟到确认、死亡后旧路线、真实客户端的坡面/阻挡、
UI 不穿透、重新获得焦点不自动行动。
`LDLocalSettingsTests` 使用显式文件夹具覆盖唯一注册表与退役动作拒绝、schema 4 不兼容恢复、
schema 5–11 显式迁移（`LocalSettingsDashMigration` 覆盖补 D 与 D 被占用时的拒绝；`LocalSettingsZoomMigration` 覆盖旧范围校验、旧值保留与核验升级），
`LocalSettingsCameraZoom` 覆盖共享镜头默认、缩放夹限和推荐按钮的仅 Zoom 修改；主音量、镜头平移速度、小地图朝向、快速施法保存回读（`LocalSettingsMasterVolume`、`LocalSettingsCameraPanSpeed`、`LocalSettingsMinimapOrientation`、`LocalSettingsQuickCast`）、
小地图的换算（`Lockdown.Rules.Minimap.{CameraUp,FacingUp,NotMirrored,NorthFixed,RoundTrip,FitsFrame}`，`LDMinimapTests`）、
产品显示默认（`LocalDisplayProductDefaults`）、只用无边框桌面分辨率（`LocalDisplayBorderlessOnly`）、
损坏/越界拒绝与恢复、草稿隔离、旅行前 flush（`LocalSettingsTravelFlush`）和显示配置实际回读；
真实窗口、VSync/画质应用及布局仍需非 nullrhi 的原生界面检查（`RunDemoFrontendCheck.ps1`）。

# 移动、输入与镜头

| 项 | 内容 |
| --- | --- |
| 规则 | [App Spec](../APP_SPEC.md) §3.1 操作；数值只在[内容与数值](../demo/CONTENT_AND_BALANCE.md)，本文只引用键名 |
| 文件 | `Navigation\LDNavigationComponent`（客户端意图、预测、指针、镜头）、`LDNavigationOrders.cpp`（服务器订单簿）、`LDOrderTypes.h`、`LDOrderRules`、`LDPathSteering`、`LDPointerFeedback`、`LDDemoNavigationSystem`；`Framework\LDPlayerInput.cpp`（映射、按键、Esc、缩放、拾取、检查）、`LDPlayerOrders.cpp`（Q/W/E/D、A、S、快速施法、预输入）；`Combat\LDCombatPreInput.h`；`Character\LDCharacter`、`LDCharacterMovementComponent`、`LDCharacterMotion.cpp`、`LDCameraSettings.h`；`UI\LDControlSettings`、`LDControlsPanel`、`LDMinimap`；`Region\LDMapRegistry` |
| 入口函数 | `SubmitOrder` / `SubmitCast` / `CancelOrder` / `QueryOrder` / `QueueAction`；`ProcessAuthorityOrders`（Actions 阶段）、`AdvanceAuthorityMotion`（Motion 阶段）、`EndLifeOrders` / `ResetMatch`；`ALDPlayerController::CanWorldCommand` / `V05Pressed` / `V05Back` / `RebuildV05Mappings`；`ULDControlSettings::Definitions` / `LoadLocalSettings` / `SaveLocalSettings` |
| 不变量 | 一个身体只有一套 CharacterMovement 运动；一个当前订单 + 一个待受理槽 + 最多 8 步 Shift 队列；同一手势只有一个消费者；进攻意图（含失败）解除复活保护；动态目标必须当前获准可见；动作只来自 `Definitions()` 一份注册表 |
| 检查 | `Lockdown.Rules.DemoControlFeel.*`、`DemoOrders.*`、`DemoMotionProbe`、`DemoPathSteering.*`、`ControlMappings`、`Minimap.*`、`LocalSettings*`；场景 `RunDemoCombatCheck.ps1`、`RunDemoCoreCheck.ps1`、`RunDemoFrontendCheck.ps1`（见[维护检查](#维护检查)） |

头文件在 `Source\Lockdown\Public`，实现在 `Private`。链路为 Enhanced Input → `ALDPlayerController` →
`ULDNavigationComponent` / `ALDCombatDirector` → CharacterMovement。真人经拥有连接 RPC 提交意图；
对手 AI（`ALDOpponentController`）在服务器上直接调用同一组 `SubmitOrder/CancelOrder/QueryOrder` 与 Combat 入口，
不伪造 LocalPlayer 或 RPC，AI 订单以注册绑定为租约、不需要心跳。没有连招、多单位框选、编队、团队命令或自由旋转镜头。

## 操作表

默认键来自 `ULDControlSettings::Definitions()`，可重绑。世界命令先过[输入所有权](#输入所有权)门槛；客户端禁用不代替服务器拒绝。

| 输入 | 本地结果 | 权威结果 |
| --- | --- | --- |
| 右键地面 | 发 `MoveGround`；同帧点击点显示绿色收缩虚线圈，本地路线检查失败改红叉 | 服务器严格求完整路径，CMC 实际行走；partial path 不报告到达 |
| 按住右键拖动 | 指针地面点相对上单 ≥ 40 cm 时最多每 0.125 s 再发普通 `MoveGround`（`LDOrderRules::ShouldRepeatHeldMove`）；本地路线已失败的点跳过 | 每次是新身份订单，受激活节流与最新待受理替换约束 |
| 右键可见单位 | 发 `AttackUnit`；目标脚下红色收拢圈 | 重查观察权限后接近、追击、在射程内逐次申请普攻 |
| 左键（未准备） | 检查指针下可见单位；点空地清除检查对象 | 不发订单 |
| A，左键 | 先用与右键攻击相同的拾取（`ObservedUnderPointer`）：指在可攻击单位上即发 `AttackUnit`，否则以指针地面发 `AttackMove`（`LDOrderRules::AttackConfirmKind`）；无单位也无地面时发缺省目的地 | A 本身不攻击；两种确认都可 Shift 排队、都解除保护；缺省目的地由服务器拒绝 |
| S | 清准备态与预输入，停止本地预测，发递增 Epoch 的 Stop，并按已发出的最大 `DirectSequence` 取消 Combat 动作 | 停移动/追击并取消可中断动作；已释放效果与伤害不回滚 |
| Q/W/E | 读 Combat owner view 的目标模式：Self 直接发送；Unit/方向/地面点进入准备，左键确认；对单位确认发 `CastUnit` 订单，对地面点或方向投射发 `CastPoint` | 服务器按来源、修订、目标与资源重查；空槽也发送由服务器拒绝；拒绝不打断当前订单 |
| D | 通用冲刺（`ELDCombatEntry::Dash`，`Innate` 来源）按下即朝指针方向发出（`LDCombatPreInput::FiresOnPress`），不准备、与快速施法无关；无地面时发缺目标请求 | 重查冷却、定身与起步阻挡；遇阻挡停在最远合法位置；不是进攻意图 |
| P | 头像下加点图标与属性视图之间切换 | 只读自身投影；死亡时可用 |
| 按住 C | 暂时展开已装构筑与加点图标，松开恢复 | 本地显示 |
| B | 开关工坊（`ULDHUD::ToggleWorkshop`），仅“构筑”可点或工坊已开时生效；存活与死亡等待都可用 | 本地显示；文本输入或子确认占用时不响应 |
| 按住 Tab | 局内排行榜 | 只读公开 `KillFeed.Scores` |
| F9 | 试玩标记：本机日志写 `LD_PLAYER_MARK`，经 `ServerPlaytestMark` 让房主日志记下；HUD 提示“已标记 #N” | 只在本局进行中生效；房主日志每名参赛者每秒最多一条（[开发指南](../DEVELOPMENT.md#试玩问题排查)） |
| 方向键/屏幕边缘 | 平移镜头 | 不改角色、视野或订单 |
| Space / Home | 按住临时跟随自己 / 切换跟随并保存偏好 | 本地 |
| 滚轮 | 世界区域内缩放 | 指针在 UI 上时滚面板不缩放 |
| 小地图左键/拖动、右键 | 平移镜头；右键对该点发 `MoveGround` | 与世界右键同一订单入口 |
| Shift + 上述右键、A 左键、Q/W/E/D | 订单带 `bQueue`，技能经 `ServerQueueCombatAction` 排队；不预输入、不按住重复；地面步骤显示小圈与虚线 | 排在当前订单与已排步骤之后，最多 `LDOrderRules::MaxQueuedSteps`（8）步 |
| Esc | 子确认/模态 → 准备态 → 主面板 → 菜单，每次一层 | 不 Stop、不发命令 |

已有准备态（Q/W/E/D 或 A）时右键只取消准备并消费这次点击，再次右键才走普通分派（`V05Pressed`）。
右键分派顺序为指针下可见单位 → 地面（`ULDNavigationComponent::ClickMove`），一次点击只产生一类订单，被拒绝不改发另一类。

### 能力目标

目标模式只由内容给出（`ELDDemoTargetMode`：`Self`、`Unit`、`DirectionProjectile`、`DirectionDash`、`GroundPoint`），
请求目标种类为 `ELDCombatTargetKind`：`Missing`、`Self`、`Unit`、`Direction`、`Point`。

| 目标模式 | 准备与确认 | 服务器处理 |
| --- | --- | --- |
| Self | 按键或 HUD 图标直接发送 | 目标固定为当前身体 |
| Unit | 左键取指针下可见单位（观察句柄 + ObservationEpoch） | 观察权限、生命、关系、距离与内容条件；失效不改选附近目标 |
| DirectionProjectile / DirectionDash | 左键取脚点到指针地面点的平面方向 | 有限非零方向；位移经 CMC root motion |
| GroundPoint | 左键取指针下地面点 | 释放时夹到技能射程 |
| 无法解析 | 无单位/地面或方向为零时以 `Missing` 发送 | 服务器明确拒绝，客户端不吞确认 |

- **快速施法**（`bQuickCast`，默认关）：Unit 技能指针下已有可见单位、方向技能指针下有地面时，按键即经 `ConfirmArmedCombat` 发送；否则照常准备。
- **施法后自动攻击**（`bAutoAttackAfterCast`，默认开）：不带 Shift 的 `CastUnit/CastPoint` 发出时按 `LDOrderRules::CastFollowUpTarget`
  记下跟进目标（单位技能取其目标；地面/方向技能取施法前服务器已激活的 `AttackUnit` 目标）。客户端按订单身份保留已发攻击的观察句柄，
  以 `ClientActive` 选正在执行的目标；排队或被拒请求不覆盖它。跟进目标在发 RPC 前登记（Listen Host 同步回执）。
  回执经 `CastFollowUpStep`：仍是最新本地订单、`Completed` 无失败、目标仍可见时发普通 `AttackUnit`；否则原地站立。
  Shift 施放、Shift 入队与新生命清掉待跟进目标；自动攻击被拒只更新状态行。攻击移动不在恢复范围。
- **预输入**（`LDCombatPreInput`）：自己 Q/W/E 前摇时再按 Q/W/E/D，只保留最新一次，前摇结束（到 `ActionReleaseAtMs` 或视图不再显示前摇）
  作为普通 Direct 请求发出（新序号、当前来源修订）。S、新生命/新局、输入被抑制或保留超过 1 s 丢弃（超时提示“前一个技能未结束，预输入已取消”）；
  `SourceRevision` 变化取消（提示“技能已变化，预输入已取消”）。普攻不需要预输入。
- 每个 Direct 请求携带 Generation/Life、单调 `DirectSequence`、入口、目标与 owner view 的 `ExpectedSourceRevision`。
  owner view 的生命或 `SourceRevision` 变化时撤销准备（`OnRep_CombatView`），HP/劲变化不撤销。按钮可用性与冷却只读 Combat owner view。
  能力确认越距由服务器拒绝，**不自动走近**；只有普攻、攻击移动与 `CastUnit/CastPoint` 订单负责接近。

### 指针反馈

`ALDPointerFeedback` 仅拥有客户端本地生成，不复制、无碰撞、不参与寻路。

- 地面订单点：绿色收缩虚线圈；A 地面点为同一圈的红色版（`ShowMove(..., bAttack=true)`）；本地 `PlanRoute` 失败显示红叉；服务器拒绝另以提示告知。
- `AttackUnit` 目标：红色收拢圈。悬停：右键此刻会攻击的单位脚下红色悬停圈（76 cm，近黑描边）。
- 选中：左键检视单位（`ReadInspected`）脚下不转动的浅色密点线圈（`SetSelected`，64 cm）；HUD 不画方框；失视野即消失。
- Shift 地面步骤：小圈（攻击移动为红色）与从身体经当前目的地到各点的虚线（`SetQueue`）；步骤开始或结束、Stop、新生命时移除。
- 光标：`ULDGameInstance::OnStart` 经 `LDCursor::Install` 把每种光标换成软件箭头；A 待确认为 `LDCursor::AttackAim`（红），技能待确认为 `CastAim`（蓝），其余普通箭头（`TickPointer`）。
- `ReadHovered` 返回悬停单位；`PointerPosition` / `GroundUnderPointer` 是点击、悬停与确认共用入口；`DevSetPointer` 仅 `WITH_DEV_AUTOMATION_TESTS` 生效。
- **拾取**（`ObservedUnderPointer`，hover、检查、右键攻击、确认与快速施法共用）：从屏幕反投影，拾取当前授权代理实际摆姿模型的三角面
  （头、脚、手臂均可选；遮挡视线的静态几何优先，重叠取最近表面）。无表面命中时退回宽松拾取：每个代理为显示脚底到头顶、
  半径 52.5 cm 的竖直胶囊，射线离它不超过 12 px（`LDModelPicking::PickTolerancePx`，按水平 FOV 与视口宽换算角度）可选，取角距离最小者；Sight 遮挡同样截断。
- 标记显示时长：地面/攻击 0.45/0.5 s，红叉 0.6 s（`LDPointerFeedback.h`）。`LD_COMBAT_INPUT` 日志区分预输入、已发送与服务器回执。

## 一个身体只有一套运动

普通移动由 CharacterMovement 执行并参与原生预测/校正。导航组件在运动前提供方向，不另写位置积分器，
不沿路径 SetActorLocation，也不把玩家临时交给另一个 AI Controller。运动上限、碰撞、斜坡和位移能力都经该身体的合法运动路径；
镜头平移或预览不改变人物位置、攻击距离或服务器判断。

| 身体角色 | 运动接线 |
| --- | --- |
| 拥有客户端 AutonomousProxy | 按回执本地规划并 `AddMovementInput`；saved move 携带 `FLDMoveIdentity`，服务器纠正后原生重放 |
| 服务器上的远程真人 | Motion 阶段由 `AdvanceAuthorityMotion` 供给转向，`AuthorizesMove` 只放行当前已接受且租约有效的身份 |
| Listen Host 自己 / 服务器 AI | 不预测，由服务器订单驱动 |

- `ConfigureDemoMovement` 从已验证内容快照读碰撞半径与基础移速（`UNIT.MoveSpeedCmPerSec` 经 Combat 投影为最大速度），沿用身体 half-height 并更新同一 CMC/NavAgent。
  导航系统在原生数据注册前安装同一 agent；生成器用带明确配置的创建入口，不能先注册默认 agent 再改半径。
- `MaxAcceleration` 与 `BrakingDecelerationWalking` 均为 8000（300 cm/s 约 38 ms 到全速），`bOrientRotationToMovement` 关闭，`SetFacing` 瞬时朝向移动方向。
- Demo 权威身体关闭 CMC 自身 component tick，由 Combat driver 发出的 `FLDAuthorityStep`/Motion permit 调用 `AdvanceAuthorityMovement`，
  沿原生 `ControlledCharacterMove/PerformMovement` 执行一次；同一 step/body 不能再积分。服务器 packed 处理保留原生时间戳验证及 ACK/error，
  其中 `MoveAutonomous` 不做第二次权威积分；`ServerShouldUseAuthoritativePosition` 禁止以客户端位置覆盖服务器结果。
- **营地静止跳过**：营地成员在原生落地、精确零速度/输入、walking 且静态支持面稳定时可跳过 `PerformMovement` 的积分与 floor sweep。
  待处理外力、launch、root motion、跳跃、穿透、强制 floor 检查、生命/胶囊/支持面或世界修订变化都走完整 CMC；跳过前用当前胶囊 blocking overlap
  排除外部身体新造成的穿透；刚停下先完成一次原生更新。跳过仍进入同一 native capture 与 `CompleteNativeMotion`，每 step/body 恰好提交一次静止批次；
  `AuthorityStationarySteps` 单独计数。参赛者不用该分支。
- **拥有者校正平滑**：校正立即改 CMC 胶囊并重放 saved moves；`FLDOwnerCorrectionSmoothing` 只保留显示位置差，按 `NetworkSimulatedSmoothLocationTime` 衰减，
  超 snap 距离直接到位。本人模型、脚下标记、头顶标签、射程圈与跟随镜头用同一显示位置；碰撞、距离验证与权威记录用真实胶囊。
- **提前起步**：远端拥有客户端的普通（非 Shift）地面订单发出当帧沿本地 `PlanRoute` 起步（`PredictSentGround`，`EarlyOrder`），不等 `Activated`。
  激活时目标相同则继续；更早订单此时才激活不拉回旧目标；订单运行前被拒或取消时，仍在服务器运行的已接受订单重新被预测，没有则停下等校正（Superseded 除外）。
  攻击、施法、攻击移动和 Shift 订单仍等激活回执。
- `ServerExceedsAllowablePositionError` 在原生判断出误差后再判：模式一致、无根运动、服务器身体或客户端 move 在行走时，误差不超过
  `LDOwnerPrediction::AcceptsWalkError`（最大速度 × 0.1 s + 10 cm）不发校正，计入 `ServerAcceptedWalkLeads`。服务器从不采用客户端位置。
- saved move 身份取身体正在走的订单（`LDOrderRules::PredictedMoveIdentity`）：提前起步的订单运行前结束时其 move 不再重放（Order 置 0，保留 Match/Life/Epoch）；
  其他新单未激活时不提前换；停止与无活动订单时保留当前 Local 身份，不退回全零；Stop 后、停止回执前发出的新单不沿用 Stop 前订单。
- **身体互相阻挡**：参赛者、营地野怪与 Boss 胶囊互相阻挡（`ECC_Pawn`）；D 冲刺只在自身扫掠期间忽略身体（`IsDashing`）。远程客户端上当前可见代理带同尺寸仅查询胶囊，
  只阻挡本人预测；Host 世界代理不加碰撞；迷雾中的身体只在服务器阻挡。点地目标已被一群身体占住时，碰到这群身体即算到达，不推挤到 `PathBlocked`
  （`LDPathSteering::BlockedAtHeldGoal`）。
- **位移**：D 冲刺与技能冲刺只在 Combat 成功释放后、击退只在 `CommitControl` 成功后经 `StartDemoDash` 写入原生 root-motion source，沿地面积分、遇阻停在最远合法位置；
  起步方向立即被静态几何挡住时 `CanStartDash` 拒绝。击退递增 `KnockbackSerial`，导航以 `KnockedBack` 放弃当前订单。定身只停自愿转向，不停重力与已提交 root motion；
  Root 期间冲刺由 Combat 拒绝且不付费。骨骼动画没有根运动。

### 原生运动记录

`LDCharacterMotion.cpp` 在 CMC 的原生 walking/falling、sweep、StepUp、接地和模式入口记录 `FLDNativeMotionBatch`（`LDNativeMotionTypes.h`）：
起止脚点、支持面、逐条 Movement/Adjustment/Support 记录及回退的试探次数。移动片段保留实际首末脚点和原生 hit-time 分数；
floor 高度调整不分摊为自愿移动时间；StepUp 失败撤销整个试探前缀；ledge 分支经原生 `GetLedgeMove/CheckFall` 见证 `RevertMove` 并核对恢复后的姿态/支持面；
`CurrentFloor` 与模式更新产生独立支持样本。未记录的位置写、缺时间区间、未解回滚或容量耗尽明确失败，不补造首末直线。

权威运动在 `BeginNativeMotionCapture/FinishNativeMotionCapture` 范围内运行。核心按 Body/stream/Step/世界修订接纳批次并检查序列、时间与空间连续；
每身体最多 256 个未消费批次、每批 128 条记录，超限背压或显式故障。精确前缀确认只释放已读记录。参赛者批次在 `CompleteNativeMotion` 前同步交给
`ALDGameMode::PublishCaptureMotion` 与喷漆/守护归约（`FLDCaptureReducer`），driver 在下一步开始时经 `ReleaseConsumedMotion` 释放；营地成员批次同样下一步释放。
native record sequence 与圈事件/提供者序列分开；`CompleteNativeMotion` 不是 Motion Fence 或喷漆完成证明。
核心步内阶段顺序为 Motion → Session → Actions → Effects → DeathsAndRewards → RespawnsAndWorld → Transactions → Capture → Close → Publish
（`LDCombatDriver.cpp`）；Actions 内先处理 Direct 动作，再推进订单。喷漆区判定与水位见[架构与网络](ARCHITECTURE_AND_NETWORKING.md)。

## 命令与取消

`ULDNavigationComponent` 是一个参赛身体当前订单的唯一写入者。订单种类（`ELDSpatialOrderKind`）为 `MoveGround`、`AttackUnit`、`AttackMove`、
`CastUnit`、`CastPoint`；Stop 是取消而不是订单种类。回执状态（`ELDOrderStatus`）为 `Queued`、`Rejected`、`Activated`、`Progress`、`Completed`、`Cancelled`
（`Expired` 只作为失败码）。没有历史订单栈。

**身份：** `FLDMoveIdentity{Match, Life, Order, Epoch}`（`uint32`），由拥有客户端 `NextLocalIdentity` 分配：每个新意图递增 Order，Stop 同时递增 Epoch；
用尽明确提示并停止下达，不回绕。拥有连接隐含在组件的 Controller 中。

1. **收件**：`ServerSubmitOrder` 与 AI 的 `SubmitOrder(FLDOrderCaller, Request)` 共用同一订单簿：核对调用者（核心当前 BodyRef/拥有 Controller）、身份与形状
   （`HasValidShape`）；同身份同条款重放原回执，异条款 `RequestConflict`；Order 不大于已收最大值或 Epoch 回退为 `StaleOrder`。通过后进入待受理槽并回 `Queued`。
   接收时间由核心按比赛原点生成 NotBefore 审计桶，迟到命令不能进入更早的运动区间。
2. **单一待受理槽**：新请求（含 Stop）替换旧待受理请求，旧的得到 `Cancelled`（`Superseded`/`Stopped`）。
3. **激活**（Actions 阶段 `ProcessAuthorityOrders(Permit)`）：非 Stop 订单受 0.1 s 激活节流（`FLDOrderBook::ActivationDue`），Stop 不受。
   攻击类先解除保护再做完整准入；准入失败回 `Rejected` 且原订单继续；成功则原订单 `Cancelled/Superseded`、新订单 `Activated`。
4. **运动**（Motion 阶段 `AdvanceAuthorityMotion`）：转向输入交给 CMC；路径 2 s 无进展先重算一次，再无进展以 `PathBlocked` 结束。
5. **心跳租约**：真人订单需拥有客户端每 0.5 s 发 `ServerHeartbeat`（不可靠，序号递增）；服务器按 `LDCommandLiveness` 的 2 s 窗口判租约，过期 `Expired`。
   心跳只刷新当前已激活身份，不创建或恢复订单。
6. **回执**：`ClientOrderReceipt` 只接受当前身份；客户端按 `ReceiptSerial` 与当前 Match/Life 过滤。回执窗口最多 16 条，当前与待受理身份永不被逐出。
7. **Stop** 提升 Order 与 Epoch，清本地预测；服务器在 Actions 阶段停止，菜单回调不直接改权威速度。
8. **新生命**：`ResetLife` 不倒退序列号；新的 Owner 生命投影先发布再清理导航，不发伪装成用户意图的 Stop。死亡、离开、终局由 `EndLifeOrders` 结束全部订单。
9. **订单簿** `FLDOrderBook` 绑定一个（match, life）：请求水位、幂等窗口与激活节流只对该局权威时钟和该命有效；新身份换绑时整体重建；
   MatchReset 经 `ResetMatch` 结束旧局订单并丢弃订单簿与内容缓存。回执序号是拥有连接的单调序，不随局复位。
10. **断线交接**保留排队、待执行及被取代的进攻意图所欠的保护解除；新控制器在下一次 Actions 阶段按原 Match/Life/Body 结清。
11. **提示**：订单进度只写 `ULDNavigationComponent::Status`；只有真实失败与“复活保护没了”经 `PostIntentNotice` 进入 HUD 单行提示，
    被自己新订单替换或按 S 不提示；同一失败再发生会再次提示。

**路径终点：** 起点总经 `PlanStart`；点击地面按 `EGoal::Ground` 严格规划，追击身体脚点按 `EGoal::Body`（见[路径与物理边界](#路径与物理边界)）。
本地预测：非权威拥有客户端收到 `Activated/Progress` 后自行规划同一路线；预测的 `MoveGround` 路线失败时客户端自行发 Stop 并提示。

### 普攻与攻击移动

**AttackUnit：** 激活时以 `NewAttackOrder` 目的重查观察句柄（`ALDVisionDirector::ResolveObservedUnit`），不能解析或指向自己即 `TargetUnavailable`，
不回退到地面或附近单位；成功后 `BindObservedOrder`。射程每次取 Combat 当前 `AttackRangeCm`（`ReadCombatBody`）。每个 Actions 阶段以 `AttackStart` 目的重查：

- 射程内且普攻就绪时，以 Nav 形 `FLDCombatRequestKey`（订单身份 + 递增 `AttackCycle`）调用 `ALDCombatDirector::StartNavAttack`；Combat 分配动作身份。
  Combat 以 `OutOfRange/Blocked` 拒绝时订单贴近并重算。
- 射程外追击：目标移动 ≥ 50 cm 且距上次规划 ≥ 0.1 s 时重算；从交战转回追击时当即重算。
- 只有仍在前摇或射程内等待时订单保持“交战”，客户端停止本地转向；本阶段到点出手的普攻不再占住身体（`LDOrderRules::ActionHoldsChase`、`FLDCombatBodyRead::ActionReleaseAtMs`）。
- 射程内等节拍时，目标沿两者连线后退 ≥ 1 cm 即继续贴近，向自己走来才停下；样本跳变超过 200 cm 不算走开（`LDOrderRules::ChasePress`）。贴近期间回执接近半径为 0。
- 对同一观察目标重下 AttackUnit 时经 `AdoptNavAttack` 接管尚未出手的普攻；其他新订单、S 与订单结束取消它，已承诺节拍不退回。
- 订单只锁一个主目标；群攻与副击由 Combat/内容处理。

**AttackMove：** 唯一目的地是确认时的地面点。没有锁定目标时，每个 Actions 阶段从 `EnumerateVisibleUnits(..., AttackMoveCandidate)` 中按脚点平面距离选最近者
（`LDOrderRules::NearestCandidate`，同距取较小观察句柄，排除自己），半径 `TARGET.AttackMoveAcquireRadiusCm`。选中后按 AttackUnit 交战直到目标死亡、失效或订单替换；
授权观察到的死亡（`FindObservedTermination`）后重新搜敌或续行；无目标到达即 `Completed`。

**失去视野**：AttackUnit 与已锁定目标的 AttackMove 都以 `Cancelled/TargetUnavailable` 结束，不走向最后可见位置、不恢复原目的地，前摇中的普攻一并取消。
只有授权观察到的死亡让 AttackUnit `Completed/TargetDied`；隐藏、过期不被当作死亡。

**嘲讽**（Combat `ReadTaunter`）：`ProcessAuthorityOrders` 按 `LDOrderRules::ForcedOrderStep` 以 `Taunted` 取消当前订单、拒绝待处理订单（含 Stop），
改为服务器强制追击（Order 为 0、不进订单簿、无租约、不发回执），只经本观察者当前区间的嘲讽者句柄（`FindObservedHandle` + `ResolveObservedUnit`）接近并普攻；
尚未进入本观察者帧时原地等待。嘲讽结束交还控制，不恢复被替换订单；强制追击自行结束时 0.1 s 后重建（`FForcedRetry` 按比赛时钟，`ResetMatch` 清零）。
对手 AI 被嘲讽时暂停身体意图（`LDOpponentRequests::BodyIntentsHeld`），被打断的订单按 Dropped 结算。

### 进攻意图解除保护

发出进攻意图即解除复活保护，目标无效、距离不足、没蓝、冷却未就绪等失败也不保留保护；准备/取消、MoveGround、Stop、D 冲刺、检查与镜头不是进攻意图。

- `AttackUnit/AttackMove`：激活时先验证调用者、身份与写许可，再调 `ALDGameMode::EndProtectionForAggressiveIntent`，之后才准入；`Rejected` 回执仍带
  `bProtectionRemoved` 与 `LifeRevision`。待受理时被替换的攻击类请求在下一次激活时补做。缺省目的地的攻击移动照样解除。重复请求只重放回执。
- Q/W/E：Combat 处理 Direct 请求时以同一入口解除（`LDCombatActions.cpp`），空来源或缺省目标也算。
- Shift 队列里的攻击订单与 Q/W/E 在收件后的下一次 Actions 阶段即解除（`SettleQueuedAggression`，每生命只记最早一条）。

### Shift 指令队列

队列由服务器 `ULDNavigationComponent` 保存：空间步骤轮到时成为普通待受理请求，技能步骤轮到时调用 `ALDCombatDirector::SubmitAction`。

- **收件**：`bQueue` 订单与 `QueueAction`（`ServerQueueCombatAction`）的技能校验调用者、形状与新鲜性后追加到 `Queue`（`FQueuedStep`），回 `Queued`。
  技能 `DirectSequence` 必须未被 Combat 见过（`QueryActionReceipt` 为 `StaleRequest`）且大于已排和执行中的序号。满 8 步时订单回 `QueueFull`、技能回 `CapacityUnavailable`。
- **出队**（`AdvanceQueue`，Actions 阶段在激活之前）：无待受理、当前订单与执行中排队技能时（`QueueHeadDue`）取队首；技能步骤记 `RunningQueuedAction`，
  等回执 `Released/Finished/Rejected`（`QueuedActionSettled`）。方向技能出手时从当前脚点重新求方向（`QueuedAimDirection`）。
- **结束一步**（`LDOrderRules::QueueAfter`）：到达、路线不通、目标不可见/死亡、击退跳到下一步；Stop、死亡、租约过期、旧生命/旧局、非拥有者、嘲讽清空队列。
  后面还有地面步骤时到达不调用 `HaltLocomotion`。
- **清空**：不带 Shift 的订单、S、`EndLifeOrders`/`ResetMatch`、嘲讽调 `ClearQueue`：空间步骤回 `Cancelled`，技能步骤回 Combat 拒绝回执
  （`Cancelled/Dead/MatchClosed/ControlBlocked`）。不带 Shift 的技能只在**被受理后**清掉它之前排的步骤（`ClearQueue(Failure, BeforeStep)`，按 `FQueuedStep::Serial` 截断），
  且不改当前订单：Direct 技能在 Combat 判定后（`AwaitDirectVerdict`/`SettleDirectVerdicts`），`CastUnit/CastPoint` 在激活准入成功时（`FPendingOrder::QueueCut`）。
- **客户端**：`IsQueueModifierDown` 读左右 Shift（固定键，不可绑给动作）；`FQueuedMark` 记自己排队的地面步骤。

### 接近后施放与拒绝不打断

- **CastUnit**：对单位 Q/W/E 发一条订单替换当前订单（Shift 时入队），经 `ServerSubmitCast`/`SubmitCast` 与 Direct 请求一同收件。Direct 必须非 D、`Unit` 目标、
  与订单观察句柄一致、序号未被 Combat 见过。不带 Shift 时收件即 `CastReady` 判定，未就绪回 `Rejected/CastRefused` 并发 Combat 拒绝回执，不占待受理槽、
  不动当前订单与队列，仍在下一次 Actions 阶段解除保护（`QueueOwedAggression`）。
  激活（`AdmitOrder`）时 `QueryAvailability` 再判：冷却、劲、受控、空槽、非单位技能立即 `CastRefused`；`Busy` 只延后。施法距离为 `RangeCm - CastMarginCm`（10 cm）。
  推进（`AdvanceCast`）：距离外与普攻共用 `ChaseTarget`；进入距离调 `StartNavCast`（按普通 Direct 判定）：`OutOfRange/Blocked` 继续贴近，`Busy` 等待，其余记录并结束订单
  （成功 `Completed`，否则 `CastRefused`）。目标失视野或死亡同 AttackUnit。
- **CastPoint**：`GroundPoint` 与 `DirectionProjectile` 确认在地面点时发出（`GroundCm`），判定同上（模式须与瞄准相符）；距离外按身体目标规划行走，进入距离即停下
  （`AdvanceCastPoint`），以 `Point` 或从当前脚点重求的 `Direction` 调 `StartNavCast`；只有 `Busy` 等待，到路线终点仍不在距离内以 `NoCompletePath` 结束。
  `DirectionDash` 与 D 没有施法距离。对手 AI 直接调 `SubmitAction`，不用 `CastUnit/CastPoint`。
- **拒绝不打断**：Q/W/E/D 任何准入失败不取消、不替换、不暂停当前订单，也不清队列。就绪判定共用 `LDOrderRules::SkillRequestReady`（`QueryAvailability` 为 `None` 或 `Busy`，
  服务器按权威时间现算，不读 owner view 的显示用 `Unavailable`）。队列非空时 Direct 技能先经 `RefuseUnreadyDirect`：未就绪的不交给 Combat，记入 Combat 的 Direct 回执窗口
  但不推进序号高水位（重放得原拒绝，条款不同 `RequestConflict`）。交给 Combat 后被拒时高水位已越过排队步骤，排队步骤仍以 `bQueuedStep` 提交、只执行一次；
  与排队步骤同序号的 Direct 以 `RequestConflict` 拒绝（`LDCombatSourceRules::DirectSequenceVerdict`）。客户端发施放订单不提前清排队标记，确认技能不发 Stop。
  已受理后才失败的施放结束订单并原地站立，不触发施法后自动攻击。

### 接口表

| 状态 | 唯一写入者 | 消费者 |
| --- | --- | --- |
| 当前订单与待受理槽（`FActiveOrder`/`FPendingOrder`）、Shift 队列（`Queue`/`RunningQueuedAction`） | 服务器 `ULDNavigationComponent` | 拥有者经 `ClientOrderReceipt`；AI 经 `QueryOrder` |
| `FLDOrderBook` 与回执窗口（16 条） | 同上，绑定一局一生命 | 拒旧与幂等重放 |
| 本地身份、预测路线、`Status` | 拥有客户端 `ULDNavigationComponent` | HUD、指针反馈 |
| 准备态（`ArmedEntry`/`bAttackMoveArmed`）、预输入槽、检查对象 | `ALDPlayerController` | HUD、光标；不复制 |
| 绑定与本地偏好 | `ULDControlSettings` | 控制器、Navigation、HUD、设置页 |
| 动作、资源、节拍、来源可用性 | `ALDCombatDirector` | Navigation 只读 `ReadCombatBody` 并申请普攻 |

| 类型/签名 | 意义 |
| --- | --- |
| `FLDSpatialOrderRequest` | `Identity`、`Kind`、`bQueue`；MoveGround/AttackMove 用 `GroundCm` + `SurfaceHint`，AttackUnit 用 `Observed` + `ObservationEpoch`；`HasValidShape` 要求其余字段默认；`SameTerms` 判重放；`IsAggressive`。不传路径点或 Actor |
| `FLDOrderReceipt` | `Identity`、`Kind`、`Status`、`Failure`、`ReceiptSerial`、可选公共 `GoalCm`/`AcceptanceCm`、`bEngaged`、`bProtectionRemoved`、`LifeRevision`；只发给拥有者，不含隐藏坐标 |
| `FLDOrderCaller`（仅服务器） | `FLDMatchContext Match`、`FLDCombatBodyRef Body`、`Controller` |
| `ELDOrderCancelReason` | `Stop/FocusLost/LifeEnded/Left/MatchClosed` |
| `SubmitOrder` / `CancelOrder` / `SubmitCast` / `QueryOrder` | 收件（只校验、去重、排队，不激活、不解除保护）/ Stop（Order 与 Epoch 都须前进）/ 施放收件（普通 `SubmitOrder` 拒绝 Cast 种类）/ 只读窗口回执，逐出的旧身份为 `Expired` |
| `QueueAction` / `ClearQueue(Failure, BeforeStep)` / `RefuseUnreadyDirect` / `AwaitDirectVerdict` | Shift 技能收件 / 清未开始步骤 / 队列非空时的 Direct 预判 / 受理后清之前步骤 |
| `ServerSubmitOrder` / `ServerStop` / `ServerSubmitCast` / `ServerQueueCombatAction`（可靠）、`ServerHeartbeat`（不可靠）、`ClientOrderReceipt`（可靠） | 只解出调用者并转交 |
| `FLDCombatActionRequest` / `FLDCombatCancelRequest` | Direct 请求与按 `ThroughDirectSequence` 的取消，经 `ServerSubmitCombatAction/ServerCancelCombatAction` 到 Combat |
| `FLDCombatRequestKey` | Combat 判别键：`Direct` 由 DirectSequence 构成，`Nav` 由订单身份 + AttackCycle 构成；两域互不比较 |

`ELDOrderFailure`：`None`、`InvalidRequest`、`NotOwner`、`StaleMatch`、`StaleLife`、`StaleOrder`、`RequestConflict`、`SequenceExhausted`、`InvalidGround`、
`NoCompletePath`、`PathBlocked`、`TargetUnavailable`、`TargetDied`、`Dead`、`MatchEnded`、`Superseded`、`Stopped`、`Expired`、`Taunted`、`KnockedBack`、`QueueFull`、
`CastRefused`、`NotStarted`（休息返回倒计时）；中文提示在 `LDNavigationComponent.cpp` 的 `FailureText`。句柄不可用统一为 `TargetUnavailable`，不泄露原因。
Combat 的资源/冷却/来源拒绝沿 Combat 回执呈现，操作层不复制校验；AI 读同一失败码决定重试。

### 生命周期矩阵

| 事件 | 本地输入/镜头 | 权威订单/动作 |
| --- | --- | --- |
| 合法新订单 / 无效新订单 | 新标记 / 失败提示 | 激活时原子替换原订单 / 原订单继续，攻击类仍已解除保护 |
| 死亡 / 新生命 | `ResetLife` 清准备、指针与路线 | `EndLifeOrders`；旧生命请求 `StaleLife` |
| 目标失视野 / 授权死亡 | 悬停圈与攻击标记消失 | `TargetUnavailable` 结束 / AttackUnit `TargetDied`，AttackMove 续行 |
| 来源变化 | 撤销准备 | Combat 按 `ExpectedSourceRevision` 重验 |
| 失焦 | 清按键，Stop，停心跳与指针 | 当前订单结束 |
| 菜单/面板/子确认 | 只阻止新世界命令 | 订单与心跳继续 |
| 心跳中断 / 离开 | — | `Expired` / `Left` |
| 终局 / 新局 | 关闭世界输入 | `ResetMatch` 重建订单簿；旧局请求 `StaleMatch` |

## 路径与物理边界

接近目标要找碰撞外的合法位置。不可走、层高歧义、无完整路径或长期无进展都明确失败，不把 partial path 当到达，不无限重算，不穿过已生效阻挡。

服务器进展与重算在 `AdvanceAuthorityMotion/ProcessAuthorityOrders`；`TickComponent` 只承担拥有者预测、心跳和指针，镜头在本地相机求值前更新。

| 世界接口 | 用途 |
| --- | --- |
| `ULDMapRegistry::QueryGround` | 取身体原生 `CurrentFloor` 并映射到注册地面，不用订单 hint；无合法 floor 为 `NotGrounded` |
| `ULDMapRegistry::QueryCompletePath`（经 `LDPathSteering::PlanRoute`） | 地面与追击路线；partial path 明确失败 |
| `ALDVisionDirector::ResolveObservedUnit` / `EnumerateVisibleUnits` / `BindObservedOrder` / `FindObservedTermination` | 观察重查、A 候选帧、订单绑定、授权死亡 |

- Demo 只有一个注册地面，`GroundSurfaceHint()` 返回其 SurfaceId；hint 不能证明合法落地，无支持面时不借 hint 向下补接地。
- 注册地面检查以胶囊局部向下 sweep 核对真实支持点的作者高度与法线，最低到脚点下一个 `MAX_FLOOR_DIST` 间隙；接触法线可在作者最大坡度内变化（坡顶/坡脚），
  不借向下兜底或更陡支撑面；坡道下沿允许胶囊由脚点旁坡面支撑。路线投影保留脚点 XY 与该处作者高度。
- 出生预约、营地身体和训练假人用物理 sweep 首次接触时的胶囊中心加原生贴地间隙，并在该原点重查静态障碍。
- **起点** `LDPathSteering::PlanStart`（参赛者经 `QueryPlanStart`/`PlanRoute`，营地成员复用）：身体停在 Recast 收缩边缘外时，只把平面起点移到一个胶囊半径内最近的
  navmesh 点，该点须在同一支持面重新通过 `QueryGround`；更远则由完整路径查询拒绝。
- **终点**（`EGoal`）：玩家点选的地面（MoveGround、AttackMove、按住右键）先按点击处严格规划；点击处不可达（墙、箱体、贴障碍、不连通）时在身体所在高度投影到
  最近 navmesh 点，以允许 partial 的查询限定在起点连通区域，按离点击点距离逐个经 `QueryGround` 与完整路径查询验证后作为终点；落点标记仍显示在点击处，续行沿用投影终点。
  世界修订过期、缺支持面或胶囊不符不投影。被追击身体的脚点同样经 `PlanStart` 落到 navmesh。注册表拒绝原因写明调整的是起点还是终点。
- 动态目标必须当前获准可见；寻路预览、自动追踪或避障反馈不能泄露隐藏单位。公共休息室配装与结算配装阶段，地面移动绕开观察帧里的可见角色与全部训练目标
  （拥有客户端用本人观察帧预测同一转向）；其他阶段不绕行，观察帧不可用时不读隐藏身体。
- 环境碰撞更新与路径重算见[世界与数据管线](WORLD_AND_DATA_PIPELINE.md)。

## 输入所有权

输入依次由文本框、模态、拖拽/捕获、面板、待确认动作、小地图和世界处理；同一手势只有一个消费者，按下时的所有权维持到释放；
打开窗口的点击不落到新窗口按钮，关闭面板不重放仍按住的输入。

`CanWorldCommand() = IsWorldLifeValid() && !IsWorldInputSuppressed()`（`LDPlayerInput.cpp`）：

- `IsWorldLifeValid`：`PublicMatch`（唯一代次/阶段来源）的代次等于本人 Core 生命投影的 Match，阶段为 Rest/Development/CaptureOpen（返回倒计时内命令照常发出，
  由服务器以 `NotStarted` 拒绝），本人 Alive 且持有身体。失去它是唯一会让 Navigation 结束已接受订单的本地条件。
- `IsWorldInputSuppressed`：菜单/设置打开（`SetMenuOpen`）、文本编辑或子确认（`IsWorldInputCaptured`），以及菜单、焦点或映射变化后仍未松开的键（fresh-press）。
  只拦新世界命令（订单、Q/W/E/D、A、S、世界与小地图点击、镜头键与缩放；HUD 图标经 `ActivateCombatEntry` 同样被拦）；已接受订单继续移动并发心跳。
  这些变化只经 `ReleaseLocalInput` 取消准备态与回中锁存；子确认在准备态之上打开时控制器下一帧前撤销准备态。
- 鼠标点击、悬停、缩放与边缘平移另查 `ULDUIRoot::PointerOverUI()`。小地图拖动经 Slate 鼠标捕获保持到松开，期间关闭边缘平移。
- **失焦**：操作系统失焦时 `FlushPressedKeys` 并由 Navigation 发一次 Stop，失焦期间不发心跳、不处理指针；回焦需新手势，不自动续走。
- **死亡**：等待复活期间只有静态地图镜头（`IsAwaitingRespawn`、`CanCameraCommand`）；P 仍可切换；三选一走成长域入口，不经世界命令门槛。暂停菜单不暂停共享世界。

`ULDUIRoot` 是唯一本地界面层来源：`TopInputLayer()` 报告文本编辑、子确认或主面板；同一时刻只开一个主面板（`ToggleMainPanel`，经 `RegisterMainPanel` 登记），
未登记的面板明确提示尚未接入。**Esc**（`V05Back`）每次只处理一层：`CloseTopModal` → `CancelArmedCombat` → `CloseTopPanel` → 打开/关闭菜单；不 Stop、不发命令或候选提交。

**映射**：`RebuildV05Mappings` 按 `Definitions()` 在运行时为每个动作建 `UInputAction`，放入一个动态 `UInputMappingContext`（优先级 10，
`bIgnoreAllPressedKeysUntilRelease`），绑定 Started/Completed/Canceled 到 `V05Pressed/V05Released`，滚轮到 `V05Zoom`。重建前移除旧句柄与旧 context，
要求所有键松开后才接受新世界命令；不停止已接受订单。仓库没有静态 IA/IMC 资产。

动作集合：`Select`、`Navigate`、`AttackMove`、`Skill1`、`Skill2`、`SkillE`、`Dash`、`Stop`、`Attributes`、`Details`、`Workshop`、`Scoreboard`、`Mark`、
`PanUp/PanDown/PanLeft/PanRight`、`Recenter`、`Follow`、`Zoom`、`Back`（Esc 保留）。没有 profile/registry 参数、第二份默认表或旧 Sprint/快捷位/多技能槽/Help/Inventory 动作。
改按键要同步 `LDControlSettings`、控制器路由、帮助文本、HUD 标记与输入检查；消费者不能查询已退役动作名而静默得到无效键。重绑冲突明确拒绝，不静默交换。

## 相机与小地图

固定俯视斜角，可跟随、回中、平移和缩放，不旋转。`Character\LDCameraSettings.h` 单点定义默认臂长 2250 cm、范围 1300–2400 cm、滚轮步长 80 cm、
pitch −60°、yaw −90°（世界 −Y 在屏幕上方）、FOV 55°、目标偏移 Z=20 cm；身体、等待相机、设置与美术镜头检查共用，不改变授权视野。
缩放结果（`ALDCharacter::ZoomedArmLength`）保存为本地 `Zoom`；没有可调的步长或范围设置。

- `PanCamera`、`Recenter`、`ToggleFollow`、`PanTo` 与 `UpdateLocalCamera` 共享镜头状态；手动平移转为自由镜头。镜臂位置与旋转用世界绝对坐标，转身不旋转镜头中心。
- 平移速度为本地 `CameraPanCmPerSec`；边缘带四边各 8 px，在失焦、指针在 UI 上、小地图拖动和 Q/W/E/D/A 准备态时关闭。
  键盘与边缘平移最多消费当前帧 50 ms 输入，长帧不被解释成整段平移；人物运动、权威时钟与网络超时不采用此限幅。
- 镜头中心夹在已注册地面的静态边界内，不用动态单位决定夹限。
- `UpdateCameraManager` 在本帧运动/校正重放后先更新本人显示偏移再更新镜头中心；`CalcCamera` 当次计算镜臂几何，不读较早缓存的 socket。
  本地控制器在相机求值前显式绑定实际 Pawn；Pawn 复制暂缺时保留最后渲染的相机姿态，Pawn 到达后立即重新绑定，迟到的引擎视点复位不能让它退回控制器原点。
- 仅 DEV 美术截图探针可临时接管本地相机，释放或截图相机销毁后恢复正常选择。
- **死亡镜头**（`IsAwaitingRespawn`，输入门 `CanCameraCommand`）：`UpdateLocalCamera` 驱动等待镜头，按 `ALDCharacter::DefaultArmCameraPose` 摆放，从这一生最后的镜头中心起步，
  可键盘/边缘/小地图平移与缩放（直接改写已应用的缩放偏好，新身体沿用），夹在静态地图边界内，不跟随任何单位、不获得视野；回中与跟随提示“无存活角色”。
  复活后由新身体接回并恢复已保存的跟随偏好。
- **DEV 测试镜头**（`LDPlayerDevTest.cpp`）：房主局内 Ctrl+回车启用房间测试模式，普通测试成员同快捷键打开面板（UMG 预览键盘处理优先消费此组合）。
  T 创建本地 `ACameraActor`，暂停普通镜头与新指令，WASD/Q/E/右键/Shift 飞行；再按 T 经 DEV 请求流提交 `TeleportToCamera`：服务器在 Transactions 内按真实 Pawn 碰撞扫描镜头下方支撑
  与胶囊净空；拒绝保留镜头，成功回到角色视角；换命、换局和退出清理。传送保留运动流与序号，只更新姿态，并在当前边界直接提交旗杆进出事实，不把瞬移路线算作经过区域。

小地图坐标转换使用当前地图定义与本地 `MinimapOrientation`：跟随镜头（默认）、跟随角色朝向、固定正北（世界 −Y 朝上）。三种都是真实俯视的旋转加统一缩放，
绘制、镜头框和拖图/右键移动共用 `LDMinimap::MakeView/ToFrame/ToWorld`，不镜像。点击静态地图不授予锁定或远程交互；左键拖动只移镜头（死亡等待可用），右键移动要求 `CanWorldCommand`。

## 本地设置与显示

`ULDGameInstance::GetLocalControlSettings()` 持有应用中的本地偏好，Controller 引用同一对象。本地偏好只影响本机呈现，不和对局经验、冷却等混为一份状态，
不影响世界距离、视野或角色速度。设置页的布局与交互见 [App Spec](../APP_SPEC.md) §7 与[视野与 UI](VISION_AND_UI.md#设置页)。

| 字段 | 默认 | 合法域 / 恢复组 |
| --- | --- | --- |
| `Bindings` | `Definitions()` 默认键 | 无冲突，Esc 保留给 Back，Shift 不可绑 / Controls |
| `bQuickCast`、`bAutoAttackAfterCast` | 关、开 | — / Controls |
| `Zoom`（cm） | 2250 | 1300–2400 / Camera |
| `bEdgePan`、`bStartFollowing` | 开、开 | — / Camera |
| `CameraPanCmPerSec` | 1800 | 900–3600，设置页 300 步进 / Camera |
| `TextScale` | 1 | 1–1.25 / Interface |
| `MinimapOrientation` | `Camera` | `Camera/Facing/North`，越界 `Corrupt` / Interface |
| `bShowNetworkDiagnostics`、`bShowFPS`、`bReduceCombatEffects`、`bCameraShake` | 关、关、关、开 | — / Interface |
| `MasterVolume`、`MusicVolume` | 1、0.6 | 0–1，越界或非有限 `Corrupt` / Audio |
| `PlayerName` | 空（尚未设置，不填默认名） | 见下，不属于任何恢复组 |

- 文件为有限字段正向清单序列化（非反射），`CurrentSchema` 为 23，与产品/网络版本无关。
- `LoadLocalSettings` 区分 `Loaded`、`Migrated`、`Missing`、`UnsupportedSchema`、`Corrupt`、`ReadFailed`、`InvalidStorage`。缺文件用当前默认值但不称已保存；
  损坏或不支持的文件保持原样，菜单与设置页提示显式恢复。只有绑定不合法而其他偏好合法时，恢复保留这些偏好，当前绑定仅本会话有效；确认替换之前通用 flush 不覆盖原文件。
  `Key`/`Rebind` 只接受注册表动作，已退役动作名得到无效键并被逐名指出。
- **迁移**：schema 5–22 显式迁移，返回 `Migrated`，处于 `ConfirmedApplied` 而非 `PersistenceVerified`，立即生效，下一次核验保存写成当前 schema；加载本身不改写旧文件。
  缺少的偏好取默认（名称空，并提示先设置名称再建房或加入）。各 schema 新增字段：6 名称、7 主音量、8 平移速度、9 小地图朝向、10 快速施法、15 两个诊断开关、
  18 施法后自动攻击、20 音乐音量、22 战斗反馈两项。缺少的动作在默认键空闲时补上并在提示中说明：Dash（≤10）、Scoreboard（≤13）、Mark（≤15）、SkillE（≤18）、
  Details（≤20）、Workshop（≤22）；Details 与 Workshop 的默认键被占时改用空闲备用键，其他动作默认键被占即按 `Corrupt` 恢复，不抢占原绑定。
  schema ≤16 先移除已退役的 `Item1`/`Item2`。Zoom 先按旧范围校验（5–11 为 1100–1700，12 为 1100–1900，13 起为当前范围），在当前范围内原样保留，
  低于 1300 调为 1300 并提示，超出各自旧范围按损坏拒绝。迁移中损坏、非法偏好或注册表外动作按 `Corrupt` 处理。
- schema 4 是已退役多注册表格式：只校验完整性并列出退役动作，返回 `UnsupportedSchema`，不加载任何旧绑定；合法镜头/文字偏好走恢复，原文件在确认替换前不变。
  当前 schema 含注册表外动作同样拒绝。旧通用 SaveGame 格式与不支持的 schema 不静默覆盖。
- **名称**：`SetAppliedPlayerName` 修改已应用对象并经 `RequestLocalSettingsFlush()` 保存，草稿不能改名。`ValidatePlayerName`：非空、首尾无空白、最多 16 字符（代理对算一个）、
  无控制字符或残缺代理、不含 `? = # / \ : % & " ' < > |`（经 `?LDName=` 传递）；空名可保存但建房/加入前必须设置；服务器会话域以同一规则再校验。文件中非空非法名称使整个文件 `Corrupt` 且不提供恢复。
- **恢复默认**：`ResetPreferenceGroup` 按 Audio/Controls/Camera/Interface 只还原一组并立即应用、保存，四组合起来即 `ResetLocalDefaults`；名称不被恢复默认删除。
  “采用推荐镜头”只改 Zoom（`UseRecommendedCameraZoom`）。画面页的显示/画质取产品默认（临时 `UGameUserSettings::SetToDefaults`，不是 `ResetToCurrentSettings`），与当前相同时不应用。
- **应用对象**：`SetAppliedMasterVolume`/`SetAppliedMusicVolume`/`SetAppliedCameraPanSpeed` 等直接改写已应用对象并请求延迟保存；GameInstance 每次加载或应用后把音量交给
  `ULDAudioSubsystem`（生效链见[视野与 UI](VISION_AND_UI.md#音效与音量)）；导航经 `GetCameraPanCmPerSec()` 读平移速度。最后一次实际 zoom/follow 在 GameInstance 标记 dirty，
  旅行/退出不等延迟 tick 才 flush；Esc、强制关页、Widget 销毁、网络失败、旅行与退出前保存已确认值。
- **写盘**：先验证字段，写同目录临时文件并回读，Windows 原子同卷替换且不预删目标，再次回读；替换前 flush 数据，失败保留原已核验文件只清理候选。
  元数据只有明确不存在才算 `Missing`，权限/共享/I/O 错误进 `ReadFailed`。只在核验后清 dirty；`SaveLocalSettings` 返回类型化结果，`EditingDraft` 不可保存，写失败保留已应用值与重试入口。
- `FLDLocalSettingsStorage::User()` 只选 `Saved/SaveGames/LockdownLocalControlsV1.sav`；已退役的 V07/V08 槽文件不读也不删。测试必须用 `Automation(FGuid)`
  （忽略的 `Artifacts/Automation/LocalSettings`），无效夹具不回落到用户文件；无人值守/commandlet 未指定夹具时禁止用户文件写入。

**显示**只由引擎 `UGameUserSettings` 管理。游戏只有无边框全屏、桌面分辨率：`ULDGameInstance::Init` 在 `UGameEngine::Init` 建窗口前经 `LDLocalSettings::HoldBorderlessDesktop`
把旧存档改为无边框桌面分辨率，再 `ApplyResolutionSettings(true)`；PIE 窗口归编辑器。`-windowed`、`-ResX/-ResY` 仍作用于本次请求（自动化与同机多开），存档保持无边框；
`DefaultInput.ini` 关闭 Alt+Enter/F11。设置页没有窗口模式、分辨率或试用回滚。

- 显示草稿只有 VSync 与整体画质（“画面”页），草稿不写配置；与已核验生效值不同才算未应用（应用失败后的请求值不算生效值）。画面页底部应用栏列出未应用项，
  “应用”经 `ApplyDisplaySettings` 立即应用并保存，“撤销更改”丢弃草稿。被拒绝的同一份草稿在修改前不能再应用，提示同时说明可撤销。
- 默认画质的 `ResolutionQuality` 为 0（沿用项目 `r.ScreenPercentage`）；每次应用画质后 `LDLocalSettings::ApplyRenderScale` 撤销 Scalability 优先级的旧比例，无需重启。
- `SaveSettings` 的 void 返回不代表成功：核验从实际配置文件读入新的 `FConfigFile`，不读 GConfig 缓存。UE 5.8 的 `GGameUserSettingsIni` 等是缓存标识而非物理路径，
  `ResolveConfigDestination` 从 `FConfigBranch::IniPath` 取真实目标，缺失即失败；NoSave、mutator 和 Flush 仍用原标识。Editor 的独立质量配置须在原生保存阶段显式 flush 后核对
  （`Scalability::SaveState` 只暂存）。已有 `NoSave` 不被解除；VSync/画质应用失败与持久化未核验分开反馈，失败保留 dirty 与重试。
- **关闭**：`ForceClose` 用于终局或面板切换；普通 `Close` 在有未应用草稿时先切到画面页显示“应用并返回 / 放弃更改并返回 / 继续编辑”，确认卡显示时再按“返回”或 Esc 即放弃草稿；
  “应用并返回”失败留在确认卡并写明原因，焦点移到“放弃更改并返回”。`Close`/`ClosePanel` 只在真正关闭时返回 true；`HasChildConfirmation`/`CloseChildConfirmation` 先结束按键捕获，
  其他确认按“保持不变”关闭。按键捕获在绑定行内进行，被拒键在行内说明且捕获继续，Esc 只结束捕获；切换页签也按“保持不变”结束捕获与确认。
- 设置页控件（页签 `Tab*`、按键框 `Bind<Action>`）只构造一次并带稳定控件名，自动化按名称查找；隐藏页折叠且不在焦点顺序里。

## 维护检查

经 `Scripts\RunTests.ps1`（`Lockdown.Rules`）：

| 用例 | 覆盖 |
| --- | --- |
| `DemoControlFeel.HeldMove` / `PreInput` / `SourceBuffer` | 按住右键节流；预输入前摇判定；来源或局变化取消旧目标 |
| `DemoControlFeel.ModelPicking` / `PresentedModelPicking` | 三角面拾取、宽松拾取体与遮挡截断、分块蒙皮与整网格一致、隐藏与倒地拒绝 |
| `DemoControlFeel.AttackClick` / `RefusedSkillKeepsOrder` / `AutoAttackAfterCast` | A 点单位等同右键；未就绪技能不动订单与队列；施法跟进与回执 |
| `DemoOrders.AttackMoveCandidate` / `BookIsolation` / `ChasePress` | A 搜敌与平局；订单簿按局/生命隔离；追击不发呆 |
| `DemoMotionProbe` | 隔离原生物理世界：滑移、StepUp 回滚、ledge 撤销、失去支持/同 XY 落地、拒绝缺失位置记录、冲刺遇墙停下（`DashStopsAtWall`） |
| `DemoPathSteering.HeldGoal` | 目标点被占时到达即停 |
| `DemoCore`、命令存活性规则 | 基础身体/地面链 |
| `ControlMappings`、`Minimap.{CameraUp,FacingUp,NotMirrored,NorthFixed,RoundTrip,FitsFrame}` | 唯一注册表与绑定；小地图换算 |
| `LDLocalSettingsTests`（`LocalSettingsDashMigration`、`LocalSettingsZoomMigration`、`LocalSettingsCameraZoom`、`LocalSettingsMasterVolume`、`LocalSettingsCameraPanSpeed`、`LocalSettingsMinimapOrientation`、`LocalSettingsQuickCast`、`LocalSettingsCombatFeedback`、`LocalDisplayProductDefaults`、`LocalDisplayBorderlessOnly`、`LocalSettingsTravelFlush` 等） | 注册表与退役动作、schema 4 恢复、显式迁移、各偏好保存回读、损坏/越界与恢复、草稿隔离、显示配置实际回读 |

场景：

- `RunDemoCoreCheck.ps1`：DEV-only 三拥有连接夹具，覆盖基础身体/地面链与运动批次前缀审计；不开放普通 Start，不证明完整 CT06。
- `RunDemoCombatCheck.ps1`（`EngineRoot`、`PackageRoot`、`TimeoutSeconds`、可选 `NetEmulation`）：参赛 Listen Host + 一个独立拥有客户端。
  Host 报告 Net、Start、ChaseGoal、AttackMove、Stop、Death、Respawn、Protection、Skills、CastFollowUp、Render、Feel、Pointer；
  Client1 报告 Net、Start、Settings、AttackMove、Stop、Death、DeathCamera、Respawn、Protection、Skills、CastFollowUp、Feel、Queue。
  Pointer 组保存 `HostSees-MoveMarker.png`、`HostSees-Hover.png`、`HostSees-AttackMarker.png` 供人工审阅（不做像素比对）。
- `RunDemoFrontendCheck.ps1`：非 nullrhi 下真实窗口、VSync/画质应用与设置页布局。

重点覆盖：新命令取代旧命令、S 后迟到确认、死亡后旧路线、真实客户端的坡面/阻挡、UI 不穿透、回焦不自动行动。
本地回环实测（加减速 8000）：Host 右键到开始移动 31–35 ms；Client1 右键到开始移动 32–37 ms、到 Activated 14–17 ms、加速到全速 42–58 ms、S 到 Stopped 9–20 ms；
开局一两秒内有一次约 100–150 ms 的一次性长帧。网络模拟下的手感未重新测量。完整 3 真人 + 3 AI 验收见[验证手册](VERIFICATION.md#最小验证选择)。

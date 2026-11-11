# Demo 操作、目标选择与订单

**目标：v0.11.0 Demo · 模块：M3 · 引擎：Unreal 5.8.4**

本文说明 [App Spec](../APP_SPEC.md) §3.1 操作在当前代码中的实现：输入映射、右键分派、
当前订单、目标准备与确认、取消、镜头、本地设置和指针反馈。它对接
[架构](ARCHITECTURE.md)、[共享契约](SHARED_CONTRACTS.md) SC-01–09，不是第二份产品规格。
§4.2 的攻击移动自动选敌、平局排序与续行规则，以及所引用的首版内容数值，已于 2026-11-02 获用户批准；
玩法数值只在 [内容与数值](CONTENT_AND_BALANCE.md) 维护，本文只引用键名。

## 1. 范围与代码入口

链路为 **Enhanced Input → `ALDPlayerController` → `ULDNavigationComponent` / `ALDCombatDirector`
→ CharacterMovement**。真人通过拥有连接的 RPC 提交意图；对手 AI（`ALDOpponentController`）
在服务器上直接调用同一组 `SubmitOrder/CancelOrder/QueryOrder` 与 Combat 入口，不伪造 LocalPlayer 或 RPC。

本模块负责右键移动/追击、A 攻击移动、Q/W/E 与 D 冲刺的准备与确认、S 停止、Esc 返回、
镜头和可保存绑定。战斗域负责动作、资源、节拍、命中与效果；世界域负责地面/路径查询与观察权限；
生命周期域负责身体、真实时间步、保护与喷漆/守护。指令队列（Shift，§4.4）是唯一的连续指令形式；
没有连招、多单位框选、编队、团队命令或自由旋转镜头。

| 领域 | 代码入口 | 当前职责 |
| --- | --- | --- |
| 订单（拥有客户端） | `Source\Lockdown\Public\Navigation\LDNavigationComponent.h`、`Source\Lockdown\Private\Navigation\LDNavigationComponent.cpp` | 本地意图与身份、右键分派、按住右键连续移动、本地预测、心跳、回执处理、指针悬停与光标、镜头。 |
| 订单（服务器） | `Source\Lockdown\Private\Navigation\LDNavigationOrders.cpp` | 收件/去重/拒旧、单一待受理槽、Shift 指令队列、激活节流、准入、追击与攻击移动、租约、终结回执。 |
| 订单类型与规则 | `Source\Lockdown\Public\Navigation\LDOrderTypes.h`、`LDOrderRules.h`、`Source\Lockdown\Private\Navigation\LDOrderRules.cpp` | `FLDMoveIdentity`、请求/回执/失败码、`FLDOrderBook`、`LDOrderRules::NearestCandidate`、`ShouldRepeatHeldMove`。 |
| 路径跟随 | `Source\Lockdown\Public\Navigation\LDPathSteering.h`、`Source\Lockdown\Private\Navigation\LDPathSteering.cpp` | `PlanStart`、`PlanRoute`（`EGoal::Ground/Body`）、`Steering`、`AtGoal`、`HaltLocomotion`。 |
| 指针反馈 | `Source\Lockdown\Public\Navigation\LDPointerFeedback.h`、`Source\Lockdown\Private\Navigation\LDPointerFeedback.cpp` | 仅本地的地面标记、攻击标记、悬停圈与选中圈。 |
| 输入映射与按键分派 | `Source\Lockdown\Public\Framework\LDPlayerController.h`、`Source\Lockdown\Private\Framework\LDPlayerInput.cpp` | 动态映射、按下/释放分派、Esc 返回、缩放、指针下单位拾取、检查对象。 |
| 战斗意图 | `Source\Lockdown\Private\Framework\LDPlayerOrders.cpp`、`Source\Lockdown\Public\Combat\LDCombatPreInput.h` | Q/W/E/D 准备与确认、快速施法、预输入、S 的动作取消、HUD 提示入口。 |
| 本地设置 | `Source\Lockdown\Public\UI\LDControlSettings.h`、`Source\Lockdown\Private\UI\LDControlSettings.cpp`、`Source\Lockdown\Private\UI\LDControlsPanel.cpp` | 唯一动作注册表、重绑、镜头/文字/音量/小地图/快速施法偏好与保存。 |
| 身体与运动 | `Source\Lockdown\Private\Character\LDCharacter.cpp`、`Source\Lockdown\Public\Character\LDCharacterMovementComponent.h` | SpringArm 镜头、缩放夹限、加减速、瞬时转向；权威运动与原生运动捕获。 |

组件技术说明见 [移动/输入/镜头](../systems/MOVEMENT_INPUT_AND_CAMERA.md)、
[战斗](../systems/COMBAT_AND_GROWTH.md)、[视野/UI](../systems/VISION_AND_UI.md)、
[世界管线](../systems/WORLD_AND_DATA_PIPELINE.md) 和
[网络](../systems/ARCHITECTURE_AND_NETWORKING.md)。

## 2. 最小完整操作表

默认键来自 `ULDControlSettings::Definitions()`，可在设置中重绑。所有世界命令先经过 §3 的输入门槛；
客户端的禁用或本地检查不代替服务器拒绝。

| 输入/条件 | 本地结果 | 权威结果/边界 |
| --- | --- | --- |
| 右键地面 | 发送一个 `MoveGround`；同帧在点击点显示绿色收缩虚线圈，本地路线检查失败时改为红叉 | 服务器按点击点严格求完整路径，CMC 实际行走；partial path 不报告到达。 |
| 按住右键拖动 | 指针地面点相对上次订单移动 ≥ 40 cm 时，最多每 0.125 s 再发一个普通 `MoveGround` | 每次重复都是带新身份的普通订单，仍受服务器激活节流与最新待受理替换约束；本地路线检查已失败的点直接跳过，不发送。 |
| 右键当前可见单位 | 发送一个 `AttackUnit`；目标脚下显示红色收拢圈 | 服务器重查观察权限后接近、追击，在射程内逐次申请普攻（§4.2）。 |
| 左键，未准备动作 | 检查指针下的当前可见单位；点空地清除检查对象 | 不移动、不攻击、不发送订单。 |
| A，然后左键 | A 进入攻击移动准备；左键时先用与右键攻击相同的指针拾取（`ObservedUnderPointer`，约 12 px 容差）：指在可攻击的可见单位上时等同右键攻击该单位，发送 `AttackUnit` 并显示攻击标记；否则把指针下地面作为 `AttackMove` 目的地发送（`LDOrderRules::AttackConfirmKind`，2026-11-07 用户决定） | A 按下本身不攻击；两种确认都按 Shift 排队、都解除保护；指针下无单位也无地面时仍发送缺省目的地，由服务器明确拒绝（§4.3）。 |
| S | 清准备态与预输入，立即停止本地预测运动，发送递增 Epoch 的 Stop，并按已发出的最大 DirectSequence 取消 Combat 动作 | 停移动/追击/待交互并取消可中断动作；已释放效果、已造成伤害不回滚。 |
| Q/W/E | 读取 Combat owner view 中对应主动的目标模式：Self 直接发送；Unit/方向技能进入准备，左键确认。对单位的确认发出 `CastUnit` 订单（§4.5），不经预输入 | 服务器按来源、修订、目标与资源重查；空槽也发送，由服务器明确拒绝，被拒绝的请求不打断当前移动或攻击订单、不清队列（§4.6）。对单位施放在距离外时先接近再施放。 |
| D | 通用冲刺（`ELDCombatEntry::Dash`，人人都有的 `Innate` 来源）：按下即朝指针方向发出（`LDCombatPreInput::FiresOnPress`），不进入准备、不需要左键确认，与快速施法无关；指针下没有地面时照常发送缺目标请求由服务器明确拒绝 | 服务器按同一 Combat 入口重查冷却、定身与起步阻挡；遇阻挡停在最远合法位置；不是进攻意图，不解除复活保护。 |
| P | 开关自身属性页 | 只读自身投影；死亡时也可打开。 |
| 按住 Tab | 显示局内排行榜，松开收起 | 只读公开 `KillFeed.Scores`；不是世界命令。 |
| F9 | 试玩标记：本机日志写 `LD_PLAYER_MARK`（远端客户端带本人状态），并经 `ServerPlaytestMark` 让房主日志也记下；HUD 提示“已标记 #N” | 不是世界命令；存活或死亡等待中都可用，只在本局进行中生效；房主日志每名参赛者每秒最多一条。 |
| 方向键/屏幕边缘 | 沿固定俯视平面平移镜头 | 不移动角色、不改变视野或订单。 |
| Space | 按住期间镜头跟住自己，松开恢复原跟随/自由状态 | 不改持久跟随偏好、不停止订单。 |
| 按住 Shift 下达上述右键、A 左键、Q/W/E/D（含小地图右键） | 订单带 `bQueue`、技能经 `ServerQueueCombatAction` 进入队列，不做预输入暂存、不按住重复；地面步骤显示小圈与虚线路径 | 服务器把它排在当前订单与已排步骤之后，最多 `LDOrderRules::MaxQueuedSteps`（8）步，满时 `QueueFull`/`CapacityUnavailable` 拒绝（§4.4）。 |
| Home | 切换镜头跟随并保存该偏好 | 本地偏好，不是服务器状态。 |
| 滚轮 | 世界区域内缩放镜头 | 指针在 UI 上时不缩放世界。 |
| 小地图左键/拖动、右键 | 左键/拖动把镜头平移到该点；右键对该点发 `MoveGround` | 与世界右键地面使用同一订单入口。 |
| Esc | 子确认/模态 → 准备态 → 普通主面板 → 菜单，每次只处理一层 | 不 Stop 已接受订单、不发世界命令。 |

**鼠标取消规则：** 已有准备态（Q/W/E/D 或 A）时，右键只取消准备并消费这次点击，不移动、
不攻击；再次右键才走普通分派（`ALDPlayerController::V05Pressed`）。
右键分派顺序为：指针下的当前可见单位 → 地面（`ULDNavigationComponent::ClickMove`）；
一次点击只产生一个类别的订单，被拒绝时不改发另一类订单。

**指针反馈**（`ALDPointerFeedback`，仅拥有客户端本地生成，不复制、无碰撞、不参与寻路）：

- 自己的地面订单点：绿色收缩虚线圈；本地 `LDPathSteering::PlanRoute` 检查失败时显示红叉，
  服务器拒绝仍以失败提示另行告知。
- 自己的 A 攻击移动地面点：与地面订单完全相同的收缩虚线圈，只是红色（`ShowMove(..., bAttack=true)`）；路线检查失败时同样是红叉。
- 自己的 `AttackUnit` 目标：红色收拢圈。
- 悬停：右键此刻会攻击的可见单位脚下显示红色悬停圈（与攻击标记同色，带近黑描边）。
- 选中：左键检视的可见单位脚下显示不转动的浅色密点线圈，位于悬停圈之内；HUD 不在其身上画方框。
  目标位置只取自拥有者已授权的观察帧（`ALDWorldViewRelay`）。
- 光标（`ULDNavigationComponent::TickPointer`，画法见 `LDCursor`）：所有光标类型都映射到游戏自己的软件箭头；
  A 攻击移动待确认时为 `LDCursor::AttackAim`（红色描边光），主动技能待确认时为 `LDCursor::CastAim`（蓝色描边光），
  其他（含悬停单位、指针在 UI 上）为普通箭头。
- `ULDNavigationComponent::ReadHovered` 返回当前悬停单位；`PointerPosition` 与 `GroundUnderPointer`
  是世界点击、悬停和准备确认共用的指针入口。`DevSetPointer` 只在 `WITH_DEV_AUTOMATION_TESTS`
  构建中替代鼠标，其他构建无效。

### 2.1 能力目标与来源

能力目标模式只由内容定义给出（`ELDDemoTargetMode`：`Self`、`Unit`、`DirectionProjectile`、`DirectionDash`、`GroundPoint`），
请求上的目标种类为 `ELDCombatTargetKind`：`Missing`、`Self`、`Unit`、`Direction`、`Point`。
以地面点为目标的能力只有 `GroundPoint` 区域技能（当前目录为临时开个短会）；其余地面点只用于移动和攻击移动订单。

| 目标模式 | 准备与确认 | 服务器处理 |
| --- | --- | --- |
| Self | 按键或 HUD 图标直接发送 | 目标固定为当前自己的身体。 |
| Unit | 进入准备；左键取指针下的当前可见单位（观察句柄与 ObservationEpoch） | 当前观察权限、目标生命、关系、距离与内容条件；失效不改选附近目标、不改为地面或自身。 |
| DirectionProjectile / DirectionDash | 进入准备；左键取自身脚点到指针地面点的平面方向 | 有限、非零的平面方向及内容几何；位移经 CMC root motion。 |
| GroundPoint | 进入准备；左键取指针下的地面点（`Point`） | 有限的地面点；释放时夹到技能射程，区域在该点生效。 |
| 无法解析的确认 | 指针下没有单位/地面或方向为零时，以 `Missing` 目标发送 | 服务器明确拒绝；不在客户端吞掉确认。 |

**快速施法**（本地设置 `QuickCast`，`ULDControlSettings::GetQuickCast`，默认关闭）：开启后，
Unit 技能在指针下已有可见单位时、方向技能在指针下有地面时，按键当下即经 `ConfirmArmedCombat` 发送；
否则仍保持准备，等待普通左键确认。

**施法后自动攻击**（本地设置 `bAutoAttackAfterCast`，`ULDControlSettings::GetAutoAttackAfterCast`，默认开启，#167）：
不带 Shift 的 `CastUnit/CastPoint` 订单发出时，`ULDNavigationComponent` 按 `LDOrderRules::CastFollowUpTarget`
记下后续目标：对单位技能取其目标；对地面/方向技能取施法前服务器已激活的 `AttackUnit` 目标，没有则不跟进。
客户端按订单身份保留已发送攻击的观察句柄，以 `ClientActive` 选取正在执行的目标；排队或被拒的请求不覆盖它，
终止回执和新生命清理相应记录。跟进目标和自动攻击身份在发 RPC 前登记，以处理 Listen Host 的同步回执。
该施放订单的回执经 `LDOrderRules::CastFollowUpStep` 判定：仍是最新的本地订单、`Completed` 且无失败、
目标在 owner view 中仍可见时，客户端发出普通的 `AttackUnit` 订单，服务器照常校验；被拒、取消、
被更新的订单或 S 取代、目标不可见时放弃，原地站立。Shift 排队的施放、Shift 入队步骤与新生命都清掉待跟进目标。
自动攻击被服务器拒绝时只更新状态行，不弹提示。攻击移动（A 地面）不在恢复范围内。

**预输入**（`LDCombatPreInput`）：自己的 Q/W/E 正在前摇时再按一次 Q/W/E/D，
控制器只保留最新一次按键，前摇结束后作为普通 Direct 请求发送，而不是先到服务器被判忙。
S、新生命或新局、输入被抑制或保留超过 1 s 时丢弃；超时会提示“前一个技能未结束，预输入已取消”。
预输入同时绑定技能的 `SourceRevision`；来源变化时取消，不把旧目标用于新技能，并提示“技能已变化，预输入已取消”。
普攻不需要预输入：攻击订单本来就在服务器等待身体空闲。

每个 Direct 请求携带 Generation/Life、单调递增的 `DirectSequence`、入口（Q/W/E/Dash）、
目标和发送时 owner view 的 `ExpectedSourceRevision`。owner view 的生命或 `SourceRevision` 变化时，
本地准备态被撤销（`ALDPlayerController::OnRep_CombatView`），需要重新准备；
普通 HP/MP 修订变化不撤销准备。按钮可用性与冷却只读 Combat 的 owner view，客户端不维护槽位冷却；
来源丢失期间的冷却规则由 Combat 来源域实现，见[战斗手册](../systems/COMBAT_AND_GROWTH.md)。
能力确认越距时直接由服务器拒绝，**不自动走近施法**；只有右键普攻和攻击移动订单负责接近。

## 3. 输入所有权、镜头与设置

### 3.1 同一手势只有一个消费者

`ALDPlayerController::CanWorldCommand()` = `IsWorldLifeValid() && !IsWorldInputSuppressed()`：

- `IsWorldLifeValid`：当前公共比赛正在进行，本拥有者已接受的生命为 Alive 且持有身体。
  失去它是唯一会结束已接受订单的本地条件。
- `IsWorldInputSuppressed`：菜单或设置打开、有按键自菜单/失焦/重建映射后尚未松开（fresh-press 门槛），
  或 UIRoot 顶层输入层是文本编辑/子确认（`IsWorldInputCaptured`）。它只阻止新世界命令；
  已接受订单继续移动并发送心跳。
- 鼠标世界点击、悬停、缩放与边缘平移额外检查 `ULDUIRoot::PointerOverUI()`：指针在 UI 上时不进入世界。
- 子确认或文本编辑打开时，控制器每帧撤销已有准备态；被捕获期间不会新建准备态。
- 小地图左键拖动通过 Slate 鼠标捕获保持到松开；拖动期间镜头边缘平移关闭。

**Esc**（`ALDPlayerController::V05Back`）每次只处理一层：`CloseTopModal`（子确认/模态）→
`CancelArmedCombat`（准备态）→ `CloseTopPanel`（普通主面板）→ 打开/关闭菜单。
Esc 不调用 Stop、不发送世界命令或候选提交。

**失焦：** 操作系统失焦时控制器清空按键（`FlushPressedKeys`）并发送一次 Stop，失焦期间不发心跳、
不处理指针；恢复焦点后需要新的按下，旧订单不会自行恢复。

**死亡：** 死亡等待复活期间只有静态地图镜头可用（`IsAwaitingRespawn`、`CanCameraCommand`）；
自身属性页仍可打开。三选一确认走成长域入口，不经过世界命令门槛。

### 3.2 只保留一套运行映射

`ALDPlayerController::RebuildV05Mappings` 按 `ULDControlSettings::Definitions()` 的唯一注册表在运行时
为每个动作创建 `UInputAction`，放入一个动态 `UInputMappingContext`（优先级 10，
`bIgnoreAllPressedKeysUntilRelease`），绑定 Started/Completed/Canceled 到 `V05Pressed/V05Released`，
滚轮轴绑定到 `V05Zoom`。重建前先移除旧绑定句柄与旧 context，并要求所有键松开后才接受新世界命令；
重建不会停止已接受订单。仓库中没有静态 IA/IMC 输入资产。

动作集合：`Select`、`Navigate`、`AttackMove`、`Skill1`、`Skill2`、`SkillE`、`Dash`、`Stop`、`Attributes`、`Scoreboard`（按住 Tab）、`Mark`（F9 试玩标记）、
`PanUp/PanDown/PanLeft/PanRight`、`Recenter`、`Follow`、`Zoom`、`Back`。
`Back` 的 Esc 是保留键；重绑校验冲突，不静默交换两个动作。

本地设置 schema 当前为 19（`ULDControlSettings::CurrentSchema`），与产品版本、网络版本分开。
旧 schema 5–18 显式迁移并标记为未保存（schema 5–13 缺 `Scoreboard` 时在 Tab 未被占用时补默认 Tab；schema 5–15 缺 `Mark` 时在 F9 未被占用时补默认 F9；schema 16 及更早的按键表删除已退役的 `Item1`/`Item2`；缺 `SkillE` 时仅在 E 未被占用时补默认 E）。schema 18 保留已存的施法后自动攻击偏好，schema 5–17 对该偏好使用默认开启；先按各自旧范围校验 Zoom（schema 5–11 为 1100–1700 cm，schema 12 为 1100–1900 cm），
在当前 1300–2400 cm 范围内则原样保留（包括旧默认 1400、1850），低于 1300 cm 的调整为 1300 cm 并在提示中说明；
不猜测用户意图或自动替换距离。schema 4 的多注册表布局被明确拒绝，其可恢复偏好也遵守旧范围。
新安装默认 2250 cm；设置页“采用推荐镜头”只修改 Zoom，不重置姓名、绑定、音量、跟随或其他偏好。
绑定集被拒绝时仍可恢复其中已校验的镜头/文字/音量/小地图偏好和玩家名。
保存失败会展示错误并保留当前内存设置及未保存标记。

### 3.3 镜头

`ALDCharacter` 的 SpringArm/Camera 与 `ULDNavigationComponent` 的
`PanCamera/Recenter/ToggleFollow/PanTo/TickCamera` 实现固定俯视斜角，不提供旋转。

- 方向键与边缘平移的速度为本地设置 `CameraPanCmPerSec`；手动平移转为自由镜头。
- 边缘平移带为视口四边各 8 px（`TickCamera` 中的固定值），在失焦、指针在 UI 上、
  小地图拖动和 Q/W/E/D/A 准备态时关闭。
- Space 按住临时跟随自己，松开恢复；Home 切换跟随并保存 `bStartFollowing` 偏好。
- 默认臂长 2250 cm，固定 pitch −60°、yaw −90°（正北朝上，#97）、FOV 55°；共享常量由 `Character\LDCameraSettings.h` 定义，
  身体、死亡等待相机、本地设置与美术镜头检查共用，不改变授权视野。
- 滚轮每格改变 80 cm 臂长并夹在 1300–2400 cm（`ALDCharacter::ZoomedArmLength`），
  结果保存为本地设置 `Zoom`；这是固定的原生常量，没有可调的缩放步长或范围设置。
- 镜头中心夹在已注册地面的静态边界内，不使用动态单位决定夹限。
- **死亡镜头**：等待复活时视角切到等待相机，沿用本生命最后的镜头中心，可自由平移与缩放静态地图，
  不跟随任何人、不获得新视野；Space/Home 提示“无存活角色”。新生命建立后恢复保存的跟随偏好。

## 4. 当前订单与战斗交接

### 4.1 一个当前订单，不是另一个动作系统

`ULDNavigationComponent` 是一个参赛身体当前订单的唯一写入者。订单种类（`ELDSpatialOrderKind`）只有
`MoveGround`、`AttackUnit`、`AttackMove`；Stop 是取消而不是订单种类。
回执状态（`ELDOrderStatus`）为 `Queued`、`Rejected`、`Activated`、`Progress`、`Completed`、`Cancelled`。
没有历史订单栈：服务器只保存一个待受理槽、一个当前订单、最多 8 步的 Shift 队列（§4.4）和有界回执窗口。

**身份：** `FLDMoveIdentity{Match, Life, Order, Epoch}`，均为 `uint32`，由拥有客户端
`NextLocalIdentity` 分配：每个新意图递增 Order，Stop 同时递增 Epoch；用尽时明确提示并停止下达。

**提交流程：**

1. 客户端 `SendOrder` → `ServerSubmitOrder` → `SubmitOrder(Caller, Request)`：校验调用者与身份、
   请求形状（`FLDSpatialOrderRequest::HasValidShape`），同身份同条款只重放原回执，异条款为 `RequestConflict`，
   Order 不大于已收最大值或 Epoch 回退为 `StaleOrder`。通过后进入待受理槽并回 `Queued`。
2. 待受理槽只有一个：新请求（含 Stop）替换旧的待受理请求，旧请求得到 `Cancelled`
   （`Superseded` 或 `Stopped`）。被替换的攻击类请求仍欠一次解除保护（§4.3）。
3. 核心 Actions 阶段 `ProcessAuthorityOrders(Permit)`：非 Stop 订单受 0.1 s 激活节流
   （`FLDOrderBook::ActivationDue`，`OrderQueryIntervalSeconds`），Stop 不受节流。
   激活时攻击类先解除保护，再做完整准入；准入失败回 `Rejected`，原已接受订单继续；
   准入成功则原订单回 `Cancelled/Superseded`，新订单回 `Activated`。
4. 核心 Motion 阶段 `AdvanceAuthorityMotion` 把当前订单的转向输入交给 CMC；
   路径 2 s 无进展先重算一次，再无进展则以 `PathBlocked` 结束。

**路径终点：** 点击的地面点严格按点击规划（`LDPathSteering::EGoal::Ground`）；
追击的身体脚点按 `EGoal::Body` 规划：贴墙、贴身或切角时，若脚点落在 Recast 侵蚀边缘外，
终点吸附到一个胶囊半径内最近的 navmesh 点并在同一支持面上重新校验。路线起点总经过 `PlanStart`。

**心跳租约：** 真人订单需要拥有客户端每 0.5 s 发送一次 `ServerHeartbeat`（不可靠 RPC，序号递增）；
服务器按 `LDCommandLiveness` 的 2 s 窗口判断租约，过期以 `Expired` 结束。
心跳只刷新当前已接受身份，不创建或恢复订单。AI 订单不需要心跳。

**本地预测：** 非权威的拥有客户端收到 `Activated/Progress` 后按回执目标自行规划同一路线并转向；
Listen Host 不预测，直接由服务器运动驱动。预测的 `MoveGround` 路线失败时客户端自行发送 Stop 并提示。

**提示：** 订单进度文本只写入 `ULDNavigationComponent::Status`。只有真实失败与“复活保护没了”
经 `ALDPlayerController::PostIntentNotice` 进入 HUD 的单行提示；被自己的新订单替换（`Superseded`）
或自己按 S（`Stopped`）不提示。

### 4.2 右键普攻与 A 攻击移动

**AttackUnit：** 激活时以 `NewAttackOrder` 目的重查观察句柄（`ALDVisionDirector::ResolveObservedUnit`），
不能解析或指向自己即 `TargetUnavailable`，不回退到地面或附近单位；成功后经 `BindObservedOrder`
把订单绑定到该观察。射程每次取 Combat 的当前 `AttackRangeCm`（`STAT.AttackRangeCm` 的派生结果），
不在操作层硬编码。每个 Actions 阶段以 `AttackStart` 目的重查目标：

- 在射程内且普攻就绪时，以 Nav 形 `FLDCombatRequestKey`（订单身份 + 递增的 `AttackCycle`）调用
  `ALDCombatDirector::StartNavAttack`；Combat 分配动作身份并负责前摇、节拍与命中。
  Combat 以 `OutOfRange/Blocked` 拒绝时订单贴近并重算路线。
- 不在射程内时追击：目标移动 ≥ 50 cm 且距上次规划 ≥ 0.1 s 时重算路线；从“交战”转回追击（出手后、
  等待结束）时当即重算，不沿交战前的旧路线走。
- 只有仍在前摇的动作或在射程内等待时订单保持“交战”，客户端停止本地转向。
  本 Actions 阶段到点出手的普攻（`ReleaseDue` 在导航之后执行）不再占住身体，订单当即转为追击
  （`LDOrderRules::ActionHoldsChase`、`FLDCombatBodyRead::ActionReleaseAtMs`）。
- 在射程内等下一次节拍时，目标比上一次样本沿两者连线后退 ≥ 1 cm 即继续贴近（press），
  向自己走来才停下等待，静止样本维持原状态；样本跳变超过 200 cm（换目标、被位移）不算走开
  （`LDOrderRules::ChasePress`）。贴近期间回执的接近半径为 0，客户端预测不在射程边缘停下。
  这样同速追击是“接近 → 射程内起手 → 出手命中（不查射程，仍须可见且无遮挡）→ 继续追 → 再次进入射程起手”，不在射程边缘走走停停
  （2026-11-07 用户决定：“只要起手就判定一定会打中，即使跑远了”，见 [战斗与属性](COMBAT_AND_ATTRIBUTES.md) §6.3）。
- 对同一观察目标（同句柄与 epoch）重新下达 AttackUnit（例如追击中连点右键）时，新订单经
  `ALDCombatDirector::AdoptNavAttack` 接管尚未出手的普攻；其他新订单、S 与订单结束仍取消它，已承诺节拍不退回。
- 被定身时没有自愿转向输入，已提交的 root motion 继续。

订单只锁定一个主目标；群攻与副击由 Combat/内容处理，不生成副目标订单。

**AttackMove：** 只由 A 后左键点在地面上产生；A 后左键点在可攻击的可见单位上时发出的是对该单位的 `AttackUnit`，
与右键该单位完全相同，不按最近单位搜敌（2026-11-07 用户决定）。唯一目的地是确认时的地面点（严格规划）。每个 Actions 阶段在没有锁定目标时，
从 `EnumerateVisibleUnits(..., AttackMoveCandidate)` 的当前可见单位中，按自身脚点平面距离选最近者
（`LDOrderRules::NearestCandidate`），同距取较小的观察句柄，排除自己；
搜敌半径为 `TARGET.AttackMoveAcquireRadiusCm`（内容公式见 [内容与数值](CONTENT_AND_BALANCE.md)）。
选中后按 AttackUnit 同一方式交战并保持到目标死亡、失效或订单替换。
合法观察中确认目标死亡（`FindObservedTermination`）后放下目标，重新搜敌或继续走向原目的地；
无目标到达目的地即 `Completed`，不变成永久自动攻击开关。

**失去视野：** 取消锁定并结束这条订单（AttackUnit 与已锁定目标的 AttackMove 都是
`Cancelled/TargetUnavailable`），不走向最后可见位置、不恢复原 A 目的地，重新可见也不重启旧订单。
已在前摇的普攻随订单一起取消，不打出（2026-11-07 后续用户决定 "let's add back the visibility and line of sight check"：出手时目标须可见且视线无遮挡）。
只有授权观察到的死亡才让 AttackUnit 以 `Completed/TargetDied` 结束、让 AttackMove 续行；
隐藏、过期或 `TargetUnavailable` 都不被当作死亡。

### 4.3 发出进攻意图即解除保护

发出进攻意图即解除复活保护，目标无效、距离不足、没蓝、CD 未就绪等失败也不保留保护。
本地准备/取消、MoveGround、Stop、检查与镜头操作不是进攻意图。

- `AttackUnit/AttackMove`：激活时先验证调用者、身份与写许可，再调用
  `ALDGameMode::EndProtectionForAggressiveIntent`，之后才做目标/路径准入。准入失败的 `Rejected`
  回执仍携带 `bProtectionRemoved` 与 `LifeRevision`。待受理时已被更新请求替换的攻击类请求，
  在下一次激活时补做一次解除。
- Q/W/E：Combat 在处理 Direct 请求时以同一入口解除（`LDCombatActions.cpp`），空来源或缺省目标也算尝试。
  D 冲刺是移动，不解除保护（App Spec §6.1“保护期间可以移动”）。
- 攻击移动确认在指针下没有地面时仍发送缺省目的地，由服务器拒绝，保护照样解除。
  重复请求只重放回执，不再次解除。
- Shift 队列里的攻击订单与 Q/W/E 在收件后的下一次 Actions 阶段即解除保护（`SettleQueuedAggression`，
  每生命只记最早一条），不等轮到它执行；之后被清空也不保留保护。

### 4.4 Shift 指令队列

App Spec §3.1 的指令队列（2026-11-06 用户决定）由 `ULDNavigationComponent` 在服务器保存，不是第二套动作系统：
空间步骤轮到时成为普通的待受理请求，走同一激活节流、准入与回执；技能步骤轮到时调用
`ALDCombatDirector::SubmitAction`，由 Combat 照常判定。

- **收件**：`FLDSpatialOrderRequest::bQueue` 为真的订单与 `QueueAction`（`ServerQueueCombatAction`）的技能，
  校验调用者、形状和新鲜性后追加到 `Queue`（`FQueuedStep`），回 `Queued`。技能的 `DirectSequence`
  必须仍未被 Combat 见过（`QueryActionReceipt` 为 `StaleRequest`），且大于已排和正在执行的序号。
  满 `LDOrderRules::MaxQueuedSteps` 时订单回 `QueueFull`、技能回 `CapacityUnavailable`。
- **出队**（`AdvanceQueue`，Actions 阶段在激活待受理请求之前）：没有待受理请求、当前订单和执行中的排队技能时
  （`QueueHeadDue`）取出队首。空间步骤放入待受理槽；技能步骤交给 Combat 并记下 `RunningQueuedAction`，
  直到其回执 `Released/Finished/Rejected`（`QueuedActionSettled`）才继续。方向技能带着排队时的地面点，
  出手时从身体当前脚点重新求方向（`QueuedAimDirection`）。
- **结束一步**：当前订单结束或排队步骤被拒绝时按 `LDOrderRules::QueueAfter` 处理——到达、路线不通/受阻、
  目标不可见/死亡、击退等跳到下一步；Stop、死亡、过期租约、旧生命/旧局、不是拥有者、嘲讽清空队列。
  后面还有地面步骤时，到达不调用 `HaltLocomotion`，客户端预测也不停身体。
- **清空**：不带 Shift 的订单（`SubmitOrder`）、S（`CancelOrder`）、`EndLifeOrders`/`ResetMatch`、嘲讽都调用 `ClearQueue`：
  空间步骤回 `Cancelled` 及原因，技能步骤回 Combat 的拒绝回执（`Cancelled/Dead/MatchClosed/ControlBlocked`）。
  不带 Shift 的技能只在**被受理后**清掉它之前排的步骤（`ClearQueue(Failure, BeforeStep)`，按步骤到达序号 `FQueuedStep::Serial` 截断，
  之后才排的步骤保留），且不改当前订单：Direct 技能在 Combat 判定后（`AwaitDirectVerdict`/`SettleDirectVerdicts`，
  Actions 阶段开头，判定前不推进队列），`CastUnit/CastPoint` 在激活准入成功时（`FPendingOrder::QueueCut`）。
  被拒绝的技能不清队列（2026-11-07 用户决定，§4.6）。
- **客户端**：`ALDPlayerController::IsQueueModifierDown` 读左右 Shift（固定键，`ULDControlSettings` 拒绝把它绑给动作）。
  Shift 右键不会按住重复，Shift 技能不经预输入暂存。`FQueuedMark` 记下自己排队的地面步骤，
  `ALDPointerFeedback::SetQueue` 画小圈与从身体经当前目的地到各点的虚线；步骤开始或结束、Stop、新生命时移除。

### 4.5 对单位或地面点施放：接近后施放

App Spec §3.1（2026-11-06 用户决定）：对单位的 Q/W/E 是一条 `ELDSpatialOrderKind::CastUnit` 订单，替换当前订单
（按 Shift 时排进 §4.4 的队列），经 `ServerSubmitCast` / `SubmitCast` 与它的 Direct 请求一同收件。

- **收件**：Direct 请求必须是非 D 的条目、`Unit` 目标且与订单的观察句柄一致，序号未被 Combat 见过。
  不带 Shift 的施放在收件时先做一次就绪判定（`CastReady`），无法施放即回 `Rejected/CastRefused` 并发出 Combat 拒绝回执，
  不占待受理槽、不动当前订单和队列；这次尝试仍在下一次 Actions 阶段解除保护（`QueueOwedAggression`，§4.6）。
- **激活**（`AdmitOrder`）：先用 `ALDCombatDirector::QueryAvailability` 判定就绪——冷却、法力、受控、空槽、非单位技能
  立即 `CastRefused` 并以 Combat 拒绝回执报告原因；自身前摇（`Busy`）只让施放延后。施法距离取该视图的 `RangeCm`
  减 `CastMarginCm`（10 cm）。随后与 AttackUnit 同样重查观察句柄并绑定。
- **推进**（`AdvanceCast`）：距离外按追击重算路线（`ChaseTarget`，与普攻订单共用）；进入距离后调用
  `ALDCombatDirector::StartNavCast`，它在当前 Actions 许可内按普通 Direct 请求判定。`OutOfRange/Blocked` 不记录请求，
  订单继续贴近；`Busy` 原地等待；其余结果记录并回执，订单结束（施放成功 `Completed`，否则 `CastRefused`），身体停下，
  Shift 队列等这次施放结束后再继续。目标失去视野或死亡与 AttackUnit 相同地结束订单。
- **对地面点或方向投射**（`CastPoint`，2026-11-06 用户决定）：`GroundPoint` 与 `DirectionProjectile` 技能确认在地面点时
  发出 `CastPoint` 订单（`GroundCm` 为指向的点），就绪判定与 `CastUnit` 相同（`CastReady`，模式须与瞄准相符）。
  点在 `RangeCm - CastMarginCm` 外时按身体目标规划到该点并行走；进入距离即停下（`AdvanceCastPoint`），
  以 `Point` 原样或从当前脚点朝该点重新求 `Direction` 调用 `StartNavCast`。对点施放只有 `Busy` 原地等待，
  其余结果都结束订单；走到路线终点仍不在距离内以 `NoCompletePath` 结束。`DirectionDash` 技能与 D 没有施法距离，不走这条路。
- 对手 AI 仍直接调用 Combat 的 `SubmitAction`，不使用 `CastUnit/CastPoint`。

### 4.6 被拒绝的技能不打断当前订单

App Spec §3.1（2026-11-07 用户决定）：Q/W/E/D 的请求被拒绝（冷却未好、法力不足、受控制、空槽等任何准入失败）时，
不取消、不替换、不暂停当前的移动或攻击订单，也不清空 Shift 队列；角色继续移动或攻击，拒绝原因仍经 Combat 回执提示。
只有被受理的施放才按 §4.4/§4.5 取代当前订单或清掉之前排的步骤。判定共用 `LDOrderRules::SkillRequestReady`：
`ALDCombatDirector::QueryAvailability` 的当前可用性为 `None` 或自身前摇 `Busy` 才算就绪（服务器按权威时间现算，
不读客户端 owner view 里只供显示的 `Unavailable`）。

- `CastUnit/CastPoint`：不带 Shift 时收件即判定（§4.5），被拒绝不进入待受理槽；就绪的施放激活时再判定一次，失败同样保留原订单。
- Direct 技能（Self、冲刺类、D、缺目标）：本来就不改当前订单。队列里有步骤时，`ULDNavigationComponent::RefuseUnreadyDirect`
  先判定，未就绪的请求不交给 Combat，而是记入 Combat 的 Direct 回执窗口但不推进其序号高水位：重放同一请求得到原拒绝，
  条款不同为 `RequestConflict`，队列里序号更小的技能步骤仍可执行；直接回拒绝回执，
  Q/W/E 仍在下一次 Actions 阶段解除保护；交给 Combat 的请求等判定后才清队列（§4.4）。
  交给 Combat 后被拒（如自身前摇中按 D 判为 `Busy`）时高水位已越过排队步骤，但排队步骤在入队时已确认 Combat 未见过，
  轮到时以排队步骤身份提交（`SubmitAction`/`StartNavCast` 的 `bQueuedStep`），不因更新的序号过期，仍只执行一次；
  与排队步骤同序号的 Direct 请求以 `RequestConflict` 拒绝。判定共用 `LDCombatSourceRules::DirectSequenceVerdict`。
- 客户端发送施放订单时不提前清掉本地排队标记，被丢弃步骤的 `Cancelled` 回执才移除标记；确认技能不发送 Stop。
- 已受理后才失败的施放（接近途中目标失效、视线、来源变化等）仍按 §4.5 结束施放订单并原地站立（不触发施法后自动攻击）。

## 5. SC-09 正式对接表

### 5.1 状态与写入者

| 状态 | 唯一写入者 | 消费者 |
| --- | --- | --- |
| 当前订单与待受理槽（`FActiveOrder`/`FPendingOrder`，私有） | 服务器 `ULDNavigationComponent` | 拥有者经 `ClientOrderReceipt` 得到回执；AI 经 `QueryOrder`。 |
| Shift 队列（`Queue`/`RunningQueuedAction`，私有） | 服务器 `ULDNavigationComponent` | 空间步骤经订单回执、技能步骤经 Combat 回执报告；AI 不使用。 |
| 订单簿 `FLDOrderBook` 与回执窗口（最多 16 条） | 服务器 `ULDNavigationComponent`，绑定一局一生命 | 拒旧与幂等重放；换局（`ResetMatch`）或换生命时整体重建。 |
| 本地身份、预测路线、`Status` | 拥有客户端 `ULDNavigationComponent` | HUD、指针反馈。 |
| 准备态（`ArmedEntry`/`bAttackMoveArmed`）、预输入槽、检查对象 | `ALDPlayerController` | HUD、光标；不复制给服务器。 |
| 绑定与本地偏好 | `ULDControlSettings` | 控制器、Navigation、HUD、设置面板；不写局内数值。 |
| 动作、资源、节拍、来源可用性 | `ALDCombatDirector` | Navigation 只读 `ReadCombatBody`（射程、普攻就绪、动作进行中）并申请普攻。 |
| 实际运动 | `ULDCharacterMovementComponent` | 原生运动批次交给核心与喷漆区判定（§7.3）。 |

### 5.2 窄类型与函数

| 类型/签名 | 字段和意义 |
| --- | --- |
| `FLDMoveIdentity` | `Match/Life/Order/Epoch`（`uint32`），也随原生 saved move 传输。 |
| `FLDSpatialOrderRequest` | `Identity`、`Kind`、`bQueue`（Shift，§4.4）；MoveGround/AttackMove 用 `GroundCm` + `SurfaceHint`，AttackUnit 用 `Observed` + `ObservationEpoch`。`HasValidShape` 要求其余字段保持默认；`SameTerms` 判断重放；`IsAggressive` 为两种攻击订单。不传路径点或 Actor。 |
| `FLDOrderReceipt` | `Identity`、`Kind`、`Status`、`Failure`、单调 `ReceiptSerial`、可选公共目标点 `GoalCm` 与 `AcceptanceCm`、`bEngaged`、`bProtectionRemoved`、`LifeRevision`；只发给拥有者，不含隐藏坐标。 |
| `FLDOrderCaller`（仅服务器，不序列化） | `FLDMatchContext Match`、`FLDCombatBodyRef Body`、`Controller`。 |
| `ELDOrderCancelReason` | `Stop/FocusLost/LifeEnded/Left/MatchClosed`。 |
| `SubmitOrder(Caller, Request) -> FLDOrderReceipt` | 真人 RPC 与 AI 共用的收件入口；只校验、去重、排队，返回 `Queued` 或前置 `Rejected`，不激活、不解除保护。 |
| `CancelOrder(Caller, Identity, Reason) -> FLDOrderReceipt` | Stop 语义：Order 与 Epoch 都必须前进；排入待受理槽，在 Actions 阶段结束当前订单。 |
| `SubmitCast(Caller, Request, Cast) -> FLDOrderReceipt` / `ServerSubmitCast` | 对单位或地面点施放订单收件（§4.5）；不带 Shift 时未就绪即拒绝（§4.6）；普通 `SubmitOrder` 拒绝 `CastUnit/CastPoint`。 |
| `QueryOrder(Caller, Identity) -> FLDOrderReceipt` | 只读：返回窗口内的回执，已被逐出的旧身份为 `Expired`。 |
| `QueueAction(Caller, Request, AimCm) -> FLDCombatActionReceipt` / `ClearQueue(Failure, BeforeStep)` | Shift 技能收件（§4.4）；清空未开始的排队步骤（给出 `BeforeStep` 时只清该序号之前排的）。 |
| `RefuseUnreadyDirect(Request, Out)` / `AwaitDirectVerdict(Sequence)` | 不带 Shift 的 Direct 技能：队列非空时未就绪即在 Combat 之前拒绝；交给 Combat 的等受理后才清之前的步骤（§4.6）。 |
| `ProcessAuthorityOrders(Permit, Error)` | Actions 阶段推进 Shift 队列、激活待受理订单并推进当前订单（追击、搜敌、普攻申请）。 |
| `AdvanceAuthorityMotion(Step, Body, Permit, Error)` | Motion 阶段把转向输入交给 `ULDCharacterMovementComponent::AdvanceAuthorityMovement`。 |
| `EndLifeOrders(Reason)` / `ResetMatch()` | 死亡、离开、终局结束订单；换局时丢弃整个订单簿与内容缓存（回执序号保留）。 |
| `ServerSubmitOrder` / `ServerStop` / `ServerQueueCombatAction`（可靠）、`ServerHeartbeat`（不可靠）、`ClientOrderReceipt`（可靠） | 拥有连接 RPC；只负责解出调用者并转交上述入口。 |
| `FLDCombatActionRequest` / `FLDCombatCancelRequest` | Direct 请求（Generation、Life、`DirectSequence`、Entry、Target、`ExpectedSourceRevision`）与按 `ThroughDirectSequence` 的取消；经 `ServerSubmitCombatAction/ServerCancelCombatAction` 到 `ALDCombatDirector::SubmitAction/CancelAction`。 |
| `FLDCombatRequestKey` | Combat 所有的判别键：`Direct` 由拥有者 DirectSequence 构成；`Nav` 只由服务器 Navigation 订单（MoveIdentity + AttackCycle）构成。 |

普攻没有单独的动作交接或结果回调：Navigation 每个 Actions 阶段读取 Combat 的身体状态，
在就绪且在射程内时以新的 `AttackCycle` 申请下一次普攻；Combat 自己分配动作身份。
Q/W/E/D 的 Direct 请求不替换当前空间订单；不带 Shift 的会清空 Shift 队列（§4.4）。

### 5.3 命令、版本、结果与去重

| 命令 | 校验 | 回执与拒旧 |
| --- | --- | --- |
| MoveGround / AttackMove | 调用者、Match/Life、身份新鲜性、形状；激活时 AttackMove 先解保护；再验地面、支持面与完整路径 | `Queued` ≠ `Activated`；路径失败为 `InvalidGround/NoCompletePath`，原订单继续。 |
| AttackUnit | 同上；激活时先解保护，再重查观察句柄并绑定 | 句柄不可用统一为 `TargetUnavailable`，不泄露原因。 |
| Stop | 调用者与 Order、Epoch 同时前进 | 当前订单 `Cancelled/Stopped`；Stop 本身回 `Cancelled`。 |
| Heartbeat | 当前已接受身份且序号递增 | 旧序号不延寿；租约过期 `Expired`。 |
| 查询 | 当前调用者与当前订单簿 | 窗口内回执或 `Expired`，不重新执行。 |

去重域为“调用者 + 当前局/生命 + 订单身份”。回执窗口最多 16 条，当前与待受理身份永不被逐出；
拥有客户端按 `ReceiptSerial` 和当前 Match/Life 过滤回执，旧回执不覆盖新状态。
S 的 Combat 取消按本地已发出的最大 `DirectSequence` 提交，覆盖服务器尚未收到的请求；
Direct 与 Nav 两种序号域互不比较。

### 5.4 拒绝与可观测反馈

`ELDOrderFailure`：`None`、`InvalidRequest`、`NotOwner`、`StaleMatch`、`StaleLife`、`StaleOrder`、
`RequestConflict`、`SequenceExhausted`、`InvalidGround`、`NoCompletePath`、`PathBlocked`、
`TargetUnavailable`、`TargetDied`、`Dead`、`MatchEnded`、`Superseded`、`Stopped`、`Expired`。
每个失败码在 `LDNavigationComponent.cpp` 的 `FailureText` 中有中文提示。Combat 的资源/冷却/来源拒绝
沿 Combat 回执由 HUD 呈现，不在操作层复制校验。AI 读取同一失败码决定是否重试。

## 6. 生命周期与竞态矩阵

| 事件 | 本地输入/镜头 | 权威订单/动作 |
| --- | --- | --- |
| Esc | 按 §3.1 每次关一层 | 不 Stop、不发命令。 |
| S | 清准备与预输入，停止本地预测，新 Epoch | 结束当前订单；Combat 取消至该 DirectSequence 的可中断动作。 |
| 合法新订单 | 新标记与“已发送”状态 | 激活时原子替换，原订单 `Superseded`。 |
| 无效新订单 | 显示失败提示 | 原已接受订单继续；攻击类意图仍已解除保护。 |
| 死亡 / 新生命 | `ResetLife` 清准备、指针与路线，生成新身份 | `EndLifeOrders` 结束旧生命订单；旧生命请求为 `StaleLife`。 |
| 目标失去视野 | 悬停圈与攻击标记消失 | 结束订单（`TargetUnavailable`），不转向最后位置。 |
| 目标授权死亡 | — | AttackUnit `Completed/TargetDied`；AttackMove 重新搜敌或续行。 |
| 来源变化 | 撤销准备态 | Combat 按 `ExpectedSourceRevision` 重验。 |
| 失焦 | 清按键，Stop，停止心跳与指针 | 当前订单结束。 |
| 菜单/面板/子确认 | 只阻止新世界命令 | 已接受订单继续，心跳继续。 |
| 心跳中断 | — | 租约过期，`Expired`。 |
| 离开 | — | 订单以 `Left` 结束。 |
| 终局 / 新局 | 关闭世界输入 | `ResetMatch` 结束订单并重建订单簿；旧局请求为 `StaleMatch`。 |

## 7. CMC、世界查询与读取边界

### 7.1 运动执行角色

| 身体角色 | 运动接线 |
| --- | --- |
| 拥有客户端 AutonomousProxy | 按已接受回执本地规划并 `AddMovementInput`；saved move 携带 `FLDMoveIdentity`，服务器纠正后按原生方式重放。 |
| 服务器上的远程真人身体 | Motion 阶段由 `AdvanceAuthorityMotion` 供给转向输入，`AuthorizesMove` 只放行当前已接受且租约有效的身份。 |
| Listen Host 自己 / 服务器 AI | 不做本地预测；与远程真人同样由服务器订单驱动，AI 订单由 `ALDOpponentController` 经 `SubmitOrder` 提交。 |

`ALDCharacter`（`Source\Lockdown\Private\Character\LDCharacter.cpp`）的 CharacterMovement
`MaxAcceleration` 与 `BrakingDecelerationWalking` 均为 8000，`bOrientRotationToMovement` 关闭；
朝向由 `SetFacing` 瞬时设置为移动方向。最大速度来自 Combat 的移动速度投影。
D 冲刺经 `ULDCharacterMovementComponent::StartDemoDash` 的 root-motion source，不逐帧传送，遇阻挡由原生 CMC 停在最远合法位置
（`Lockdown.Rules.DemoMotionProbe.DashStopsAtWall`）；起步方向立即被静态几何挡住时 `CanStartDash` 拒绝；
定身只停自愿转向，不停重力与已提交的 root motion。
**身体互相阻挡（#97，用户原话：“right now characters walk through each other without collision, add collision”）：**
参赛者、营地野怪与 Boss 的胶囊都阻挡彼此（`ECC_Pawn` 不再忽略），服务器运动停在别的身体前或沿其滑开；
D 冲刺仍穿过身体（冲刺路径减速要命中路径上的敌人），只在冲刺自己的扫掠期间忽略身体（`ULDCharacterMovementComponent::IsDashing`）。
远程客户端上，当前呈现的可见单位代理带同尺寸的仅查询胶囊，只阻挡本人预测的身体，让预测停在服务器会停的地方；
Host 世界里已有权威身体，代理不加碰撞。迷雾中看不到的身体只在服务器上阻挡，预测撞上时由服务器纠正。
点地移动的目标点已被别的身体占住时（2026-11-07 用户选择“Yes, stop quietly”），身体碰到占点的一群身体就算到达，安静停下，
不推挤到 `PathBlocked`：离点太近、本人站不上去的身体占点，碰着占点身体的身体也算这一群
（`LDPathSteering::BlockedAtHeldGoal`，规则测试 `Lockdown.Rules.DemoPathSteering.HeldGoal`）。

### 7.2 世界接口，操作层不得复制实现

| 接口 | 操作域用途 |
| --- | --- |
| `ULDMapRegistry::QueryGround` | 身体支持面：取身体原生 `CurrentFloor` 并映射到注册地面，不使用订单里的 hint；无合法 floor 为 `NotGrounded`。 |
| `ULDMapRegistry::QueryCompletePath`（经 `LDPathSteering::PlanRoute`） | 地面与追击路线；partial path 明确失败。 |
| `ALDVisionDirector::ResolveObservedUnit` | 新攻击订单、每次 AttackStart 与 A 候选的当前观察重查。 |
| `ALDVisionDirector::EnumerateVisibleUnits` | A 的当前可见候选帧。 |
| `ALDVisionDirector::BindObservedOrder` / `FindObservedTermination` | 订单与观察的绑定；授权观察到的死亡记录。 |

Demo 只有一个注册地面，`GroundSurfaceHint()` 返回它的 SurfaceId；客户端点击与小地图都携带该 ID，
服务器重新校验，hint 不能证明合法落地。

### 7.3 实际运动与喷漆区判定

权威运动在核心 Motion 阶段执行：`ULDCharacterMovementComponent::AdvanceAuthorityMovement` 在
`BeginNativeMotionCapture/FinishNativeMotionCapture` 范围内运行原生 CMC，产出不可变的
`FLDNativeMotionBatch`（`Source\Lockdown\Public\Character\LDNativeMotionTypes.h`）：
起止脚点、支持面、逐条 Movement/Adjustment/Support 记录及被回退的试探次数。
批次由 `ALDGameMode` 接收（`PublishCaptureMotion`）并交给喷漆与守护归约（`FLDCaptureReducer`），
已消费的批次在下一步开始前经 `ReleaseConsumedMotion` 释放。

核心步内阶段顺序为 Motion → Session → Actions → Effects → DeathsAndRewards → RespawnsAndWorld →
Transactions → Capture → Close → Publish（`Source\Lockdown\Private\Combat\LDCombatDriver.cpp`）。
Actions 内先处理 Direct 动作，再推进各参赛者订单。喷漆区判定、事件时间与水位规则见
[会话与比赛生命周期](SESSION_MATCH_LIFECYCLE.md)。

### 7.4 读取白名单

| 消费者 | 允许的数据 |
| --- | --- |
| 自身 HUD/按钮 | 自身 owner state、Combat owner view、订单回执。 |
| 悬停/光标/检查/准备确认 | 只读本局本生命的当前观察帧（`ALDWorldViewRelay::ReadObservedFrame`）；帧不再当前或单位不可见时检查对象被清除并提示“目标已不可见”。 |
| 指针标记 | 自己的订单点与当前观察帧中的目标脚点。 |
| 小地图/镜头 | 静态地图、公开地点与合法可见单位；平移/缩放不影响视野。 |
| AI | 经与真人相同的订单与动作入口；提交仍在服务器重新解析权限。 |
| 死亡镜头 | 静态地图与公共信息；不跟随、不观察隐藏玩家。 |

## 8. 参数、单位与单一来源

玩法值与公式只在 `CONTENT_AND_BALANCE.md` 维护，经 SourceData → 生成载荷进入游戏；本文不复制数值。

| 内容键 | 本模块消费方式 |
| --- | --- |
| `UNIT.MoveSpeedCmPerSec` | 经 Combat 投影成为 CMC 最大速度。 |
| `STAT.AttackRangeCm` / `FORMULA.AttackRange` | AttackUnit/AttackMove 的射程，每次从 `ReadCombatBody` 读取当前值。 |
| `TARGET.AttackMoveAcquireRadiusCm` | A 搜敌半径（内容公式按视野半径与当前射程计算）。 |
| `VISION.RadiusCm` | 视野服务与 A 搜敌半径公式。 |
| 各 `ABILITY.*` 的 `TargetMode` 等字段 | 决定 Q/W/E 的目标模式与几何。D 冲刺读 `character.dash`。 |

**本地设置**（`ULDControlSettings`，只影响本机呈现，不影响世界距离、视野或角色速度）：

| 字段 | 默认值 | 合法域 |
| --- | --- | --- |
| `Bindings` | `Definitions()` 的默认键 | 无冲突，Esc 保留给 Back |
| `bEdgePan` | 开 | — |
| `bStartFollowing` | 开 | — |
| `Zoom`（镜头臂长 cm） | 2250 | 1300–2400；旧 schema 5–11 按原 1100–1700、schema 12 按原 1100–1900、schema 13 按当前范围校验，低于 1300 的调整为 1300，其余原值迁移 |
| `TextScale` | 1 | 1–1.25 |
| `CameraPanCmPerSec` | 1800 | 900–3600 |
| `MinimapOrientation` | `Camera` | `Camera/Facing/North`（HUD 小地图使用，`LDMinimap::MakeView`） |
| `bQuickCast` | 关 | — |
| `MasterVolume` | 1 | 0–1 |

**工程常量**（不是产品数值）：订单激活节流与追击重算间隔 0.1 s、无进展 2 s、目标位移重算阈值 50 cm、
地面到达容差 40 cm、回执窗口 16 条（`LDNavigationComponent.h`）；客户端心跳 0.5 s、租约 2 s
（`LDCommandLiveness.h`）；按住移动 0.125 s / 40 cm（`LDOrderRules.h`）；预输入保留 1 s
（`LDCombatPreInput.h`）；边缘平移带 8 px、滚轮 80 cm/格；指针从屏幕反投影，拾取当前授权代理实际摆姿的模型三角面，
不再使用中心 32 px 圆。头、脚和伸出的手臂均可选；遮挡视线的静态几何优先，重叠模型取最近表面。
没有任何模型表面在指针下时，按已批准的手感决定（“模型 + 脚下光圈 + 屏幕上模型周围 ~12px 容差都算点中，多个候选取离光标最近的”）
退回宽松拾取：每个代理的拾取体为从显示脚底到头顶、半径等于根环足迹 52.5 cm 的竖直胶囊，射线离它不超过
12 px（`LDModelPicking::PickTolerancePx`，按水平 FOV 与视口宽度换算成角度）即可选，取角距离最小者；
模型表面命中始终优先，Sight 遮挡同样截断宽松拾取。
hover、检查、右键攻击、普通确认与快速施法共用此入口（`ALDPlayerController::ObservedUnderPointer`）；
地面/攻击标记显示 0.45/0.5 s，红叉 0.6 s（`LDPointerFeedback.h`）。`LD_COMBAT_INPUT` 日志区分预输入、已发送和服务器回执，不把客户端提交当作命中。

## 9. 当前组件地图

### 9.1 订单与指针

`Source\Lockdown\Public\Navigation\` 与 `Source\Lockdown\Private\Navigation\`：
`LDNavigationComponent`（客户端意图、预测、指针、镜头）、`LDNavigationOrders.cpp`（服务器订单簿与推进）、
`LDOrderTypes.h`、`LDOrderRules`、`LDPathSteering`、`LDPointerFeedback`、`LDDemoNavigationSystem`。

### 9.2 输入、战斗意图与设置

`Source\Lockdown\Private\Framework\LDPlayerInput.cpp`（映射、按键、Esc、缩放、单位拾取、检查）、
`LDPlayerOrders.cpp`（Q/W/E/D、A、S、快速施法、预输入、提示）；
`Source\Lockdown\Public\Combat\LDCombatPreInput.h`；
`Source\Lockdown\Private\UI\LDControlSettings.cpp`、`LDControlsPanel.cpp`、`LDMinimap.cpp`。

### 9.3 运动、世界与观察

`Source\Lockdown\Private\Character\LDCharacter.cpp`、`LDCharacterMovementComponent.cpp`；
`Source\Lockdown\Public\Region\LDMapRegistry.h`；`Source\Lockdown\Public\Vision\LDVisionDirector.h`；
`Source\Lockdown\Private\Combat\LDCombatDriver.cpp`（阶段调度）；`Source\Lockdown\Public\AI\LDOpponentController.h`。

### 9.4 不存在的旧部分

当前代码没有：静态 IA/IMC 输入资产与 Bootstrap 输入生成、控制设置 profile 或 Tactical/Hybrid 默认集、
按住加速的旧 Sprint、Skill3+/Item3+ 槽、左键开店或拾取、方向普攻式的 A、“接近但不追击”的旧订单、
单独的订单视图类型或动作结果回调。退役范围以
[架构 §3.2](ARCHITECTURE.md#32-删除清单与防回归约束) 为准，不得恢复。

## 10. 验证

### 10.1 规则测试

经 `Scripts\RunTests.ps1`（套件 `Lockdown.Rules`）运行：

| 用例 | 覆盖 |
| --- | --- |
| `Lockdown.Rules.DemoControlFeel.HeldMove` | 按住右键的重复频率与位移门槛。 |
| `Lockdown.Rules.DemoControlFeel.PreInput` | 预输入入口与前摇判定。 |
| `Lockdown.Rules.DemoControlFeel.SourceBuffer` | 普通投影更新保留预输入，来源或局变化取消旧目标。 |
| `Lockdown.Rules.DemoControlFeel.ModelPicking` / `PresentedModelPicking` | 三角面边界、空隙、正反面与深度；宽松拾取体的穿过、容差内外、身后与遮挡截断；分块拾取在缩放/剪切骨骼与权重不足下包围盒保守、结果与整网格追踪一致、射线外的块不蒙皮；真实导入骨骼模型的分块蒙皮与渲染器 CPU 蒙皮一致、站立/攻击/施法头脚与侧缘拾取与整网格追踪同距、包围盒外的近距离射线不蒙皮、拾取体与模型身高一致、隐藏和倒地拒绝。 |
| `Lockdown.Rules.DemoOrders.AttackMoveCandidate` | A 搜敌的最近者、半径与平局排序。 |
| `Lockdown.Rules.DemoOrders.BookIsolation` | 订单簿按局/生命隔离与重置。 |
| `Lockdown.Rules.DemoOrders.ChasePress` | 普攻追击不发呆：到点出手的普攻不再占住订单，射程内等节拍时目标走开即贴近。 |
| `Lockdown.Rules.DemoControlFeel.AttackClick` | A 后左键点单位等同右键攻击该单位，点地面为攻击移动（2026-11-07）。 |
| `Lockdown.Rules.DemoControlFeel.RefusedSkillKeepsOrder` | 未就绪的技能请求被立即拒绝而不动当前订单与队列；前摇只延后；被拒的技能步骤不清队列（2026-11-07）。 |
| `Lockdown.Rules.DemoControlFeel.AutoAttackAfterCast` | 施法跟进目标和回执规则；排队、拒绝、S 和新生命下按已激活的攻击追踪目标；自动攻击拒绝不重复提示。 |
| `Lockdown.Rules.ControlMappings` | 唯一动作注册表与绑定。 |
| `Lockdown.Rules.Minimap.*`、`Lockdown.Rules.LocalSettingsMinimapOrientation` | 小地图朝向与设置。 |

### 10.2 拥有连接场景

`Scripts\RunDemoCombatCheck.ps1`（参数 `EngineRoot`、`PackageRoot`、`TimeoutSeconds`，
可选 `NetEmulation` 传入引擎丢包/延迟模拟）启动一个参赛的 Listen Host 与一个独立拥有客户端。
Host 必须报告检查组 Net、Start、ChaseGoal、AttackMove、Stop、Death、Respawn、Protection、Skills、
CastFollowUp、Render、Feel、Pointer；Client1 报告 Net、Start、Settings、AttackMove、Stop、Death、DeathCamera、Respawn、
Protection、Skills、CastFollowUp、Feel、Queue。CastFollowUp 组核对 Listen Host 的单位施法跟进与同步拒绝，
以及真实拥有客户端在已激活攻击、排队的失效目标和被拒施法之后，方向施法恢复原攻击。
Pointer 组验证悬停圈、地面标记与攻击标记，并保存
`HostSees-MoveMarker.png`、`HostSees-Hover.png`、`HostSees-AttackMarker.png` 供人工审阅（不做像素比对）。
输出写入忽略的 `Artifacts`。

### 10.3 实测手感

本地回环、加减速 8000 下（2026-11-04，编辑器版连跑三次）：首次右键行走等开局头几帧的一次性卡顿过去后下单，
Host 右键到开始移动 31–35 ms（4–5 帧，最长一帧不超过 15 ms），再次右键 31–36 ms；
Client1 右键到开始移动 32–37 ms、到 Activated 回执 14–17 ms、加速到全速 42–58 ms、S 到 Stopped 回执 9–20 ms。
开局头一两秒内本进程会有一次约 100–150 ms 的一次性长帧（加载并构建开局显示的内容），此时下单要多等这一帧；
旧测法在出生首帧下单，曾把这一帧算进 Host 的点击响应（打包版 159 ms，[#61](https://github.com/rlsn/Lockdown-alpha/issues/61)）。

## 11. 产品规则来源与限制

§4.2 的搜敌、平局与续行，失视野即停止追击、无单位 LastSeen 标记，死亡静态自由镜头，
发出进攻意图即解除保护（失败也解除），以及引用的首版内容数值均已获用户批准；
批准不等于已完成平衡测试。

当前限制：

- 没有独立的逐手势捕获结构；跨菜单/失焦/重建映射的手势由 fresh-press 门槛处理，
  小地图拖动依赖 Slate 鼠标捕获。
- Expired 只作为失败码出现，不是回执状态。
- 网络模拟下的手感与 8000 加减速下的手感未重新测量。
- 完整 3 真人 + 3 AI 的验收由其他检查入口负责，见[开发指南](../DEVELOPMENT.md#最小验证选择)。

## 12. 已读取的公开引擎资料

以下 Epic 官方资料说明所用的通用引擎机制，不替代 Unreal 5.8.4 下的编译与运行验证。

| 来源 | 使用范围 |
| --- | --- |
| [Epic：Networked Movement in CharacterMovement](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine) | saved moves、自定义网络 move、authority 重现/客户端 correction、root-motion source。 |
| [Epic：Enhanced Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine) | LocalPlayer context、动作触发阶段、上下文优先级和运行时重绑。 |
| [Epic：FInputModeGameAndUI](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FInputModeGameAndUI) | UI 未处理的输入仍可进入玩家输入，因此需要显式的指针/捕获检查。 |
| [Epic：OnCharacterMovementUpdated](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Character/OnCharacterMovementUpdated) | 运动更新末端事件；首末位置不能代表所有中间子段。 |
| [Epic：FScopedMovementUpdate](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FScopedMovementUpdate) | 延迟提交/回退；被回退的试探运动不计入实际运动。 |

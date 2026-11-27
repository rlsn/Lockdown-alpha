# 移动、输入与镜头

本页记录输入路由、施法瞄准与预输入、空间寻路与坡道转向、角色位移/冲刺、俯视镜头及本地控制/显示设置（`LDPlayerController`、`LDNavigationComponent`、`LDCharacterMovementComponent`、`LDCameraRigComponent`、`ULDControlSettings`、`LDCursor`）。

## 输入层优先级与鼠标/键盘分工

本地输入按严格单向优先级消费，禁止跨层穿透：
1. **模态与主面板层**：局内菜单、设置页、全屏构筑工坊（`B` 切换；`Esc` 逐层关闭子确认 → 悬浮预览 → 主面板 → 打开局内菜单）。阻塞面板开启期间抑制世界点击与技能释放，已接受的服务器移动订单继续执行，关闭时不重放面板期间按下的输入。
2. **HUD 指针交互层**：技能栏点击、属性栏左键加点/右键或滚轮退点、迷你选卡面板（`1/2/3` 或点击；`Tab` 收起/展开待选卡，长按 `Tab` 呼出战术计分板）、小地图左键观察镜头 / 右键寻路或单位锁定。
3. **施法瞄准与快速施法层（`Q / W / E`、`D`）**：
   - 默认开启快速施法（`bQuickCast = true`，`ULDControlSettings::CurrentSchema = 25`）。
   - **键盘双模态释放（`QuickCastHolds[3]` 三槽独立跟踪）**：按下 `Q/W/E` 开始计时；按住 `< 0.18 s` 松开视为**轻点（Tap）**，不弹瞄准预览圈、不改瞄准光标、忽略轻点期间右键走位取消，松键瞬间朝当前光标位置/目标施放；按住 `>= 0.18 s` 进入**长按预览（Hold）**，显示世界射程与几何预览，松键施放，按右键（`CancelPreviewedQuickCastHolds`）仅取消已进入预览的槽位、播放 `UIReject` 并闪烁槽位边框且不吞掉该次右键移动，同时恢复其余仍在按住槽位的预览。
   - 关闭快速施法或鼠标点击 HUD 技能槽时，走经典两步（先 `Armed` 预览，左键确认、右键或 `Esc` 取消）。
   - **爆劲蓄力（Overdrive）**：劲满 `OVERDRIVE.Threshold`（60）时按住技能满 `OVERDRIVE.HoldSec`（0.35 s）蓄成爆劲，HUD 动作槽与角色脚下同步显示蓄力环；单位技能目标解析失败或 `CombatView` 定义变更时独立清理 `OverdriveEntry`。
4. **移动与攻击移动层**：右键地面 `MoveGround`（按住右键每 `50 ms` 节流重发，并在显式新单、排队施法、`S` 停止、换命与立即施法时失效合并缓存 `InvalidateClickMoveCoalesce`）、右键敌方 `AttackUnit`、`A` + 左键地面 `AttackMove`（搜敌半径 `min(VISION.RadiusCm, 2 * AttackRangeCm)`）、`S` 立即停止移动与未出手动作。

## 高低差瞄准、楼顶排除与双槽预输入队列

- **可行走地面层隔离与障碍投影（`ULDMapRegistry` / `LDPathSteering`）**：`TerrainFloors` 仅收纳合法地面、坡道与可踏上低矮铺层，显式阻挡体（墙体、建筑、树木）与无坡道连通的高空屋顶绝不进入地面支撑或指针射线（`TraceGround` / `GroundUnderPointer`），杜绝选中或站立楼顶；世界与小地图右键点击建筑、树木或边界内侧时统一解析到下方可行走地面层，经 `ProjectGroundGoal` 自动投影至连通可达的最近地面导航点。
- **角色脚平面等高投影（`AimPointAtElevation`）**：方向型（`Direction`）、直线型（`Line`）与扇形（`Cone`）技能在把屏幕光标射线转为水平瞄准角前，先与施法者当前脚底平面 `Z = FootLocation.Z` 求交（仅射线近平行时回退地形命中点），消除高台/坡道视差导致的斜向偏射；地面落点型（`GroundPoint`）仍取真实可行走地面交点。
- **全模式双槽 FIFO 预输入（`450 ms`）**：客户端与服务端均维护最多 **2 步**、有效期 **450 ms** 的到达序 FIFO 技能队列，覆盖全部目标模式（`Self`、`Unit`、`Direction`、`GroundPoint`、`Cone`、`Line`），支持前摇/收势/位移期间无缝滚键盘连招，也支持 `Shift` 显式排队。前序动作完成或取消即按序弹出下一步校验执行；死亡、换局、`S` 停止或过期自动清空并回执。

## 寻路、坡道转向与冲刺物理

- **订单生命周期（`ULDNavigationComponent`）**：每次空间订单绑定 `(Generation, ContenderId, Life, Sequence)` 与订单租约心跳；单位目标进入视野宽限（`0.3 s`）保持追踪，彻底失视野立即终止追击且不暴露隐藏坐标。
- **3D 坡道拐点推进（`LDPathSteering::ShouldAdvanceCorner`）**：平坦地面允许沿法平面提前切弯；一旦前后路径段存在高度差（`|ΔZ| > 15 cm`）或当前站立面法线倾斜（`ImpactNormal.Z < 0.999`），强制要求水平误差 `<= 28 cm` 且垂直误差 `<= 35 cm` 才推进下一拐点，防止坡顶/桥口切角卡死在侧翼挡墙。
- **冲刺手感与贴脸防滑（`LDCharacterMovementComponent`）**：
  - 主动冲刺（通用 `D` `450 cm / 0.18 s` 与位移技能）使用复制根运动源 `FLDRootMotionSource_EaseOutDash`，位移曲线为 `EaseOutCubic`（`1 - (1 - t)^3`），初段爆发、末段柔顺刹停；击退保持线性常力 `FRootMotionSource_ConstantForce`。
  - **位移落点（`LandedDashDistance`）**：冲刺、击退、拉拽与野怪扑击统一经 `StartDemoDash`，起步前按地面技能同一规则（`LDGroundLanding::Resolve`）把距离截到“合法地面且有完整步行路线回到起点”的最远处，速度不变、提前结束；沿途没有合法地面则原地不动。根运动仍由原生扫掠挡墙，这一步防止冲上岩顶、台地或越过围栏落到步行与寻路无法离开的地方。
  - **贴脸防穿模截断（`ClampInnateDashDistance`）**：朝 `45°` 锥角内最近可见敌方冲刺时自动保留 `135 cm` 普攻舒适身位；已在 `<= 135 cm` 内时跳过零距离根运动但正常结算主动冲刺触发与强化普攻。
  - **冲刺中指令保留**：主动冲刺期间下达的右键移动、普攻追击或射程外目标订单（含暂态 `InvalidGround` / `NoCompletePath`）不丢弃，保留至冲刺落地帧立即重规划并衔接出手。

## 俯视镜头与语义光标

- **俯视镜头（`LDCameraRigComponent`）**：俯角 62°、水平 FOV 55°，刚性居中跟随；镜头距焦点为玩家缩放值的 `LDCamera::ArmScale = 1.2` 倍（缩放值 13–24 m、默认 22.5 m，对应臂长 15.6–28.8 m、默认 27 m）。固定俯角旋转（不随角色朝向转动），支持滚轮平滑缩放（`CameraZoomLevel` 持久化）、屏幕边缘推屏、按住空格临时居中本人、**`Y` 键切换锁定跟随（`Follow`）**、小地图左键拖拽观察（松开不弹回，按空格或 `Y` 回本人）；死亡期间保持静态世界观察权限，不赋予额外活体视野。
- **四态硬件/软件光标（`LDCursor`）**：按优先级实时切换 `Disabled`（不可达地面/硬控禁动）、`CastAim`（技能预览瞄准中）、`AttackHover`（悬停合法可见敌方，配 `1.5 px` `#FF4D4F` 敌方描边支架）与 `Default`。可达性探测严格按 `>= 120 ms` 节流；光标的地面落点、本人位置和已登记的世界都没变时沿用上次的答案（最多 1 s），不重新找路。

## 本地设置与显示

- **控制与界面存档（`ULDControlSettings`）**：保存于 `Saved/ControlSettings.json`（原子写临时文件 + 回读校验，支持 `-LDSettingsPath` 隔离多开实例），当前 `CurrentSchema = 25`。覆盖全部动作键位（冲突键自动互换绑定，禁止半套残留）、快速施法开关（默认开）、镜头缩放与推屏参数、主音量与分类音量、界面缩放、色盲辅助及特效强度（100% / 60% / 30%）。
- **显示模式持久化（`ApplyAndSaveDisplaySettings`）**：支持无边框全屏、窗口化与独占全屏，分辨率列表与帧率上限（60 / 120 / 144 / 240 / 不限），写入引擎 `GameUserSettings.ini` 后调用 `VerifyDisplaySettingsFile` 逐项回读核对 `FullscreenMode`、`ResolutionSizeX`、`ResolutionSizeY` 与 `FrameRateLimit`，不一致即报 `ReadbackFailed`；设置面板关闭或销毁绝不隐式提交未应用的显示草稿。

## 检查入口

- 纯规则：`Lockdown.Rules.DemoOrders.*`、`Lockdown.Rules.DemoControlFeel.*`、`Lockdown.Rules.DemoMotionProbe`、`Lockdown.Rules.LocalSettings*`、`Lockdown.Rules.LocalControlRegistryIsolation`、`Lockdown.Rules.LocalDisplayPersistenceReadback`。
- 拥有连接场景：`Scripts/RunDemoCoreCheck.ps1`、`Scripts/RunDemoCombatCheck.ps1`。

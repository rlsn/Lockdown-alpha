# Demo 操作、目标选择与订单实施报告

**目标：v0.10.0 Demo · 模块：M3 · 引擎：Unreal 5.8.4**

本文落实 [App Spec](../APP_SPEC.md) §3.1，并对接
[架构](ARCHITECTURE.md)、[共享契约](SHARED_CONTRACTS.md) SC-01–09 和
[实施计划](IMPLEMENTATION_PLAN.md) I2/I3/I5。它不是第二份产品规格，也不是已实现 API 手册。
**本轮只改文档。** 下列新增、改造、删除与验收均为后续实施任务。
玩法确认以 Spec 和明确批准的决定为准；文中“拟建”“建议”均不能作为已完成或已批准的证明。

## 1. 结论、范围与证据

保留现有 **Enhanced Input → PlayerController → Navigation → CharacterMovement/saved moves**
和服务器 Combat 路径，补齐一条有界当前订单、目标权限重查、动作交接及输入所有权。
真人通过拥有连接提交意图；AI 通过服务器绑定的参赛身份调用同一验证入口，
不伪造 LocalPlayer、PlayerController RPC 或本地输入。

本模块负责右键移动/追击/商店接近、A 攻击移动、四类目标准备与确认、Q/W 与 1/2 路由、
取消/失效、镜头和可保存绑定。战斗域写动作、资源、节拍、命中与效果；
世界域写几何查询与观察权限；生命周期域写身体资格、真实时间、占领和终局。
不增加连招、指令队列、多单位框选、编队、团队命令、自由旋转镜头或新的施法模式选项。

源码基线为 `fa11d09a762bccadf495191072e222dd158fc014`。
本工作区继承的文档提交为 `7e7a40d39ec2f99f18b809b83e89479bd34a823d`；
Source、Config、Content、SourceData、Scripts 与源码基线无差异。
以下是源码/文档静态证据，不是 Unreal 构建、资产加载、网络体验或完整 Demo 的运行证明。

| 证据 | 已核验入口 | 当前事实与目标缺口 |
| --- | --- | --- |
| E01 | [LDNavigationComponent.h:7–54](../../Source/Lockdown/Public/Navigation/LDNavigationComponent.h#L7-L54)、[cpp:96–151](../../Source/Lockdown/Private/Navigation/LDNavigationComponent.cpp#L96-L151) | `FLDMoveIdentity` 的 Match/Life/Order/Epoch 均为 `uint32`；已有 Navigate/Stop/ResetLife、ServerOrder/ServerStop/ServerHeartbeat/ClientOrder。Navigate 仍要求本地控制器，不能直接作为服务器 AI 入口。 |
| E02 | [LDNavigationComponent.cpp:161–230](../../Source/Lockdown/Private/Navigation/LDNavigationComponent.cpp#L161-L230)、[237–319](../../Source/Lockdown/Private/Navigation/LDNavigationComponent.cpp#L237-L319) | 已有解析、完整路径/接近点、接受/到达、心跳与失活；目标接近不是完整的目标追击状态机。现有 `AuthorizesMove` 的 `!Enabled()` 放行必须随旧模式门槛删除，不成为 Demo 的免校验路径。 |
| E03 | [LDCharacterMovementComponent.cpp:22–127](../../Source/Lockdown/Private/Character/LDCharacterMovementComponent.cpp#L22-L127)、[129–160](../../Source/Lockdown/Private/Character/LDCharacterMovementComponent.cpp#L129-L160) | 已有 saved move/自定义网络 move、身份相同才合并、MoveAutonomous 权限检查；StartAuthorityDash/StartContentDash 使用原生 root-motion source。仍混有 sprint/B91 耦合；没有目标占领所需的完整运动事件/水位。 |
| E04 | [LDPlayerController.cpp:147–191](../../Source/Lockdown/Private/Framework/LDPlayerController.cpp#L147-L191)、[LDPlayerInput.cpp:65–104](../../Source/Lockdown/Private/Framework/LDPlayerInput.cpp#L65-L104)、[139–234](../../Source/Lockdown/Private/Framework/LDPlayerInput.cpp#L139-L234)、[270–291](../../Source/Lockdown/Private/Framework/LDPlayerInput.cpp#L270-L291) | 静态 IA 加载与 `RebuildV05Mappings` 动态注册并存。右键主要走 ClickMove，没有 Demo 的单位/商店/地面分派；商店接近还在左键服务路径。当前 A 是准备直接攻击，不是攻击移动。 |
| E05 | [LDControlSettings.cpp:7–92](../../Source/Lockdown/Private/UI/LDControlSettings.cpp#L7-L92)、[h:12–34](../../Source/Lockdown/Public/UI/LDControlSettings.h#L12-L34) | 有 Bindings、Rebind/Valid/Save、Schema/Profile、bEdgePan/bStartFollowing/TextScale/Zoom；有 Defaults/TacticalDefaults/HybridDefaults。不能仅改 HUD 字母而继续保留旧 profile 注册表。 |
| E06 | [LDTacticalRequest.h:7–32](../../Source/Lockdown/Public/Combat/LDTacticalRequest.h#L7-L32)、[LDContentActions.cpp:41–117](../../Source/Lockdown/Private/Combat/LDContentActions.cpp#L41-L117)、[LDContentLifecycle.cpp:35–42](../../Source/Lockdown/Private/Combat/LDContentLifecycle.cpp#L35-L42) | 请求已有 TargetMode、来源/修订、Observed/Direction/Point 等字段；已有 Self/Unit/地面/方向相关执行和重复普攻。仍包含额外目标类别、旧服务取消和自动提示，不是统一四模式注册表或完整 A/右键追击。 |
| E07 | [LDVisionDirector.cpp:117–186](../../Source/Lockdown/Private/Vision/LDVisionDirector.cpp#L117-L186)、[LDWorldViewRelay.cpp:96–128](../../Source/Lockdown/Private/Vision/LDWorldViewRelay.cpp#L96-L128) | 已有 CanObserve/CanSee/ResolveObserved、逐观察者句柄、快照/Conceal 游标。新命令和持续订单都要重查，不保存隐藏 Actor 作为追踪目标。 |
| E08 | [LDNavigationProbe.cpp:64–110](../../Source/Lockdown/Private/Tests/LDNavigationProbe.cpp#L64-L110)、[208–254](../../Source/Lockdown/Private/Tests/LDNavigationProbe.cpp#L208-L254) | 现有用例含旧 life/Stop/阻挡/原生运动断言；其中“接近观察快照但不追击或攻击”正是需要替换的行为，不应保留为 Demo 回归要求。 |
| E09 | [LDInputProbe.cpp:66–153](../../Source/Lockdown/Private/Tests/LDInputProbe.cpp#L66-L153)、[LDHUDProbeExtension.cpp:29–86](../../Source/Lockdown/Private/Tests/LDHUDProbeExtension.cpp#L29-L86) | 已有 UI 不穿透、重绑不累积委托、无效观察不回退、失视野清理、临时回中与持续跟随分离等有用断言；旧 A 方向攻击和旧技能/物品槽断言须重写。 |
| E10 | [LDScaffoldBootstrapCommandlet.cpp:274–326](../../Source/LockdownEditor/Private/ScaffoldBootstrap/LDScaffoldBootstrapCommandlet.cpp#L274-L326) | 生成 `IMC_Gameplay` 与 11 个命名 IA，并断言 13 个映射；“映射数”不是“动作数”。尚未用 AssetRegistry/Blueprint/实际 Cook 证明其二进制引用可删除。 |

当前组件知识见 [移动/输入/镜头](../systems/MOVEMENT_INPUT_AND_CAMERA.md)、
[战斗](../systems/COMBAT_AND_GROWTH.md)、[视野/UI](../systems/VISION_AND_UI.md)、
[世界管线](../systems/WORLD_AND_DATA_PIPELINE.md) 和
[网络](../systems/ARCHITECTURE_AND_NETWORKING.md)。

## 2. 最小完整操作表

以下按键来自 Spec；Follow 沿用现有 Home 绑定并提供界面开关。
表中的新增订单语义是本报告的单一待审实施方案，不维护旧 profile 行为。
输入必须先经过 §3 的捕获，再判断本表；禁用图标不能替代服务器拒绝。

| 输入/条件 | 本地结果 | 权威结果/边界 |
| --- | --- | --- |
| 右键合法地面 | 一个 `MoveGround` 意图和待确认标记 | 世界给完整合法路径；CMC 实际行走，绕阻挡，不将 partial path 报为到达。 |
| 右键当前可见可攻击单位 | 一个 `AttackUnit`，显示合法目标/待确认 | 当前权限重查后接近、追击、在真实射程内逐次申请普攻；不以点击时快照永久授权。 |
| 右键公共商店 | 一个 `ApproachShop`，不是施法或购买 | 到交互范围后请求真实商店会话；旧订单/旧 life 的迟到结果不能自动开页。 |
| 左键，未准备动作 | 查看当前合法对象；地面点击清除检查对象 | 不换受控角色，不攻击、不移动、不改变 Q/W 或 1/2 的所有者。 |
| A，然后左键合法地面 | 进入准备态；确认一个 `AttackMove` 地面目的地 | A 按下本身不攻击。搜敌与续行见 §4.2；不是旧方向普攻，也不是移动键长按。 |
| S，游戏键盘输入可用 | 清准备态，立即停止本地自愿路径预测，提交新 Stop epoch | 停移动/追击/待交互并取消可中断动作；不回滚已释放效果、已发生伤害或已提交普攻节拍。 |
| Q/W | 解析当前角色主动槽；空槽明确不可用 | 使用 Learned 来源及当前构筑/绑定；不解释成装备、书籍或旧 Skill3+。 |
| 1/2 | 解析对应装备实例的主动；无主动明确不可用 | Gear 来源按实例校验；店外、圈内、两槽已满均可按战斗条件释放，不需要报价或空槽。 |
| P | 开关自身属性页 | 只读自身投影，不改变当前战斗/占领资格。 |
| 方向键/屏幕边缘 | 沿固定镜头平面平移 | 不移动角色、不授予视野、不改变订单目标/射程；边缘平移受焦点和捕获约束。 |
| Space | 回中；按住期间临时跟住自身，释放后恢复原跟随/自由状态 | 复用现有 Recenter 输入，不修改持久 Follow 偏好、不停止角色订单，不新增可选镜头模式。 |
| Home/镜头跟随开关 | 切换本地跟随，界面反映真实模式 | 不是角色控制模式或服务器状态。 |
| 滚轮 | 世界区域内按允许范围缩放 | 指针在可滚动面板上时仅滚动面板，不同时缩放世界。 |
| Esc | 编辑/重绑/拖拽捕获 → 顶层子确认/模态 → 未被高层消费的 Armed → 普通主面板 → 设置 | 绝不越过模态取消背后 Armed；每次只一个 Handled。本次 Esc 不 Stop 已接受订单、不发命令 RPC、不消费 Offer；退出实际会话另走会话域。 |
| 重新绑定/恢复默认 | 校验冲突后一次替换映射，帮助/HUD 同步 | 保存的是本地设置，不保存技能、钱、冷却、订单或候选。 |

**明确建议的鼠标取消规则：** 已有准备态时，第一次右键只取消准备并消费整次手势；
不会同时移动、攻击或打开商店。再次右键才走普通分派。
这是一个规则，不增加“取消且移动”的备用选项；审核时以可观测输入结果确认。
右键分派顺序为：输入捕获/准备取消 → 光标命中的合法可见单位 → 公共商店 → 合法地面；
先对同一次点击确定一个目标类别，拒绝该类别时不静默换成另一条命令。
A 复用同一“准备后点击确认”边界，不要求一直按住 A，不另建组合键持续状态。

### 2.1 四类能力目标与来源

能力目标类型只由内容定义给出，不由玩家发送的标签改变合法性。
使用 Combat 的 typed target/来源定义，不在 Controller 维护另一份技能几何。
首版目录未使用某类型时，用显式测试夹具覆盖接口，不为凑类型增加产品技能。
类型契约不授权保留未选效果/handler/资产；Ground 解析本身已有移动/A 等真实消费者，
能力执行支持集仍取批准目录的实际需求。夹具不能充当新增可玩技能或正常运行默认内容。

| 目标模式 | 准备/预览与确认 | 服务器重查/失败 |
| --- | --- | --- |
| Unit | 高亮当前允许的单位与射程；左键提交该观察句柄 | 当前观察权限、目标生命、关系、距离/LOS 与内容条件。失效不选附近目标、不改成地面、不自动改为自身。 |
| Ground | 地面落点与范围预览；左键提交脚点坐标及公开地面 trace 得到的 SurfaceId | 通过带必填 SurfaceHint 的 QueryGround 重查地面/支持面/地图版本，再查内容范围与几何；hint 不证明合法落地，不静默吸附别层或把越距点夹成另一次施法。 |
| Direction | 以自身脚点为起点的方向/长度预览；左键确认方向 | 有限、非零的平面方向及内容几何；单位只是画面上的参考，不能伪装成持续锁定。 |
| Self | 图标/自身范围预览说明作用；一次按键或图标点击提交 | 目标固定为当前自己的身体；不借当前检查对象改变目标，不要求多余世界点击。 |

非 Self 的 Q/W/1/2 只准备，不因按下/松开/系统键盘重复自行释放。
首版采用明确确认，不另加智能施法、长按蓄力或双目标连选。
能力确认时越距直接反馈，**不自动走近施法**；只有右键普攻和商店订单具有这里批准的接近职责。
**明确确认、Self 直接使用及普攻/攻击移动命令必须发送规范化意图**：
客户端预测目标无效、无支持面、SurfaceHint 无效或范围/MP/CD 不足不能吞掉请求；
这些世界/可执行性失败也可能按 §4.4 解除保护，不能伪装成 Malformed。
无效确认可保留仍可修正的预览并显示原因，不能显示为施放成功；来源、生命或权限失效则撤销预览。
空来源或过期绑定是可识别激活尝试的执行失败，不是畸形包。
发送哪个 Q/W/1/2 入口由明确的 activation slot 标签描述；标签不是来源/实例或冷却身份，
不能让服务器把过期来源自动换成槽位当前新内容。

准备态捕获的是来源身份与相关构筑/物品修订，不只是槽号。
Q/W 升级、遗忘或替换，装备出售/解绑，或准备期间同槽换成另一实例，
均要求重新准备；普通 HP/MP 快照 revision 增长不能无故取消预览。
**已确认：同款双持的属性相加、同名被动只生效一次，两份装备主动各自独立冷却，可连续使用两次。**
两个物品仍是不同实例，1/2 必须解析相应来源；不能按同款定义/family 合并成一个共享 CD。
客户端不维护槽位私有冷却，全部读取 Combat 的来源 readiness；
换槽、卖出重买仍不能洗掉未结束冷却，持久冷却身份由 Combat/装备域统一。
**来源丢失期间继续计时，重新持有时沿用原结束时间，重购/重学不得刷新。**
按钮暂时消失不删除服务器的 readiness；再次获得后显示同一未结束期限的剩余时长。

## 3. 输入所有权、镜头与设置

### 3.1 同一手势只有一个消费者

在现有 `ALDPlayerController` 输入边界建立 `FLDInputCaptureState`：
记录设备/按键或鼠标按钮、按下时的消费者、捕获代次和释放前抑制状态。
它只是本地输入状态，不是另一份服务器订单。

指针/世界确认手势的消费顺序为：文本/重绑/拖拽捕获 → 模态界面 → 命中的普通面板 →
准备中的目标确认 → 小地图 → 世界。**按下时确定的消费者保持到释放**，
不能在按下/释放之间关闭窗口后把释放转发给世界。
开面板的点击不会继续激活新面板按钮，关面板/重绑后的仍按住按键必须先释放。
一次物理按键周期最多提交一次离散动作；图标点击和热键进入同一入口，
不是各发一条 RPC。相机连续轴与一次性战斗按键分开处理。

`CanWorldCommand` 改为读取实际输入上下文：鼠标指针在 UI 上阻止该鼠标手势；
键盘文本焦点阻止游戏快捷键。不能把“光标悬停普通面板”当作所有键盘都失效，
也不能让文本框里的 S/A/Q/W 进入世界。模态界面在全视口抑制新世界命令。
`FInputModeGameAndUI` 只表示 UI 未处理后仍可交给游戏，**不是自动防穿透证明**。

“顶层”取 UIRoot 当前实际输入层栈，不沿用 `V05Back` 对旧 Panel 指针的固定 if 顺序。
UI 模块提供 `TopInputLayer/CloseTopPanel`，Controls 只消费，不再维护第二份打开面板列表。
与 UI 模块收敛为**同一时刻一个主要面板及其子对话框**：切换主要面板先关闭旧页，
不让两个主要面板争抢键盘。下表是该主面板内部的实际输入层级；
同层以最后打开/获得显式焦点者为顶层，关闭后回到仍有效的下一层。

**Esc 使用单独的 Back 路由，不把指针命中顺序或无条件 Armed-first 直接套过来：**
编辑/重绑/拖拽捕获 → 顶层子确认/模态 → 未被高层消费的 Armed → 普通主面板 → 设置。
高层捕获或模态存在时先由该层处理并截断，绝不越过它取消背后 Armed；
只有没有高层阻挡的普通主面板，Back 才允许先取消 Armed 而保留主面板。
每次 Esc 在首次 Handled 后立即返回，不连续关闭两层，不调用 StopOrder、
世界命令 RPC 或候选提交。已经发送的其他请求仍按真实域回执处理，不由 Esc 回滚。

| 层 | 当前 Demo 界面 | Esc/输入边界 |
| --- | --- | --- |
| 局部编辑/拖拽捕获 | 重绑、文本编辑/输入法组合、拖拽 | 先退出当前捕获并消费 Esc；不顺带关下层、取消背后 Armed 或发世界命令。 |
| 顶层子确认/模态 | 替换/买卖等明确子确认、设置/退出菜单 | 先处理该层并截断，不让 Armed 抢先；阻止全视口新世界命令。关闭页面不提交候选或消费机会。 |
| 普通主面板 | 商店、自身属性、三选一主页面（不含子确认） | 卡片/按钮命中消费、文本焦点消费键盘，面板外仍可操作世界；没有高层时 Back 可先取消 Armed，否则只关主面板，不暂停、不改已接受订单。 |
| 不可用 Esc 清除的状态呈现 | 战斗 HUD、击杀通知、死亡状态、结算结果、小地图底层 | 不把状态层当可关闭面板；继续按当前阶段的设置/退出或结果导航处理，不恢复游戏输入。 |

开属性/商店等面板不暂停联网世界、不保护身体，不自行撤销已经接受的订单。
打开会捕获目标确认的界面时撤销本地准备态；现有订单在没有 S/替换/失效的情况下继续，
当前连接的订单心跳与 Widget 焦点分开。购物仍可能被击杀或击退离店。
`OpenChildConfirmation` 在建立子层前调用 `CancelArmed`，但保留打开它的原手势所有者
及释放前抑制；不能清空 capture 后把同次鼠标释放交给世界。
子确认存在期间，所有 Arm 入口都检查同一高层 gate，不新建背后 Armed。
操作系统失焦则走明确的输入失焦取消：清准备/按钮锁存/相机轴，提交一次新 Stop，
停止旧心跳；重新获得焦点不重放物理按住键、不恢复旧订单。

正式拟建本地接口是 `CanWorldCommand(const FLDInputContext&) const`、
`CancelArmed(ELDLocalCancelReason)`、`InvalidateLocalInput(ELDInputInvalidationReason)`，
均在现有 Controller；`StopOrder()` 仍进入 Navigation 的 Stop。
Context 引用当前 UI 层和手势所有者，不复制一个可由 UI 写入的“服务器可行动”状态。
Invalidate 对映射重建/身体替换清锁存，对 OS 失焦另外提交 Stop；
开普通面板不能伪装失焦。此 gate 只防本地穿透，服务端仍执行全部准入。
死亡界面允许确认保留的三选一/主动替换，走成长域的持久参赛者/候选入口，
不通过“必须有存活 Pawn”的世界命令 gate；旧局/旧候选/旧修订仍拒绝。
选择只修改构筑，不复活、不治疗、不重抽候选；关闭页面不丢机会。

### 3.2 只保留一套运行映射

选定方案是**改造现有动态映射路径**，由 `ULDControlSettings` 的一份动作注册表创建
`UInputAction` 和唯一 Gameplay mapping context；移除静态 IA 加载分支与旧 Bootstrap 输入生成。
不同时维护一套静态 IMC 默认值和另一套动态默认值。
运行时 action/context 由现有 Controller 的强引用成员持有，重绑先解除旧绑定/context/委托再重建；
Pawn 重建或重新 possession 不累积一份新的委托。只有真实 LocalPlayer 安装 Enhanced Input。

保留/改名后的唯一动作集合：
`Select`、`Navigate`、`AttackMove`、`Skill1`、`Skill2`、`Item1`、`Item2`、
`Stop`、`Attributes`、`PanUp/PanDown/PanLeft/PanRight`、`Recenter`、`Follow`、`Zoom`、`Back`。
三选一、商店、设置里的按钮直接调用各域操作，不为旧 Development/Inventory/Help 热键保留旧玩法。
`Back` 的 Esc 保留为可恢复的退出捕获键；重绑界面显示保留键和冲突原因，不静默交换两个动作。

删除控制设置中的 `Profile`、TacticalDefaults/HybridDefaults 与旧动作迁移分派。
本地设置 schema 单独管理，不等于产品版本或网络版本。
读取旧文件时只迁入仍合法的名称/镜头/显示偏好及现存动作绑定；
不支持/冲突的绑定明确提示并要求采用当前有效绑定，不能静默复活旧动作。
保存失败展示错误并保留当前可用内存设置及未保存标记，不谎称已持久化。

### 3.3 镜头

沿用 `ALDCharacter` 的 SpringArm/Camera 和 Navigation 的
`PanCamera/Recenter/ToggleFollow/PanTo/TickCamera`，固定俯视斜角，不提供自由旋转。
手动平移转入当前会话的自由镜头，Follow 开关同步；Space 的临时回中不改持久 Follow，
释放回中键恢复原状态，偏好改变由显式 Follow 操作保存。
相机使用世界提供的静态地图边界，不使用动态隐藏单位决定夹限、碰撞或缩放。

边缘平移使用当前 viewport/DPI 的有效内容区域，禁用于失焦、文本编辑、重绑、拖拽、
模态捕获和准备瞄准期间；保留现有“瞄准时不误触边缘平移”的约束。
滚轮和方向键只由其手势所有者消费；镜头距人物再远，视野仍来自自身合法视野源。
**死亡镜头允许自由平移静态地图，但不跟随/观察隐藏玩家，也不获得新的活体视野。**
死亡时撤销动态目标与单位 LastSeen 标记；公共占领、固定地点和自己的持久界面仍可查看。
没有存活身体时，Recenter/临时跟随明确显示“无存活角色”，不改跟其他玩家；
保存的 Follow 偏好在自己的新生命建立后恢复，不能用切镜头取得观察权限。

## 4. 当前订单与战斗交接

### 4.1 一个当前订单，不是另一个动作系统

`ULDNavigationComponent` 是当前订单的唯一写入者，保留本地预测与服务器状态分离。
服务器当前订单只有以下种类：`MoveGround`、`AttackUnit`、`AttackMove`、
`ApproachShop`、与已接受 Combat action 关联的 `Action`。
`Idle/Approaching/Following/AwaitingAction/Attacking/Completed/Cancelled/Failed` 是过程状态，
不是玩家可选模式。没有历史订单栈；最多保存当前订单和有界待受理意图。

**提交时点：** 本地 Armed → 发请求 → 收件 Queued → 本人/当前身份/新鲜性及写许可 →
可识别进攻意图解除保护 → 完整动作准入 →
原子激活/替换 → 执行/终结。Queued 仅证明收到，不足以取消原订单。
无效新意图反馈拒绝，原已接受订单仍有效，但已识别进攻意图的解除保护不回滚；
合法新意图提交后旧单失效。
替换后新动作的 release 再验失败，则结束新动作，不自动恢复已被替换的历史订单。
Stop 不等待目标/来源/资源验证，仍检查拥有者、局、生命和递增 epoch。

最高已见请求序号、当前已提交订单身份、回执发布序号是三个不同概念。
一个较新的无效请求不能撤销已提交订单的运动权限；较旧 Pending 也不能在较新输入后突然激活。
本地尚未确认的意图/标记不是服务器的 `ActiveOrder`。
客户端 Stop 使用自己递增的 Order/Epoch；服务器因目标失效结束订单时记录该 identity 已终结，
不擅自推进客户端尚不知晓的序号空间。新同生命命令仍须新 Order，旧 terminal identity 永不复活。

### 4.2 右键普攻与 A 攻击移动

**已确认初始普攻为近战单体，且首版必须能通过装备/属性获得群攻与攻击距离成长。**
订单只锁定一个主目标；后续群攻/余波由 Combat/内容处理，不生成副攻击订单、
不新增群攻快捷键、不给隐藏单位额外锁定权限。
`UNIT.BasicAttackRangeCm` 只是初始基础；预览、接近、普攻准入与 A 搜索消费
`STAT.AttackRangeCm`/`FORMULA.AttackRange` 的当前有效结果，不能硬编码初始范围。

`AttackUnit` 每次准备接近、重算路径及申请一次普攻前，调用世界的当前观察解析。
目标在范围外时取合法接近点和完整路径；到范围内才交给 Combat。
新目标候选的存活/关系/观察授权与 AttackStart 的实际射程判定分开：
不能直接用当前“必须已在射程内”的 `LegalTarget` 谓词筛掉所有需要接近的单位。
Combat 接受本次 AttackStart 后拥有其前摇、节拍、资源/状态与命中；
Navigation 不另维护攻击冷却、不用表现动画结束触发伤害。
目标仍合法但移动出范围时按相同订单继续合法追击；无进展/无完整路径明确失败，不无限重算。
装备/属性使有效射程改变时，更新当前合法目标的接近计划与预览；增加可提前进入射程，
减少则重新求完整接近点，不生成新免费 AttackStart、不清原节拍。
动作开始后的快照/复验顺序仍由 Combat 统一，Input 不另实现一套范围公式。

`AttackMove` 的唯一目的地是确认时的合法静态地面点。拟定最小搜敌规则：
只从 `EnumerateVisibleUnits(..., Attack)` 的当前可攻击候选中，按自身脚点 XY 距离升序，
同距以该观察帧稳定的 opaque handle 排序；不读取隐藏 HP、装备、仇恨或私有威胁值作选择。
搜敌范围只引用 `TARGET.AttackMoveAcquireRadiusCm`，不在控制器另设一个攻击/视野半径。
已有合法目标保持到死亡、失效或明确替换，避免每帧最近者切换抖动。
没有候选时继续原地面目的地；当前合法观察中确认目标死亡后重新按同规则选取，空集则续行。
到达目的地且无当前合法目标则完成，不把一次 A 变成永久原地自动攻击开关。

上述 acquisition/tie-break/续行是待 PR 审查的最小交互方案；不加入阵营、单位种类优先级或设置分支。
**失去视野的确定规则：取消目标锁定并停止这条追击订单。**
AttackUnit 和已经锁定目标的 AttackMove 均终结当前订单，清路径/重复普攻关联，
不转为 LastSeen 地面移动、不恢复原 A 目的地、不保留单位 LastSeen 小地图标记。
重新可见也不重启终结的旧单，必须有新的明确订单。
Conceal/stale/TargetUnavailable 不可被当作“已观察到目标死亡”来继续 A，
否则会借恢复地面或重新索敌绕过停止规则。

授权死亡的唯一证据是世界域的 `FLDObservedUnitTermination`，其 Kind 为 ObservedDeath，
并在实际死亡时取得合法 pose/观察权限见证；不是隐藏 Death 表、旧视图或全量集合删项推断。
服务器操作域将它绑定到事件发生时原有的完整 `FLDMoveIdentity`；
消费时原单必须仍未取消，目标句柄/ObservationEpoch 匹配，且真实事件时间不早于原单接受。
它只能完成该原单的当前交战；旧 epoch、重放或早于新订单接受的终止不能驱动新订单。
客户端 Relay/WorldView 的显示消息不调用 Navigate/SubmitOrder，不能自行重启导航；
合法订单变化仍走服务器绑定后的命令回执。

每次重复普攻由当前服务器订单推进产生 `AttackCycle`，组成 Nav 形 `FLDCombatRequestKey`；
Navigation 不分配 CombatToken。完整准入通过后由 Combat 统一分配服务器 Token，
并将请求键绑定到该 Token；完整 MoveIdentity/AttackCycle 只负责请求关联和去重。
派生重复普攻继承原 Navigation 进攻意图身份，不为解除保护再次生成序号。
被控制时，Combat 的 Move/Attack/Skill/Item 等准入类别分别决定可执行性；
Root 不自动代表不能普攻或施法，受伤也不等于取消占领。
可中断与已释放动作的区别以 Combat 合同为准，不用单个“Busy”布尔值覆盖全部控制。

### 4.3 商店接近

`ApproachShop` 使用公共 POI，不借全局 Actor 扫描或旧 uint8 Site 编码选择服务。
`FindApproach` 提供合法端点/路径；到达仍以服务器真实脚点到商店中心的 XY 距离判断。
路径 acceptance 只用于导航收敛，不能放大 `SHOP.InteractRadiusCm`。
到达结果关联当前订单后才调用商店域的 Open，并由该域建立有效会话/报价。
到达不是购买成功，也不是免死/安全区。

离店/死亡/终局使真实商店会话失效；页面迟到、旧 Open 回执或重新进店不能恢复旧报价执行权。
已打开页面的关闭按钮只关闭页面，不创建另一个接近订单；要再次接近必须有新意图。
交易由经济域重查与原子提交；本模块没有改钱、买入或卖出入口。

### 4.4 发出进攻意图即解除保护

**用户已确认：只要发出进攻意图就解除保护；目标无效、距离不足、没蓝、CD 未就绪等失败不能保留保护。**
本地准备/取消/只读 Availability 查询没有发送执行意图，不解除；
MoveGround、ApproachShop、Stop、检查和镜头操作不是进攻意图。
AttackUnit（含追击）、AttackMove，以及可识别的角色/装备主动激活尝试走同一解除边界，
不能等到 FindApproach 成功、AttackStart 或 release 才解除。

服务器先验证本人、当前 Match/Body/Life、新请求/取消水位、可解析的命令形状和当前写 permit，
然后调用 GameMode/生命域唯一的 `EndProtectionForAggressiveIntent`，发布真实资格变化；
之后才检查目标、路径/距离、来源绑定、资源、冷却和执行控制条件。
旧局/旧生命/旧 epoch/重放/终局/真正畸形的枚举或非有限包在解除前拒绝，
不能让旧请求影响新身体。无目标、空来源、过期 SourceBinding 是规范化执行失败，
不能伪装成 Malformed 绕过已识别意图。

请求目标允许显式 Missing；不以零向量、附近对象或其他目标类型填补。
地面确认无可用支持面时，保留显式 Missing，或在已有地面点上携带实际无效的 SurfaceHint；
不补造 SurfaceId，不做空 hint 向下投影。InvalidSurface/WrongSurface/NotGrounded
属于世界语义拒绝，不在客户端吞掉规范化进攻确认，也不跳过已经识别意图的解除保护。
UI 对资源/目标不足显示原因和“发出进攻意图即解除保护，失败亦解除”提示，
但确认事件仍进入既有 SubmitOrder/SubmitAction，不另发一条独立的“解除保护 RPC”。
仅因处于保护就禁止按钮或先在客户端吞掉确认，会错误保留保护，因此不得这样实现。

解除保护是意图本身的已确认副作用，不是动作成功、MP/CD 支付或订单替换成功。
Rejected 回执仍携 `ProtectionRemoved` 和生命域的 `uint64 LifeRevision`，UI 按真实 owner 投影更新；
不能宣称“被拒绝所以所有状态都没变”。解除可能使圈内角色立即具备占领资格，
由 Life 事件进入同一核心边界，不能等待下一次帧末人数采样。
重复请求只重放结果，不再次解除、重发资格变化或重新提交动作。

## 5. SC-09 正式对接表

本节所有新增类型/重签名均为**拟建接口**。它们绑定现有组件，不声明已经存在。
世界查询以世界报告维护的同名唯一合同为准；Combat 名称已与战斗域对齐。
生命周期的 body key/时钟/水位/终局闸最终公共类型由该域绑定；下列窄适配字段不另造核心状态。
AI 消费者对接其模块拟建的 `ALDOpponentController : ALDPlayerController`，不继承 B91/B92；
它没有 NetConnection/LocalPlayer/UI，只调用 SubmitOrder/CancelOrder/QueryOrder。
执行驱动按真实拥有客户端、本地玩家和注册服务器 AI 区分，不从 IsLocalController 推导全部权限，
也不另接 AAIController 的 MoveToActor 或第二套运动。

### 5.1 状态与写入者

| 状态/拟建类型 | 唯一写入者与落点 | 消费者/不允许回写的投影 |
| --- | --- | --- |
| 当前订单 `FLDOrderState` | 现有 `ULDNavigationComponent`，由权威准入/取消/动作结果更新 | 含 ActiveIdentity、kind/phase、typed target、当前路径游标、uint32 OrderRevision、uint64 AttackCycle、关联 RequestKey 与接受后的 ActionId/ActionRevision；拥有者 `FLDOrderView`、AI 只读。 |
| 路径与自愿运动输入 | Navigation 计算合法方向；实际位置/速度由现有 CMC 写 | 自身预测/saved moves；不由 UI、路径节点或 Combat 直接逐帧 SetActorLocation。 |
| 本地准备 `FLDArmedTarget`、检查 `FLDInspectionSelection`、捕获 `FLDInputCaptureState` | `ALDPlayerController` 输入层 | HUD/光标/预览；不复制成服务器真实目标或状态。 |
| 输入注册/本地偏好 | `ULDControlSettings`；Navigation/Character 应用镜头设置 | ControlsPanel、HUD 标签；不写局内数值。 |
| 订单 receipt/high-water | Navigation 的有界请求记录与拒旧水位 | ClientOrder/AI 结果；清表现缓存不能让旧订单重执行。 |
| 动作/能力可用性 | `ALDCombatDirector` 的 Action/Source/Availability | Navigation 只等待/取消/申请；不再平行保留一套无限重复普攻目标/冷却。 |
| 实际运动事件 `FLDAuthorityMotionSegment` | CMC 在真实权威运动提交点记录 | 世界提取圈进出，生命周期归约；客户端 correction/预览不能写事件。 |

### 5.2 窄类型与函数

| 拟建类型/签名 | 字段和意义 |
| --- | --- |
| `FLDOrderCaller`（仅服务器，不序列化） | 采用核心 `FLDMatchContext Match` 和 `FLDCombatBodyRef Body`，不独存第二份 Match/Life。Body.Victim.Kind 必须为 Contender，EntityId 是参赛者，BodyIdentity 是该次身体 FGuid；由拥有连接或已注册 AI 解析，并取得世界 ObserverContext。 |
| `ELDSpatialOrderKind` / `FLDSpatialOrderRequest` | kind 仅 `MoveGround/AttackUnit/AttackMove/ApproachShop`；完整现有 `FLDMoveIdentity`；按 kind 仅一个地面、ObservedUnit 或公共 Shop 目标。地面带地图修订/SurfaceHint，POI 由注册记录给支持面；单位用观察代次，不传无关构筑版本、路径数组或服务器 Actor。 |
| `FLDOrderTarget` | 单一判别载荷：Ground=脚点 cm+必填 FName SurfaceHint（注册 SurfaceId）；ObservedUnit=句柄/ObservationEpoch/代次；Shop=公共 POI ID/地图修订；可显式 Missing。多臂/未知 kind/非有限数是畸形，缺目标/过期/错误支持面是后续执行失败，不伪造替代值。 |
| `FLDOrderReceipt` / `FLDOrderView` | 原请求 identity、uint32 OrderRevision、uint64 ReceiptSerial、`Queued/Rejected/Activated/Progress/Completed/Cancelled/Expired`、安全错误码及合法目标/端点；动作关联携 RequestKey，准入后才有可选 ActionId，不用0冒充已分配 Token；另含 ProtectionRemoved/LifeRevision，不含隐藏坐标。 |
| `FLDCombatRequestKey`（Combat 所有） | 判别二形：Direct{Generation, Contender, Life, uint64 DirectSequence}；Nav{Generation, Contender, Life, FLDMoveIdentity, uint64 AttackCycle}。Direct 由该参赛者输入/AI适配产生；Nav 仅由当前服务器 Navigation 订单推进产生，不相信外部提交的 Nav 标签。 |
| `uint64 CombatToken` / ActionId / 内容 AttackId | Combat 完整准入时唯一分配，回执绑定 RequestKey→Token；ActionId/AttackId 是该 Token 在 Match/Caller 下的同一身份，不另增动作/攻击序列。ActionRevision 是发布修订，不是新动作 ID。 |
| `ULDNavigationComponent::SubmitOrder(const FLDOrderCaller&, const FLDSpatialOrderRequest&) -> FLDOrderReceipt` | 真人 ServerOrder wrapper 与服务器 AI 共用的收件/排队入口；只做身份/形状/拒旧/去重等前置，返回 Queued 或前置 Rejected。不得激活/替换/解除保护，不因本地或提前路径/资源失败吞掉可识别进攻意图。 |
| `CommitPendingOrders(const FLDGameplayWritePermit&) -> void` | 核心 Actions 阶段重新校验当前前置/permit，识别进攻意图并先经 Life 解除保护，再做完整当前状态/世界/路径准入及原子激活。结果写 receipt；不跨帧保存 permit，不用 IsGameplayWriteOpen 的一次读值冒充许可。 |
| `ServerOrder(FLDSpatialOrderRequest)` / `ClientOrder(FLDOrderReceipt)` | 替换现有旧 RPC 签名，不保留旧 Surface+Observed+Site wrapper。owning RPC 只负责解身份/传输，不能由 payload 指定 owner。 |
| `CancelOrder(Caller, FLDMoveIdentity, ELDOrderCancelReason) -> FLDOrderReceipt` | ServerStop、失焦、生命/离局/终局清理的统一订单取消；提高水位，通知 Combat 取消适用动作，清路径/接近/待 Open；不退资源。 |
| `ServerHeartbeat(FLDMoveIdentity, uint32 Sequence)` | 复用现有非可靠心跳，只刷新完整当前已接受 identity 且 sequence 更新的租约；不创建订单、不重新激活已取消路线。 |
| `CommitActionHandoff(const FLDGameplayWritePermit&, Caller, ProposedIdentity, ActionId) -> FLDOrderCommitResult` | 完整准入时使用 Combat 分配的 Token 同身份 ActionId，在同一权威边界原子替换旧单；Queued 不调用。ValidateWritePermit/旧 epoch/较新意图检查同属原子交接，失败不能只提交 Combat 半边。 |
| `OnCombatOutcome(const FLDCombatActionOutcome&) -> void` | 消费 OnActionFinished；核对原 RequestKey→CombatToken 绑定、当前 ActionId/关联订单和更新 ActionRevision，终结或继续合法订单，不凭未知映射或旧通知续打。 |
| `QueryOrder(const FLDOrderCaller&, const FLDMoveIdentity&) -> FLDOrderReceipt` | 只读当前/保留回执或明确 Expired，供 UI/AI 恢复丢回包；查询不能推进订单/动作/租约，也不能观察其他参赛者。 |

`FLDArmedTarget` 只保存本次来源、必要修订、目标模式和本地预览；
真正施法继续使用战斗域的 `FLDCombatActionRequest`，不塞进空间订单的大封包。
当前 `FLDTacticalRequest` 的字段/序列化由 Combat 改造为该合同，输入不另定义同义 wire。
旧的客户端 Token 输入语义改为明确 RequestKey，不把 DirectSequence 当成服务器 CombatToken。
新 wire/动作支持集必须同步生产者、decoder、拒旧、拥有者回执和实际兼容检查；
产品版本号不能替代协议版本。计数不回绕复用旧执行身份；耗尽或非法序号明确拒绝。
现有移动 wire 的 Match 对应 `Caller.Match.Generation`，Life 对应 `Caller.Body.Victim.Life`；
核心 `FLDMatchContext.LedgerMatch` 是独立 FGuid，由核心建立映射，不能强转。
`OnCombatOutcome` 可以更新关联状态/排队下一次尝试，但不能越过下一合法 Actions 阶段
递归释放新动作；每次真实 Gameplay 提交都使用当时有效的 permit。

### 5.3 命令、版本、结果与去重

| 命令 | 必要校验/提交阶段 | 回执与拒旧 |
| --- | --- | --- |
| MoveGround/AttackMove | 拥有者、Match/Life、Order/Epoch、进行中/存活及 permit；AttackMove 先解保护，MoveGround 不解；再验地图/必填 SurfaceHint/真实地面/完整路径，A 候选另查观察 | Queued ≠ Activated，Activated ≠ Arrived；同 identity 重放/异条款拒绝；A 的 InvalidSurface/WrongSurface/NotGrounded/路径失败不回滚已解除保护。 |
| AttackUnit | 当前身份/阶段/新鲜性及 permit 后先解保护；再验观察者/句柄/目标 life、可见、关系及完整接近路径；每次接近/AttackStart 再验 | 隐藏/失效句柄统一 TargetUnavailable，不泄露真实目标，也不保留已解除的保护。旧观察不能重新锁定。 |
| ApproachShop/Open | 空间身份、地图/公共 POI、合法路径；到达真实交互距离后，在当前订单内申请商店会话 | Open/报价由商店域回执；旧 order/life 返回不能打开新会话或使 UI 宣告购买。 |
| Q/W、1/2 | 用 Direct 形 RequestKey 提交；当前身份/新请求/shape/permit 后先解保护，再验来源/版本/目标/资源；完整准入才分配 Token，release 再查 | Queued/准入前 Rejected 可以没有 Token，以 RequestKey 等待/重放/取消；Rejected 可含 ProtectionRemoved。来源变化不改释为新内容，空来源/过期绑定不伪装 Malformed。 |
| Stop/适用取消 | 当前拥有者/局/生命和新 Order/Epoch；CancelAction 可指原 RequestKey 或 ThroughDirectSequence，尚未收到 Token 也可取消；Nav 按原 MoveIdentity/Epoch 失效并取消已关联请求 | Direct 水位只比较该调用者/局/生命内的 DirectSequence，绝不数值比较服务器 Token。已 release 不撤回/不提前节拍；迟到请求、ACK和通知不能越过各自取消域。 |
| Heartbeat/AI 租约 | 真人当前订单及递增心跳序号；AI 使用服务器绑定仍存活的策略/参赛记录，不发送伪造 RPC | 旧序号不延寿，过期终结；失去 controller/record/生命时失效。UI 焦点不是拥有连接身份。 |
| 只读订单查询 | 当前调用者和当前局；查自身当前状态/保留回执 | 回执缓存过窗返回 Expired 并给合法当前状态，不重新执行，不无限保存全局日志。 |

请求去重域为“调用者 + Match/Life + 领域请求 identity”，不按显示名称或按钮位置。
保留有界请求/回执缓存、当前 ActiveOrder 和独立拒旧高水位；被逐出缓存的旧请求明确 Expired。
比缓存上限更多请求之后，重放最早包仍不得执行；同 ID 异条款不成为第二次操作。
本地输入边沿消重与网络幂等都要实现，不能用其中之一替代另一个。
Movement ACK 只确认导航，Combat receipt 只确认相应动作；两者不能互相充当命中或交易成功。

Direct 与 Nav 的判别形本身参与请求键，不能将 DirectSequence 与 AttackCycle 扁平化成一个整数空间。
两形的 Generation/Contender/Life 都须匹配实际 Caller；Nav 外层 Generation/Life
还必须与 MoveIdentity.Match/Life 一致，不能保留两份互相矛盾的身份上下文。
真人 RPC 和 AI 策略不得伪造 Nav 形请求；HasAuthority 不等于当前 Navigation 生产者权限。
DirectSequence 由该参赛者的唯一输入/服务器 AI 适配单调分配，Q/W、1/2 等 Direct 激活共享此域；
关闭面板或重绑不重置计数。StopOrder 在按下时记录本地已发出的 DirectSequence 上界，
通过 FLDCombatCancelRequest 提交 ThroughDirectSequence，并独立发出新的 Navigation Order/Epoch；
不能用服务器当时“已收到的最大请求”代替仍在途的客户端上界。
取消早于请求到达时，保留相应 RequestKey 拒绝记录或 Direct 高水位，
不因尚无 Token/回执就忽略取消。Nav 的取消 epoch 和 Direct 的 ThroughDirectSequence 分别维护；
只通过已保存的 RequestKey→Token 映射找到被取消动作，不跨序号域大小比较。

### 5.4 拒绝与可观测反馈

拟建 `ELDOrderFailure` 集中由输入/UI/AI 消费：
`InvalidRequest`、`NotOwner`、`StaleMatch`、`StaleLife`、`StaleOrder`、`CancelledEpoch`、
`SequenceExhausted`、`MapChanged`、`InvalidGround`、`NoCompletePath`、`PathBlocked`、
`InvalidSurface`、`WrongSurface`、`NotGrounded`、`NoProgress`、`TargetUnavailable`、
`SourceChanged`、`UnavailableAction`、`Controlled`、
`Dead`、`NotPlaying`、`MatchEnded`、`Expired`。
Combat 的资源/冷却/来源原因和 Vendor 的范围/报价错误沿其正式错误类型映射，
不复制一套费用/距离校验。玩家得到可理解提示和失败标记；AI 得到明确可重试类别，
不能靠反复每帧重发维持 Pending。世界内部查询失败不能当作“可走”或“看不见所以安全”。

## 6. 生命周期与竞态矩阵

| 事件 | 本地输入/检查/镜头 | 权威订单/动作 | 回执/已释放效果 |
| --- | --- | --- | --- |
| Esc/子确认 | 按捕获→子确认/模态→Armed→普通主面板→设置逐层一次 Handled；高层不可被 Armed 越过；打开子确认先清 Armed 但保留释放抑制 | 不 Stop 已接受订单、不发世界/候选命令、不退款或消费 Offer；不把 Esc 当 S | 已发的其他请求由真实域回执处理，不能伪造 Cancelled/Success 或用关闭子层撤回已提交结果。 |
| S | 清准备与自愿运动预测，生成新 epoch，并记录已发 DirectSequence 的取消上界 | Navigation/Combat 分别按 MoveIdentity 或 RequestKey/Direct水位清路线/追击/可中断动作与旧未接受请求 | 无 Token 也可取消；旧 ACK/saved move/ActionRevision 不续行，已 release 按 Combat 生命周期。 |
| 合法新订单/动作替换 | 新意图有独立待确认显示 | 完整准入后原子替换；旧路径/待 Open/重复普攻关联失效 | 新动作失败不恢复历史单；旧回执仅可查阅，不改变当前操作。 |
| 无效新意图 | 显示原因和已解除保护的真实结果；本地预测失败不吞确认 | 不替换原已接受订单；可识别进攻意图在当前前置通过后仍解保护；更老 Pending 不迟到激活 | 不扣未提交资源、不重置节拍；重复不再发资格变化或动作。 |
| 死亡/复活 | 清准备、生命相关检查/捕获/路径/单位标记；保留三选一并允许确认；相机仅自由看静态图，无新活体视野 | 旧 life 的移动/追击/待 Open/未释放动作失效；复活从 Idle 新 life 开始；死亡选择只改构筑 | 等级/技能/装备/金币/候选和应持续冷却归原所有者保留；选择不复活/治疗，旧动作不击中新 life。 |
| 目标死亡/离局/销毁 | 清该目标检查/预览/代理关联 | AttackUnit 终结；AttackMove 仅在合法观察中确认死亡时按 §4.2 重获候选；观察失效直接停止，不以隐藏死亡/销毁原因触发续行 | 来源/目标 life 均复验；无效句柄不回退相邻对象，不利用拒绝原因探测隐藏状态。 |
| 目标失视野/观察 stale | 撤销选择/预览/动态信息及单位 LastSeen 标记 | 取消锁定并终结当前 AttackUnit 或追击中的 AttackMove；清路径，不转 LastSeen、不恢复旧 A 地面目的地 | 晚到旧快照/ACK 或重新可见都不能重启旧单；新行为须新明确订单。 |
| 来源升级/替换/出售/解绑 | 清依赖旧来源的准备；按钮读取新投影 | release 前由 Combat 重验并取消不再合法的请求；新槽位内容不继承旧输入 | 冷却继承由来源域，不由清图标/清订单归零；已 release 不退款。 |
| 失焦/输入模式重建 | 清按压/拖拽/边缘平移并抑制至释放 | OS 失焦提交 Stop 并停止旧心跳；普通面板不伪造失联/暂停 | 恢复焦点不重放；重绑一次后仅一个有效委托。 |
| 普通玩家离开 | 清该连接输入/观察/页面 | 移除合法调用者并取消订单/待 Open；不转为全知 AI | 离开不是击杀；保留必要拒旧记录直到域关闭。 |
| 终局 | 关闭新世界输入，显示已授权结果 | 先由核心关闭写闸，再取消/清理，不能从清理回调继续动作/交易 | 已提交回执可只读重放；效果不再推进伤害/奖励/占领。 |
| Host 离开/失联 | 返回菜单并显示原因 | 会话域结束；不继续本地权威模拟或重发到无效世界 | 所有旧连接回包无执行权。 |
| 新局/重新 possession | 保留本地设置/名称，清全部旧订单/目标/观察/绑定实例关联 | 新 Match/生命/身体；只从批准初始状态开始 | 旧 Match 的 RPC/ACK/action/view 一律不能应用新局。 |
| 重复输入/旧 ACK | 显示以当前身份/修订为准，不重复成功动画 | 同条款只重放，异条款拒绝；已终结状态不能被旧 Activated 反转 | 每类流分别维护游标，不用一个全局 LatestRevision 替代。 |

## 7. CMC、世界查询与读取边界

### 7.1 运动执行角色

| 身体角色 | 订单/运动接线 | 禁止 |
| --- | --- | --- |
| 真拥有客户端 AutonomousProxy | 只预测自己的当前合法意图；沿现有 saved move 携带完整身份；server correction 后按正确身份重放 | 第二位置积分器、发一串客户端路径点当权威、旧 epoch 的 saved move 重新驱动。 |
| 远程真人的服务器身体 | CMC 重现并验证当前订单/自愿输入；服务器独立查完整路径、地形与可用运动 | 同时再跑一份 AI 跟随造成双重积分；只因收到位置就认定路径合法。 |
| Listen Host 自己/服务器 AI | 在原 CMC 更新前供给一次合法方向；Host 仍走普通准入，AI 从注册记录解身份 | `IsLocalController()` 免校验、临时把玩家交给 AIController、假 LocalPlayer 或 client RPC。 |
| 其他客户端表现 | 使用合法观测/原生允许表现，不写权威运动 | 相机移动后遍历服务器或客户端隐藏 Pawn，或让代理碰撞参与权威占领。 |

保留 `FLDSavedMove::CanCombineWith/SetMoveFor`、`FLDNetworkMoveData::Serialize` 的身份语义，
删除 sprint 相关扩展位和旧 B91 搬运/全图分派时同步读写两端、压缩标志与回放。
`GetMaxSpeed` 消费唯一战斗属性投影；Input/AI 不另设 MoveSpeed。
拒绝旧自愿 steering 不能停掉合法重力、原生碰撞纠正或已经提交的位移效果。
dash/击退仍经同一个 CMC/root-motion 能力边界，不调用逐帧 Teleport/SetActorLocation 追路径。

### 7.2 世界接口，操作层不得复制实现

以下沿用世界域的拟建名称。输入只消费结果；其最终参数结构、地图版本和错误类型由
`WORLD_CAMPS_AND_VISION.md` 维护，集成时绑定，不在本模块另建 Ground/LOS 算法。

| 世界域唯一接口 | 操作域用途/不能越界的职责 |
| --- | --- |
| `QueryGround(WorldRevision, FootCm, Capsule, SurfaceHint)` | SurfaceHint 是必填 FName 注册 SurfaceId，无默认、无三参重载。None/未知返回 InvalidSurface，实际支持面不匹配返回 WrongSurface；服务器重验真实地面/capsule，hint 不是落地证明。 |
| `QueryCompletePath(WorldRevision, FromGround, ToGround, Capsule)` | 地面移动；静态预览不用隐藏单位做动态障碍，partial path 明确失败。 |
| `ResolveObservedUnit(ObserverContext, Handle, Purpose)` | 请求收到、每次追击/AttackStart/单位施法时当前重查；内部目标只在服务端作用域使用。 |
| `EnumerateVisibleUnits(ObserverContext, Purpose)` | A 的合法候选帧；含 Match/ObserverLife/ViewGeneration/ViewRevision/MapRevision/ServerTime 和授权字段；帧不是永久执行权限。 |
| `FLDObservedUnitTermination{Handle, ObservationEpoch, Generation, TerminationSequence, OccurredAtServerMs, Kind=ObservedDeath}` | 世界在实际死亡时记录合法 pose/权限见证，经同一 Relay/WorldView 路径发布；服务器操作域绑定原订单后处理，客户端仅表现。不得由隐藏死亡状态、Conceal/stale/TargetUnavailable/FullSetRemoved 或离局清理合成。 |
| `FindApproach(ObserverContext, Origin, UnitHandle 或 PublicPOI, RangeCm, Capsule)` | 单位/商店的合法端点和完整路径；净空不扩大玩法范围，不能借目标真实隐藏位置求路径。 |
| `TraceSight(SourceGround, TargetSample, WorldRevision)` | 与阻挡行走分开的 LOS 结果/明确失败；最终攻击/效果几何合法性仍由 Combat 决定。 |
| `QueryCircleMembership` / `ExtractCircleCrossings` | 对实际已提交运动给出圈内/有序进出；世界不据此自己判胜，输入也不结算占领。 |

SurfaceHint 的类型/来源只有一套：地面点击从**公开地图地面 trace**取得当前 WorldRevision
中注册的 SurfaceId；小地图坐标转换后也取该公开支持面，不猜默认平面。
服务器重新检查地面与胶囊，客户端 hint 只是期望，不是权限或接地证明。
当前身体的支持面由权威 CMC.CurrentFloor 的真实有效 floor 映射 Registry，
不能使用历史缓存、屏幕落点或空 hint 向下投影；无合法 floor 返回 NotGrounded/不具占领资格。
POI 接近规划使用该 POI 注册的 SurfaceId。Ground-Present 载荷显式携带必填 FName；
NAME_None/未知值仍是可解析的失败输入，由世界返回 InvalidSurface，不能靠三参重载补默认。
Missing 目标不伪造脚点或支持面；其进攻意图前置仍遵守 §4.4。

终止记录沿现有可靠撤销/终止及全量恢复路径维护 Generation、ObservationEpoch、
TerminationSequence 的去重和因果，不另开能绕过撤销的影子通道。
OccurredAtServerMs 只供条目传输/显示；服务器绑定订单与提交顺序仍保留核心
OccurredAtSeconds 的真实时间，不能用同毫秒桶相等证明事件发生在新订单之后。

### 7.3 实际运动与连续占领

拟建 `FLDAuthorityMotionSegment` 使用核心 `FLDCombatBodyRef Body`、
`FLDAuthorityStep Step`、FGuid StreamId、uint64 SegmentSequence、WorldRevision、
FromFootCm/ToFootCm、double StartServerSeconds/EndServerSeconds 和运动类别。
一个身体的自愿运动/root motion/击退共用同一 Motion 流，不按输入来源拆成可重复记账的轨迹。
事件只来自权威 CMC 成功提交的实际运动子段；其生产/消费函数落在现有组件：

- `ULDCharacterMovementComponent::RecordAuthoritySegment(const FLDAuthorityMotionSegment&) -> FLDMotionReceipt`：
  在真实运动提交点收集实际子段；
  自愿走路、滑移/绕角、root motion、击退和必要落地都覆盖。碰撞尝试与最终提交分开，
  `FScopedMovementUpdate` 回退的试探段丢弃，不能产生虚假离圈。
- `ULDCharacterMovementComponent::PublishMotionThrough(const FLDProviderFence&) -> FLDFenceReceipt`：
  仅在该边界所有实际段已记录、
  世界查询成功且交给核心事件提供者之后，调用核心 PublishThrough。无移动也需封空流；
  不能在本边界仍可能发生动作位移/击退/落地时，仅因首个 CMC 更新结束就提前封口。
- 生命周期运动适配将世界提取的圈进出连同本流序号交给唯一占领 reducer；
  死亡、离局、保护变更由各自实际写入者另发资格事件和水位，不由 Input 猜测。
  无合法 CurrentFloor 的 NotGrounded 是明确的否定结果，应发布不具占领资格/离开有效集合，
  并完成已处理边界，不因投影 XY 在圈内继续计时。它不是查询系统故障；
  真正缺数据/查询未完成才不能造成功或推进缺失证据的水位。

与 `SESSION_MATCH_LIFECYCLE.md` §4/6 收敛的核心接口如下，均是拟建，定义由核心单点维护：

| 核心类型/函数 | 运动域对接责任 |
| --- | --- |
| `FLDMatchContext{Generation, LedgerMatch}`；`FLDVictimKey{Kind, EntityId, Life}`；`FLDCombatBodyRef{Victim, BodyIdentity}` | 不将 ContenderId、身体 GUID、life 数字或 LedgerMatch 混为同一身份；旧 body 退休有最终事件/水位。 |
| `GetMatchContext/GetAuthorityStep/GetAuthorityNowMs`；`FLDAuthorityStep{Match, StepSerial, StartServerSeconds, EndServerSeconds}` | GameMode 唯一实际时钟/步边界，CMC 使用此 step 而不是被补算回拨的 World 时间或客户端时间。 |
| `BeginAuthorityStep(Match, int64 EndAtMs) -> FLDStepPermitResult`；`ValidateMotionStep(Step, Body) -> ELDCommitReject` | 核心在真实已到达的整数毫秒边界推进原生 step；每次 authority CMC 处理都校验 step/body，包括 packed/saved moves，不能绕过或重复积分。 |
| `FLDCapturePresenceEvent.Stamp`：Generation/Provider/StreamId/ProducerSequence/OccurredAtSeconds/OccurredAtMs/RecordedAtMs/CommitOrdinal | 使用核心共有事件头；原始 double OccurredAtSeconds 保留真实先后，毫秒字段只分桶。核心按同刻阶段/因果分配 CommitOrdinal；消费者先按真实秒、再按已分配 Ordinal，不按 Provider/Phase 重排，不将同毫秒桶误作同时发生。 |
| `AuthoritySecondsToEventMs` | 核心 checked 向上取整的秒→毫秒转换，仅用于发生时刻，避免截止后的实际事件向前回填。水位只声明真正完成的时间，不能把此 ceil 直接用于任意小数秒的完成边界。 |
| `PublishCapturePresence(FLDCapturePresenceEvent{Body, Stamp, Inside}) -> FLDEventReceipt` | 世界 ExtractCircleCrossings 的真实变更逐项发布；单次事件失败不得继续报告已处理成功。 |
| `CompleteBodyMotion(const FLDAuthorityStep&, const FLDCombatBodyRef&, uint64 LastSegmentSequence) -> FLDMotionReceipt` | 仅确认初次原生基础姿态/段记录完成，不是提供者 Fence，也不宣布后续因果位移不存在。 |
| `PublishThrough(FLDProviderFence{Generation, Provider, StreamId, ThroughMs, ThroughSequence}) -> FLDFenceReceipt` | ThroughMs 只能取核心真正完成的精确整数毫秒 Boundary.AtMs；不能 ceil 小数完成点。缺序/查询失败不推进，退休流最后事件须等所属桶闭合。 |
| `AckConsumed(const FLDAuthorityBoundary{Match, BatchSerial, AtMs}&)` | 等 Combat 边界姿态读取及 Objective 均完成才释放已消费运动记录，不只等一个消费者。 |
| `FLDGameplayWritePermit{Boundary, Phase, GateRevision}`；`ValidateWritePermit`；`TryCloseMatch` | permit 只在当前提交栈有效。核心先关闸再取消；Navigation cleanup 不发新 Gameplay，快速 IsGameplayWriteOpen 不代替最终校验。 |
| `ALDGameMode::EndProtectionForAggressiveIntent(AController&, const FLDMatchContext&, const FLDCombatBodyRef&, const FLDAggressiveIntent&, const FLDGameplayWritePermit&) -> FLDProtectionReceipt` | 提交点从注册记录重新解析真实 Controller；Life 再核当前 Caller/Match/Body/Life/Alive/permit，从 Actions 真实边界生成资格 Stamp，不能由调用者伪造发生时间。后续执行拒绝仍保留其解除结果。 |
| `FLDAggressiveIntent{Kind, Request}` / `FLDAggressionRequestKey{Origin, Sequence, CancelEpoch}` | Kind 限 AttackUnit/AttackMove/BasicAttack/ActivateCharacter/ActivateEquipment；OriginCombat.Sequence 用 DirectSequence，不用尚未分配的服务器 Token，CancelEpoch=0；OriginNavigation 用原 Order/Epoch。派生 RepeatAttack 继承原进攻意图，不另发解除序。域先核新鲜性/异条款再调用 Life。 |
| `FLDProtectionReceipt{Generation, Body, Request, Outcome, ProtectionRemoved, LifeRevision, EventStamp?, Reason}` | LifeRevision 为 uint64；重复只重放，不能再发资格事件。动作/订单 Rejected 回执保留 ProtectionRemoved/LifeRevision；不与 OrderRevision/ActionRevision 混用。 |

核心在真正同一实际时刻的阶段序是 Session（离开/HostEnd/保护到期）→ Motion 证据 → Actions
（当前前置/识别进攻即解保护/完整准入/release）→ Effects → DeathsAndRewards → RespawnsAndWorld →
Transactions → Capture 闭合重查 → Close → Publish。
Motion 证据阶段不等于提前封闭所有可能后续产生的运动；Capture 前必须使实际提供者水位齐备。
保护到期等号在本刻 actions/hits 前生效；中间进出仍保留。本文只消费此单序，不另启动调度器。
同一个毫秒桶内可能先离圈、后保护到期；必须保留并先比较 OccurredAtSeconds，
不能因 Session 阶段排在 Motion 前就把稍晚的资格变化套到稍早的运动上。
核心边界经过 Collecting/Sealing/Closed；Sealing 前排空 Actions/Effects/Respawns/事务因果子批，
其产生的实际运动仍进同一 Body Motion 流。Closed 后收到的新意图进入下一真实 step，
不伪造 `last + 1` 时间。已有整数毫秒期限不经过 double 往返再次取整。

SegmentSequence 是实际运动段序；Stamp.ProducerSequence 是发布事件序，二者不能混用。
一条已接受直线段穿过圆圈可能产生进入和退出两个事件，每个事件递增发布序号，
不能共用一个去重键而丢掉退出。CompleteBodyMotion 使用段序，
Fence.ThroughSequence 使用本流已发布事件序；无圈变更也封其真实已处理边界。

精确 CMC 子步/碰撞提交挂接和原生流完整性属于 CT06 的实施门槛。
现有 `OnMovementUpdated` 是一次运动更新结束通知，**单独用它的首末位置不能证明所有中间子段**；
普通 Tick、客户端 correction replay、客户端时间戳和被 AdvanceAuthority 回拨的 World 时间
都不能为新的权威事件造历史。时间来自核心提供的当前实际模拟边界，不在本模块创建第二时钟。

世界的圈边界用合法地面脚点 XY 中心距和包含边界的规则；即使同批最终位置相同，
进入又退出/离开又返回仍逐项消费并破坏连续性。只在相关提供者水位齐备后判定到期，
完成同刻的死亡/离圈/争夺必须先纳入。纯受伤、仍在圈内的移动/控制/攻击不发假离圈事件。
新局/旧 life/重复序号拒绝，已消费事件按核心水位释放；不永久积累逐帧轨迹日志。
捕获不完整或查询失败要显式报错并阻止错误完成，不能退回帧末位置假装已经证明连续占领。

### 7.4 读取白名单

| 消费者 | 允许的数据 | 隐藏/失效处理 |
| --- | --- | --- |
| 自身 HUD/来源按钮 | 自身 owner 状态、来源 Availability、合法回执 | 不从检查对象或全局 PlayerState 补全自身缺项；Pending 不显示成成功。 |
| 检查/单位光标/预览 | 当前可见对手的名称、等级、血量/蓝量、状态、装备及必要观察句柄；外观身份按 Spec | 不显示技能、CD、其他属性或金币；装备展示定义而非私有实例/source readiness。Conceal/stale/life 改变清字段、目标与单位 LastSeen 标记，不缓存可更新 Actor。 |
| 小地图/镜头地面点击 | 静态地图和公开 POI、自身及合法可见单位 | 小地图标记不携带隐藏单位锁定；平移/缩放不影响视野来源。 |
| AI 策略 | 自身订单与来源、与真人等价的观察帧、公共圈信息 | 不遍历 GameMode 身体/账本全表；策略提交仍重新解析权限。 |
| 普通 Listen Host 主画面 | 与其参赛者相同的授权投影 | 原生 Mesh/附属 FX/声音与 proxy 统一裁剪，不因持有 authority Actor 全知或双重显示。 |
| 死亡后的镜头/检查 | 静态地图、固定地点、公共占领和自身持久信息 | 可自由拖动静态图；不跟随/观察隐藏玩家、不获得新活体视野；不能把死亡前单位快照保留为 LastSeen 标记。 |
| 错误/调试输出 | 自身安全错误码；服务器本地检查可记录必要身份 | 玩家回执不附隐藏坐标/真实拒绝对象；运行证据放忽略的 Artifacts，不广播给其他人。 |

## 8. 参数、单位与单一来源

所有首版玩法值及公式只从 `CONTENT_AND_BALANCE.md` 引用；本报告不复制数值或另造默认值。
下面是内容组已给出的稳定键/目录字段，**数值与部分内容仍待审**。
生成后走 SourceData → 既有 compiler/loader/contract → Content；游戏不读取本 Markdown。

| 正式键/目录字段 | 单位/所有者 | 本模块消费方式 |
| --- | --- | --- |
| `UNIT.MoveSpeedCmPerSec` | cm/s，内容→Combat 派生属性 | CMC GetMaxSpeed，非相机速度/AI 自设速度。 |
| `UNIT.CollisionRadiusCm` | cm，内容/世界碰撞 | 路径净空与接近点；不加减到普攻/商店半径。 |
| `UNIT.BasicAttackTargetMode`、`UNIT.BasicAttackRangeCm` | 目标枚举、cm，内容/Combat | 已确认初始近战单体的基础输入，不作为全局固定接近/搜索距离。 |
| `STAT.AttackRangeCm`、`FORMULA.AttackRange` | 当前派生 cm，内容/Combat | 预览、FindApproach、实际普攻范围与 A 搜索统一消费；装备/属性改变后刷新，不能继续使用初始值。 |
| `CHOICE.attr_reach.AttackRangeCmFlat`、`ITEM.item_echo_maul.Stats.AttackRangeCmFlat` | cm，内容/成长装备 | 射程成长的首版内容引用；实际投影只做一次，Input 不再另加修正。 |
| `STAT.BaseAttackSpeed`、`STAT.AttackSpeed`、`UNIT.BasicAttackWindupSec`、`FORMULA.AttackIntervalMs`、`FORMULA.AttackWindupMs` | 次/s、s、ms，内容/Combat | Navigation 只读可用性，Combat 在合法 AttackStart 固定本次前摇/NextAttackAt；取消不返节拍。 |
| `TARGET.AttackMoveAcquireRadiusCm` | cm，内容 | 引用当前派生攻击距离的 A acquisition；仅限可见候选，不复制其派生公式/值或偷用初始范围。 |
| `VISION.RadiusCm` | cm，内容/世界视野 | 由视野服务消费，不由相机或目标 UI 改写。 |
| `SHOP.InteractRadiusCm` | cm，内容/世界/商店 | 实际脚点 XY 中心距；路径到达容差不能代替成交范围。 |
| `ABILITY.active_arc_bolt.TargetMode/RangeCm/WindupSec/ProjectileRadiusCm/ProjectileSpeedCmPerSec` | 各斜杠字段沿同一完整前缀；目标枚举、cm、cm/s、s；内容/Combat | Direction 来源预览；不把失效 Unit 自动变为该方向技能。 |
| `ABILITY.active_guard_pulse.TargetMode/RangeCm/WindupSec`、`ABILITY.active_binding_snare.TargetMode/RangeCm/WindupSec` | Self/Unit、cm、s；内容/Combat | 自身释放或当前单位句柄；效果/三级说明从同一目录读取，不在 Input 写伤害或 Root 时长。 |
| `ITEM.item_phase_anchor.Active.TargetMode/RangeCm/DashDistanceCm/DashDurationSec/WindupSec`、`ITEM.item_arcane_battery.Active.TargetMode/RangeCm/WindupSec` | Direction/Self、cm、s；内容/Combat | 装备真实实例；位移经 CMC；无合法可移动段明确失败，不造免费穿墙。 |
| 无主动装备的 Active 定义语义 | 内容 schema 明确“无主动”，非加载失败默认值 | `item_iron_plate/item_edge_blade/item_runic_focus/item_siphon_emblem` 的 1/2 明确不可用，不回退角色技能。 |

距离采用世界/内容已对齐的**合法脚点 XY 中心距**；胶囊仅解决运动净空。
时间显示由服务器绝对期限计算；内容的秒到 Combat 毫秒只在该域集中转换，
客户端不会在图标到零时自行发一次免费动作。内容缺失/非法则启动或来源可用性明确失败，
不使用旧 PrototypeTuning 数字兜底。

本地键不属于战斗数值表：保留 `bEdgePan/bStartFollowing/Zoom/TextScale/Bindings`；
拟补 `CameraPanCmPerSec`、`ZoomMinArmCm/ZoomMaxArmCm`、`ZoomStepCm`、
`EdgePanBandViewportFraction`，由 `ULDControlSettings` 和 UI 设置模块维护单位/合法域/保存。
这些值只作用镜头，不影响世界距离、视野或角色速度；不在内容表复制一套相机值。
导航重算/无进展、回执容量、心跳周期/失联界限为工程限制，沿现有 liveness/导航边界集中定义，
与玩法半径分离，必须有限且可测试；不把现有 profile 常量宣称为批准的体验指标。

## 9. 复用、删除、新增与顺序

下表是后续实施的精确责任清单，不是本轮实际删改。`Public/Private` 从
`Source\Lockdown` 展开；引用架构 R/D 项时仍补本模块消费者。
“原样”仅指局部不变量，不代表整文件、旧模式或旧数值原样保留。

### 9.1 复用与改造

| 项 | 落点/保留能力 | 实际消费者、前提与改造 |
| --- | --- | --- |
| CT-R1 原样机制，R01/R02 | `Character\LDCharacterMovementComponent.h/.cpp` 的 saved move identity/不合并异身份、native correction；`Framework\LDCommandLiveness.h` 的单调存活性 | 真实拥有客户端、服务器和 AI 身体；同批剥离 sprint 位/B91，保留重力和原生位移。不能保留 `Enabled=false` 免权威路径。 |
| CT-R2 改造，R05 | `Navigation\LDNavigationComponent.h/.cpp` 的 Local/Server、serial/epoch、ACK/心跳/沿路径运动和相机 | Move/A/追击/商店及 AI 共用；Plan/ResolveOrder 内几何归世界服务，组件只消费唯一查询，不复制一份新寻路。 |
| CT-R3 改造，R13 | `Framework\LDPlayerController.h/.cpp`、`Framework\LDPlayerInput.cpp` | 动态映射、输入捕获、准备与确认、owner RPC。将 V05/profile 路由收敛为单流程，字段/声明/回调一起删改。 |
| CT-R4 改造，R13 | `UI\LDControlSettings.h/.cpp`、`UI\LDControlsPanel.h/.cpp` | 同一注册表、冲突检查、持久化与 HUD 标签；无静态 IMC 第二来源、无旧 profile settings。 |
| CT-R5 改造/抽取，R07/R09 | `Combat\LDCombatDirector.h/.cpp`、`LDTacticalRequest.h`、`LDContentActions.cpp/LDContentLifecycle.cpp` | 只保留 Combat 单次动作/命中/来源验证；持续追击及重复申请归 Navigation，删除另一份独立重复订单推进；源冷却不搬到 Input。 |
| CT-R6 改造，R11/R12 | `Core\LDGroundQueries.h/.cpp`、`Vision\LDVisionDirector.h/.cpp`、`LDWorldViewRelay.h/.cpp` | 世界模块提供 Query/Resolve/Enumerate；输入消费观察权限和合法几何，不修改私有世界存储。 |
| CT-R7 局部复用，R04 | `Character\LDCharacter.h/.cpp` 的 SpringArm/Camera/原生身体 | 相机和自己身体；删除旧职业/工具依赖由相应闭包负责，不因视野过滤销毁权威身体。 |

### 9.2 真正删除的旧部分

| 项/架构关联 | 明确删除落点 | 当前消费者、先接什么、完成顺序 |
| --- | --- | --- |
| CT-D1，D16/D24 | `LDPlayerInput.cpp` 的 `RebuildV05Mappings/V05Pressed/V05Released/V05Back` 旧分派、Skill3–6/Item3–4/Sprint/旧 Inventory/Development/Help 路由、左键商店/拾取分支；Controller 中对应声明/绑定/summary/legacy aliases | 先把有用实现迁到单一注册/统一按压释放和 UIRoot 层栈；`PointerService/TickAutomaticPOI` 的合法到达开店交给当前 ApproachShop 回执，删除旧独立轮询/触发，不留空壳。 |
| CT-D2，D11/D14/D16 | `LDPlayerController.h/.cpp` 的旧 Assassinate/Dash/Ward/Interact 按键入口、SprintStarted/SprintStopped/SetSprint 路由及旧服务 hold RPC；`LDPlayerInput.cpp` 的旧 held/release/profile 特例 | 先把当前装备 dash、目标确认和 Combat 取消接到实际来源请求；只保留必要 token/liveness 机制，不保留旧免费能力或站桩服务。 |
| CT-D3，D11/D14 | CMC 的 `bWantsSprint/SprintSpeed/bCombatWalkOnly` 旧 sprint/profile 分支、saved sprint 位，以及 B91 搬运 Cast；Navigation 中 B91/旧 site/pickup/service 分派 | 先有 Combat MoveSpeed/控制投影、世界查询和服务器调用者适配；同步 Serializer/CompressedFlags/回放/声明/include/测试后删。 |
| CT-D4，D04/D16 | Navigation 接受订单和 Content 动作里的 Trade 取消调用；Controller 的对应成员/反射引用；Development/Production/Context 旧 hold/取消/Pending 链 | 按架构 B02 与各领域断掉实际消费者，保留当前 Combat/Vendor/成长取消；不为保留一个 CancelFor 调用继续保留 Trade 系统。 |
| CT-D5，D11 | `Private\World\LDB91SourceControls.cpp` 的来源指针/键盘/观察分派及关联 B91 controller 方法 | 用现有动态输入/Combat 来源/世界 ObserverContext 接回必要能力后，随 D11 全图闭包删除文件与全部声明/调用；不复制其旧全图政策成 Demo 适配层。 |
| CT-D6，D15/D16 | `LDControlSettings` 的 Profile、TacticalDefaults/HybridDefaults、Skill3–6/Item3–4 及旧 Context/Trade 迁移政策 | 当前注册表和显式设置迁移完成后删除旧字段/分支；删除局内玩法保存入口不影响本地设置 Save。 |
| CT-D7，D24 | `Content\Lockdown\Generated\Input\IA_Move.uasset`、`IA_Aim.uasset`、`IA_Sprint.uasset`、`IA_Attack.uasset`、`IA_Dash.uasset`、`IA_Assassinate.uasset`、`IA_Interact.uasset`、`IA_Ward.uasset`、`IA_Help.uasset`、`IA_Menu.uasset`、`IA_Zoom.uasset`、`IMC_Gameplay.uasset` | 本报告选择唯一动态注册，不新增第二套 IA/IMC。先迁 Controller 静态 LoadObject 和存活 Blueprint/CDO 引用，再移除 Bootstrap Input 生成/固定 13 映射验证及加载/cook引用，经 C3 才物理删除这些包。 |
| CT-D8，D27/D29/D30 | `LDNavigationProbe.cpp` 的“快照接近但不追击”、旧 sprint/pickup/site 场景；`LDInputProbe.cpp` 旧 A 方向攻击、多动作槽；`LDHUDProbeExtension.cpp` 旧物品/技能/地图分派断言 | 保留墙/坡/错层/Stop/旧 life/隐私、无回退/无穿透及回中/持续跟随解耦断言并改 Demo 用例；删除旧玩法场景，不将它们 skip 或留 profile 回归。 |
| CT-D9，D30/D31/D33 | `RunMovementCheck.ps1` 的 MigrationSandbox 依赖；`RunNavigationCheck.ps1/RunInputCheck.ps1/RunHUDCheck.ps1` 旧 V05/profile/probe计数；`CaptureInputUI.ps1` 旧授予/场景参数及失去消费者的输入专用 helper | 改现有 runner 为当前组件/唯一 Demo 场景；按确切用例集合验证，不只降低成功数。旧其他领域 probe 与脚本随各自闭包真删。 |
| CT-D10，D34 | 系统手册中被本批删除的旧 profile输入、长按/旧槽位/静态资产说明及相应命令 | 与代码批次同步重写，保留准确 CMC/身份/输入/镜头技术知识；不另存旧稿或会话日志。 |

CT-D7 是当前选定输入生产方式下的目标删除集合，**未做二进制引用核验，不是现在可盲删的结论**。
如果 C3 发现存活 Blueprint 使用某包，先改该消费者使用唯一动态动作/输入边界并重存，
而不是以发现引用为理由永久保留第二输入系统。

### 9.3 新增落点

| 项 | 最小新增与所有者 | 真实消费者/前置 |
| --- | --- | --- |
| CT-N1 | `Public\Navigation\LDOrderTypes.h`：本节空间订单/回执/安全错误类型；具体状态与处理留现有 Navigation | Controller、AI、组件测试；先统一世界/Combat 公共类型引用，禁止循环 include 或庞大通用命令封包。 |
| CT-N2 | 现有 Navigation 的 SubmitOrder/CancelOrder/CommitActionHandoff/OnCombatOutcome 与单一当前订单推进 | 世界查询、Combat 单次动作和核心身份/终局；无需新导航 Manager。 |
| CT-N3 | 现有 Controller 的输入捕获/typed preparation，以及当前 ControlSettings 注册表与新增镜头字段 | UIRoot/ControlsPanel/HUD/Minimap 按当前接口接线；不增加第二 SaveGame。 |
| CT-N4 | 现有 CMC 的权威实际子段采集及运动提供者适配；必要窄记录声明放 `LDCharacterMovementComponent.h`，核心事件类型由生命周期域拥有 | 世界圈提取与核心水位；必须完成原生挂接验证，不能仅补 OnMovementUpdated 帧末采样。 |
| CT-N5 | `Private\Tests\LDOrderRulesTests.cpp`：订单/回执/捕获可分离纯规则；现有 Input/Navigation/HUD/Movement probe 原位改 Demo | 有真实服务与批准参数后接 owning-client 场景；不新建另一产品模式或旧 profile 测试地图。 |

### 9.4 每批依赖闭包门槛

执行架构 P0–P5，但本模块的实际次序必须包含：

1. 明确当前唯一调用者/动作/序列化支持集，协调共享 Controller/Combat/世界/地图负责人；
   列 include、RPC、委托、字符串加载、CDO、WBP 事件、IMC 与脚本消费者。
2. 先把保留的动态输入、权威命令/取消/来源和 CMC 机制接回单流程；
   混合文件先抽所需再删剩余，不能让移除 profile 的提交短路所有权验证。
3. C3 合并 AssetRegistry/Reference Viewer 的 hard/soft/management 引用和
   C++/配置/Bootstrap 动态路径。旧反射类仍可加载时，编译/迁移/重存存活 WBP/DataAsset/地图；
   不能把 C++ 零命中当作 UHT/Blueprint/资产无引用。
4. 真删无消费者的旧函数/字段/RPC/注册、生成资产/输入生产和测试；重启 Editor 加载全部存活根，
   执行 UHT、Editor/Game 构建、Bootstrap Validate、当前用例，核对有效配置没有回落旧默认。
5. 在隔离输出 Cook/Package 并读 staged/AssetRegistry，确认旧 Input 包/旧地图/旧载荷不再进入包。
   拥有客户端和正常参赛 Host 验证实际按键/重绑/观察，最后验证新局。

不修改共享引擎或插件，不手工文本合并 `.uasset/.umap`，不改生成 hash 来通过检查。
缺内容/几何只阻塞相关完整动作，不阻塞已明确的旧绑定、Trade 和重复输入生产闭包准备。

## 10. 可独立分派的任务与验收

### 10.1 实施任务

| 任务 | 负责人/落点 | 真正前置 | 交付与退出条件 |
| --- | --- | --- | --- |
| CT01 单一注册与捕获 | Input/UI；Controller、ControlSettings/ControlsPanel | I1 连接/阶段接口；当前动作清单 | 一次手势一个消费者；重绑保存/失败/恢复可见；删旧 profile 与第二映射生产路径。 |
| CT02 空间订单与回执 | Navigation/网络；LDOrderTypes、现有组件 | CT01 接线；世界 QueryGround/CompletePath、核心身份/写闸 | ground/Stop/heartbeat/高水位正确；无本地控制器前提的服务器入口；旧身份与过窗重放不执行。 |
| CT03 目标订单与攻击交接 | Navigation+Combat | CT02；世界观察/FindApproach；Combat Submit/Cancel/Outcome；批准的有效射程公式 | 右键接近/追击/重复普攻及 A 最小方案；失视野停当前追击；Queued 不抢取消；当前射程/群攻成长真实接线，AttackStart/命中归 Combat。 |
| CT04 四目标与来源按钮 | Input+Combat+成长装备/UI | CT01/CT02，SourceRef/Availability、当前目录和来源冷却合同 | Q/W 与 1/2 严格分离，准备/确认/release 复验；换槽/双持/出售无悬空按钮和冷却重置。 |
| CT05 商店/面板/小地图 | Input+世界+Vendor/UI | CT02、公共 POI、交互查询、真实商店会话回执 | 接近实际范围再 Open，旧回包不开店；UI/小地图无穿透、无隐藏锁定、不中断联机模拟。 |
| CT06 实际运动事件 | CMC+世界+生命周期/Combat | CT02，正式实际时钟/提供者水位/圈查询，批准圈几何 | 原生子段/提交与回退挂接经 UE5.8.4 验证；同帧进出不丢失、无客户端补造事件、截止同刻正确。 |
| CT07 镜头与失焦 | Input/UI；现有 Character/Navigation | CT01；静态地图边界；按已确认白名单接入观察/死亡状态 | 固定角/平移/回中/跟随/缩放/保存真实工作，失焦不续旧输入；死亡可拖静态图，无新活体视野或隐藏跟随。 |
| CT08 源码/资产/门禁闭包 | Runtime+Editor/资产负责人 | 相应 CT01–07 接线及 C3/C4；不是等所有模块完成才清理 | CT-D 逐项消费者归零；UHT/加载/Cook 无悬空依赖；旧玩法专测真删、有用不变量迁完。 |
| CT09 真实连接整合 | 网络 QA+各消费者 | 当前切片 CT01–08；完整玩法用批准内容 | 正常 Listen Host+拥有客户端；最后 3 真人+3 AI，报告当前结果，不用旧日志或纯 Host 截图代替。 |

CT06 的时钟/提交挂接是明确技术集成门槛，不应包装成要求用户选择算法的问题。
各任务完成自己的组件切片不等于 I2/I3/I5 或整个 Demo 已验收。

### 10.2 正反场景

下列是**拟新增/替换用例 ID**，不是现有自动化 selector。
各场景记录输入 identity、Combat RequestKey→Token 绑定、OrderRevision/ActionId、拒绝码、
实际脚点及必要观察游标，
正好覆盖所述不变量；不能记录或广播对手私有构筑来“帮助 UI 调试”。

| 用例 | 前置与操作 | 可观测正例/反例 |
| --- | --- | --- |
| CT-A01 地面/路径 | 真拥有客户端沿墙/坡/转角点击；None/未知 SurfaceHint、错误支持面、错层/越界/不可达/非有限输入；核对 trace、CurrentFloor、POI 三种来源 | 四参 QueryGround 显式拒绝 InvalidSurface/WrongSurface/NotGrounded，不靠三参/空hint投影兜底；仅完整合法路径 Activated/Arrived，CMC无穿墙/瞬移。 |
| CT-A02 追击普攻 | 可见合法目标在射程外，右键后移动目标 | 实际接近、范围内 AttackStart、按 Combat 节拍重复；目标走远可合法重算；旧“只到快照不追击”断言被替换。 |
| CT-A03 A 搜敌 | A 尚未点击；确认地面后出现两个等距可见候选，随后收到原订单匹配 epoch 的授权 ObservedDeath/空集 | 按下 A 不攻击；只在批准 acquisition 范围内确定选择；合法终止仅推进服务器绑定的原单，空集续行/完成，不永久挂机。 |
| CT-A04 视野撤销 | 追击/前摇中 Conceal；注入 FullSetRemoved、旧快照/ACK、重复或旧 epoch 的终止、早于新订单接受的 ObservedDeath，再次可见 | 取消锁定、停止当前追击；不走 LastSeen/旧 A 目的地、不留单位标记。非授权死亡不能触发 A 重选；旧终止/显示消息及重现都不驱动新订单或绕过命中权限。 |
| CT-A05 停止/乱序 | 发路线/能力后在 Token 回包前 S；取消比请求先到；延后旧 ACK/Order/心跳/saved move/ActionOutcome | 按 RequestKey/Direct水位或Nav epoch取消；不拿 ThroughDirectSequence 比 Token，不因无 Token 漏取消；不续走/续打，合法重力和已释放内容保留。 |
| CT-A06 Queued/准入 | 原合法路线进行时发无效新施法；另让新施法合法准入后在 release 前失效 | Queued 不先停旧路线或解保护；Actions 当前前置通过后进攻意图先解保护；执行拒绝不替换旧单，release 失败不恢复历史路线/扣未提交 MP/CD。 |
| CT-A07 身份/重复/过窗 | 同条款/异条款/过窗重放、旧身份；DirectSequence与AttackCycle数值相同；外部伪造Nav键 | Direct/Nav完整判别键不撞号，独立准入由Combat分配Token；同键只重放原映射；旧/异条款/伪造拒绝，不按槽位或名字解owner，RepeatAttack不重复生成解除保护事件。 |
| CT-A08 Q/W 与 1/2 | 学两个角色主动、装备两件同款主动；连续按1/2，再把一件替换为无主动装备 | 两份主动可分别释放且仅进入自己的 CD，不误用同名 family 共CD；属性相加、同名被动一次由装备域证明；无主动不可用。 |
| CT-A09 来源变化 | 准备后升级/换槽/遗忘/出售，确认后 release 前再次改变；等候后重购/重学 | 旧来源请求失败而不是施放新内容；零部分耗费/免费刷新；失源期间继续计时，重新持有沿用原结束时刻，不冻结、不因清 UI 消失。 |
| CT-A10 四目标 | 使用实际目录和必要显式夹具验证 Unit/Ground/Direction/Self；越距/零方向/错层/失效句柄 | 预览模式与定义一致，无效不消费；不改目标类型、不自动近邻/地面/自身回退、不自动走近施法。 |
| CT-A11 装备主动非交易 | 店外、圈内、装备满槽使用 1/2；资源不足/控制禁用 | 符合战斗准入可 release，不查报价/购买余额/空槽；失败是正确战斗原因，未解绑装备。 |
| CT-A12 UI 手势/Esc | 文本 S/A/Q、按下跨 UI/世界与列表滚轮；Shop/Choices 主页外 Armed 后开买卖/替换子确认，再 Esc，并注入残留 Armed | Choices主页外仍可世界操作；OpenChildConfirmation清Armed且capture抑制至释放，模态不再Arm。Esc只关子层、不越模态、不Stop/RPC/消费Offer；普通panel可先取消Armed，无穿透/暂停。 |
| CT-A13 重绑/失焦 | 多次重绑同一动作、键仍按住时重建映射、保存失败/重启、Alt-Tab 再回来 | 一个物理周期一个动作/委托；退休键不再触发；明确未保存；恢复焦点不自动移动/施法。 |
| CT-A14 相机/小地图 | 边缘/方向键/Space 短按与按住/Home/滚轮和 UI 捕获组合，镜头移到迷雾外 | 固定俯视角，临时回中不改变持久跟随；准备瞄准时边缘不平移；视野/角色位置/射程不变，不借地标下达隐藏目标命令。 |
| CT-A15 商店 | 右键接近；实际距离边界内/外、被击退、死亡、替换订单后接收旧 Open | 内可 Open、外拒绝；路由 acceptance 不放宽商店半径；旧回包不开店，交易仅由 Vendor 当前会话成功。 |
| CT-A16 生命/终局/新局 | 旧生命有 Pending/追击时死亡；终局后重放；同房间新局 | 旧动作不能操作新身体；构筑/钱/冷却按 Spec 保留或新局重置；终局先关闸，清理无新副作用。 |
| CT-A17 AI/Host 等价 | 普通参赛 Listen Host、拥有客户端、真实服务器 AI 给等价合法/非法意图 | 同一准入/错误/资源/距离/权限；AI 不造连接/LocalPlayer；Host 不全知、不产生重复原生+proxy 表现。 |
| CT-A18 连续占领 | 真实 CMC 走路/滑移/位移，单批出入且帧末相同；CurrentFloor失效但投影XY仍在圈内，再合法落地 | 全部真实进出被消费，回退试探不造事件；NotGrounded发布不具资格而非假接地或冻结水位；圈内合法移动/受伤不误清零。 |
| CT-A19 同刻/水位 | 截止前缺运动、同刻死亡/争夺、截止后进入；单段穿圈入/出；同毫秒桶先离圈后保护到期；晚因果阶段位移 | 先按实际秒区分先后，再用核心已分配 CommitOrdinal，不按 phase 重排因果子批；不能把先离圈者算成曾有效占领。两事件独立去重；仅闭合桶 Fence，初次 Motion 不提前封口；旧请求/重复无效，已接受前缀仍完整消费且结果一次。 |
| CT-A20 资产/运行闭包 | 重启 Editor/包，逐个实际动作/重绑/新局；读取 staged 输入包清单 | 没有废弃 IA/IMC/旧入口依赖和 MissingClass/加载警告；Bootstrap 不重造删除资产；无旧 profile “兼容通过”。 |
| CT-A21 射程/群攻成长 | 初始近战单体打野后，用真实属性选择/装备取得射程和群攻；在追击中增减射程 | 预览/接近/实际 AttackStart/A 搜索都读 STAT.AttackRangeCm；不清已付攻击节拍；一次主命中产生 Combat 群攻，不生成隐藏次目标订单或新快捷键。 |
| CT-A22 进攻意图与保护 | 保护期确认无效目标/无支持面/InvalidSurface/越距/无蓝/CD/空来源/过期绑定；A无路；对照Armed、Move/Shop/Stop及旧包/畸形 | 世界语义失败不伪装Malformed或被本地吞掉；当前前置后仍解保护并发一次资格事件，Rejected明示变化且不释放/扣资源；对照/旧包不解，旧life不影响新生命。 |
| CT-A23 死亡选择 | 死亡时打开保留的三选一并确认/替换，再在复活前后重发旧确认 | 合法一次构筑变化，候选不重抽、机会不丢失；不复活/治疗、不以无 Pawn 拒绝合法选择；旧请求/重复仍按成长契约拒旧。 |
| CT-A24 检查/死亡白名单 | 可见对手变化装备/血蓝/状态后离开视野；自身死亡后拖镜头、回中/跟随、查看公开圈 | 可见时仅许可名称/等级/血蓝/状态/装备；无技能/CD/其他属性/金币。隐藏即清选择和单位标记；死亡自由看静态图但不跟踪玩家/获得活体视野。 |

真实网络用例至少覆盖**可视且正常参赛的 Listen Host + 独立拥有客户端**，最终覆盖
Host+两拥有客户端+三 AI；无 probe 全知特免。对订单/动作/观察分别注入丢包、延迟、
抖动、重复和乱序；网络配置是测试条件，不当作未经批准的产品性能指标。
纯规则夹具不代替真实 CMC 运动，nullrhi 不证明实际 UI/镜头/Host 隐藏表现。

### 10.3 现有检查的精确复用边界

现有 `RunNavigationCheck.ps1`、`RunInputCheck.ps1`、`RunHUDCheck.ps1`
支持 EngineRoot、PackageRoot、Region、RoundTripMs、JitterMs、OneWayBaseMs、Port。
RoundTripMs 当前只允许 0/100/150，JitterMs 为 0–25，OneWayBaseMs 为 -1–100；
不要臆造 `-Profile Demo`、`-TestFilter` 或任意 RTT 选项。
它们当前仍是旧场景和固定旧成功计数，不表示 CT-A 场景已经存在。

对应入口：
[Navigation](../../Scripts/RunNavigationCheck.ps1#L2-L27)、
[Input](../../Scripts/RunInputCheck.ps1#L2-L27)、
[HUD](../../Scripts/RunHUDCheck.ps1#L2-L27)、
[Movement](../../Scripts/RunMovementCheck.ps1)、
[RunTests](../../Scripts/RunTests.ps1)。
迁移这些 runner 和 probe 的当前用例集合，保留 missing/failed/notRun/inProcess 报告失败门槛；
废弃 sprint/快照不追击/旧 A/旧槽位断言真实删除，不恢复旧 profile 来让场景通过。
新接口/资产批次再用 Editor/Game 构建、Bootstrap Validate 和 Cook；
本轮文档不运行昂贵 Unreal 场景。

## 11. 审核状态、产品门槛与限制

### 11.1 已确认规则与剩余实施门槛

技术命名、单一注册表、局部回执/捕获结构和合理确定性排序由工程实施；
批准前的内容和行为方案保持“待审”，不作为运行输入。
初始近战单体、首版群攻/射程成长，以及双持属性/被动/独立主动 CD/失源继续计时均已确认；
发出进攻意图即解除保护（无效执行也解除）、死亡可确认持久选择也已确认。
失视野停止当前追击且无单位 LastSeen 标记、死亡静态自由镜头及可见检查白名单也已确认。
**本模块不再将这些行为列为待用户选择。** A 搜敌/平局等工程交互建议和具体目录、
数值/公式仍需 PR 审查，不能从结构规则已确认推导已完成平衡验收。
核心时间/body key/水位/写闸已按 §7.3 绑定当前跨域拟建合同，仍须联合审核；
CMC 子段挂接需要原生验证。这是明确工程前提，不要求用户替工程师选择回调或字段类型。

| 门槛 | 剩余条件 | 不能据此宣称 |
| --- | --- | --- |
| 内容/公式 | 当前有效射程、动作几何/时序、实际目录和本地设置合法域完成对应审查/导入 | 初始形态已确认就等于数值已批准或完整发育已平衡。 |
| 原生/跨域 | CT06 子段捕获、核心原始秒排序/提供者水位/写许可与 Combat/World 联合接线 | 有类型表或单帧位置就已经证明连续占领。 |
| 资产/运行 | C3/C4、UHT/Editor/Game、Bootstrap/Cook，以及真实拥有客户端/Host/AI 完整场景 | 文档或旧 profile 检查可以代替目标 Demo 的运行通过。 |

### 11.2 专家与独立审核

| 实际 agent | 独立职责 | 结论状态 |
| --- | --- | --- |
| `controls-moba-expert`，`52d034d0-df43-4c6a-ade8-2d39faf2f7cd` | MOBA 操作、目标/订单体验、镜头与失败体验；不作者兼任 QA | 完整研究已返回并整合：右键命中分派与 A 是实质缺口；保留准备/接受/释放分界、输入捕获和相机机制，明确实际面板层级。 |
| `controls-unreal-expert`，`6bd2b09d-2dbe-4511-a7af-4db668240de4` | 独立源码/Enhanced Input/CMC/网络/资产和门禁依赖研究 | 研究与有限证据补全已返回；可复用基础存在，但目标追击/A/统一目标路由和实际运动事件仍欠接线。 |
| `controls-network-qa`，`a1599240-57b8-400d-86c2-cf8ad91ffc03` | 两份完整研究及整合稿齐备后的独立整稿审核；不兼任研究作者 | 静态审核有条件通过，未发现阻塞文档缺陷；不将已有源码、正确链接或文档完整性视为运行通过。 |
| `controls-contract-qa`，`751f96d6-a553-4bec-81b6-7b1b6ca90062` | 共享核心合同和新增产品边界的独立专项复核；与主体审核者不同，未参与写作 | 最终增量静态审核有条件通过；收件/许可激活、进攻意图资格变化、射程成长与事件水位等对接无未关闭的阻塞审核项。 |

两份独立研究、主体审核与契约专项复核共同覆盖当前报告。
**结论是文档有条件通过，不是全部实施门槛已关闭。**
剩余条件为相应内容/公式批准，以及 CT06 原生运动、资产/UHT/Cook、
真实拥有客户端/正常 Host 和完整场景的实际验证。

本轮当前只核验静态文档/源码与公开引擎资料，未构建 UE、加载 Blueprint/地图、
运行生成器/Bootstrap、Cook、真实联机或数值平衡测试。
文档完成与“模块已实现/全部 D1 已封口/完整 Demo ready”是不同结论。
原始研究/审核留在会话记录或会话工件；仓库报告只保留当前方案、实际身份和最终结论，
不复制聊天全文、撤回方案、旧稿或逐轮过程日志。

## 12. 已读取的公开引擎资料

通过 Context7 官方 Unreal 文档集合检索并读取 Epic 原文，核验日期为 2026-11-01。
检索没有独立的 UE 5.8.4 补丁集合；这些通用机制支持方案选择，
不替代该锁定版本的项目编译和实际运动/输入验收。

| 来源 | 本报告使用范围 |
| --- | --- |
| [Epic：Networked Movement in CharacterMovement](https://dev.epicgames.com/documentation/en-us/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine) | saved moves、自定义网络 move、authority 重现/客户端 correction、root-motion source；不另造位置积分器。 |
| [Epic：Enhanced Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine) | LocalPlayer context、动作触发阶段、上下文优先级和运行时重绑；不自动证明 UI 手势独占。 |
| [Epic：FInputModeGameAndUI](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FInputModeGameAndUI) | UI 未处理仍可进入玩家输入，因此还需要明确捕获/消费。 |
| [Epic：OnCharacterMovementUpdated](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Character/OnCharacterMovementUpdated) | 是运动更新末端事件；不能将单个首末位置对当作所有中间真实子段。 |
| [Epic：FScopedMovementUpdate](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FScopedMovementUpdate) | 延迟提交/回退与 pending overlaps；实际事件必须排除未提交的试探运动。 |

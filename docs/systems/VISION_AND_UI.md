# 视野过滤与 UI 状态

战争迷雾首先是服务器的读取权限，不是给已经收到的敌人信息盖一层黑色材质。
本文说明当前观测与表现组件；产品显示内容以 [App Spec](../APP_SPEC.md) 为准。

## 权威观测链

| 入口 | 责任 |
| --- | --- |
| `Vision\LDVisionDirector` | 服务器侧。`FLDSightGeometry` 做静态视线；`FLDObserverLedger` 管逐观察者的句柄、epoch、revision、ObservedDeath 事实和 Nav pin；导演按核心时钟采样并发布，处理死亡记录、命令解析和视线查询 |
| `Vision\LDViewTypes` | 冻结的 `FLDWorldView` 观测帧、隐藏通知与终结通知的线缆格式和校验；保存与加载都会校验 |
| `Vision\LDWorldViewRelay` | 每个拥有连接一个 relay：接收游标、超时清除、代理与迷雾表现 |
| `ALDObservedEntityProxy` | 客户端非复制的人形表现，无碰撞，不参与权威战斗或资源所有权 |

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
- 参赛者单位只携带 SC-07 白名单：参赛者 Id（客户端经公开名册换成名称）、等级、HP/MP、公开状态、两件装备。
  不含技能、冷却、完整属性、金币或规范身体身份。
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
- 0.25 s 收不到 full 帧就清除全部单位并标记 stale：不外推位置，不保留 LastSeen。
- 换局（`OwnerState.Match`）、观察者生命变化、断线、局终和非法载荷（`ProtocolRejected`）都会清除状态。
  属于其他世界修订的帧会被拒绝。
- 清除时销毁对应代理及其表现。
- 迷雾是覆盖在注册地面边界上的 128² 纹理：当前覆盖亮，其余调暗以便看清静态地形；没有探索层。
- 采样节奏和帧格式（schema 1、覆盖位图 2048 字节）在代码中维护，不在这里设定 Demo 平衡值。
  采样按核心时钟排程：普通迟帧保持原相位，停顿后跳过错过的间隔，只采当前姿态一次，不补发历史批次。

## UI 只读模型

| 信息 | 允许来源 |
| --- | --- |
| 自身血蓝、护盾、临时状态与被动 | Combat 的仅本人复制 `ALDPlayerController::CombatView`（同 Match/Life 才使用；没有身体时退回 Lives 投影的血蓝）；`LDHUD::SelfStatusLine/PassiveLine` 只格式化，被动按装备来源各显示一次 |
| 自身等级、经验、零花钱、装备与待选 | Growth 的 `ReadOwnProgression/ReadOwnEquipment`（`FLDOwnerState.Growth`）；满级显示 MAX，缺失显示“同步中” |
| 自身生命、保护与复活倒计时 | Lives 投影的 `LifePhase`、`ProtectedUntilMs`、`RespawnAtMs`，以及 `ReadDemoAuthorityTimeMs` 给出的同局权威时刻；读不到时显示“同步中”，不用本地时间或 0 代替；倒计时在该时刻到达截止前不显示 0，显示本身也不解除保护 |
| Q/W 与 1/2 | `CombatView.Entries` 经 `LDHUD::ActionInput/Action` 把意图可提交性（本人当前 Match/Life、对局进行中、存活、未被子确认或文字编辑占用）与执行限制（空槽、无主动、冷却、胆量、控制、服务器原因）分开；图标与热键同走 `ActivateCombatEntry`，限制只置灰说明，不吞掉显式激活；本人战斗投影缺失时显示“未同步”且不发送。准备态读 `ReadArmedCombatEntry/IsAttackMoveArmed`，回执 `LatestCombatReceipt` 分开提示“未执行”与“复活保护已解除” |
| 被选中的其他单位 | 当前帧中 Handle 与 Epoch 都匹配的单位，经 `LDHUD::ObservedCard/ObservedUnit` 只显示白名单 |
| 小地图 | 注册地面边界、静态阻挡（区分阻走/阻视线）、小卖部、升旗台圈、两档营地标记；当前覆盖与当前可见单位；不画探索层或出生点 |
| 属性 P | `ULDAttributesPanel` 只格式化 `CombatView.Attributes` 的各来源与最终值、已学被动、装备被动和临时状态；行固定，刷新不重置滚动 |
| 三选一 | `ULDChoicesPanel` 读当前 Offer 的三张固定卡，命令带 OfferId/OptionId/各修订；Q/W 已满时进入替换子确认，不默认槽位；同一机会的修订刷新保留原选项 |
| 小卖部 | `ULDShopPanel` 读当前 Opened 会话报价（`ReadOwnShop`）与本人两槽装备；只有 `OnShopOpenResult` 的 Opened 与 Combat 的 `ReadShopInteraction` 同代次、同生命、同小卖部时才开页，迟到或旧交互结果不弹出；远端客户端在下一次成长发布前先用 Opened 自带的报价。买卖命令带所显示的会话、报价与修订，卖出需子确认；死亡、换命、换局或会话结束自动关闭，受伤不关闭 |
| 公共圈、击杀通知与结果 | `ALDGameState::PublicMatch`（唯一阶段来源，校验代次）、`KillFeed`（按序号去重，只含参赛者死亡）、`MatchResult`（与 `PublicMatch.ResultId` 一致才显示，缺失时“结算同步中”而非零表）；倒计时到零只等待权威状态，不宣布开放或胜利 |
| 命中、失败与进度 | 经裁剪的自身反馈，而非完整服务器日志 |

成长请求一个家族同时只有一个在途：面板在提交前先用 `PeekGrowthRequestId` 记下将分配的序号，
因为 Listen Host 会在提交调用内回答；只在 Controller 也结束该请求时（Final、未保留序号的拒绝、
Final/过期回执）解除等待。

名称来自复制的 `ALDGameState::DemoLobby` 名册，查不到时显示“未列名参赛者”。
技能、装备、野怪和状态的中文名经 `LDHUD::ContentText` 从 `DemoContent` 字符串表读取；
表或键缺失时显示稳定 Id 并记录 `LD_HUD_DISPLAY_TEXT_MISSING`，不猜名称。
`LDHUDModel`、`LDHUD`、`LDMinimap`、`LDUIRoot` 及各 Panel 是主要 UMG/Slate 入口。
属性、三选一、小卖部三个普通主面板由 `LDHUD` 创建并停靠在自身画布上（命中测试只算面板本体），
并向 `ULDUIRoot::RegisterMainPanel` 登记；同一时刻只开一个，权威事件用 `ShowMainPanel` 打开，
不会顶掉玩家随后打开的菜单或设置。

受控角色、检查对象、准备中的动作、指针捕获、镜头模式和打开面板分别管理。
选中另一角色并不改变技能/装备按钮的所有者；禁用按钮只是反馈，不能代替服务器检查。
Widgets 不得遍历所有 Pawn/PlayerState 来补全 Tooltip 或小地图。

## UI 交互与错误

输入捕获、焦点、Esc、菜单与镜头的共同约束见 [移动与输入](MOVEMENT_INPUT_AND_CAMERA.md)。
界面使用当前映射注册表显示按键，不能写另一份易失效的按键表。
等待、成功、失败、失去权限和过期状态需区别显示，不以本地超时虚构成功。
本地设置的类型化加载/保存、显式恢复与显示试用生命周期见
[移动与输入](MOVEMENT_INPUT_AND_CAMERA.md#本地设置与显示试用)。
设置页的显示草稿与已应用偏好分离，终局强制关闭调用 `ForceClose`，
不能把关闭、销毁或旧确认回调当成保留显示试用。

数据更新应围绕已有快照/回执和局部变化，不为每个 Widget 新增一次全世界扫描。
增加 HUD 字段时同时检查服务器是否授权它、离开视野如何清理、life 改变如何失效，
以及对应提示、特效和辅助显示是否绕过了同一限制。

## 检查入口

- `Lockdown.Rules.DemoVision.{Codec,Privacy,Ordering,Handles,Termination,Sight}` 覆盖线缆、白名单、游标、句柄与 pin、终结和视线几何。
- `HUDObservationBoundary`（`LDHUDRulesTests`）覆盖检视器白名单和地图换算。
- `HUDLifeStatus`（`LDHUDRulesTests`）覆盖自身生命行：权威时刻缺失时显示不可用，倒计时在截止前不显示 0，死亡不显示金币损失。
- `HUDOwnResources`、`HUDActionView`、`HUDPublicMatch`、`HUDContentParameters`、`HUDCombatOwnerView`（`LDHUDRulesTests`）
  覆盖本人资源与等级、Q/W/1/2 意图与限制分离、公共圈/击杀/结果、内容参数行与原因文案、本人状态/被动行与战斗回执提示。
- `ViewPrivacySchema`（`LDViewTests`）覆盖公共 GameState（只有 `DemoLobby`、`DemoClock`）以及 Lobby、DemoClock 的结构。
- `RunDemoVisionCheck.ps1`（`Tests\LDDemoVisionProbe`）：参赛且渲染的 Listen Host 与两个拥有客户端经正常大厅开局，
  三具身体走到公共占领锚点后，检查拓扑、客户端受限注册表、Host 只经自身投影呈现他人（含生成当帧隐藏）、
  单连接丢帧（引擎网络模拟，非 Shipping）后的超时清除与无 LastSeen，以及客户端断开后 Host 不再呈现离开者。
  正常开局门禁打开前它无法运行；未实际运行不算通过。
- 必须观察真正受限的客户端及其可访问记录，包括 Listen Host 上非本地原生身体是否隐藏；不能只看服务器全知视图或一张截图。

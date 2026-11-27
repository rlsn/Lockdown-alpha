# 视野与界面手册

本页记录服务器视野授权、迷雾距离场渲染、头顶世界标签、小地图、局内无滚动 HUD、构筑工坊、蓝图查看、设置面板与音频管线（`LDVisionDirector`、`LDWorldViewRelay`、`LDFogDistanceField`、`LDWorldLabels`、`LDMinimap`、`LDUIRoot`、`LDHUD*`、`LDWorkshop`、`LDChoicesPanel`、`LDAudio*`）。

## 服务器视野授权与迷雾表现

- **权威视线与复制隔离（`LDVisionDirector` / `LDWorldViewRelay`）**：
  - 参战身体对隐藏观察者关闭引擎自动网络相关性，客户端绝不持有视野外敌方参赛者或野怪的实时坐标/血量/状态。
  - 服务器对每位观察者在 `VISION.RadiusCm`（`1120 cm`，停电日 `840 cm`）基础范围内，以脚点高差平滑调整水平半径（`75%–125%`），用 `120 cm` 虚拟眼高检查目标脚下地面和实物遮挡。短暂遮挡有 `0.3 s` 宽限，超出有效半径立即失视；仅向本人连接下发授权帧 `FLDWorldView`（含可见单位快照、可见弹道/战术地面实体/清营回复球/预警及 `512 × 320` 方格覆盖位图 `CurrentBits`）。`CurrentBits` 只用于显示（PK 观战与队友视野合并、弹道收尾特效），不授权单位或弹道：观察者与 PK 队友移动时，各自的覆盖最多每 `0.25 s`（`FLDObserverCoverage::MovingRefreshSeconds`）重扫一次，首次采样、换命与世界版本变化立即重扫。清营回复球及《甩你一鞋》落点拖鞋、《警戒封条》双端地面光带、《幻影脱壳》诱饵残影、《疯狗乱拳》穿透拳风与《反应装甲》破片迸射全部经 `FLDWorldView` 视野裁剪后渲染，脱离视野或拾取/到期立即销毁。Relay 在世界销毁/关停时由 `IsShuttingDown()` 拦截迟到 RPC。
- **迷雾后处理（`LDFogDistanceField` / `MI_FogMist`）**：本机用自己的存活脚点和当前关卡原生碰撞计算同边界方格覆盖，分帧采样并在完整计算后更新地面雾层和小地图。PK 的额外观察范围使用服务器授权。距离场驱动 `MI_FogMist` 的边缘和世界坐标噪声；单位、选中和弹道权限基于服务器观察帧。显示值向新距离场缓动时，只重算并上传仍在变化的那块矩形（`LDFogDistanceField::Difference` / `Blend`），不是整张 `512 × 320` 纹理。
- **头顶血条、遮挡剪影与受击反馈（`LDWorldLabels`）**：
  - **LoL 式分段缓冲血条**：每 `250 HP` 一小格、每 `1000 HP` 加粗黑刻线（直观体现肉度），护盾层以银白段拼接在当前血量右侧，扣血带白色受击滞后缓冲层（`AdvanceBarTrail`，精确按超出保持期的时间衰减，帧率无关），并配状态胶囊徽章（如 `破防×3`、`灼烧×5`、硬控外圈倒计时）。
  - **墙后半透剪影与悬停描边**：对当前 `FLDWorldView` 已授权可见但胸部被静态场景遮挡的本方/敌方参赛者（`70 ms` 节流射线，穿透 Pawn 与可移动组件追踪静态墙），绘制 `25%` 不透明度胶囊剪影（本方 `#7DD3FC`、敌方 `#F87171`）；鼠标悬停可见敌方时叠加 `1.5 px` `#FF4D4F` 锁定支架。

- **场景遮挡淡化（`LDPropFade`）**：带 `LDFadeable` 的可见组件，其材质均支持 `LDFade` 时可按本机镜头和已获授权的身体淡化。普通不支持淡化的组件保留原始显示和投影。
- **篮球手（`ULDBasketballAnimInstance`）**：本人、可见代理和倒地副本共用 `ULDBasketballMotionSet`。运球从最终掌心到地面反弹，急转归正姿势后混合，单手普攻和双手技能对齐已有出手时序。动作导入使用 `ImportDemoArt.ps1 -Mode ImportBasketball`，由 `Lockdown.Rules.Art.BasketballRuntime` 验证。

## UI 只读模型

所有局内界面严格遵守**零滚动视图（No ScrollView）**原则（仅主菜单、Lobby、局内菜单、设置页、构筑蓝图查看与 DEV 测试面板属于非局内界面可滚动）；全部文本走 `SourceData/UI/ui_fonts.json` 导入的思源黑体（`FF_Regular/Bold/Heavy`），局内数字、键位与计时用同一复合字体中的 `FF_Figure`（Roboto Bold Condensed Italic，Apache 2.0，缺字回落 Heavy）；战斗色令牌在 `LDHUDStyle.h`，直播风格表现令牌在 `LDBroadcastStyle.h`。

| 区域 / 面板 | 真实实现行为与数据来源 |
| --- | --- |
| **底部操作条（`ULDHUD`，直播风格）** | 席位号块（席位色 + 号码，其下三格普攻连招段位）、等级与经验条（悬停看数值）、生命大数字与分段条（每 `250 HP` 一格，满血池过高时每 `1000 HP`；白色受击拖尾、护盾续接；低血转红，色盲模式转金）、劲条与爆劲刻度；`Q/W/E` 主动槽（元素角标、等级刻度、冷却遮罩与秒数、缺劲提示、硬控/禁用角标 `定身/沉默/眩晕/嘲讽/击退/缴械/禁`）与 `D` 冲刺槽，槽下键位色带（可行动橙、冷却/受限暗、被拒红）。条带与数字由 `LDBroadcastPaint` 绘制，样式令牌取自 `LDBroadcastStyle.h`，数字用 `FF_Figure`（Roboto Bold Condensed Italic）。`ULDStatBar` 不在条上：它是构筑工坊加点的提交队列（HUD 每帧 `Pump`），属性键在条上方展开其最终属性网格。构筑入口只有一个，由 `ULDWorkshop` 的收起态在操作条右侧绘制。与操作条、比分条、目标卡、小地图相交的头顶标签不绘制。 |
| **比分条、事件流与头顶标签** | 左上比分条由 `LDHUD::ScoreBug`/`SeatPlates` 生成，只用公开信息（名单、涂鸦持有者、热搜、事件流窗口内的公开击杀通知）。右上事件流由 `PaintFeed` 绘制，击杀行来自 `FEventLine` 的席位与名字（通知不含技能，中间格只表示击杀来源类别）。头顶标签为席位号牌 + 分段条：本人橙色、他人白色、当前目标或悬停者转红并显示名字。 |
| **动态技能说明弹层（`EvaluateLiveSkillTooltip`）** | 悬停 `Q/W/E` 实时结合本人当前等级 $G(L)$、额外 AD、AP、双系数与自适应主属性（`BonusAD*1.4 >= AP` 自动切换物理/魔法配色与数值）算出下一击真实伤害/护盾，并同步展示当前有效元素爆劲效果（含无元素技能镶嵌 `SkillElement` 勋章解锁的对应元素爆劲）；按住 **`Alt`** 展开完整公式拆解（基础成长 + 系数加成 + 镶嵌构筑牌乘区）。 |
| **构筑气场环（`ULDWorkshop`、`LDBuildRingModel/Paint/Decal`）** | 围绕本人角色的直播径向仪表，野外与休息共用。层次：脚下雷达（`LDBuildRingDecal`，世界空间平面，会被角色遮挡；角标点击与滚轮经 `ULDStatBar::Allocate/Refund` 加点）、墨色环带上的五个宿主及固定牌槽、左侧五维与二级属性表（行内容取自 `LDAttributeProjection` 的每点影响，悬停雷达角时对应行提亮、增量见说明卡）、休息右侧商店（`FLDShopView`）、操作条上方的构筑条与野外三选一行。压暗是以角色为中心的径向层，位于操作条与小地图之下。联动只来自卡牌与主动技能目录的共享词条 `VocabMask`，画成按环上角度串起相关项的一笔白线，说明卡在环顶，会遮挡悬停目标或提示时移到右上。装卸、镶嵌、选元素与替换走 `ULDComponentStorage`，奖励、重抽、跳过、终局改装、图纸、洗点与商店走 `ULDChoicesPanel`；这两个类只负责请求与回执，界面全部由环绘制。开环不捕获输入，未落在环部件上的点击照常进入世界；`1/2/3` 与 `Alt+1/2/3` 由 HUD 交给 `Workshop->HandleKey`。开环、发牌、悬停、装上、翻页与收起动效都不超过 `200 ms`。休息期“重置训练 / 准备完毕”在右下，字号放大放不下时抬到构筑条上方。布局由纯函数按视口与字号计算，`LayoutProblems()` 报告溢出与重叠。休息规划窗无阻塞菜单时自动打开一次，野外死亡由玩家按需打开。 |
| **战术计分板（长按 `Tab`）与目标卡** | 长按 `Tab` 每名参赛者一行（最多 8 行）：席位号牌（席位色 + 号码）、热搜皇冠、最后目击等级、主旗杆实时涂鸦守护进度条、K/D/A、已目击暴露的 `Q/W/E` 图标及终局揭示的改装。顶部目标卡在目标刚失视野时保留 `2.5 s`、`55%` 透明度的最后快照，超期清空。 |
| **战术小地图（`ULDMinimap`）** | 底图采样 `SCS_BaseColor` 真实地形并压暗（直播风格黑底），叠加**高墙白色实线轮廓（`#F2F2F2`）、矮墙半透色块与灰色线（`#9AA0A8`，挡人但透视野/远程弹道）及实时场景坡道上坡箭头（`#E2C986`）**，其上覆盖实时迷雾层、公开事件图标（旗杆、躺平角、空投、外卖柜、小迷你旗、擂台、标记营地、热搜模糊圆）与当前可见单位（绝不显示迷雾内其他玩家）；白色镜头视锥框与 `Z = OwnFootZ` 平面求交，消除高度偏置。 |
| **死亡承伤回放（`DamageRecap`）** | 死亡等待期间右侧展示致死前窗口内按攻击者与来源聚合的物理/魔法/真实承伤占比及元素反应规则链。 |

### 构筑环与其下界面的点击

`ULDWorkshop` 铺满整个 HUD 并叠在操作条之上。它只在有东西可按时接收指针（指针下有环的部件，或手里拿着一张牌等一次落点）；其余位置对指针透明，点击落到它下面的界面：`Q/W/E` 槽与槽角的升阶「+」、属性行、右下角按钮。升阶页由「+」或 `Ctrl + Q/W/E` 打开，页内两张卡可点击或按 `1`/`2` 选择。

### 结算页的报告切换

结算页右侧报告板（`ULDHUD::RefreshResultInspectedBuild`）的各报告叠在一个从不换父级的 `UOverlay` 里。新报告先以透明度 0 在旧报告之上排版，等画出的尺寸连续两帧不变（最多 12 帧）再替换旧报告（`ULDHUD::SettleResultReport`）。原因是 `LDResultBuild::MakeReport` 的整体缩放要等一帧拿到换行后的高度：直接替换时，新报告先空一帧，再按错误比例画一帧。检查入口：覆盖探针的 `Match_ResultsSettle` 页，日志 `LD_UI_COVERAGE_RESULT_SWITCH` 记录切换后每帧的尺寸。

### 局内提示

本人生命低于 30% 时，操作条血条、本人头顶血条与屏幕四边暗角都用低血色（直播红；色盲模式用金色），暗角缓慢呼吸，不压到画面中部。收起态的待选三选一用可操作橙色描边呼吸，直到选走。休息期进入押注步时，构筑环自动收起，把画面中部让给 PK 押注板。

### 界面的每帧开销

界面只在所示内容变化时重算，绘制层每帧只做投影与提交：

- 读模型按 `LDHUD::FReadModelGate` 采样；其下的 `ULDWorkshop::Rebuild` 与 `ULDStatBar::Present` 各自比较输入（`LDHUD::SameProgression`、`SameAttributes` 与其余入参），输入相同就保留上一次的环模型与属性格。
- 构筑环收起时的布局（操作条旁的入口与待选奖励）只跟环模型、控件尺寸与缩放、操作条位置和键位名变化重排；展开时每帧排版，两侧面板贴环缩放的结果按其读取的数值记忆（`ULDWorkshop::Refit`）。
- 脚下雷达（`ALDBuildRadarDecal`）的渲染目标只在 `FLDBuildRadarState` 变化时重绘，跟随角色移动只改平面变换。
- `LDRingPaint::FPainter` 把连续的无贴图图元合成一个网格提交（三角形保持绘制顺序），遇到图片或文字先提交；`LDRingPaint::Wrap` 记住最近的换行结果，`LDUIIcons::Find` 记住已找到的贴图。
- 世界空间到屏幕的投影在一次绘制里用 `FLDWorldProjector` 读一次视图矩阵（头顶标签、瞄准预览、伤害数字），像素与 `APlayerController::ProjectWorldLocationToScreen` 相同。

检查入口：`Scripts\RunPerfBudget.ps1 -Match -Trace`，再按[开发指南](../DEVELOPMENT.md)的无界面导出命令导出 GameThread 计时表；界面相关计时点为 `LDHUDRebuildReadModel`、`LDHUD_*Present`、`LDWorkshop_*`、`LDWorldLabels_*`、`LDMinimap_*`。

## 构筑蓝图文件与查看

结算页与大厅入口可打开 `ULDBlueprintPanel` 只读查看本局或历史 `.ldbuild.json`（`schema_version=5`，单文件上限 `2 MiB`），支持“导出文件”至 `Saved/Blueprints/Exports/`（免自动清理）、“打开所在文件夹”及从 `Blueprints / Exports / Imports` 严格校验导入查看；不含任何上传或局内一键套用属性功能。

### 设置页

局内与主菜单共用的 `ULDSettingsPanel` 含 **6 个分类页签**：**画面**（窗口化/无边框/全屏、分辨率、帧率上限、画质档位，草稿点击应用才写盘校验，`ForceClose` 丢弃未应用草稿）、**音频**（总音量与分轨音量）、**操作**（全键位点击重绑、冲突自动互换、快速施法开关、恢复默认）、**镜头**（缩放、边缘推屏速度与开关）、**界面**（HUD 缩放、色盲模式）、**特效**（战斗特效强度档位）。

## 音效与音量

正式音效登记于 `SourceData/Audio/audio_cues.json`（CC0 来源 + SHA-256），经 `Scripts/ImportDemoAudio.ps1 -Mode Import` 生成 `SW_*` 与 `DA_DemoAudio`，全部归入 `SC_DemoMaster` 子混音分类（覆盖普攻/施放/命中/重击/死亡、原生精准闪避 `SelfPerfectDodge`、招架格挡 `SelfParry`、拾取拖鞋/回复球 `SelfPickup`、同色元素石融合 `UIMedalFuse` 及全局提示音）；敌方施放与受击音效仅在当前授权观察帧可见时衰减播放，绝不通过声音泄露迷雾内隐藏动作。

## 表现层覆盖清单

全流程界面的覆盖与状态（启动 → 主菜单 → 开房/加入 → Lobby → 对局 → 结算 → 复盘）。状态：NOT STARTED / IN PROGRESS / IMPLEMENTED / LOCALLY VERIFIED / USER APPROVED / BLOCKED。证据为运行时截图，由 `Scripts/CaptureUICoverage.ps1`（探针 `LDUICoverageProbe`）在 1920×1080 与 1280×720、100% 与 125% 字号下拍摄，文件名 `<页面>_<1080|720>_<100|125>.png`，整理后存于主仓 `Artifacts/Audits/PresentationOverhaul/production/ui/`。

| 项 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- |
| 标题页 / 主菜单 | LOCALLY VERIFIED | `ULDUIRoot::BuildFrontend`、`StyleRow` | `production/ui/m2/A1-frontend.jpg`、`Front_Menu*` | 菜单下的“本地设置存储无效”只出现在 `-unattended` 拍摄进程（不选设置文件），玩家看不到 |
| 怎么玩（六张卡） | LOCALLY VERIFIED | `ULDUIRoot` HowTo 页 | `Front_HowTo*` |  |
| 连锁图鉴 | LOCALLY VERIFIED | `ULDUIRoot` Codex 页（菜单页，可滚动） | `Front_Codex*` | 只拍到空图鉴状态 |
| 开始页：我开房 / 我加入 | LOCALLY VERIFIED | `ULDUIRoot` StartPage | `Front_Start*` | 开房/加入进行中与失败提示未拍；输入框字号按建页时的字号，运行中改字号不重排 |
| 退出确认 | LOCALLY VERIFIED | `ULDUIRoot::BuildConfirm` | `Front_Exit*` |  |
| 设置：画面 / 音频 / 操作 / 镜头 / 界面 / 特效 | LOCALLY VERIFIED | `ULDControlsPanel`（菜单页，可滚动）；音频页四条总线（总音量 / 音乐 / 音效 / 界面）走 `ULDGameInstance::SetAppliedBusVolume`，松手试听 `PlayBusPreview` | `*_Settings_<页签>*`、`production/ui/m3/` | 画面页在离屏拍摄下只显示“需要可交互窗口”；试听声音未经人耳确认 |
| Lobby（角色集结卡：8 张席位卡、房主 / 客机 / 满员 / 无 AI、准备与开始） | LOCALLY VERIFIED | `ULDUIRoot::BuildWorld`、`RebuildRoster`、`RefreshLobby`；席位立绘 `LDUIBackgrounds::SeatAsset` ← `lobby_seat_1..8`：同一球员 8 个定格（篮球动作与个性动作各半，个性动作隐藏篮球；`ULDHumanoidVisualComponent::HoldPose`）与 8 个机位，`Scripts/MakeLobbySeats.py` 抠图并涂席位球衣色 | `production/ui/lobby/impl/`、`production/ui/m8/`、`Lobby_*`（客机视角由真实第二进程拍摄） | — |
| 构筑蓝图查看 | LOCALLY VERIFIED | `ULDBlueprintPanel` | `Lobby_Blueprint*` | 只拍到无记录状态 |
| 离开房间确认 | LOCALLY VERIFIED | `ULDUIRoot::BuildConfirm` | `Match_MenuLeave*` |  |
| 战斗 HUD（比分条、目标卡、事件流、底部操作条、小地图） | USER APPROVED | `ULDHUD`、`LDBroadcastPaint` | `hud/iter2-*`、`Match_Field*` |  |
| 构筑气场环（雷达、宿主、牌、属性表、商店、三选一） | USER APPROVED | `ULDWorkshop`、`LDBuildRing*` | `build-v2/compare-*-r3.png`、`Match_RestOpening*` |  |
| 休息期 HUD 与休息按钮 | LOCALLY VERIFIED | `ULDHUD` RestActions | `Match_RestRingClosed*` |  |
| 长按 Tab 战术计分板 | LOCALLY VERIFIED | `ULDHUD::PaintScoreboard` | `production/ui/m1/A3-match.jpg`、`Match_Scoreboard*` |  |
| 局内菜单 | LOCALLY VERIFIED | `ULDUIRoot::BuildWorld` 菜单条 | `Match_Menu*` |  |
| 技能悬停说明与 Alt 公式 | LOCALLY VERIFIED | `ULDHUD` ActionDetail（探针用 `ULDHUD::ProbeHover` 代替系统光标，仅 DEV 构建） | `Match_SkillHover*`、`Match_SkillHoverAlt*`、`production/ui/m11/` | — |
| 死亡横幅、承伤回放与复活选点 | LOCALLY VERIFIED | `ULDHUD` DeathBanner、SpawnChoice、`LDRecapChain` | `Match_Death*`、`Match_Respawned*` | 以 DEV 无主地面状态造成真实死亡拍摄；PvP 击杀来源未拍 |
| PK 报名与 PK 对阵板（押注、准备、进行、结算与得点） | LOCALLY VERIFIED | `ULDHUD::RefreshPK`、`LDHUD::PKPanel`（`FPKMemberView`、赔率、押注数、倒计时秒数）：红色 PK 标与场次、倒计时大数字、每方一行（席位牌、单人方的名字与等级、押注点、赔率，押注期整行即押注按钮），出局者变暗加红杠，胜方整行金色，其下每人得点；气场环打开时是同一语言的一行押注条 | `Match_R1*`、`production/ui/m6/`、`production/ui/m9/`（真实对局第一次中场休息，AI 自行报名，房主观战） | 观众投掷（`Match_R1PKThrow*`）与房主亲自参赛（`CaptureUICoverage.ps1 -Entrant`，`production/ui/m12/`）已拍；组队对阵、平局与无效 PK 未出现过（赛制随机，至今每次都是混战） |
| 野外点位：天降外卖、中途目标（喷漆小旗、擂台）、快递柜 | LOCALLY VERIFIED | `ULDWorldLabels::NativePaint`（点位状态牌：一字图标块、名称、剩余秒数；小旗喷漆进度按喷漆者席位色、争夺时直播红；快递柜有人在取时滚动条） | `Match_Airdrop*`、`Match_Objective*`、`Match_FlagSpray*`、`Match_Locker*`、`Match_Arena*`、`Match_FlagContested*`、`Guest_FlagContested*`、`production/ui/m9/`、`production/ui/m10/` | — |
| 二手贩子与画饼老板 | LOCALLY VERIFIED | `LDWorldLabels`（头顶名字与血条）；击败后的三选一走构筑气场环，奖励提示由 `LDHUD::FieldRewardEventName` 命名 | `Match_Peddler*`（二手贩子）、`Match_PeddlerReward*`（击败后）、`Match_Arena*`（画饼老板入镜）、`production/ui/m13/` | 击败后的奖励在截图里是排在外卖柜三选一后面的一份（“+1”）和右下的到账提示，没有拍到它自己作为当前三选一的标题；另有一局二手贩子整局没有放置出来（营地两个落点都被占，120 次 `LD_CAMP_PLACEMENT_DIAGNOSTIC peddler`），属玩法侧 |
| 升级时刻 | LOCALLY VERIFIED | `ULDHUD::PaintMoments`、`LDWorldFeedback` | `hud/seq-levelup/` |  |
| 击杀时刻（本人击杀 K.O.） | LOCALLY VERIFIED | `ULDHUD::PaintMoments`：比分条下一块小牌（K.O.、本人席位 ▸ 被击杀者席位色块与号码、下方红条），不遮挡战斗画面 | `Match_Kill*`、`production/ui/m4/`（探针用 DEV“设置属性”加攻击力打出的真实击杀，画面定格于固定时刻） | — |
| 阶段过场（野外 ↔ 休息、最终野外） | LOCALLY VERIFIED | `ULDHUD::RefreshReadModel`（阶段跟踪）、`PaintMoments`：切换前 5 秒在比分条下倒数并标出下一阶段，最后 350 ms 两块斜板全屏擦合，切换后继续向右擦开，留下横贯屏幕中部的包装条（LIVE、阶段名、8 段进度），再同向擦出 | `Match_StageCount*`、`Match_StageCard*`、`production/ui/m4/` | 只拍到开局休息 → 野外 1/3；野外 → 休息与最终野外未拍 |
| 技能升阶页（二级分化与三级预览） | LOCALLY VERIFIED | `ULDHUD::RefreshRankUp` | `Match_RankUp*` | 打开菜单、设置或被遮挡时自动关闭 |
| 开局横幅与世界反馈文字 | LOCALLY VERIFIED | `LDWorldFeedback` | `Match_Kill*`（击杀后的升级横幅）、`Match_Field*` | 命中确认字的动效属手感，未改 |
| 头顶标签与构筑环的重叠 | LOCALLY VERIFIED | `LDWorldLabels` | `Match_R1Build*`、`Match_R1BettingRing*`（开环时环后的角色不画头顶标签） | — |
| 终局（终局改装、旗杆） | LOCALLY VERIFIED | `ULDHUD`、构筑环 | `Match_Final*` | 终局改装三选一未拍 |
| 结算页 | LOCALLY VERIFIED | `ULDHUD` Result 区块、`LDResultBuild` | `production/ui/m2/A4-results.jpg`、`Match_Results*` |  |
| 赛后复盘（切换席位查看构筑） | LOCALLY VERIFIED | `ULDHUD::SelectResultContender`、`RefreshResultInspectedBuild` | `Match_ResultsReplay*` | “成长与卡牌”“全员战斗数据”页签未拍 |
| 网络卡顿提示与诊断 | LOCALLY VERIFIED | `ULDHUD` NetSpike、Diagnostics | `Guest_NetSpike*`、`production/ui/m10/`（真实客机，引擎 `NetEmulation.PktLag` / `PktLoss`，只对客机、拍完关闭） | 诊断面板未单独拍 |
| 按钮交互态与页面转场 | LOCALLY VERIFIED | `LDUIPaint::Button`、`ULDCommandButton`、`ULDUIRoot::StyleRow`：按下时下沉 2 单位并变暗（主按钮深橙、白块变灰、普通块与菜单行为墨底橙边） | `Front_MenuHover*`（悬停）、`Lobby_HostWait*`（禁用）、`Lobby_GuestReady*`（已选可撤销的白色块）、`Match_MenuPressed*`（菜单行按下）、`Match_StageCard*`（阶段过场）、`production/ui/m11/` | 普通斜块的按下态已拍（`Lobby_ButtonPressed*`），主按钮与白块的按下态未单独拍；页面之间除阶段过场与结算标题滑入外是直接切换 |

## 检查入口

- 纯规则：`Lockdown.Rules.Vision.*`、`Lockdown.Rules.Minimap.*`、`Lockdown.Rules.WorldLabels.*`、`Lockdown.Rules.HUD*`、`Lockdown.Rules.ViewPrivacySchema`、`Scripts/TestDemoArtImport.py`。
- 拥有连接场景：`Scripts/RunDemoVisionCheck.ps1`、`Scripts/RunDemoCombatCheck.ps1`。

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

## 构筑蓝图文件与查看

结算页与大厅入口可打开 `ULDBlueprintPanel` 只读查看本局或历史 `.ldbuild.json`（`schema_version=5`，单文件上限 `2 MiB`），支持“导出文件”至 `Saved/Blueprints/Exports/`（免自动清理）、“打开所在文件夹”及从 `Blueprints / Exports / Imports` 严格校验导入查看；不含任何上传或局内一键套用属性功能。

### 设置页

局内与主菜单共用的 `ULDSettingsPanel` 含 **6 个分类页签**：**画面**（窗口化/无边框/全屏、分辨率、帧率上限、画质档位，草稿点击应用才写盘校验，`ForceClose` 丢弃未应用草稿）、**音频**（总音量与分轨音量）、**操作**（全键位点击重绑、冲突自动互换、快速施法开关、恢复默认）、**镜头**（缩放、边缘推屏速度与开关）、**界面**（HUD 缩放、色盲模式）、**特效**（战斗特效强度档位）。

## 音效与音量

正式音效登记于 `SourceData/Audio/audio_cues.json`（CC0 来源 + SHA-256），经 `Scripts/ImportDemoAudio.ps1 -Mode Import` 生成 `SW_*` 与 `DA_DemoAudio`，全部归入 `SC_DemoMaster` 子混音分类（覆盖普攻/施放/命中/重击/死亡、原生精准闪避 `SelfPerfectDodge`、招架格挡 `SelfParry`、拾取拖鞋/回复球 `SelfPickup`、同色元素石融合 `UIMedalFuse` 及全局提示音）；敌方施放与受击音效仅在当前授权观察帧可见时衰减播放，绝不通过声音泄露迷雾内隐藏动作。

## 检查入口

- 纯规则：`Lockdown.Rules.Vision.*`、`Lockdown.Rules.Minimap.*`、`Lockdown.Rules.WorldLabels.*`、`Lockdown.Rules.HUD*`、`Lockdown.Rules.ViewPrivacySchema`、`Scripts/TestDemoArtImport.py`。
- 拥有连接场景：`Scripts/RunDemoVisionCheck.ps1`、`Scripts/RunDemoCombatCheck.ps1`。

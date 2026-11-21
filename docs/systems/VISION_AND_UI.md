# 视野与界面手册

本页记录服务器视野授权、迷雾距离场渲染、头顶世界标签、小地图、局内无滚动 HUD、构筑工坊、蓝图查看、设置面板与音频管线（`LDVisionDirector`、`LDWorldViewRelay`、`LDFogDistanceField`、`LDWorldLabels`、`LDMinimap`、`LDUIRoot`、`LDHUD*`、`LDWorkshop`、`LDChoicesPanel`、`LDAudio*`）。

## 服务器视野授权与迷雾表现

- **权威视线与复制隔离（`LDVisionDirector` / `LDWorldViewRelay`）**：
  - 参战身体对隐藏观察者关闭引擎自动网络相关性，客户端绝不持有视野外敌方参赛者或野怪的实时坐标/血量/状态。
  - 服务器对每位观察者在 `VISION.RadiusCm`（`1400 cm`，停电日 `1050 cm`）内按静态高墙遮挡计算可见集，附带 `VISION.SightGraceSec`（`0.3 s`）擦墙宽限；仅向本人连接下发授权帧 `FLDWorldView`（含可见单位快照、可见弹道/地面状态/预警及 `256 × 128` 视野覆盖位图 `CurrentBits`）。Relay 在世界销毁/关停时由 `IsShuttingDown()` 拦截迟到 RPC。
- **迷雾后处理（`LDFogDistanceField` / `MI_FogMist`）**：客户端将 `256 × 128` 覆盖位图转换为世界厘米有符号距离场驱动 `MI_FogMist` 平滑边缘与世界坐标噪声，纯属视觉表现，不反向参与任何逻辑选中或可见性判定。
- **头顶血条、遮挡剪影与受击反馈（`LDWorldLabels`）**：
  - **LoL 式分段缓冲血条**：每 `250 HP` 一小格、每 `1000 HP` 加粗黑刻线（直观体现肉度），护盾层以银白段拼接在当前血量右侧，扣血带白色受击滞后缓冲层（`AdvanceBarTrail`，精确按超出保持期的时间衰减，帧率无关），并配状态胶囊徽章（如 `破防×3`、`灼烧×5`、硬控外圈倒计时）。
  - **墙后半透剪影与悬停描边**：对当前 `FLDWorldView` 已授权可见但胸部被静态场景遮挡的本方/敌方参赛者（`70 ms` 节流射线，穿透 Pawn 与可移动组件追踪静态墙），绘制 `25%` 不透明度胶囊剪影（本方 `#7DD3FC`、敌方 `#F87171`）；鼠标悬停可见敌方时叠加 `1.5 px` `#FF4D4F` 锁定支架。

## UI 只读模型

所有局内界面严格遵守**零滚动视图（No ScrollView）**原则（仅主菜单、Lobby、局内菜单、设置页、构筑蓝图查看与 DEV 测试面板属于非局内界面可滚动）；全部文本走 `SourceData/UI/ui_fonts.json` 导入的思源黑体（`FF_Regular/Bold/Heavy`），样式令牌单点收口于 `LDHUDStyle.h`。

| 区域 / 面板 | 真实实现行为与数据来源 |
| --- | --- |
| **底栏动作槽与状态条（`ULDHUD`）** | 展示左键状态、右键普攻连招段位、`Q/W/E` 主动槽（含等级/分支点、冷却秒数、缺劲提示及硬控/禁用角标 `定身/沉默/眩晕/嘲讽/击退/缴械/禁`）与 `D` 冲刺槽；HP 条与劲条（满 `60` 亮爆劲刻度）；左侧紧凑五维栏支持随时点击 `+` 加点、休息期点击/滚轮退点，悬停通过权威 `LDAttributeProjection` 实时预览下一级属性增益。 |
| **动态技能说明弹层（`EvaluateLiveSkillTooltip`）** | 悬停 `Q/W/E` 实时结合本人当前等级 $G(L)$、额外 AD、AP、双系数与自适应主属性（`BonusAD*1.4 >= AP` 自动切换物理/魔法配色与数值）算出下一击真实伤害/护盾；按住 **`Alt`** 展开完整公式拆解（基础成长 + 系数加成 + 镶嵌构筑牌乘区）。 |
| **三类奖励差异化选卡（`ULDChoicesPanel`）** | 按奖励类别采用三套独立视觉母版——**角色技能候选**（金色科技斜角框 + 目标模式徽章）、**构筑牌候选**（竖版扑克塔罗框 + 普通/稀有/传说色带 + 宿主槽位角标）、**元素石/勋章获得**（八角晶石发光徽章），一眼区分获取物种类；支持 `1/2/3` 快捷键选择与 `Tab` 折叠。 |
| **全屏构筑工坊（`ULDWorkshop`）** | **阶段自动开关规则**：进入休息室规划窗（R0–R3）且无阻塞菜单时**自动打开一次**；**野外死亡等待期间不自动弹窗遮挡视野**，改为底栏亮起 `[B] 死亡整备` 胶囊提示由玩家按需呼出。工坊采用 `PresentConverged` 同帧双遍预排版（零首帧跳变），左侧常驻角色实时总属性面板（`CharacterPanel`），右侧展示 Q/W/E/D/本体插槽、元素槽、无滚动分页牌库与休息商店/洗点/遗忘按住确认（`HoldButton`）。 |
| **战术计分板（长按 `Tab`）与目标卡** | 长按 `Tab` 展示 6 人战术行：座位色头像、热搜皇冠、最后目击等级、主旗杆实时涂鸦守护进度条、K/D/A、已目击暴露的 `Q/W/E` 图标及终局揭示的改装。顶部目标卡在目标刚失视野时保留 `2.5 s`、`55%` 透明度的最后快照，超期清空。 |
| **战术小地图（`ULDMinimap`）** | 底图采样 `SCS_BaseColor` 真实亮色地形并叠加**高墙实线轮廓（`#22252A`）、矮墙半透色块与虚线（`#6FA3B8`，挡人但透视野/远程弹道）及实时场景坡道上坡箭头（`#E2C986`）**，其上覆盖实时迷雾层、公开事件图标（旗杆、躺平角、空投、外卖柜、小迷你旗、擂台、标记营地、热搜模糊圆）与当前可见单位（绝不显示迷雾内其他玩家）；白色镜头视锥框与 `Z = OwnFootZ` 平面求交，消除高度偏置。 |
| **死亡承伤回放（`DamageRecap`）** | 死亡等待期间右侧展示致死前窗口内按攻击者与来源聚合的物理/魔法/真实承伤占比及元素反应规则链。 |

## 构筑蓝图文件与查看

结算页与大厅入口可打开 `ULDBlueprintPanel` 只读查看本局或历史 `.ldbuild.json`（`schema_version=5`，单文件上限 `2 MiB`），支持“导出文件”至 `Saved/Blueprints/Exports/`（免自动清理）、“打开所在文件夹”及从 `Blueprints / Exports / Imports` 严格校验导入查看；不含任何上传或局内一键套用属性功能。

### 设置页

局内与主菜单共用的 `ULDSettingsPanel` 含 **6 个分类页签**：**画面**（窗口化/无边框/全屏、分辨率、帧率上限、画质档位，草稿点击应用才写盘校验，`ForceClose` 丢弃未应用草稿）、**音频**（总音量与分轨音量）、**操作**（全键位点击重绑、冲突自动互换、快速施法开关、恢复默认）、**镜头**（缩放、边缘推屏速度与开关）、**界面**（HUD 缩放、色盲模式）、**特效**（战斗特效强度档位）。

## 音效与音量

正式音效登记于 `SourceData/Audio/audio_cues.json`（CC0 来源 + SHA-256），经 `Scripts/ImportDemoAudio.ps1 -Mode Import` 生成 `SW_*` 与 `DA_DemoAudio`，全部归入 `SC_DemoMaster` 子混音分类；敌方音效仅在当前授权观察帧可见时播放，绝不通过声音泄露迷雾内隐藏动作。

## 检查入口

- 纯规则：`Lockdown.Rules.Vision.*`、`Lockdown.Rules.Minimap.*`、`Lockdown.Rules.WorldLabels.*`、`Lockdown.Rules.HUD*`、`Lockdown.Rules.ViewPrivacySchema`、`Scripts/TestDemoArtImport.py`。
- 拥有连接场景：`Scripts/RunDemoVisionCheck.ps1`、`Scripts/RunDemoCombatCheck.ps1`。

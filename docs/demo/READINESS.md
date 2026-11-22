# Demo 实施就绪评估

本页记录当前代码基线的验证覆盖与未验收边界。产品规则以 [App Spec](../APP_SPEC.md) 为准，组件接口以[系统手册](../DEVELOPMENT.md#按任务读取系统手册)和源码为准，待办与排期见 [Issue](https://github.com/rlsn/Lockdown-alpha/issues) 与 [Project 看板](https://github.com/users/sinmentis/projects/4)。

## 结论

**v0.14.2 Demo 主流程已完整接通，里程碑多人打包版实机总验收尚未完成。**
运行流程：前端建房/加入 → Lobby（房主设 0–7 名 AI，房主免准备）→ 开局休息 R0 → 野外 1 → 休息 R1（含 PK）→ 野外 2 → 休息 R2（含 PK）→ 野外 3 → 休息 R3（选终局改装）→ 最终野外（封街争旗）→ 唯一结果 → 房主带在线房间回 Lobby 开新局。

- 当前运行输入（`SourceData\Demo\content.json` 与 `L_Demo`）为获批试玩输入，未经平衡测试。
- 自动化门禁覆盖 `356` 项 Python 测试与 `536` 项原生 `Lockdown.Rules` 测试；联机场景均在同机回环连接上验证。

## 完成标准与验证矩阵

“规则”指 `RunTests.ps1` 的 `REQUIRED_TESTS`（CI `native` 全量执行）；“场景”指 `RunDemo*Check.ps1` 拥有连接场景（详见[验证手册](../systems/VERIFICATION.md#最小验证选择)）。

| 完成标准 | 已核验覆盖（源码 + 自动化） | 未运行或现存限制 |
| --- | --- | --- |
| 入口与启动 | `LDDemoEntry`、`ULDUIRoot`；规则 `DemoEntry*`、`DemoLobby*`；场景 Entry / WorldRoom / SessionFault | 前端鼠标实机手点完整交互待人工验收 |
| 八席联机与断线 | `LDGameModeSession`（8 席容量、房主免准备、调 AI 数保留真人准备、房主离开 `PreLoadMapWithContext` 通知清退、意外断线 `ParkDisconnectedMember` 同进程重连）；场景 Opponent / Capture / SessionFault | 跨机器局域网与公网直连未跑；意外断线保留身体供同进程重连与旧“断线即清身体”设计存在偏移；房主进程被强杀时客户端约 60s 超时 |
| 地图、寻路与坡道 | Bootstrap Validate、`DemoWorld.*`、`StaticScene.*`、`DemoControlFeel.ElevationAimAndClip`（50° 可走坡度、35cm 台阶、坡口 3D 高差 28cm/35cm 防切角）；场景 Core / Combat | 动态改图与多层重叠桥面未支持 |
| 营地生态与续航 | 34 处四档营地（每档 3 种抽样、随野外时间 T 升阶、拴绳回位与无路原地重置、清营掉落 `90cm / 12s` 回复球）；规则 `DemoCamp.*`、`CampDecision`；场景 Camp | 12 种野怪体感平衡与远古专属模型待试玩打磨 |
| 成长、加点与换招 | 1–16 级、开局与 `[2, 5, 9]` 级主动三选一、`[3, 6, 9, 12, 15]` 技能点三级分支、五维 18 点关键石、休息室洗点/商店与野外躺平角脱战换招（退还技能点并迁移兼容技能牌）；规则 `DemoGrowth*`、`Growth.*`、`Combat.AdaptiveAndKeystones`；场景 Growth | 连升多级与满级溢出给点仅在规则层验证 |
| 战斗、元素与构筑牌 | 劲/三段连击/爆劲（60 门槛）/0.12s 原生精准闪避、24 主动（含自适应与双系数）、89 构筑牌（`100/22/6` 权重）+ 5 枚三阶融合元素勋章、五元素十反应 + Y05 反应锁单次豁免、多源独立 `ShieldLayers` 护盾栈、全形态 2 步 `450ms` 预输入、默认 `0.18s` 快速施法；规则 `Combat.*`、`DemoCombat*`、`DemoBuildPrimitives.*`、`DemoControlFeel.*`；场景 Combat | 144 个技能分支子项与多牌极端连锁组合未逐一跑拥有连接场景 |
| 喷漆、守护与终局 | 主旗杆与私人旗杆同刻归约、终局 8 项改装、封街缩圈、有效守点完成率 → 终局击杀 → 终局伤害超时破僵；规则 `DemoCapture.*`（含 `ShowdownAntiStalemate`）、`DemoFinalMod.*`、`DemoShowdown.*`；场景 Capture / Opponent | 铁笼物理阻挡仅经完整场景间接覆盖 |
| 视野与小地图 | 服务器稀疏 `FLDWorldView` 白名单、客户端距离场迷雾、单次静态底图 + 矮墙虚线/实时坡道箭头语义层 + 高地 `OwnFootZ` 视口框、`VISION.SightGraceSec`；规则 `DemoVision.*`、`ViewPrivacySchema`、`Minimap.*`；场景 Vision | 坡顶水平平台边缘整数层阶视野规则待独立工单收口 |
| 界面、设置与复盘 | 96 DPI 字阶、LoL 百血/千血分格缓冲头顶条、墙后 25% 剪影、三态奖励分流与交战自动折叠、技能实时总伤与 `Alt` 公式展开、工坊第 0 帧收敛、全员战术 Tab 板、赛后全员构筑切换与伤害占比条、Schema 25 本地设置（含窗口模式/分辨率/帧率上限/色弱模式）；规则 `HUD*`、`WorldLabels.*`、`LocalSettings*`、`LocalDisplay*`；场景 Frontend | 新版工坊、三态牌面与音效混音尚待打包版全屏人工审阅 |
| 对手 AI 与评估 | `ALDOpponentController` + `LDOpponentPolicy`（打野、PK、选牌、加点、双关键石聚焦、目标争夺、争旗回防）与 `BuildEvaluation` Harness；规则 `DemoOpponentPolicy.*`、`BuildEvaluation*`；场景 Opponent | 满员打包版全流程（含 R1/R2 PK）待里程碑复跑 |

# 验证手册

日常该跑什么见[开发指南](../DEVELOPMENT.md#验证)（`Scripts\Verify.ps1`）。本页是参考资料：每个检查与场景覆盖什么、
里程碑清单、规则测试集合的维护方式，以及多人共用一台机器时的限制。

## 最小验证选择

先完成一批连贯开发，再集中验证收尾。以下是按改动选择检查的索引，不是每次任务都要执行的清单：
只覆盖实际改动及其直接依赖，优先增量构建、规则前缀和已有场景定向模式。
复用 `Binaries`、`Intermediate` 与构建缓存，不做例行清理重建；未受后续改动影响的通过结果不重复验证。
整局、全量导入、`-FullUnity` 与全套场景不是日常默认步骤。

检查失败先保留本次 `Artifacts`，查看日志与失败断言，结果标记为未通过；不原样重复长测来判断偶发性。
修复相关原因后只重跑失败项与受影响检查；没有定向入口时报告覆盖缺口，
仅在明确要求专项验证时重跑长测。不因长场景附带的无关失败扩展任务，也不把未确认归因写成既有问题。

| 改动 | 起点 |
| --- | --- |
| 文字与导航 | 链接、代码入口与当前事实，`git diff --check` |
| SourceData / 编译器 | 对应 Sync `--check`、`Test*Import.py`，核对载荷与 contract |
| Demo 世界 / 资产 | `BootstrapAssets.ps1 -Mode Generate/Regenerate/RefreshDefinitions/RegenerateFrontend/Validate`（`RegenerateFrontend` 只重建空白的 `L_Frontend`；每次都拒绝前端地图里出现世界设置以外的任何 Actor）；`Lockdown.Rules.DemoWorld` 的精确有符号几何、typed 投影与资产往返；真实拥有连接和完整移动/观测另行验收 |
| Demo 世界 / 拥有连接 | `RunDemoWorldRoomCheck.ps1`；实际 Listen Host 加两个拥有客户端校验同一 Lobby/内容根/原生地面与导航，隔离 UserDir；没有 AI 或玩法身体，不代替完整对局 |
| Demo 核心 / 拥有身体 | `RunDemoCoreCheck.ps1`；DEV-only 三拥有身体、地面/Stop/重放与原生 CMC 执行夹具，调用同一核心提交路径且不改变普通 Start 门禁；开局身体按名册位落在公共休息室锚点并像普通开局一样进入 R0，地面与原生审计在 R0 返回后的第一条野外生命上进行。可选 `-EarlyRestReady` 在三端观察其他真人与共享假人、远端走近和离开两类服务 NPC 后提交准备，检查最后 10 秒并提前进入野外；开局点数不足购买时，远端通过跳过开局候选取得点数再购买。`-ReplayFieldReturn` 用真实放置路径重放一次捕获的坡缝返场脚点，要求原脚点能够精确准备并撤回该夹具请求；`-FinalEntryOnly` 只检查六个终局入口经权威出生路径准备成功，不模拟完整休息时间线；营地成员只计数不受检；不代替完整战斗、随机出生、完整视野或 CT06 |
| 喷漆守护 / 终局 / 新局 | `Lockdown.Rules.DemoCapture` 与 `Lockdown.Rules.DemoCore.CaptureCompletesInsideStep` 覆盖守护、打断、贡献及复活配置等规则；`RunDemoCaptureCheck.ps1` 默认用 Listen Host、两个拥有客户端和三个生产 AI，经正常大厅开局；`-AllHuman` 用 Listen Host 与五个拥有客户端，不添加 AI。两种模式都检查三段野外、最终休息时六人的普通主动选择与锁定、六个终局入口、目标结束、成长冻结、返回 Lobby 与第二局。runner 要求各角色报告且仅报告对应的稳定检查 id，缺失、多出或重复都失败。它不证明独立 FIN 效果、所有复活和截止细节、视觉质量或网络异常；通过结论只认当次 Artifacts |
| Demo 纯值战斗 / 时间许可 | `Lockdown.Rules.DemoCombatEvaluation`、`Lockdown.Rules.DemoCore`；伤害/时长使用当前 producer 的 `approved_base`，合成 `base` 保留供导入拒绝和变异测试；纯值成功不等于实际命中提交 |
| 营地纯决策 | `Lockdown.Rules.CampDecision` 使用同一 `approved_base` 内容夹具，覆盖授权读取完成、身份/世界/时间拒绝、只在已攻击本营的参赛者中稳定选敌、从当前脚点量的感知半径、只约束成员自身的活动范围和成员回位；不创建 Actor 或执行移动/攻击/重置，不代替 N03 真实接线 |
| 营地 / 拥有连接 | `Lockdown.Rules.DemoCamp` 覆盖名册、每生命死亡记账与清营计时、整组反击来源、回营重置键和观察事实；`RunDemoCampCheck.ps1` 在全部提供者接入、普通 Start 门禁打开后，以实际 Listen Host 加两个拥有客户端经正常大厅开局，覆盖批准名册的全部营地落点、不先手、整组反击、最后一击每生命一次收益、清空后按内容计时整组刷新、拴绳回营的公开提示与不发奖重置。当前布局的夹具选择 `l09`/`h05`：接近点为盆地内开阔、可达的近战位置，目标搜索范围内没有同种野怪的其他营地；逃离点距 `h05` 约 14.7m，超过 High 拴绳距离，并避开老板巡逻与旗杆。#C5 按批准内容核对低档营全部成员的奖励。R0 返回随机抽取野外出生点，客户端沿避开营地成员家点与老板巡逻的夹具路线走到营地接近点，整个场景须在野外 1 内完成。唯一渲染的 Client2 另存 `CampGuardianReturning.png`（回位的高档野怪、名字与回位标记）和 `CampLowPair.png`（刷新后的低档两只与名字）供表现审阅，不做像素比对，两档分两张记录。不代替 3+3、AI、SpawnBlocked/动态占位或网络异常 |
| 对手 AI / 拥有连接 | `Lockdown.Rules.DemoOpponentPolicy` 覆盖纯策略、帧投影、请求构造与回执结算；`RunDemoOpponentCheck.ps1` 在普通 Start 门禁打开后，以实际 Listen Host、两个拥有客户端和三个 AI 的满员房间经正常大厅开局，真人不参与。它核对每个 AI 独立绑定且不补位、自主移动，获胜 AI 升级并确认选择、死亡期间不发身体请求、死亡后按时复活并继续行动、每 30 秒的身体请求与受理时被拒订单有界（准入时被拒的订单逐个 AI 报告为 `orders_rejected`，不计入）、每个 AI 在开局休息 R0 的配装步骤内处理开局三选一且在公共休息室于报名窗口前除对自己槽位对应的共享假人试招外不移动、窗口内只走进公共报名圈并在锁定时全部报名（报名 PK 的 AI 在 PK 步骤以 PK 生命作战）、旗杆开放前取得经验（发育期至少一半时间死亡或回血恢复的除外）、争夺暂停下喷漆进度持有者留在喷漆区迎战且区内争夺者不离开去刷野，最终由 AI 以 `GuardSucceeded` 守护成功获胜且它是公开涂鸦主人，回到新 Lobby 后 AI 全部退役；可加 `-PackageRoot` 对打包版运行。每个角色在开局一分钟时按一次试玩标记，供 `SummarizePlaytestLog.py` 核对标记前后的事件（不是检查项）。本局没有 AI 死亡、复活或相应争夺时，相应项报告为未覆盖（NotExercised），单独列出，不计为通过；渲染的 Host 另存供人工审阅的截图（不是检查）：`HostMatch*.png`，以及 `HostChoices.png`（开局主动三选一）、`HostOpponentCard.png`（可见 AI 的 Q/W/E 卡）、`HostKillAssist.png`（带助攻者的击杀播报，本局出现时）、`HostSpray.png`/`HostGuard.png`（镜头移到旗杆的喷漆与守护 HUD、涂鸦板）、`HostResults.png`（含助攻列的结算）。“至少一个 AI 实际完成 D 冲刺”（报告逐个 AI 记录 `dashes` 与 `dashes_landed`）只作报告项：未出现时记为 NotExercised，不卡门槛，多局与打包版稳定后再定是否升为硬门槛；不代替隐藏状态非干扰、字段白名单、真人离开或网络异常 |
| Demo 属性 / 原生记录 | `Lockdown.Rules.DemoAttributeProjection` 覆盖批准属性、五维点数投影、构件条件项和资源比例；`Lockdown.Rules.DemoMotionProbe` 用隔离原生物理验证时间分数、回滚、接地转换和缺失记录拒绝；真实拥有身体夹具显式审计保留前缀，不代替最终提供者 Fence |
| 原生代码 / 反射 | Editor/Game 构建、Bootstrap Validate、相关原生规则 |
| 入口 / 房间 | `TestDemoEntry.ps1`、`Lockdown.Rules.DemoEntryContract/DemoInputByteIntegrity/DemoMatchIdentity/DemoSessionReceipts/DemoLobbyAuthority` |
| 休息与外层流程 | `Lockdown.Rules.DemoRest.Timeline` 覆盖纯时间线：批准时长、PK 报名窗口（截止时刻含在内）、锁定保持、报名不足 2 人跳过 PK、野外进度 T 在休息与 PK 中冻结而在野外推进、完整中场休息 126 s、PK 提前结束、最终野外、按死亡时 T 固定的复活等待与客户端 T 推导；`Lockdown.Rules.DemoRest.PhasePolicy` 覆盖隔离组、冻结身体、阶段边界清理与补满；`Lockdown.Rules.DemoRest.Services.*` 覆盖休息服务（见[成长事务](GROWTH_TRANSACTIONS.md)）；`Lockdown.Rules.DemoRest.EntryHold` 覆盖入场未就绪时 PKReady→PK 与返回→野外停在边界、就绪后按实际时刻进入，`Lockdown.Rules.DemoRest.SpawnIdentity` 覆盖撤回的出生请求以新序号重试、不触发 `RequestConflict`，`Lockdown.Rules.DemoPK.RingOutsideTime` 覆盖按真实圈外时长切分圈伤，`Lockdown.Rules.DemoVision.EffectAccess` 覆盖观众与同队共享视野中飞行物的可见权限。拥有连接场景探针已按休息流程改写（如 `RunDemoOpponentCheck.ps1` 核对 AI 在开局休息 R0 内选择开局主动、报名窗口前除训练试招外不移动、窗口内走进公共报名圈并在锁定时全部报名参赛，检查假人槽位并尝试触发 `Lives` 扩容，实际未扩容时记为未覆盖，另以夹具制造 PK 出生点全被占与返场部分阻挡，见[对手 AI](../demo/OPPONENT_AI.md) §8.2） |
| 原生旧入口拒绝 | `RunDemoEntryCheck.ps1`，要求真实 URL 被拒绝后回前端，累计 Pawn/旧模式/服务/子系统激活为零 |
| 前端设置实际渲染 | `RunDemoFrontendCheck.ps1`，新 `UserDir` 下的 windowed RHI、合成 UMG 点击与 Slate 按键事件：经命名页签逐个打开五个分类页（隐藏页不进焦点顺序），100/125% 各页图像、诊断开关与文本偏好保存/回读、设置页无窗口模式/分辨率、显示草稿撤销、在别页返回时确认卡回到画面页、再按返回即放弃、VSync 应用保存回读、行内改绑（占用键被拒仍在捕获、空闲键保存回读、Esc 只取消捕获、经 Slate 指针路由在捕获中点页签即结束捕获并切页）、按页恢复默认只影响本页、引擎锁定画质组时应用失败后改走再改回或撤销仍需应用、打开设置时在途的镜头距离延迟保存失败后底栏立即出现“立即保存”；不修改 OS 显示模式 |
| 输入 / 导航 / HUD | `Lockdown.Rules.ControlMappings/IssuedCommandLiveness`、`Lockdown.Rules.LocalSettings*/LocalDisplay*/LocalControlRegistryIsolation`、`Lockdown.Rules.HUD*`；拥有连接下的订单与输入路由见 `RunDemoCombatCheck.ps1`，HUD 观测投影见 `RunDemoVisionCheck.ps1` |
| 音效 / 音量 | `ImportDemoAudio.ps1 -Mode Validate`（Bootstrap Validate 也执行）；`Lockdown.Rules.Audio*`（含 `AudioMusicSelection`）与 `LocalSettingsMasterVolume`、`LocalSettingsMusicVolume`。`-nosound` 场景只证明无错误日志，实际听感、换曲、设备输出与音量需在有声卡的可视运行中确认 |
| 美术 / 装饰 | `ImportDemoArt.ps1 -Mode Validate`（Bootstrap Validate 也执行，含标题/房间背景图的清单、哈希、尺寸与 UI 纹理设置）；`Lockdown.Rules.ArtCatalog/ArtCatalogAsset/ArtDecorations`；`CaptureDemoArt.ps1` 在 Demo 地图大厅渲染全部身体、各动画姿势与装饰后的场地截图，并核对装饰无碰撞、不挡视线追踪、不改 navmesh。不代替身体接线、战斗或联机验收 |
| 战斗 / 观测复制 | `RunDemoCombatCheck.ps1` 为 DEV 拥有连接的战斗与订单场景：经普通大厅 Start，决斗点与技能点避开老板巡逻警戒范围和营地家点，收缩边外脚点作为点选地面被拒、作为追击终点可规划，已接受行走上打开设置超过订单租约（订单与心跳继续、关闭后不重放）、远程攻击移动与 S、右键击杀与击杀播报、死亡静态镜头随小地图平移、随机复活的精确保护及空 Q 解除保护；随后 Host 用 DEV 成长夹具预置候选并给 Client1 发放经验，双方经普通构筑命令学会主动（Host：扒裤子；Client1：锅盖挡脸与甩你一鞋），实战覆盖定身被观察者看到、锅盖挡脸的护盾与解除定身、甩你一鞋的 `Chalk` 飞行表现/命中/减速公开状态和基础射程内的追击普攻；Host 离屏渲染主视图并保存 `HostSees-DuelSite/MoveMarker/OwnHit/Hover/AttackMarker/Root/Shield/ChalkFlight/OwnStrike.png` 供人工审画面，不做像素比对；双方报告操作手感实测，只报告不设门槛（不含营地/AI/终局）。`RunDemoVisionCheck.ps1` 是观测场景，覆盖拓扑、受限注册表、拥有连接与权限清除，不含AI、战斗数值或完整对局验收 |
| 成长 / 奖励 | `RunTests.ps1` 中的 `DemoGrowth*`、`DemoDevTestCommands`、`DemoAssist*` 规则（含地图事件奖励按事件与序号去重）；`RunDemoGrowthCheck.ps1` 为 DEV 拥有连接的击杀奖励/构筑场景，Host 带 DEV 成长夹具预置拥有客户端的候选，先在第一局开局休息 R0 核对拥有客户端在配装步骤把开局主动学到 Q 并分配开局点数、在返回倒计时内攻击移动（含 Shift 排队）与 Q 被拒，以及两端看到同一开局与野外 1 开始时刻（`Rest0`），且第一局击杀跨越野外中段休息时两端在休息中暂停、核对每次休息只让野外生命退场而不计死亡（公开死亡数不变）并在返回后放回下一条野外生命（`Rest`），覆盖击杀后交战窗口仍在时选到的等级候选当场学到 W 并加点，死亡中选择学到 E 并加点、不复活不治疗、复活后 E 仍在，Listen Host 作为击杀者（击杀拥有客户端与低档营地野怪）在本人视图、HUD 与渲染的“+X 经验”飘字中看到精确奖励，并结束本局后开第二局核对新局隔离（上一局的构筑请求以 OldMatch 拒绝、回执查询为 InvalidContext；不含 AI/喷漆守护） |
| 网络异常 / 拥有连接 | `RunDemoSessionFaultCheck.ps1`（可加 `-PackageRoot` 对打包版运行）分七段：Leave 中客户端先打开并取消 Lobby 离开确认（说明返回主菜单、房间继续且其他成员仍留在 Lobby），再经局内菜单确认“离开房间并返回主菜单”，提示其不能重加入、本局记录保留且其他玩家继续；Host 结束后结果表记其离局且无击杀/死亡，Host/加入者的结算退出按钮分别说明关闭房间或仅离开；HostLoss 结束 Host 进程，客户端回到主菜单并显示“会话中断”，不出现结果表；Join 对关闭端口和无法解析的地址显示“连接失败：原因”，仍停在前端且可再次加入；Refuse 用真实加入按钮依次遇到版本不兼容（DEV 参数让该客户端声明另一产品版本）、三真人加三 AI 满员和对局已开始，各自显示中文原因与原因码，始终未进入房间；Race 中一次加入已通过 PreLogin，Host 在它载入房间时开局，最终准入检查拒绝它，客户端显示“对局已开始”而不是会话中断；HostLeave 中 Host 经局内菜单确认关闭整个房间并离开，客户端回到主菜单并显示“房主已离开”，不显示会话中断或结果表，Host 自己的主菜单也不显示失败；HostQuit 中 Host 进程在客户端仍连接时正常退出（与直接关窗口相同，引擎走 PreExit 拆除世界而不是换图），Host 须以 0 退出且日志无 `LD_DEMO_AUTHORITY_FAULT`，客户端回到主菜单并显示“会话中断”，不出现结果表。全部在同机回环上运行，不代表跨机器局域网或公网直连 |
| 全新检出 | `VerifyCleanCheckout.ps1`；它验证本地 LFS 缓存检出，不证明远程分发 |
| 构筑评估 Harness | `Lockdown.Rules.BuildEvaluation`（种子与生产默认、评估画像、工坊构筑计划、原生输入、对决时限）与 `python -B -m unittest discover -s .\Scripts -p "TestBuildEvaluation*.py"`；改到原生驱动、种子挂点或 Adapter 判读时，用 `RunBuildEvaluation.ps1 -Manifest <示例清单>` 跑一次真实小规模运行，并用 `-ReplayRun <运行目录> -Case <id>` 核对重放一致。它是 DEV 实验工具，结果只描述所覆盖的构筑、固定策略、场景与预算，不是验收或平衡结论，见[构筑评估 Harness](BUILD_EVALUATION.md) |

`RunDemoSessionFaultCheck.ps1 -RefuseOnly` 只运行版本不兼容、六席满房和开局后的拒绝场景，不覆盖其他网络异常。

## 合并前与里程碑检查

每个里程碑交付、每日包/发布或明确要求的专项验证前，跑一轮下面的检查：

```powershell
.\Scripts\BuildEditor.ps1 -FullUnity
.\Scripts\BuildGame.ps1 -FullUnity
.\Scripts\RunTests.ps1 -ImportTests Run
python -B -m unittest discover -s .\Scripts -p "Test*.py"
.\Scripts\BootstrapAssets.ps1
.\Scripts\RunDemoVisionCheck.ps1     # 截图：HUD、身份色、装饰与环境
.\Scripts\RunDemoOpponentCheck.ps1   # 3 真人 + 3 AI 满员对局，多个模块的检查合并在一局里
.\Scripts\RunDemoOpponentCheck.ps1 -UIReview 2v2 -TimeoutSeconds 300 # 仅 R0 团队 UI，正常报名与真实拥有连接
.\Scripts\RunDemoOpponentCheck.ps1 -UIReview 3v3 -TimeoutSeconds 300 # 同上，六人报名
```

`-UIReview` 固定一个能按获准概率抽到团队的 DEV 随机种子，不改产品参数或复制视图。三个拥有连接
经正常大厅开局并自行走进报名圈（2v2 仅 Client1 加三 AI；3v3 全员），核对公开阵营和当前获准观察，
Host 与 Client1 保存 100%/125% 团队截图，Client2 无渲染但同样检查真实拥有连接的公开关系与观察；
Host 另存两种分辨率、两档文字缩放的构筑与服务图，避免三路渲染挤占同机显存。
Host 沿既有 `LDDevGrowthFixture` 经验入口取得一次普通升级构件，真实选择并安装开局构件，
使背包同时含已安装/未安装项，并经面板执行加点增减和一次确认、服务购买与后续拒绝反馈；
不把这些夹具成长当作自然对局结果。
PK 结算还独立核对各类基准池乘 1.5 后向上取整与按场次轮换的公开奖励行、渲染 HUD 文字，
以及三个拥有连接的实际属性点到账（未分配＋已分配＋永久花费的合计，避免同期构筑花费影响比较）。
2v2 的两名真实观众经正常押注入口分别押两方，要求实际有胜方且一名观众收到竞猜奖；
平局、NoContest 或漏跑不计作该奖励覆盖通过。Host 在所有端确认首次到账后，
让同一组奖励身份在下一正常 Transactions 边界重试，核对权威奖励余额和拥有端点数没有重复增长。
runner 要求三端恰好报告各项必需检查、相同公开结算与精确到账，另要求 Host/Client1 的结算截图；
缺失、重复或 `not_exercised` 都失败，不用截图存在替代数值断言。
此模式不运行 R01/R02/R14 阻挡夹具，也不是完整 AI 对局验收。

AI 满局遥测不是验收：`RunDemoOpponentCheck.ps1 -Telemetry -MatchCount <1-50>` 只启动一个无渲染的 Listen Host，
它作为唯一真人留在房间里不动，与三个 AI 经正常大厅连打 N 局（每局各有 `-TimeoutSeconds`，局间回 Lobby）。
每局写一份 `MatchNN.json`：结束原因（`GuardSucceeded`/`HostEnded`/`None`）与胜者、旗杆开放与首杀（以及首个死亡）时刻、全部击杀及其助攻者、
每次参赛者死亡的击杀者与助攻者各自所得经验（取自 `ALDGrowthDirector::DevRecentRewards`，每条生命一次）、每名参赛者每秒的等级/经验曲线、
喷漆尝试/续喷/被争夺暂停/失去/被打断/喷成次数、争夺、喷成、改漆与涂鸦因主人死亡或离局被清除的次数、
获胜涂鸦的守护用时（结果时刻减喷上时刻）（取自喷漆与守护归约的 DEV 计数 `FLDCaptureDevTally`，边界内的中间变化也计入），
以及成长记录：首个主动的获得时刻、已做选择数、每次学习主动的等级、终局主动与图纸、未分配点数与各五维点数
（记录与汇总的 schema 均为 5）；`SummarizeOpponentTelemetry.py` 汇总为 `Summary.json`
（胜负方式、各槽获胜数、时长与首杀分布、每分钟等级、喷漆/改漆/清除原因与守护用时、死亡与助攻奖励、
首个主动时刻、选择与学习次数、学习时等级、终局主动与图纸数、未分配点数与五维点数去向，以及 AI 的等级、击杀、助攻、死亡与每分钟等级）。这些数字只描述这几局，不作为平衡目标，也不判断 AI 行为对错。

其余场景（Core、Combat、Camp、Growth、Capture、SessionFault）放在里程碑或发布冻结后的完整验收里，在打包版上各跑一次。
改到某个场景覆盖的系统时，PR 里也要跑那个场景。
新增的原生测试要同步登记到 `CheckRuleReport.py`。如果某个模块的检查需要真实对局，优先补进满员场景，不另开新场景。

## 规则测试集合

`RunTests.ps1` 执行已有 `Lockdown.Rules` 套件，由 `CheckRuleReport.py` 核对明确的当前必需用例集合。
缺报告、失败、未完成、缺用例或重复结果都不能通过；新增或迁移原生测试时同步维护此集合，
并运行 `python -B -m unittest discover -s .\Scripts -p TestRuleReport.py` 核对源码注册与报告拒绝路径。
带 `-Filter` 或跳过数据导入组时，`CheckRuleReport.py --plan` 先核对源码注册与必需集合完全一致，
再把所选用例换算成 `Automation RunTests` 的锚定过滤（`^前缀.`、`^完整名$`），报告必须恰好包含所选用例。
原生测试夹具由 `ExportDemoNativeFixtures.py --cache Artifacts\DemoContentFixtureCache --profile <runtime|full>` 提供：
按它读取的全部输入（自身及 `Scripts` 内的 import、`SourceData`、生成的 Demo 数据、原生 ID 与 contract 头文件）的哈希建目录。
清单记录导出过程实际写出的每个文件及其哈希，发布前与暂存目录逐一比对；每次使用前再逐个核对，输入不变时直接复用，任何输入或文件变化都会重新导出。
保留当前和上一份；别的运行正在写的暂存目录（`*.tmp`）不清理，超过 6 小时才视为遗留并删除。
`runtime` 只写普通规则与 `DemoContent.DevReload` 需要的四个有效用例和一个轻量拒绝用例；选中任一 `IMPORT_TESTS` 时自动改用 `full`，
生成完整有效变异与拒绝矩阵。
原生测试只读这些夹具（需要改源文件的检查先复制到 `Intermediate` 下的临时目录）。
需要更具体的选择、网络条件或打包路径时，先查看相关脚本参数，不把所有昂贵场景作为每次文档改动的门槛。
Core、Combat、Camp、Growth、Capture、Vision、Opponent 和 SessionFault 这 8 个 `RunDemo*Check.ps1`
接受 `-PackageRoot <PackageWin64 的 Archive 目录>`，改为启动打包的 Development 版；
Frontend、Entry 与 WorldRoom 只在编辑器中运行。
DEV probe 只在带开关时生效，证据先写入包内 `Lockdown\Artifacts`，结束后移回 runner 的输出目录。
编辑器与打包版行为可能不同（例如非编辑器构建的 FName 不保留大小写），交付验收以打包版场景为准。
`RunDemoCombatCheck.ps1` 与 `RunDemoOpponentCheck.ps1` 另接受
`-NetEmulation '-PktLag=40 -PktLagVariance=10 -PktLoss=1 -PktDup=1 -PktOrder=1'` 这类引擎包模拟参数
（只在 Development/编辑器构建生效），每个角色进程只影响自己发出的包；
runner 核对每个角色的日志确实解析了每一项。这只是同机模拟，不代表跨机器局域网或公网直连。
`RunDemoCombatCheck.ps1` 与 `RunDemoVisionCheck.ps1` 的 Host 主视图默认 1280×720，
加 `-Resolution 1080p` 以 `-ForceRes` 渲染 1920×1080（输出目录带 `-1080p` 后缀），用于两种分辨率的截图审阅。
源码、配置和生成输入在一次原生构建/运行期间保持稳定。
删除反射类型或成员前，可用 `AuditAssetReferences.ps1 -RequestPath <审计请求JSON>`。
它在旧原生类仍存在时加载全部项目包，记录 AssetRegistry、linker import/soft/name 表、
对象引用、Blueprint/CDO 和 WorldSettings；配置/动态源码路径仍要单独核对。
输出位于忽略的 Artifacts。类型已退休后的复查使用 `-AllowMissingRoots`，
不能拿这一开关代替删除前的根存在证明。

`CaptureInputUI.ps1` 复用现有 Demo 前端原生 RHI/UMG 捕获夹具，可指定窗口尺寸，
在独立 `UserDir` 下生成前端、五个设置分类各自 100/125%（`Settings-<Display|Audio|Controls|Camera|Interface>-<100|125>.png`）、
显示草稿丢弃、改绑冲突与恢复本页默认确认图像。
图像存在不等于真实 OS 输入或完整对局验收。

### 数据导入测试组

`RunTests.ps1` 默认使用 `-ImportTests Auto`。其中有 5 个数据导入测试（`CheckRuleReport.py` 的 `IMPORT_TESTS`：
`DemoContent.TypedProjection/StrictRefusal/AtomicAssetRoundTrip` 与 `DemoWorld.TypedProjection/AtomicAssetRoundTrip`），
它们把全部导出夹具经原生导入器逐个导入，占一次完整运行的大半时间。
它们只读 SourceData、生成的 Demo 数据、夹具导出脚本及其 import、导入器原生源码（由 `CIChangeScope.py`
从导入器文件按 `#include` 闭包算出；闭包只按改动后的代码计算，所以删除或改名掉的原生源文件一律算作输入）、规则测试 runner 与构建配置。`RunTests.ps1 -ImportTests Auto`
用 `CIChangeScope.py --worktree` 比较当前工作区（含未提交与未跟踪文件）与 `origin/main` 的合并基点，
这些输入都没变时跳过该组，其余必需测试照常全部运行；`-ImportTests Skip` 直接跳过，
`-ImportTests Run` 显式运行完整导入门禁。没有运行导入组时，runner 只生成 `base`、`approved_base`、
`exact_integer`、`world_spawn_shift` 四个运行规则所需有效夹具和一个轻量拒绝夹具，不再生成全部 rank 变异和拒绝用例。
规则测试统一经 `Tests/LDTestApprovedFixture` 读取 `approved_base`：同一进程第一次调用严格校验索引并导入，
之后共用这份不可变快照，不再每个测试各导入一次；失败不缓存，下一次调用重新报错。
PR 检查用同一判定，改到这些输入的 PR 一定会跑该组。

## 多人共用一台机器

引擎的 `Build.bat` 是全机唯一的：任一工作区在构建时，其他 `Build.bat` 调用会排队到它结束。
每个编辑器进程启动时都会调用一次 `Build.bat -Mode=ValidatePlatforms` 检查平台 SDK，
因此会等别人的构建跑完（实测有人构建时一次 `-game` 启动在这里等了 4 分 21 秒）。
`RunTests.ps1`、所有编辑器版 `RunDemo*Check.ps1`（经 `Get-LDGameLaunch` 或直接启动）与
`CaptureDemoArt.ps1`、`CaptureInputUI.ps1` 都不 cook、不打包，
它们经 `Common.ps1` 的 `Set-LDPlatformValidation Skip` 设置引擎自带的 `UE_SKIP_UBT_SDK_SETUP=1`，
跳过这一步并把平台 SDK 视为有效，所以不再被其他构建卡住；这不改动共享引擎安装。
`OpenEditor.ps1`、`OpenStandalone.ps1`、`PackageWin64.ps1` 与资产 commandlet
（`BootstrapAssets`、`ImportDemoArt`、`ImportDemoAudio`、`AuditAssetReferences`）保留真实校验，
并清除同一 shell 里前一个 runner 留下的该变量，它们在别人构建期间仍会等待。
自己的 `BuildEditor`/`BuildGame` 同样会排队，排队时日志首行是
“Build.bat is already running, waiting for existing script to terminate...”。
打包版场景（`-PackageRoot`）不启动编辑器，不受影响。共用机器时控制并发：
无头编辑器总数不超过 3 个，已有 2 个 UnrealBuildTool 构建进程（`dotnet ... UnrealBuildTool.dll`）时先等，
否则内存不足会拖慢或打断所有人的构建和场景。
同一台机器上最多 2 个会话同时做重验证（构建、完整规则测试、场景），场景运行彼此错开。

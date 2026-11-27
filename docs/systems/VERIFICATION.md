# 验证与场景矩阵

日常开发与 PR 提交遵循“最小变更范围验证”：一批改动改完后验证一次；全量原生套件、完整多进程场景、打包与帧率测量留给 CI、集成与里程碑发布门禁。
何时立即验证、何时随批验证、何时留到集成见 [AGENTS：验证与交付](../../AGENTS.md#验证与交付)。

## 最小验证选择

每批连贯改动后优先运行 `.\Scripts\Verify.ps1`（纯文档不构建；改了新增/修改的规则测试时传 `-Filter <测试全名>`，只改产品源码未改测试时不跑本地规则测试；可用 `-Plan` 预览选择理由、`-Rules Auto` 按 `#include` 闭包跑有界局部规则）。

| 变更域 | 最小本地验证入口 |
| --- | --- |
| `docs/**`、`*.md` | 无需构建；核对相对链接与代码事实一致 |
| `Scripts/*.py` | `python -B .\Scripts\RunPythonTests.py`（失败后可用 `--failed` 定向重跑） |
| `SourceData/Demo/*` / `World/*` | `SyncDemoWorldData.py --check`、`SyncDemoContentData.py --check`、`Test*Import.py`；刷新资产用 `BootstrapAssets.ps1 -Mode RefreshDefinitions`（改几何用 `Regenerate`） |
| `SourceData/Art` / `UI` / `Audio` | `ImportDemoArt.ps1 -Mode Validate`、`ImportDemoAudio.ps1 -Mode Validate` |
| `Source/Lockdown*` C++ | `.\Scripts\Verify.ps1`（增量 `BuildEditor.ps1`）；新增原生 `Lockdown.Rules.*` 测试须同步登记到 `Scripts\CheckRuleReport.py` 的 `REQUIRED_TESTS` 并跑 `TestRuleReport.py` |
| 必须由真实拥有连接证明的联机/视野/阶段行为 | 按需选跑对应 `RunDemo*Check.ps1`：`Core`（初生/运动/返场）、`Combat`（寻路/施法/追击/离屏帧）、`GrowthLink`（成长指令的回执与视图同步、一次真实击杀）、`Camp`（反击、回位、段内清空和中场整图重置）、`Vision`（视野隔离/`-ReactionOnly`/`-IceOnly`）、`Capture`（三段野外+R3终局改装+断线恢复）、`Opponent`（满房 AI）、`SessionFault`（退房/拒入/房主离开） |
| 整局能否跑完、有没有报错（AI、权威步、营地、结算一起跑） | `.\Scripts\RunDemoOpponentCheck.ps1 -BotMatch`（见[机器人对局](#机器人对局)） |

### 按问题性质选最便宜的入口

同一个问题用能证明它的最便宜的一层，不用更贵的一层重复证明；耗时是本机编辑器版的典型墙钟（含进程启动，编辑器版到地图就绪约 15–30 秒，打包版约 5 秒）。
渲染入口在房主被拒绝进入地图、断言或权威故障时立即失败并给出日志行；连续漏三页也立即停，不逐页等超时。

| 问题性质 | 入口 | 典型耗时 | 何时跑 |
| --- | --- | --- | --- |
| 公式、数值、状态机、结算 | 规则测试：`Verify.ps1 -Filter <测试全名>`（无渲染、隔离世界） | 约 35 秒 | 本批新增或修改了测试时 |
| AI 决策、整局能否跑完 | `RunDemoOpponentCheck.ps1 -BotMatch`（固定步长加速、无渲染） | 7–9 分钟 | 改 AI、权威步或结算流程的一批 |
| 特效、动作、打击感的画面 | `CaptureDemoArt.ps1`、`CaptureFeelSample.ps1`（验证舞台直接触发，不走对局） | 1–2 分钟 | 画面有变化的一批，一次 |
| 界面页面 | `CaptureUICoverage.ps1 -Pages <页面名前缀>`：只拍这些页并在最后一页后结束，途中的页照走不拍；阶段倒计时由 DEV `AdvanceCountdown` 跳过；默认只拍 1920×1080、100% 字号，`-AllSizes` 加拍 1280×720 与 125% | 只拍结算页约 1 分钟（`-Pages Match_Results`：房主带 AI 单独开局，DEV `DecideMatch` 直接锁定结果，不走整局），只拍大厅页约 2.5 分钟，只拍第一次休息各页约 5.5 分钟，对局全部页约 10.5 分钟 | 界面有变化的一批，按改到的页选；全部页与 `-AllSizes` 只在集成 |
| 指令、回执、视图同步等真实拥有连接行为 | 对应的 `RunDemo*Check.ps1` 最小场景 | GrowthLink 约 1 分钟，Combat 约 4 分钟，Core、Camp、Vision 3–6 分钟，Capture 约 11 分钟 | 改到该行为的一批，一次 |
| 满房真实连接 | `RunDemoOpponentCheck.ps1`（实时整局） | 约 15 分钟 | 只在集成 |
| 帧率 | `RunPerfBudget.ps1 -Match -PackageRoot <打包目录>` | 约 1.5 分钟，另加打包 | 只在集成 |
| 打包 | `PackageWin64.ps1` | 约 25 分钟 | 只在集成 |

### 成长的四层验证

成长相关的行为按层验证，每层只证明它能证明的东西；精确数值只写在前两层。

| 层 | 证明什么 | 入口 |
| --- | --- | --- |
| 规则测试 | 公式、结算、状态机的精确数值 | `Lockdown.Rules.DemoGrowth*`、`Growth.*`，PR CI 全跑 |
| 权威层集成测试 | 多个系统在隔离世界里一起结算，不要渲染和网络 | 如 `Lockdown.Rules.DemoAssist.Settlement`（`LDGrowthRulesTests.cpp`） |
| 真连接短场景 | 指令能到、回执能回、拥有者自己的视图与 HUD 跟上 | `.\Scripts\RunDemoGrowthLinkCheck.ps1` |
| 机器人对局 | 整局跑完、没有报错、每个 AI 都在成长 | `.\Scripts\RunDemoOpponentCheck.ps1 -BotMatch` |

`RunDemoGrowthLinkCheck.ps1`（探针 `LDDemoGrowthLinkProbe.cpp`）：一个渲染的 Listen Host（带 `-LDDevGrowthFixture`）加一个无头的真实客户端，零 AI，含启动约一分半到两分钟。
状态用 DEV 测试命令经各自的拥有连接摆好：Host 跳过开局休息、暂停阶段倒计时、把自己传送到客户端身边当靶子；客户端 DEV 发经验。断言只写不变量：

- 客户端的 DEV 经验发放回执为已应用，它自己的视图里经验、等级与主动机会都增加；
- 客户端经 `SubmitBuild` 选主动、加一点，回执均为已应用，视图多出该主动、该属性点数增加，战斗投影带上该技能且生命上限变大；
- 客户端自己的攻击指令以目标死亡结束，视图经验增加，且等于服务器对该选手的权威记录；Host 的生命确实结束，公开击杀播报指向客户端；
- Host 渲染的 HUD 等级徽标与它自己的视图一致。

它不重算任何期望数值，不断言未分配点数（热搜存活点随时到账）。休息跨段、死亡期间构筑、复活、营地奖励与换局隔离没有真连接场景覆盖；这些行为的精确断言属于前两层。

### 机器人对局

`RunDemoOpponentCheck.ps1 -BotMatch [-AICount <1-7>] [-MatchCount <1-50>]`：一个闲置的无头 Listen Host 加满房 7 个 AI（`-AICount` 可改），以 60 步/秒固定步长不渲染地打完整局，一局含启动约 5–7 分钟墙钟。
`JudgeBotMatch.py` 读取 `Telemetry.json`、各局 `MatchNN.json` 与日志，只判不变量，结论写入 `Verdict.json`：

- 每一局都以锁定的结果结束（有结果 ID，结束原因不是空也不是房主手动结束），且墙钟时间在 `-TimeoutSeconds` 之内；
- 日志没有 Error、Fatal、断言失败或 `LD_DEMO_AUTHORITY_FAULT`；
- 房间是一名闲置 Host 加指定数量的 AI，无人离开；
- 每个 AI 都做过主动选择、学会过主动且经验有增长。

`LogLD*: Warning: LD_*` 一类的游戏告警（如营地回位受阻 `LD_CAMP_RETURN_DEAD_END`、Boss 寻路被拒 `LD_BOSS_PATH_REJECTED`）按标识计数并列出，不判失败。
等级、击杀数、胜者与任何平衡数值都不判；它只有 Host 一个进程，没有远端连接，不能代替真实拥有连接的满房检查（不带 `-BotMatch` 的原入口）。

### 数据导入测试组

`RunTests.ps1` 默认 `-ImportTests Auto`：由 `CIChangeScope.py --worktree` 检测 `SourceData`、导出脚本与导入器 `#include` 闭包，未改动时自动跳过 5 个重型导入测试（`DemoContent.TypedProjection/StrictRefusal/AtomicAssetRoundTrip`、`DemoWorld.TypedProjection/AtomicAssetRoundTrip`）并复用 `Artifacts\DemoContentFixtureCache` 的 `runtime` 夹具；改到导入链路时传 `-ImportTests Run` 跑完整变异/拒绝矩阵。完整套件必须显式传 `RunTests.ps1 -Full`。

## 合并前与里程碑检查

Ready PR 由 `.github/workflows/pr-check.yml` 自动把关 `scope -> python -> native (BuildEditor + RunTests -Full)`。里程碑交付或发布冻结前在打包版执行完整检查：

```powershell
.\Scripts\BuildEditor.ps1 -FullUnity
.\Scripts\BuildGame.ps1 -FullUnity
.\Scripts\RunTests.ps1 -Full -ImportTests Run
python -B .\Scripts\RunPythonTests.py
.\Scripts\BootstrapAssets.ps1
.\Scripts\PackageWin64.ps1
.\Scripts\RunDemoVisionCheck.ps1 -PackageRoot '<Archive目录>'
.\Scripts\RunDemoOpponentCheck.ps1 -PackageRoot '<Archive目录>'
.\Scripts\RunDemoOpponentCheck.ps1 -BotMatch -MatchCount 3 -PackageRoot '<Archive目录>'
.\Scripts\RunDemoGrowthLinkCheck.ps1 -PackageRoot '<Archive目录>'
```

AI 多局遥测（诊断用，非门禁）：`.\Scripts\RunDemoOpponentCheck.ps1 -Telemetry -Accelerated -MatchCount <1-50> [-AICount <1-7>]`（60 步/秒固定步长，约 2 倍速以上跑完，默认 3 个 AI，输出 `Summary.json`；只记录不判定）。

## 编辑器进程的启动开销

每次规则测试、每个编辑器版场景进程都要启动一个编辑器进程。脚本用三处设置压低这部分固定开销：

- `Common.ps1` 设置 `UE-ZenLimitProcessLifetime=false`：引擎的本机 Zen 缓存服务不再随编辑器进程退出，下一次启动直接连上已在运行的 `zenserver.exe`。它会在脚本结束后继续留在后台，需要时可手动结束，下次启动会重新拉起。
- `RunTests.ps1` 显式启用 `GameFeatures` 插件：`-DisablePlugins=AllToolsets` 会连带不加载它，而 `DefaultGame.ini` 仍要扫描它的资产类型，否则每次启动都会触发一次引擎断言并发送错误报告。
- `RunTests.ps1` 让编辑器以引擎空地图启动，不加载编辑器默认地图 `L_Demo`：规则测试自己搭建世界，或自行加载要读取的地图。

## 多人共用一台机器

一台机器上的并行会话（包括各自 worktree 里的 AI 会话）共用三种资源，等待它们是并行开发最大的时间开销：

| 资源 | 上限 | 做法 |
| --- | --- | --- |
| 引擎构建（`BuildEditor`、`BuildGame`、Verify 的构建步骤） | 全机同时 1 个：共享引擎 `Build.bat` 全局互斥，第二个构建会被阻塞或以 `ConflictingInstance` 失败 | 已有 UBT（`dotnet ... UnrealBuildTool.dll`）在跑时先做别的，不重复发起；所以每批只构建一次 |
| 打包（`PackageWin64.ps1`） | 全机同时 1 个：UAT 单例，跨工作区须串行 | 只在集成分支上做 |
| 渲染运行（截图、录制、带画面的场景、`-game`） | 同时最多 2 个 | 每批一次，一次覆盖全部改动 |
| 帧率测量（`RunPerfBudget.ps1`） | 独占：期间没有其他渲染运行、构建、cook 或着色器编译 | 只在集成分支上做；环境不干净时测得的数字作废 |
| 无头编辑器进程（规则测试、无画面场景） | `<= 3` 个 | — |

`RunTests.ps1` 与编辑器版 `RunDemo*Check.ps1` 通过 `UE_SKIP_UBT_SDK_SETUP=1` 跳过启动期 SDK 校验，不被他人的构建阻塞。
编辑器开着且启用 Live Coding 时，同一工作区的构建会被拒绝，先关编辑器。
需要编译和截图的会话同机最多两个；第三个并行任务选不占引擎的工作（文档、数据、Python、资产制作），否则它的时间主要花在排队上。

# 开发指南

产品目标见 [App Spec](APP_SPEC.md)，协作约束见 [AGENTS](../AGENTS.md)，验收状态见[实施就绪评估](demo/READINESS.md)。
本页按章节查阅，不需要通读。

## 环境与首次启动

使用 PowerShell 7、Python 3、Git LFS、Unreal **5.8.4** 及兼容的 MSVC/Windows SDK。
精确引擎基线在 `Config\PlatformLock.json`。需要 .NET Framework SDK，只有现代 .NET runtime 或 targeting pack 不够。

在工作区根目录执行：

```powershell
git lfs install
git lfs pull                     # 新工作区的 Content 只有 LFS 指针，规则测试、编辑器和场景都需要先拉取
$env:LD_UE_ROOT = '<UE 5.8.4 安装目录>'
.\Scripts\Preflight.ps1          # 核对引擎版本与工具链
.\Scripts\BuildEditor.ps1        # 编辑器目标；编辑器、规则测试和 -game 运行都只需要它
.\Scripts\OpenStandalone.ps1     # 打开前端；-HostRoom 直接以 Host 进入 L_Demo
.\Scripts\OpenEditor.ps1
```

`BuildGame.ps1`（游戏目标）只用于里程碑检查，日常开发不需要；打包脚本自己构建。
引擎查找依次使用 `-EngineRoot`、`LD_UE_ROOT`、忽略的 `Config\Local\Environment.json` 和项目引擎关联；
本机路径不写进共享配置，每个新 shell 都要设置自己的环境变量。
修改反射类时先关闭编辑器。`-HostRoom` 不会绕过已批准数据与原生合同检查。

仓库的 `unreal-mcp`（`.mcp.json`）由编辑器里的 ModelContextProtocol 插件在 `127.0.0.1:8000` 提供，
只有 `OpenEditor.ps1` 打开的编辑器在运行时才能连上；`OpenStandalone.ps1`、规则测试与打包版都禁用该插件。
编辑器打开的是哪个工作区，MCP 改的就是哪个工作区。

## 验证

改完一批连贯改动后，在工作区根目录跑一次：

```powershell
.\Scripts\Verify.ps1                                    # 按改动决定跑什么
.\Scripts\Verify.ps1 -Plan                              # 只列出会跑的检查
.\Scripts\Verify.ps1 -Filter Lockdown.Rules.DemoGrowth  # 另跑这些前缀下的规则测试
```

它比较工作区（含未提交与未跟踪文件）与 `origin/main` 的合并基点：

| 改动 | 本地运行 |
| --- | --- |
| 只有 `docs`、`*.md`、工作流 | 不运行 |
| `Scripts/*.py` | 对应的 `Test<模块>.py`；有模块找不到对应测试时跑全部 Python 测试 |
| `SourceData`、`Content/Lockdown/Data` | 两个 Sync `--check` 和全部 Python 测试 |
| `Source`、`*.uproject` | 一次增量 `BuildEditor.ps1` |
| 其他（`Config`、资产、PowerShell 脚本） | 不运行；按[验证手册](systems/VERIFICATION.md#最小验证选择)选对应检查 |

- 规则测试的门禁是 PR 的 CI（[CI 与发布](systems/CI_AND_RELEASE.md#pr-检查与每日试玩包)），本地不重复跑整套。
  新增或改了规则测试、或在排查失败用例时，才加 `-Filter`（可给多个前缀）；它是部分运行，不算门禁。
  新增的原生测试同步登记到 `CheckRuleReport.py`，并在 PR 里写明测试名。
- 一批改动跑一次，不在每次小编辑后构建或测试。无冲突地合并 `main` 后不重跑；
  冲突涉及生成文件（`Content/Lockdown/Data`、生成资产、`*Contract.h`）时沿 Sync 入口重新生成，不手工拼接。
- 场景（`RunDemo*Check.ps1`）只在改动涉及网络、交易、成长、胜负等必须用真实拥有连接证明的行为，
  或 Issue 明确要求时才在 PR 前跑；其余集中到里程碑验收。
- `-FullUnity`、`RunTests.ps1 -ImportTests Run`、全套场景只用于里程碑、发布和全新检出，
  清单见[验证手册](systems/VERIFICATION.md#合并前与里程碑检查)。
- 新工作区没有 `Binaries` 与 `Intermediate`，第一次构建要 3–6 分钟（之后的增量构建约 20 秒）。
  只改文档、数据或 Python 的任务不需要构建。
- Python 测试失败时看输出结尾 `Log:` 指向的日志，修复后用 `python -B .\Scripts\RunPythonTests.py --failed` 只重跑失败项。
- PR 保持 Draft 到改动稳定：转 Ready 后每次推送都占用自托管 runner。
- 共用机器的并发上限见[验证手册](systems/VERIFICATION.md#多人共用一台机器)。

## 按任务读取系统手册

| 工作 | 技术说明 |
| --- | --- |
| 模块、Lobby、IP 联机、生命代次、回执 | [架构与网络](systems/ARCHITECTURE_AND_NETWORKING.md) |
| 点击移动、预测、取消、输入层、镜头 | [移动、输入与镜头](systems/MOVEMENT_INPUT_AND_CAMERA.md) |
| 伤害/状态、时序、效果来源、构筑 | [战斗与成长](systems/COMBAT_AND_GROWTH.md) |
| 奖励、选择、去重、原子事务 | [成长事务](systems/GROWTH_TRANSACTIONS.md) |
| 战争迷雾、观测句柄、HUD 与隐私 | [视野与 UI](systems/VISION_AND_UI.md) |
| 地形、碰撞、动态对象、SourceData、资产 | [世界与数据管线](systems/WORLD_AND_DATA_PIPELINE.md) |
| 每个检查与场景覆盖什么、规则测试集合、里程碑清单、共用机器 | [验证手册](systems/VERIFICATION.md) |
| 打包、产品版本、PR 检查、每日试玩包、自托管 runner | [CI 与发布](systems/CI_AND_RELEASE.md) |
| 构筑评估（DEV 实验编排、原生重放、合法成长与反事实报告） | [构筑评估 Harness](systems/BUILD_EVALUATION.md) |

这些手册保留当前有效的实现知识。含有兼容名称的组件或运行数据不自动成为 Demo 需求，
未核对代码的旧设计提案也不作为当前架构事实。

依赖级复用、已完成的删除闭包与防回归约束见 [架构审计](demo/ARCHITECTURE.md)；
它不替代 App Spec 的产品规则。

## 工程目录

| 目录 | 职责 |
| --- | --- |
| `Source\Lockdown\Public` / `Private` | 当前原生运行模块接口与实现 |
| `Source\LockdownEditor` | 原生导入、地图/资产生成与保存 |
| `SourceData` | 当前生成器消费的内容定义、schema、ID 映射与 CSV |
| `Content\Lockdown\Data` | 已生成并校验的运行载荷 |
| `Content\Lockdown\Generated` | 原生目录、派生碰撞网格与保存的正式关卡 |
| `Content\Lockdown\Audio` | `ImportDemoAudio.ps1` 由 `SourceData\Audio` 清单导入的音效目录与声音资产 |
| `Content\Lockdown\Art` | `ImportDemoArt.ps1` 由 `SourceData\Art` 清单导入的身体、动画、装饰道具与美术目录；`Art\UI` 是 `-Mode ImportUI` 由 `SourceData\UI\ui_images.json`（项目方提供的标题与房间背景图，不属于 CC0 美术包）与 `ui_icons.json`（`SourceData\UI\Icons` 的主动、五维属性、基础构件与状态图标，#85）导入的 UI 纹理 |
| `Config` | 项目、平台与布局配置；本机设置放忽略的 `Config\Local` |
| `Scripts` | 构建、生成、运行和当前结果校验 |
| `docs` | 当前产品规则与组件技术说明，不存构建输入或每次运行档案 |
| `Artifacts` | 本机运行输出，Git 忽略 |

## 数据修改

正式地图的布局在 UE 关卡里编辑，保存并关闭编辑器后执行 `Scripts/SyncDemoMap.ps1`；不要手动维护第二份世界坐标。
细节与约束见[标准关卡编辑与同步](systems/WORLD_AND_DATA_PIPELINE.md#标准关卡编辑与同步)。以下 SourceData 创作规则继续适用于玩法内容和数值。

根据系统手册找到源文件及对应 Sync 入口，修改 SourceData，而不是直接改生成载荷或 contract 宏。
schema、单位、稳定 ID、来源与原生处理器要一致。源文件可能按字节哈希，避免无关换行或排版修改。

Demo 编译器和机制检查：

```powershell
python -B -m unittest discover -s .\Scripts -p TestDemoContentImport.py
python -B .\Scripts\SyncDemoWorldData.py --check
python -B .\Scripts\SyncDemoContentData.py --check
.\Scripts\TestDemoEntry.ps1
```

没有批准的 `SourceData\Demo\content.json` 及实际世界载荷/contract 时，
Demo `--check` 的明确失败是输入门禁，不是可玩成功。
现有首轮方案已获批准并落入作者数据。修改时先运行 `SyncDemoWorldData.py`，
再运行 `SyncDemoContentData.py`；重建 Editor 后用 `BootstrapAssets.ps1 -Mode Regenerate`
更新唯一原生目录/地图，不手改生成头或用旧值补字段。
无需重建几何的内容变更使用 `-Mode RefreshDefinitions`，随后仍须通过保存资产校验。
Demo 内容/世界生成器的 Python 导入回归可单独运行：

```powershell
python -B -m unittest discover -s .\Scripts -p "Test*Import.py"
```

授权修改源定义后，不带 `--check` 运行对应生成器，复核全部输出，再做原生导入/相关检查。
依赖链可能同时更新载荷和头文件；不能只提交其中一边。
生成器失败要修正输入或真实依赖，不能跳过校验、填写默认值或使用历史档案让错误看起来通过。

### Bootstrap 模式

`BootstrapAssets.ps1` 默认只做 Validate，先执行 Demo compiler `--check`；
缺批准输入、世界载荷/contract 或原生世界资产时失败。Generate 创建缺失的 Demo 目录资产，正式世界必须恢复已保存的 LFS 地图；
Regenerate 刷新保存关卡的派生碰撞、导航和目录，不重新摆放对象或重写材质；两者均不生成旧地图或旧目录。
仅内容/显示元数据或根摘要变化时，可用 RefreshDefinitions 重新导入已有的
`DA_DemoContent`、`DA_DemoWorld`，不重写地图或 mesh；它仍重新加载并验证已有世界，
几何或导航不匹配会失败，不会偷偷改为生成。该模式要求三个 Demo 资产已经存在。
Validate 从保存的地图重新加载并完成 mesh/physics 初始化，再核对原生碰撞、导航和地点绑定。

### 调参热重载（DEV）

只调 `SourceData\Demo\content.json` 里的数值时，可以不重新编译 C++、不重启编辑器：
用 `OpenEditor.ps1 -DevContentReload`（PIE）或 `OpenStandalone.ps1 -HostRoom -DevContentReload` 启动
（即 `-LDDevContentReload`，只在未 cook 的 DEV 构建中生效），改完运行 `python -B Scripts\SyncDemoContentData.py`，
回到大厅开新局即使用新数值。规则如下：

- 每局开始时（`InitializeAuthorityCore`）读一次磁盘上的 Sync 输出并固定到局末，局中改文件不影响本局；进程内其他读取者拿到的是同一份。
- 只放开 `demo_content.json`（及其显示文本与根摘要）：载荷仍经完整原生导入校验；`world.json`、世界源文件、schema、原生 ID 与 contract 必须与构建一致；`DA_DemoWorld` 仍须按新数值重新投影出完全相同的几何。任何一项不符，开局失败并说明原因，需要 Bootstrap 加重新构建。
- 房间只接受房主与 AI，远程客户端加入时被拒绝（它跟不上房主每局的重载）。
- 打包版带此开关时内容加载直接失败，仍需重新 cook。`RunDemoOpponentCheck.ps1 -Telemetry -DevContentReload` 用同一机制跑遥测，每局记录所用内容根。
- 热重载只是试数值的捷径：数值经批准后照常 Sync、`BootstrapAssets -Mode RefreshDefinitions` 并重新构建；热重载会话的结果不作为验收证据。

## 运行与联机

`OpenStandalone.ps1` 和包内默认入口打开 `L_Frontend`；编辑器默认打开正式工作关卡 `L_Demo`。
Host 仅请求 `/Game/Lockdown/Generated/Maps/L_Demo?listen`；
`LDDemoEntry` 在本地旅行前及原生 GameMode 内拒绝旧 profile、旧地图和缺失/不匹配的 Demo 载荷。
Join 还携带独立 wire 版本、产品版本和根 manifest 摘要；连接中不表示准入已成功。
原生 GameMode 不再通过旧 profile 创建战斗、经济、人口、旧目标或 probe 服务。

GameMode 提供三真人容量、Host 权限、0–3 AI 名额配置、结构名单版本与有界会话回执。
Ready 变化不增加结构版本，成员/AI 名额变化清除真人 Ready；开局时每个 AI 名额由对手提供者生成一个服务器
`ALDOpponentController`。只有全部正常开局提供者都已注册时才能开局，缺任一提供者时明确拒绝。
玩家名称经 `?LDName=` 传入并由服务器按设置中的名称规则复核；加入被拒时客户端显示中文原因与稳定原因码。
场景见[验证手册](systems/VERIFICATION.md#最小验证选择)。

Host 建立监听世界，客户端通过 Host 的可达地址和端口连接。
同机可使用回环地址，其他设备使用局域网地址；公网直连需要相应网络配置，不提供自动穿透。
同一验证中使用相同产品构建、引擎和内容；连接失败要读取前端反馈和当前输出日志。
不要把开发者全知实例当作普通远程拥有客户端。

排查远程卡顿时先在设置“界面”打开网络诊断与 FPS：区分 RTT/抖动/丢包/未收包时长和本机长帧，
遇到异常按 F9，并保留同一局客户端与房主日志。Tailscale 连接应另外检查
`tailscale ping <房主的 Tailscale 地址>` 的 direct/relay 结果：中继与直连是不同网络路径，
游戏日志不能单独证明哪条路径导致了延迟。不要通过放宽视野权限或关闭服务器校正掩盖丢包。
`RunDemoVisionCheck.ps1 -NetEmulation "-PktLag=650 -PktLagVariance=30 -PktLoss=2" -RenderClient1`
可在回环上验证高延迟、丢包、视野清除/恢复与真实拥有客户端镜头，保存 Host 与 Client1 的渲染截图；
它不替代跨机器或 Tailscale 的链路实测，也不表示游戏能消除链路本身的 1300 ms RTT。

### DEV 测试模式

Host 用 `OpenStandalone.ps1 -DevTest`（打包版 `OpenPackaged.ps1 -DevTest`，或直接给可执行文件加 `-LDDevGrowthFixture`）启动，
这个房间就是 DEV 测试会话；客户端无需开关。房间页与对局顶部横幅对所有成员显示“测试模式”
（热重载时另标“内容热重载”）。对局中点底部 HUD 的“测试”按钮打开测试面板，不用记命令：

- 资源与重复试验：输入数量“加流量”（经验）；“补满生命与法力”、重置冷却、重置到开局；
- 测试属性：十一项属性各一个输入框，旁边显示服务器提交后的最终值与来源（含“测试”这一项）；清除全部测试属性；
- 技能与属性卡：运行目录里全部技能与属性卡，显示已有等级或张数，“学习”作为当前这次选择的结果；Q/W/E 都满时另有“替换 Q/W/E”；
- 整局（只有房主看得到）：刷新已清空营地、提前开圈。

面板只把输入换成同一组 DEV 测试命令，结果由服务器按下表规则提交；数量或数值格式不对时在本地拒绝、不发送，
面板顶部显示最近一次的结果或拒绝原因。也可以按 `~` 打开控制台使用同样的命令：

| 命令 | 作用 |
| --- | --- |
| `ld.Dev.XP <数量>` | 给自己加经验，升级与机会按正常奖励结算 |
| `ld.Dev.Refill` | 活着时补满生命与法力 |
| `ld.Dev.ResetCooldowns` | 自己的技能与 D 冲刺冷却全部就绪 |
| `ld.Dev.List` | 列出运行目录里全部技能与属性卡的 id |
| `ld.Dev.Learn <id> [Q\|W\|E]` | 把任意合法的技能或属性卡当作当前这次三选一的结果；Q/W/E 都满时用 Q、W 或 E 确认替换 |
| `ld.Dev.Attr <属性> <数值>` | 在自己的构筑之上叠加一层测试属性（`MaxHP`、`MaxMP`、`AD`、`AP`、`Armor`、`MR`、`Haste` 为点数，`Range` 为整厘米，`AttackSpeed`、`MoveSpeed` 为基础值的百分比，`Crit` 为百分点；最多两位小数，0 去掉该项） |
| `ld.Dev.ClearAttr` | 去掉自己的全部测试属性，构筑本身不变 |
| `ld.Dev.RefreshCamps` | 只限房主：所有已清空、正在等刷新的营地在下一个边界按正常刷新重新生成，存活营地不动 |
| `ld.Dev.OpenCapture` | 只限房主、只在前三段定时野外：跳过剩余的野外段与休息，本边界直接进入最终野外并开放旗杆，喷漆、守护时长和全部规则不变 |
| `ld.Dev.Reset` | 自己回到开局：1 级、0 经验、没有技能、属性卡和测试属性，重新给一组开局三选一，冷却全部就绪，活着时补满 |

学习仍受真实规则约束：要有一次未用的选择机会（没有时先用 `ld.Dev.XP` 升级）、等级上限、Q/W/E 三槽与选择数上限。
测试属性在属性视图的悬停来源里单独显示为“测试”，跨死亡保留到本局结束，
最终值超出目录上下限时整条命令被拒绝而不是截断。刷新营地与提前开圈改变整局，只接受房主自己的连接，
其他客户端发送时回 `NotAuthority`；其余命令只作用于发送者自己，经服务器在 Transactions 阶段提交，
与正常奖励和选择共用同一提交路径；
没有开关的 Host、Shipping 构建、旧局或旧生命的请求都会被明确拒绝。
测试会话的结果是夹具证据，不能当作无作弊完整对局的验收。实现与限制见[成长事务](systems/GROWTH_TRANSACTIONS.md)。

## 试玩问题排查

试玩期玩完一局后，把日志交给 AI 复盘和排查。日志只在已提交的事件和每秒/每 10 秒的采样上写行，没有逐帧日志。
2026-11-06 的 AI 满员场景实测：对局中房主日志约 150 KiB/分钟（每秒心跳占七成，命中随交战增多），20 分钟一局约 3–5 MiB；
客户端日志约 16 KiB/分钟。

**日志在哪里**（时间戳为 UTC，如 `[2026.11.06-03.15.10:419][帧号]`）：

| 运行方式 | 日志 |
| --- | --- |
| 每日试玩包 / `OpenPackaged.ps1` | `<解压目录>\Archive\Windows\Lockdown\Saved\Logs\Lockdown.log` 是本次运行，更早的运行改名为 `Lockdown-backup-<时间>.log` |
| `OpenStandalone.ps1` / 编辑器 | 工作区 `Saved\Logs\Lockdown.log` |
| `RunDemo*Check.ps1` | 输出目录里的 `Host.log`、`Client1.log`… |

每个玩家交自己的日志就能复盘自己的视角；要查“实际为什么”（谁打的、打了多少、为什么没给经验），再要同一局房主的日志。
房主日志按 #134 的决定记录完整权威信息：开局头（产品版本、源码提交、内容摘要、网络地址）与名单、每次命中、
每名参赛者每秒一行心跳、死亡的凶手/致命来源/助攻/无奖励原因、经验与升级、三选一出卡与选择、
每 10 秒每个客户端连接的网络质量和对局结束汇总。客户端日志只有该玩家本来就能看到的信息。
各标签与字段的契约见[架构与网络](systems/ARCHITECTURE_AND_NETWORKING.md#运行日志与试玩记录)。

**标记键**：对局中遇到奇怪的事立刻按 F9（设置里可改键），本人日志与房主日志都记下这一刻（序号、权威时间、本人状态），
HUD 提示“已标记 #N”；死亡等待中也能按。抱怨时说“第 N 个标记附近”。

**整理脚本**：

```powershell
# 房主与客户端日志按 UTC 合并；label=路径 给每份日志起名
python -B .\Scripts\SummarizePlaytestLog.py 房主=<房主的 Lockdown.log> 我=<自己的 Lockdown.log> --out Artifacts\Playtest.md
# 只有自己的客户端日志也能整理自己的视角
python -B .\Scripts\SummarizePlaytestLog.py <自己的 Lockdown.log>
# 场景输出目录下的全部 *.log；--check-client 核对客户端日志里没有 Host 专有的权威行
python -B .\Scripts\SummarizePlaytestLog.py <RunDemo 输出目录> --check-client
```

脚本按局（match generation）切分，把参赛者 GUID 换成名字（同名玩家附 ID 末四位），每局输出：开局头与名单、结果与每人终局构筑、
每次死亡及死前 10 秒的命中（只有客户端日志时为公开击杀通知里的每次死亡，本人死亡另列死前的本人受伤、受控与所见施法；
`--death-window` 可改）、经济时间线、每个标记前 30 秒到后 10 秒的事件与标记前最近的状态行（`--mark-before`/`--mark-after`；
AI 决策与订单簿记默认只计数，`--mark-all` 全部列出）、性能与网络汇总、警告与错误。
测试：`python -B -m unittest discover -s .\Scripts -p TestSummarizePlaytestLog.py`。

**开关**：

- `-LogCmds="LogLDNav Verbose"` 打开某一分类的 Verbose 行；`-LogCmds="LogLDTrace off"` 关掉每秒心跳和本人状态行（连采样也跳过）。
  `RunDemoOpponentCheck.ps1 -LogCmds '<同样的值>'` 把它传给每个角色。
- 需要看清卡在哪一帧时，再按需开启 Unreal Insights：给可执行文件加 `-trace=default,net -statnamedevents -tracefile=<路径>.utrace`，
  用引擎的 `Engine\Binaries\Win64\UnrealInsights.exe` 打开。追踪文件很大且有开销，只在复现卡顿时开，常规试玩不开。
- 帧率预算（[内容与数值 §8.1](demo/CONTENT_AND_BALANCE.md)）用 `.\Scripts\RunPerfBudget.ps1 -Match` 量：真实 Demo 对局（Listen Host + 3 AI，发育期）里，
  先在每档画质下带人群预热至少 4 秒，再依次测空场与满员人群；`PerfBudget.json` 记录预热和测量结果并按预算判定。
  加 `-PackageRoot <打包目录>` 测打包版（结论以它为准），
  不加时是编辑器 `-game`（含编辑器开销、约 ±15% 波动，只用于定位）。不加 `-Match` 测 Lobby 场地。
  `-Trace` 额外录 Unreal Insights CPU 追踪 `PerfBudget.utrace`；`-ExtraArguments '-ExecCmds="<cvar> <值>"'` 做单项 A/B。
  无界面导出计时表：`Start-Process -Wait UnrealInsights.exe -ArgumentList '-OpenTraceFile="<utrace>" -ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimerStatistics <csv> -threads=GameThread" -AutoQuit -NoUI'`。
  游戏线程上权威阶段（`LDPhase_*`）、视野发布、HUD 读模型等已有 `LD*` 命名的追踪区段。

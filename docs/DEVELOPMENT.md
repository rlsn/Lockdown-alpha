# 开发指南

产品目标见 [App Spec](APP_SPEC.md)，共同开发约束见 [AGENTS](../AGENTS.md)。
当前仓库实现了该 Demo 的完整流程；验收结果、未运行与未实现项见 [实施就绪评估](demo/READINESS.md)。
模块边界与跨模块契约见 [实施计划](demo/IMPLEMENTATION_PLAN.md) 和 [共享契约](demo/SHARED_CONTRACTS.md)。
默认入口只有前端和唯一 Demo 世界，没有第二套产品模式。

## 环境与首次启动

使用 PowerShell 7、Python 3、Git LFS、Unreal **5.8.4** 及兼容的 MSVC/Windows SDK。
精确引擎发行基线在 `Config\PlatformLock.json`；Preflight 按该引擎的规则验证工具链。
需要 .NET Framework SDK，只有现代 .NET runtime 或 targeting pack 不够。

在当前工作区根目录执行：

```powershell
git lfs install
git lfs pull
$env:LD_UE_ROOT = '<UE 5.8.4 安装目录>'
.\Scripts\Preflight.ps1
.\Scripts\BuildEditor.ps1
.\Scripts\BuildGame.ps1
.\Scripts\OpenStandalone.ps1
```

脚本也接受 `-EngineRoot`。根目录按脚本所在工作区解析，不依赖某台电脑的固定项目路径。
引擎查找依次使用参数、`LD_UE_ROOT`、忽略的 `Config\Local\Environment.json`
及项目引擎关联；未注册的安装应显式指定，不修改共享项目配置为本机路径。
每个新 shell 都需要自己的环境变量。

原生模块缺失时先构建，修改反射类时关闭相关编辑器。
`OpenStandalone.ps1` 当前可打开前端；`-HostRoom` 只选择 `L_Demo`，
不会绕过已批准数据与原生合同检查。旧 Profile/NavigationCheckpoint 参数已移除。
`BootstrapAssets.ps1` 默认只做 Validate，先执行 Demo compiler `--check`；
缺批准输入、世界载荷/contract 或原生世界资产时失败。Generate 创建缺失的唯一 Demo 目录/世界资产，
Regenerate 显式更新现有生成资产；两者均不生成旧地图或旧目录。
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

### 多人共用一台机器

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

## 按任务读取系统手册

| 工作 | 技术说明 |
| --- | --- |
| 模块、Lobby、IP 联机、生命代次、回执 | [架构与网络](systems/ARCHITECTURE_AND_NETWORKING.md) |
| 点击移动、预测、取消、输入层、镜头 | [移动、输入与镜头](systems/MOVEMENT_INPUT_AND_CAMERA.md) |
| 伤害/状态、时序、效果来源、构筑 | [战斗与成长](systems/COMBAT_AND_GROWTH.md) |
| 实例、报价、购买、去重、原子事务 | [库存与事务](systems/INVENTORY_AND_TRANSACTIONS.md) |
| 战争迷雾、观测句柄、HUD 与隐私 | [视野与 UI](systems/VISION_AND_UI.md) |
| 地形、碰撞、动态对象、SourceData、资产 | [世界与数据管线](systems/WORLD_AND_DATA_PIPELINE.md) |

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
| `Content\Lockdown\Generated` | 受生成器管理的原生资产 |
| `Content\Lockdown\Audio` | `ImportDemoAudio.ps1` 由 `SourceData\Audio` 清单导入的音效目录与声音资产 |
| `Content\Lockdown\Art` | `ImportDemoArt.ps1` 由 `SourceData\Art` 清单导入的身体、动画、装饰道具与美术目录；`Art\UI` 是 `-Mode ImportBackgrounds` 由 `SourceData\UI\ui_images.json`（项目方提供的标题与房间背景图，不属于 CC0 美术包）导入的 UI 纹理 |
| `Config` | 项目、平台与布局配置；本机设置放忽略的 `Config\Local` |
| `Scripts` | 构建、生成、运行和当前结果校验 |
| `docs` | 当前产品规则与组件技术说明，不存构建输入或每次运行档案 |
| `Artifacts` | 本机运行输出，Git 忽略 |

## 数据修改

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

产品版本以 `Config\DefaultGame.ini` 的 `ProjectVersion` 为准。
它不同于 `Lockdown.uproject` 的 `FileVersion`/`EngineAssociation`，也不同于协议、目录与枚举版本。
`PackageWin64.ps1` 从同一设置读取产品版本。

## 当前工程运行与联机

`OpenStandalone.ps1`、编辑器启动配置和包内默认入口都打开 `L_Frontend`。
Host 仅请求 `/Game/Lockdown/Generated/Maps/L_Demo?listen`；
`LDDemoEntry` 在本地旅行前及原生 GameMode 内拒绝旧 profile、旧地图和缺失/不匹配的 Demo 载荷。
Join 还携带独立 wire 版本、产品版本和根 manifest 摘要；连接中不表示准入已成功。
原生 GameMode 不再通过旧 profile 创建战斗、经济、人口、旧目标或 probe 服务。

GameMode 提供三真人容量、Host 权限、0–3 AI 名额配置、结构名单版本与有界会话回执。
Ready 变化不增加结构版本，成员/AI 名额变化清除真人 Ready；开局时每个 AI 名额由对手提供者生成一个服务器
`ALDOpponentController`。只有全部正常开局提供者都已注册时才能开局，缺任一提供者时明确拒绝。
玩家名称经 `?LDName=` 传入并由服务器按设置中的名称规则复核；加入被拒时客户端显示中文原因与稳定原因码。
当前场景见下文“最小验证选择”。

Host 建立监听世界，客户端通过 Host 的可达地址和端口连接。
同机可使用回环地址，其他设备使用局域网地址；公网直连需要相应网络配置，不提供自动穿透。
同一验证中使用相同产品构建、引擎和内容；连接失败要读取前端反馈和当前输出日志。
不要把开发者全知实例当作普通远程拥有客户端。

### DEV 测试模式

Host 用 `OpenStandalone.ps1 -DevTest`（打包版 `OpenPackaged.ps1 -DevTest`，或直接给可执行文件加 `-LDDevGrowthFixture`）启动，
这个房间就是 DEV 测试会话；客户端无需开关。房间页与对局顶部横幅对所有成员显示“测试模式”
（热重载时另标“内容热重载”）。对局中点底部 HUD 的“测试”按钮打开测试面板，不用记命令：

- 资源与重复试验：输入数量“加流量”（经验）、“加米”（金币）；“补满精神状态与电量”、重置冷却、重置到开局；
- 测试属性：十一项属性各一个输入框，旁边显示服务器提交后的最终值与来源（含“测试”这一项）；清除全部测试属性；
- 装备：两个槽位当前装备与出售；运行目录里全部装备按目录价格购买；
- 技能与属性卡：运行目录里全部技能与属性卡，显示已有等级或张数，“学习”作为当前这次选择的结果；Q/W 都满时另有“替换 Q/W”；
- 整局（只有房主看得到）：刷新已清空营地、提前开圈。

面板只把输入换成同一组 DEV 测试命令，结果由服务器按下表规则提交；数量或数值格式不对时在本地拒绝、不发送，
面板顶部显示最近一次的结果或拒绝原因。也可以按 `~` 打开控制台使用同样的命令：

| 命令 | 作用 |
| --- | --- |
| `ld.Dev.XP <数量>` | 给自己加经验，升级与机会按正常奖励结算 |
| `ld.Dev.Gold <数量>` | 给自己加金币（米） |
| `ld.Dev.Refill` | 活着时补满生命与法力 |
| `ld.Dev.ResetCooldowns` | 自己的技能与装备冷却全部就绪 |
| `ld.Dev.List` | 列出运行目录里全部技能、属性卡与装备的 id |
| `ld.Dev.Learn <id> [Q\|W]` | 把任意合法的技能或属性卡当作当前这次三选一的结果；Q/W 都满时用 Q 或 W 确认替换 |
| `ld.Dev.Buy <id>` / `ld.Dev.Sell <1\|2>` | 不去商店，按目录价格买入到空槽或卖出该槽装备 |
| `ld.Dev.Attr <属性> <数值>` | 在自己的构筑之上叠加一层测试属性（`MaxHP`、`MaxMP`、`AD`、`AP`、`Armor`、`MR`、`Haste` 为点数，`Range` 为整厘米，`AttackSpeed`、`MoveSpeed` 为基础值的百分比，`Crit` 为百分点；最多两位小数，0 去掉该项） |
| `ld.Dev.ClearAttr` | 去掉自己的全部测试属性，构筑本身不变 |
| `ld.Dev.RefreshCamps` | 只限房主：所有已清空、正在等刷新的营地在下一个边界按正常刷新重新生成，存活营地不动 |
| `ld.Dev.OpenCapture` | 只限房主、只在发育期：旗杆在本边界提前开放，喷漆、守护时长和全部规则不变 |
| `ld.Dev.Reset` | 自己回到开局：1 级、0 经验、没有技能、属性卡、装备和测试属性，开局金币，重新给一组开局三选一，冷却全部就绪，活着时补满 |

学习仍受真实规则约束：要有一次未用的选择机会（没有时先用 `ld.Dev.XP` 升级）、等级上限、Q/W 两槽与选择数上限；
买卖照样付钱和退钱、两槽满时拒绝。测试属性在属性面板里单独显示为“测试”，跨死亡保留到本局结束，
最终值超出目录上下限时整条命令被拒绝而不是截断。刷新营地与提前开圈改变整局，只接受房主自己的连接，
其他客户端发送时回 `NotAuthority`；其余命令只作用于发送者自己，经服务器在 Transactions 阶段提交，
与正常奖励、选择和买卖共用同一提交路径；
没有开关的 Host、Shipping 构建、旧局或旧生命的请求都会被明确拒绝。
测试会话的结果是夹具证据，不能当作无作弊完整对局的验收。实现与限制见[库存与事务](systems/INVENTORY_AND_TRANSACTIONS.md)。

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

每个玩家交自己的日志就能复盘自己的视角；要查“实际为什么”（谁打的、打了多少、为什么没给钱），再要同一局房主的日志。
房主日志按 #134 的决定记录完整权威信息：开局头（产品版本、源码提交、内容摘要、网络地址）与名单、每次命中、
每名参赛者每秒一行心跳、死亡的凶手/致命来源/助攻/无奖励原因、经验金币与升级、三选一出卡与选择、买卖、
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

## 最小验证选择

| 改动 | 起点 |
| --- | --- |
| 文字与导航 | 链接、代码入口与当前事实，`git diff --check` |
| SourceData / 编译器 | 对应 Sync `--check`、`Test*Import.py`，核对载荷与 contract |
| Demo 世界 / 资产 | `BootstrapAssets.ps1 -Mode Generate/Regenerate/RefreshDefinitions/RegenerateFrontend/Validate`（`RegenerateFrontend` 只重建空白的 `L_Frontend`；每次都拒绝前端地图里出现世界设置以外的任何 Actor）；`Lockdown.Rules.DemoWorld` 的精确有符号几何、typed 投影与资产往返；真实拥有连接和完整移动/观测另行验收 |
| Demo 世界 / 拥有连接 | `RunDemoWorldRoomCheck.ps1`；实际 Listen Host 加两个拥有客户端校验同一 Lobby/内容根/原生地面与导航，隔离 UserDir；没有 AI 或玩法身体，不代替完整对局 |
| Demo 核心 / 拥有身体 | `RunDemoCoreCheck.ps1`；DEV-only 三拥有身体、地面/Stop/重放与原生 CMC 执行夹具，调用同一核心提交路径且不改变普通 Start 门禁；营地成员只计数不受检；不代替完整战斗、随机出生、视野或 CT06 |
| 喷漆守护 / 终局 / 新局 | `Lockdown.Rules.DemoCapture`（含 `.SprayAndGuard`、`.SprayInterrupt`、`.GraffitiCleared`）与 `Lockdown.Rules.DemoCore.CaptureCompletesInsideStep`；`RunDemoCaptureCheck.ps1` 在全部提供者接入、普通 Start 门禁打开后，以实际 Listen Host 加两个拥有客户端（各自以 `?LDName=` 传入已知中文名称，含一个带内部空格）经正常大厅开局，覆盖名单与结算行显示所选名称（而非在线子系统昵称）、Client1 喷漆而 Client2 进区争夺（暂停）后离开（续计）直到喷上涂鸦、主人走出喷漆区后守护照常运行、Client2 独自改漆使守护暂停并在 3.5s 前走出（改漆进度清零、守护从暂停处续计）、守护满时长以 `GuardSucceeded` 锁定唯一结果与写闸关闭、各端世界中喷漆区边界与旗杆涂鸦板的出现/随公开状态变色与染色/回 Lobby 移除、Host 同世界回到新一代 Lobby，并正常开出第二局、核对其干净开局（开局成长值、空构筑、无状态与冷却、营地与旗杆涂鸦复位、新局边界、旧代请求拒绝）；runner 要求每个角色恰好报告其稳定检查 id（Host 28 项、每个客户端 22 项），缺失、多出或重复都失败。第一局不改变成长和冷却，因此它不证明脏状态被复位；不代替 3+3、AI、战斗死亡与硬控打断（含喷漆者死亡、被控清零和主人死亡清除涂鸦，由规则测试覆盖）或网络异常 |
| Demo 纯值战斗 / 时间许可 | `Lockdown.Rules.DemoCombatEvaluation`、`Lockdown.Rules.DemoCore`；伤害/时长使用当前 producer 的 `approved_base`，合成 `base` 保留供导入拒绝和变异测试；纯值成功不等于实际命中提交 |
| 营地纯决策 | `Lockdown.Rules.CampDecision` 使用同一 `approved_base` 内容夹具，覆盖授权读取完成、身份/世界/时间拒绝、只在已攻击本营的参赛者中稳定选敌、从当前脚点量的感知半径、只约束成员自身的活动范围和成员回位；不创建 Actor 或执行移动/攻击/重置，不代替 N03 真实接线 |
| 营地 / 拥有连接 | `Lockdown.Rules.DemoCamp` 覆盖名册、每生命死亡记账与清营计时、整组反击来源、回营重置键和观察事实；`RunDemoCampCheck.ps1` 在全部提供者接入、普通 Start 门禁打开后，以实际 Listen Host 加两个拥有客户端经正常大厅开局，覆盖五营落点、不先手、整组反击、最后一击每生命一次收益、清空后按内容计时整组刷新、拴绳回营的公开提示与不发奖重置；唯一渲染的 Client2 另存 `CampGuardianReturning.png`（回工位的高档野怪、名字与回工位标记）和 `CampLowPair.png`（刷新后的低档两只与名字）供表现审阅，不做像素比对。相邻高低营锚点相距约 3057cm，超出固定镜头画面，两档分两张；不代替 3+3、AI、SpawnBlocked/动态占位或网络异常 |
| 对手 AI / 拥有连接 | `Lockdown.Rules.DemoOpponentPolicy` 覆盖纯策略、帧投影、请求构造与回执结算；`RunDemoOpponentCheck.ps1` 在普通 Start 门禁打开后，以实际 Listen Host、两个拥有客户端和三个 AI 的满员房间经正常大厅开局，真人不参与。它核对每个 AI 独立绑定且不补位、自主移动，获胜 AI 升级并确认选择、至少一个 AI 到店购买、死亡期间不发身体请求、死亡后按时复活并继续行动、每 30 秒的身体请求与被拒订单有界、每个 AI 开局十秒内处理开局三选一（D10）、旗杆开放前取得经验（发育期至少一半时间死亡或回血恢复的除外）、争夺暂停下喷漆进度持有者留在喷漆区迎战且区内争夺者不离开去刷野或购物，最终由 AI 以 `GuardSucceeded` 守护成功获胜且它是公开涂鸦主人，回到新 Lobby 后 AI 全部退役；可加 `-PackageRoot` 对打包版运行。每个角色在开局一分钟时按一次试玩标记，供 `SummarizePlaytestLog.py` 核对标记前后的事件（不是检查项）。本局没有 AI 死亡、复活或相应争夺时，相应项报告为未覆盖（NotExercised），单独列出，不计为通过；渲染的 Host 另存供人工审阅的截图（不是检查）：`HostMatch*.png`，以及 #112 的 `HostChoices.png`（开局三选一与重抽按钮）、`HostOpponentCard.png`（可见 AI 的 Q/W 卡）、`HostKillAssist.png`（带助攻者的击杀播报，本局出现时）、`HostSpray.png`/`HostGuard.png`（镜头移到旗杆的喷漆与守护 HUD、涂鸦板）、`HostResults.png`（含助攻列的结算）。“每个 AI 都到店购买”（开局的米已够买最便宜的装备）与“至少一个 AI 实际完成 D 冲刺”（D19；报告逐个 AI 记录 `dashes` 与 `dashes_landed`）只作报告项：未出现时记为 NotExercised，不卡门槛，待 [#117](https://github.com/rlsn/Lockdown-alpha/issues/117) 多局验证后再定是否升为硬门槛；不代替隐藏状态非干扰、字段白名单、真人离开或网络异常 |
| Demo 属性 / 原生记录 | `Lockdown.Rules.DemoAttributeProjection` 覆盖批准属性、双装备、条件被动和资源比例；`Lockdown.Rules.DemoMotionProbe` 用隔离原生物理验证时间分数、回滚、接地转换和缺失记录拒绝；真实拥有身体夹具显式审计保留前缀，不代替最终提供者 Fence |
| 原生代码 / 反射 | Editor/Game 构建、Bootstrap Validate、相关原生规则 |
| 入口 / 房间 | `TestDemoEntry.ps1`、`Lockdown.Rules.DemoEntryContract/DemoInputByteIntegrity/DemoMatchIdentity/DemoSessionReceipts/DemoLobbyAuthority` |
| 原生旧入口拒绝 | `RunDemoEntryCheck.ps1`，要求真实 URL 被拒绝后回前端，累计 Pawn/旧模式/服务/子系统激活为零 |
| 前端设置实际渲染 | `RunDemoFrontendCheck.ps1`，新 `UserDir` 下的 windowed RHI、合成 UMG 点击、100/125% 图像、文本偏好保存/回读、设置页无窗口模式/分辨率、未应用显示草稿再按返回即放弃、VSync 应用保存回读；不修改 OS 显示模式 |
| 输入 / 导航 / HUD | `Lockdown.Rules.ControlMappings/IssuedCommandLiveness`、`Lockdown.Rules.LocalSettings*/LocalDisplay*/LocalControlRegistryIsolation`、`Lockdown.Rules.HUD*`；拥有连接下的订单与输入路由见 `RunDemoCombatCheck.ps1`，HUD 观测投影见 `RunDemoVisionCheck.ps1` |
| 音效 / 音量 | `ImportDemoAudio.ps1 -Mode Validate`（Bootstrap Validate 也执行）；`Lockdown.Rules.Audio*` 与 `LocalSettingsMasterVolume`。`-nosound` 场景只证明无错误日志，实际听感、设备输出与音量需在有声卡的可视运行中确认 |
| 美术 / 装饰 | `ImportDemoArt.ps1 -Mode Validate`（Bootstrap Validate 也执行，含标题/房间背景图的清单、哈希、尺寸与 UI 纹理设置）；`Lockdown.Rules.ArtCatalog/ArtCatalogAsset/ArtDecorations`；`CaptureDemoArt.ps1` 在 Demo 地图大厅渲染全部身体、各动画姿势与装饰后的场地截图，并核对装饰无碰撞、不挡视线追踪、不改 navmesh。不代替身体接线、战斗或联机验收 |
| 战斗 / 观测复制 | `RunDemoCombatCheck.ps1` 为 DEV 拥有连接的战斗与订单场景：经普通大厅 Start，收缩边外脚点作为点选地面被拒、作为追击终点可规划，已接受行走上打开设置超过订单租约（订单与心跳继续、关闭后不重放）、远程攻击移动与 S、右键击杀与击杀播报、死亡静态镜头随小地图平移、十点随机复活的精确保护及空 Q 解除保护；随后 Host 用 DEV 成长夹具（仅 Host 带 `-LDDevGrowthFixture`，证据目录写 `DevFixture.json`）预置选项并发放经验，双方经普通选择学会技能，实战覆盖扒裤子定身被观察者看到、我没事我很好的护盾与净化、Client1 的天外飞瓶投射物命中 Host 和增程卡的普攻命中；Host 离屏渲染主视图，在到达对决点、对决点旁地面右键的落点标记、本人受击、右键击杀前的悬停环、右键后的攻击标记、定身、护盾、天外飞瓶飞行中段（须已画出粉尘拖尾）和本人挥击时各存一张截图（`HostSees-DuelSite/MoveMarker/OwnHit/Hover/AttackMarker/Root/Shield/ChalkFlight/OwnStrike.png`，地面与击杀右键经 DEV 指针走普通 `ClickMove`）供人工审画面，不做像素比对；双方报告的 `feel` 与成功行给出操作手感实测（下单到起动及其间帧数与最长帧、到全速、S 到停住与滑行距离、S 到 Stopped 回执；首次下单等本进程开局帧稳定后发出），只报告不设门槛（不含装备主动/营地/AI/终局）；`RunDemoVisionCheck.ps1`（可加 `-PackageRoot`）是观测与隐身场景：参赛渲染的 Listen Host 与两个拥有客户端经普通大厅开局，先核对拓扑、受限注册表、Host 只经自身投影呈现他人、单连接丢帧后的超时清除与断开后不再呈现（检查组 `#1/#9/#15/#15H/#23/#28`），随后是路人甲 T 恤隐身段（三个角色都报 `#ST`，Host 另报 `#STH`）：Host 的 DEV 成长夹具（仅 Host 带 `-LDDevGrowthFixture`，证据目录写 `DevFixture.json`）在大厅为两个客户端预置含你瞅啥的开局三选一、开局后补足每人买路人甲 T 恤所缺的米，三人各经自己的连接到商店买路人甲 T 恤，站成边长 600 cm 的三角后依次四轮各隐身一人：Client1 在 Client2 的攻击锁定下隐身、走近后普攻现身；Client2 原地隐身后施放你瞅啥现身；Host 隐身走近后被 Client1 打中现身；Client1 冷却后再隐身、走近后被 Client2 嘲讽现身。每个观察者经 relay 的 DEV 钩子逐帧记录收到的每一帧，按 Host 从权威记录的隐身结束时刻（仅作测试基准）划分：隐身期间的帧都不得在揭示半径（250 cm，另加 40 cm 采样余量）外呈现该单位，不得再出现、解析或代理旧句柄（小地图点与飞字只读这一帧），受限客户端不持有对方身体，Listen Host 在每个游戏 tick 都不绘制其原生身体；Client2 先取得已激活的攻击订单，隐身后以 TargetUnavailable 取消，直到隐身结束都不恢复；近处观察者在揭示半径内重新发现时句柄是新的且看不到隐身状态；远处观察者在隐身本应到期前以新句柄重新看到它，隐身者自己的视图显示隐身提前结束。不含 AI 使用隐身/嘲讽（#79）、战斗数值或完整对局验收 |
| 库存 / 账本 | `RunTests.ps1` 中的 `DemoGrowth*` 规则；`python -B -m unittest discover -s .\Scripts -p TestEconomyEvidence.py`；原生账本证据经 `CheckEconomyEvidence.py` 独立复算；`RunDemoGrowthCheck.ps1` 为 DEV 拥有连接的击杀奖励/选择/买卖/装备主动冷却债/账本场景，Host 带 DEV 成长夹具预置买方 Offer，覆盖 Q/W 满槽替换与重学沿用原冷却截止、死亡等待中确认选择不复活不治疗，Listen Host 作为击杀者（击杀买方与低档营地野怪）在本人视图、HUD 与渲染的“+X 米 / +X 经验”飘字中看到精确奖励，并结束本局后开第二局核对新局隔离（不含 AI/喷漆守护）；买方客户端离屏渲染，买满两件后另存 `Client1ShopUpgrade.png`（商店两件装备的出售与进阶入口，供人工审阅） |
| 网络异常 / 拥有连接 | `RunDemoSessionFaultCheck.ps1`（可加 `-PackageRoot` 对打包版运行）分七段：Leave 中一名客户端经局内菜单离开，其余两人继续，Host 结束后结果表记其离局且无击杀/死亡；HostLoss 结束 Host 进程，客户端回到主菜单并显示“会话中断”，不出现结果表；Join 对关闭端口和无法解析的地址显示“连接失败：原因”，仍停在前端且可再次加入；Refuse 用真实加入按钮依次遇到版本不兼容（DEV 参数让该客户端声明另一产品版本）、三真人满员和对局已开始，各自显示中文原因与原因码，始终未进入房间；Race 中一次加入已通过 PreLogin，Host 在它载入房间时开局，最终准入检查拒绝它，客户端显示“对局已开始”而不是会话中断；HostLeave 中 Host 经局内菜单离开，房间在 Host 换图前向成员发出关闭，客户端回到主菜单并显示“房主已离开”，不显示会话中断或结果表，Host 自己的主菜单也不显示失败；HostQuit 中 Host 进程在客户端仍连接时正常退出（与直接关窗口相同，引擎走 PreExit 拆除世界而不是换图），Host 须以 0 退出且日志无 `LD_DEMO_AUTHORITY_FAULT`，客户端回到主菜单并显示“会话中断”，不出现结果表。全部在同机回环上运行，不代表跨机器局域网或公网直连 |
| 全新检出 | `VerifyCleanCheckout.ps1`；它验证本地 LFS 缓存检出，不证明远程分发 |

### 合并前与里程碑检查

每个 PR 至少要跑过 `BuildEditor.ps1` 和受影响的规则测试，并写明新增的测试名。
改动较大的 PR，以及每个里程碑交付前，跑一轮下面的检查：

```powershell
.\Scripts\BuildEditor.ps1 -FullUnity
.\Scripts\BuildGame.ps1 -FullUnity
.\Scripts\RunTests.ps1
python -B -m unittest discover -s .\Scripts -p "Test*.py"
.\Scripts\BootstrapAssets.ps1
.\Scripts\RunDemoVisionCheck.ps1     # 截图：HUD、身份色、装饰与环境
.\Scripts\RunDemoOpponentCheck.ps1   # 3 真人 + 3 AI 满员对局，多个模块的检查合并在一局里
```

AI 满局遥测不是验收：`RunDemoOpponentCheck.ps1 -Telemetry -MatchCount <1-50>` 只启动一个无渲染的 Listen Host，
它作为唯一真人留在房间里不动，与三个 AI 经正常大厅连打 N 局（每局各有 `-TimeoutSeconds`，局间回 Lobby）。
每局写一份 `MatchNN.json`：结束原因（`GuardSucceeded`/`HostEnded`/`None`）与胜者、旗杆开放与首杀（以及首个死亡）时刻、全部击杀及其助攻者、
每次参赛者死亡的击杀者与助攻者各自所得金币/经验（取自 `ALDEconomyDirector::DevRecentRewards`，每条生命一次）、每名参赛者每秒的等级/经验/金币曲线、
买入与卖出（由装备实例的出现与消失得出，附目录价格）、喷漆尝试/续喷/被争夺暂停/失去/被打断/喷成次数、争夺、喷成、改漆与涂鸦因主人死亡或离局被清除的次数、
获胜涂鸦的守护用时（结果时刻减喷上时刻）（取自喷漆与守护归约的 DEV 计数 `FLDCaptureDevTally`，边界内的中间变化也计入），
以及 #112 §13 的成长记录：首个主动的获得时刻、终局技能与 Lv3 技能数、按等级应得与已用的重抽令牌、已做选择数、每次主动替换（时刻、等级、被替换技能与等级、新技能与等级，由构筑修订前后 Q/W 主动的消失得出）、
终局构筑的机制标签计数（每个已学技能和持有装备各计一次）；`SummarizeOpponentTelemetry.py` 汇总为 `Summary.json`
（胜负方式、各槽获胜数、时长与首杀分布、每分钟等级与金币、购买次数、喷漆/改漆/清除原因与守护用时、死亡与助攻奖励、
首个主动时刻、Lv3 数量分布、重抽使用率（已用/应得）、主动替换率（替换次数/选择次数）与替换时等级、获胜者的机制标签构成等）。这些数字只描述这几局，不作为平衡目标，也不判断 AI 行为对错。

其余场景（Core、Combat、Camp、Growth、Capture、SessionFault）放在里程碑或发布冻结后的完整验收里，在打包版上各跑一次。
改到某个场景覆盖的系统时，PR 里也要跑那个场景。
新增的原生测试要同步登记到 `CheckRuleReport.py`。如果某个模块的检查需要真实对局，优先补进满员场景，不另开新场景。

### 规则测试集合

`RunTests.ps1` 执行已有 `Lockdown.Rules` 套件，由 `CheckRuleReport.py` 核对明确的当前必需用例集合。
缺报告、失败、未完成、缺用例或重复结果都不能通过；新增或迁移原生测试时同步维护此集合，
并运行 `python -B -m unittest discover -s .\Scripts -p TestRuleReport.py` 核对源码注册与报告拒绝路径。
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
在独立 `UserDir` 下生成前端、设置 100/125% 和显示草稿丢弃图像。
旧场景/Profile/书店/准备参数已移除；图像存在不等于真实 OS 输入或完整对局验收。

## 打包与产物

```powershell
.\Scripts\PackageWin64.ps1
.\Scripts\OpenPackaged.ps1 -PackageRoot '<本次 Archive 目录>'
```

打包脚本先检查 Demo 源/载荷/contract，再验证 Bootstrap，仅选择前端和 Demo 两张图。
当前缺少完整玩法接线时不应打包；旧 cook/staging 依赖仍须随对应内容闭包迁移，
入口检查不代表最终包已无旧载荷。
成功输出记录独立的 Demo entry contract、产品版本、源输入与包文件摘要。
`OpenPackaged.ps1` 拒绝没有对应 manifest、包含旧 map set 或可执行文件摘要不符的包。
打包完成不代表运行、联机或新 Demo 验收已经通过。
保留的发布构建按 [AGENTS](../AGENTS.md) 的 known-working 规则管理；
运行日志按本次问题取用，不提交大量快照到 docs，也不自动删除别人的工作产物。

### PR 检查与每日试玩包

`.github/workflows/pr-check.yml` 在指向 `main` 的非草稿 PR 每次推送（以及转为 Ready for review）时运行，
同一 PR 的新推送会取消未完成的旧运行；只改 `docs/**` 或 `*.md` 的推送不触发。它分三个任务：
- `scope`（GitHub 托管）：`Scripts/CIChangeScope.py` 按 PR 合并提交相对 base 的改动选择门槛。
  改到 `Source`、`Config`、`SourceData`、`Content`、PowerShell 脚本或 `RunTests.ps1` 加载的 Python 脚本
  （及其在 `Scripts` 内的全部 import）时需要原生门槛；只改其他 `Scripts/*.py` 或工作流时不需要。
- `python`（GitHub 托管 Windows，每个 PR 都跑）：`Scripts/RunPythonTests.py` 以最多 4 个进程并行执行
  `Scripts` 下的 Python 测试（与 `unittest discover` 同一组测试和通过规则，带类级夹具的测试类整体留在一个进程里）。
  工作进程不是守护进程，测试可以再开进程：`ExportDemoNativeFixtures.py` 默认按 CPU 数并行编译夹具用例
  （`--jobs` 可调，输出字节与串行一致），`RunTests.ps1` 也走同一导出。
  不需要 Unreal 与 LFS 内容；本地也可直接运行 `python -B .\Scripts\RunPythonTests.py`。
- `native`（维护者本机的自托管 runner，仅在需要原生门槛且分支在本仓库内时）：增量 `BuildEditor.ps1` 与
  `RunTests.ps1`，失败时上传 Artifacts 证据。runner 关闭时它在 GitHub 上排队；fork PR 不在这台 runner 上运行。
它覆盖每个 PR 的最低门槛，不代替改到场景时应跑的 `RunDemo*Check`。

`.github/workflows/nightly-package.yml` 每天 03:00（北京时间）排队一次，也可在 Actions 页手动运行（只能选 `main`）。
它只在维护者本机的自托管 runner（标签 `lockdown-ue58`）上执行，每次运行发布一个试玩版本，
检出的是任务开始时 `main` 的最新提交（不是排队时的提交）：
- `main` 上没有新提交（已有 `vX.Y.Z` tag 包含 HEAD）时直接跳过；手动运行勾选 `force` 时仍发布下一个 patch。
- `Scripts/CIReleaseVersion.py` 选版本：`Config/DefaultGame.ini` 的 `ProjectVersion` 还没有 tag 且比最新 tag 新时
  直接使用（有人经 PR 改到新的 minor/major）；否则取两者较新者的 patch+1。
  需要改 ini 时，工作流在 `main` 最新提交之上本地提交 `chore: release version X`，打包这个提交。
  它不推送到 `main`（`main` ruleset 要求 PR，个人仓库也无法把 GitHub Actions 加入 bypass），
  只随 tag `vX` 推送；所以包内版本、清单与 tag 一致，`main` 上的 ini 保持基础版本，已发布的版本以 tag 为准。
- 增量 `BuildEditor.ps1`、`RunTests.ps1`、`PackageWin64.ps1`，核对清单的版本、提交与干净工作区，
  把 Archive、manifest、`Package.log` 与 `README.txt` 启动说明打成 `Lockdown-vX-win64.zip`，
  推送 tag `vX` 并发布为 Release `vX`；发布失败时撤回刚推送的 tag，重跑会再次发布。
  随后只保留最新两个 `vX.Y.Z` Release，更早的删除但保留 tag：tag 保存已发布的提交，也是下一个版本号的依据。
- 产品版本参与加入房间的校验，试玩时所有玩家需使用同一个 Release。
该包不跑任何 `RunDemo*Check` 场景，不能代替里程碑验收。

runner 不装成服务，只在手动打开期间接任务；关闭期间排队的运行在 GitHub 上最多等待 24 小时。
一台 runner 同时只跑一个任务，Start 在本机已有任何 runner 进程时拒绝启动，
所以这台机器上同一时间最多一个 CI 任务，其余在 GitHub 上排队。
所有任务共用 runner 目录下的同一个工作区。每个任务开始时 `Scripts/CIResetWorkspace.ps1` 清掉未跟踪文件、
上次的 Artifacts、`Saved` 下的日志、崩溃转储、自动保存与暂存包；`Binaries` 与 `Intermediate` 保留用于增量构建，
合计超过 12 GB 时整体删除并从头构建；所在盘可用空间不足 10 GB 时任务直接失败。
Start 会删除 runner `_diag` 中超过 14 天的日志。

```powershell
.\Scripts\CIRunner.ps1 Install -RunnerRoot <runner 目录>   # 每台机器一次，需要仓库管理员的 gh 登录
.\Scripts\CIRunner.ps1 Start    # 以 BelowNormal 优先级打开，构建进程继承该优先级
.\Scripts\CIRunner.ps1 Stop     # 立即关闭；正在跑的任务会被终止并记为失败
.\Scripts\CIRunner.ps1 Status
```

仓库是公开的，Install 与每次 Start 会把 `CIJobGuard.ps1` 复制进 runner 目录，作为任务开始钩子：
只接受本仓库的 `schedule`、`workflow_dispatch`，以及分支在本仓库内的 `pull_request`；
fork PR、`pull_request_target` 等其他任务在第一步之前就被拒绝。
仓库设置要求所有外部贡献者的 fork PR 工作流都需维护者批准。

## 文档维护与共同交付

玩法决定改 App Spec，组件接口/约束改对应系统手册，工作状态与讨论放 Issue/PR。
有效系统说明按内容保留、合并和校准；失效设计与重复档案删除，不以版本目录持续堆叠。
四份核心文档只是入口，不限制必要技术手册的数量。

PR 说明实际变化、复用边界、已运行/未运行的检查及限制，不复制整段会话。
共享历史不改写，二进制资产先分工。仓库内约束并不代表 GitHub Ruleset 已配置；
远端保护由维护者另行确认，不能仅凭这份文档宣称禁强推已在服务器强制生效。

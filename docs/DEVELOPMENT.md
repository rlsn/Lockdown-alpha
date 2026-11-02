# 开发指南

产品目标见 [App Spec](APP_SPEC.md)，共同开发约束见 [AGENTS](../AGENTS.md)。
当前仓库具备原生工程和可复用组件，但尚未完整实现该 Demo。
下面的命令运行当前工程，不是新玩法已经完成的保证。
目标 Demo 的开工门槛与依赖见 [实施就绪评估](demo/READINESS.md)、
[实施计划](demo/IMPLEMENTATION_PLAN.md) 和 [共享契约](demo/SHARED_CONTRACTS.md)。
默认入口已收敛为前端和唯一 Demo 世界；尚未迁移的组件不因此获准作为第二套产品模式运行。

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

依赖级复用、真实裁剪闭包和基础门槛见 [架构审计](demo/ARCHITECTURE.md)；
其源码依据与建议不替代 App Spec，也不代表删除、资产核验或游戏实现已经完成。

## 工程目录

| 目录 | 职责 |
| --- | --- |
| `Source\Lockdown\Public` / `Private` | 当前原生运行模块接口与实现 |
| `Source\LockdownEditor` | 原生导入、地图/资产生成与保存 |
| `SourceData` | 当前生成器消费的内容定义、schema、ID 映射与 CSV |
| `Content\Lockdown\Data` | 已生成并校验的运行载荷 |
| `Content\Lockdown\Generated` | 受生成器管理的原生资产 |
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

GameMode 已提供三真人容量、Host 权限、0–3 AI 名额配置、结构名单版本与有界会话回执。
Ready 变化不增加结构版本，成员/AI 名额变化清除真人 Ready。AI 名额不是已实现的对手控制器。
typed 内容/世界目录、精确资产校验、唯一地图与原生静态几何/导航已经实现；
当前仍缺完整实际玩法提供者、观测/运动时序及真实对局接线；
即使具备名单与准备状态，也不会通过旧服务开始一局。
保留组件的旧场景 runner 可能仍含待迁移选择器，原生入口会拒绝它们；
不能把这些 runner 当作当前可运行的 Demo 场景。

Host 建立监听世界，客户端通过 Host 的可达地址和端口连接。
同机可使用回环地址，其他设备使用局域网地址；公网直连需要相应网络配置，不提供自动穿透。
同一验证中使用相同产品构建、引擎和内容；连接失败要读取前端反馈和当前输出日志。
不要把开发者全知实例当作普通远程拥有客户端。

## 最小验证选择

| 改动 | 起点 |
| --- | --- |
| 文字与导航 | 链接、代码入口与当前事实，`git diff --check` |
| SourceData / 编译器 | 对应 Sync `--check`、`Test*Import.py`，核对载荷与 contract |
| Demo 世界 / 资产 | `BootstrapAssets.ps1 -Mode Generate/Regenerate/RefreshDefinitions/Validate`；`Lockdown.Rules.DemoWorld` 的精确有符号几何、typed 投影与资产往返；真实拥有连接和完整移动/观测另行验收 |
| Demo 世界 / 拥有连接 | `RunDemoWorldRoomCheck.ps1`；实际 Listen Host 加两个拥有客户端校验同一 Lobby/内容根/原生地面与导航，隔离 UserDir；没有 AI 或玩法身体，不代替完整对局 |
| Demo 核心 / 拥有身体 | `RunDemoCoreCheck.ps1`；DEV-only 三拥有身体、地面/Stop/重放与原生 CMC 执行夹具，调用同一核心提交路径且不改变普通 Start 门禁；营地成员只计数不受检；不代替完整战斗、随机出生、视野或 CT06 |
| 抢圈 / 终局 / 新局 | `Lockdown.Rules.DemoCapture`；`RunDemoCaptureCheck.ps1` 在全部提供者接入、普通 Start 门禁打开后，以实际 Listen Host 加两个拥有客户端经正常大厅开局，覆盖连续独占占领、一次争夺后重新计时、唯一结果与写闸关闭、Host 同世界回到新一代 Lobby，并正常开出第二局、核对其干净开局（开局成长值、空构筑、无状态与冷却、营地与抢圈复位、旧代请求拒绝）；runner 要求每个角色恰好报告其稳定检查 id（Host 21 项、每个客户端 15 项），缺失、多出或重复都失败。第一局不改变成长和冷却，因此它不证明脏状态被复位；不代替 3+3、AI、战斗死亡或网络异常 |
| Demo 纯值战斗 / 时间许可 | `Lockdown.Rules.DemoCombatEvaluation`、`Lockdown.Rules.DemoCore`；伤害/时长使用当前 producer 的 `approved_base`，合成 `base` 保留供导入拒绝和变异测试；纯值成功不等于实际命中提交 |
| 营地纯决策 | `Lockdown.Rules.CampDecision` 使用同一 `approved_base` 内容夹具，覆盖授权读取完成、身份/世界/时间拒绝、只在已攻击本营的参赛者中稳定选敌、从当前脚点量的感知半径、只约束成员自身的活动范围和成员回位；不创建 Actor 或执行移动/攻击/重置，不代替 N03 真实接线 |
| 营地 / 拥有连接 | `Lockdown.Rules.DemoCamp` 覆盖名册、每生命死亡记账与清营计时、整组反击来源、回营重置键和观察事实；`RunDemoCampCheck.ps1` 在全部提供者接入、普通 Start 门禁打开后，以实际 Listen Host 加两个拥有客户端经正常大厅开局，覆盖五营落点、不先手、整组反击、最后一击每生命一次收益、清空后按内容计时整组刷新、拴绳回营的公开提示与不发奖重置；不代替 3+3、AI、SpawnBlocked/动态占位或网络异常 |
| 对手 AI / 拥有连接 | `Lockdown.Rules.DemoOpponentPolicy` 覆盖纯策略、帧投影、请求构造与回执结算；`RunDemoOpponentCheck.ps1` 在普通 Start 门禁打开后，以实际 Listen Host、两个拥有客户端和三个 AI 的满员房间经正常大厅开局，真人不参与。它核对每个 AI 独立绑定且不补位、自主移动，获胜 AI 升级并确认选择、至少一个 AI 到店购买、死亡期间不发身体请求、死亡后按时复活并继续行动、每 30 秒的身体请求与被拒订单有界，最终由 AI 按占领获胜，回到新 Lobby 后 AI 全部退役；可加 `-PackageRoot` 对打包版运行。本局没有 AI 死亡或复活时，相应的死亡与复活项报告为未覆盖（NotExercised），单独列出，不计为通过；不代替隐藏状态非干扰、字段白名单、真人离开或网络异常 |
| Demo 属性 / 原生记录 | `Lockdown.Rules.DemoAttributeProjection` 覆盖批准属性、双装备、条件被动和资源比例；`Lockdown.Rules.DemoMotionProbe` 用隔离原生物理验证时间分数、回滚、接地转换和缺失记录拒绝；真实拥有身体夹具显式审计保留前缀，不代替最终提供者 Fence |
| 原生代码 / 反射 | Editor/Game 构建、Bootstrap Validate、相关原生规则 |
| 入口 / 房间 | `TestDemoEntry.ps1`、`Lockdown.Rules.DemoEntryContract/DemoInputByteIntegrity/DemoMatchIdentity/DemoSessionReceipts/DemoLobbyAuthority` |
| 原生旧入口拒绝 | `RunDemoEntryCheck.ps1`，要求真实 URL 被拒绝后回前端，累计 Pawn/旧模式/服务/子系统激活为零 |
| 前端设置实际渲染 | `RunDemoFrontendCheck.ps1`，新 `UserDir` 下的 windowed RHI、合成 UMG 点击、100/125% 图像、文本偏好保存/回读及未应用显示草稿丢弃；不修改 OS 显示模式 |
| 输入 / 导航 / HUD | `Lockdown.Rules.ControlMappings/IssuedCommandLiveness`、`Lockdown.Rules.LocalSettings*/LocalDisplay*/LocalControlRegistryIsolation`、`Lockdown.Rules.HUD*`；拥有连接下的订单与输入路由见 `RunDemoCombatCheck.ps1`，HUD 观测投影见 `RunDemoVisionCheck.ps1` |
| 战斗 / 观测复制 | `RunDemoCombatCheck.ps1` 为 DEV 拥有连接的战斗与订单场景：经普通大厅 Start，已接受行走上打开设置超过订单租约（订单与心跳继续、关闭后不重放）、远程攻击移动与 S、右键击杀与击杀播报、死亡静态镜头随小地图平移、十点随机复活的精确保护及空 Q 解除保护（不含技能/装备主动/营地/AI/终局）；`RunDemoVisionCheck.ps1` |
| 库存 / 账本 | `RunTests.ps1` 中的 `DemoGrowth*` 规则；`python -B -m unittest discover -s .\Scripts -p TestEconomyEvidence.py`；原生账本证据经 `CheckEconomyEvidence.py` 独立复算；`RunDemoGrowthCheck.ps1` 为 DEV 拥有连接的击杀奖励/选择/买卖/装备主动冷却债/账本场景，并结束本局后开第二局核对新局隔离（不含营地/AI/抢圈） |
| 网络异常 / 拥有连接 | `RunDemoSessionFaultCheck.ps1`（可加 `-PackageRoot` 对打包版运行）分三段：Leave 中一名客户端经局内菜单离开，其余两人继续，Host 结束后结果表记其离局且无击杀/死亡；HostLoss 结束 Host 进程，客户端回到主菜单并显示“会话中断”，不出现结果表；Join 对关闭端口和无法解析的地址显示“连接失败：原因”，仍停在前端且可再次加入。全部在同机回环上运行，不代表跨机器局域网或公网直连 |
| 全新检出 | `VerifyCleanCheckout.ps1`；它验证本地 LFS 缓存检出，不证明远程分发 |

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

## 文档维护与共同交付

玩法决定改 App Spec，组件接口/约束改对应系统手册，工作状态与讨论放 Issue/PR。
有效系统说明按内容保留、合并和校准；失效设计与重复档案删除，不以版本目录持续堆叠。
四份核心文档只是入口，不限制必要技术手册的数量。

PR 说明实际变化、复用边界、已运行/未运行的检查及限制，不复制整段会话。
共享历史不改写，二进制资产先分工。仓库内约束并不代表 GitHub Ruleset 已配置；
远端保护由维护者另行确认，不能仅凭这份文档宣称禁强推已在服务器强制生效。

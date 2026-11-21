# 开发指南

产品目标见 [App Spec](APP_SPEC.md)，协作约束见 [AGENTS](../AGENTS.md)，验收状态见[实施就绪评估](demo/READINESS.md)。
本页按章节查阅，不需要通读。先用任务索引选一份手册，再从手册的入口表定位实际实现；不要顺读全部系统。

流程直达：[环境与首次启动](#环境与首次启动) · [验证](#验证) · [工程目录](#工程目录) ·
[数据修改](#数据修改) · [运行与联机](#运行与联机) · [试玩问题排查](#试玩问题排查) · [任务管理](#任务管理issuemilestoneproject)。

## 按任务读取系统手册

| 工作 | 技术说明 |
| --- | --- |
| 关卡、地编、美术、TA 与场景相关功能（必读） | [关卡创作自由与开发约束](LEVEL_AUTHORING_CONTRACT.md)，再按下表读具体接口 |
| 模块、Lobby、IP 联机、阶段与休息、生命代次、喷漆与结果、回执 | [架构与网络](systems/ARCHITECTURE_AND_NETWORKING.md) |
| 操作表、订单、点击移动、预测、取消、输入层、镜头、本地设置 | [移动、输入与镜头](systems/MOVEMENT_INPUT_AND_CAMERA.md) |
| 伤害/状态、属性、时序、效果来源、构筑、死亡事实 | [战斗与成长](systems/COMBAT_AND_GROWTH.md) |
| 奖励、经验、选择、去重、原子事务 | [成长事务](systems/GROWTH_TRANSACTIONS.md) |
| 对手 AI 的接口与策略 | [对手 AI](systems/OPPONENT_AI.md) |
| 战争迷雾、观测句柄、HUD、界面页面、设置页与隐私 | [视野与 UI](systems/VISION_AND_UI.md) |
| 地形、碰撞、世界查询、营地、SourceData、资产 | [世界与数据管线](systems/WORLD_AND_DATA_PIPELINE.md) |
| 每个检查与场景覆盖什么、规则测试集合、里程碑清单、共用机器 | [验证手册](systems/VERIFICATION.md) |
| 打包、产品版本、PR 检查、每日试玩包、自托管 runner | [CI 与发布](systems/CI_AND_RELEASE.md) |
| 构筑评估（DEV 实验编排、原生重放、合法成长与反事实报告） | [构筑评估 Harness](systems/BUILD_EVALUATION.md) |

每个玩法域只有这里的一份手册，开头的表给出文件、入口函数、不变量与检查入口；跨域语义在[共享契约](demo/SHARED_CONTRACTS.md)，数值在[内容与数值](demo/CONTENT_AND_BALANCE.md)。

### 文档维护归属

| 事实 | 唯一维护位置 |
| --- | --- |
| 常驻安全、协作、实现边界与阅读路由 | [AGENTS](../AGENTS.md) |
| 产品规则、显示术语与完成标准 | [App Spec](APP_SPEC.md#按任务查规则) |
| 内容、公式与批准数值 | [内容与数值](demo/CONTENT_AND_BALANCE.md#按任务查内容)；运行输入沿 SourceData → Sync → Content 生成 |
| 跨领域所有权、身份、时间与交接语义 | [共享契约](demo/SHARED_CONTRACTS.md) |
| 本领域接口、文件、入口函数与不变量 | 上表对应的系统手册 |
| 环境、日常命令、数据修改与 Issue/PR 流程 | 本页对应章节 |
| 检查覆盖范围、限制、测试集合与里程碑清单 | [验证手册](systems/VERIFICATION.md#最小验证选择) |
| 打包、版本、runner 与 CI 门禁 | [CI 与发布](systems/CI_AND_RELEASE.md) |
| 验收证据、未运行和未实现 | [就绪评估](demo/READINESS.md) |

README 只作项目概览与入口。新增说明写入所属章节，其他位置链接过去，不复制整套规则或测试清单。
Issue/PR 保存任务进度、决策原话与本次验证结果，不将这些记录写入系统手册。

## 环境与首次启动

使用 Windows 10/11、PowerShell 7、Python 3、Git LFS、Unreal **5.8.4** 及兼容的 MSVC/Windows SDK。
精确引擎基线在 `Config\PlatformLock.json`。需要 .NET Framework SDK，只有现代 .NET runtime 或 targeting pack 不够。
`.uasset`、`.umap` 以及 `SourceData\Audio`、`SourceData\Art` 下的音频与 3D 源文件、`SourceData\VFX` 的特效遮罩由 Git LFS 管理。

在工作区根目录执行：

```powershell
git lfs install
git lfs pull                     # 新工作区的 Content 只有 LFS 指针，规则测试、编辑器和场景都需要先拉取
# 在根目录 .env 添加 LD_UE_ROOT="<UE 5.8.4 安装目录>"；此文件已被 Git 忽略
.\Scripts\Preflight.ps1          # 核对引擎版本与工具链
.\Scripts\BuildEditor.ps1        # 编辑器目标；编辑器、规则测试和 -game 运行都只需要它
.\Scripts\OpenStandalone.ps1     # 打开前端；-HostRoom 直接以 Host 进入 L_Demo
.\Scripts\OpenEditor.ps1
```

`BuildGame.ps1`（游戏目标）只用于里程碑检查，日常开发不需要；打包脚本自己构建。
引擎查找依次使用 `-EngineRoot`、进程环境变量 `LD_UE_ROOT`、工作区根目录 `.env` 中的 `LD_UE_ROOT`、
忽略的 `Config\Local\Environment.json` 和项目引擎关联。`.env` 只读取此键的字面量值，可使用成对的单/双引号，
不执行文件内容；重复键或未闭合引号会明确报错，仍核对锁定引擎版本。本机路径只放被忽略的本机配置，
不用在每个新 shell 重设；已有 `.env` 时保留其余键，只更新引擎路径。`.env` 变化会使本地原生验证缓存失效。
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
.\Scripts\Verify.ps1 -Plan -Rules Auto                  # include 图推导规则选择；仅计划
.\Scripts\Verify.ps1 -Rules Auto                        # 执行有界的受影响规则子集
.\Scripts\Verify.ps1 -Force                             # 不复用通过结果
.\Scripts\RunChain.ps1 -Filter <测试全名> -Scenario Vision,OpponentUI   # 构建 → 定向规则测试 → 指定场景，一条命令
```

`RunChain.ps1` 依次跑增量构建（`-NoBuild` 跳过）、`-Filter` 给出的规则测试与 `-Scenario` 列出的场景，任一步失败即停，
结束时汇总每步的结果、耗时与证据目录；它只是把手工串联的几条命令合成一条，不是门禁。
`Verify.ps1`、`RunTests.ps1`、经 `Common.ps1` 启动的脚本与 `LocalVerification.py` 自行把控制台和 Python 子进程设为 UTF-8，
中文区域的机器上不需要先设 `PYTHONUTF8` 或 `PYTHONIOENCODING`。

它比较工作区（含未提交与未跟踪文件）与 `origin/main` 的合并基点：

| 改动 | 本地运行 |
| --- | --- |
| 只有 `docs`、`*.md`、工作流 | 不运行 |
| `Scripts/*.py` | 反向 Python 导入闭包中的 `Test*.py`，包含字面量脚本引用；无静态可达测试时报告缺口，不回退全量 |
| `SourceData`、`Content/Lockdown/Data` | 两个 Sync `--check`；仅选择导入闭包中引用该数据路径的 Python 测试 |
| `Source`、`*.uproject` | 一次增量 `BuildEditor.ps1`；计划同时列出反向 include 图推导的规则名，`-Rules Auto` 可选执行 |
| 其他（`Config`、资产、PowerShell 脚本） | 不运行；按[验证手册](systems/VERIFICATION.md#最小验证选择)选对应检查 |

验证阶梯：一批连贯改动跑一次 Verify → 本批新增或修改了测试时，用这些测试的全名 `-Filter` /
`RunPythonTests.py --pattern <文件名>` 定向检查 →
失败后只重跑失败项（Python 用 `--failed`；原生用失败项的 `-Filter`）→ 非草稿 PR CI 完整门禁 → 里程碑全量。
不因为没有同名测试、未知依赖或局部失败就升级为本地完整套件。

`LocalVerification.py` 维护选择和缓存，`Verify.ps1` 只编排参数；Python import 提取和闭包遍历复用 `CIChangeScope.py`，
CI 拒绝缺失入口，局部计划保留已删除模块的依赖边。静态分析识别 Python import、字面量脚本引用、
数据路径与项目双引号 include；实现文件通过同名头文件关联使用者。它不运行 Python 测试来发现依赖。
字面量目录引用会保守包含子目录；动态加载、运行时拼接、宏 include、跨编译单元隐藏接线可能缺失，CI 完整门禁兜底，局部通过不等于完整验收。
超过 12 个 Python 测试模块或 80 项原生规则时，计划明确标为交 CI，不自动跑全量；
这是本地工具的开销边界，不是产品规则。显式 `-Filter` 也不能通过 Verify 启动完整套件。
需要检查选择结果可用 `-Plan -Json`；模拟路径只能用于计划，例如
`.\Scripts\Verify.ps1 -Plan -Paths Scripts\SummarizePlaytestLog.py`，不会修改工作区或将模拟通过写入缓存。

通过记录和独立日志位于忽略的 `Artifacts\Verification`。复用要求命令、输入文件内容/目录成员、
Python/工具环境、构建输出（如适用）及日志摘要一致；计划和跳过信息显示上次通过时间与日志路径。
删除证据、修改输入、失败或运行中输入变化都不能产生可复用的通过结果；`-Force` 强制重验。
本地静态输入指纹不证明外部引擎/SDK 安装未被修改，切换或维护外部工具后用 `-Force`，不能把本机缓存当作 CI 门禁。

- 规则测试的门禁是 PR 的 CI（[CI 与发布](systems/CI_AND_RELEASE.md#pr-检查与每日试玩包)），本地不重复跑整套。
  只有新增或改了规则测试、或在排查失败用例时，才加 `-Filter`，且只写这些测试的全名（可给多个），
  不按 `Lockdown.Rules.Combat` 这类领域前缀成组跑；它是部分运行，不算门禁。只改产品代码、没改测试时不跑规则测试，交 CI。
  新增的原生测试同步登记到 `CheckRuleReport.py`，并在 PR 里写明测试名。
- 先完成一批连贯开发，再集中做一次增量、定向验证收尾，不在每次小编辑后构建或测试。
  提前检查仅用于定位具体缺陷或验证高风险实现假设；保留 `Binaries`、`Intermediate` 与构建缓存，不为验证清理重建。
  已通过且输入未受后续改动影响的检查不重复跑。无冲突地合并 `main` 后不重跑；
  冲突涉及生成文件（`Content/Lockdown/Data`、生成资产、`*Contract.h`）时沿 Sync 入口重新生成，不手工拼接。
- 场景（`RunDemo*Check.ps1`）只在改动涉及网络、交易、成长、胜负等必须用真实拥有连接证明的行为，
  或 Issue 明确要求时才在 PR 前跑；只选覆盖实际改动及其直接依赖的最小场景或已有定向模式。
  不用仅有 Host 的调试场景替代拥有连接证据；同一模块不等于需要整局回归。
  没有定向入口时报告覆盖缺口，不默认启动长测，其余集中到里程碑验收。
- Core、Combat、Camp、Growth、Capture、Vision、Opponent 复用 `DemoEntry.ps1` 的进程与证据 harness；
  新场景沿用它，入口只维护探针参数、断言和特殊错误规则，不复制端口、身份、轮询与退出流程。
- 失败先看本次 `Artifacts`、日志与断言并标记未通过，不原样重跑来碰运气通过或判断偶发性。
  修复相关原因后只重跑失败项及受影响检查；没有定向入口的长测仅在明确要求专项验证时重跑。
  无关失败保留证据和未确认归因，不扩大开发范围。
- `-FullUnity`、`RunTests.ps1 -Full -ImportTests Run`、全套场景只用于里程碑、发布和全新检出，
  或明确要求的专项验证；清单见[验证手册](systems/VERIFICATION.md#合并前与里程碑检查)。
- fork PR 不跑 CI 的原生任务：改了 `Source` 的 fork PR 在本地用 `Verify.ps1 -Filter` 跑受影响的规则前缀并在 PR 里写明结果；
  合并前由维护者在本仓库分支上补跑原生门禁。
- 新 worktree 不继承 `Binaries` 与 `Intermediate`，Content 还可能是 LFS 指针，需要 `git lfs pull`。
  首次构建通常要 3–6 分钟，已有产物的增量构建约 20 秒，实际取决于机器与改动；因此串行任务优先复用已构建工作区。
  不复制别的工作区的 Binaries/Intermediate 来承诺增量复用；只改文档、数据或 Python 的任务不需要构建。
- Python 测试失败时看输出结尾 `Log:` 指向的日志，修复后用 `python -B .\Scripts\RunPythonTests.py --failed` 只重跑失败项。
- PR 保持 Draft 到改动稳定，攒一批再推：Ready PR 每次推送都触发 CI；需要 native 门禁时会占用自托管 runner。
  已 Ready 的 PR 修评审意见或合并 main 时，只跑 `Verify.ps1` 再推，规则测试交给这次推送触发的 CI。
- 共用机器的并发上限见[验证手册](systems/VERIFICATION.md#多人共用一台机器)。

## 工程目录

| 目录 | 职责 |
| --- | --- |
| `Source\Lockdown\Public` / `Private` | 当前原生运行模块接口与实现 |
| `Source\LockdownEditor` | 原生导入、地图/资产生成与保存 |
| `Source\LockdownTests` | `Lockdown.Rules` 规则测试与夹具（仅编辑器目标） |
| `SourceData` | 当前生成器消费的内容定义、schema、ID 映射与 CSV |
| `Content\Lockdown\Data` | 已生成并校验的运行载荷 |
| `Content\Lockdown\Generated` | 原生目录、派生碰撞网格与保存的正式关卡 |
| `Content\Lockdown\Audio` | `ImportDemoAudio.ps1` 由 `SourceData\Audio` 清单导入的音效目录与声音资产 |
| `Content\Lockdown\Art` | `ImportDemoArt.ps1` 由 `SourceData\Art` 清单导入的身体、动画、装饰道具与美术目录；`Art\UI` 是 `-Mode ImportUI` 由 `SourceData\UI\ui_images.json`（项目方提供的标题与房间背景图，不属于 CC0 美术包）与 `ui_icons.json`（`SourceData\UI\Icons` 的主动、五维属性、构筑牌与状态图标，#85）导入的 UI 纹理，以及由 `ui_fonts.json`（`SourceData\UI\Fonts` 的思源黑体与 OFL 许可）导入的 `Art\UI\Fonts` 字体；`Art\VFX\Hit` 是 `-Mode ImportVFX` 由 `SourceData\VFX\vfx_textures.json`（`SourceData\VFX\Blender\hit_textures.py` 用 Blender 渲染的命中遮罩）导入的特效纹理与导入器生成的两个特效材质 ；铁笼栏片 `SourceData\Art\Lockdown\CageFence\cage_fence.glb` 由 `Scripts\BuildCageFenceModel.py` 在 Blender 中生成（`blender -b --factory-startup --python Scripts\BuildCageFenceModel.py -- <输出 glb>`），改动后重算清单哈希再导入；`-Mode ImportProps` 只重新导入道具与装饰道具引用，保留目录里的身体与从地图读出的装饰记录（不做摆放检查），用于地图仍在重新摆放时加入新道具 |
| `Config` | 项目、平台与布局配置；本机设置放忽略的 `Config\Local` |
| `Scripts` | 构建、生成、运行和当前结果校验 |
| `docs` | 当前产品规则与组件技术说明，不存构建输入或每次运行档案 |
| `Artifacts` | 本机运行输出，Git 忽略 |

## 数据修改

正式地图在 UE 关卡里编辑。静态网格体、装饰、材质和灯光保存后即可 Play；碰撞、地面高度和视线读取当前关卡的原生碰撞。`Scripts/SyncDemoMap.ps1` 用于显式刷新布局的派生源快照，不是普通场景编辑的 Play 前置条件。
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

GameMode 提供真人和最多 3 个 AI 共用的六人容量、Host 权限、结构名单版本与有界会话回执。
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
（热重载时另标“内容热重载”）。开发构建也允许房主在局内按 `Ctrl+回车` 将当前房间开启为测试模式；客户端仅可使用已由房主开启的测试房间。对局中按 `Ctrl+回车` 或点底部 HUD 的“测试”按钮打开测试面板：

- 跑图测试：测试面板可为自己开启／关闭“生命不减”，抵挡伤害和环境掉血；它不会关闭怪物索敌。
  房主可暂停／恢复阶段倒计时；角色移动、物理、AI、战斗和技能冷却继续运行，恢复时从剩余时间继续。
  计时暂停包含准备区、野外阶段及终局结束与缩圈倒计时；占旗交互与战斗技能的独立计时仍按真实时间运行。
- 场景测试：T 自由镜头／再次 T 传送；WASD 移动、Q/E 升降、右键转向、Shift 加速。传送在服务器事务边界检查当前生命、静态表面支撑及胶囊净空，不要求导航网格或地面标签；失败保留自由镜头并反馈原因。新生命、换局、离开时退出镜头；
- 和平模式：本人开启／关闭，其他参赛者和野怪不能对本人起手、命中伤害或控制；跨死亡保留，换局清除；
- 跳过开局准备：房主发起，先检查出生池的原生支撑与到野外旗杆的完整路径；失败时保留当前角色和准备阶段并明确拒绝。检查通过后正常锁定构筑并通过真实返场准备进入第一段野外，场景身体未准备好时继续等待；

- 资源与重复试验：加经验、补满生命与劲、重置冷却、重置到开局；加 1 个技能点、把劲设为 0 到上限之间的整数（只在面板里，没有控制台命令）；
- 主动技能：运行目录里全部主动，显示是否已学，“学习”直接学到空槽（不需要机会或图纸），Q/W/E 都满时用“替换”；
- 五维加点：用自己的未分配点数给五维属性加点；
- 测试属性：十一项属性各一个输入框，旁边显示服务器提交后的最终值与来源（含“测试”这一项）；清除全部测试属性；
- 打击感与特效：`ld.Feel.*` 每项一行 -/+ 步进（命中停顿、镜头、出手闪光、命中粒子、拟声字、出手预测、挥击轨迹、施法聚能）与“全部恢复初值”；
  这些是本机客户端控制台变量，不发送 DEV 命令、只在本次运行有效，设置页“特效”页步进同一组值（[视野与界面](systems/VISION_AND_UI.md)）；
- 整局（只有房主看得到）：刷新已清空营地、提前开圈。

面板的成长与战斗项只把输入换成同一组 DEV 测试命令，结果由服务器按真实规则提交；数量或数值格式不对时在本地拒绝、不发送，
面板顶部显示最近一次的结果或拒绝原因。也可以按 `~` 打开控制台使用同样的命令：

| 命令 | 作用 |
| --- | --- |
| `ld.Dev.XP <数量>` | 给自己加经验，升级与机会按正常奖励结算 |
| `ld.Dev.Refill` | 活着时补满生命，劲设为上限 |
| `ld.Dev.ResetCooldowns` | 自己的技能与 D 冲刺冷却全部就绪 |
| `ld.Dev.List` | 列出运行目录里全部主动技能与五维属性的 id |
| `ld.Dev.Learn <主动 id> [Q\|W\|E]` | 立即学到最低的空槽；给出 Q、W 或 E 时替换该槽的主动 |
| `ld.Dev.Blueprint <主动 id>` | 不消耗机会，把该主动加入待学图纸 |
| `ld.Dev.Points <五维 id> <数量>` | 用自己的未分配点数给该五维属性加点 |
| `ld.Dev.Attr <属性> <数值>` | 在自己的构筑之上叠加一层测试属性（`MaxHP`、`AD`、`AP`、`Armor`、`MR`、`Haste` 为点数，`Range` 为整厘米，`AttackSpeed`、`MoveSpeed` 为基础值的百分比，`Crit` 为百分点；最多两位小数，0 去掉该项） |
| `ld.Dev.ClearAttr` | 去掉自己的全部测试属性，构筑本身不变 |
| `ld.Dev.RefreshCamps` | 只限房主：所有已清空、正在等刷新的营地在下一个边界按正常刷新重新生成，存活营地不动 |
| `ld.Dev.OpenCapture` | 只限房主、只在前三段定时野外：跳过剩余的野外段与休息，本边界直接进入最终野外并开放旗杆，喷漆、守护时长和全部规则不变 |
| `ld.Dev.Reset` | 自己回到开局：1 级、0 经验，没有主动、图纸、已分配点数和测试属性，重新给一组开局主动三选一，冷却全部就绪 |

学习仍受 Q/W/E 三槽约束。测试属性在属性视图的悬停来源里单独显示为“测试”，跨死亡保留到本局结束，
最终值超出目录上下限时整条命令被拒绝而不是截断。刷新营地与提前开圈改变整局，只接受房主自己的连接，
其他客户端发送时回 `NotAuthority`；其余命令只作用于发送者自己，经服务器在 Transactions 阶段提交，
与正常奖励和选择共用同一提交路径；没有开关的 Host、Shipping 构建、旧局或旧生命的请求都会被明确拒绝。
测试会话的结果是夹具证据，不能当作无作弊完整对局的验收。实现与限制见[成长事务](systems/GROWTH_TRANSACTIONS.md)。

## 试玩问题排查

试玩期玩完一局后，把日志交给 AI 复盘和排查。日志只在已提交的事件和每秒/每 10 秒的采样上写行，没有逐帧日志。
AI 满员场景实测：对局中房主日志约 150 KiB/分钟（每秒心跳占七成，命中随交战增多），20 分钟一局约 3–5 MiB；
客户端日志约 16 KiB/分钟。

**日志在哪里**（时间戳为 UTC，如 `[2026.11.06-03.15.10:419][帧号]`）：

| 运行方式 | 日志 |
| --- | --- |
| 每日试玩包 / `OpenPackaged.ps1` | `<解压目录>\Archive\Windows\Lockdown\Saved\Logs\Lockdown.log` 是本次运行，更早的运行改名为 `Lockdown-backup-<时间>.log` |
| `OpenStandalone.ps1` / 编辑器 | 工作区 `Saved\Logs\Lockdown.log` |
| `RunDemo*Check.ps1` | 输出目录里的 `Host.log`、`Client1.log`… |

每个玩家交自己的日志就能复盘自己的视角；要查“实际为什么”（谁打的、打了多少、为什么没给经验），再要同一局房主的日志。
房主日志记录完整权威信息：开局头（产品版本、源码提交、内容摘要、网络地址）与名单、每次命中、
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

有房主日志时，每局还有一节“Metrics (spec §16)”（参赛者以 `LD_MATCH_ROSTER` 为准；休息室私人训练不计：`LD_TRAINING_DUMMY_SPAWN` 的假人与名单外的参赛者类身体，以及心跳全在休息里的生命——开局到第一次 `LD_FIELD_ENTER`、每次 `LD_REST_ENTER` 到下一次 `LD_FIELD_ENTER`——的劲账本与完美闪避；伤害因子与反应种类也排除假人），末尾一节把全部局汇总成史诗 #287 §16 的指标表（目标只供判断，不是门禁）：

| 指标 | 口径 |
| --- | --- |
| 伤害占比 | 参赛者造成的 `LD_HIT dmg`，按 `via` 分普攻 / 技能（`learned:`）/ 牌（`card:`）/ 其他，打参赛者与打野怪分开；反应结算的命中带施加者自己的来源，`LD_HIT` 没有反应字段，所以反应伤害计在施加者的来源里，反应分项写 “no data” |
| 每场施法次数 | 一名参赛者的“一场”：它的 `LD_CAST` 与它和其他参赛者互相的命中，间隔超过 5 s（交战窗口）或它死亡即结束；死后 5 s 内它留下的效果（区域、飞行中的弹道）的命中仍算这一场（`LD_CAST` 记在提交释放的那次推进结束时，死亡后 50 ms 内的施法仍归这一场）；跨局汇总只用有 `LD_CAST` 的局；只计含参赛者互相命中的场；同级场 = 开场时它与所有交手者等级相同 |
| 击杀时间 | 死者与凶手之间（任一方向）、间隔不超过 5 s 的连续命中中第一次命中到死亡回执；死者在这段里也打中过凶手才算互打击杀，没还手的只计“单方面击杀”条数；按死前一刻双方等级判同级 |
| 劲的收入构成 | 野外生命的 `LD_MOMENTUM` 各项之和与占比，另列脱战漂移、花费、爆劲次数与这些生命里的 `LD_PERFECT_DODGE` 次数；账本在身体移除时才写，局末仍存活的生命没有账本，报告写明缺账本的野外生命条数 |
| 质变 | 一名参赛者全部场内对参赛者的伤害 ÷（各场同级裸构筑 DPS × 场长，至少 1 s）≥ 3；裸构筑 DPS 默认是 §3.9 的估算（L1 72、L10 154，线性插值、两端沿最近一段外推），`--baseline-dps 1=..,6=..,10=..` 换成实测值，报告写明所用基线 |
| 被选率 | `LD_OFFER`（主动）与 `LD_CARD_OFFER`（牌）出现次数中被 `LD_CHOICE` 或 `LD_CARD kind=PickCard` 选中的比例，主动（`active_*`）与牌分列；另列装上过的牌（`LD_CARD installed=1`）占参赛者·局的比例 |
| 胜者构筑种类 | 胜者 `LD_MATCH_SUMMARY_ROW` 的主动（不分槽位）、属性点与每张已装的牌及其位置（`cards=`，同名多份分别计），都相同才算同一种；行里没有点数或牌字段时写 “no data” |
| 离谱时刻 | 单次命中 ≥ 目标生命上限 60% 的条数（列出最大的 5 条），或 `LD_CHAIN steps` ≥ 5 |
| 最长连锁、伤害因子 | `LD_CHAIN`、`LD_HIT factor_bp`/`factors`；日志里没有这些行时写 “no data”，旧日志另报 `amp_bp` 峰值 |

缺少相应日志行的指标写 “no data” 和原因，不输出 0。局按代次（generation）切分；代次只在一个 Host 进程内唯一，几份 Host 日志用了同一代次时各算一局，标题附日志名，这一代次的客户端日志另列。不同目录里的同名日志（如两次遥测的 `Telemetry.log`）按文件区分，名字后附上级目录名。`--metrics-only` 只输出指标两节。
AI 整局遥测的房主日志也可以直接汇总：`python -B .\Scripts\SummarizePlaytestLog.py <遥测输出目录>\Telemetry.log --metrics-only`
（遥测输出目录见[验证手册](systems/VERIFICATION.md#合并前与里程碑检查)）。
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

## 任务管理：Issue、Milestone、Project

仅在建票、改期、提交 PR 或合并时读取本节；开发中的常驻约束见 [AGENTS](../AGENTS.md)。

- **合并同类**：同一系统、同一类型的工作合成一张票，子项用 checklist 列出，由一个人连续做完。
  发现新问题时先搜现有的票：能并入就在原票里加子项，不能并入才新建。
- **票的内容**：只写三部分：目标；范围（checklist）；验收标准。引用相关规格章节和已批准决定的原话，
  不写个人上手步骤或环境教学。
- **标签**：每张票打一个类型标签，可选：`类型:逻辑`、`类型:数值数据`、`类型:表现`、
  `类型:工具工程`、`类型:测试`、`类型:流程`。
  另外按情况加：本期不做的加 `12/31之后`；缺陷加 `bug`；需要用户拍板的加 `question`。
- **Milestone**：每两周一个，依次是 M1、M2、M3、M4、交付；截止日期以 GitHub Milestone 为准。
  排期越偏逻辑越早、越偏视觉越晚；M4 只做试玩、打磨和迭代，不加新功能。
  带 `12/31之后` 的票不挂 milestone。
- **看板**：[Lockdown 12/31 内部版](https://github.com/users/sinmentis/projects/4)。“排期”视图按“开始日期”和“结束日期”显示。
  PR 关联票后状态自动变为 In Progress，票关闭后自动变为 Done；开工但还没有 PR 时，手动改为 In Progress。
- **新建票必做**：打类型标签、指派负责人、挂 Milestone 或打 `12/31之后` 标签；
  加入看板，状态设为 Todo，填写开始日期和结束日期，结束日期不晚于 Milestone 的截止日。漏掉任何一项就不会出现在排期里。
- **改期**：更换 Milestone 或负责人时，同步更新结束日期。票过了结束日期还没完成，在票里写明原因并改期。
- **重复的票**：用 `gh issue close <N> --duplicate-of <M>` 关闭，目标票在“合并来源”里列出原票。
- **产品决定**：决定原话记在对应 Issue；落地后的规格归属见 [AGENTS：任务管理](../AGENTS.md#任务管理issuemilestoneproject)。

从开工到关闭：

1. 把票指派给自己。
2. 在干净且无人使用的现有工作区，从最新 `main` 切独立分支；同一任务跟进复用原会话与工作区。
   仅确需并行时建新 worktree，协调者派生会话也须说明 LFS 和首次构建的冷启动成本。
3. 提 PR，描述里写 `Fixes #N`、改动内容和验证结果，通过、失败、未运行要分开写，并写明新增的测试名。
4. 验证通过后以 merge commit 合并，并删除分支；禁止 force push，不改写共享提交。
5. 确认票已经关闭。

一张票可以拆成多个 PR：前面的 PR 写 `Part of #N`，最后一个写 `Fixes #N`。

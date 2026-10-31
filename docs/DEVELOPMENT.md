# 开发指南

产品目标见 [App Spec](APP_SPEC.md)，共同开发约束见 [AGENTS](../AGENTS.md)。
当前仓库具备原生工程和可复用组件，但尚未完整实现该 Demo。
下面的命令运行当前工程，不是新玩法已经完成的保证。

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
.\Scripts\BootstrapAssets.ps1 -Mode Validate
.\Scripts\OpenStandalone.ps1
```

脚本也接受 `-EngineRoot`。根目录按脚本所在工作区解析，不依赖某台电脑的固定项目路径。
引擎查找依次使用参数、`LD_UE_ROOT`、忽略的 `Config\Local\Environment.json`
及项目引擎关联；未注册的安装应显式指定，不修改共享项目配置为本机路径。
每个新 shell 都需要自己的环境变量。

原生模块缺失时先构建，修改反射类时关闭相关编辑器。
有缺失生成资产时使用 `BootstrapAssets.ps1` 的 Generate 模式；
Validate 不创建资产。Regenerate 是显式替换操作，不是所有失败的通用修复。

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

将规格转为实施工作时，先读 [Demo 总体架构与共享契约报告](demo/ARCHITECTURE.md)；
其中的复用审计、建议任务和待决问题不替代 App Spec，也不代表游戏已经实现。

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

例如当前目录检查：

```powershell
python -B .\Scripts\SyncB84Data.py --check
python -B .\Scripts\SyncV09Data.py --check
python -B -m unittest discover -s .\Scripts -p "Test*Import.py"
```

授权修改源定义后，不带 `--check` 运行对应生成器，复核全部输出，再做原生导入/相关检查。
依赖链可能同时更新载荷和头文件；不能只提交其中一边。
生成器失败要修正输入或真实依赖，不能跳过校验、填写默认值或使用历史档案让错误看起来通过。

产品版本以 `Config\DefaultGame.ini` 的 `ProjectVersion` 为准。
它不同于 `Lockdown.uproject` 的 `FileVersion`/`EngineAssociation`，也不同于协议、目录与枚举版本。
`PackageWin64.ps1` 从同一设置读取产品版本。

## 当前工程运行与联机

无 Profile 的 `OpenStandalone.ps1` 打开前端。
当前前端 Host 使用 `V05` 工程入口，不会自动启用 App Spec 的三选一、两个装备槽或完整对手 AI。
需要针对既有组件检查时显式选择已有 profile：

```powershell
.\Scripts\OpenStandalone.ps1 -Profile v08_integration
```

脚本支持的其他 profile 见参数声明；完整地图有独立的 `OpenB90.ps1`、`OpenB91.ps1` 和相应运行器。
不要同时拼接两套地图、人物、经济或计时规则。

Host 建立监听世界，客户端通过 Host 的可达地址和端口连接。
同机可使用回环地址，其他设备使用局域网地址；公网直连需要相应网络配置，不提供自动穿透。
同一验证中使用相同产品构建、引擎和内容；连接失败要读取前端反馈和当前输出日志。
不要把开发者全知实例当作普通远程拥有客户端。

## 最小验证选择

| 改动 | 起点 |
| --- | --- |
| 文字与导航 | 链接、代码入口与当前事实，`git diff --check` |
| SourceData / 编译器 | 对应 Sync `--check`、`Test*Import.py`，核对载荷与 contract |
| 原生代码 / 反射 | Editor/Game 构建、Bootstrap Validate、相关原生规则 |
| 输入 / 导航 / HUD | `RunInputCheck.ps1`、`RunNavigationCheck.ps1`、`RunHUDCheck.ps1` 的相关模式 |
| 战斗 / 观测复制 | `RunCombatCheck.ps1`、`RunCombatNetworkCheck.ps1`、`RunViewNetworkCheck.ps1` |
| 物品 / 服务 | `RunInventoryCheck.ps1`、`RunVendorCheck.ps1` 和相关事务检查 |
| 全新检出 | `VerifyCleanCheckout.ps1`；它验证本地 LFS 缓存检出，不证明远程分发 |

`RunTests.ps1` 执行已有 `Lockdown.Rules` 套件。需要更具体的选择、网络条件或打包路径时，
先查看相关脚本参数，不把所有昂贵场景作为每次文档改动的门槛。
源码、配置和生成输入在一次原生构建/运行期间保持稳定。

## 打包与产物

```powershell
.\Scripts\PackageWin64.ps1
.\Scripts\OpenPackaged.ps1 -PackageRoot '<本次 Archive 目录>'
```

打包脚本验证 Bootstrap 后生成独立输出，并记录产品版本、源输入与包文件摘要。
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

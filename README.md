# Lockdown

**v0.10.0 · Unreal Engine 5.8.4 · Windows PC**

**对外标题：谁是雾港最强淫人王**（“淫”是雾港黑话，只指把人揍趴下）。技术项目名仍为 Lockdown。

雾港大学城后街的烂尾球场上，脆皮大学生无差别大乱斗：一款采用 MOBA 点击操作和俯视斜角镜头的自由混战游戏。
打野与战斗获取成长，通过三选一（开盲盒）和装备建立优势，最终在旗杆上喷上自己的涂鸦并守住，成为淫人王。

## 当前状态

Demo 的完整流程已经实现：前端建房或加入 → Lobby（Host 选 0–3 个对手 AI）→ 从零打野、对抗、
三选一与购物 → 旗杆开放后喷漆、守护与围攻 → 唯一结果 → Host 带房间回 Lobby 开新局。
运行入口只有前端和唯一 Demo 地图，旧模式代码已经删除。

- 首版内容与数值是用户批准的首次试玩输入，**没有做过平衡测试**。
- 所有联机验证都在同一台机器的回环连接上运行；跨机器局域网与公网直连没有验证。
- 下一阶段以好玩为先：先补齐玩法逻辑和内容，再反复试玩调整，最后做正式美术和声音。
  缺少的内容见[实施就绪评估](docs/demo/READINESS.md#剩余工作)的“剩余工作”。
- 验收结果、未运行与未实现项见[实施就绪评估](docs/demo/READINESS.md)。产品规则只以 [App Spec](docs/APP_SPEC.md) 为准。

## 环境

| 需要 | 说明 |
| --- | --- |
| 系统与 Shell | Windows 10/11，PowerShell 7，Python 3 |
| 引擎 | Unreal Engine **5.8.4**（精确基线见 `Config\PlatformLock.json`），通过 `LD_UE_ROOT` 或脚本的 `-EngineRoot` 指定安装目录 |
| 工具链 | 与该引擎兼容的 MSVC 与 Windows SDK，.NET Framework SDK（只有现代 .NET runtime 不够） |
| 二进制资产 | Git LFS：`.uasset`、`.umap` 以及 `SourceData\Audio`、`SourceData\Art` 下的音频与 3D 源文件 |

本机路径不提交到仓库；每个新 shell 都需要设置自己的环境变量。

## 快速开始

在工作区根目录执行：

```powershell
git lfs install
git lfs pull
$env:LD_UE_ROOT = '<UE 5.8.4 安装目录>'
.\Scripts\Preflight.ps1          # 核对引擎版本与工具链
.\Scripts\BuildEditor.ps1        # 编辑器目标
.\Scripts\BuildGame.ps1          # 游戏目标
.\Scripts\OpenStandalone.ps1     # 以无边框全屏打开前端；加 -HostRoom 直接以 Host 进入 Demo 地图
.\Scripts\OpenEditor.ps1         # 打开编辑器
```

游戏固定以无边框全屏、桌面分辨率运行。同机多开时给每个 `OpenStandalone.ps1` 加 `-Windowed`（1280×720 窗口），
一个建房，其余用回环地址加入；窗口模式下指针离开窗口就无法边缘平移，正式试玩不要用。

## 测试与打包

| 目的 | 命令 |
| --- | --- |
| 原生规则测试（必需集合见 `Scripts\CheckRuleReport.py`，当前 242 项） | `.\Scripts\RunTests.ps1` |
| 内容与世界生成器检查 | `python -B .\Scripts\SyncDemoWorldData.py --check`，`python -B .\Scripts\SyncDemoContentData.py --check` |
| Python 测试 | `python -B -m unittest discover -s .\Scripts -p "Test*.py"` |
| 试玩日志整理（房主与客户端日志合并成每局时间线） | `python -B .\Scripts\SummarizePlaytestLog.py <日志或目录>…`，见[开发指南](docs/DEVELOPMENT.md#试玩问题排查) |
| 生成资产、音效与美术校验 | `.\Scripts\BootstrapAssets.ps1`（默认 Validate，同时校验音效与美术目录） |
| 拥有连接场景 | `.\Scripts\RunDemo<Core/Combat/Camp/Growth/Capture/Vision/Opponent/SessionFault>Check.ps1`，加 `-PackageRoot <包的 Archive 目录>` 在打包版上运行 |
| 全新检出验证 | `.\Scripts\VerifyCleanCheckout.ps1` |
| 打包 Development 版 | `.\Scripts\PackageWin64.ps1`，输出到 `Artifacts\<时间戳>-PackageWin64\Archive` |
| 打开打包版 | `.\Scripts\OpenPackaged.ps1 -PackageRoot <Archive 目录>` |

每个场景覆盖和不覆盖的内容、数据修改流程见[开发指南](docs/DEVELOPMENT.md)。
所有运行输出写入 Git 忽略的 `Artifacts`。

## 协作流程

1. 所有工作都以 [GitHub Issue](https://github.com/rlsn/Lockdown-alpha/issues) 为单位，并按 Milestone 和
   [看板](https://github.com/users/sinmentis/projects/4) 排期，详见 [AGENTS](AGENTS.md) 的“任务管理”一节。
   共享接口、核心配置和地图改动，先与负责人协调。
2. **每张票**：
   1. 指派给自己；
   2. 从最新的 `main` 建分支；
   3. PR 至少通过 `BuildEditor.ps1` 和受影响的规则测试，描述写 `Fixes #N`；
   4. 验证通过后合入 `main`，合并后删除分支。
3. **里程碑与发布**：
   1. 冻结后跑完整门禁、`VerifyCleanCheckout.ps1` 和 `PackageWin64.ps1`；
   2. 在打包版上跑全部场景；
   3. 全部通过后，才替换唯一一份已知可用的包。
4. 禁止 force push，包括 `--force-with-lease`；同步共享改动用 merge。

完整约束见 [AGENTS](AGENTS.md)。

## 文档地图

| 需要 | 入口 |
| --- | --- |
| 游戏规则与完成标准 | [App Spec](docs/APP_SPEC.md) |
| 环境、命令、场景与数据修改 | [开发指南](docs/DEVELOPMENT.md) |
| 协作与实现规则（人和 AI 共用） | [AGENTS](AGENTS.md) |
| 验收结果、未运行与未实现项 | [实施就绪评估](docs/demo/READINESS.md) |
| 模块边界与跨模块契约 | [实施计划](docs/demo/IMPLEMENTATION_PLAN.md) · [共享契约](docs/demo/SHARED_CONTRACTS.md) · [架构审计](docs/demo/ARCHITECTURE.md) |
| 各玩法域的设计与实现说明 | `docs\demo` 下的领域报告（会话、操作、战斗、成长与经济、世界与营地、AI、界面） |
| 首版内容与数值（唯一来源） | [内容与数值](docs/demo/CONTENT_AND_BALANCE.md) |
| 现有组件的实现与接口 | `docs\systems` 下的系统手册，索引见[开发指南](docs/DEVELOPMENT.md#按任务读取系统手册) |

## 文件边界与第三方素材

人和 AI 阅读 `docs`；生成器读取 `SourceData`；游戏读取 `Content` 载荷和原生资产。
修改数据走对应的 Sync 或导入入口，不手改生成的载荷、合同头文件或二进制资产。

音效与 3D 美术均为 CC0 素材，来源、许可与文件哈希记录在
`SourceData\Audio\audio_cues.json` 与 `SourceData\Art\art_assets.json`，每个素材包的许可原文与源文件放在同一目录。

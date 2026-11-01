# Lockdown

**v0.10.0 · Unreal Engine 5.8.4 · Windows PC**

一款采用 MOBA 操作和俯视斜角镜头的自由混战游戏：打野与战斗获取成长，
通过三选一和装备建立优势，最终独占唯一占领点获胜。

当前仓库提供原生工程和可复用系统，**尚未完整实现目标 Demo**。
产品规则只以 [Demo App Spec](docs/APP_SPEC.md) 为准；
已有运行 profile 和内容目录不代表其全部玩法属于 Demo。

## 从这里开始

| 需要 | 入口 |
| --- | --- |
| 理解游戏目标与验收范围 | [App Spec](docs/APP_SPEC.md) |
| 判断 Demo 开工门槛、缺口与实施依赖 | [实施就绪评估](docs/demo/READINESS.md) · [实施计划](docs/demo/IMPLEMENTATION_PLAN.md) |
| 配置环境、构建和运行 | [开发指南](docs/DEVELOPMENT.md) |
| 让 AI 开始工作、了解协作规则 | [AGENTS](AGENTS.md) |
| 理解现有组件并复用 | [系统手册索引](docs/DEVELOPMENT.md#按任务读取系统手册) |

## 启动当前工程

先安装兼容的 C++ 工具链、Windows SDK、.NET Framework SDK 与 Git LFS。
精确引擎基线在 `Config\PlatformLock.json`；使用 PowerShell 7，在工作区根目录执行：

```powershell
git lfs install
git lfs pull
$env:LD_UE_ROOT = '<UE 5.8.4 安装目录>'
.\Scripts\Preflight.ps1
.\Scripts\BuildEditor.ps1
.\Scripts\BootstrapAssets.ps1 -Mode Validate
.\Scripts\OpenStandalone.ps1
```

脚本支持 `-EngineRoot`；本机路径不提交到仓库。
缺少生成资产、选择已有 profile、多人调试和打包方式见开发指南。
上面的命令启动当前工程，不等于新 Demo 的完整玩法验收。

## 文件边界

人和 AI 阅读 `docs`；生成器读取 `SourceData`；游戏读取 `Content` 载荷和原生资产。
系统手册保留当前有效的实现知识，失效设计和过程档案不在当前文档树中保留副本。
目标实施将直接收敛现有入口，只维护 Demo 一条流程；实施准备文档不代表这项代码裁剪已完成。
贡献通过独立工作区与 PR 集成，禁止 force push；具体约束见 AGENTS。

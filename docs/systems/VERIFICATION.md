# 验证与场景矩阵

日常开发与 PR 提交遵循“最小变更范围验证”；全量原生套件与完整多进程场景留给 CI 与里程碑发布门禁。

## 最小验证选择

每批连贯改动后优先运行 `.\Scripts\Verify.ps1`（纯文档不构建；改了新增/修改的规则测试时传 `-Filter <测试全名>`，只改产品源码未改测试时不跑本地规则测试；可用 `-Plan` 预览选择理由、`-Rules Auto` 按 `#include` 闭包跑有界局部规则）。

| 变更域 | 最小本地验证入口 |
| --- | --- |
| `docs/**`、`*.md` | 无需构建；核对相对链接与代码事实一致 |
| `Scripts/*.py` | `python -B .\Scripts\RunPythonTests.py`（失败后可用 `--failed` 定向重跑） |
| `SourceData/Demo/*` / `World/*` | `SyncDemoWorldData.py --check`、`SyncDemoContentData.py --check`、`Test*Import.py`；刷新资产用 `BootstrapAssets.ps1 -Mode RefreshDefinitions`（改几何用 `Regenerate`） |
| `SourceData/Art` / `UI` / `Audio` | `ImportDemoArt.ps1 -Mode Validate`、`ImportDemoAudio.ps1 -Mode Validate` |
| `Source/Lockdown*` C++ | `.\Scripts\Verify.ps1`（增量 `BuildEditor.ps1`）；新增原生 `Lockdown.Rules.*` 测试须同步登记到 `Scripts\CheckRuleReport.py` 的 `REQUIRED_TESTS` 并跑 `TestRuleReport.py` |
| 必须由真实拥有连接证明的联机/视野/阶段行为 | 按需选跑对应 `RunDemo*Check.ps1`：`Core`（初生/运动/返场）、`Combat`（寻路/施法/追击/离屏帧）、`Growth`（R0–休息跨段/奖励/换局隔离）、`Camp`（反击、回位、段内清空和中场整图重置）、`Vision`（视野隔离/`-ReactionOnly`/`-IceOnly`）、`Capture`（三段野外+R3终局改装+断线恢复）、`Opponent`（满房 AI）、`SessionFault`（退房/拒入/房主离开） |

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
```

AI 多局遥测（诊断用，非门禁）：`.\Scripts\RunDemoOpponentCheck.ps1 -Telemetry -Accelerated -MatchCount <1-50>`（60 FPS 固定步长约 3 倍速跑完，输出 `Summary.json`）。

## 多人共用一台机器

共享引擎 `Build.bat` 全局互斥；`RunTests.ps1` 与编辑器版 `RunDemo*Check.ps1` 通过 `UE_SKIP_UBT_SDK_SETUP=1` 跳过启动期 SDK 校验避免被他人构建阻塞，但 `BuildEditor`/`BuildGame`/`PackageWin64`（UAT 单例，跨工作区打包须串行）仍受互斥约束。同机并发红线：无头编辑器进程 `<= 3` 个，活跃 UBT 构建进程（`dotnet ... UnrealBuildTool.dll`）`>= 2` 个时先等待，最多 `2` 个会话同时跑重型构建或场景。

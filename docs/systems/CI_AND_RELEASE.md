# CI 与发布

## 打包与产物

```powershell
.\Scripts\PackageWin64.ps1
.\Scripts\OpenPackaged.ps1 -PackageRoot '<本次 Archive 目录>'
```

打包脚本先检查 Demo 源/载荷/contract，再验证 Bootstrap，仅选择前端和 Demo 两张图。
当前缺少完整玩法接线时不应打包；旧 cook/staging 依赖仍须随对应内容闭包迁移，
入口检查不代表最终包已无旧载荷。
成功输出记录独立的 Demo entry contract、产品版本、源输入与包文件摘要。
包内不带用不上的引擎附件：`Lockdown.uproject` 关闭默认启用的 `NNEDenoiser`（连带它拉进的 ONNX Runtime、
DirectML 与降噪模型），脚本删去 UAT 按 Development 配置一律放入、但 D3D12 包从不加载的 Vulkan 校验层。
`OpenPackaged.ps1` 拒绝没有对应 manifest、包含旧 map set 或可执行文件摘要不符的包。
打包完成不代表运行、联机或新 Demo 验收已经通过。
保留的发布构建按 [AGENTS](../../AGENTS.md) 的 known-working 规则管理；
运行日志按本次问题取用，不提交大量快照到 docs，也不自动删除别人的工作产物。

## 产品版本

产品版本是 `Config\DefaultGame.ini` 的 `ProjectVersion`：加入房间按它校验，`PackageWin64.ps1` 也从这里读取。
`main` 上它只是版本线（有人经 PR 改 minor/major 时才变），已发布的版本以 tag 为准：每日打包在本地版本提交里写入
发布版本和 `[Lockdown.Build] bReleaseBuild=True`，只随 tag 推送。标题页显示“版本 X.Y.Z (<短提交>)”（提交读不到时为 `unknown`），
只有该标志为 True 的构建显示裸版本号；其他构建（编辑器、本地打包）写成“版本 X.Y.Z-dev (<短提交>)”（打包自有未提交改动的工作区时注明），以免被当成某个发布版。
`main` 上的标志必须保持 False，每日打包的 `plan` 遇到 True 会拒绝。
它不同于 `Lockdown.uproject` 的 `FileVersion`/`EngineAssociation`，也不同于协议、目录与枚举版本。

## PR 检查与每日试玩包

`.github/workflows/pr-check.yml` 在指向 `main` 的非草稿 PR 每次推送（以及转为 Ready for review）时运行，
同一 PR 的新推送会取消未完成的旧运行。按整个 PR 相对 base 的差异分流：只改 `docs/**` 或 `*.md` 时，
仅运行轻量 `scope`，`python` 与 `native` 都跳过；混有代码、数据或工作流改动时仍运行相应检查。它分三个任务：
- `scope`（GitHub 托管）：`Scripts/CIChangeScope.py` 按 PR 合并提交相对 base 的改动选择 `docs|python|native` 门槛。
  改到 `Source`、`Config`、`SourceData`、`Content`、PowerShell 脚本或 `RunTests.ps1` 加载的 Python 脚本
  （及其在 `Scripts` 内的全部 import）时需要原生门槛；只改其他 `Scripts/*.py` 或工作流时不需要。
  它同时输出 `import_tests=run|skip`：数据导入测试的输入（见[开发循环与验证节奏](VERIFICATION.md#数据导入测试组)）没有改动时，
  原生任务以 `RunTests.ps1 -ImportTests Skip` 运行其余全部必需测试。
- `python`（GitHub 托管 Windows，非纯文档 PR 才跑）：`Scripts/RunPythonTests.py` 以最多 4 个进程并行执行
  `Scripts` 下的 Python 测试（与 `unittest discover` 同一组测试和通过规则，带类级夹具的测试类整体留在一个进程里）。
  工作进程不是守护进程，测试可以再开进程：`ExportDemoNativeFixtures.py` 默认按 CPU 数并行编译夹具用例
  （`--jobs` 可调，输出字节与串行一致），`RunTests.ps1` 经 `--cache` 走同一导出。
  其中 `TestRuleReport.py` 核对源码注册与必需集合一致，跳过数据导入组的原生运行依赖这一点发现漏登记的测试。
  不需要 Unreal 与 LFS 内容；本地也可直接运行 `python -B .\Scripts\RunPythonTests.py`。
  每次运行的完整输出另存到 `Artifacts\PythonTests\<时间>-<pid>.log`（保留最近 10 个），输出最后一行 `Log:` 给出路径；
  失败的测试 id 记在 `Artifacts\PythonTests\last-failed.json`，每次运行覆盖。`--failed` 只重跑这些测试，
  输出标注“PARTIAL RUN (--failed), NOT A GATE”，不能代替 CI 门禁；没有失败记录时报错退出。
  该脚本自身的测试：`python -B -m unittest discover -s .\Scripts -p TestRunPythonTests.py`。
- `native`（维护者本机的自托管 runner，仅在需要原生门槛且分支在本仓库内时）：增量 `BuildEditor.ps1` 与
  `RunTests.ps1`，失败时上传 Artifacts 证据。runner 关闭时它在 GitHub 上排队；fork PR 不在这台 runner 上运行。
  `CIResetWorkspace.ps1` 清空 Artifacts 时保留按输入哈希校验的夹具缓存 `Artifacts\DemoContentFixtureCache`。
它覆盖每个 PR 的最低门槛，不代替改到场景时应跑的 `RunDemo*Check`。

`.github/workflows/nightly-package.yml` 每天 03:00（北京时间）排队一次，也可在 Actions 页手动运行（只能选 `main`）。
它只在维护者本机的自托管 runner（标签 `lockdown-ue58`）上执行，每次运行发布一个试玩版本，
检出的是任务开始时 `main` 的最新提交（不是排队时的提交）：
- `main` 上没有新提交（已有 `vX.Y.Z` tag 指向 HEAD，或指向以 HEAD 为父提交的版本提交）时直接跳过；
  手动运行勾选 `force` 时仍发布下一个 patch。
  检出与 PR check 的 `native` 一样只取深度 1，tag 也按深度 1 单独拉取，判断只读 tag 提交本身，不需要历史：
  两者共用工作区，取全历史会让每次运行都在 PR check 之后重新拉全部历史，这台 runner 的网络上这种长传输会被重置。
- `Scripts/CIReleaseVersion.py` 选版本：`Config/DefaultGame.ini` 的 `ProjectVersion` 还没有 tag 且比最新 tag 新时
  直接使用（有人经 PR 改到新的 minor/major）；否则取两者较新者的 patch+1。
  每次发布都在 `main` 最新提交之上本地提交 `chore: release version X`（写入版本与 `bReleaseBuild=True`），打包这个提交。
  它不推送到 `main`（`main` ruleset 要求 PR，个人仓库也无法把 GitHub Actions 加入 bypass），
  只随 tag `vX` 推送；所以包内版本、清单与 tag 一致，`main` 上的 ini 保持基础版本，已发布的版本以 tag 为准。
- 增量 `BuildEditor.ps1`、`RunTests.ps1`、`PackageWin64.ps1`，核对清单的版本、提交与干净工作区，
  把 Archive、manifest、`Package.log` 与 `README.txt` 启动说明打成 `Lockdown-vX-win64.zip`，
  调试符号 `Lockdown.pdb`（约占解压后包体的三分之一）不进主包，按原相对路径单独打成 `Lockdown-vX-win64-symbols.zip`，
  解压到同一目录即位于 exe 旁；本地 `PackageWin64.ps1` 的输出仍保留它。
  推送 tag `vX` 并以这两个文件发布为 Release `vX`；发布失败时撤回刚推送的 tag，重跑会再次发布。
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

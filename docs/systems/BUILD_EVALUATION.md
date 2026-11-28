# 构筑评估 Harness

DEV 实验工具（非产品模式）：在真实无画面 Listen Host（`-nullrhi -UseFixedTimeStep -FPS=60`）上按可复现种子执行成对/因子构筑实验，输出可重放的权威 `LD_*` 轨迹与成对区间统计，不替产品宣布“平衡通过”。

## 分层与运行入口

| 层 | 位置 | 责任 |
| --- | --- | --- |
| 通用核心 | `Scripts\BuildEvaluation.py` | 清单 v2 校验、能力表准入、块种子展开、磁盘/事件/墙钟预算门禁、成对区间统计、重放比对（不含任何游戏词汇） |
| 玩法 Adapter | `Scripts\BuildEvaluationLockdown.py` | 从 `Content\Lockdown\Data\Demo` 生成能力表（`lockdown_demo` v22）、写原生输入、判读 `LD_*` 权威日志与重放一致性 |
| 原生驱动 | `Source\Lockdown\Private\Tests\LDBuildEvaluationProbe.*`、`Evaluation\LDEvaluation*` | 1 名编排 Host + 2 名服务器 AI 进入 `duel_site` 对决，落盘 `native\results\<case>.json` 与 `native\traces\<case>.log` |
| 唯一入口 | `Scripts\RunBuildEvaluation.ps1` | `plan -> native -> analyze` 及单 Case 重放核验 |

```powershell
.\Scripts\RunBuildEvaluation.ps1 -Manifest Scripts\BuildEvaluationManifests\e3-first-levelup-branches.json
.\Scripts\RunBuildEvaluation.ps1 -Manifest <清单.json> -PlanOnly
.\Scripts\RunBuildEvaluation.ps1 -ReplayRun <运行目录> -Case <case-id>
python -B .\Scripts\BuildEvaluation.py analyze --run-dir <运行目录> --rejudge
python -B .\Scripts\CombineBuildEvaluation.py --run <批次1> --run <批次2> --output <合并结果.json>
```

## 清单、预算门禁与统计边界

- **严格准入（schema v2）**：未知字段或缺失必填预算一律拒绝；能力表外的机制/决策/场景/指标标为 `Unsupported*`，不用近似值替代；目标运行目录非空时直接拒绝写入。
- **硬预算上限（`BUDGET_CEILINGS`）**：`standard` 批最多 `60` Case、单 Case `<= 180 s`、整批 `<= 5400 s`、单 Case 权威事件 `<= 200,000` 行、单批磁盘 `<= 2 GiB`、证据根 `Artifacts\BuildEvaluation` 总磁盘 `<= 6 GiB`；任一门禁触发即停止进程并标 `BudgetExceeded`。
- **统计与重放**：以独立世界种子（双侧交换取均值）为统计单元，仅确认集（`confirm`）主指标产出结论（`RuleViolation / BalanceConcern / NoCounterexampleInCoveredSet / InsufficientEvidence / NoTarget`）。`duel_site` 支持 `natural`、`legal_search` 与 `diagnostic`（可显式预置开局主动、升级牌与经验供给）。`duel_site` 的 `clear_camps: true` 是评估夹具：初始营地站齐后全部营地组离场且本段不再刷新（不产生死亡与奖励，地图与正式营地不变），用于排除出生路线或缠斗漂移卷入营地；未出现唯一的 `LD_EVAL_CAMPS_CLEARED` 判 `CampsNotCleared`。
- **无效样本**：Adapter 把不能代表构筑的 Case 判 `Invalid` 并单列原因，缺任一侧的种子整体不进分母：双方都是走近交战的架势（默认 pursue 或 burst）而到时从未互相命中是 `NoEngagement`（接近或寻路失败，不是删失，原因里写双方最近距离）；学会主动的一方交战至少 `NEVER_CAST_GRACE_MS`（5 s，另加该方 `reaction_ms`）一次未施放是 `NeverCast`（AI 执行失败；设了 `skip_bp` 的一方不判）。交战后到时未分胜负、或 kite/hold/guard/strafe 架势到时未交手，才是删失。两侧属性点预算不同（一方 `keep_points_unspent`）的 arm 在报告限制里写明，差值不能单独归因于构筑。判读规则变化时 Adapter 版本递增，旧证据用 `analyze --rejudge` 重新判读。
- **房主级开关（自动强化）**：`duel_site` 参数 `empower: true`（可加 `empower_host: Basic|Q|W|E|D`）让 `RunBuildEvaluation.ps1` 以 `-LDEmpower`（`-LDEmpowerHost=...`）启动房主，开关记入 `runner.json engine_switches`；记录与场景不符时整批判 `Invalid`。开关作用于整个进程，off/on 对照是同一组种子的两次运行。开启时 `subject_empowers`/`opponent_empowers` 计付费的 `LD_EMPOWER`（applied；prepaid 是先前已付费强化的兑现，只计入 `facts.empower.delivered`），各 Case 的 `facts.empower` 记宿主、处理器、扣费与劲不足次数；关闭的运行出现 `LD_EMPOWER` 判 `Invalid`（Setup）。宿主默认由 AI 自选；计划里某一侧写 `empower_host`（每个 arm 即每组 Case 可不同）时，原生驱动在比赛开始后经权威宿主命令提交，回执 `LD_EMPOWER_HOST` 必须恰好一次且该侧只以该宿主强化，否则判 `Invalid`（`EmpowerHostNotApplied`）；`LD_EVAL_PROFILE empower=off|auto|<宿主>` 必须与运行一致。

## 检查入口

- `python -B -m unittest discover -s .\Scripts -p "TestBuildEvaluation*.py"`、`Lockdown.Rules.BuildEvaluation`。

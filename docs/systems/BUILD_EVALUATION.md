# 构筑评估 Harness

DEV 实验工具（非产品模式）：在真实无画面 Listen Host（`-nullrhi -UseFixedTimeStep -FPS=60`）上按可复现种子执行成对/因子构筑实验，输出可重放的权威 `LD_*` 轨迹与成对区间统计，不替产品宣布“平衡通过”。

## 分层与运行入口

| 层 | 位置 | 责任 |
| --- | --- | --- |
| 通用核心 | `Scripts\BuildEvaluation.py` | 清单 v2 校验、能力表准入、块种子展开、磁盘/事件/墙钟预算门禁、成对区间统计、重放比对（不含任何游戏词汇） |
| 玩法 Adapter | `Scripts\BuildEvaluationLockdown.py` | 从 `Content\Lockdown\Data\Demo` 生成能力表（`lockdown_demo` v15）、写原生输入、判读 `LD_*` 权威日志与重放一致性 |
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
- **统计与重放**：以独立世界种子（双侧交换取均值）为统计单元，仅确认集（`confirm`）主指标产出结论（`RuleViolation / BalanceConcern / NoCounterexampleInCoveredSet / InsufficientEvidence / NoTarget`）。`duel_site` 支持 `natural`、`legal_search` 与 `diagnostic`（可显式预置开局主动、升级牌与经验供给）。

## 检查入口

- `python -B -m unittest discover -s .\Scripts -p "TestBuildEvaluation*.py"`、`Lockdown.Rules.BuildEvaluation`。

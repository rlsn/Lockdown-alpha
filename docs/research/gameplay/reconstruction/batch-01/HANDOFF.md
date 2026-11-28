# Batch 01 Handoff

新 Session 恢复工作只需读：本文件 → [STATUS.md](STATUS.md) → [TASKS.md](TASKS.md) → [../DECISION_REGISTER.md](../DECISION_REGISTER.md)。
原始指令：[CHARTER.md](CHARTER.md)（第一道）；第二道指令（Momentum + R）的决定已写入 DECISION_REGISTER M-01…M-08，研究工作区见 [momentum-r/README.md](momentum-r/README.md)。研究输入：`docs/research/gameplay/*.md`（主检出目录，未跟踪）。

## 位置

- 主检出（S0 Coordinator / 集成者）：仓库主检出，已构建。
- 研究文档、本工作区、`Scripts\GameplayResearchLab.py` 均为主检出中的**未跟踪文件**；worktree 中不存在，按绝对路径读取。
- 子 Session 的设计研究产出写入 `docs\research\gameplay\reconstruction\batch-01\research\<session>\`（各写各的目录）。

## 文件所有权（同一文件只有一个写入者）

| 所有者 | 文件 / 范围 |
| --- | --- |
| S0 | `reconstruction/` 下 ROADMAP、DECISION_REGISTER、batch-01 的 STATUS/TASKS/DESIGN_DECISIONS/VALIDATION/HANDOFF；集成分支 |
| S1 | `Combat/LDCombatCards.cpp`、`LDCombatElementsDirector.cpp`、`LDCombatSkillDirector.cpp`、`LDCombatActions.cpp`、`LDCombatBranchDirector.cpp`、`LDCombatControlRules.cpp`、`World/LDFieldPacing.cpp`、`LDGameModePacing.cpp`、`LDCaptureReducer.cpp`、`LDGameModeShowdown.cpp`、`LDChoicesPanel.cpp`、`LDProgressionRules.cpp`、`LDMatchTimeline.cpp` 及对应测试 |
| S2 | `LDMapRegistry.cpp`、`LDCampLifecycle.cpp`、`LDMapTraversalAuditTests.cpp`、`AI/*`（`LDOpponentPolicy/Frame`）、`LDCombatObservation.cpp`、`LDBreakoutPresets.cpp`、`Scripts/BuildEvaluationManifests/*` 及对应测试；`L_Demo.umap` 只报告不编辑 |
| S5 | `Scripts/SummarizePlaytestLog.py`、`SummarizeOpponentTelemetry*`、`RunBuildEvaluation.ps1` 报告/样本标注、主检出中未跟踪的 `GameplayResearchLab.py`/`TestGameplayResearchLab.py` |
| S3 / S4 / S5 / MR 构筑融合 | 只读源码；只写各自 `research/` 或 `momentum-r/<子目录>/`；可写隔离实验脚本于该目录 |
| S1（MR） | 删除分支 `sinmentis/b01-mr-removal`（不并入基线直至迁移获批）；`momentum-r/removal/` |
| 共享 | `Scripts/CheckRuleReport.py` `REQUIRED_TESTS`：S1/S2 各自登记新增测试，冲突由 S0 合并时解决。跨所有者文件先消息 S0。 |

## 共享机器规则（12 线程 / 32 GB）

- 引擎构建全机同时 1 个（`Build.bat` 互斥）；**只有 S1、S2 与 S0（集成时）构建**。S3/S4/S5 需要构建或原生运行先向 S0 申请。
- 无头编辑器进程（规则测试、无画面场景、BuildEvaluation）全机 ≤ 3；渲染运行 ≤ 2；打包/帧率只在集成分支由 S0 做。
- 新 worktree 需：从主检出复制 `.env`（`LD_UE_ROOT`），`git lfs pull`，首次构建 3–6 分钟。
- 构建/测试放后台，不轮询；每批改完只验证一次（AGENTS.md「验证与交付」）。

## 汇报约定

子 Session 只在一批就绪、被阻塞或需要 PO 决策时消息 S0；消息写：任务 ID、分支与提交、验证结果（通过/失败/未运行分开）、新增测试名、REVIEW 拆分项。

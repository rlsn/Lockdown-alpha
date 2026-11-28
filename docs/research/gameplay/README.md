# Lockdown 游戏性全面研究与重构工作区 (Gameplay Research & Redesign Workspace)

本目录汇总 Lockdown `v0.14.2`（Commit `e62ff30473ce`）独立游戏性审计、构筑流派分类、机器可读交互图谱、经济与滚雪球模型、战斗与终局目标评估、外部十款标杆游戏一手研究、自动化实验工坊与分级实施路线图。

## 交付物导航

| 文档 / 数据产物 | 核心内容 |
| --- | --- |
| **[EXECUTIVE_SUMMARY.md](EXECUTIVE_SUMMARY.md)** | **执行摘要（两页决策版）**：直接回答核心乐趣、五大严重问题、系统存废清单、最优先三大实验、好玩判定标准与真人试玩边界。 |
| **[GAMEPLAY_AUDIT.md](GAMEPLAY_AUDIT.md)** | **全面游戏性审计**：真实运行基线、全链路 Gameplay System Map（严格六态标注）、三套核心循环替代方案、新手认知负荷与赛后复盘诊断。 |
| **[BUILD_ARCHETYPE_MATRIX.md](BUILD_ARCHETYPE_MATRIX.md)** | **构筑生态与流派矩阵**：12 大真实流派（可用/质变/破格三阶定义、克制与转型枢纽）、伪流派/断头流派清单、5 套选牌机制蒙特卡洛实验（15,000 局）。 |
| **[GAMEPLAY_INTERACTION_GRAPH.md](GAMEPLAY_INTERACTION_GRAPH.md)** | **卡牌与跨系统交互图谱分析**：真实 C++ 事件总线拓扑、6 大反馈闭环、实机捕获的 `A12×A09` 假人指数爆炸风暴与窄门/孤儿词条审计。 |
| **[ECONOMY_AND_PROGRESSION_REPORT.md](ECONOMY_AND_PROGRESSION_REPORT.md)** | **经济、PvE 与滚雪球报告**：`L_Demo.umap`（64 营地）与 `world.json`（34 营地）分裂核验、野怪效率倒挂、单一货币“关键石惩罚税”与雪球放大链路。 |
| **[COMBAT_AND_OBJECTIVE_REPORT.md](COMBAT_AND_OBJECTIVE_REPORT.md)** | **战斗、元素、FFA 与终局报告**：Unreal 5.8.4 原生 60Hz 44 组对抗实测、移速/闪避/爆劲负收益剖析、10 反应与地面断供评估、5 种终局胜利条件 8p 锦标赛对照。 |
| **[EXTERNAL_BENCHMARKS.md](EXTERNAL_BENCHMARKS.md)** | **外部标杆游戏一手研究**：Battlerite、LoL Arena、Eternal Return、Supervive、Hades、Noita、Backpack Battles、Brotato、Slay the Spire、Dota 2 的一级证据（A/B/C 分级）与结构迁移边界。 |
| **[GAMEPLAY_EXPERIMENT_LAB.md](GAMEPLAY_EXPERIMENT_LAB.md)** | **自动化游戏性实验工坊与证据台账**：L1–L4 实验架构、8 大公平视野策略 Bot + 离线先知上界定义、EXP-01..06 完整复现命令与 8 人真人盲测协议。 |
| **[REDESIGN_PROPOSALS.md](REDESIGN_PROPOSALS.md)** | **实验重构提案全集**：四大核心议题各自的 A（最小）/ B（中等）/ C（激进）/ D（删除合并）方案，内置独立审查 Agent 6 的逆向反证护栏与回滚策略。 |
| **[IMPLEMENTATION_BACKLOG.md](IMPLEMENTATION_BACKLOG.md)** | **优先级实施 Backlog（P0–P3）**：逐项列明问题、实锤代码/日志证据、设计假设、修改范围、成本、依赖、风险、可证伪验收标准与回滚条件。 |
| **[data/interaction_graph.json](data/interaction_graph.json)** | **机器可读交互图谱数据**：从 `SourceData\Demo\content.json` 提取的 `133` 节点、`542` 有向边、总线供需与反馈闭环 JSON。 |
| **[data/experiment_results.json](data/experiment_results.json)** | **可复现模拟实验结果集**：固定跨进程确定性种子 `20261128` 下的选牌、战斗矩阵、人数敏感度与终局方案对比完整输出。 |

## 配套可执行实验代码

- `Scripts\GameplayResearchLab.py`：确定性游戏性实验与图谱提取引擎（只读消费正式 `SourceData` 与 `Content` 载荷，不污染正式游戏逻辑）。
- `Scripts\TestGameplayResearchLab.py`：实验引擎对齐与跨进程可复现性单元测试。

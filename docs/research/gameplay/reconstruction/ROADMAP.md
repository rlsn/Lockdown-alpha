# Gameplay Reconstruction Roadmap

本地未跟踪工作区（不提交到 `main`；仓库规定任务进度不进仓库文档）。研究基线 `e62ff30473ce`（v0.14.2）。
原始指令见 [batch-01/CHARTER.md](batch-01/CHARTER.md)。

| 批次 | 范围 | 状态 |
| --- | --- | --- |
| **Batch 01 — Technical Foundation & Combat** | A01 运行稳定性 · A02 玩法正确性 · A03 地图导航 · A04 AI 对手 · A05 工程测试 · A06 基础战斗 · A07 闪避机动 · A08 爆劲 · A09 战斗平衡 | **进行中** |
| Batch 02 — Build Ecosystem & Progression | A10 构筑流派 · A11 卡牌设计 · A12 选牌随机 · A13 属性投资 · A14 元素系统 | 未开始（禁止提前启动） |
| Batch 03 — World Ecology & Match Loop | A15 PvE 生态 · A16 地图争夺 · A17 滚雪球与翻盘 · A18 对局结构 | 未开始 |
| Batch 04 — Final Showdown & Integrated Experience | A19 终局 · A20 玩家体验 · 跨系统集成 · 全局平衡 · 真人试玩 | 未开始 |

## 边界规则

- 只执行当前批次；发现后续批次的问题，写进下方「后续批次 Backlog」（问题 / 证据 / 依赖），不扩大范围。
- 例外：确定性运行态 Bug、稳定性、性能、测试基础设施错误可跨系统修复。
- 本批次不做：卡牌/构筑生态重构、选牌稀有度、元素玩法、经济架构、PvE 生态与营地刷新、中场 PK、对局阶段、终局重设计、大规模 UI 美术、新地图/角色。

## 后续批次 Backlog

| 目标批次 | 问题 | 证据 | 依赖 / 备注 |
| --- | --- | --- | --- |
| B02 | A12 叠层 / A09 追击是否需要正式玩法上限（研究报告建议“每秒 5 层、单拳 4 段”，未批准） | `IMPLEMENTATION_BACKLOG.md` P0-1 | Batch 01 只修事件因果与技术预算 |
| B02 | 商店津贴 / 双货币提案 | `IMPLEMENTATION_BACKLOG.md` P1-1 | **已被产品原则 3.2 否决**，Batch 02 不得以津贴形式重提 |
| B02 | Ⅲ阶勋章自触发反应的玩法重做（元素共鸣） | `COMBAT_AND_OBJECTIVE_REPORT.md` §3 | 确定性映射是否为 Bug 由 A02 判定 |
| B02 | 反应公式缺少额外 AD 加成 | 同上 §3 | 元素玩法 |
| B03 | 地图 64 营地 vs 规格 34 营地分裂 | `ECONOMY_AND_PROGRESSION_REPORT.md` | A03 只记录与修可达性，不改数量 |
| B03 | 休息窗 / PK / 押注时长 | `COMBAT_AND_OBJECTIVE_REPORT.md` §4.1 | 0 观众空等是否 Bug 待 A02 核规格 |
| B04 | 终局单旗几何死锁、私人旗杆/篡位 | 同上 §4.2 | Batch 01 只修 GuardRatchet / 超时结算 |
| 关卡负责人 | 营地 h10 横在西南入场区通往旗杆的通道上（c001 种子）；#353 剩 h08_0 高台、h09_0 孤立导航岛、h09 锚点 Z=867 与成员 Z 500 不同层；是否允许怪放在高台上 | `research/S2/A03_MAP_NAV_REPORT.md` | 不改地图，等负责人 |
| 关卡负责人 | (682, 2020~2040) 地面上方 54–71 cm 有一层台面，导航分层很紧（Boss 曾卡住，已在代码侧修复） | S2 `b164c94c` | 知会 |
| 关卡负责人 | capture 西南侧 `StaticMeshActor_80` 约 (−1427, 482, 502) 附近击退可把角色推入静态碰撞 | S2 诊断 | 代码侧由 S1 修 T02-22 |

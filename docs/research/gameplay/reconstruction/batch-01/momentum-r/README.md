# Momentum + Build-Driven R 研究工作区

PO 第二道指令（Batch 01 内）：删除通用长按爆劲与通用精准闪避，研究劲的存在价值与构筑驱动的 R 系统。
指令原文由 S0 经应用原文转发给各 Session；决策摘要见 [../../DECISION_REGISTER.md](../../DECISION_REGISTER.md)（M-01…）。

## 统一术语

| 术语 | 含义 |
| --- | --- |
| 劲（Momentum） | 现有战斗资源条；本轮重新研究其职责 |
| 通用长按爆劲 | Q/W/E 按住时长 + 劲门槛触发的爆劲（含 0.35s 蓄力、Alt 一键蓄满）——**已批准删除** |
| 通用精准闪避 | 预警 / 擦身精准闪避判定及其减伤、回劲、返还、事件、UI、AI——**已批准删除** |
| 普通冲刺 | D 键多用途冲刺；保留，240 cm/s 基准不变，不加双充能、闪身键、通用无敌帧 |
| R | 统一、简单的新按键；其行为由构筑决定（研究中，未批准） |
| M1–M4 | 劲资源架构候选（共享消耗 / 战斗积累 / 混合 / 状态型） |
| R1–R5 | R 架构候选（基础强化+扩展 / 可构筑终极技能 / 可构筑状态 / 改变触发对象 / 原创） |
| C1–C6 | R 构筑权来源（卡牌 / 关键石 / 技能分支 / 显式核心选择 / 涌现 / 混合） |

## 分工与写入目录（各写各的目录）

| 角色（指令 §13） | 执行 Session | 目录 |
| --- | --- | --- |
| S0 Coordinator | 主会话 | 本文件、`MOMENTUM_R_DESIGN_DECISION.md`、`../*.md` 状态文件 |
| S1 Removal & Migration Engineering | B01 S1（运行稳定与玩法正确性） | `removal/` + 独立删除分支 |
| S2 Momentum Resource Research | B01 S4（原冲刺闪避与爆劲研究） | `momentum/` |
| S3 R Gameplay Architecture | B01 S3（原基础战斗研究） | `r-architecture/` |
| S4 Build Integration & Emergent Design | 新建 Session | `build-integration/` |
| S5 External Research & Critique | B01 S5（评估性能与反证） | `critique/` |

B01 S2（导航与 AI）继续技术基线工作；AI 中的精准闪避判断由 S1 列入迁移清单，经 S0 协调后改动 `AI/*`。

## 授权边界

- 已批准：删除通用长按爆劲、删除通用精准闪避、确定性修复、工程与测试优化、隔离研究与必要原型。
- 未批准：新劲架构、普通技能劲消耗修改、R 具体行为与消耗、构筑与 R 绑定、受影响卡牌替代设计、新数值、新正式玩法系统。
- 删除工作在独立分支完成；在 R 机制确定并完成关联迁移前，不并入可玩默认版本（不得出现无效卡牌、错误说明或失效构筑）。

## 最终交付

`MOMENTUM_R_DESIGN_DECISION.md`（S0 汇总，中文，七部分：执行摘要 / 劲资源 3–5 架构 / R 3–5 架构 / ≥8 真实构筑 / 三套综合候选 / 迁移计划 / ≤5–8 个 PO 决策问题）。

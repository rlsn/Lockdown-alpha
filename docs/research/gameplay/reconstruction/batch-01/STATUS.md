# Batch 01 Status

- **阶段**：Step 5 前段 — 已批准战斗方向的实施准备（C-09…C-14）+ 技术基线集成；劲 / R 暂停（M-09）
- **当前基线**：`main` @ `e62ff3047`（= 研究基线 `e62ff30473ce`，HEAD 未变）
- **集成分支**：`sinmentis/b01-integration`（主检出），已合入 S1 基线（至 `398d2928`）与 S5（`sinmentis-miniature-guide`）；等待 S2 后统一构建 + Clean Baseline

## Sessions

| Session | 范围 | 分支 / 工作区 | 状态 |
| --- | --- | --- | --- |
| S0 Coordinator | 范围、审批、集成、持久状态 | 主检出 `main`；session `8b79a6e8` | 运行中 |
| S1 Runtime & Correctness + MR 删除迁移 | A01, A02, MR-01, MR-02 | worktree；session `9780f7d1-db7e-4fd4-a3f7-1cd33b7a0fbf` | 运行中 |
| S2 Nav & AI | A03, A04 | worktree；session `d41026a8-3cd9-4e30-9943-79dd68d4ac14` | 运行中 |
| S3 → MR R 架构研究 | MR-05（原 A06/A09 已交付） | worktree（不构建）；session `dd81dd96-51f7-461e-9891-ee89e6deb473` | 运行中 |
| S4 → MR 劲资源研究 | MR-04（原 A07/A08 已交付） | worktree（不构建）；session `068eb853-4577-4b4d-9101-15018a510df3` | 运行中 |
| S3b 战斗补充研究 | S3B-01..07 | worktree（不构建）；session `b1b6fad8-8e1c-4dfc-95d4-353ecb17199b`；写 `research/S3/` | 运行中 |
| MR 构筑融合 | MR-06 | worktree（不构建）；session `b4eb7cf4-93fb-40f9-b68f-f81462de4dba` | 运行中 |
| S5 Eval + MR 外部研究与反证 | A05、MR-07、MR-08 | worktree（构建/原生运行须申请）；session `124d6548-060a-41e6-8094-47ba9293b046` | 运行中 |

## 已完成（集成分支 `sinmentis/b01-integration`，本地未推送）

- 技术基线：A12×A09 风暴、F2/F3、9 项卡牌 / 元素 / 终局确定性缺陷、#353 部分与 c001 导航（AI 绕行）、AI 施法 / 预警 / 躲避 / 缩圈 / 回复球 / 换牌、Boss 卡死与刷屏、击退落入静态碰撞、评估设施（无效样本、清场、横移、反应 / 预判旋钮）。
- 删除通用长按爆劲与精准闪避；Y11/D04/D09/U07 删除（M-01/M-02/M-10/M-11）。
- 已批准战斗规则：C-12 前摇落点预警 + 你被锁定；C-09/C-15 四个分支可读起手 + 公开承诺环；C-11 打断 50% 冷却。
- 原型（开关，不进默认）：急眼了 `-LDProtoAgency`（+ 判据探针）；S08 2B 延迟 `-LDProtoPotDelay`。
- 自动强化 M-12（开关 `-LDEmpower` / `EMPOWER.Enabled`，默认关）：宿主印记、五类处理器 + 形态补丁 ①–⑦、防递归、HUD、AI、遥测、评估支持。
- 原生证据：见 VALIDATION.md（BotMatch 多轮、X1/X2 两轮 + 追加、empower off/on、GrowthLink、渲染 CombatCheck 关 / 开）。

## 未完成 / 阻塞

- 完整规则套件未跑：本批影响约 556 项规则测试，超出本地预算，须推送集成分支并以非草稿 PR 交 CI（等 PO 同意推送）。
- 关卡负责人：营地 h10 堵西南入场通道；#353 余 3 点；高台怪是否允许；StaticMeshActor_80 附近复核（ROADMAP 记录）。
- S2 进行中：c001 的 PresetNotApplied；AI 宿主打分让冲刺 / 坦克样板选 D / 守类。
- 渲染未覆盖：落点预警、你被锁定、承诺环、对手强化就绪、急眼了标记的实际观感（需定向截图或真人会话）；X3 前摇中移动手动检查未做。
- PO 待决：急眼了判据（≤1.5 s 用 T1.3/W3/嘲讽1.0，代价普通围攻在 1.42 s 触发；或放宽到 ≤1.6 s 用 T1.4/W2.5/嘲讽1.0，普通围攻不受影响）；R12（擂台胜者须在场）。
- 自动强化转默认前：Batch 02 卡牌迁移（K08/K12/D08/脑力关键石、劲收入牌、急速约束）、拳宿主频率高于预测（进场劲 75.5）、守 / 动处理器未经 AI 验证。

## 下一步

1. PO：急眼了判据；是否推送集成分支并开 PR 交 CI 跑完整规则套件。
2. S2 完成 AI 宿主与 PresetNotApplied 后合入；一次定向渲染截图覆盖新视觉。
3. Batch 01 收尾报告；不自动进入 Batch 02。

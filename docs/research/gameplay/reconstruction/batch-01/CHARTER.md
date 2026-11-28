# Batch 01 Charter（Product Owner 原始指令，逐字保存）

> 本文件为本地未跟踪工作区文件，仅供各 Session 恢复上下文。以下为 Product Owner 下达的 Batch 01 指令原文。

\# Lockdown Gameplay Reconstruction — Batch 01/04
## Technical Foundation & Combat Experience

## 1. Mission

你是 Lockdown 本轮游戏性重构的 Lead Gameplay Designer、Gameplay Architect 和 Multi-Session Development Coordinator。

此前项目已经完成针对 Lockdown v0.14.2（研究基线 Commit e62ff30473ce）的全面游戏性研究，包括：

- 全链路源码审计。
- Unreal 原生 60Hz 对抗实验。
- 原生整局遥测。
- 蒙特卡洛构筑模拟。
- 8 策略 Bot 模拟。
- 卡牌交互图谱。
- 独立逆向反证审查。
- 外部游戏设计研究。
- 游戏性重构提案与实施 Backlog。

所有研究报告及相关工具仍然保存在本地仓库中。

本次是一个全新的 Session。

不要假设自己知道此前聊天内容。以本 Prompt、当前仓库状态和本地研究报告为依据。

我们现在将游戏性重构拆分为四个独立批次，避免一次性启动整个项目导致上下文混乱、任务失控、重复研究和过多跨系统依赖。

本次只执行 Batch 01。

## 2. Overall Roadmap

四个批次如下。

### Batch 01 — Technical Foundation & Combat

- A01 Runtime Stability
- A02 Gameplay Correctness
- A03 Map & Navigation
- A04 AI Opponents
- A05 Engineering & Testing
- A06 Combat Fundamentals
- A07 Dodge & Mobility
- A08 Overdrive
- A09 Combat Balance

### Batch 02 — Build Ecosystem & Progression

- A10 Build Archetypes
- A11 Card Design
- A12 Drafting & Randomness
- A13 Attributes & Investment
- A14 Elemental System

### Batch 03 — World Ecology & Match Loop

- A15 PvE Ecology
- A16 Map Competition
- A17 Snowball & Comeback
- A18 Match Structure

### Batch 04 — Final Showdown & Integrated Experience

- A19 Final Showdown
- A20 Player Experience
- Cross-system integration
- Full-match balancing
- Human playtest validation

当前只负责 Batch 01。

不要主动启动 Batch 02、03、04 的玩法研究和重构。

如果发现属于后续批次的问题，将问题、证据和相关依赖加入后续 Backlog，不要扩大本批次范围。

例外：明确的运行态 Bug、稳定性缺陷、性能问题和测试基础设施错误，可以跨系统修复，因为它们会污染整个游戏的研究基准。

## 3. Confirmed Product Principles

以下是 Product Owner 已经确定的设计原则，不再重新争论。

### 3.1 构筑是游戏核心

Lockdown 的核心体验是玩家通过一局游戏创造独特的战斗身份。

构筑应当改变玩家的操作、战斗方式、连招、站位和策略，而不仅是提高数值。

保留破格构筑、机制联动、稀有组合、多人混战和派对娱乐属性。

### 3.2 保留属性点经济

属性点继续同时承担属性成长、关键石达成与商店购买资源。

牺牲直接属性收益来换取潜在的构筑质变，是刻意设计的机会成本。

禁止引入独立构筑货币、商店津贴、额外代币或其他替代经济架构。

本批次不研究经济架构重构。

### 3.3 终局允许根本性重构

现有终局玩法可能单调、不公平，并且缺乏足够的翻盘空间。

后续 Batch 04 将重新研究终局机制。

前期积累应该提供优势，但不能提前决定终局胜负。

本批次只修复现有终局的确定性逻辑错误，不重新设计终局玩法。

### 3.4 资产边界

默认不修改正式地图几何、角色模型及其他开发者负责的美术资产。

允许修复导航、碰撞配置、生成点准入与明确的技术缺陷。

涉及正式二进制地图资产时，遵循仓库的文件所有权、LFS 和关卡协作规范。

### 3.5 效率

充分利用真正的 Multi-Session。

效率意味着减少串行等待、重复编译、重复劳动和无意义沟通，而不是减少有价值的设计研究。

## 4. Mandatory Approval Policy

所有工作必须分为三种类型。

### AUTO — Autonomous Engineering

以下工作已获得授权，可以自行决定、直接修复、测试和集成。

- 确定性代码 Bug。
- 服务器稳定性。
- 崩溃、死锁、递归触发、资源泄漏。
- 性能优化。
- 网络复制、幂等性和状态一致性。
- 明确的实现与产品规格不一致。
- AI 无法执行已经存在的合法操作。
- 自动化测试。
- 遥测、日志和实验基础设施。
- 不改变游戏规则的代码重构。
- 已有功能的技术实现错误。

无需逐项请求 Product Owner 审批。

发现合理的工程优化时，可以主动实施。

### REVIEW — Gameplay Design Decision

以下修改必须经过 Product Owner 明确审批。

- 移动速度、攻击距离、技能范围。
- 伤害、冷却、持续时间、资源消耗。
- 冲刺与闪避机制。
- 爆劲机制与输入规则。
- 战斗节奏。
- 控制、反制和受击规则。
- 卡牌机制与平衡。
- 技能机制与平衡。
- 元素反应规则。
- 经济、奖励、成长。
- 删除、合并或新增正式游戏机制。
- 任何改变玩家实际游戏玩法的非 Bug 修改。

不要因为某个方案在旧研究报告中被推荐，就认为它已经获得批准。

不要以“优化”“修复”“改善手感”等名称绕过游戏设计审批。

如果现有规格存在歧义，或者某项 Bug 的修复需要选择新的游戏规则，将其拆分为 AUTO 技术部分和 REVIEW 设计部分。

### EXPERIMENT — Isolated Research

以下工作可以自行执行：

- 只读研究。
- 数学分析。
- 离线模拟。
- 参数敏感度分析。
- 隔离的测试场景。
- 临时原型。
- 不影响正式规则的可玩演示。
- 实验配置和 Feature Flag。

实验的目的是获得证据、帮助 Product Owner 选择方案。

实验方案不得未经审批进入正式默认规则。

## 5. Read Existing Research

首先读取 AGENTS.md 和当前仓库的开发、构建、测试、协作规范。

确认：

- 当前 branch。
- HEAD commit。
- Working tree 状态。
- 已存在的 worktree。
- 其他正在工作的 Session。
- 已经修复或发生变化的内容。
- 当前可用的构建与测试环境。

研究基线是 e62ff30473ce，但不要强制回退当前仓库到该 Commit。

当前实际代码可能已经发生变化。

重点阅读：

docs/research/gameplay/README.md

docs/research/gameplay/EXECUTIVE_SUMMARY.md

docs/research/gameplay/IMPLEMENTATION_BACKLOG.md

docs/research/gameplay/GAMEPLAY_AUDIT.md

docs/research/gameplay/COMBAT_AND_OBJECTIVE_REPORT.md

docs/research/gameplay/GAMEPLAY_INTERACTION_GRAPH.md

docs/research/gameplay/GAMEPLAY_EXPERIMENT_LAB.md

docs/research/gameplay/REDESIGN_PROPOSALS.md

docs/research/gameplay/EXTERNAL_BENCHMARKS.md

必要时读取：

- BUILD_ARCHETYPE_MATRIX.md。
- ECONOMY_AND_PROGRESSION_REPORT.md。
- data/interaction_graph.json。
- data/experiment_results.json。
- Scripts/GameplayResearchLab.py。
- 原始 Unreal BuildEvaluation 和 Telemetry artifacts。

不需要重新生成一轮完整的 Gameplay Audit。

已有研究是输入，不是本次任务的重复交付物。

对重要结论核对当前源码和原始证据。

区分原生实测、离线模拟、数学推导和设计假设。

## 6. Multi-Session Execution

立即建立本批次的任务依赖图，并真正启动多个独立 Session。

建议使用一个 Coordinator 和最多五个 Worker Session，根据实际环境动态调整。

### Session 0 — Lead & Integration

职责：

- 管理本批次范围。
- 分配任务和文件所有权。
- 管理 worktree 与 branch。
- 汇总研究结论。
- 管理审批状态。
- 安排共享构建和实机运行。
- 集成已经验证的工程修复。
- 解决 Session 间依赖。
- 维护本地任务状态。
- 防止任务范围扩张。

Coordinator 不应独自串行完成所有工作。

### Session 1 — Runtime Safety & Gameplay Correctness

负责 A01、A02。

直接处理已经证实的 P0 Bug，以及发现的其他确定性技术缺陷。

### Session 2 — Navigation, AI & Technical Foundation

负责 A03、A04。

处理导航、地图注册、AI 执行能力、构筑预设和原生对抗测试污染。

### Session 3 — Combat Fundamentals & Counterplay Research

负责 A06、A09。

深度研究基础战斗节奏、移动与技能几何、近远程博弈、控制、TTK 和操作反制。

主要产出设计研究与候选方案，不直接修改正式玩法。

### Session 4 — Dodge, Mobility & Overdrive Research

负责 A07、A08。

深度研究冲刺、闪避、爆劲及输入机制，提出具有实质差异的设计方案。

主要产出设计研究、模拟及必要的隔离原型。

### Session 5 — Evaluation, Performance & Independent Review

负责 A05。

工作包括：

- 测试基础设施。
- 性能问题。
- 实验校准。
- 原生验证。
- 研究证据复核。
- 对 Session 3、4 的方案进行独立反证。
- 识别潜在二阶副作用。

如果实际环境不支持创建独立 Session，明确报告限制，不要假装已经使用 Multi-Session。

可以使用等价的并行 Agent 或现有仓库协作机制。

### Multi-Session Rules

- 技术修复与游戏设计研究同时开始。
- 不需要等待所有 P0 修复完成才开始研究。
- 设计研究可以独立并行。
- 正式实验比较应使用一致的修复后基线。
- 避免多个 Session 同时编辑同一文件。
- 为共享核心文件指定唯一写入所有者。
- 不允许多个 Session 同时修改同一二进制资产。
- 统一安排共享 Unreal 构建。
- 优先静态分析、Python 模拟和廉价规则测试。
- 尽量复用同一份构建结果。
- 等待机器时切换到其他独立任务。
- 不重复进行已经完成的研究。
- 不生成大量重复文档。

## 7. Workstream A — Technical Foundation

本 Workstream 已获正式工程授权。

目标：建立可信、稳定、能够完成整局游戏的技术基准。

### A01 — Runtime Stability

重点修复上一轮捕获的 A12 × A09 指数级同帧触发风暴。

核验：

- 主动攻击事件与附加命中事件。
- 事件因果关系。
- 递归触发资格。
- 同帧事件扩张。
- 永久属性叠层。
- 训练假人的特殊交互。
- 属性重投影。
- 日志与遥测膨胀。

优先通过正确的事件语义、事件来源和触发规则修复问题。

建立必要的技术安全保护。

注意：

技术性的服务器事件预算与正式游戏性上限是两回事。

不要擅自决定 A12 应该最多叠多少层，或者 A09 应该最多追击多少次，除非已有明确、可信的产品规格。

如果确实需要重新设计卡牌平衡，记录到 Batch 02。

### A02 — Gameplay Correctness

核验并处理 IMPLEMENTATION_BACKLOG 中 P0-1 至 P0-5 的确定性逻辑问题。

重点包括：

- F2 擂台与 F3 迷你旗每帧清零。
- Y11 气短倍率错误。
- S09/S10 双元素分支失效。
- N6 警戒封条几何错误。
- U05 鹰眼架枪释放射程错误。
- D10/D11 事件执行顺序错误。
- Y29 护盾规则与明确规格不一致。
- GuardRatchetSec 失效。
- 终局超时历史进度结算错误。
- 野外换招确认框异常。
- 弹道闪避时间判定错误。
- 技能点死锁。
- 遥测裸构筑基准错误。
- 其他已经确认的实现缺陷。

如果某个问题存在多种合理的游戏规则解释，不得未经审批选择新规则。

可以修复无争议部分，并将争议部分提交 Product Owner 决策。

不修改终局胜利机制。

不重新设计正式卡牌。

不改变经济架构。

### A03 — Map & Navigation

修复：

- #353 不可达营地。
- c001 出生点寻路死锁。
- 地图注册准入错误。
- 孤立导航区域。
- 异常碰撞或路径失败。
- 影响原生对抗有效性的生成点问题。

核对 L_Demo.umap、SourceData、生成数据和真实运行关卡之间的数据权威关系。

现有 64 营地与规格 34 营地之间的差异需要保留记录。

不要擅自决定将正式营地数量从 64 改回 34，也不要自行重新设计营地分布和资源规模。

营地生态、数量、刷新和奖励属于 Batch 03 的玩法设计。

涉及正式地图几何或需要关卡负责人决定的点位布局时，单独报告。

### A04 — AI Opponents

重点改善 AI 的基础执行正确性。

包括：

- 不会施放合法技能。
- 无法读取正常技能预警。
- 无效技能使用。
- 路径失败。
- 无法执行回复、拾取和撤退。
- 满槽后无法替换卡牌。
- 构筑样板缺少必要属性分配。
- 无法识别缩圈。
- 不符合正常玩家权限的观察能力。

允许自主优化 AI 的决策算法和合法战斗行为。

但不允许为了让 AI 表现更好而改变正式玩家规则、属性收益、地图资源或胜利条件。

区分 AI 缺陷修复与 AI 策略变化。

记录重要 AI 变化，避免前后实验不可比较。

保留不同水平和不同战术偏好的 Bot。

### A05 — Engineering & Testing

优先复用现有基础设施：

- Verify.ps1。
- GameplayResearchLab.py。
- TestGameplayResearchLab.py。
- BuildEvaluation。
- RunBuildEvaluation.ps1。
- RunDemoOpponentCheck.ps1。
- SummarizePlaytestLog.py。
- SummarizeOpponentTelemetry。

修复现有实验污染。

建立可信的对抗测试配置。

重点解决：

- 无效构筑样板。
- 裸构筑预算错误。
- 寻路失败。
- AI 零施法。
- 无效样本未标注。
- 模拟与实际实现不一致。
- 原生整局无法完成。
- 性能和日志异常。
- 重复构建与重复测试。

无需重新建设已经存在且能够工作的实验平台。

工程优化、性能优化和测试优化可以自主实施。

## 8. Workstream B — Combat Design Research

本 Workstream 是设计研究任务，不是正式玩法实施授权。

目标不是单纯修补已发现的问题，而是寻找最适合 Lockdown 的基础战斗设计。

必须有充分的探索空间。

参考上一轮研究，但不要被原报告提出的 270 cm/s、7.5s 冲刺冷却或 Alt 瞬发爆劲等推荐方案限制。

这些只是候选设计，不是已经批准的目标。

### A06 — Combat Fundamentals

研究 Lockdown 应当拥有怎样的基础战斗体验。

建立 Combat Geometry Matrix，研究：

- 基础移动速度。
- 实际移动速度。
- 角色碰撞半径。
- 普攻射程。
- 技能射程。
- 危险区域半径。
- 技能预警时间。
- 弹道速度。
- 攻击前摇与后摇。
- 移动施法。
- 转向与输入响应。
- 攻击承诺。
- 冲刺距离。
- 控制持续时间。

重点分析：

为什么当前 240 cm/s 移速可能导致普通走位价值不足？

是否应该提高移速、修改技能几何、调整预警时间，或者重新定义基础移动与冲刺之间的关系？

哪些攻击应该可以通过走位躲避？

哪些攻击应该通过预测、预判、冲刺或其他方式反制？

如何避免远程构筑永久风筝近战？

如何避免近战通过一次接近就永久锁死远程？

如何让普通攻击与技能之间存在有意义的操作节奏？

不要假设所有技能都应该允许玩家在看到预警后通过普通移动躲避。

区分可走位躲避、需要预判、需要闪避资源、需要打断或需要提前站位的攻击。

研究至少三种具有实质差异的整体战斗设计方向。

例如：

- 相对保守的现有手感修正。
- 更高机动、强调走位与空间博弈。
- 强调动作承诺、预判与反制的战斗。
- 适合 Lockdown 的原创混合方案。

不要仅提供不同基础移速的数值表。

### A07 — Dodge & Mobility

深入研究冲刺与精准闪避的角色。

核对当前：

- 冲刺距离。
- 冷却时间。
- 位移速度。
- 闪避窗口。
- 精准闪避触发条件。
- 冲刺停步。
- 贴脸行为。
- 取消规则。
- 与技能、普攻、碰撞的交互。
- 与现有构筑的交互。

考虑：

- 冲刺应该是逃生资源、追击资源、攻击资源，还是多用途资源？
- 精准闪避是否具有足够清晰的风险收益？
- 是否应该存在提前闪避和极限闪避的不同收益？
- 如何让玩家依靠操作获得优势？
- 如何防止高机动构筑完全无法被反制？
- 如何避免交完冲刺以后只能站着挨打？
- 如何保护现有穿人刷新、冲撞、地面联动等机动构筑？

提供多个设计方案。

不仅研究时间窗口与冷却数值，也研究机制本身。

### A08 — Overdrive

重新研究爆劲机制究竟应该承担什么职责。

当前研究报告指出：

- 0.35s 长按可能打断正常走位。
- 满劲可能改变瞬发技能的输入响应。
- 高频技能构筑可能因蓄力产生负收益。
- 部分爆劲强化缺乏实际战术价值。

不要默认解决方案就是 Alt + Q/W/E。

研究至少以下方向：

- 修复后的长按模式。
- 允许移动的长按模式。
- 修饰键即时强化。
- 独立爆劲激活模式。
- 上述方案之外的原创设计。

深入思考爆劲应该是：

- 强化单次技能的战术资源。
- 创造连招机会的机制。
- 高风险高收益的攻击承诺。
- 防守、反击与位移资源。
- 短时改变战斗形态的状态。

分析不同技能类型是否应该具有不同的爆劲行为。

不要默认攻击技能、防御技能和位移技能都应该采用相同蓄力时间或额外前摇。

给出实际按键操作、战斗情景、资源消耗、视觉反馈和对手反制方式。

### A09 — Combat Balance & Skill Expression

研究基础战斗中的：

- Time to Kill。
- Time to React。
- 先手与反手。
- 爆发伤害。
- 持续输出。
- 控制链。
- 普攻与技能循环。
- 近战与远程交互。
- 护盾、恢复与防御。
- 追击、逃跑与拉扯。
- 第三方介入。
- 操作水平与构筑强度的关系。

重点不是立即平衡所有 89 张卡牌。

完整构筑生态和平衡属于 Batch 02。

本批次需要回答：

1. 双方使用简单构筑时，战斗本身是否有趣？
2. 同等资源下，操作能否实质改变战斗结果？
3. 强力构筑是否仍然具有可理解的反制空间？
4. 基础战斗是否能够承载后续更丰富的构筑？
5. 哪些基础战斗规则正在限制未来的设计空间？

使用具有代表性的近战、远程、技能循环、控制、重击、防御和机动构筑作为测试样本。

不要在本批次对全部流派进行大规模卡牌数值平衡。

## 9. Deep Research Method

我希望 AI 深入研究，而不是立即实现第一个看起来合理的设计。

充分结合：

- 已有研究报告。
- 当前真实代码。
- 原始实测证据。
- 游戏设计理论。
- 相关游戏的开发者访谈。
- GDC 与游戏设计案例。
- 玩家真实反馈。
- 数学与几何模型。
- 低成本模拟。
- 必要的隔离原型。
- 独立反证。

可以研究 Battlerite、LoL Arena、Dota 2、Eternal Return、Supervive、格斗游戏、动作游戏及其他具有相关设计原理的游戏。

不局限于已有外部研究报告。

不要简单复制其他游戏的功能。

分析：

- 为什么某种设计有效？
- 为什么某种设计失败？
- 它依赖哪些前提？
- Lockdown 是否具备这些前提？
- 引入后会产生哪些新的决策？
- 会破坏哪些现有乐趣？
- 会对其他系统产生什么影响？

效率不代表减少设计探索。

但不要重复已有证据充分的问题发现工作。

把研究时间主要投入到解决方案、设计比较与潜在副作用。

## 10. Mandatory Design Alternatives

对于以下四个设计决策域，分别给出三至五个具有实质差异的候选方案：

D01 — 基础战斗节奏与移动。

D02 — 冲刺、机动与精准闪避。

D03 — 爆劲机制与输入方式。

D04 — 基础战斗反制与平衡原则。

每个候选方案必须包括：

- 设计理念。
- 具体修改内容。
- 当前规则与新规则对比。
- 玩家实际如何操作。
- 一个具体战斗场景。
- 为什么可能更好玩。
- 新产生的玩家决策。
- 对近战与远程的影响。
- 对不同构筑的影响。
- 潜在滥用和反制方式。
- 二阶副作用。
- 技术成本与风险。
- 现有证据。
- 尚未验证的假设。
- AI 推荐程度及理由。

候选方案必须有真实机制差异，不能仅通过更改几个数值凑出多个版本。

至少包括：

- 保守优化方案。
- 结构性改进方案。
- 更大胆的替代方案。

鼓励原创设计。

如果已有足够证据表明某个方向不可行，解释原因并淘汰它，而不是浪费时间制作完整实现。

## 11. Cross-System Combat Design

Session 3 和 Session 4 完成独立研究后，由 Coordinator 与 Independent Reviewer 进行交叉整合。

不能孤立决定移动、冲刺、闪避和爆劲。

这些系统高度耦合。

例如：

- 提高移速可能放大远程风筝能力。
- 缩短冲刺冷却可能强化穿人刷新构筑。
- 移除爆劲蓄力可能使爆发伤害失去预警。
- 延长技能预警可能让慢速近战构筑失效。
- 提高基础反制能力可能降低某些控制构筑的价值。

最终形成两至四套内部一致的 Combat Design Package。

每套应说明：

- 基础战斗哲学。
- 具体移动与操作规则。
- 冲刺与闪避逻辑。
- 爆劲设计。
- 攻防反制关系。
- 对现有构筑的兼容性。
- 预期游戏体验。
- 风险与验证方式。

这些完整方案是为了帮助 Product Owner 选择，而不是替 Product Owner 做最终决定。

允许 Product Owner 从不同方案中组合机制。

明确哪些方案可以组合，哪些互相冲突。

## 12. Evidence & Validation

充分使用已有研究工具。

优先低成本、高信息价值的验证。

采用以下证据分级：

L1：源码、配置、数学及静态分析。

L2：离线模拟。

L3：Unreal 原生对抗。

L4：Unreal 原生完整对局。

L5：渲染实机和真人交互。

不得把 L2 模拟结果描述为原生游戏实测。

不得把无头测试描述为真人操作体验。

对于战斗设计，优先使用：

- 同构筑镜像对抗。
- 同资源预算交叉对抗。
- 近战与远程。
- 有无冲刺资源。
- 有无爆劲。
- 不同反应延迟。
- 不同控制状态。
- 正常技能使用与失误。
- 第三方介入。

研究完整 TTK 分布、操作窗口、反制机会、伤害分布和战斗节奏。

不要只比较 DPS。

不要为了追求统计规模而运行大量重复、没有新增信息价值的实验。

如有必要，制作少量隔离的可玩原型，让 Product Owner 能够直观理解设计差异。

## 13. Batch 01 Execution Flow

### Step 1 — Bootstrap

读取仓库规范、现有报告和当前代码。

建立 Batch 01 范围、任务所有权、依赖关系和 Multi-Session 工作区。

立即启动技术修复与设计研究。

不要等待 Product Owner 再次批准已明确授权的工程任务。

### Step 2 — Parallel Engineering & Research

技术 Session：

实际修复 Bug、改善性能、完善测试与 AI。

设计 Session：

深入研究战斗设计，提出竞争方案，运行必要的低成本实验。

这两类工作并行推进。

### Step 3 — Clean Baseline

集成已经验证的确定性修复。

建立 Batch 01 Clean Baseline。

至少尝试验证：

- 已知触发风暴无法复现。
- F2/F3 事件能够正常完成。
- 已知导航失败已修复或明确隔离。
- AI 能正常执行已有技能。
- 原生整局能完成终局及赛后流程。
- 原有正式玩法规则未被擅自修改。
- 新增回归测试通过。
- Verify.ps1 通过。

保存实际测试证据。

如果某些验证受环境限制无法执行，明确报告，不得伪造成功。

### Step 4 — Product Approval Gate

当战斗设计研究完成后，向 Product Owner 提交统一的 Batch 01 Design Decision Package。

这是强制审批门禁。

交付：

- 当前基础战斗的真实问题。
- D01–D04 的候选方案。
- 每个方案的具体规则差异。
- 代表性玩家操作情景。
- 研究证据。
- 潜在风险。
- AI 推荐及其原因。
- 两至四套可组合的整体战斗方案。
- 明确需要 Product Owner 决定的问题。

用中文表达。

不要只提供抽象设计术语。

让我能够清晰理解 AI 想改什么，改动以后玩家实际如何操作，以及可能得到或失去什么。

在此节点等待 Product Owner 对正式玩法设计的明确选择。

尚未批准的玩法不得进入正式实现。

如果仍存在已授权的独立技术任务，可以继续执行，不需要全部 Session 停工。

### Step 5 — Approved Implementation

仅在 Product Owner 明确批准对应设计后执行。

根据批准结果重新组织 Multi-Session。

并行完成：

- 正式代码实现。
- 配置与 SourceData 更新。
- 必要的 UI 和 VFX 反馈。
- 回归测试。
- 原生对抗验证。
- 渲染实机验证。
- 集成与回滚准备。

实现细节由 AI 自行决定，不需要逐项审批。

不得擅自扩大已批准的玩法范围。

### Step 6 — Batch Closure

完成 Batch 01 技术与设计验收。

记录最终版本和遗留问题。

停止在本批次边界。

不要自动开始 Batch 02。

## 14. Persistent Work State

为了防止 Session 上下文耗尽或任务恢复后再次混乱，在现有研究目录下维护一个简洁的持久化工作区。

建议：

docs/research/gameplay/reconstruction/

包含：

ROADMAP.md

DECISION_REGISTER.md

batch-01/STATUS.md

batch-01/TASKS.md

batch-01/DESIGN_DECISIONS.md

batch-01/VALIDATION.md

batch-01/HANDOFF.md

如果仓库已有更合适的任务记录结构，优先复用。

ROADMAP.md 记录四个批次及其边界。

DECISION_REGISTER.md 记录：

- Product Owner 已确认的原则。
- 已批准的设计。
- 已否决的设计。
- 等待审批的设计。
- 设计决策的依据。

STATUS.md 记录：

- 当前阶段。
- 当前基线。
- 正在工作的 Session。
- 已完成任务。
- 未完成任务。
- 当前阻塞。
- 下一步。

TASKS.md 为唯一任务状态来源。

每个任务必须具有：

- Task ID。
- Area ID。
- Owner。
- AUTO / REVIEW / EXPERIMENT 分类。
- Status。
- Dependencies。
- Evidence。
- Commit 或其他实现引用。

状态包括：

- Pending。
- In Progress。
- Researching。
- Awaiting Approval。
- Approved。
- Implemented。
- Verified。
- Deferred。
- Blocked。

HANDOFF.md 必须允许新的 Session 在不读取整个历史聊天的情况下恢复当前工作。

保持简洁。

不要让任务管理文档成为新的工程负担。

不要为每个小任务生成独立长篇报告。

## 15. Scope Control

本批次不进行：

- 全面卡牌重构。
- 全面构筑生态重构。
- 选牌与稀有度重构。
- 元素系统玩法重构。
- 经济架构研究。
- PvE 生态重新设计。
- 营地刷新机制设计。
- 中场 PK 玩法重构。
- 对局阶段重构。
- 终局玩法重新设计。
- 大规模 UI 美术重构。
- 新地图或新角色制作。

以上任务已经分配给后续批次。

如果基础战斗研究发现它们与当前设计存在依赖，记录依赖和可能影响即可。

确定性 Bug 修复除外。

## 16. Completion Criteria

Batch 01 最终完成时应具备：

1. 稳定、可信的技术基线。
2. 已知 P0 缺陷的修复结果及剩余问题清单。
3. 能够正常执行完整对局的原生验证证据，或明确的剩余环境阻塞。
4. 深入的战斗设计研究。
5. D01–D04 的多个有效设计候选。
6. Product Owner 明确的玩法审批记录。
7. 已批准方案的实际实现。
8. 原生对抗与必要的渲染实机验证。
9. 可追溯、可回滚的代码变更。
10. 后续 Batch 02 可以直接继承的稳定基线和 HANDOFF。

不能仅凭文档完成、编译成功或测试通过宣称战斗已经变得更好玩。

真人体验仍然需要实际验证。

## 17. Start Now

现在开始执行 Batch 01。

第一步：

读取 AGENTS.md、本地研究报告与当前仓库状态。

第二步：

建立本批次任务边界、持久化任务记录及 Multi-Session 工作分配。

第三步：

同时启动已授权的技术修复和深入战斗设计研究。

第四步：

完成可信技术基线，并提交具体、具有原创性、可供选择的战斗设计方案。

不要先返回一份冗长的执行计划，然后停止工作。

实际启动 Session，实际研究，实际修复，实际验证。

但在涉及正式游戏玩法修改时，必须先获得我的明确审批。

只执行 Batch 01。

不要启动其他三个批次。

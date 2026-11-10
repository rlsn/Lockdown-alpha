# 构筑评估 Harness

DEV 工具，不是产品模式。它回答“在本次覆盖的构筑、策略、场景与预算内，某个选择或组合是否改变了原生对局结果”，
给出可重放的证据，不输出趣味性总分，也不在没有批准阈值时判定“平衡”。
票与验收见 [#194](https://github.com/rlsn/Lockdown-alpha/issues/194)；产品规则仍只以 [App Spec](../APP_SPEC.md) 为准。

## 分层与依赖方向

| 层 | 位置 | 责任 |
| --- | --- | --- |
| 通用核心 | `Scripts\BuildEvaluation.py` | 实验清单校验、按能力表准入、Case 展开与种子、执行状态与分母、成对统计、发现分级、报告、重放比对 |
| 玩法 Adapter | `Scripts\BuildEvaluationLockdown.py` | 从运行目录生成能力表、抽取方案引用、写原生输入、把原生证据判成状态/指标/事实、规范化重放事件 |
| 原生驱动 | `Source\Lockdown\Private\Tests\LDBuildEvaluationProbe.*`、`Evaluation\LDEvaluation*` | 在真实 Listen Host 里按正常大厅开局，执行 Case，写结果与权威日志轨迹 |
| 入口 | `Scripts\RunBuildEvaluation.ps1` | plan → 原生 → analyze（及重放比对）的唯一入口 |

核心不认识任何卡表、三选一、槽位、属性维度或胜利条件；它只认 Adapter 能力表里声明的
mechanic / decision / scenario / metric 标识。清单写明 Adapter 模块名，核心按名字导入，Adapter 只依赖核心的文件约定，
核心从不导入 Adapter（`TestBuildEvaluation.py` 用假 Adapter 跑通核心并检查核心源码不含游戏词汇）。

玩法或构筑结构大改时：改写或新增 Adapter 与场景，核心不改。能力表里没有的机制、决策、场景或指标一律在准入时拒绝
（`UnsupportedMechanic` / `UnsupportedDecision` / `UnsupportedScenario` / `UnsupportedMetric`），不用旧卡、零效果或近似公式替代。
不同 Adapter 版本、内容摘要或能力表摘要的运行不拼接成一份结论；重放要求能力表摘要不变。

## 命令

```powershell
.\Scripts\RunBuildEvaluation.ps1 -Manifest Scripts\BuildEvaluationManifests\e3-first-levelup-branches.json
.\Scripts\RunBuildEvaluation.ps1 -Manifest <清单> -PlanOnly           # 只校验、准入、展开 Case，不启动原生
.\Scripts\RunBuildEvaluation.ps1 -ReplayRun <运行目录> -Case <case id> # 新进程重放一个 Case 并与原记录比对
python -B .\Scripts\BuildEvaluation.py analyze --run-dir <运行目录> --rejudge  # 用当前 Adapter 重新判读已录证据
```

运行要求编辑器构建（`BuildEditor.ps1`）；一次运行只启动一个无画面 Listen Host（`-nullrhi`、固定步长 `-UseFixedTimeStep -FPS=60`，
可用 `-FixedFps` 改），顺序执行全部 Case，不并发、不做定时运行、不外发报告。计划与重放只写入不存在或空的运行目录，
非空目录在写任何文件前被拒绝，旧证据不会混入新计划，重放也不能覆盖源记录。输出在忽略的
`Artifacts\BuildEvaluation\<时间戳>-<实验 id>\`：

| 文件 | 内容 |
| --- | --- |
| `manifest.json` | 冻结的清单与解析结果（核心版本、Adapter、内容摘要、能力表摘要、证据级别、Case 数） |
| `capabilities.json` | 本次使用的能力表 |
| `coverage.json` | 每个 arm 的准入结论与各执行状态计数 |
| `cases.jsonl` | 每个 Case 的块、种子、arm、状态、原因、指标、事实 |
| `native_input.json` | Adapter 写给原生驱动的输入（只含可运行 Case） |
| `native\native_run.json`、`native\results\<case>.json`、`native\traces\<case>.log` | 原生运行身份、每个 Case 的结果、该局的权威 `LD_*` 日志行 |
| `metrics.json`、`findings.json`、`report.md` | 统计、发现与报告；重放运行另有 `replay.json` |
| `Host.log`、`runner.json` | Host 完整日志；退出码、超时、日志错误，以及运行前后编辑器可执行文件和游戏模块的 SHA-256 |

## 实验清单

清单 schema `build_evaluation.manifest` v1，未知字段一律拒绝，预算等字段没有默认值。

| 字段 | 含义 |
| --- | --- |
| `experiment.id` / `hypothesis` | 标识与要检验的假设 |
| `adapter.module` / `adapter.id` | Adapter 模块名与它声明的 id，二者必须一致 |
| `scenario.kind` / `parameters` | Adapter 场景与其参数（由 Adapter 校验） |
| `sample_class` | `natural`（自然成长）、`legal_search`（合法搜索）、`diagnostic`（条件准备）；后两者必须在 `assumptions` 写明供给或准备假设 |
| `design` | `paired_arms`（各 arm 对 `reference`）、`decision`（同一决策点的多个选项）、`factorial_2x2`（四个 arm 的 `levels.first/second` 为 treatment/reference） |
| `design.arms[].plan` | Adapter 定义的方案，对核心不透明 |
| `blocks` | `root_seed`（十进制 uint64 字符串）、`splits.search/confirm` 块数、`sides`（2 表示每块再交换双方位置跑一次） |
| `metrics` | `primary`、`secondary`，以及 `material_difference`：已批准的实质差异阈值，未批准时必须写 `null` |
| `budget` | `max_cases`、`case_wall_timeout_s`、`run_wall_timeout_s`；可运行 Case 超过上限时整个计划被拒绝，不启动任何进程 |

块种子 = SHA-256(`build-evaluation/v1|root|split|index`) 的前 8 字节；同一块的所有 arm 与两侧共用世界种子，
搜索与确认集种子不相交（计划时核对）。Case id 为 `<s|c><块号><a|b>-<arm>`。

## 状态、证据与结论

| 类别 | 取值 |
| --- | --- |
| 执行状态 | `NotRun`、`Completed`、`Unsupported`、`Invalid`、`Faulted`、`BudgetExceeded`、`Interrupted`（`Running` 保留） |
| 证据级别 | `Static`、`NativeControlled`、`NativeSpatial`、`OwnedConnection`、`Rendered` |
| 结论 | `RuleViolation`、`BalanceConcern`、`NoCounterexampleInCoveredSet`、`InsufficientEvidence`、`NoTarget` |

只有 `Completed` 进入统计分母；其余状态在报告与 `coverage.json` 中逐条列出原因。超时是删失，不是输；
预算耗尽、原生故障、缺结果都不是胜负。

统计的独立单位是**种子**，不是执行侧：同一世界种子的两侧先平均，再计算 arm 均值、成对差、
Student t 的 95% 区间与平均差非零的种子数及其 95% Wilson 区间。某指标所需 arm 的任一侧缺失或无效时，
整个种子不进入该项分母；不同 arm 的单独均值可有不同的有效种子数。搜索集和确认集种子碰撞时计划被拒绝。
各独立种子的差值完全相同时区间“退化”，报告不给区间、也不拿它和阈值比较（离散的 `duel_score` 常见这种情况）。
`decision` 对明确标注 `higher_better` 或 `lower_better` 的指标另给各侧平均后每个选项的平均后悔值、最佳占比和有选项差异的种子占比；
`descriptive` 指标保留均值、删失计数和成对差，不定义最佳选项或后悔值。`factorial_2x2` 给参考替换交互
`V(A,B) − V(A,b0) − V(a0,B) + V(a0,b0)`。`time_to_event` 指标的删失 Case 按时限计入（受限均值）并计数。
只有确认集的主指标产生结论：没有批准阈值时是 `NoTarget`（只报告效应与区间）；有阈值时区间整体超出为
`BalanceConcern`，整体落在 ±阈值内为 `NoCounterexampleInCoveredSet`，区间退化或跨越阈值为 `InsufficientEvidence`。
搜索集只作探索。每条结论附差距最大的种子的全部相关 Case（包括两侧）作为最小复现。
报告与 `metrics.json` 分别标记录制 Adapter 和当前判读 Adapter；`cases.jsonl` 记下每个已判读 Case 的证据判读版本。
已完成 Case 的判读版本与当前 Adapter 不同时，普通分析会拒绝复用旧事实，必须显式加 `--rejudge` 以同 ID 的 Adapter 从原生结果与轨迹重新判读；
重判后可以普通分析，但原能力表和录制版本不变。证据合同违规随 Case 记在 `cases.jsonl`，重新分析也会再次报告。
Adapter 可在能力表 `limits.max_cases_per_run` 声明一次原生运行接受的 Case 上限（Lockdown 为原生驱动的 500），超出在计划时拒绝。

`RuleViolation` 来自：运行身份与计划不符（内容摘要、固定步长、种子派生版本、运行前后程序摘要）、证据合同被违反（如 `LD_HIT` 的
`hp_lost` 与扣血前后不符）、同一决策块分支的前缀不一致、换种子随机结果不变、运行日志出现错误或权威故障、重放分歧。

## Lockdown Adapter v4（v0.10 Demo）

能力表来自运行目录 `Content\Lockdown\Data\Demo`：内容摘要是 `manifest.json` 的 SHA-256（即原生目录根），
每个技能、属性卡与开局卡都是 `Supported`（原生导入器拒绝没有处理器的定义），技能另记 `draftable`。
不在运行目录里的标识（例如 v0.11 候选卡）不是 Supported，相关 arm 不运行。能力表还记录获得重抽的等级。
判读旧版能力表时，只有本地目录根及其内容载荷哈希均与录制时一致，才从该目录恢复缺少的重抽等级；否则拒绝判读。

场景 `duel_site`（证据级别 `NativeSpatial`）：

1. Host 是唯一真人，只编排、不参战、不计分；两名服务器 AI（1 真人 + 2 AI）作为对决双方。
2. Host 在大厅武装 Case 种子并为两名 AI 登记同一个 DEV 评估画像，再按正常流程开局。
3. 开局休息 R0 的配装步骤内各自按计划完成开局选择；探针首次看到野外 1（`Development`）时 `DevGrantReward` 发放计划的经验供给（`LD_REWARD cause=dev_fixture`，经正常奖励复合事务）。
   条件实验用 `DevQueueOffer` 预置序号 2 起的三张候选（属性卡品质为预置的 Blue）。
4. 每次机会按计划规则选择：`ai_select`（生产 `SelectChoice`，含重抽）、`option_index`（实际 Offer 的第 0–2 张）、
   `definition`（实际 Offer 中的指定卡；新主动在 Q/W/E 满时需要 `replace`）。规定的卡不在实际 Offer 中时 Case 判
   `Invalid`，不替换成别的卡。
5. 计划选择全部消费后双方走向公开地点 `site`，看见指定对手即用生产 AI 的交战（`LDOpponentPolicy::EngageUnit`）攻击，不撤退；
   第一名对决者死亡或到 `duel_timeout_ms`（不超过 Boss 出现时间）结束，Host 结束本局并回到大厅。

计划时就能确定的错误在准入时拒绝：规则数与供给给出的机会数不符、开局卡出现在序号 ≥2 或其他卡出现在序号 1、
不可抽取的技能、预置开局卡或预置数超过机会数、预置只允许 `diagnostic` 样本。

判读时以下情况为 `Invalid` 并说明原因：营地/Boss/Host 等非对决者与对决者之间在结果前有命中（`ExternalInteraction`）、
结果时刻及之前获得供给以外的经验（`UncontrolledXP`；只豁免击杀者因这次决定性击杀在结果时刻获得的奖励）、供给数额不符（`SupplyMismatch`/`SupplyMissing`）、计划选择未完成
（`PlanNotReached`）、预置候选不是实际抽到的 Offer（`PresetNotApplied`）、死亡缺致死命中或死亡回执（`TraceIncomplete`）、
击杀者不是另一名对决者（`ExternalKill`）、画像未被采用或种子身份未进入名单（`Setup`）。
双方每个计划序号都必须有初始 `LD_OFFER`、可核对的重抽记录和最终 `LD_CHOICE`；验证候选、所选卡与计划规则、
等级品质、剩余重抽及终局开局卡。缺失、空白、行数不符、格式损坏或不一致的轨迹判为 `TraceIncomplete`，不能进入统计。
多 arm 的分支点是主体规则首次不同的序号；可比较的前缀保留此前每次初始 Offer、重抽和提交选择的时间、等级、
完整有序候选（种类、定义、起始/目标等级和品质）、所选种类/定义/等级/品质、替换、待选数与揭示技能；
分支点只纳入决定之前的初始 Offer。缺少选择元数据的轨迹判为 `TraceIncomplete`，前缀不一致的种子不参与决策推断。
报告用的简短 Offer 与选择标签不作为配对证据。

| 指标 | 含义 |
| --- | --- |
| `duel_score` | +1 主体击杀对手，−1 主体先死，0 到时未分胜负（删失） |
| `damage_dealt_hp` / `damage_taken_hp` | 结果前主体对对手、对手对主体已提交命中的实际掉血（不含护盾吸收） |
| `subject_hp_end_bp` | Case 结束时主体生命比例（死亡为 0）；时限可能落在两帧之间，终态是探针结束时的帧状态，不是精确时限快照 |
| `decision_time_ms` | `PublicMatch.StartedAtMs`（开局、即 R0 开始）到第一名对决者死亡，到时删失 |

事实（`cases.jsonl`）：双方每个序号的实际 Offer 与选择、重抽 Offer、终局构筑、分支标签（分支点所选卡）、
分支前缀、出生位置、首个有效命中的一方与时间、随机指纹。

## 原生实现

全部在 `WITH_DEV_AUTOMATION_TESTS` 内，且只在带 `-LDBuildEvaluation` 的进程里生效；没有这个开关或没有武装 Case 时，
每个挂点保持生产随机，`Lockdown.Rules.BuildEvaluation.Seeds` 核对这一默认。

| 种子流（`Evaluation\LDEvaluationSeeds`，派生版本 1） | 挂点 |
| --- | --- |
| `growth.match_secret` | `ALDDevelopmentDirector::BeginMatch` 的 `MatchSecret`（所有 Offer 与重抽） |
| `world.spawn` | `ALDGameMode::StartDemoMatch` 的 `SpawnRandom`（出生点与 AI 开局计划） |
| `combat.crit` | `InitializeAuthorityCore` 创建 Combat 后的 `CritRandom` |
| `world.airdrop` | `LDAirdrop::Resolve` 的投放点序列 |
| `identity.ai.<slot>` / `identity.session.<n>` | AI 参赛者身份；Host 会话身份按加入顺序固定（身份决定同一边界内的处理顺序） |

热搜圈（按 ledger 身份）、营地成员与效果 GUID 不种子化：前者不影响对决，后者不参与排序。

评估画像（`Evaluation\LDEvaluationPolicy`）只替换 `ALDOpponentController` 里 `LDOpponentPolicy::Decide` 这一次调用；
Frame、命令、回执与失败记忆仍是该适配器的，生产决策不变。`LDOpponentPolicy::EngageUnit`/`WalkTo` 是 DEV 构建公开的
生产交战与行走原语。

`LDBuildEvaluationProbe` 每帧推进：Join（核对 Listen Host、`-LDDevGrowthFixture`、内容根与计划一致）→ Lobby（2 个 AI、准备、
武装种子、登记画像、开始捕获日志、开局）→ Starting（核对种子身份进入名单、画像被采用、预置入队）→ Running（野外 1 供给、
画像失败、击杀通知、时限、墙钟预算）→ 写结果与轨迹 → 结束本局 → 回大厅并等 AI 退休。输入经
`LDBuildEvaluation::ParseInput` 严格解析（`Lockdown.Rules.BuildEvaluation.Input`），输入与输出都必须在 `Artifacts` 下。
轨迹是该局期间 `LogLDCombat/Growth/Match/AI/Trace/Test` 的 `LD_*` 行，沿用[运行日志合同](ARCHITECTURE_AND_NETWORKING.md#运行日志与试玩记录)，
Adapter 只汇总这些已提交事实，不自行计算伤害或抽卡。
发生在 `StartedAtMs + DuelTimeoutMs` 及之前的击杀计入；更晚的通知忽略，按精确时限判为删失。结果前命中指标按该时限截断，
但终态 HP/MP 与重放终态仍记录探针实际结束帧。
#198 外层流程接入后，`StartedAtMs` 是开局（R0 开始）而不是野外 1 开始，所以时限与 `decision_time_ms` 包含 R0 的 `REST.OpeningBuildSec`；
对决越过野外 1 终点时进入 R1，对决身体按阶段边界退休（不是死亡）。这些对判读的影响尚未处理，Harness 也未在新流程下运行。

## 重放

`-ReplayRun` 以原 Case 的身份、种子与方案新开进程重跑。只有两次都成功完成，计划、Case、能力表、原生运行身份、
编辑器可执行文件和游戏模块的运行前后摘要均可核对且一致，双方选择证据与心跳资源/位置完整，才进行成功判定。
比较终态的构筑、HP/MP 等字段和双方心跳资源，以及到 Case 结束为止的
`LD_OFFER/CHOICE/REWARD/LEVEL_UP/HIT/DEATH_RECEIPT`（身份换成角色名）；位置按 `-SpatialToleranceCm`（默认 0）比较。
报告保留首个分歧。空轨迹、缺失必要证据、运行失败或身份不符都不能标为 `Replayed`。
旧运行可用 `--rejudge` 重新判读，但旧版能力表与 v4 不同，不能当作 v4 原生重放的同版本证据。
程序摘要只覆盖编辑器可执行文件和项目模块，不证明其他引擎 DLL 或插件的逐字节一致；`source_dirty=unknown`
也不证明工作区无修改。跨平台、换构建或未记录摘要的旧运行不承诺一致。

## 测试

| 测试 | 覆盖 |
| --- | --- |
| `Lockdown.Rules.BuildEvaluation.Seeds` | 派生确定性与金值、流相互独立、无开关时拒绝武装且身份仍为新 GUID |
| `Lockdown.Rules.BuildEvaluation.Policy` | 三种选择规则、缺卡与替换未指定的拒绝、休息配装步骤内选择、计划完成前不动、只攻击指定对手、不撤退、走向与到达、失败后停止 |
| `Lockdown.Rules.BuildEvaluation.Input` | 原生输入的严格解析与各类拒绝 |
| `Lockdown.Rules.BuildEvaluation.Horizon` | 非帧对齐时限之前、恰在时限和之后的击杀边界 |
| `TestBuildEvaluation.py` | 核心清单拒绝、准入与设计不完整、预算、种子分割、配对/决策/交互统计、删失、分母、阈值判定、身份失配、重放比对、依赖方向 |
| `TestBuildEvaluationLockdown.py` | 能力表即运行目录、计划期非法方案、未入目录的候选卡不支持、预置只限 diagnostic、原生输入与换边、合成轨迹的指标与各 `Invalid` 原因、证据合同、重放事件、运行身份 |

## 首版范围与后续

首版覆盖本地方案 E0–E3：内容/能力准入与证据合同、可重放的原生 Case、合法成长路径与受控交战、等预算分支与 2×2 交互及最小复现。
示例清单在 `Scripts\BuildEvaluationManifests`。尚未覆盖：v0.11 机制（需批准并原生实现后由 Adapter 声明）、
六类策略画像与按画像调参（E5，接入点是评估画像这一处 Decide 替换）、拥有连接/渲染证据的反制与可读性、
多人或旗杆整局胜率、增量与定期评估（E6，调度与预算需另行批准）。
平衡目标与实质差异阈值、每批计算预算都由用户决定，未批准时不填默认值。

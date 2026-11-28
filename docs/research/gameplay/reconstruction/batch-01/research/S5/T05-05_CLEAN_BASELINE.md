# T05-05 Clean Baseline 原生验证配置（供 S0 在集成分支执行）

前提：集成分支已合入 S1/S2 修复与 S5 提交（分支 `sinmentis-miniature-guide`：`ae582982` 无效样本判读 + 实测裸基准；`e490d82b` 目标生命周期指标；之后的架势与操作旋钮豁免提交）。
命令都在集成工作区根目录执行；构建只做一次，之后所有项复用同一份构建。无头进程全机 ≤ 3：本表原生项串行，或至多 2 个并行，且不与 S1/S2 构建重叠。

## 执行顺序与机器时长

| # | 项（CHARTER §13 Step 3） | 命令 | 级别 | 预计墙钟 |
| --- | --- | --- | --- | --- |
| 0 | 构建 + Verify + 新增回归测试 | `.\Scripts\Verify.ps1 -Filter <本批新增测试全名...>`（自带增量构建） | L1 | 构建 3–6 min + 测试 ~1 min/前缀 |
| 1 | 正式玩法规则未被擅自修改 | §1 diff 审查 | L1 | 5 min |
| 2 | 风暴不可复现 | S1 规则测试 + §2 手动复现 | L1/L5 | 测试 <1 min；手动 5 min |
| 3 | F2/F3 正常完成 | 规则测试 + BotMatch 日志（§3） | L1/L4 | 随 #6 |
| 4 | 导航失败修复或明确隔离 | `MapTraversal` + 重跑 p3-breakout（§4） | L1/L3 | 测试 ~1 min；原生 ~15 min（44 Case，上次 880 s） |
| 5 | AI 正常施放已有技能 | #4 的运行 + BotMatch 日志 | L3/L4 | 随 #4/#6 |
| 6 | 原生整局完成终局与赛后 | `RunDemoOpponentCheck.ps1 -BotMatch -MatchCount 3` | L4 | 15–21 min（每局 5–7 min） |
| 7 | 实测裸构筑基线（研究基础设施，非门禁） | `p3-ttk-baseline-l1/l6/l10` | L3 | 共 ~20 min |
| 8 | 满房真实连接 + 结算页（集成门禁） | `RunDemoOpponentCheck.ps1`；`CaptureUICoverage.ps1 -Pages Match_Results` | L4/L5 | ~15 min + ~1 min |
| 9 | X1/X2 原生实验（研究，非门禁；PO 排序后精简） | §9：4 份清单，48 Case | L3 | ~16 min |
| 10 | X3 前摇中移动（研究，PO 优先） | 并入 §2 的手动渲染会话 | L5 | +2 min |

#0–#7 串行约 75–90 min；#4 与 #6 并行（2 个无头进程）约 50 min；#9 另加约 1.5–1.7 h（可与 #6 并行）。

## §1 正式玩法规则未被擅自修改（L1）

```powershell
git --no-pager diff --stat main...HEAD -- SourceData Content/Lockdown/Data
python -B .\Scripts\SyncDemoContentData.py --check
python -B .\Scripts\SyncDemoWorldData.py --check
git --no-pager diff main...HEAD -- Source
```

通过：SourceData / 运行载荷 diff 为空，或每一处都对应 TASKS 中规格明确的 AUTO 项；两个 `--check` 通过；C++ 中每个新增或修改的数值常量都能对应到 T01–T04 任务（技术事件预算、测试夹具除外）。改变伤害、冷却、范围、持续时间、经济的常量必须有 REVIEW 批准记录。由 S5 做一次独立 diff 审查后签字。

## §2 触发风暴不可复现

1. L1（门禁）：S1 新增的 A12×A09 规则测试（研究建议名 `Lockdown.Rules.Combat.OvercapFocusDummyBounded`，以 S1 登记到 `CheckRuleReport.py` 的全名为准）。通过：测试绿；断言的是技术事件预算与“附加命中不再触发附加命中”的因果规则，不是 A12 层数或 A09 次数的玩法上限（T01-05 已 Defer 到 B02）。
2. L5（手动、渲染 Host、一次）：DEV 测试会话中房主执行 `ld.Dev.Breakout breakout_attack_speed`（该样板含 A12 `card_heated_up` + A09 `card_quick_hands`），在休息室对训练假人连续普攻 ≥ 65 s，再 `python -B .\Scripts\SummarizePlaytestLog.py <Host 日志> --metrics-only`。通过：进程不挂死；同一权威时刻 `t=` 的房主 `LD_HIT` 数不超过技术预算；`LD_PERF` 无 >100 ms 卡顿；日志增长线性。
3. 原生整局只作旁证：duel_site 对手约 4 s 即死，BotMatch 不保证有 AI 凑出 A12+A09，不能证明“不可复现”。

## §3 F2/F3 正常完成

- L1：S1 的目标推进规则测试（研究建议 `Lockdown.Rules.FieldEcology.ObjectiveAdvanceMonotonic`，以实际全名为准）。
- L4：`python -B .\Scripts\SummarizePlaytestLog.py <BotMatch 输出目录>\Telemetry.log --metrics-only`。通过：`Field objectives that ended (Won or Expired)` 为 `N of N`，且没有 `repeated phase lines`；每局每条为 `#serial kind Announced→Open→Won|Expired`。对照：EXP-01 打包日志为 `2 of 3; 11163 repeated phase lines`（arena #2 每帧重发 Open）。

## §4 导航 / §5 AI 施法

```powershell
.\Scripts\Verify.ps1 -Filter Lockdown.Rules.StaticScene.MapTraversal
.\Scripts\RunBuildEvaluation.ps1 -Manifest Scripts\BuildEvaluationManifests\p3-breakout-samples.json
```

root seed `20261290` 不变，种子 `c001` 的出生点与 EXP-02 相同。Adapter v16 自动判读：

- 导航通过：报告“覆盖与执行状态”中 0 个 `NoEngagement`（EXP-02 用 v16 重判为 22/44）。若仍有，列出 Case 与 `facts.spawn`，确认是否为 S2 已报关卡负责人的点位（明确隔离）。
- 施法通过：0 个 `NeverCast`（EXP-02 重判为 4：hedgehog、dash_ghost 两侧）；每个 arm 每侧 `subject_casts ≥ 1`。
- 读倍数时看报告“限制”里的属性点预算不对等：样板花完全部点数，`bare` 对手 54 点全不分配；差值不能单独归因于构筑。
- 整局侧：#6 汇总中 `AI that never cast in any of their fights | 0 | 0`。

## §6 原生整局完成终局与赛后（L4）

```powershell
.\Scripts\RunDemoOpponentCheck.ps1 -BotMatch -MatchCount 3
python -B .\Scripts\SummarizePlaytestLog.py <输出目录>\Telemetry.log --metrics-only
```

通过：`Verdict.json` passed（每局锁定结果 ID、结束原因非空且非 HostEnded、墙钟内结束、无 Error/Fatal/断言/`LD_DEMO_AUTHORITY_FAULT`、每个 AI 有选择/学会/经验增长），加 §3、§5 两条日志判据。失败只重跑失败局。
限制：BotMatch 只有 Host 进程，不含真实拥有连接与结算界面；赛后界面由 #8 覆盖。

## §7 实测裸构筑基线（非门禁）

```powershell
foreach ($level in 'l1','l6','l10') { .\Scripts\RunBuildEvaluation.ps1 -Manifest "Scripts\BuildEvaluationManifests\p3-ttk-baseline-$level.json" }
python -B .\Scripts\SummarizePlaytestLog.py <Telemetry.log> --metrics-only `
  --baseline-run <l1 目录> --baseline-run <l6 目录> --baseline-run <l10 目录> --baseline-run <#4 p3-breakout 目录>
```

各 run 中双方都不分配属性点的 `bare` arm、Completed 且未删失的 Case 给出 L1/L6/L10/L16 实测裸构筑 DPS；之后所有“质变”结论用这组基线并写明来源。旧默认（§3.9 估算 L1 72 / L10 154，外推到 L16 ≈ 209）已删除；EXP-02 的 L16 实测为 129.6 DPS。

## §9 X1/X2（L3 研究实验，第二批）

PO 排序：X1 与 X3 优先；X2 只保留近远程控距；X4 取消。第一批（76 Case）每个 arm 实际只有 1 个有效种子，结果与问题见 `X1_X2_RESULTS.md`。S2 T05-07 修复后重写为下表：全部 `clear_camps: true`，每个 arm 8 个确认种子 × 双侧（16 Case），共 256 Case，约 1.5–1.7 h。全部通过 `plan`（adapter v18，0 unsupported）。

```powershell
$m='docs\research\gameplay\reconstruction\batch-01\research\S5\manifests'
foreach ($f in Get-ChildItem $m -Filter 'b01-x*.json') { .\Scripts\RunBuildEvaluation.ps1 -Manifest $f.FullName }
```

| 清单 | arm（只改 subject 一侧） | Case | 回答 |
| --- | --- | --- | --- |
| `b01-x1-operation-ranged` / `-melee` | `exact` / `latency600`（使用延迟）/ `aim150`（瞄准误差） | 48 / 48 | 同构筑镜像中两个维度对胜负、TTK 的影响 |
| `b01-x1-warning-dodge-cycle` | `warn_off`（参照，不躲）/ `warn0` | 32 | 躲避是否生效（第一批 `warn0` 仍吃满板砖）；主指标为承伤 |
| `b01-x1-warning-react-cycle` | `warn0`（参照）/ `warn250` / `warn450` | 48 | 看到预警后的反应速度；主指标为承伤 |
| `b01-x1-lead-strafe` | `lead_off`（参照，生产 AI）/ `lead_on`；攻击方板砖 + 嘲讽弹 + 过期外卖，目标 `strafe 500` | 32 | 对横向走位目标，延迟落点与弹道技能的提前量价值 |
| `b01-x2-spacing-ranged-vs-melee` | `pursue` / `burst600` / `kite600` | 48 | 远程对追击近战的三种移动策略 |

- 躲避清单的每个 arm（含参照）都设 `reserve_dash`：追击与伤害性冲刺不用冲刺，留给躲避。
- 已删除：cycle 操作镜像（5/6 超时，对旋钮不敏感）；两份扒裤子 lead 清单。S2 核算抽裤腰带 2B 前摇 0.14 s × 移速 240–300 ≈ 34–42 cm，小于半线宽 65 cm，不预判也命中。

**操作维度与旋钮**（不把更快反应等同于更高操作水平）：

| 维度 | 旋钮 |
| --- | --- |
| 看到预警后的反应 | `warning_react_ms`（缺省 −1 不躲）+ `reserve_dash` |
| 使用时机 | `reaction_ms`（技能就绪后才使用、撤退开始的延迟；不是对预警的反应） |
| 瞄准精度 | `aim_error_cm`（地面点、弹道、直线、扇形；单体、自身、冲刺精确） |
| 预判 | `lead_aim`（按观测速度瞄准预测位置） |
| 移动策略 | 架势 `pursue / burst / kite / hold / guard / strafe` |
| 资源使用、技能顺序 | 没有旋钮：需要新增评估策略（S2）或用 L5 真人 |

判读边界：结果只说明“AI 在该维度变差时会怎样”，不说明真人是否会预判、会不会看预警（L5）。`lead` 与躲避清单需要按技能统计命中（`LD_HIT via=` 计数），否则胜率差会被普攻淹没。
## §10 X3：前摇中能否移动（PO 优先，L5，约 2 min）

并入 §2 的同一个渲染 DEV 会话：房主施放板砖（地面点）与扒裤子（方向），在前摇期间右键地面。记录：身体是否在前摇中位移、技能是否仍然施放、是否丢失。只记录事实，不判通过。若需要无头可重复的版本，可由 S1/S2 补一条规则测试（施法订单前摇中下达移动单，断言位移与施放结果）。
## §11 自动强化验证计划（PO 第四道指令；开关内实现）

只验证机制是否正确，不验证平衡。平衡数值见 `momentum-r\` 的 PARAMS，风险见 `critique\AUTO_EMPOWER_CRITIQUE.md`。

**遥测约定**（实现者写；S5 的 `SummarizePlaytestLog.py` 已按此汇总，缺失时显示 n/a）：

| 行 / 字段 | 内容 |
| --- | --- |
| `LD_EMPOWER t= entity= life= host=Basic\|Q\|W\|E\|D skill=<主动 id 或 -> target=contender\|camp\|none before= cost= after= serial= payload=<处理器 id>` | 每次强化释放提交时写一行（扣费的同一事务）；`serial` 对每个参赛者唯一 |
| `LD_HIT … empower=<serial>` | 该强化动作及其派生事件造成的命中；非强化命中不写该字段 |
| `LD_MOMENTUM … empower=<次数> armed_ms=<在门槛以上的毫秒数>` | 开关开启时总是写（即使次数为 0）；开关关闭时不写，用来区分“没有这个机制”和“机制在但没触发” |
| `LD_EVAL_PROFILE … empower=0\|1 empower_host=<宿主>` | BuildEvaluation 判读开关状态与宿主 |

**汇总列**（已实现，提交 `33b89958`）：触发次数与每参赛者每野外分钟的触发次数；宿主分布；目标分布（参赛者、营地、无）；打空率（没有一次带标记的命中）；平均消耗与触发前的劲；250 ms 内的重复触发次数（循环检测）；单次强化最多命中数；在门槛以上的时间占比。

**规则测试类别**（实现者写，CI 跑；新测试名进 `REQUIRED_TESTS`）：
1. 触发：劲达到门槛时，宿主的下一次合法动作强化并扣费一次；非宿主动作不扣；门槛恰好等于当前劲的边界；劲未达门槛时不强化。
2. 扣费时机：只在释放提交时扣费，打空照扣；前摇被取消、打断、死亡、换局，或订单修订号过期被拒，都不扣；同一回执重复到达不重复扣。
3. 防递归：强化动作及其派生事件不再触发强化、不产生劲收入；一个权威步内同一身体最多强化一次。
4. 迁移交互（逐卡，开关开启）：人头刷新、脑力关键石、一招回本、脑子转得快、撞着就刷新加冲刺耗劲、拿命换、再来一下别喘、偷劲（扒裤子 3B1、文武双全）。每张牌断言它对强化消耗的作用符合 SPEC。
5. 宿主变更：已就绪时更换宿主，只有新宿主的下一次动作强化；正在前摇的旧宿主动作不扣。
6. 开关关闭：行为与删除分支一致；内容哈希与 contract 不变。
7. 视图：拥有者视图显示就绪状态与宿主；对手只在视野内、宿主可用时看到就绪标记，看不到载荷细节。

**原生对抗**（L3，开关 off/on 对照；需实现者让 BuildEvaluation 能按 Case 设开关与宿主，S5 再扩 adapter 与清单）：

| 构筑（`LDBreakoutPresets`） | 代表 | 宿主（S2 在预设中指定，SPEC 确认） |
| --- | --- | --- |
| `breakout_attack_speed` | 普攻攻速 | 普攻 |
| `breakout_skill_engine` | 技能循环 | 主伤害技能 |
| `breakout_element_loop` | 元素反应 | 施加元素的技能 |
| `breakout_hedgehog` | 护盾、反伤 | 护盾技能 |
| `breakout_dash_ghost` | 冲刺 | D |
| `breakout_minefield` | 工事、区域 | 布置类技能 |

- 每个构筑打同级裸构筑（裸构筑以普攻为宿主），开关 off/on × 2 个种子 × 双侧，共 48 Case，约 16 min。
- 另跑 `RunDemoOpponentCheck.ps1 -BotMatch` 开关 off/on 各 1 局，约 2 × 20 min，看完整对局中的野区浪费、就绪时间与循环。
- 通过判据（机制，不看胜率）：on 组每个 LD_EMPOWER 满足 `before − after = cost`；250 ms 内重复触发为 0；如果 SPEC 采用对策 #2，普攻宿主与 D 宿主打营地的触发为 0；每个 AI 至少触发一次（触发 0 次判为无效样本，与 NeverCast 同理，adapter 后续加）。off 组没有 LD_EMPOWER 行，结果落在 Clean Baseline 区间内。
- 胜率与 TTK 的变化只作为参数 Session 的输入，不作为通过判据。

**多人同步最小场景**（真实拥有连接，约 2 min）：扩展 `RunDemoCombatCheck.ps1` 或 `RunDemoGrowthLinkCheck.ps1` 的探针。
- 摆状态：客户端经 `SubmitBuild` 指定宿主；用 DEV 命令把劲设到门槛以上（`LDCombatDevTest.cpp` 若没有设劲命令，由实现者补）。
- 断言：客户端对 Host 的宿主动作回执为已应用；服务器只写一条 LD_EMPOWER，且客户端视图里的劲减少量等于消耗；客户端 HUD 显示就绪状态与宿主；Host 的对手视图看到客户端的就绪标记，强化后标记消失；重发同一订单不产生第二次扣费。

**执行命令**（S5 `70572f88` 合入后，在集成分支的已构建工作区执行；原生由 S0 排机）：

```powershell
$m='docs\research\gameplay\reconstruction\batch-01\research\S5\manifests'
# 原生对抗（同种子两次运行，各 24 Case，合计约 2 × 15 min；on 由清单 empower:true 自动加 -LDEmpower）
.\Scripts\RunBuildEvaluation.ps1 -Manifest "$m\b01-empower-off.json"
.\Scripts\RunBuildEvaluation.ps1 -Manifest "$m\b01-empower-on.json"
# 拥有连接的多人同步（约 2 min）+ 遥测与客户端隐私核对
.\Scripts\RunDemoGrowthLinkCheck.ps1 -ExtraArguments '-LDEmpower'
python -B .\Scripts\SummarizePlaytestLog.py <输出目录> --metrics-only --check-client
# 渲染检查（约 4 min；Host 1080p 截图 + 渲染客户端）：劲条门槛刻痕、宿主槽金边、对手“强化就绪”
.\Scripts\RunDemoCombatCheck.ps1 -Resolution 1080p -RenderClient -ExtraArguments '-LDEmpower'
# 截图没赶上就绪状态时，录 Host 画面复看（不出判定）
.\Scripts\RunDemoCombatCheck.ps1 -CaptureOnly -RecordGame <录制目录> -ExtraArguments '-LDEmpower'
```

现有探针不断言强化本身。多人与渲染两项的强化证据靠事后核对：`SummarizePlaytestLog` 的 Auto-empower 行应有客户端的 applied 触发，且 `before − after = paid`；`--check-client` 通过（`LD_EMPOWER` 属于房主专有行）；截图或录像由人工核对。
## 结果登记

每项把输出目录、`report.md` / `Verdict.json` 路径与“通过 / 失败 / 未运行”写入 `VALIDATION.md`；不能执行的项写明环境原因，不以旧报告充当当前证据。

# T05-04 性能与日志异常静态剖析（L1 源码 + 现有日志计数）

证据：当前源码（工作区 `sinmentis-miniature-guide`，基于 `e62ff304`）；日志计数来自 EXP-01 打包 `Game.log`（6.7 MB）与 EXP-03 `telemetry_7ai_excerpt.log`（截取 7.5 MB，风暴本体已被截掉）。未做原生剖析（未构建、未运行）。

## 结论（按影响排序）

| # | 位置（所有者） | 每帧/每事件做了什么 | 影响 | 最小技术修复（不改玩法） | 置信 |
| --- | --- | --- | --- | --- | --- |
| P1 | `World/LDFieldPacing.cpp:75-110` `FLDObjectiveState::Advance`；`Framework/LDGameModePacing.cpp:147-165`（S1，T02-01） | 每次推进从 Segment 0 扫起；当前段 ≥ 第 2 段时，先把 View 重置成第 1 段、再重建当前段，**每帧清空 `ArenaDamageP2/ArenaLastHitT`** 并返回“已变化” | 每帧：`bPublicMatchDirty`（整份公共视图重新赋值并复制，`LDGameModeCapture.cpp:867,892`）+ `LD_OBJECTIVE` + `LD_ARENA_CLOSE` 两行 Display 日志。EXP-01：`LD_OBJECTIVE` 11,171 行、`LD_ARENA_CLOSE` 11,164 行（占日志行数的大头）；擂台伤害永远累计不起来 → 结算时 `ArenaWinner` 恒为空 → 恒 `Expired` | 只在真实换段时初始化（记住当前段，不从 0 重扫）；仅状态真变化才置脏与写日志 | 已读码确认 |
| P2 | 同上（S1） | 二阶：修好 P1 后擂台能被赢下。若残留“已结算又被重开”的路径，第二次结算有赢家时 `PayPacingReward` → `CommitFieldReward` 按 `(Event, Serial)` 去重返回 `PermanentlyRejected`（`LDGrowthDirector.cpp:471-477`），`CommitOwedFieldReward` 对非 Granted 返回 false（`LDGameModeFieldMarkers.cpp:124-129`）→ Transactions 失败 | 由“刷屏”变为“权威步报错” | S1 的回归测试需覆盖：擂台赢下后继续推进 ≥ 1 段，公共视图保持 `Won`+赢家，不再次结算、不再次付奖 | 推断（读码） |
| P3 | `Combat/LDCombatCards.cpp:1457-1467` A12（S1，T01-01..03） | 每次合格命中更新专注层数并 `RefreshTemporary`，整套属性重投影 | 每次附加命中一次完整重投影；与 P4 相乘 | 派生临时项不变时跳过刷新；附加命中不参与“新增层数”的因果判定（规则侧由 S1 按事件来源处理） | 已读码 |
| P4 | `Combat/LDCombatCards.cpp:695-718` A09（S1） | 按超额攻速复制主命中，每个附加命中走完整命中管线（结算、重投影、`LD_HIT`、复制） | 成本 ∝ 命中数；EXP-03 单拳 111,846 命中 | 服务器每权威步事件技术预算 + 触发时告警（T01-02）；不设玩法上限 | 已读码 |
| P5 | `Combat/LDCombatEffects.cpp:796` `LD_HIT`（未分配，S0） | 每次命中一行 Log | 正常对局 2–8 千行；风暴下与命中数同量级（EXP-03 写到 1.2 GB） | **不要降为 Verbose**：`LD_HIT` 是 SummarizePlaytestLog、BuildEvaluation、JudgeBotMatch 的权威输入，降级会让所有原生证据失效。正确做法是 P4 的事件预算 + 超预算时写一条汇总告警（`LD_EVENT_BUDGET`） | 已读码 |
| P6 | `AI/LDOpponentController.cpp:1104` `LD_AI_DECISION`（S2） | 每次决策一行 Display，无变化检测 | 341 / 603 行，中等 | 仅决策变化时写，或限频；`Scripts\` 中只有 SummarizePlaytestLog 把它当例行行计数（`ROUTINE_TAGS`），不依赖每帧完整 | 已读码 |
| P7 | `Framework/LDGameModeTrace.cpp:135` `LD_HEARTBEAT`（S0） | 每人每心跳间隔一行（非每帧） | 3,885 / 5,928 行，可接受 | 保持：心跳是位置/资源证据，BuildEvaluation 重放比对使用 | 已读码 |
| P8 | `LD_BASKETBALL_PRESENTATION`（S0） | 表现层行 | 1,036 / 190 行 | 若只用于调试，降为 Verbose（S5 已核对：`Scripts\` 无任何脚本读取它） | 计数确认，用途待查 |

`LD_PERF`：两份日志峰值卡顿 1,060 ms（打包）与 1,243.6 ms（遥测截取），未归因；建议集成后跑一次 `RunDemoOpponentCheck.ps1 -Telemetry -Accelerated -Trace`（Insights）在修复 P1/P4 后复测，不在此前猜测原因。

## 日志体积（前若干标签，行数）

| 标签 | 遥测截取 | 打包 Game.log |
| --- | ---: | ---: |
| LD_OBJECTIVE | — | 11,171 |
| LD_ARENA_CLOSE | — | 11,164 |
| LD_HIT | 7,771 | 2,274 |
| LD_HEARTBEAT | 5,928 | 3,885 |
| LD_NAV_ACCEPT | 986 | 1,969 |
| LD_COMBAT_INPUT | — | 1,182 |
| LD_BASKETBALL_PRESENTATION | 1,036 | 190 |
| LD_SELF_ORDER | — | 936 |
| LD_NAV_END | 682 | 483 |
| LD_AI_DECISION | 603 | 341 |
| LD_CAMP_DEATH | 487 | 598 |
| LD_CAST | 433 | 374 |

## 已在 S5 侧落地的检测

- `SummarizePlaytestLog.py` 新增“Field objectives”行与跨局表行：每个 serial 的相位流、是否结束、重复相位行数（EXP-01 输出 `2 of 3; 11163 repeated phase lines`）。修复后 Clean Baseline 以 `N of N` 且无重复为通过（见 T05-05）。

## 交给其他 Session

- S1：P1/P2（含上表回归测试要求）、P3/P4（事件预算与重投影跳过）。P5 的“不要降级 LD_HIT”约束。
- S2：P6。
- S0：P5 预算告警标签与 P8 归属。

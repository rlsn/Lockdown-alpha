# Batch 01 Validation Ledger

只记录本批次实际执行的验证；通过 / 失败 / 未运行分开写，不引用旧报告充当当前证据。

## Clean Baseline 验收项

| 项 | 方法 | 级别 | 结果 | 证据 |
| --- | --- | --- | --- | --- |
| 触发风暴不可复现 | 新增规则测试 + 原生整局 | L1/L4 | 规则测试**通过**（A12A09NoFeedback、HitBudget）；3 局整局 `LD_CHAIN_GUARD`=0；A12+A09 假人手动复现（L5）未运行 | 同上 |
| F2/F3 正常完成 | 规则测试 + 原生整局 | L1/L4 | **通过**：S2 `751751c4` 后 3/3 目标结束（arena 与两面小旗均 Expired，无人取胜）；早先“擂台不结束”为日志缺口 | `Artifacts61128-180852-999-DemoOpponent-BotMatch` |
| 导航失败修复或隔离 | MapTraversal + BuildEvaluation c001 | L1/L3 | **已隔离**：c001 不再 90 s 寻路死锁，但 24/24 Case 因与营地 h10 接触判 Invalid（ExternalInteraction）；根因为 h10 横在西南入场通道，需关卡负责人移点 | 同上 |
| AI 正常施放已有技能 | BuildEvaluation 样板施法计数 + BotMatch | L3/L4 | **通过**：BotMatch AI 零施法 = 0；p3 c000 种子 12 arm 全部有效完成（无 NeverCast） | `Artifacts\BuildEvaluation\20261128-165308-p3-breakout-samples\report.md` |
| 原生整局完成终局与赛后 | `RunDemoOpponentCheck.ps1 -BotMatch -MatchCount 3` | L4 | **通过**：3/3 局锁定结果（GuardSucceeded×2、ClearOut×1，813–913 s），无错误/权威故障；警告 LD_CAMP_PATH_STALLED×30、RETURN_BLOCKED×10、RETURN_DEAD_END×12（营地寻路，关联 #353）。仅 Host + AI，非真实拥有连接与结算界面 | 同上 |
| 正式玩法规则未被擅自修改 | diff 审查（SourceData / 数值常量） | L1 | **通过**：SourceData 只有 Y29 `shield_cap` 0.3（规格明确，T02-08）；两个 Sync `--check` 通过 | `git diff main...b01-integration -- SourceData` |
| 新增回归测试通过 | `Verify.ps1 -Filter <全名>` | L1 | **通过** 16/16（部分运行，非门禁） | `Artifacts\20261128-165106-250-Automation` |
| `Verify.ps1` 通过 | 集成分支一次 | L1 | **通过**（增量构建 + 选定检查；完整规则套件未运行，交 CI） | `Artifacts\20261128-164414-003-BuildEditor`、`Artifacts\Verification\20261128T035101Z-b6311a3935c8.log` |

## 执行记录

| 时间 | 分支@提交 | 命令 | 结果 | 证据路径 |
| --- | --- | --- | --- | --- |
| 2026-11-28 16:44 | `sinmentis/b01-integration`@`8a0c5de8`（S1 至 `398d2928` + S2 + S5） | `Verify.ps1 -Filter <16 项>` | 构建通过；16/16 通过 | 见上 |
| 2026-11-28 16:55 | 同上 + S5 `33b89958` | `RunBuildEvaluation p3-breakout-samples`；`RunDemoOpponentCheck -BotMatch -MatchCount 3` | 运行中 | `Artifacts\b01-p3-breakout.log`、`Artifacts\b01-botmatch.log` |
| 2026-11-28 17:10 | 同上 | BotMatch ×3 汇总 | 野外 TTK 中位 5.2 s（n=105）；同级 TTK 中位 7.0 s（n=15）；野外时间占比 55%（<65% 健康线，B03）；劲收入 combat 40% / basic 24% / skill 15% / reaction 6% / dodge 0.8% | `Artifacts\20261128-165308-081-DemoOpponent-BotMatch\Summary.json` |
| 2026-11-28 17:15 | `8a0c5de8`+S5 | p3-breakout-samples（48 Case） | c000 有效：同预算参照 bare_spent TTK 11.7 s；skill_engine 4.1 s、element_loop 5.1 s、attack_speed 5.5 s、snowball 6.5 s、big_one 7.0 s、berserk 7.1 s、lifesteal 7.4 s、minefield 10.0 s；hedgehog 14.3 s、dash_ghost 14.2 s 慢于参照；旧裸基线 bare 21.6 s。c001 全部 Invalid（h10）。n=1 种子，描述性 | `Artifacts\BuildEvaluation\20261128-165308-p3-breakout-samples` |
| 2026-11-28 17:16 | `301b76c9`（+S2 f3201b62/e6fa28c4/f0d09d13） | Verify + 4 项定向；X1/X2 7 份清单 76 Case | 构建与 4/4 测试通过；76/76 runnable 完成（描述性，InsufficientEvidence，S5 解读中） | `Artifacts\BuildEvaluation\20261128-17*-b01-x*` |
| 2026-11-28 18:08 | 集成 + S2 `751751c4` | KeptAcrossSteps + BotMatch ×1 | 通过；目标 3/3 结束；终局 NoChampion（150 s 超时无人胜出，终局属 B04；超时历史进度 R5 修复待 S1 合入）；AI 零施法 0 | 同上 |
| 2026-11-28 18:33 | `8b68cf1e`（+ S1 删除分支与 A02 默认项） | Verify + 14 项定向；BotMatch ×2 | 构建通过；14/14 通过；2/2 局锁定结果（GuardSucceeded、ClearOut）；目标 6/6 结束；AI 零施法 0；劲收入无爆劲 / 精准闪避项（n/a）。**新问题**：`LD_BOSS_PATH_REJECTED` ×12823 每帧刷屏（Boss 追击起点不在导航面）→ 交 S2 | `Artifacts\20261128-183326-418-Automation`、`Artifacts\20261128-183436-084-DemoOpponent-BotMatch` |
| 2026-11-28 19:20 | `71eba3d2`（+ S2 `33a9355a` `b164c94c`；adapter v18） | 4 项定向；BotMatch ×2 | 4/4 通过；2/2 局 GuardSucceeded；目标 6/6；AI 零施法 0；`LD_BOSS_PATH_REJECTED` 12823→2；`LD_AI_DODGE result=dash` 40 次（生产 AI 躲避生效） | `Artifacts\20261128-192029-456-Automation`、`Artifacts\20261128-192202-616-DemoOpponent-BotMatch` |
| 2026-11-28 19:36 | + S5 `7fd5c49e` | X1/X2 第二轮（6 份清单，256 Case，clear_camps、每 arm 8 种子） | 运行中 | — |
| 2026-11-28 20:38 | `d102204d`（+ S1 战斗批次） | 8 项定向；BotMatch ×1 | 8/8 通过；1/1 局 GuardSucceeded；目标 3/3；AI 零施法 0；无错误 | `Artifacts\20261128-203851-478-Automation`、`Artifacts\20261128-204020-767-DemoOpponent-BotMatch` |
| 2026-11-28 19:36–20:30 | `71eba3d2`+`7fd5c49e` | X1/X2 第二轮 256 Case | 254 完成；躲避承伤 −22%（8/8），250/450 ms 无差；预判对横移目标无价值；远程站桩五五开、风筝 −23%；瞄准影响远程、时机影响近战 | `research/S5/X1_X2_RESULTS.md` §6 |
| 2026-11-28 20:48 | + S1 `30bbadfb` `cffad170` | 3 项定向；X1 追加 80 Case；急眼了阈值 1.6 扫描 | 3/3 通过；80/80 runnable（S5 解读中）；急眼了：单人 0 触发、围攻保留 100%、墙边首逃 1.91/1.96 s（判据之间） | `Artifacts\20261128-205132-948-Automation`、`Artifacts\BuildEvaluation\20261128-2052*`、`Artifacts\ProtoAgency\agency-probe-20261128-211121.json` |
| 2026-11-28 21:17–21:24 | `02b6bde5`+ | 急眼了扫描 1–4（S3b §10） | T1.2/W3：VCut 墙边首逃 1.48 s（通过），但 D 普通围攻 1.32 s 触发（早于自然 1.47 s）、贴墙单人 1.47 s 触发；T1.3/W3：1.61（之间），D 1.42；T1.4/W2.5：1.71，D 7.84 s；RollFrom2 无影响。与 S3b 模型预测一致 | `Artifacts\ProtoAgency\agency-probe-20261128-2117*–2123*` |
| 2026-11-28 21:27 | `b2314e3c`（+ S1 `63501cc8` `35168f6a`、MR-Impl `9a9f28aa`：自动强化 + S2 AI；schema 29；adapter v19） | Verify 22 项；扫描 5–6；GrowthLink -LDEmpower；BotMatch -LDEmpower；empower off/on 48 Case | 首轮 `DemoVision.Privacy` 失败（EmpowerReady 未列入批准字段）→ `2bf56d7f` 修正后：Verify 22/22 通过 | `Artifacts\20261128-213243-895-Automation` |
| 2026-11-28 21:37 | `2bf56d7f` | 急眼了扫描 5–6（嘲讽权重 1.0） | 5：T1.3/W3 → VCut 墙边首逃 **1.45 s 通过**，但普通围攻 D 1.42 s 触发（早于自然 1.47）；6：T1.4/W2.5 → **1.55 s**（之间），D 首轮不触发（7.84 s），贴墙单人 5.2 s；VNext 均 1.60。与 S3b 预测一致 | `Artifacts\ProtoAgency\agency-probe-20261128-213702.json`、`-213725.json` |
| 2026-11-28 21:38 | `2bf56d7f` | GrowthLink `-LDEmpower`（真实拥有连接） | **通过**（强化断言含宿主回执、视图就绪、对手可见性、单次扣费） | `Artifacts\20261128-213729-309-DemoGrowthLink-Owned` |
| 2026-11-28 21:39 | `2bf56d7f` | BotMatch `-LDEmpower` ×1 | **通过**：GuardSucceeded；目标 3/3；AI 零施法 0；183 次强化（2.86/人/分钟），宿主普攻 68% / Q 32%，处理器拳 70% / 打 30%，目标参赛者 98%，触发前劲均 75.5，就绪占 20%，250 ms 内重复 0；**单次强化最多命中 20 次待查** | `Artifacts\20261128-213909-606-DemoOpponent-BotMatch` |
| 2026-11-28 21:44 | `2bf56d7f` | 自动强化 off/on 各 24 Case | 48/48 runnable（S5 解读中） | `Artifacts\BuildEvaluation\20261128-214404-b01-empower-off`、`-214716-b01-empower-on` |
| 2026-11-28 21:50 | `2bf56d7f` | 渲染 CombatCheck `-LDEmpower` 1080p | **失败**：追击普攻命中距离 268.3 cm > 投射射程 250（同场景不带 `-LDEmpower` 通过，247.9 cm）→ 交 MR-Impl | `Artifacts\20261128-215051-754-DemoCombat-OwnedOrders-1080p`、`Artifacts\20261128-215507-058-DemoCombat-OwnedOrders` |
| 2026-11-28 21:57 | — | 自动强化 off/on 解读（S5） | 开启后击杀提前 8–16%（元素 +7% 例外）、样板承伤 −8~−37%；仅地雷阵 1 例翻转胜负；守 / 动处理器样板方未出现；拳间隔 2.7 s（L2 预测 6–9 s，快 2–3 倍，因进场劲 75.5）；“单次 20 命中”为汇总脚本缺陷（已修 `624a7fb9`，实际最多 5） | `momentum-r/critique/EMPOWER_NATIVE_RESULTS.md` |
| 2026-11-28 22:18 | `sinmentis/b01-integration`（+ MR-Impl 探针修正 `74ee62fd`） | 渲染 CombatCheck 1080p，关 / 开 `-LDEmpower` | **两态均通过**（追击普攻命中 248.96 / 248.33 cm）；开启态 HUD 劲条显示“劲·普攻”、门槛刻痕、达门槛变金；关闭态显示技能劲耗。**未在截图中覆盖**：C-12 前摇落点预警、“你被锁定”、承诺环、对手“强化就绪”标记（需要定向截图场景或真人渲染会话） | `Artifacts\20261128-221803-268-DemoCombat-OwnedOrders-1080p`、`Artifacts\20261128-222211-093-DemoCombat-OwnedOrders-1080p` |

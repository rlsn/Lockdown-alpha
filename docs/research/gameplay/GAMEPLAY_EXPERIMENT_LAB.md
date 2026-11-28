# Lockdown 自动化游戏性实验工坊与实测证据录 (Gameplay Experiment Lab & Evidence Ledger)

为实现“低成本、可复现、与实机互校”的游戏性研究，本次计划复用了仓库既有的 `BuildEvaluation` 原生无头评估框架、`SummarizePlaytestLog` / `SummarizeOpponentTelemetry` 遥测管线，并新增了零侵入的确定性游戏性实验引擎 **`Scripts\GameplayResearchLab.py`**（配套单元测试 **`Scripts\TestGameplayResearchLab.py`**）。

---

## 1. 实验基础设施分层架构

| 层级 | 工具与路径 | 运行成本 | 核心能力与边界 |
| --- | --- | --- | --- |
| **L1：静态图谱与精确代数审计** | `Scripts\GameplayResearchLab.py`<br>(`CatalogModel` + `extract_interaction_graph`) | **~0.2 秒**（纯 Python） | 直接解析 `SourceData\Demo\content.json` 与 `world.json`，输出 [data/interaction_graph.json](data/interaction_graph.json)，自动校验营地经验总盘、属性点边际 ROI、闭环链路与词汇错配。 |
| **L2：蒙特卡洛选牌与 8 策略 Bot 锦标赛** | `Scripts\GameplayResearchLab.py`<br>(`simulate_draft_and_formation`, `simulate_combat_matrix`, `simulate_ffa_tournament`) | **~25 秒**（运行 15,000 次选牌 + 2,500 场对决 + 3,500 局多人锦标赛） | 内置 **8 类公平局部视野策略 Bot**（`Farmer`, `Aggressor`, `Opportunist`, `ObjectiveHunter`, `Turtle`, `ComboSpecialist`, `BuildOptimizer`, `Beginner`）及独立标注的 **离线全图先知上界 Bot (`OracleFullInfo`)**，支持对还未写进 C++ 的重构方案（A/B/C/D）做低成本反事实压测。**边界声明**：模型胜率不等同于真实三维微操胜率，关键结论必须经 L3 原生实机校准。 |
| **L3：Unreal 5.8.4 原生 60Hz 确定性对抗** | `Scripts\RunBuildEvaluation.ps1`<br>(`-nullrhi -UseFixedTimeStep -FPS=60`) | **~3–12 分钟 / 批** | 在真实 `L_Demo` 关卡导航、碰撞与权威 `LDCombatDriver` 上执行成对交换种子对抗，捕获真实寻路死锁、AI 技能漏放与精确毫秒级 TTK。 |
| **L4：Unreal 5.8.4 原生整局加速遥测与实机日志** | `Scripts\RunDemoOpponentCheck.ps1 -Telemetry -Accelerated` + `Scripts\SummarizePlaytestLog.py` | **~4–8 分钟 / 局** | 满房 8 席（Host + 7 AI）跑完 `R0 → F1..F3 → R3 → Showdown` 全部状态机，实测捕获跨阶段重置漏洞与极限同帧触发风暴。 |

### 快速复现命令
```powershell
# 1. 运行研究实验工坊单元测试（4 项确定性与目录对齐测试）
python -B -m unittest discover -s .\Scripts -p TestGameplayResearchLab.py

# 2. 重新生成完整交互图谱与全套蒙特卡洛/对抗/FFA 实验数据
python -B .\Scripts\GameplayResearchLab.py --seed 20261128

# 3. 核验本地仓库验证计划
.\Scripts\Verify.ps1 -Plan
```

---

## 2. 八大策略测试 Bot 定义与权限边界

在 `Scripts\GameplayResearchLab.py`（`BOT_PROFILES` 与 `simulate_ffa_tournament`）中，所有标准测试 Bot 严格受限于 `1120 cm` 局部视野半径、`240 cm/s` 真实移速耗时与合法属性点预算：

| Bot 策略画像 | 视野/信息权限 | 核心决策偏好与测试目的 | 8 人局现行规则 (`V1`) 胜率 | 8 人局轮换占点 (`V2`) 胜率 | 8 人局单旗拔河 (`V5`) 胜率 |
| --- | --- | --- | --- | --- | --- |
| **`Farmer`（刷野发育狂）** | 公平迷雾 | 优先清最近野怪、回避早期冲突、冲高等级 | `9.2%`（8p 缺怪饿死） | `0.4%`（不占点必输） | `7.2%` |
| **`Aggressor`（狂战猎手）** | 公平迷雾 | 见人主动开战、抢空投、必报中场 PK | `10.8%`（被黄雀收割） | **`18.4%`** | `12.0%` |
| **`Opportunist`（黄雀收割者）** | 公平迷雾 | 蹲守交战区边缘，专抢 `<62% HP` 残血与野怪尾刀 | **`15.6%`**（现行生态受益者） | `15.2%` | `12.8%` |
| **`ObjectiveHunter`（目标控图王）** | 公平迷雾 | 控空投/迷你旗，终局积极抢喷漆棘轮与守点 | `11.6%`（被多人踩圈冻死） | **`25.6%`** | **`16.0%`** |
| **`Turtle`（铁龟肉山）** | 公平迷雾 | 堆体格/脸皮护盾，避战保命，拖终局缩圈与超时判胜 | **`24.0%`**（**现行头号毒瘤主导策略**） | `3.2%` | `20.4%` |
| **`ComboSpecialist`（极限连招师）** | 公平迷雾 | 强搜高协同技能与稀有催化卡，吃操作上限 | `10.0%` | **`16.4%`** | `9.2%` |
| **`BuildOptimizer`（全局构筑高手）** | 公平迷雾 | 兼顾刷野节奏、灵活看牌转型与关键石卡点 | `10.8%`（终局被肉盾抹平） | **`20.8%`** | `14.4%` |
| **`Beginner`（新手乱点玩家）** | 公平迷雾 | 路线低效、随机选牌、不善闪避与商店刷新 | `8.0%`（乱局超时混赢偏高） | `0.0%` | `8.0%` |
| **`OracleFullInfo`（全图先知上界）** | **离线全图透视** *(仅作理论上界对照)* | 精确计算全图最高 XP/距离比营地与残血坐标 | `15.2%`（证明单圈冻结连全图挂都难破局！） | — | — |

---

## 3. 本次研究全部实证实验台账（Evidence Ledger）

所有实验均在 Commit `e62ff30473ce02d73761aadaea092d8a7e965248`、Catalog `fc05a7db4abfe62ebc956fdf2e0167a508c91048e529471c8b36623defd3c142` 下执行，绝无伪造数据：

| 实验 ID | 实验类型与方法 | 随机种子与样本量 | 执行命令 | 核心实测结果 | 不确定性与边界 | 原始证据路径 |
| --- | --- | --- | --- | --- | --- | --- |
| **EXP-01**<br>打包版真人试玩复盘 | 真实客户端交互对局日志法医解析（1 真人 + 4 AI，完整四段） | 实机单局（`776.8s`） | `python -B .\Scripts\SummarizePlaytestLog.py "Artifacts\20261128-123211-963-Packaged-Interactive\Game.log"` | 真人 L16（`81` 属性点，`0` 商店购买）`10/0/1` 夺冠；AI 卡在 L7–L10；完美闪避仅 `6` 次；**捕获 `LD_OBJECTIVE` 每帧重置刷屏 `22,335` 行**；发现 `SummarizePlaytestLog.py` 裸基准虚高 `2.29×`。 | 样本 $n=1$，对手全为当前存在策略缺陷的 AI，不能代表高水平真人互打。 | `Artifacts\20261128-123211-963-Packaged-Interactive\Game.log`（本机忽略目录） |
| **EXP-02**<br>Unreal 60Hz 十大质变样板原生评估 | 真实 UE 5.8.4 无头空间对决（`duel_site` @ `capture`） | Root Seed `20261290`；2 独立种子 × 双侧交换 = **44 Cases** | `.\Scripts\RunBuildEvaluation.ps1 -Manifest Scripts\BuildEvaluationManifests\p3-breakout-samples.json` | 1) Seed `c000` 实测裸构筑 TTK `21.38s`，`skill_engine` `2.78s`（`7.68×`）、`attack_speed` `4.21s`（`5.07×`）、`element_loop` `4.41s`（`4.85×`），证明核心乘区具备破格潜力；<br>2) **捕获 `hedgehog` 与 `dash_ghost` 生产 AI 全场 `0` 次施放主动**；<br>3) **捕获 Seed `c001` 出生点 `(-4772,-3546,502)` 寻路死锁 90s 超时**。 | `duel_site` 使用固定 AI Engage 微操且 8 个预设未分配 48 属性点。 | `Artifacts\BuildEvaluation\20261128-131440-p3-breakout-samples\report.md` |
| **EXP-03**<br>Unreal 60Hz 满房 8 席整局加速遥测 | 真实 UE 5.8.4 无头整局运行（1 空闲 Host + 7 AI） | 实机随机种子，跑至 `t = 740.04s`（R3 末） | `.\Scripts\RunDemoOpponentCheck.ps1 -Telemetry -Accelerated -MatchCount 2 -AICount 7` | 1) 78 条野外命均值 TTK `5.4s`（中位数 `4.8s`），普攻伤害占 `42.1%`、技能 `51.3%`、卡牌直伤仅 `6.5%`；完美闪避仅 `8` 次；<br>2) **捕获 P0 级同帧指数爆炸 Bug**：`AI 对手 2`（`A12+A09`）在 R3 打训练假人单拳飙至 **`111,846` 次命中**，挂死权威服务器！ | 因 R3 假人触发风暴挂死，未自然进入该局 `FinalField`（已截取 `7.5 MB` 完整前置证据并回收磁盘）。 | `Artifacts\GameplayResearch\telemetry_7ai_excerpt.log` |
| **EXP-04**<br>选牌与构筑成型蒙特卡洛 | 精确复刻 `100/22/6` 权重与词汇加权，对比 5 套选牌/商店规则（含智能槽位汰换） | Seed `20261128`（SHA-256 跨进程确定性种子）；`5` 组 × **`3,000` 局** = **15,000 局** | `python -B .\Scripts\GameplayResearchLab.py --seed 20261128` | 现行 `S0` 质变率仅 `24.1%`、传说单次可见率仅 `1.81%`；解耦商店货币 (`S3`) 令转型恢复率从 `65.3%` 升至 `85.4%`；混合引导 (`S4`) 质变率达 `62.6%`、破格率 `16.0%`。 | 模拟采用理性启发式评分选牌，未包含人类选手误读描述的概率。 | [data/experiment_results.json](data/experiment_results.json) |
| **EXP-05**<br>离散步长 12 流派战斗矩阵与爆劲 A/B | `50ms` 步长精确伤害/抗性/连招/反应模拟，含爆劲蓄力开关对照 | Seed `20261128`；12×12 流派 × 16 局 + 等级差矩阵 = **2,500+ 场对决** | `python -B .\Scripts\GameplayResearchLab.py --seed 20261128` | 证明 `+2` 级同流派胜率即逼近 `100%`；证明高频技能流开启 `0.35s` 蓄力爆劲使总 DPS 从 `1301.7` 暴跌至 `889.0`（`-31.7%`）；定位流派克制环与 `snowball_reaper` 单挑弱势。 | 一维距离/命中率近似，弹道躲避率按固定技能参数抽样，不等同实机三维地形拉扯。 | [data/experiment_results.json](data/experiment_results.json) |
| **EXP-06**<br>多人敏感度 (`2/4/6/8p`) 与单因子阶梯 A/B | 基于真实营地坐标、移速、迷雾视野与严格占圈规则的 8 策略 Bot 锦标赛 | Seed `20261128`（SHA-256 确定性种子）；15 组配置 × **`250` 局** = **3,750 局完整比赛** | `python -B .\Scripts\GameplayResearchLab.py --seed 20261128` | 8p 基线人均等级 `8.86`、第三方收割 `42.7%`、终局超时 `54.0%`；小/中野 `65s` 刷新使均等升至 `11.30`、收割降至 `17.6%`；**反证验证**：单旗改良 (`V5`) 虽将主旗守赢从 `8.8%` 提至 `28.0%`，但仅靠单圈无法消除 8p 超时（`48.4%`），唯有轮换双点 (`V2`, `0%`) / 皇冠持有 (`V3`, `6.4%`) / 听牌赛点 (`V4`, `0%`) 能根除僵局。 | 策略模型采用 10s 宏观切片，最终趣味性与社交观感必须经真人盲测。 | [data/experiment_results.json](data/experiment_results.json) |

---

## 4. 必须由真人试玩回答的问题与标准试玩协议（Human Playtest Protocol）

> **铁律**：Bot 与蒙特卡洛模拟只能证伪“数值死锁、经济断档、概率不可达与代码 Bug”，**永远无法证明人类玩家觉得操作爽快、挫败可接受或想再来一局**。

在完成 P0 级 Bug 修复与最小实验开关后，必须执行以下 **8 人真人盲测协议（3 局 × 15 分钟 = 45 分钟标准场）**：

1. **盲测分组设计（Within-Subject ABBA 交叉）**：
   - **第 1 局**：修完 P0 Bug 后的现行规则基线（`240 cm/s`、`0.35s` 按住爆劲、单旗独处冻结）。
   - **第 2 局**：实验组 B1（`280 cm/s` + `Alt` 瞬发爆劲 + 每轮休息 `2` 点商店津贴 + `950 cm` 拔河终圈 + 无休息室强制 PK）。
   - **第 3 局**：自由选择最想复仇或复现的一套流派（观察玩家是否主动说出“上局我看到某某流派很猛，这局我想凑一套”）。
2. **赛后即时定量问卷（1–5 分制 + 一句话复述，不引导答案）**：
   - **死亡归因清晰度**：“刚才最后一次阵亡，你能准确说出对手靠什么核心机制杀死了你吗？”（核对玩家回答与真实 `LD_HIT` 账本一致率，目标 `>= 75%`）。
   - **构筑身份感知**：“这局第 6 分钟以后，你的走位和按键顺序和开局第 1 分钟有明显不同吗？”（目标 `>= 4.0/5.0`）。
   - **终局服气度**：“无论这局你拿第几名，你觉得冠军赢下终局的方式合理、精彩吗？”（重点监控落后翻盘局与被翻盘头名的挫败感）。
   - **真实再来一局冲动（Behavioral Retention）**：3 局正式测试宣布结束后，服务器保持开放 20 分钟——**直接统计有多少名测试者在无人要求的情况下自发点击了“再来一局”！** 这比任何口头好评都更真实。

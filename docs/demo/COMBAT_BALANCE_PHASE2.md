# 战斗平衡 Phase 2：原生对照与结构候选

> **结论：CONCERNS；整体平衡 NOT ASSESSED。** B07 的固定区域伤害漏读来源加成已复现并修复；强化普攻、暴击和 B04 的乘法收益已在受控原生结算中观察到，但尚不能据此认定普攻流全面占优。一个 L10 原生近战对照对所测法师配置不利，不代表远程、区域或整个法师流派。严格跨进程重放仍失败，针对相应重放的确定性结论暂停。
>
> 此文是第二轮独立复审材料，完整 S01–S22 和 B/M/C 目录及扩展创意见[第一轮审计](COMBAT_BALANCE_AUDIT.md)。第一轮 AP 改动按用户要求保留；没有可信的 AP 5→8 原生配对因果实验，不把它称为已验证平衡。

## 1. 证据、输入和身份

| 标记 | 含义 |
|---|---|
| 代码事实 | 当前源码、内容目录或授权合同；不是对局表现 |
| 原生受控 | Editor 中调用真实 Combat 结算，但夹具可能直接投影安装、固定位置、强制命中或暴击；不是完整对局 |
| 原生对局 | 真实地图、权威身体及生产 Engage AI 的自动对决；不是人类客户端或真人玩法 |
| 解析 | 规定输入下的算术或统计，不隐含未测行为 |
| 设计判断 / 假设 | 需要对照或玩家测试，不是已批准的运行规则 |
| NOT TESTED | 没运行、证据不充分或现有工具不支持；不按通过计 |

| 必需来源 | 状态与用途 |
|---|---|
| `README.md`、`docs\APP_SPEC.md` | FOUND；Demo 范围和产品规则 |
| `docs\DEVELOPMENT.md`、`docs\systems\COMBAT_AND_GROWTH.md`、`docs\systems\BUILD_EVALUATION.md` | FOUND；真实执行接口、开发检查与 Harness 限制 |
| `docs\demo\READINESS.md`、`IMPLEMENTATION_PLAN.md`、`SHARED_CONTRACTS.md` | FOUND；跨系统契约和验收边界 |
| `docs\demo\CONTENT_AND_BALANCE.md`、`SourceData\Demo\content.json`、`SourceData\Demo\content.schema.json` | FOUND；当前内容、规则和输入合同；实际为 22 主动、18 基础、12 改装、12 连接 |
| Combat、Growth、AI 源码及 `Scripts\BuildEvaluation*.py` | FOUND；逐项区分生产路径与测试入口 |
| 审批过的胜率、TTK、DPS 或实质差异目标 | **ABSENT / NOT ASSESSED - NO TARGET**；下列区间不是达标判定 |
| 真实玩家和完整地图交战证据 | **ABSENT**；不能证明玩家乐趣或多人战术强度 |

工作区基础 HEAD 为 `f02eed0470aa891dac5fc2f69043cfea02b87f42`，两轮均有未提交改动；原生记录的 `source_dirty` 是 `unknown`。内容根为 `2196dd4f3663e6f1945abbd799e49ff7d34313dc809d31bee1372bb79302526a`，引擎为 UE `5.8.4-58902052+++UE5+Release-5.8`。L10 对照使用 Adapter v10、60 FPS，模块 SHA256 为 `a4eeb6ddd5bf6a2e1edcb2eecee61458c01426596af0df86b05234023ad77c88`。

源码工作树、Adapter 和模块后来继续变化；旧记录有模块摘要、清单及事件，但 `source_dirty=unknown` 不是可重建的源码快照。当前 checkout 运行相同清单可重做**当前版本的类似实验**，不能据此声称重建了该旧二进制，亦不能与不同身份的数据合并。

## 2. P0：评估基础设施

### 2.1 已实现

**代码事实。** 修复开局固定主动被 `LD_DEV_FIXTURE_PRESET_REFUSED` 拒绝的问题，并增加逐级固定构件的诊断供给：仅在开发自动化编译、显式 `-LDDevGrowthFixture`、`-LDBuildEvaluation`、权威 Demo 房间及已武装的评估 AI 槽条件下启用。主动候选受目录和资格校验；逐级构件经过供给校验与正常奖励提交，随后经正常构筑命令安装。它不把普通局的随机抽取概率改成预设，也不是玩家可用的授权入口。诊断清单中可用单张主动候选，不代表正常开局三选一的发生概率。

`LDBuildEvaluationProbe` 记录真实野外双方脚点；同一权威时刻的双杀按同一时刻判读。`Scripts\CombineBuildEvaluation.py` 在汇总前检查实验设计、Adapter/内容/二进制身份、种子去重、Case 完整性、已记录违规及实际换边。统计单位是**每个种子内两边的均值**，不是把两边和两组的 Case 当作独立样本。Student-t 95% 区间是有限样本的近似，不取代预先批准的效应阈值。

有版本化、可审计的诊断清单：`Scripts\BuildEvaluationManifests\phase2-fixed-opening.json` 与 `phase2-b04-c08-smoke.json`。固定某个合法候选仍是人为干预；它证明该候选的后续事务，不证明自然获取率。

### 2.2 尚未实现

没有新增地图或新的 AI 策略。对局仍在现有 `L_Demo` 的 `duel_site`，策略为生产 Engage AI；动态风筝、主动躲避弹道、追击脱身、主动控制排序均 **NOT IMPLEMENTED / NOT TESTED**。AI 可观察信息和导航要先按现有授权视野设计，不能让策略读取隐藏弹道或敌方构筑。

真实墙体/身体阻挡的联机运动、3–6 人 FFA、区域争夺、拥有连接和玩家试玩均 **NOT TESTED**。已有原生冲撞/撞墙单元测试直接调用碰撞结算，不是实际角色撞墙场景。没有取得完整的合法构筑搜索、资源争夺指标或服务器事件负载对照。

### 2.3 重放阻塞

**真实原生测试：严格重放失败。** `Artifacts\BuildEvaluation\20261112-223358-phase2-b04-c08-smoke` 的 `c000a-b04_c08` 跨进程重放 `Artifacts\BuildEvaluation\20261112-223600-replay-c000a-b04_c08` 被当前比较器判为 `RuleViolation`。46 条规范事件中，同一时间双方独立成长选择/加点的顺序不同；原多 Case 进程的绝对 match generation 为 2，单 Case 重放为 1。其余规范事件、终态和资源一致；135 个空间样本最大偏差 0 cm。

对 §3.2 第一批 `c000a-b04_c08` 另做同模块跨进程重放 `Artifacts\BuildEvaluation\20261112-230250-replay-c000a-b04_c08`，两次模块 SHA256 都是 `793dba6ec164546f1845e91161895f7f54d0f80c4ceae2174a3db6a90feddeb9`。比较器判 **RuleViolation**：41 条规范事件中的首次差异是 t=33 双方选择顺序，其后 t=15550 选择与 t=15800 加点顺序也互换，出生记录的绝对 generation 为 2 / 1。身份、终态、资源均一致，72 个空间样本最大偏差 0 cm；其余规范事件一致。

这解释了**已观察到的**差异，不能把严格检查改称通过，也不能证明其他种子或场景都只有无害差异。不可把所有同刻事件排序后静默通过：须先为相对 generation 和已证明独立的准备事件建立红绿测试，同时保留同主体及战斗因果顺序，再重新运行原生重放。L10 对照未做这一跨进程重放；B04/C08 对局结果在此问题解决前仅作探索性描述。

### 2.4 自动报告文字错误

Harness 自动生成的报告目前错误地把 `duel_score=0` 一律描述为到时删失。**代码事实：** 0 也可以是同帧互杀，只有 timeout 删失；应查看原生 `outcome.kind` 和指标的 `censored`。§3.2 的 14 场 0 分均为互杀、没有删失。该报告文字尚未修复，不影响本节按原生记录重新核对的分类。

## 3. 公平配对：已测及其边界

### 3.1 L10 法师配置与普攻配置

**原生对局，非 AP 调整数值前后实验。** 两批根种子 `20261112`、`20261113`，每批 15 个独立种子；每种子两个方案、双方出生点互换，共 120/120 Case 完成，30/30 种子的两组各自真实换边。数据与种子映射位于 `Artifacts\phase2-paired-l10\combined-v10-verified.json`。

双方均 L10、30 点、无安装构件、开战满 1142 HP / 478 MP；固定主动候选及 1440 经验为 DEV 准备。对手固定 S01/S17/S03、26 力道＋4 身手。主体基准与对手同构；另一组主体 S01/S18/S06、30 脑力。只测一项中心对决，角色既换主动又换加点，不能把差异归因于其中一项。

| 指标，按每种子两侧平均 | 法师组 − 普攻组 | 95% 近似区间 | 样本 |
|---|---:|---:|---:|
| 对决得分 | −0.367，标准差 0.765 | [−0.652, −0.081] | 30 组种子 |
| 主体造成的实际 HP 损失 | −135.53，标准差 112.92；范围 −339.865 至 6.39 | [−177.69, −93.37] | 30 组种子 |

30 组中法师组两侧均输 16 组、各胜一侧 9 组、两侧均胜 5 组；60 场的 19 胜/41 负仅描述 Case，不是 60 个独立样本。没有实质差异目标；观察到的对照不等于法师整体胜率，也不能证明远程风筝、范围收益、玩家反制或原版 AP5 与当前 AP8 的差异。

### 3.2 B04 与 C08 的增量

两批配对的记录与状态见本节最终结果。两组主体均 L3、9 点力道、同样获得并安装 B04，也同样获得 C08；实验组额外把 C08 安装到 B04，对照组保留 C08 未安装。双方固定同一开局及 L3 主动；**对手构件仍按种子抽取**，不是双方均被固定获得 B04/C08。两组主体的获取数量相同，已使用的安装效果不同，故这是 C08 启用的边际，而不是等效成熟构筑、自然抽取或 B04 吸血范围方案的 A/B。

已冻结批次清单的文字假设误写成“两侧”均固定构件；其具体方案及原生记录只对主体指定构件，本文按实际执行解释。冻结文本中的“固定装备”指构件，不是装备系统；仓库模板已纠正措辞，未改写旧证据。

**原生对局，探索性。** 根种子 `20261115`、`20261116`，各 15 个独立种子，每种子两个方案、两侧互换；120/120 Case 完成，30/30 组各有两种方案的真实换边，0 条记录违规，关键指标无删失。批次为 `Artifacts\BuildEvaluation\20261112-224212-phase2-b04-c08-paired-a` 和 `Artifacts\BuildEvaluation\20261112-225140-phase2-b04-c08-paired-b`，合并及逐 Case 映射为 `Artifacts\phase2-b04-c08-paired-30-verified.json`。两批均为 Adapter v11、60 FPS、相同内容根及模块 SHA256 `793dba6ec164546f1845e91161895f7f54d0f80c4ceae2174a3db6a90feddeb9`；`source_dirty=unknown` 不等于源码快照。逐侧检查 60/60 对的主体分支前缀、对手主动供给与选择、L1→L3 构件供给及 L3 构筑操作一致。

每组先对两侧求均值，再计算实验组减对照组；以下区间是 30 个种子的近似 Student-t 95% 区间，不能把 120 个 Case 当作独立样本：

| 指标 | C08 启用 − 未安装，均值 | 标准差；种子均值范围 | 95% 区间 |
|---|---:|---|---|
| 对决得分 | 0.000 | 0；0 至 0 | 退化，不能给出有效区间 |
| 主体造成的实际 HP 损失 | +0.50 | 2.72；−0.71 至 +11.81 | [−0.52, +1.51] |
| 主体承受的实际 HP 损失 | −12.49 | 19.17；−42.19 至 0 | [−19.65, −5.33] |
| 首次死亡时间 | +196 ms | 995 ms；−386 至 +5365 ms | [−176, +568] |

两组各 60 场中均为 44 次主体击杀、14 次同帧互杀、2 次主体先死；逐种子逐侧的胜负类别也相同。得分差全为零并不证明两者等价，且本实验没有批准的实质差异阈值。实际 HP 损失差受对局结束时机和其他命中变化影响，不能简单等同于 C08 的直接伤害。

从对决窗口的权威 `LD_HIT` 记录另行计数：C08 启用组的 60 个 Case 共发生 **280 次** `component_breath_strike` 命中，按种子两侧平均为 4.67 次/Case（标准差 0.51，范围 2.5–5.0，区间 [4.47, 4.86]）；这些命中造成的实际 HP 损失平均 27.36/Case（标准差 1.00，范围 22.87–27.79，区间 [26.99, 27.73]），对照组均为 0。全部双方 `LD_HIT` 事件按同样方法的差值为 +4.37 次/Case（标准差 0.85，范围 +1.5 至 +5.0，区间 [+4.05, +4.68]）。这些是记录中的命中数量，不是全部服务器事件、CPU、带宽或性能测量。重放失败使这些结果仅能描述本次数据，不能作为确定性因果或全场景强度结论。

## 4. 受控原生机制与高频组合

以下为真实 Combat 原生结算，均不是多场 AI 对局；没有把护盾吸收当作 HP 损失。

### 4.1 强化普攻、暴击、B04

L1、同一目标和投影、目标无盾；每项强制一次命中，强制暴击或不暴击，并非实际暴击频率。B04 按实际 HP 损失的 12% 治疗，例中治疗者有足够缺失生命。

| 普攻包 | 非暴击 HP 损失 / 治疗 | 强制暴击 HP 损失 / 治疗 |
|---|---:|---:|
| 普通主击 | 59.32 / 7.11 | 118.64 / 14.23 |
| 加 S11 | 113.98 / 13.67 | 227.96 / 27.35 |
| 加 S17 | 78.81 / 9.45 | 157.62 / 18.91 |
| S11＋S17 | 133.47 / 16.01 | 266.94 / 32.03 |

**代码事实＋原生受控：** S11/S17 合并进合法普攻主击，主击可以暴击，B04 按实际生命损失回血，因此强化与暴击会共同增加恢复基数。技能本体、反应或额外事件没有因此变成合法暴击。此单击不包含攻击间隔、护盾、溢出治疗、M12 还款或实战可达率；不能直接外推持续 DPS 或胜率。

### 4.2 单独记录的高频收益

| 组合 | 原生受控结果 | 限制 |
|---|---|---|
| B04→C08 | 一次实际掉血 59.32、治疗 7.11、储存 3.55；C08 每秒至多尝试一次发射，`60G` 是储存上限 | 目标须合法、可见且满足来源条件；不是每秒必造成 60 伤害 |
| B10＋B11→C09 | 三次受控主击各触发一次导电，目标损失依次 76.26、109.31、109.31；后两击消费上一击形成的强化 | 1 秒间隔的固定命中；不是自然命中率或全局事件上限 |
| B03＋B02＋C12→B02 | 对手三次受控普攻后，持有 C12 的目标实际损失 2610.16，对照 2567.79，差 42.37；账目每次反击分别新增 2 笔和 1 笔，第三次已触发兑现 | 夹具人为 `MaxHP=100000`，B03 按最大生命缩放；直接投影安装含稀缺 C12，三次输入也不是实际攻速预算，**不得当作自然构筑实战强度** |

这些例子展示事件如何产生收益，没有证明存在普适最强组合或无界循环。`CounterKeepsRootBudget` 只断言 C11 治疗供给的 C08 反击沿用 EffectId；它没有消费反击并断言 C11 的 30G 治疗预算。正常安装要求宿主已安装、已安装实例不得再次安装，卸载宿主会连带卸下挂件：不能凭一份 C11 同时接 S06 和 C08，直接 C08↔C11 闭环的正常可达性及启动路径也未证明。第一轮代码修订按用户要求保留，但“合法实战已发生并修复超额治疗”的说法撤回；多根治疗合并入同一 C08 储量时的归属仍待验证。

### 4.3 B07、C01、M05 和远程入口

- **B07 区域兼容：已证实 Bug，已修复。** 修复前 `ZoneControlAmp` 两次固定区域脉冲预期来源加成 1200 bp、实际为 0，红灯见 `Artifacts\20261112-222949-154-Automation`。现于释放时快照来源项、脉冲按命中时目标状态判断；绿灯 `Artifacts\20261112-223355-321-Automation` 中未受控目标实际损失 18.00、减速目标 20.16，符合已有 +12% 规则。未改变 B07 数值。
- **C01：未复现所疑问题。** `FireShieldWindows` 检查固定窗口边界、到期调度和卸载重装锚点；通过不证明所有延迟或多人时序均无缺陷。
- **M05：有真实取舍，不支持“当前必然上限无效”。** 原生投射物夹具中 S06 射程 750→950 cm；300 cm 的实际 HP 损失 76.27→61.01，850 cm 为 0→61.01。当前目录最长可接主动为 S06，未发现达到 1000 cm 后无收益的合法实例；未来上限附近应拒绝无实际增益的安装属于待批准行为，不能凭本例宣称已测该上限。
- **S04 / S06 / S21：** S04 是 600 cm 点选单位、0.25 s 前摇、65G＋0.65AP、1.6 s 沉默；释放时重新验证观察、距离及视线，不是方向弹道。S06 为 750 cm、半径 45 cm、1600 cm/s 的首个命中停止弹道，附移动减速；S21 为 650 cm、半径 40 cm、1300 cm/s，附攻速降低及新治疗/新盾降低。受控原生夹具验证 S06/S21 的横向移开可躲、前方身体可挡、对应状态可提交。**实际命中率、动态风筝、S04 实战命中率和反制频率均 NOT TESTED**，不能从静态范围直接宣称远程弱多少。

## 5. 等预算属性与防御

### 5.1 脑力与力道边际

**原生受控。** 每级 `3×L` 点，比较全脑力与将 1 点脑力换成 1 点力道；同级未分配属性目标、S06 强制命中、普攻强制不暴击、无安装构件。数值为单次实际 HP 损失：

| 等级 | S06 全脑力 / 换 1 力道 | 非暴击普攻全脑力 / 换 1 力道 |
|---|---:|---:|
| L3 | 143.11 / 137.86 | 62.62 / 64.67 |
| L6 | 213.04 / 208.04 | 67.18 / 69.14 |
| L10 | 296.69 / 291.98 | 72.64 / 74.48 |
| L16 | 377.83 / 373.51 | 79.72 / 81.41 |

每点力道还增加 40 bp 暴击概率；换掉一脑力减少 1 急速和 0.03 MP/s。此测试未模拟自然暴击、技能循环、耗蓝、弹道命中或持续 DPS，不能直接比较完整属性路线。AP5→AP8 的同源前后证据 **NOT TESTED**。

**解析，不是原生循环实测。** S06 基础冷却 8000 ms、前摇 200 ms、每次消耗 45 MP；急速使用 `LDTacticalRules.cpp::CooldownDurationMs` 的整数向上取整，冷却从释放后开始。假设上一发释放后冷却一到便启动下一次前摇、不受服务器帧量化/控制/走位影响，且不计脱战额外恢复、构件或外部回蓝：

| 等级 | 全脑力 / 换 1 力道的 S06 冷却 ms | 全脑力基础回蓝 MP/s | 全脑力按理想节奏连续释放的平均法力缺口 MP/s |
|---|---:|---:|---:|
| L3 | 7340 / 7408 | 1.63 | 4.338 |
| L6 | 6780 / 6838 | 2.14 | 4.307 |
| L10 | 6154 / 6202 | 2.82 | 4.262 |
| L16 | 5406 / 5443 | 3.84 | 4.187 |

缺口按 `45 / ((冷却毫秒 + 200) / 1000) − 回蓝率` 计算；真实法力不足会停止该理想释放节奏，有限蓝量、帧调度、命中、其他主动和回血均未模拟。这只说明指定条件下基础回蓝不足以长期支撑该理想频率，不能推断实际耗尽时间或法师整体强度。

### 5.2 体格与脸皮

**原生受控。** 每级等预算 `3×L` 全投体格或脸皮，从各自满血开始，承受 3 次各 300 原始伤害，第 2 次之前尝试治疗 100；分别无盾或先给固定 100 盾。每级对应两种抗性相同，所以此夹具下物理和魔法结果相同。下表是**最终 HP**，不是生存时间或相同起始生命对照：

| 等级 | 体格无盾 / 有盾 | 脸皮无盾 / 有盾 |
|---|---:|---:|
| L3 | 368.30 / 468.30 | 213.17 / 313.17 |
| L6 | 846.89 / 946.89 | 489.77 / 563.18 |
| L10 | 1480.26 / 1580.26 | 815.46 / 857.64 |
| L16 | 2421.90 / 2521.90 | 1254.18 / 1266.12 |

脸皮组有盾时的实际治疗在 L6/L10/L16 分别因满血截断为 73.41/42.18/11.94；未推进自然回生、持续攻击或真实对抗。不能据此宣称任一属性总体防御更强。

为避免将更高最终 HP 误认为减伤更高，下表列出同一原生测试的满生命、双抗和**无盾三次总 HP 损失**；脸皮在这些条件下减伤更多，而体格起始生命更多。体格每点还增加 0.08 HP/s 自然回生，但本例没有推进时间去结算该收益。

| 等级 | 体格：满生命 / 双抗 / 三次掉血 | 脸皮：满生命 / 双抗 / 三次掉血 |
|---|---:|---:|
| L3 | 1006 / 22 / 737.70 | 736 / 44.5 / 622.83 |
| L6 | 1450 / 28 / 703.11 | 910 / 73 / 520.23 |
| L10 | 2042 / 36 / 661.74 | 1142 / 111 / 426.54 |
| L16 | 2930 / 48 / 608.10 | 1490 / 168 / 335.82 |

## 6. 结构性问题与两套候选

**已观察的结构：** 一份可暴击的强化主击同时承担伤害和基于实际掉血的 B04 恢复；攻速增加合法主击频率，还能增加按主击触发的构件机会。技能则可能同时支付冷却、MP、前摇、命中和距离成本；B07 曾使部分区域入口损失本应获得的联动。上述是机制和受控现象；“这些成本导致全场景法师/远程弱势”仍是待检验假设。

以下为**候选方案，不是已批准内容；所有候选 A/B 均 NOT TESTED**。先用相同预算、策略、场地、种子与换边记录，再决定是否改正式规则；不要把建议的效果当成实测收益。

| 问题 | 候选 A | 候选 B | 区分实验 / 风险 |
|---|---|---|---|
| 普攻持续与恢复叠乘 | B04 按非暴击的主击反事实 HP 损失计算，保留强化包回血 | B04 仅以主击本体的实际 HP 损失份额回血，强化包仍可暴击造成伤害 | 测有/无盾、破盾、过量伤害、治疗溢出及 M12；须定义份额归属，不能重复消费生命或造假命中 |
| 法师循环 | 优先对照 M06 的冷却收益与 MP 代价，观察法力耗尽和选择机会 | 保留 AP 数值，以 B07 区域修复为基础对照元素、C06 和区域命中收益 | 固定构件获取预算，记录完整技能释放、耗蓝和实际命中；不把全局加 AP 当默认解 |
| 远程战术 | S04 保持稳定点选与视线反制，单独评估沉默控制价值 | 将 S06/S21 作为可躲弹道分别评估飞速、半径、射程和 S21 的反普攻价值 | 需真实移动、掩体及脱身策略；速度/范围调整会改变反制窗口，现无可批准新数字 |
| 构筑行为单一 | 比较 M01 安装于不同合法出口，记录站位与多目标命中而非只算伤害 | 比较 C02 撞墙、撞人的角度决策及失败成本 | 真实碰撞和 3+ 人目标选择尚缺；避免把派生事件伪装成普攻或原生施法 |

**可立即保留的具体改动：** B07 原应有的 +12% 在合法区域脉冲生效，原先有效加成为 0、修复后为 12%；这是恢复现有规则而非新调平参数。其余候选没有足够 A/B 证据支持批准具体新数值。第一轮脑力 AP/点 `5→8`、AP 上限 `250→400` 是当前工作区已有修改，不是第二轮提出或证明的新数值；应在同源原生前后对照和设计评审后决定最终采用与否。

## 7. 待用户决定的规则

以下均未自动实施或批准：

1. S12 是否采用真实身体阻挡；S13 的碰撞、推进和停止语义如何定义，不能默认穿人。
2. S10 原生首击与派生弹射分别是否响应 B02、B18、C12，逐项定义来源身份和次数。
3. 保留强化普攻可暴击的前提下，B04 吸血基数选现状、候选 A、候选 B 或其他方案；先取得 A/B 数据。
4. M05 在射程上限附近的实际增益为零时，是否拒绝安装；当前内容尚无已发现的此类合法实例。
5. S14 是否不解除 S21 的攻速降低，以及状态分类、卡面和现有代码如何一致。
6. 多个不同根动作的治疗在 C08 一个发射窗口中合并后，派生预算如何归属；需先确认正常可达的具体联动。

**当前代码事实：** S10 的主动链跳复制原主动命中载荷；感电状态的放电另建 `bStatusSettle` 命中，不能只因都叫“电弹射”就视为同一种事件。S14 的 `DispelSlow` 清理移动减速列表，S21 的攻速降低存于独立列表，现代码不会因此清除它；专门的 S21→S14 对局与卡面可读性仍未验证。

下一轮最值得验证：① 修复严格重放并对多种子复验；② 同一引擎和源状态下实现 B04 基数 A/B；③ 可观察信息合规的远程风筝、躲弹、追击/脱身；④ 真实墙体碰撞、多人 FFA 与区域占领；⑤ 合法构筑搜索、事件负载和真人试玩。具体机制扩展可优先探索墙角方向、技能区域与元素接力、带明确代价的防守反击；完整不少于十项的独立提案见第一轮 Report G。

## 8. 修改、测试与复现

### 8.1 本工作区的实际变更

| 范围 | 主要文件及行为 |
|---|---|
| DEV 固定供给 | `LDDevGrowthFixture.*`、`LDDevelopmentDirector.*`、`LDGrowthDirector.*`；仅诊断入口预置主动和逐级构件，复用奖励及安装事务 |
| 原生评估 | `LDBuildEvaluationProbe.*`、`LDBuildEvaluationTests.cpp`、`LDGameModeRest.cpp`、`BuildEvaluationLockdown.py`；记录出生和校验预设、同时死亡与身份 |
| 多批配对 | 新增 `Scripts\CombineBuildEvaluation.py`、`TestCombineBuildEvaluation.py`，以及两份诊断清单；拒绝身份、案例、种子或换边不一致 |
| B07 | `LDCombatMechanicsDirector.cpp`、`LDCombatDirector.h`；区域释放快照已批准来源项，后续命中走原有增伤结算 |
| 原生回归 | `LDBuildPrimitivesTests.cpp`、`LDGrowthRulesTests.cpp`、`Scripts\CheckRuleReport.py`；记录受控指标并扩充窗口、供给和结算断言 |
| 第一轮保留 | `LDCombatEffects.cpp`、`LDCombatWorkshop.cpp`、`LDCombatRest.cpp`、`LDCombatDirector.h` 的 C08 根身份变更；`SourceData\Demo\content.json`、生成载荷/资产/contract 和属性测试的 AP 改动；**见前述证据限制** |
| 文档 | `docs\systems\BUILD_EVALUATION.md`、`COMBAT_AND_GROWTH.md`、第一轮和本报告 |

新增规则测试全名：`Lockdown.Rules.Combat.Events.ZoneControlAmp`、`.EmpoweredCritLifesteal`、`.AttributeMarginal`、`.DefenseBudget`、`.CounterHealStore`、`.CounterExtraBase`、`.ElementChainFrequency`、`.ProjectileCounterplay`、`.RangeModTradeoff`；第一轮另有 `.CounterKeepsRootBudget`。缩写的各项均承接同一前缀。既有 `FireShieldWindows` 扩充断言。

### 8.2 已运行与未运行

- 最新增量 `BuildEditor.ps1 -MaxParallelActions 4` **通过**：`Artifacts\20261112-223849-838-BuildEditor`。
- 最新原生受影响前缀 87/87 **通过**：`Artifacts\20261112-230110-686-Automation`；这是 377 项中的部分运行，不是全量门禁。
- `python -B -m unittest discover -s Scripts -p "Test*BuildEvaluation*.py"`：100 项通过；拒绝覆盖测试输出的 `COMBINATION REFUSED ... File exists` 为预期拒绝。
- `python -B .\Scripts\SyncDemoContentData.py --check`：生成一致性通过。第一轮 368/368 完整规则门禁是**第二轮改动前**的历史证据，不代表当前全量通过。
- `git -c core.whitespace=cr-at-eol diff --check`：通过；未设置该选项时，Git 把受 `-text` 属性保护、索引和工作区均为 CRLF 的 `SourceData\Demo\content.json` 两个修改行的回车报为空白。原始字节参与内容哈希，本轮未改换行。
- 严格重放 **失败**，见 §2.3；不能把本轮评估基础设施声称为已完成。
- 本轮当前完整规则门禁、真实拥有连接、真实墙体、FFA、区域争夺、玩家测试与服务器性能对照均 **NOT TESTED**。

### 8.3 复现命令

下列命令在仓库根执行，`LD_UE_ROOT` 由本机环境提供。诊断清单的两次大型对照实际冻结清单位于 `Artifacts\phase2-paired-l10\a.json`、`b.json` 和 `Artifacts\phase2-b04-c08-paired-a.json`、`phase2-b04-c08-paired-b.json`；这些输出为本机忽略目录。初始模板在 `Scripts\BuildEvaluationManifests`，更改根种子或样本数须生成新的清单，不覆盖已有证据。

```powershell
.\Scripts\BuildEditor.ps1 -MaxParallelActions 4
python -B .\Scripts\SyncDemoContentData.py --check
python -B -m unittest discover -s Scripts -p "Test*BuildEvaluation*.py"
.\Scripts\RunTests.ps1 -Filter Lockdown.Rules.Combat,Lockdown.Rules.BuildEvaluation,Lockdown.Rules.DemoAttributeProjection,Lockdown.Rules.DemoBuildPrimitives,Lockdown.Rules.DemoCombatEvaluation,Lockdown.Rules.DemoCombatPrimitives,Lockdown.Rules.DemoElements,Lockdown.Rules.DemoGrowth,Lockdown.Rules.DemoMechanics,Lockdown.Rules.DemoDevTestCommands -ImportTests Skip
.\Scripts\RunBuildEvaluation.ps1 -Manifest .\Scripts\BuildEvaluationManifests\phase2-b04-c08-smoke.json
.\Scripts\RunBuildEvaluation.ps1 -ReplayRun .\Artifacts\BuildEvaluation\20261112-224212-phase2-b04-c08-paired-a -Case c000a-b04_c08
python -B .\Scripts\CombineBuildEvaluation.py --run .\Artifacts\BuildEvaluation\20261112-224212-phase2-b04-c08-paired-a --run .\Artifacts\BuildEvaluation\20261112-225140-phase2-b04-c08-paired-b --output .\Artifacts\phase2-b04-c08-recombined.json
```

合并命令的输出必须不存在；重放要求源运行与当前模块身份一致，且当前预期失败。上述具体 `Artifacts` 路径是本机忽略的证据，不随 Git 自动分发。重新运行 30 组时，将同一原始清单复制到两个新的 `Artifacts` 文件，分别设置不同 `blocks.root_seed`、`splits.confirm=15`、`sides=2`、`budget.max_cases=60`，其余设计和预算一致；对每份调用 `RunBuildEvaluation.ps1 -Manifest <清单>` 后合并。须检查具体 Case 状态、实际换边、固定帧、预算和版本身份；若被拒绝、重放不一致或样本不足，保留原始失败证据，不把它当成功对照。

§3.2 的 C08 轨迹统计可由合并映射复算；以下代码只计对决起点至结局时间内、主体命中对手的该构件事件，以十进制累加日志中精确到 0.01 HP 的值，再调用 Harness 的浮点近似区间函数：

```powershell
@'
import json, sys
from decimal import Decimal
from pathlib import Path
sys.path.insert(0, "Scripts")
from BuildEvaluation import summarize
combined = json.loads(Path(r"Artifacts\phase2-b04-c08-paired-30-verified.json").read_text(encoding="utf-8-sig"))
counts, damage = [], []
for seed in combined["seed_mapping"].values():
    run, sides = Path(seed["run"]), []
    for side in ("0", "1"):
        case = seed["cases"]["b04_c08:" + side]
        result = json.loads((run / "native" / "results" / (case + ".json")).read_text(encoding="utf-8-sig"))
        ids = {row["role"]: row["contender"] for row in result["contenders"]}
        start, end = result["started_at_ms"], result["started_at_ms"] + result["outcome"]["t_ms"]
        hits = []
        for line in (run / "native" / "traces" / (case + ".log")).read_text(encoding="utf-8-sig").splitlines():
            if line.startswith("LD_HIT "):
                event = dict(field.split("=", 1) for field in line.split()[1:] if "=" in field)
                if (start <= int(event["t"]) <= end and event.get("src") == ids["subject"]
                    and event.get("victim") == ids["opponent"] and event.get("via") == "component:component_breath_strike"):
                    hits.append(Decimal(event["hp_lost"]))
        sides.append((len(hits), sum(hits, Decimal(0))))
    counts.append(sum(row[0] for row in sides) / 2)
    damage.append(sum((row[1] for row in sides), Decimal(0)) / 2)
print("total hits", sum(counts) * 2, "hits per Case", summarize(counts))
print("C08 HP loss per Case", summarize(damage))
'@ | python -B -
```

# 战斗与成长实现手册

本页记录权威战斗结算、属性投影、冷却轨道、元素反应与地面状态、构筑牌/关键石触发链、野怪攻击几何及局末构筑蓝图台账（`LDCombatDirector`、`LDAttributeProjection`、`LDTacticalRules`、`LDCombatActions`、`LDCombatEffects`、`LDCombatCards`、`LDCombatElements*`、`LDCombatMonsters`、`LDBuildBlueprint*`）。完整参数与目录见 [内容与数值](../demo/CONTENT_AND_BALANCE.md)。

## 身份与接口

- **单一战斗状态所有者（`ALDCombatDirector`）**：拥有本局全部参赛者身体、野怪成员、Boss、训练假人与召唤物（如 `active_bag_wall` 书包实体）的实时生命/劲/护盾层（`ShieldLayers`）、冷却轨道（`FLDCombatTrack`）、控制与减益状态、飞行弹道、周期区域、地面状态、承伤回放窗口（`DamageRecap`）与助攻账本（`LDAssist`）。
- **单一属性投影（`LDAttributeProjection`）**：从角色基值 + 等级成长 + 五维点数（体格/脸皮/力道/脑力/身手）+ 已装构筑牌静态项（含 `Y13` 护甲转生命、`CardGrowth` 叠加层、`AttackSpeedBonusRatio`）+ 限时增益/低血条件项一次算出 `Final` 属性，并钳制在目录上下限内（移速 `[150, 525]`、攻速 `[0.35, 2.5]`、暴击率 `[0, 1]`、抗性 `>= 0`）。
- **冷却轨道解耦**：角色主动按 `(Match, Contender, AbilityId)` 建唯一轨道，保存绝对 `ReadyAtMs`；换槽、升级分支、死亡或遗忘再学均沿用原 `ReadyAtMs`，冷却急速仅在下次成功释放时计算 `ceil(1000 * BaseCD * 100 / (100 + H))`。通用冲刺 `character.dash` 独立持有单条 `Innate` 轨道。

## 从意图到结果

1. **前闸与解保护**：校验拥有者、当前 `Generation/Life`、阶段写闸与请求去重；合法进攻意图（`AttackUnit`、`AttackMove` 及任意 `Q/W/E` 主动尝试，即使随后因缺劲或冷却被拒）立即解除本人复活保护。
2. **起手与释放（`LDCombatActions`）**：
   - **三段普攻连招**：`AttackStart` 通过射程与控制检查即提交攻击节拍，前摇 `ceil(180ms * 0.85 / AS)` 内若被打断退还节拍；到达释放时刻复查可见性与无高墙阻挡（不因目标拉远取消已挥出的一击），第 3 段伤害 `×1.25`，连招超时 `2.5 s` 归回第 1 段。
   - **动作提示**：导航普攻出手后取消收招时清除收招的起手提示，保留出手时间供本人音效去重；不会将普攻投影成施法或重置已提交的普攻节拍。
   - **主动技能与三级分支**：26 个主动技能按 `SKILL.GrowthPerLevel`（每角色等级 `+4.5%`）放大固定基础值，并叠加当前技能等级（1–3 级）及已选 2 级 A/B、3 级 A/B 分支补丁；仅在成功 `Release` 瞬间扣劲、写 `ReadyAtMs` 并快照源属性（包括双系数 `ADRatioBp + APRatioBp` 与自适应系数 `AdaptiveRatioBp`：`BonusAD * 1.4 >= AP` 取额外 AD 并转物理伤害，否则取 AP 走魔法伤害）。
   - **爆劲（Overdrive）**：劲满 `60` 且蓄力满 `0.4 s` 释放时额外消耗 `60` 劲，按技能有效元素强化本次释放——优先读取技能原生/分支元素，原生无元素时读取该技能槽镶嵌的 `SkillElement` 元素勋章（仍无元素时仅享受基础数值放大 `×1.35`；火 `+3` 层灼烧、水减速升至 `45%/2s`、电直伤追加 `0.5R`、冻附 `0.6s` 定身、馊持续 `×2`）。
3. **伤害、乘区、护盾与吸血（`LDCombatEffects` / `LDTacticalRules`）**：
   - **结算顺序**：基础与系数和 → 暴击（普攻 `×2`，技能仅装 `Y09` 时可暴击）→ 乘区连乘 `DamageFactorBp`（同名牌多份相加为一乘区，不同来源相乘，技术上限 `×1000`）→ 双抗减伤 `100 / (100 + max(0, Resist - Breach*3))` → 护盾层吸收（按最早到期层优先扣减，每打空一层触发一次破盾事件如 `Y06`）→ 实扣生命 `HPLostP2`。
   - **吸血与反伤**：普攻吸血（`Y17`）与技能吸血（`K15`、`Y10`）均按实际扣血 `HPLostP2` 结算且无上限；`Y01` 反伤按直接受击总量（含打在护盾上的份额）反弹 `25%` 真实/对应伤害，反伤标记 `bReflect` 不暴击、不吸血、不再触发反伤。
   - **治疗、重伤与溢出转盾**：所有治疗入口统一经 `HealBody`（受“馊”减疗削减，触发体格 18 点关键石周期护盾与 `Y14` 满血溢出转 `5 s` 护盾层；`Y14` 同源未到期护盾余额严格封顶于每份 `30%` 最大生命，溢出时刷新到期时间并仅补足差额，不影响其他来源护盾层独立叠加）。
4. **五元素反应、地面状态与连锁护栏**：
   - **元素附着与 10 种双元素反应（`LDCombatElementsDirector`）**：技能原生元素、宿主元素勋章（技能槽每次直接命中生效；本体槽从普攻第 2 段起生效）及冰箱词条命中目标时，若目标无元素则挂单元素（火叠独立 3 s 灼烧最多 5 层；水湿 5 s；电麻每第 3 次受击触发 `0.5R`；冻叠减速最多 3 层；馊减疗减盾 `40%` 4 s），若已有异种元素则立即消耗并触发 10 种反应之一（基准值 `R = 25 + 8*L + 0.35*AP`）及地面状态转换。
   - **地面状态上限与自免**：水洼、冰面（加减速惯性 `×0.5`、冲刺/击退距离 `×1.5`）、火场、毒雾、电场仅对非主人敌人生效；每人最多 6 片、全场最多 40 片，超出淘汰最早片。
   - **连锁与免费重放护栏**：同一次施法链有深度与单步上限保护；`Y05`（余波再起）每条连锁提供一次同目标反应冷却豁免，豁免用尽后仅对未反应目标挂次生元素；`K06`（再来一下）与 `A08` 的免费自动重放（`bAutoRecast`）保留 `K09` 冷却惩罚但屏蔽免费回劲、减 CD 与脑力关键石刷钱循环。

## 权威时序

同一权威模拟步内严格按确定性顺序推进，不依赖 Actor Tick 偶然先后：
1. 积分上一步至当前时刻的自然生命/劲回复（保留亚 P2 余数）与脱战/躺平角回复；
2. 推进到期前摇释放、飞行弹道碰撞、周期区域脉冲、灼烧/地面状态跳伤与野怪延迟段；
3. 结算控制递减（2 s 窗口内硬控 `100% -> 60% -> 30%`，下限 `0.2 s`）、护盾层到期与击退/冲刺根运动完成；
4. 原子冻结本步全部 `HP -> 0` 致死事实，提交 `CommitDeathReward` 并清理死者活体持续状态（已释放的独立弹道/区域/护盾按原期限继续；正式退房者立即取消全部未完效果）；
5. 随后才由 `GameMode` 基于最新存活与受控状态推进旗杆喷漆/守护、野外目标与终局胜负裁决。

## 野怪攻击模式

四档 12 种野怪除小野 HR 外，每第 3 次成功释放以锁定几何预警技能替代普攻（扇形 `ConeWarning`、直线 `LineWarning`、圆形落点或短冲，由 `LDCombatMonsters.cpp` 与 `LDMonsterRules` 共用预警和命中判定）；回位开始即取消未释放预警，沿路径回锚点（或连续 3 次无路 / 6 s 停滞触发原地 `LD_CAMP_RETURN_DEAD_END` 重置）后满当前成长阶生命并清仇恨，已死同伴不复活、不重复发奖。

## 局末构筑蓝图

对局正常锁定 `MatchResult` 时，每位真人拥有者收到本局完整 `FLDBuildBlueprint`（`schema=lockdown.build-blueprint`、`schema_version=5`），包含开局至终局每次构筑结构变更的版本时间段、Q/W/E 分支、五维分配、已装牌/勋章及宿主绑定参数、终局改装与归属伤害统计，并由 `LDBuildBlueprintStore` 原子写入 `Saved/Blueprints/match-<ResultId>.ldbuild.json`（自动保留最近 50 份且总计 `<= 100 MiB`）。

## 检查入口

- 纯规则：`Lockdown.Rules.DemoCombat*`、`Lockdown.Rules.DemoAttributeProjection.*`、`Lockdown.Rules.DemoMechanics.*`、`Lockdown.Rules.DemoBuildPrimitives.*`、`Lockdown.Rules.Combat.*`、`Lockdown.Rules.DamageRecap.*`、`Lockdown.Rules.BuildBlueprint.*`。
- 拥有连接场景：`Scripts/RunDemoCombatCheck.ps1`、`Scripts/RunDemoCampCheck.ps1`。

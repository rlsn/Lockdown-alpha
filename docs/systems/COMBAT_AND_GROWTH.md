# 战斗与成长实现手册

本页记录权威战斗结算、属性投影、冷却轨道、元素反应与地面状态、构筑牌/关键石触发链、野怪攻击几何及局末构筑蓝图台账（`LDCombatDirector`、`LDAttributeProjection`、`LDTacticalRules`、`LDCombatActions`、`LDCombatEffects`、`LDCombatCards`、`LDCombatElements*`、`LDCombatMonsters`、`LDBuildBlueprint*`）。完整参数与目录见 [内容与数值](../demo/CONTENT_AND_BALANCE.md)。

## 身份与接口

- **单一战斗状态所有者（`ALDCombatDirector`）**：拥有本局全部参赛者身体、野怪成员、Boss、训练假人与召唤物（如 `active_bag_wall` 书包实体）的实时生命/劲/护盾层（`ShieldLayers`）、冷却轨道（`FLDCombatTrack`）、控制与减益状态、飞行弹道、周期区域、地面状态、承伤回放窗口（`DamageRecap`）与助攻账本（`LDAssist`）。
- **单一属性投影（`LDAttributeProjection`）**：从角色基值 + 等级成长 + 五维点数（体格/脸皮/力道/脑力/身手）+ 已装构筑牌静态项（含 `Y13` 护甲转生命、`CardGrowth` 叠加层、`AttackSpeedBonusRatio`）+ 限时增益/低血条件项一次算出 `Final` 属性，并钳制在目录上下限内（移速 `[150, 525]`、攻速 `[0.35, 2.5]`、暴击率 `[0, 1]`、抗性 `>= 0`）。
- **成长牌读数（`FLDCombatOwnerView.CardStacks`，仅本人）**：`OwnerCardStacks` 按与结算相同的口径给出每张已装成长牌副本（`Serial`、`CardId`）的当前整数 `Count`——K16 就绪闲置整秒、Y22 未造成伤害整秒（均只算交战中及开打前带入）、K17 本局命中参赛者次数、K18 窗口内其他技能释放数、Y23 站在自己地面状态整秒、Y24 未兑现笔数；牌的单步加成乘 `Count` 即当前加成，计数为 0 不发行，每行最多每秒变化一次。A14 目标身上的破防层数走可见单位的 `Exposed` 状态层数（按视野下发）。
- **冷却轨道解耦**：角色主动按 `(Match, Contender, AbilityId)` 建唯一轨道，保存绝对 `ReadyAtMs`；换槽、升级分支、死亡或遗忘再学均沿用原 `ReadyAtMs`，冷却急速仅在下次成功释放时计算 `ceil(1000 * BaseCD * 100 / (100 + H))`。通用冲刺 `character.dash` 独立持有单条 `Innate` 轨道。

## 从意图到结果

1. **前闸与解保护**：校验拥有者、当前 `Generation/Life`、阶段写闸与请求去重；合法进攻意图（`AttackUnit`、`AttackMove` 及任意 `Q/W/E` 主动尝试，即使随后因缺劲或冷却被拒）立即解除本人复活保护。
2. **起手与释放（`LDCombatActions`）**：
   - **三段普攻连招**：`AttackStart` 通过射程与控制检查即提交攻击节拍，前摇 `ceil(180ms * 0.85 / AS)` 内若被打断退还节拍；到达释放时刻复查可见性与无高墙阻挡（不因目标拉远取消已挥出的一击），三段伤害依次 `×0.9 / ×0.9 / ×1.3`（`COMBO.StepRatios`），连招超时 `2.5 s` 归回第 1 段。
   - **动作提示**：导航普攻出手后取消收招时清除收招的起手提示，保留出手时间供本人音效去重；不会将普攻投影成施法或重置已提交的普攻节拍。
   - **主动技能与三级分支**：24 个主动技能按 `SKILL.GrowthPerLevel`（每角色等级 `+4%`）放大固定基础值，并叠加当前技能等级（1–3 级）及已选 2 级 A/B、3 级 A/B 分支补丁；仅在成功 `Release` 瞬间扣劲、写 `ReadyAtMs` 并快照源属性（包括双系数 `ADRatioBp + APRatioBp`、双抗系数 `ResistRatioBp` 与自适应系数 `AdaptiveRatioBp`：`BonusAD * 1.4 >= AP` 取额外 AD 并转物理伤害，否则取 AP 走魔法伤害）。
   - **落点预警从前摇开始（C-12，`FLDCombatLandingPreview`）**：目标形式为地面点且当前形态含 `ground_impact` 的技能，准入时按释放时同一规则（`LDGroundLanding::Resolve`）锁定落点；整个前摇期间以地面效果将沿用的观察键发布 `BookShadow` 预警（剩余时间 = 前摇剩余 + 落地延迟），释放后砸在这个落点上；前摇被打断即不再发布。K06/A08 的重放从各自释放处重新计算落点。
- **承诺分支被控制打断的部分冷却（C-11，`interrupt_cooldown`）**：带 `interrupt_cooldown` 的分支形态（S03 2A、S13 2B、S02 2B 及其 3 阶，`0.5`）在释放时刻之前被控制打断（`ControlBlocked`）时，技能进入按同一急速公式与下限计算的该比例冷却；多充能技能只花掉一格的这一部分。主动取消、分支二段与重放不受影响，不扣劲也不返劲，打断者不获任何收益。
3. **伤害、乘区、护盾与吸血（`LDCombatEffects` / `LDTacticalRules`）**：
   - **结算顺序**：基础与系数和 → 暴击（普攻 `×2`，技能仅装 `Y09` 或耗盾暴击牌时可暴击）→ 乘区连乘 `DamageFactorBp`（同名牌多份相加为一乘区，不同来源相乘，技术上限 `×1000`）→ 双抗减伤 `100 / (100 + max(0, Resist - Breach*3))` → 脸皮关键石平摊减免 → 护盾层吸收（按最早到期层优先扣减，每打空一层触发一次破盾事件如 `Y06`）→ 实扣生命 `HPLostP2`（激活脸皮 18 点关键石时，单次实扣超出 `35%` 最大生命的份额转入 `StaggerBleeds`，在 `2.0 s` 内每 `0.5 s` 延缓结算）。
   - **吸血与反伤**：普攻吸血（`Y17`）与技能吸血（`K15`、`Y10`）均按实际扣血 `HPLostP2` 结算且无上限；`Y01` 反伤按直接受击总量（含打在护盾上的份额）反弹 `25%` 真实/对应伤害，反伤标记 `bReflect` 不暴击、不吸血、不再触发反伤。
   - **治疗、重伤与溢出转盾**：所有治疗入口统一经 `HealBody`（受“馊”减疗削减，触发体格 18 点关键石周期护盾与 `Y14` 满血溢出转 `5 s` 护盾层；`Y14` 同源未到期护盾余额严格封顶于每份 `30%` 最大生命，溢出时刷新到期时间并仅补足差额，不影响其他来源护盾层独立叠加）。
4. **五元素反应、地面状态与连锁护栏**：
   - **元素附着、三阶勋章融合与 10 种双元素反应（`LDCombatElementsDirector`）**：技能原生元素、宿主元素勋章（技能槽每次直接命中生效；本体槽从普攻第 2 段起生效；同色勋章融合 `Ⅱ 阶` 使该宿主触发的反应伤害 `+35%`，`Ⅲ 阶` 命中挂主元素后额外附着确定性异种副元素自触发反应）及冰箱词条命中目标时，若目标无元素则挂单元素，若已有异种元素则立即消耗并触发 10 种反应之一（基准值 `R = 30 + 4*(L - 1) + 0.25*AP`）及地面状态转换。
   - **地面状态上限与自免**：水洼、冰面（加减速惯性 `×0.5`、冲刺/击退距离 `×1.5`）、火场、毒雾、电场仅对非主人敌人生效；每人最多 6 片、全场最多 40 片，超出淘汰最早片。
   - **连锁与免费重放护栏**：同一次施法链有深度与单步上限保护；`Y05`（余波再起）每条连锁提供一次同目标反应冷却豁免，豁免用尽后仅对未反应目标挂次生元素；`K06`（再来一下）与 `A08` 的免费自动重放（`bAutoRecast`）保留 `K09` 冷却惩罚但屏蔽免费回劲、减 CD 与脑力关键石刷钱循环；`A09` 由攻速换来的追加打击（`bAttackSpeedStrike`）不给 `A12` 叠层、不计 `A13` 进度，切断攻速自我放大。
   - **服务器命中预算（技术保护，非玩法上限）**：`ALDCombatDirector::TechHitsPerStep`（4096）限制一个权威步结算的命中数，其余顺延到下一步；排队命中超过 `TechPendingHitsCap`（16384）时丢弃最新的部分，`A09` 一次出手的追加打击也不超过该上限。正常对局每步只有几十次命中，触发即异常，分别记 `LD_CHAIN_GUARD reason=Step`（Warning）与 `reason=Overflow`（Error）。

## 权威时序

同一权威模拟步内严格按确定性顺序推进，不依赖 Actor Tick 偶然先后：
1. 积分上一步至当前时刻的自然生命/劲回复（保留亚 P2 余数）与脱战/躺平角回复；
2. 推进到期前摇释放、飞行弹道碰撞、周期区域脉冲、灼烧/地面状态跳伤与野怪延迟段；
3. 结算控制递减（2 s 窗口内硬控 `100% -> 60% -> 30%`，下限 `0.2 s`）、护盾层到期与击退/冲刺根运动完成；
4. 原子冻结本步全部 `HP -> 0` 致死事实，提交 `CommitDeathReward` 并清理死者活体持续状态（已释放的独立弹道/区域/护盾按原期限继续；正式退房者立即取消全部未完效果）；
5. 随后才由 `GameMode` 基于最新存活与受控状态推进旗杆喷漆/守护、野外目标与终局胜负裁决。

## 权威身体与每步开销

- **每步的移动**：每个注册身体每步走一次原生移动（`ULDCharacterMovementComponent::AdvanceAuthorityMovement`），并留下本步的移动记录，冲刺接触判定要读每个身体的这份记录（包括站着不动的）。
- **静止的营地成员**：没有输入、站在静态地面上的营地成员走 `TryAdvanceStationaryMovement`，不跑原生物理，只查一次“有没有东西挤进来”（`LDStationaryPenetration`）。这次查询的耗时主要在查询本身的固定开销，实测让它跳过脚下的静态地面并不更快。
- **营地成员身体**（`ALDCampMemberCharacter`）不带表现组件、镜头臂和镜头，也不运行 Actor Tick：它只通过观察代理显示，由营地在权威步内驱动。`ALDCharacter` 的这三个子对象是可选的，参赛者身体总是齐全。
- **空闲的营地成员**在 Actions 阶段不读地面、视野和战斗状态，直接原地保持；只有已消费的敌对接触能让它进入交战（`LDCampDecision::Evaluate` 对 Idle 恒答 Hold）。
- **内容查找**：`FLDDemoContentSnapshot` 对每张定义表建有按 id 的索引，技能、卡牌和怪物另有按名字（`FindAbilityByName`、`FindCardByName`、`FindMonsterByName`）、营地另有按锚点（`FindCampByAnchor`）的索引；界面取显示行走 `FLDDemoContentDefinitions::FindDisplay`。查不到时仍走完整路径给出原因。
- **二手贩子落点被占**：偏移点和营地锚点都站不下时不算故障，`AdvanceFieldPacing` 每隔 1 s 再找一次，直到它的出现窗口结束。
- **追踪区段**：`LDPhase_*`（阶段）、`LDStep_NextDue`、`LDStep_SortBodies`、`LDActions_*`、`LDEffects_*`、`LDVision*`、`LDAI_Frame*`；多段表达式里的一段用 `LD_TRACED`（`Core/LDTrace.h`）。按身体触发的代码不加区段，一局会多出上千万个事件。整局追踪见[开发指南](../DEVELOPMENT.md#试玩问题排查)的 `RunDemoOpponentCheck.ps1 -Trace`。

## 野怪攻击模式

四档 12 种野怪除小野 HR 外，每第 3 次成功释放以锁定几何预警技能替代普攻（扇形 `ConeWarning`、直线 `LineWarning`、圆形落点或短冲，由 `LDCombatMonsters.cpp` 与 `LDMonsterRules` 共用预警和命中判定）；回位开始即取消未释放预警，沿路径回锚点（或连续 3 次无路 / 6 s 停滞触发原地 `LD_CAMP_RETURN_DEAD_END` 重置）后满当前成长阶生命并清仇恨，已死同伴不复活、不重复发奖。

## 局末构筑蓝图

对局正常锁定 `MatchResult` 时，每位真人拥有者收到本局完整 `FLDBuildBlueprint`（`schema=lockdown.build-blueprint`、`schema_version=5`），包含开局至终局每次构筑结构变更的版本时间段、Q/W/E 分支、五维分配、已装牌/勋章及宿主绑定参数、终局改装与归属伤害统计，并由 `LDBuildBlueprintStore` 原子写入 `Saved/Blueprints/match-<ResultId>.ldbuild.json`（自动保留最近 50 份且总计 `<= 100 MiB`）。

## 隔离原型

服务器命令行开关（`LDProtoFlags`，每个进程只读一次）。开关不写入 `SourceData`，也不参与内容哈希；不带开关时，所有规则与正式版本一致。启用时写一行 `LD_PROTO flags=… params=…`；参数无效时写 `LD_PROTO_INVALID`，并且不启用任何原型。

- **急眼了（C-10，`-LDProtoAgency`）**：真人或 AI 选手每受到他人的控制，就按权重记录失去操作权的时段。权重：眩晕与击退 1.0，嘲讽 0.8，定身 0.5，沉默与缴械 0.2，减速 0；同一时刻多个控制重叠时，只按其中最大的权重计算一次。
  - 触发：滑动窗口 `-LDProtoAgencyWindow=`（默认 4 s）内的累计值达到 `-LDProtoAgencyThreshold=`（默认 2 s）时触发，触发时刻精确到毫秒（`AgencyDueMs`）。两次触发至少间隔 `-LDProtoAgencyCooldown=`（默认 6 s）。
  - 守护期：触发后进入 `-LDProtoAgencyGuard=`（默认 1 s）守护期，期间新的硬控不生效。
  - 两种变体：`-LDProtoAgencyCut=Immediate`（V-Cut，默认）立即结束他人施加的眩晕、定身、缴械、沉默与嘲讽，击退的位移照常完成，但不再锁定输入；`NextControl`（V-Next）不结束当前控制，只防住下一次。
  - 翻滚：同一连击链（距上一次击退结束不到 2 s）中，第 `-LDProtoAgencyRollFrom=`（默认 3）次及以后的击退，以及守护期内受到的击退，都照常推开身体，但不锁定动作、不打断前摇与移动指令，也不计入失去操作权的时长。
  - 日志：`LD_PROTO_AGENCY trigger|guarded|roll`。守护期内所有看得见该选手的人（包括本人）在其头顶看到开发用文字“急眼了”（[视野与 UI](VISION_AND_UI.md)）。翻滚击退推开期间，位移仍由根运动接管。
- **一锅端落地延迟（C-14，`-LDProtoPotDelay=<秒>`）**：只把 S08（`active_quick_meeting`）2B、3B1、3B2 的 `ground_impact` 延迟改为该值（0–5 s），落点预警也随之延长；3B1 的二次爆炸仍在首砸后按内容延迟发生。

## 检查入口

- 纯规则：`Lockdown.Rules.DemoCombat*`、`Lockdown.Rules.DemoAttributeProjection.*`、`Lockdown.Rules.DemoMechanics.*`、`Lockdown.Rules.DemoBuildPrimitives.*`、`Lockdown.Rules.Combat.*`、`Lockdown.Rules.DamageRecap.*`、`Lockdown.Rules.BuildBlueprint.*`。
- 拥有连接场景：`Scripts/RunDemoCombatCheck.ps1`、`Scripts/RunDemoCampCheck.ps1`。

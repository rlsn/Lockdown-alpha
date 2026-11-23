# Demo 内容与数值

本页是 `SourceData\Demo\content.json`（经 `Scripts\SyncDemoContentData.py` 生成运行载荷与原生契约）的人读参考表。产品规则见 [App Spec](../APP_SPEC.md)，验收覆盖见[实施就绪评估](READINESS.md)。当前数值均为试玩输入，未经平衡测试。

## 按任务查内容

| 任务 | 内容章节 |
| --- | --- |
| 基值、单位、公式、空间和节奏 | [§3 单位、参数与公式](#3-单位参数与公式) |
| 五维属性、关键石、主动与分支 | [§4.1 属性](#41-五维属性与成长参数)、[§4.2 主动](#42-角色主动)、[§4.3 学习](#43-主动机会学习与加点) |
| 构筑牌、元素勋章、反应、状态、事件与终局 | [§4.4 构筑牌](#44-构筑牌与参数)、[§4.5 共用规则](#45-关键词状态与共用战斗规则)、[§4.6 全员规则](#46-全员规则与地图事件)、[§4.7 终局](#47-终局改装) |
| XP、营地、野怪与 AI 参数 | [§6 经验、营地和发育供给](#6-经验营地和发育供给) |
| 帧率预算与目录契约 | [§8.1 性能预算](#81-性能预算)、[§10 数据来源与提交契约](#10-数据来源与提交契约) |

当前运行目录包含：**1 个基础角色、5 项五维属性（含 18 点关键石）、24 个三级分支角色主动、89 张构筑牌 + 5 枚三阶融合元素勋章（共 94 项 `cards` 条目）、8 项终局改装、四档 12 种野怪与 1 个 Boss**。所有世界距离按脚点中心 XY 厘米判定，不重复加减胶囊碰撞半径。

## 3. 单位、参数与公式

### 3.1 统一表示与失败语义

| 类型简称 | 作者表示/单位 | 原生建议与取整边界 |
| --- | --- | --- |
| P2 | 至多两位小数的资源/属性点 | 精确解析为 int64 百分之一点；当前 HP 与劲只在原有战斗状态中有一份真值 |
| R4 | 至多四位小数的无量纲比例/倍率 | 精确 basis points；概率与倍率是不同字段，倍率允许大于 1 |
| Rate | 次/秒、点/秒、cm/s，字段逐项注明 | 保留精确有理/足够精度中间结果；周期和资源提交最后才取整 |
| Sec | 非负、至多三位小数的秒 | 作者秒数必须可精确转换为整毫秒；不悄悄截去小数。派生 CD/间隔向上整毫秒 |
| Count | 等级、数量、XP 的整数 | bool 不是整数；运行累计 XP 用有界 int64，乘加预检溢出 |
| Cm | 单层脚点 XY 厘米 | 参数有限且非负；实际位置/路径由世界正式查询提供，不另建内容网格 |

所有范围、分母、精度和有限性先验证。非法输入整次拒绝，保持旧有效状态与产物，不用 Clamp
掩盖 NaN、溢出或缺少字段。表内每项属性的上下限是投影的夹取界：
构筑投影把每项属性统一夹到 `STAT.<属性>.Min/Max`，不再因属性点的极端组合拒绝整个构筑；
上下限数值不变，内容验证只检查伤害与护盾项在极值属性下不溢出 int64。AS/MS/概率的玩法夹取另外显式定义。
同一次属性变更只投影最终来源集合一次，UI 的格式化不回写结算。

### 3.2 基础角色 `character_demo`

显示名为**脆皮大学生**。令等级为 $L$，$k=L-1$。表中 `.Base` 与 `.PerLevel` 是作者键，`STAT.<属性>` 是派生结果。
初始只有普攻和 D 键通用冲刺；未学技能、未分配属性点，只有开局点数与一次开局主动机会（§4.1、§4.3）；生命按下表满值初始化，劲为 `MOMENTUM.Start`（§3.2.1）。

| 稳定参数键 | 单位/类型 | Base；PerLevel | 合法结果范围 | 依赖、理由与可复算样例 |
| --- | --- | --- | --- | --- |
| `STAT.MaxHP.Base/PerLevel` | HP 点/P2 | 860；125 | 1..20000 | 同级对打击杀时间与拉扯节奏校准；L10=1985，L16=2735 |
| `STAT.AD.Base/PerLevel` | 物攻点/P2 | 70；3.2 | 0..2000 | 普攻 1.0 AD；L5=82.8，L10=98.8 |
| `STAT.AP.Base` | 法强点/P2 | 0 | 0..3000 | 无每级自然增长；通过脑力属性点与构筑牌提升 |
| `STAT.Armor.Base/PerLevel` | 护甲点/P2 | 18；2 | 0..1000 | 首版无负抗/穿透；L10=36，不是 36% 减伤 |
| `STAT.MagicResist.Base/PerLevel` | 魔抗点/P2 | 18；2 | 0..1000 | 与护甲用相同点制公式；不能直接填入旧 MR 比例字段 |
| `STAT.BaseAttackSpeed` | 次/秒/Rate | 0.85 | 正数 | 约 1.177s 一个基础攻击节拍 |
| `STAT.AttackSpeedPerLevelRatio` | 每级加成/R4 | 0.02 | 0..1 | 与身手属性点等同类来源相加；L10 无其他来源 AS=1.003 |
| `STAT.HPRegen.Base/PerLevel` | HP 点/秒/Rate | 1.60；0.12 | 0..12 | 基础回复始终有效，体格属性点另外增加；L10=2.68/s |
| `STAT.CritChance.Base` | 概率/R4 | 0 | 0..1 | 普攻暴击按 `FORMULA.Crit`；暴击率由力道属性点获得；技能不暴击（Y09 除外，§4.5） |
| `STAT.CritMultiplier` | 倍率/R4 | 2.00 | 固定 | 不作为首版可堆叠属性；70 AD 暴击原始量 140 |
| `STAT.CooldownHaste.Base` | 冷却点/P2 | 0 | 0..400 | 只影响角色主动的下一次释放，不影响 proc/复活/刷新/喷漆/守护；急速后的冷却不低于 `SKILL.CooldownFloorSec` |
| `UNIT.MoveSpeedCmPerSec` | cm/s/Rate | 300 | 派生 173..675 | 是设计值，不宣称由引擎默认值批准 |
| `STAT.MoveSpeedCmPerSec` | cm/s/Rate | 按来源派生 | 173..675 | `FORMULA.MoveSpeed`；30%减速 300→210 不被夹紧；初始300；UI/运动读取最终值 |
| `UNIT.CollisionRadiusCm` | cm/Cm | 40 | 固定 | 最小双身体中心分离 80cm，小于建议普攻距离；世界负责净空/防重叠 |
| `UNIT.BasicAttackRangeCm` | cm/Cm | 250 | 基础值 | 给80cm碰撞分离之外的接近余量；真实射程必须读下行派生值 |
| `STAT.AttackRangeCm` | cm/Cm | 按来源派生 | 80..1000 | `FORMULA.AttackRange`；初始250 |
| `UNIT.BasicAttackWindupSec` | s/Sec | 0.180 | 正数 | 按 AS 反比缩放；基础 180ms，不能把前摇与整段攻击周期混用 |
| `UNIT.BasicAttackTargetMode` | enum | Unit | 仅 Unit | 可攻击、可见、敌对参赛者或野怪；自己非法 |
| `UNIT.BasicAttackDamageType` | enum | Physical | 固定 | 每次普攻原始量为 AD（按连击段乘 `COMBO.StepRatios`），不耗劲，无隐含顺劈 |
| `UNIT.BasicAttackADRatio` | 倍率/R4 | 1.00 | 固定 | 与物攻相乘，不读AP；暴击另按AttackId处理 |
| `character.dash.distance` | cm/Rational | 450 | 正数 | 通用冲刺（D 键，每名参赛者都有）；按 `EaseOutCubic` 先快后慢曲线位移，遇阻挡停在最远合法位置；未装 `card_pass_through` 时正前方交战锥内贴近敌人停在 135cm 处 |
| `character.dash.duration` | s/Sec | 0.18 | 正数，短于冷却 | 450cm 在 0.18s 内按 `1 - (1 - t)^3` 曲线完成 |
| `character.dash.cooldown` | s/Sec | 9.0 | 正数 | 不受技能急速影响 |

### 3.2.1 劲、连击、爆劲与完美闪避

劲是唯一的技能资源，以 P2 整数存在战斗资源记录里，取整与亚 P2 余数沿用 `POLICY.RegenIntegration`。野怪没有劲
（`CAMP.MomentumPolicy=NotApplicable`），技能不耗劲。

| 稳定参数键 | 值 | 规则 |
| --- | --- | --- |
| `MOMENTUM.Max` | 100 | 上限 |
| `MOMENTUM.Start` | 50 | 开局、复活、每次休息开始时设为此值 |
| `MOMENTUM.CombatPerSec` | 5/秒 | 交战中（距本条生命最近一次真实战斗事件不到 `MOMENTUM.IdleDelaySec`）每秒增加；脑力每点另加 0.06/秒 |
| `MOMENTUM.BasicPerRefInterval` | 10 | 普攻主击命中获得 10 × 本次攻击间隔 ÷ `MOMENTUM.BasicRefIntervalSec`，向下取 P2；每秒所得与攻速无关（约 7.5/秒） |
| `MOMENTUM.BasicRefIntervalSec` | 1.334s | 基础攻速 0.75 的攻击间隔 |
| `MOMENTUM.SkillHit` | 5 | 一次技能释放对每个目标的第一次原生直接命中给一次 |
| `MOMENTUM.Reaction` | 10 | 触发元素反应的人获得 |
| `MOMENTUM.PerfectDodge` | 20 | 精准闪避获得 |
| `MOMENTUM.PerfectDodgeWindowSec` | 0.3s | 预警效果结算前这段时间内按下的 D 躲出范围算预警精准闪避 |
| `MOMENTUM.DashDodgeWindowSec` / `DashDodgeCutRatio` / `DashDodgeCooldownRefundRatio` | 0.12s / 0.5 / 0.3 | D 冲刺（及 `as_dash` 翻滚分支）起手 0.12s 内擦过敌方直接攻击/弹道命中时触发原生精准闪避：本次伤害减半、返还通用冲刺 30% 剩余冷却、获得 `MOMENTUM.PerfectDodge` 并触发全部 `perfect_dodge` 牌 |
| `MOMENTUM.IdleDelaySec` | 5s | 脱战多久后开始回落 |
| `MOMENTUM.IdleReturnPerSec` | 10/秒 | 脱战时每秒向 `MOMENTUM.Start` 靠拢，到达后停住 |
| `AIRDROP.Momentum` | 40 | 领取外卖获得 |
| `COMBO.StepRatios` | 0.9、0.9、1.3 | 普攻三段的原始量倍率，先于暴击；第 3 段用暴击档的命中停顿 |
| `COMBO.ResetSec` | 2.5s | 距上次成功普攻起手超过即回到第 1 段；前摇取消的普攻不推进 |
| `OVERDRIVE.Threshold` | 60 | 释放瞬间劲至少这么多才按爆劲结算，否则按普通施放结算；爆劲花掉本人门槛那么多的劲（不少于技能劲消耗），余下的保留 |
| `OVERDRIVE.HoldSec` | 0.35s | 劲满时按住技能键这么久发出爆劲请求（按住 Alt 再按一下技能键时蓄力自动走完）；服务器看到的蓄力至少一半才承认 |
| `OVERDRIVE.Scale` | 1.35 | 伤害、护盾、治疗的基础量与系数整体乘；控制时长不乘 |
| `OVERDRIVE.FireBurnStacks` | 3 | 烫：额外灼烧层数（区域按跳数分摊） |
| `OVERDRIVE.WaterSlow` / `WaterSlowSec` | 45% / 1.5s | 湿：减速提高到此 |
| `OVERDRIVE.ShockReactionRatio` | 0.5 | 麻：立即额外电一次，伤害为反应伤害 R 的这一倍 |
| `OVERDRIVE.RotDurationScale` | 2 | 馊：削减持续时间乘此 |
| `AI.DodgeChance` | 0.5 | 对手 AI 看到将落在自己身上的敌方预警、D 就绪时闪出的概率 |
| `AI.EnemyGroundPathCost` | 3 | 对手 AI 规划步行路线时，路线落在可见敌方地面状态里的长度乘此 |

- 收入、花费、回落与爆劲按生命记账，每条生命结束时写一行 `LD_MOMENTUM`（各项收入、回落、花费、爆劲次数与结束值），不逐次写。
- 完美闪避适用于技能前摇至少 0.15s 的弹道、直线、扇形、自身范围、地面落点、陷阱爆炸与野怪预警技能；普攻不适用。
  每个效果实例对每人最多一次；写 `LD_PERFECT_DODGE` 并进入本人的战斗投影。
- 本体元素槽（勋章装在本体上）从普攻第 2 段起每次命中都挂元素；`COMBO.ResetSec` 内不断连即可。
- 爆劲按本人门槛扣劲（Y11 为 35），不清空整条劲；蓄力被眩晕、沉默或击退取消时不扣劲。

属性点叠加后单项属性可能触及上下限；
按 §3.1 统一夹取，被夹掉的部分不生效，不拒绝构筑。
Root 是动作禁止，不把移动速度属性改成 0 后再夹回 173。

### 3.3 可实施公式表

| 公式 ID | 输入 → 输出/顺序 | 范围、取整与样例 |
| --- | --- | --- |
| `FORMULA.StatProjection` | base + level + 五维属性点（§4.1）+ 临时来源的 flat 先相加；同类 percent 相加后乘一次；构筑牌作为同一投影的永久来源 | 先校验来源/族，再最终投影，不逐来源取整/封顶；相同来源集合换显示槽结果不变 |
| `FORMULA.Resistance` | 原始物理/魔法伤害 $D$ 与对应抗性点 $R$ → $D\times100/(100+R)$ | $R\in[0,200]$；R=0 不减伤，R=100 减半；不实现未被目录使用的负抗/穿透分支 |
| `FORMULA.Crit` | 合法普攻主击在唯一 AttackId 取一次随机值 $u\in[0,1)$；$u<p$ 时主击原始 AD 乘 2；与主击合并结算的普攻强化包（S11、S17、B12、C09 等 `empower_basic`）随主击一起乘 2 | 服务器独立确定性随机流；重发同 AttackId 不重抽；副击、吸血治疗不可暴击。技能不暴击（§4.5） |
| `FORMULA.Damage` | source 快照原始量 → 普攻暴击 → ×(1+Σ伤害加成，§4.5) → 目标实时抗性（含破防） → 向下取 0.01 → 现有屏障吸收 → 实际 HP 损失 | HP 损失不超过目标剩余 HP；70 对 18 抗=59.32，140 对 18 抗=118.64；0.009 对零抗=0，不设最低 1 点伤害 |
| `FORMULA.AttackSpeed` | $0.85\times(1+0.02k+\Sigma ASBonus)$ → AS | 最后夹取到 [`STAT.AttackSpeed.Min`, `STAT.AttackSpeed.Max`] = [0.30, 5.00] 次/秒 |
| `FORMULA.MoveSpeed` | $UNIT.MoveSpeedCmPerSec\times(1+\Sigma MoveSpeedBonusRatio)$ | 最后夹取[173,675]cm/s；不把Root动作禁止当减速属性 |
| `FORMULA.AttackRange` | 基础 `UNIT.BasicAttackRangeCm` + 永久 `AttackRangeCmFlat` → `STAT.AttackRangeCm` | 无乘区；目标/导航接近/预览/AttackMove全用派生值 |
| `FORMULA.AttackIntervalMs` | $\lceil1000/AS\rceil$ → 相邻合法 AttackStart 的最短间隔 | AS=.75 得 1334ms；已出手的攻击保留端点；前摇在出手时刻之前被打断时退回该次提交的端点 |
| `FORMULA.AttackWindupMs` | $\lceil1000\times UNIT.BasicAttackWindupSec\times STAT.BaseAttackSpeed/AS\rceil$ | 基础 180ms；必须小于本次攻击间隔。出手前被取消（冲刺、技能、S、控制、失去目标、死亡）退还已提交节拍；到出手时刻才被拒的挥空不退 |
| `FORMULA.CooldownMs` | $\lceil1000\times BaseCDSec\times100/(100+H)\rceil$ → 本次释放持续时间 | H 非负；6s、H=30 得 4616ms。10s 释放得到 ReadyAt=14.616s，随后升级不改这个端点 |
| `FORMULA.Regen` | rate × 同一权威时间的实际 elapsed + 亚P2积分余数 → 可提交的0.01点及新余数 | 见 `POLICY.RegenIntegration`；活体才恢复，满资源丢弃溢出。1.6/s 分1000次1ms仍回复1.60 |
| `FORMULA.ResourceMaxChange` | 以P2整数计算 $\lfloor oldCurrent\times finalNewMax/oldMax\rfloor$ | 用户要求保持比例，最终一次向下量化；40/100→150为60→30为12→150为60→100回40；零上限/活HP下溢见§10.3 |
| `FORMULA.XPToNext` | $60+20(L-1)$，仅 $L=1..15$ | 正整数；达到十六级不再升级，之后按 `GROWTH.MaxLevelPointsXP` 给点（§4.1）。累计阈值为 0/60/140/240/360/500/660/840/1040/1260/1500/1760/2040/2340/2660/3000 |

普攻节拍从通过初始合法性检查的 AttackStart 提交；技能则在成功 release 才提交
劲/冷却。前摇内复查目标/来源，导航不负责扣费或退节拍。前摇在出手时刻之前被打断没有挥出，
Combat 退回这次提交的节拍；到出手时刻因目标失效被拒是一次已开始的挥空，节拍照常消耗，
不是把非法最初输入当作成功攻击。最初动作输入被拒绝时不提交攻击节拍、费用或攻击效果，
但已认证进攻意图的解除保护仍按§10.2生效；不能把“失败无费用”写成“任何状态都不变化”。
前摇中的构筑/来源变更取消该暂存攻击，不能把两个修订的属性拼起来。

派生时长向上取毫秒之后，新截止时间还须从原始实际释放时刻安全建立：
`ReadyAtMs = ceil(OccurredAtSeconds × 1000) + DurationMs`，不能先floor起点而提前到期。
例如1000.8ms开始、持续1ms，截止为1002ms；1001.9ms时还不能使用。
`OccurredAtMs`若是ceil桶，只用于分桶；真正先后仍按原始OccurredAtSeconds及同刻因果顺序。
Ready比较用原始时间或安全的floor当前毫秒，不能用ceil当前桶提早开放。唯一时钟/Stamp由生命周期域维护。

### 3.4 空间与对局节奏参数

| ID | 单位/类型 | 建议初值/派生 | 依据与适用范围 |
| --- | --- | --- | --- |
| `VISION.SightGraceSec` | s/Sec | 0.3 | 被障碍物刚挡住的单位仍算看见的时间：擦过树干、路灯不打断锁定、追击与前摇；不跨视野半径、隔离或瞬移 |
| `VISION.RadiusCm` | cm/Cm | 1120 | 同高、无遮挡的基础水平视野半径；停电日为 840cm；初始 A 搜敌半径 min(1120, 2×250)=500cm |
| `VISION.NeutralHeightCm` | cm/Cm | 35 | 双方实际脚下地面高差不超过此值时，视野半径保持原值；小台阶不改变视野资源 |
| `VISION.FullHeightCm` | cm/Cm | 120 | 高差从中性阈值到此值时，以 smoothstep 平滑达到完整倍率；高差更大不继续叠加 |
| `VISION.UphillRadiusScale` | ratio/bp | 0.75 | 看高处的水平半径下限；常态最短 840cm |
| `VISION.DownhillRadiusScale` | ratio/bp | 1.25 | 看低处的水平半径上限；常态最远 1400cm；同步修正野怪既有感知半径，攻击、技能和追击边界不因此增加 |

| `TARGET.AttackMoveAcquireRadiusCm` | cm/Cm | min(VISION.RadiusCm, 2×STAT.AttackRangeCm)，初始500 | 随真实普攻射程变化；只搜合法可见目标；订单/优先级归操作域 |
| `CAPTURE.RadiusCm` | cm/Cm | 450 | 直径900cm，基速穿过约3.0s，比远程技能射程小一圈但留出圈内走位；地面装饰离旗杆的净空沿用 `camp_clearance_cm`，不随喷漆区变大；按脚点平面距离判定、边界算在内；区内仍可走位，普通受击不打断喷漆 |
| `CAPTURE.SpraySec` | s/Sec | 3.5 | 喷漆区内独自累积满即喷上涂鸦（终局在 1.15 / 2.30 / 3.50 s 设三段棘轮存档点）；争夺暂停保留，常规离区/死亡/离局/硬控施加清零（终局回退至已达棘轮存档） |
| `CAPTURE.GuardCheckpointSec` / `CAPTURE.GuardResumeSec` | s/Sec | 10 / 8 | 常规过半存档与恢复；终局在 7 s / 14 s 设两段守护存档点，且终局每次击杀参赛者推进 4 s 守护进度 |
| `CAPTURE.GuardSec` | s/Sec | 20 | 主人独自在核心内累计守护；离圈、死亡暂停，他人进区冻结，只有对手喷成才清当前进度；历史独占守护贡献保留 |
| `SHOWDOWN.UnlockDelaySec` | s/Sec | 10 | R3 返场至隔离封街战场后开始计时，到期解锁旗杆 |
| `SHOWDOWN.DurationSec` | s/Sec | 150 | 从终局开始计（含 10 s 解锁等待），终局最长 150 s；到期仅在存活、在局、非保护且有贡献者中按有效守点完成百分比 → 终局击杀 → 终局伤害排序，仍并列或无人贡献则无人胜 |
| `SHOWDOWN.RespawnLimit` | count/int | 2 | 每人终局最多入队两次复活，不继承野外的无限复活 |
| `SHOWDOWN.FirstRespawnSec` / `SecondRespawnSec` | s/Sec | 18 / 24 | 按已入队复活次数决定等待，合法队列可跨过截止点完成 |
| `SHOWDOWN.RespawnCutoffSec` | s/Sec | 90 | 从终局开始计；之后死亡不再入队，已入队复活仍可完成 |
| `SHOWDOWN.ClosureStartsSec` | s/Sec | 75 | 从终局开始计，到期从初始圈向终圈线性封街直到终局结束；圈外伤害按扫掠脚点的真实圈外时长结算 |
| `SHOWDOWN.RingRadiusCm` / `RingFinalRadiusCm` | cm/Cm | 1800 / 600 | 封街开始时的安全半径与终局结束时的终圈；须满足 0 < 终圈 < 初始圈 |
| `SHOWDOWN.RingPreviewSec` | s/Sec | 10 | 封街开始前这么久显示终圈 |
| `SHOWDOWN.RingDamageTickSec` | s/Sec | 0.25 | 圈外伤害每 250 ms 按真实圈外时间结算一次 |
| `SHOWDOWN.RingDamageEarlyMaxHPRatioPerSec` / `LateMaxHPRatioPerSec` | ratio/R4 | 0.05 / 0.12 | 封街前一半、后一半圈外每秒扣最大生命的比例，直扣生命，须大于 0 |
| `SHOWDOWN.KillHealMaxHPRatio` / `SHOWDOWN.KillMomentum` | ratio/R4；momentum | 0.35 / 50 | 终局不给经验；击杀参赛者的人回复最大生命的 35% 并得到 50 劲 |
| `MATCH.FieldSegmentSec` | s/Sec | 160 | 三段定时野外各 160 s；野外进度 T 只在野外推进，休息暂停 T；R3 返场十秒后开放终局旗杆 |
| `REST.OpeningBuildSec` | s/Sec | 20 | 开局 R0 构筑/试招窗；所有真人准备后立即结束 |
| `REST.BuildSec` | s/Sec | 40 | R1–R3 构筑/试招窗；所有真人准备后立即结束；AI 不阻塞 |
| `REST.SignupSec` | s/Sec | 10 | R1、R2 配装的最后这么久兼作 PK 报名窗；全员准备提前结束时保留 |
| `REST.DrawSec` / `REST.BettingSec` / `REST.PKReadySec` | s/Sec | 0 / 6 / 3 | PK 公布赛制与押注合并为 6 s 押注步骤，随后 3 s 准备（锁输入） |
| `REST.PKMaxSec` | s/Sec | 45 | PK 对打上限；提前分出胜负即结束 |
| `REST.SettlementSec` / `REST.PayoutSec` | s/Sec | 3 / 0 | PK 结算与派奖合并为 3 s 结算步骤；单次 PK 最多 57 s |
| `PK.ArenaRadiusCm` / `PK.RingMidRadiusCm` / `PK.RingFinalRadiusCm` | cm | 1800 / 600 / 150 | PK 圈：开打后 `PK.RingShrinkStartSec` 起缩，`PK.RingMidSec` 到中圈，`PK.RingFinalSec` 到终圈 |
| `PK.RingShrinkStartSec` / `PK.RingMidSec` / `PK.RingFinalSec` / `PK.RingPreviewSec` | s/Sec | 5 / 25 / 38 / 5 | 缩圈时间点与下一圈的预告提前量 |
| `PK.RingDamageTickSec`；`PK.RingDamage{Early,Mid,Final}MaxHPRatioPerSec` | s；比例 | 0.25；0.05 / 0.12 / 0.25 | 圈外每秒损失开打时最大生命的比例，按三个阶段 |
| `PK.TeamFormatRatio` | 比例 | 0.5 | 4、6 或 8 人时抽到两队对抗的概率；3、5、7 人固定自由混战，2 人单挑 |
| PK 出生环 | cm | 1000–1400 / 950 | 8 个 `pk_spawn_*` 绕主旗杆每 45° 一个，离旗杆 1000–1400 cm，相邻至少 950 cm |
| `PK.RewardMultiplier`；`PK.ParticipationPoints` / `PK.WinnerPointsMax` / `PK.BettingPoolMaxPoints` | 比例；点 | 1.5；1 / 3 / 2 | PK 三个属性点奖池的基数与倍率 |
| `THROW.CooldownSec` / `THROW.TelegraphSec` / `THROW.RadiusCm` / `THROW.RangeCm` | s；cm | 10 / 0.8 / 120 / 4000 | 观众投掷：冷却、落点预警、命中半径、射程 |
| `THROW.SlowRatio` / `THROW.SlowSec` / `THROW.ExposeSec` | 比例；s | 0.2 / 1 / 2 | 投掷命中：减速与暴露位置 |
| `REST.ReturnSec` | s/Sec | 3 | 返野外倒计时，锁移动、攻击、施法与冲刺输入（自己的构筑不锁；洗点、遗忘、替换与商店关闭） |
| `REST.ReturnSearchRadiusCm` | cm/Cm | 300 | 返场原脚点被占用时只在同可达面此半径内找不重叠位置，仍失败用现有批准出生点 |
| `REST.EntryReadyTimeoutSec` | s/Sec | 30 | 返回野外是全员一次性共同提交：有人放不下时入场冻结、时间线停在边界并每拍重试；超时为明确的权威故障 `LD_REST_RETURN_UNREADY` |
| `LIFE.RespawnBaseSec` / `RespawnPerLevelSec` | s/Sec | 6 / 0.5 | 野外复活等待 = 6 + 0.5 × 死亡时等级（秒）；考试周另加 `DAILY.ExamRespawnBonusSec` |
| `LIFE.SpawnChoiceCount` | Count | 3 | 死亡时从全部野外出生点不放回抽这么多个候选；不超过 4，也不超过出生点数 |
| `LIFE.SpawnChoiceSec` | s/Sec | 6 | 死者在这段时间内选一个候选，不选用第一个 |
| `OBJECTIVE.StartSec` / `AnnounceLeadSec` | s/Sec | 100 / 10 | 每段定时野外第 100 s 开放野外目标，提前 10 s 预告；须在本段内开始并结束 |
| `OBJECTIVE.FlagWindowSec` / `FlagRadiusCm` / `FlagSpraySec` | s, cm, s | 30 / 200 / 3 | 野外 1、3 的小迷你旗：开放 30 s，半径 200 cm，独自累计喷漆 3 s 获胜（规则同旗杆喷漆） |
| `OBJECTIVE.FlagCardOffer` | Count | 3 | 小迷你旗胜者的牌选组大小 |
| `OBJECTIVE.ArenaSec` / `ArenaRadiusCm` | s, cm | 40 / 900 | 野外 2 的擂台：以旗杆为中心，开放 40 s，只计双方都在圈内的参赛者伤害 |
| `OBJECTIVE.ArenaCardOffer` | Count | 5 | 擂台胜者的牌选组大小；擂台胜者另在 R3 多一个终局改装候选 |
| `LIFE.ProtectionSec` | s/Sec | 3 | 短暂移动/读局时间；不能计入“必需回防延迟”，合法主动可以提前解除 |

相对高度取双方实际支撑面，不能用世界高度分层、模型名字、身体动画或镜头高度代替。
令 `t=clamp((abs(目标地面Z−观察者地面Z)−VISION.NeutralHeightCm)/(VISION.FullHeightCm−VISION.NeutralHeightCm),0,1)`，
`s=t²(3−2t)`，半径为 `VISION.RadiusCm × lerp(1,对应方向倍率,s)`；距离按 XY 水平直线计算，含边界。
地图的 `sight_sample_height_cm=120` 是统一虚拟眼高；地形遮挡检查眼睛到目标地面，向下看时免除脚下平台边缘造成的台脚盲区。
实心桥面、屋顶仍遮住下面的单位，其他山体和实际障碍仍可遮挡；只露头不授予单位可见。倍率数值属于试玩方案，尚未经过平衡测试。

野外复活的候选在全部出生点中等概率不放回抽取，不按安全度筛选；
所有属性/技能和待选继续保留，只有等待与位置代价。
复活短于守护窗意味着死者可能及时返场改漆或围攻；这是已批准的节奏取舍，不保证自由混战必然有限时间结束。
终局以 `SHOWDOWN.*` 的期限、两次复活与入队截止为准，不使用最终野外的重复复活推导；期限内没有目标胜者时执行目标贡献裁决。

### 3.5 今日热点、营地词条与隐藏物

每局开始从十二个今日热点中不放回抽 `DAILY.Count`（2）个，整局生效并公开。

| 热点 | 参数 | 规则 |
| --- | --- | --- |
| 下雨 | `DAILY.RainIntervalSec` 20、`RainPuddles` 6、`RainPuddleSec` 10、`RainWetBonusSec` 2 | 每 20 s 在野外随机 6 处生成无主水洼（10 s）；湿持续 +2 s |
| 寒潮 | `DAILY.ColdFreezeSec` 20、`ColdChillBonusSec` 2 | 水洼生成 20 s 后变冰面；冻持续 +2 s |
| 停电 | `DAILY.BlackoutVisionCut` 0.25、`BlackoutShockScale` 2 | 参赛者视野半径 −25%（同高基础半径由 1120cm 降至 840cm，野怪感知不变）；麻的额外伤害 ×2 |
| 台风 | `DAILY.TyphoonIntervalSec` 30、`TyphoonPushCm` 150 | 每 30 s 全图向同一随机方向吹 150 cm，人和地面状态一起吹 |
| 高温 | `DAILY.HeatBurnTickScale` 1.3 | 灼烧每跳 ×1.3；起雾不生成雾区 |
| 打折 | `DAILY.SalePriceScale` 0.5 | 商店技能、牌、技能点与刷新价 ×0.5，向上取整，不低于 1 |
| 外卖日 | `DAILY.DeliveryLandSec`（12 个时刻）、`DeliveryExpireSec` 15 | 天降外卖改用这 12 个落地时刻，每份 15 s 后消失 |
| 巡查 | `DAILY.PatrolBossAppearSec` 120 | 画饼老板在 T=120 s 出场 |
| 考试周 | `DAILY.ExamMonsterXPScale` 1.3、`ExamRespawnBonusSec` 3 | 野怪经验 ×1.3；野外复活等待 +3 s |
| 健身 | `DAILY.GymDashCooldownCut` 0.3 | 冲刺冷却 −30% |
| 加班 | — | 所有营地刷新都带“加班”词条 |
| 吃瓜 | `DAILY.RumorLeaders` 2 | 上热搜每次公开前两名，各有模糊圆与悬赏 |

营地词条：高、精英与远古营地每次刷新以 `AFFIX.Chance`（0.3）的概率均匀带六个之一（加班日全部营地都带加班）。

| 词条 | 参数 | 规则 |
| --- | --- | --- |
| 加班 | `AFFIX.OvertimeAttackSpeed` 0.3 | 成员攻速 +30% |
| 团建 | `AFFIX.TeambuildXPScale` 1.5 | 营地多刷一名成员（高档营地为本营种类，精英与远古营地为一只高档种），站在营地锚点；成员经验 ×1.5 |
| 冰箱 | `AFFIX.FridgeCardOffer` 3 | 攻击挂冻、脚下常驻冰面；清营得一组三选一 |
| 奖金 | `AFFIX.BonusHP` 0.4、`BonusCardOffer` 5 | 成员生命 +40%；清营得一组五选一 |
| 裁员 | `AFFIX.LayoffAttack` 0.3 | 同营每倒下一个，其余攻击 +30%（可叠） |
| 审计 | `AFFIX.AuditTelegraphCut` 0.3、`AuditSkillPointsMax` 1 | 技能预警缩短 30%；清营得一个技能点，每局每人最多 1 个 |

隐藏物：

| 对象 | 参数 | 规则 |
| --- | --- | --- |
| 外卖柜 | `EXPLORE.LockerCount` 4、`LockerEarliestSec` 30、`LockerLatestSec` 420、`LockerStaySec` 45、`LockerRadiusCm` 150、`LockerChannelSec` 3、`LockerCardOffer` 3 | 每局从 `locker_*` 中抽 4 个，各在 T ∈ [30, 420] s 的随机时刻出现，45 s 后没人打开就消失，出现期间公开在小地图上；柜旁 150 cm 内连续 3 s 打开（受伤或硬控打断），必得一组三选一 |
| 二手贩子 | `EXPLORE.PeddlerEarliestSec` 120、`PeddlerLatestSec` 400、`PeddlerOffsetCm` 600、`PeddlerHP` 1800、`PeddlerMoveSpeed` 280、`PeddlerStaySec` 60、`PeddlerCardOffer` 5 | 每局一次，T 在 [120, 400] s 内随机出现在随机营地旁 600 cm，不攻击、被打就逃，60 s 后离开；击杀得一组五选一 |

AI 读取 `AI.ObjectiveReachCm`（2500）与 `AI.ObjectiveEntryHPRatio`（0.6）：生命不低于 60% 且离目标点不超过 2500 cm 时前往开放或预告中的野外目标。

### 3.6 跨域政策稳定键

| 键 | 值/语义 | 批准边界 |
| --- | --- | --- |
| `POLICY.StatStacking` | AddFlatThenAdditivePercent | 固定属性相加已确认；完整聚合/量纲按本报告首轮方案 |
| `POLICY.MissingSourceCooldown` | ContinueServerTime | 重持原来源沿用原结束时间 |
| `POLICY.DeadChoiceCommit` | Allowed | 死亡中可选择、学到空槽、加点与装牌，仅改保留构筑、不操作尸体资源 |
| `POLICY.ResourceMaxChange` | PreserveRatio | 比例策略；P2量化/拒绝细节见 `FORMULA.ResourceMaxChange` |
| `POLICY.ReleasedEffectSourceLoss` | ContinueForRemainingContender | 死亡/遗忘后已释放独立快照按原期限继续，所有者仍须在本局 |
| `POLICY.SourceLeave` | CancelUnsettledNoRefund | 正式离局立即取消未结束效果，已提交结果不回滚、不退款 |
| `POLICY.InvalidAggressionProtection` | BreakOnAggressiveIntent | 鉴权/身份/去重/写闸后、source/target/劲/冷却校验前处理 |
| `POLICY.CritSampling` | OncePerAttackId | 公式/实现建议，不是每tick重抽 |
| `POLICY.LifestealBasis` | ActualHPLost | 按实际扣掉的生命计算（不含护盾吸收与溢出伤害）；普攻吸血（Y17）不计合并强化普攻包，技能吸血（K15、Y10）按技能实际生命伤害生效；多来源相加，不设上限 |
| `POLICY.LifestealRecursiveTriggers` | Disabled | 治疗、反伤与周期状态伤害不再次触发吸血或命中特效 |
| `POLICY.BasicSecondaryTriggers` | Disabled | 普攻附加伤害不独立触发额外命中链 |
| `POLICY.RegenIntegration` | CarrySubP2Remainder | 同一 Combat 资源记录保留有界亚 P2 余数；先按旧速率积分至变更时刻，再切换新速率 |

## 4. 技能、状态和属性目录

定义 ID 均为稳定小写 ASCII；运行中的 24 项主动使用各自的稳定原生编号（五维属性使用 27–31），退役编号保留不复用。显示名与说明的唯一运行输入为 `content.json` 的 `display` 表。

### 4.1 五维属性与成长参数

五维属性用属性点购买，每点可加在任意一项，可以重复加。下表是每点收益（运行中）。

| ID | 显示名 | 原生编号 | 每点收益 |
| --- | --- | --- | --- |
| `stat_physique` | 体格 | 27 | `MaxHPFlat=70`、`HPRegenFlat=0.08`/秒 |
| `stat_thick_skin` | 脸皮 | 28 | `ArmorFlat=3.5`、`MagicResistFlat=3.5` |
| `stat_strength` | 力道 | 29 | `ADFlat=3.5`、`CritChanceBonusRatio=0.006` |
| `stat_brains` | 脑力 | 30 | `APFlat=6`、`CooldownHasteFlat=1`、`MomentumRegenFlat=0.06`/秒（交战中的劲回复） |
| `stat_agility` | 身手 | 31 | `AttackSpeedBonusRatio=0.02`、`MoveSpeedBonusRatio=0.005` |

每项点数 × 每点收益作为永久来源进入 `FORMULA.StatProjection`：同类 flat 与加法比例先相加再统一投影，不逐点相乘；
结果按 §3.1 夹到上下限，被夹掉的部分不生效，不拒绝加点。

**关键石（首轮实现候选，尚未做平衡验收）**：某项已分配点数达到 18 时，单独激活该项效果；不同属性分别判断，可同时激活多项，不设“最多两项”的硬门控。只看已分配点数，不含未分配或永久消费点数；休息洗点后按新的分配重新生效。超出 18 点不继续强化关键石效果。

| 属性 | 激活后的效果 |
| --- | --- |
| 体格 | 脱战回复与躺平角回复的起效等待缩短 50%（`COMBAT.KeystonePhysiqueOutOfCombatCut`）；有效治疗（技能治疗与吸血，不含自然/脱战回复）的 50%（`COMBAT.KeystonePhysiqueHealToShieldRatio`）转化为护盾，上限为最大生命的 12%（`COMBAT.KeystonePhysiqueShieldMaxHPRatio`），持续 4 秒，内置冷却 4 秒 |
| 脸皮 | 每 5 秒至多一次（`COMBAT.KeystoneThickSkinIntervalSec`），受到单次抗性后至少 30 点的敌方直接伤害时，在护盾前减免 `Max(45, 伤害 × 22%)`（`COMBAT.KeystoneThickSkinGuardPoints`、`COMBAT.KeystoneThickSkinGuardCapRatio`）并回复 10 劲；触发后 1.5 秒内（`COMBAT.KeystoneThickSkinSteadySec`）不吃减速，被击退的距离只剩一半（`COMBAT.KeystoneThickSkinKnockbackRatio`）。此外，单次折后实际扣血超过最大生命的 35%（`COMBAT.KeystoneThickSkinBleedThresholdRatio`）时，超出部分不立即扣除，转为 2.0 秒内每 0.5 秒结算一次的延缓流血（期间可开盾或吸血抵消，不重复触发命中或反伤） |
| 力道 | 对已减速、被减攻速、正被击退或受硬控的目标额外造成 14% 伤害（`COMBAT.KeystoneStrengthDamageRatio`）；主普攻暴击或第 3 段命中参赛者时额外造成目标当前生命 4% 的物理伤害（每目标 1.5 秒冷却） |
| 脑力 | 自己触发元素反应时返还本次施法实际已付劲耗的 30%（`COMBAT.KeystoneBrainsMomentumRefundRatio`；零耗劲勋章触发给 15 劲 `COMBAT.KeystoneBrainsReactionMomentum`，免费自动重放不触发），并让所有仍在冷却的已学技能冷却减少 1 秒（`COMBAT.KeystoneBrainsCooldownCutSec`）；同一释放最多一次 |
| 身手 | D 键冲刺冷却缩短 40%（`COMBAT.KeystoneAgilityDashCooldownCut`）；自愿冲刺起步获得 25% 移速加成持续 1.5 秒，自愿冲刺完成后 3 秒内（`COMBAT.KeystoneAgilityEmpowerDurationSec`）下一次普攻额外造成 `35 点 + 0.45 × 攻击力`（`COMBAT.KeystoneAgilityDamageBase`、`COMBAT.KeystoneAgilityADRatio`），同一窗口内保持移速 +25%（`COMBAT.KeystoneAgilityMoveSpeedRatio`） |

数值是首轮实现/试玩输入，不表示 TTK、胜率或抗压目标已达成。与构筑牌同时生效时，各效果独立叠加；护盾沿用 `ShieldPolicy=StackToLatestExpiry`。

| 参数 | 值 | 作用 |
| --- | --- | --- |
| `GROWTH.OpeningPoints` | 3 | 开局（成长 `BeginMatch`）给的点数；已确认决定 |
| `GROWTH.PointsPerLevel` | 3 | 每次升到 2–16 级各给的点数；已确认决定 |
| `GROWTH.MaxLevelPointsXP` | 240 XP | 满 16 级后，超出 `XP.Threshold[16]` 的累计 XP 每满这么多 |
| `GROWTH.MaxLevelPoints` | 3 | 每满一段给的点数；满级后不再发牌三选一 |
| `GROWTH.BuildSafeWindowSec` | 4 s | 交战窗口（§4.3）：只供 HUD 在交战中收起野外文字摘要；不限制任何成长命令 |
| `progression.active_offer_levels` | [2, 5, 9] | 额外主动机会的等级 |

基础点数 = `OpeningPoints` + `PointsPerLevel` ×（L − 1）+ `MaxLevelPoints` ×
⌊max(0, XP − `XP.Threshold[16]`) / `MaxLevelPointsXP`⌋（最后一项只在 16 级计算）及池空奖励；
未分配点数 = 已得 − 五维点数之和，不能为负。
不计额外奖励时开局与正常成长到 16 级共 48 点；之后每满 240 XP 再得 3 点。

**技能等级成长**：固定项随拥有者等级 L 乘 G(L)，再乘技能等级倍率，`SKILL.GrowthPerLevel=0.04`：

$$
G(L)=1+0.04(L-1)
$$

| L | 1 | 4 | 8 | 12 | 16 |
| --- | --- | --- | --- | --- | --- |
| G(L) | 1 | 1.12 | 1.28 | 1.44 | 1.6 |

| 参数 | 值 | 作用 |
| --- | --- | --- |
| `SKILL.RankBaseScale` | 1、1.35、1.7 | 技能 1–3 级的基础量倍率（伤害、护盾、治疗、反震、区域跳伤） |
| `SKILL.RankRatioScale` | 1、1.25、1.5 | 技能 1–3 级的攻击力 / 法强 / 生命上限系数倍率 |
| `SKILL.PointLevels` | 3、6、9、12、15 | 到这些等级各得 1 个技能点 |
| `SKILL.CooldownFloorSec` | 0.3s | 急速后的 Q/W/E 冷却下限（动画与网络的技术下限，不是玩法上限） |

- **成长范围**：最终基础量 = 表内基础值 × G(L) × 等级倍率；系数 × 等级系数倍率；爆劲再 × 1.35。
  效果的基础量（伤害、区域与周期伤害、强化普攻、护盾）和限时属性加成的点数乘 G；劲耗、比例、持续时间、冷却、射程、层数不随等级变化，只随分支变化；
  劲的获得量（如“每命中一人回 10 劲”）不乘。
- **技能点**：未花的技能点 = `SKILL.PointLevels` 中已达到的个数 − 各技能（等级 − 1）之和；遗忘或替换技能退回它花的全部技能点。
- **快照**：主动在释放时按当时的等级、技能等级、分支和属性计算；护盾、灼烧在施加时固定。
- **灼烧**：每层每秒 `COMBAT.BurnPerSec`(4.5) × G(L施加者) + `COMBAT.BurnAPRatioPerSec`(0.03) × 施加者法强。
  每层自施加起独立持续 `COMBAT.BurnSec`(3s)，自施加起每 `COMBAT.BurnTickSec`(0.5s) 结算一次；
  最多 `COMBAT.BurnMaxStacks`(5) 层，满层时新层替换最早到期的一层。

### 4.2 角色主动

伤害类型 P = 物理（按护甲），M = 魔法（按魔抗）；H = 施法者生命上限。表中是 G = 1、技能 1 级的基础值，即 `content.json` 的效果值，
固定项随拥有者等级乘 G（§4.1）。冷却是基础冷却，受技能急速影响。技能不暴击。
五种元素、十种反应与地面状态见 §4.4。

| 编号 | ID / 名称 | 状态 | 开局池 | 目标、射程、前摇 | 劲 / 冷却 | 效果 |
| --- | --- | --- | --- | --- | --- | --- |
| S01 | `active_elbow` 滚远点 | 运行中 | 是 | 单位 220cm；0.15s | 25 / 4.2s | P 65 + 0.85 攻击力；把目标沿施法者指向目标的方向击退 300cm，0.25s |
| S02 | `active_pants_down` 扒裤子 | 运行中 | 是 | 方向直线 440cm，半宽 65cm；0.14s | 25 / 6.0s | 抓扯命中首个敌人：P 55 + 0.7 攻击力 + 0.5 法强；定身 1.1s（移动或冲刺中目标 +0.3s）；3 层破防 |
| S03 | `active_crotch_grab` 掏蛋子 | 运行中 | 否 | 方向冲刺 280cm / 0.16s；0.12s | 30 / 6.3s | 首个接触目标同时触发普攻与技能命中效果：72 + 0.95 自适应；正面减速与减攻速各 35%（1.5s）；从后背 150° 扇面或对受控/撞墙目标命中必定暴击并眩晕 1.1s |
| S05 | `active_unplug` 拧你手腕 | 运行中 | 否 | 自身；无前摇 | 20 / 6.3s | 0.45s 移动卸力招架（期间减伤 65%）：受敌方普攻/冲刺/弹道命中时向 280cm 反震 P 65 + 0.75 额外攻击力 + 0.65×(护甲+魔抗)、缴械 1.3s，并将下次普攻置为第 3 段重拳且 3s 内攻速 +35%（未受击结束返还 30% 冷却） |
| S06 | `active_snark_bolt` 甩你一鞋 | 运行中 | 是 | 方向弹道 850cm，弹道半径 55cm，1650cm/s；0.18s | 25 / 4.2s | 首个命中的敌人受 85 + 0.9 自适应（触发普攻命中追加效果），减速 25%（1.5s），并在落点生成存在 3s 的拖鞋（半径 110cm）：施法者拾取返还 55% 剩余冷却并在 3s 内获 +20% 攻速与 +15% 移速 |
| S07 | `active_seethe` 烫你屁股 | 运行中 | 是 | 自身 300cm；0.25s | 30 / 4.9s | 烫：M 60 + 0.6 法强 + 2% H；2 层灼烧 |
| S08 | `active_quick_meeting` 脚底开锅 | 运行中 | 是 | 地面点 650cm，半径 240cm；0.25s | 35 / 5.6s | 烫：固定区域 3s，释放后 0、1、2s 各一跳；每跳 M 30 + 0.25 法强、1 层灼烧、减速 20%，0.5s |
| S09 | `active_hose_down` 滋你一脸 | 运行中 | 是 | 方向直线 600cm，半宽 80cm；0.2s | 25 / 4.9s | 湿：直线上的敌人受 M 55 + 0.55 法强；沿施放方向击退 180cm，0.15s；潮湿 |
| S10 | `active_zap` 电你个哆嗦 | 运行中 | 是 | 单位 650cm；0.25s | 30 / 4.9s | 麻：M 60 + 0.65 法强；麻；之后每 0.15s 弹一次，最多 2 次，每次为上一次的 70%：350cm 内有未被弹过的其他敌人就弹给最近的一个，没有就弹回第一个目标 |
| S11 | `active_bail_out` 贴脸补一拳 | 运行中 | 是 | 方向冲刺 350cm / 0.2s；无前摇 | 20 / 4.9s | 3s 内下一次普攻多造成 P 55 + 0.75 攻击力 + 0.45 法强，并入普攻主击 |
| S13 | `active_charged_ram` 蛮牛顶 | 运行中 | 是 | 方向冲刺 460cm / 0.28s；0.1s | 30 / 6.3s | 无元素：出发获护盾 70 + 0.4 攻击力 + 6% H + 0.55×(护甲+魔抗)，2.5s；每个接触敌人受 P 60 + 0.6 攻击力 + 0.4 法强 + 3% H + 0.45×(护甲+魔抗) 并震退 180cm（0.16s） |
| S14 | `active_not_mad` 锅盖挡脸 | 运行中 | 是 | 自身；无前摇 | 25 / 7s | 护盾 95 + 0.65 攻击力 + 0.6 法强 + 8% H + 0.5×(护甲+魔抗)，3s；解除减速和定身（不解除缴械） |
| S15 | `active_stare_down` 来打老子啊 | 运行中 | 否 | 自身 300cm；0.2s | 30 / 8.4s | 嘲讽 300cm 内的参赛者 1.0s；自身护甲、魔抗各 +14（乘 G），3s |
| S16 | `active_brace` 顶着揍 | 运行中 | 否 | 自身；0.1s | 25 / 8.4s | 3s 内受到的敌方伤害减 20%，仍可移动、普攻、施法和冲刺；记录本技能实际减掉的伤害，最多 120（乘 G 与等级倍率）；自然结束时在自身 300cm 反震 M 50 + 记录量的 60%；死亡、离局不反震 |
| S17 | `active_triple_combo` 左右开抽 | 运行中 | 是 | 自身；无前摇 | 20 / 6.3s | 5s 内接下来 3 次本体普攻各多造成 P 24 + 0.28 攻击力 + 0.18 法强，并入普攻主击；仍有次数时攻速 +35% |
| S18 | `active_calculus_drop` 板砖糊脸 | 运行中 | 是 | 地面点 650cm；0.25s | 35 / 5.6s | 释放 0.55s 后在固定点落下：80cm 内 110 + 1.0 自适应（额外攻击力 × 1.4 ≥ 法强时为物理，否则为魔法），80–200cm 为中心伤害的 60%；落点有可见预警 |
| S19 | `active_flyer_trap` 踩一脚试试 | 运行中 | 否 | 地面点 550cm；0.15s | 30 / 6.3s | 放一张陷阱，保留 6s；敌人进入 100cm 后 0.45s 在陷阱处爆炸，半径 180cm：M 100 + 0.75 法强，减速 30%，1.5s；到期未触发不爆炸 |
| S22 | `active_open_mic` 扯嗓子喷你 | 运行中 | 是 | 自身 340cm；0.15s | 25 / 5.6s | 环形战吼清除圈内敌方普通弹道，对周围造成 M 65 + 0.55 攻击力 + 0.5 法强 + 3% H（对野怪 ×1.6）并震退 160cm（0.15s）；获护盾 75 + 0.45 法强 + 6% H + 0.6×(护甲+魔抗)，每命中 1 名敌人护盾 +25% |
| N1 | `active_cola_shake` 冰可乐摇一摇 | 运行中 | 是 | 方向扇形 400cm / 60°；0.15s | 25 / 4.2s | 冻：M 55 + 0.5 法强；冻；命中推开 100cm（0.12s） |
| N2 | `active_desk_fan` 电风扇 | 运行中 | 否 | 地面点 500cm 放置，朝向 = 施放方向；0.2s | 35 / 7s | 无元素：风扇 4s，每 0.5s 向前方 600 × 200cm 矩形吹一次：推开 60cm（`COMBAT.PushSec` 0.12s） |
| N3 | `active_stale_takeout` 过期外卖 | 运行中 | 否 | 方向弹道 700cm，弹道半径 60cm，1400cm/s；0.2s | 30 / 5.6s | 馊：首个命中的敌人受 M 75 + 0.7 法强 + 0.7 攻击力；馊；落点（命中点或终点）留毒雾 4s；命中推开 100cm（0.12s） |
| N4 | `active_school_bag` 占座书包 | 运行中 | 是 | 地面点 450cm；0.2s | 30 / 7.7s | 放下一个书包（障碍物，不是身体）：生命 150 + 20 × (L−1)，身体半径 40cm，挡住所有身体的移动和敌人的弹道，不挡视线；4s 后或被打掉时在半径 200cm 内爆炸，敌人受 M 85 + 0.7 攻击力 + 0.65 法强（对野怪 ×1.4）；同一技能只留 1 个（Y12 每份 +1） |
| N5 | `active_flurry_rush` 疯狗乱拳 | 运行中 | 否 | 自身；无前摇 | 25 / 7.0s | 4s 狂热步法（攻速 +35%、移速 +18%）：期间每次普攻额外向前挥出 420cm 穿透拳风（半径 65cm，1900cm/s），造成 32 + 0.38 自适应伤害 |
| N6 | `active_cordon_tape` 警戒封条 | 运行中 | 否 | 地面点 560cm；0.16s | 30 / 6.8s | 横向拉设 500×70cm 弹性警戒带（持续 5s）：敌方触碰受 65 + 0.7 自适应伤害、弹退 180cm、减速 30%（1.5s）并易伤 +15%（2.5s）；本人飞行弹道穿过封条时射程与伤害 +25% |

- **开局池**：15 个主动带 `opening: true`（S01、S02、S06–S11、S13、S14、S17、S18、S22、N1、N4）；其余 9 个特化技能（S03、S05、S15、S16、S19、N2、N3、N5、N6）只在 2、5、9 级机会与商店出现。
- **开局三选一分桶**：开局机会先各抽 1 个近战搏斗（指向且吃攻击力）、1 个远程或范围、1 个位移或控制（冲刺、自身或带控制），某桶抽空时由其余补足。
- **自适应与双系数技能**：掏蛋子、甩你一鞋、板砖糊脸、疯狗乱拳、警戒封条按 `adaptive_ratio` 自适应结算（额外攻击力 × 1.4 ≥ 法强时走物理护甲并吃额外攻击力，否则走魔法魔抗并吃法强）；扒裤子、拧你手腕、贴脸补一拳、蛮牛顶、锅盖挡脸、左右开抽、扯嗓子喷你、过期外卖、占座书包同时吃攻击力与法强系数（重装招架/冲撞/护盾另吃双抗或生命上限加成）。
- 同一技能自己放下的地面状态（N1 冰面、N3 毒雾）、警戒封条与风扇同时只留一份，新的使旧的消失；Y12 每份多留一份。
- 踩一脚试试的陷阱与警戒封条始终按视野可见，不隐形、不提供视野、不模仿奖励或涂鸦；板砖、封条与陷阱走合法地面与遮挡查询；正常死亡不撤回已释放的独立落地效果，离局或终局取消。
- 滚远点的击退遇阻挡停在最远合法位置；撞墙眩晕来自分支（S01 B1、S09 B1）和连接件，并使用真实阻挡判定。
- **打断喷漆**：扒裤子（定身）、掏蛋子（侧后/受控眩晕）、滚远点（击退）、蛮牛顶（震退）、扯嗓子喷你（震退，3B1 附沉默）、来打老子啊（嘲讽）。
  拧你手腕的缴械、所有减速、灼烧和伤害都不打断喷漆。
- **硬控（计入递减）**：定身、眩晕、沉默、缴械、嘲讽；击退不计递减（§4.5）。
- 冲刺类主动遵守 Root 禁止 Dash（`POLICY.RootBlocks`）。锅盖挡脸被沉默或眩晕时不能使用，所以不能解除沉默。
- 甩你一鞋的方向请求不需要指定隐藏单位；服务器按真实碰撞处理，客户端不能借弹体回执取得隐藏位置。
  弹体最远飞行约 0.469s，必须有方向和宽度一致的表现。单位目标无效不能悄悄转换成地面或方向目标。
  技能命中其他参赛者或野怪；自己不是敌对目标。

#### 4.2.1 技能分支

每个技能 1 级之外有六个分支：2 级在 2A、2B 中选一个；3 级在已选的 2A 下选 3A1 或 3A2，或在 2B 下选 3B1 或 3B2。
3 级包含它的 2 级改动。表中数值也随等级倍率与 G 放大（除非写明固定；劲的获得量不放大）。分支数据在 `content.json`
各技能的 `ranks` 里，以登记过的补丁写成（改目标、改字段、乘字段、加/删效果、加冷却），导入器拒绝未登记的补丁；
编译器把六条路径分别解析成完整的分支定义，原生按等级与技能等级放大。名称后括号里是分支的显示名。

| 编号 | 2A | 3A1 | 3A2 | 2B | 3B1 | 3B2 |
| --- | --- | --- | --- | --- | --- | --- |
| S01 | 给我过来：改为拉到身前 120cm | 拉完就揍：2s 内下一次普攻 +100% | 拉进水里：拉完挂湿 | 一脚踹远：击退 500cm | 踹到墙上：撞墙眩晕 0.8s | 踹出内伤：被踹的人 3s 内受到伤害 +20% |
| S02 | 拽裤腰带：命中后将目标拉近 220cm | 光着挨揍：定身期间你对他伤害 +25% 且下次普攻强化 | 静电秋裤：拉近后挂麻 | 抽裤腰带：抓扯射程扩至 620cm，挥空减速前方敌人 | 顺手摸兜：抓中偷取目标 22 劲 | 连扒两下：3s 内免劲再扒一次，重复命中改为缴械 1.0s |
| S03 | 蓄势黑手：突刺扩至 360cm、伤害提高 | 断子绝孙：对低于 45% 生命目标按已损生命追加处决伤害 | 麻筋寸劲：转魔法伤害，触发暴击时向周围迸射感电冲击波 | 掏完就溜：命中后向后/移动方向翻滚 250cm（享 0.12s 精准闪避） | 惯犯连手：触发背刺眩晕时返还 55% 剩余冷却 | 疼得撒手：命中额外缴械 1.1s 并加速自身 |
| S05 | 借力贴身：反震成功向攻击者踏步贴近并回 18 劲 | 以彼之道：反震额外附加所挡原始伤害的 55% | 卸腕连锤：反震后接下来 3 次普攻全视为第 3 段重拳 | 站桩铁腕：招架延至 0.65s，成功获双抗+最大生命护盾 | 震麻虎口：反震对近战/冲刺来源额外眩晕 0.7s | 气沉丹田：反震成功回复 12% 已损生命 |
| S06 | 大号拖鞋：弹道变宽，命中把人拉近 200cm | 泡水拖鞋：挂湿 | 臭脚老鞋：挂馊 | 回旋拖鞋：到终点折返，回程可再命中 | 带电回程：回程挂麻 | 接住再抽：拾取/接鞋时下次普攻 +80% |
| S07 | 越烧越旺：灼烧 4 层 | 烧透了：满 5 层时额外 M 40 + 0.4 法强 | 烫一大片：半径 +100cm | 烫一路：之后 2s 每秒以你为中心再烫一次（40%） | 烫出劲：每次再烫回 5 劲 | 烫出壳：每次再烫给自己 20 + 2% H 护盾 |
| S08 | 边走边开：区域跟随你，半径 ×0.8 | 会上溜达：区域里你移速 +20% | 会上装死：区域里你受到伤害 −15% | 一锅端：改为一次 M 90 + 0.8 法强、3 层灼烧 | 锅再炸：落点 1s 后再炸一次（50%） | 锅底最烫：中心 80cm 内伤害 ×1.5 |
| S09 | 拉回来：改为拉近 180cm | 湿身挨打：拉近后 1.5s 内他受到普攻伤害 +30% | 带电水枪：拉近后挂麻 | 高压水枪：宽度 ×0.5，伤害 ×1.6，击退 300cm | 冲到墙上：撞墙眩晕 0.8s | 冲得过瘾：命中回 15 劲 |
| S10 | 多弹两下：最多弹 4 次 | 电出火：每弹一次挂 1 层灼烧 | 电出劲：每次命中回 3 劲 | 一道粗的：不弹射，伤害 ×1.8 | 过载：命中后 2s 内他每次受到直接伤害都触发麻的额外伤害（不再数三次） | 远程电击：射程 900cm |
| S11 | 隔空连打：强化存 3 次（各 70%），有强化时普攻射程 +250cm | 打出劲：每次强化普攻回 5 劲 | 最后一脚：第三次强化击退 200cm | 闪身：冲刺距离 500cm，冲刺后 0.5s 内穿过身体 | 穿身而过：冲刺穿过的敌人也吃一次强化伤害 | 看准了打：强化普攻 +50% 并必定暴击 |
| S13 | 重装护甲：护盾提高并延长 | 碾过留痕：冲撞定身 0.7s | 重锤震碎：护盾破裂向周围爆发反伤 | 远途猛冲：冲刺距离扩至 600cm | 撞墙塌方：终点震地冲击 | 二段回顶：2s 内可免劲再冲一次 |
| S14 | 铁锅：护盾 ×1.5，持续 4s | 锅反弹：护盾存在时反弹 20% 直接伤害 | 锅底回血：护盾自然结束时剩余量的 50% 治疗自己 | 锅盖拍脸：护盾 ×0.6，同时对 300cm 内 M 60 + 0.6 法强、击退 150cm | 拍出劲：拍到人回 15 劲 | 拍出厚锅：拍到人时护盾翻倍 |
| S15 | 嘴硬：嘲讽 1.4s | 脸皮厚：嘲讽期间你受到伤害 −25% | 破你防：嘲讽期间被你普攻的人挂 1 层破防 | 来来来：先拉近 150cm 再嘲讽 0.8s | 撞一块：拉近两人以上时各受 P 40 + 0.4 攻击力 | 来一个打一个：嘲讽期间你攻速 +40% |
| S16 | 硬顶：减伤 35%，记录上限 ×1.5 | 顶晕你：反震眩晕 0.5s | 顶出血：反震也治疗自己记录量的 30% | 越顶越疼：持续 2s，每被打一次你的下一次普攻 +10%（可叠） | 顶开你：反震推开 200cm | 顶成壳：结束时记录量转为等量护盾 3s |
| S17 | 多抽两下：强化 5 次（各 75%） | 抽出劲：每次强化回 4 劲 | 抽破防：每次强化挂 1 层破防 | 狠抽：强化 2 次（各 180%） | 抽晕你：第二次眩晕 0.4s | 抽准了：两次都暴击 |
| S18 | 连环砖：落下后 0.8s 同点再落一次（50%） | 第二砖更狠：第二砖中心伤害 ×2 | 带电砖：第二砖挂麻 | 精准砖：只砸中心 120cm，外圈取消，伤害 ×1.4 | 砸晕：中心命中眩晕 0.6s | 砸出劲：中心命中回 20 劲 |
| S19 | 雷区：每人同时 2 张陷阱 | 连线：两张相距 400cm 内时连线，穿线的人受 M 30 + 0.3 法强 | 湿传单：爆炸挂湿 | 大地雷：半径 260cm，触发后 0.25s 爆炸 | 炸飞：爆炸击飞 150cm | 炸软：爆炸改为定身 0.8s |
| S22 | 余音绕梁：战吼后 2s 每秒散发声浪光环 | 低音震胸：最大生命扩圈且每跳叠 1 层破防 | 越吼越嗨：声浪伤害的 30% 转为护盾 | 破音大嗓门：震退扩至 320cm，撞墙目标易伤 +20% | 震耳欲聋：战吼附加沉默 1.0s | 破嗓爆盾：引爆自身现有护盾追加范围伤害 |
| N1 | 喷地成冰：扇形区留冰面 3s | 冰上加料：站在这片冰面上的敌人受到你的伤害 +15%（乘区） | 冰面持久：冰面持续 6s | 连喷：0、0.25、0.5s 三下，各 50% | 冻住：第三下定身 0.6s（计硬控递减） | 冰爽回劲：每下命中回 4 劲 |
| N2 | 大风扇：矩形宽 300cm，推开 100cm | 吹上墙：被风推到墙上的敌人眩晕 0.5s | 带料的风：风同时带上你最后施加的元素 | 摇头扇：朝向左右各摆 60°，每秒 60° | 吹出劲：每次吹到敌人回 2 劲 | 开一整天：持续 6s |
| N3 | 全家桶：毒雾半径 ×1.5 | 熏得手软：毒雾里的敌人攻速 −20% | 臭得持久：毒雾持续 7s | 外卖到付：命中后 1.5s 在他脚下再爆一次（M 45 + 0.42 法强，即 60%） | 加倍馊：二次爆炸挂 8s 的馊 | 到付回本：二次爆炸回 15 劲 |
| N4 | 两个书包：同时 2 个 | 书包连墙：两个书包之间连一道宽 60cm 的墙，跟书包一起消失 | 冰镇书包：爆炸挂冻 | 炸药包：生命减半，爆炸伤害 ×2，半径 260cm | 书包炸飞：爆炸击退 250cm | 炸出火场：爆炸后留火场 3s |
| N5 | 宽幅拳风：拳风加宽并延长至 520cm | 破甲连风：每道拳风叠 1 层破防 | 吸血乱拳：拳风享受 20% 吸血 | 狂热加码：开启攻速提高至 +55% | 重拳风暴：第 3 段普攻拳风伤害 ×1.8 | 战意续航：参与击杀延长 2.5s 并回 20 劲 |
| N6 | 高压封条：拉长至 680cm，二次触碰或冲刺触碰眩晕 0.9s | 导电封条：触碰挂麻 | 封锁减速带：减速提高至 50% | 弹射加速门：友方弹道穿过伤害加成升至 +40% 且弹速 ×1.35 | 穿门穿透：穿过封条的弹道获 +1 穿透 | 跨线提速：自身穿过封条刷新 1.5s 冷却并加速 |

- 拉近（S01 2A、S06 3A1、S09 2A、S15 2B）、回程与穿透（S06 2B、S21 2B）、再落（S08 3B1、S18 2A）、区域跟随（S08 2A、S20 2A）、
  末击击退（S11 3A2、S22 3B1）、强化普攻射程（S11 2A）复用改装件的同名机制；分支自带时不算牌的触发。
- “2A 给我过来 / 边走边开 / 隔空连打”与改装件“过来吧你 / 拖着会开 / 隔空三连”同义，显示名避开牌名。
- 野怪不会升技能；对手 AI 有技能点就升（§13 对手 AI）。

### 4.3 主动机会、学习与加点

产品规则见 App Spec §4.2；本节是参数与可检验边界。

| 规则 | 内容与参数 |
| --- | --- |
| 开局（1 号机会） | 成长 `BeginMatch` 时给出；从 `opening: true` 的主动中等概率、不放回抽 3 个 |
| 2、5、9 级 | `progression.active_offer_levels=[2,5,9]`：每达到其中一级加一次机会，不放回抽 3 个；力道或脑力投入更多时，吃对应系数（攻击力或法强）的技能权重为 2；该级的点数照发 |
| 排除与不足 | 已学主动与待学图纸不进入候选；只有 1–2 个合格时只给这些；没有合格时机会排队、不生成候选，构筑变化后重新判定；排队不阻挡加点 |
| 队列与随机 | 同时只有一组候选，其余机会排队；随机由本局私有种子、参赛者与机会序号决定，同一序号重试不换种子；没有权重、没有重抽 |
| 选择 | 选中项成为待学图纸；有空槽时同一次提交学到首选空槽（未指定则按 Q、W、E 第一个空槽），Q/W/E 已满时存为图纸，回执写明原因（没有空槽）；对局进行中随时可选（交战中、死亡等待、休息每个步骤） |
| 学习图纸 | 学到指定的空槽，时机同上（死亡等待中也可以）；满槽替换仅在休息配装中或野外躺平角脱战就绪后允许（退还旧技能已投技能点，兼容技能牌直接迁至新技能，不兼容牌退回牌库） |
| 加点 | 一次至少 1 点，按五维分配，合计不超过未分配点数；时机同上（对局进行中随时，含交战与死亡等待）；洗点只在休息配装中（支持全退或按五维单退） |
| 交战窗口 | `GROWTH.BuildSafeWindowSec=4`：普攻成功起手或进攻技能释放、对敌方造成正伤害（含灼烧与区域跳伤）、受到敌方正伤害、被施加定身/眩晕/沉默/缴械/嘲讽/击退（单独减速不算）后 4s 内算交战；只用于 HUD 交战中收起野外摘要，不限制学习、加点或装牌 |

- 重学同一定义沿用该定义原有的冷却记录与结束时间。

### 4.4 构筑牌与参数

`content.json` 的 `cards` 共 94 项（89 张构筑牌 + 5 枚独立掉落三阶融合元素勋章；K05/K11 的 `host_control` 与 K07/K10 的 `host_hit_types` 按技能当前已选分支完整效果集校验可装宿主）。规则见 [App Spec §4.4](../APP_SPEC.md#44-构筑牌)。

**参数**（运行中）：

| 参数 | 值 | 作用 |
| --- | --- | --- |
| `CARDS.CapacityBase`、`CARDS.LevelsPerSlot` | 6、3 | 容量 = 6 + ⌊(等级 − 1) / 3⌋：L1–L3 6 格，L4 7 格，L7 8 格，L10 9 格，L13 10 格，L16 11 格；所有位置共用，没有分列上限 |
| `CARDS.IdleCarrySec` | 5 s | 攒个大的（Y22）与憋着一发狠的（K16）只在交战中累计闲置；开打前最多带入这么多秒 |
| `CARDS.OfferSize` | 3 | 每组牌三选一的张数（不重复） |
| `CARDS.OfferEveryLevels` | 3 | 升到能被它整除的等级时给一次牌三选一（3、6、9、12、15 级，共 5 组）；属性点仍每级给 |
| `CARDS.VocabWeight` | 1.2 | 与已装牌或已学技能（含已选分支 `vocab`）共享 1 个词汇的牌权重 ×1.2，共享 2 个及以上封顶 ×1.8；基础权重为牌的 `drop_weight` × `draw_weight`；限定宿主的技能牌在当前没有任何已学技能能装时不出现 |
| `CARDS.FreeRerolls`、`CARDS.RerollsPerRest` | 3、1 | 开局重抽次数；R1–R3 每次休息再给的次数 |
| `CARDS.SkipPoints` | 1 | 跳过一组牌三选一得到的属性点 |
| `DAMAGE.FactorTechCap` | ×1000 | 一次伤害乘区之积的技术上限（只防溢出，`LD_DAMAGE_FACTOR_CAPPED`） |
| `CHAIN.MaxDepth`、`CHAIN.MaxEffectsPerStep` | 16、256 | 连锁最深步数；一个权威步最多产生的后续效果，其余顺延（`LD_CHAIN_GUARD`） |
| `CHAIN.EndSec`、`CHAIN.MinKeySteps` | 3 s、3 | 连锁多久没有新的一步就结束；结束时至少有几种不同步骤才算一把连锁键（App Spec §4.4） |
| `AI.CardSkipChance` | 30% | 一组牌里没有一张和构筑共享词汇时 AI 跳过换属性点的概率 |
| `AI.CardVocabScore`、`AI.BreakoutChance`、`AI.BreakoutCardScore` | 2、25%、5 | AI 选牌时每个共享词汇加分；开局抽质变目标的概率；目标样板里的牌额外加分（[对手 AI](../systems/OPPONENT_AI.md)） |

- **稀有度与抽取权重**：每张构筑牌按 `drop_weight` 分为普通 `100`、稀有 `22`、传说 `6`；悬赏、空投等高档来源设最低稀有度保底；叠加不设上限。

**词汇**：`basic_hit`、`combo3`、`skill_hit`、`skill_cast`、`overdrive`、`reaction`、`element`、`dash`、`dash_contact`、`perfect_dodge`、
`knockback`、`wall_impact`、`shield`、`shield_break`、`controlled`、`control`、`damaged`、`low_hp`、`kill`、`idle`、`ground`、`heal`、
`momentum`、`attack_speed`、`move_speed`、`crit`、`armor`、`max_hp`（原生 `ELDCardVocab` 位序）。技能的词汇由编译器按元素、投送、控制、
位移与护盾推出（`ability_vocab`），分支各自另算；界面不显示词汇。

**同名叠加**：同一张牌的多份是独立的牌，各占一格。比例类按同一来源相加（乘区内加，乘区间乘）；次数类逐份增加
（A06 每多一份多一下，K06 每多一份多一次重放，D05 每多一份多一段）；Y07 护盾量相加；Y03 每条命一次与份数无关。

**目录**（编号、名称、类型、效果与词汇；形式由编号首字母决定：K 技能、D 冲刺、Y 本体；A 开头的普攻向牌也是本体牌，装在本体上）：

| 编号 | 定义 | 名称 | 类型 | 效果 | 词汇 |
| --- | --- | --- | --- | --- | --- |
| A01 | `card_third_breath` | 第三拳回口气 | 触发 | 第 3 段命中 → 回 10 劲 | combo3, momentum |
| A02 | `card_punch_think` | 边打边想 | 触发 | 普攻命中 → 随机一个未就绪技能冷却 −0.5 s | basic_hit, skill_cast |
| A03 | `card_solid_punch` | 拳拳到肉 | 触发 | 第 3 段命中同时算一次“技能命中”（触发技能命中类牌与劲，不触发任何技能本身效果） | combo3, skill_hit |
| A04 | `card_third_shove` | 第三拳滚蛋 | 触发 | 第 3 段把主目标推开 150 cm / 0.12 s（真实阻挡，可撞墙） | combo3, knockback |
| A05 | `card_long_arm` | 隔空抽你 | 改造 | 普攻射程 +330 cm，变成会被躲开的飞行物（1800 cm/s、半径 55 cm），命中即停 | basic_hit |
| A06 | `card_both_hands` | 左右开弓 | 改造 | 第 3 段变成两下，各 0.8 倍，两下都算普攻命中 | combo3, basic_hit |
| A07 | `card_wide_swing` | 抡圆了打 | 改造 | 第 3 段变成身前 160° 横扫：主目标全额，其他人 60% | combo3 |
| A08 | `card_fist_skill` | 拳里带招 | 改造 | 第 3 段命中 → 以 45% 伤害/护盾/治疗效果、50% 硬控时长放出 Q 技能（不耗劲、不进冷却、不触发爆劲；按 Q 的射程） | combo3, skill_cast |
| A09 | `card_quick_hands` | 手快有手慢无 | 转换 | 攻速超过 1.35 次/秒的部分，每 0.1 次/秒 → 每次普攻多打一下的概率 +18%（可超过 100%：每满 100% 必定多一下） | attack_speed, basic_hit |
| A10 | `card_slow_craft` | 慢工出细活 | 转换 | 攻速每比 1.2 次/秒低 0.1，普攻伤害 +8%（乘区） | attack_speed |
| A11 | `card_scholar_punch` | 读书人打架 | 转换 | 普攻额外造成 40% 法强的魔法伤害（并入主击） | basic_hit |
| A12 | `card_heated_up` | 越打越上头 | 成长 | 连续打同一个人，每下攻速 +4%，无上限；换目标或 3 s 没打清零 | attack_speed, basic_hit |
| A13 | `card_muscle_memory` | 熟能生巧 | 成长 | 普攻每满 4 点进度（命中参赛者记 2、野怪记 1），本局攻击力 +2、攻速 +1% | basic_hit |
| A14 | `card_breach_fist` | 越揍越不经打 | 成长 | 普攻命中挂 1 层破防（每层双抗 −3，上限 5，4 s） | basic_hit, armor |
| K01 | `card_next_up` | 打完换下一招 | 触发 | 这个技能命中 → 其他已学技能冷却各 −1 s | skill_hit, skill_cast |
| K02 | `card_pay_back` | 一招回本 | 触发 | 这个技能每次命中回 6 劲 | skill_hit, momentum |
| K03 | `card_quick_takedown` | 收人头不用等 | 触发 | 这个技能命中过的参赛者在助攻窗口内死亡、你得到击杀或助攻 → 本技能冷却刷新 | kill |
| K04 | `card_setup_line` | 先垫一句 | 触发 | 这个技能命中挂记号 4 s；你的其他技能命中有记号的人 +35%（乘区）并消耗记号 | skill_hit |
| K05 | `card_pin_amp` | 按住了就放大 | 触发 | 只装在有控制的技能上（含墙撞眩晕、末波定身等附带控制）：这个技能造成控制后 3 s 内，你的下一个技能伤害 +40%（乘区） | control |
| K06 | `card_encore` | 再来一遍 | 改造 | 这个技能释放后 0.6 s 在同一目标/方向自动再放一次，60% 效果，不耗劲；每多一份多一次 | skill_cast |
| K07 | `card_crowd_hit` | 一群人一起挨 | 改造 | 只装在飞行物或范围技能上：范围、飞行物半径与直线宽度 ×1.6，伤害不变 | — |
| K08 | `card_again_now` | 再来一下别喘 | 改造 | 冷却 −35%，劲耗 +50% | skill_cast, momentum |
| K09 | `card_big_one` | 攒够一顿狠的 | 改造 | 冷却 +50%，伤害 ×1.8（乘区） | skill_hit |
| K10 | `card_long_reach` | 站远点照样抽你 | 改造 | 只装在指向或飞行物技能上：射程 +50%；命中时离目标至少 550 cm 则伤害 +25%（乘区） | — |
| K11 | `card_hold_longer` | 按住多揍一会 | 改造 | 只装在有控制的技能上（含附带控制）：控制时长与附带控制时长 +40%（仍计递减），冷却 ×1.2 | control |
| K12 | `card_blood_price` | 拿命换 | 转换 | 这个技能改用生命支付（每 1 劲 = 0.25% 生命上限），不再耗劲；伤害 +20%（乘区）；生命不足时拒绝 | low_hp, heal |
| K13 | `card_lend_fist` | 拳头借给技能 | 转换 | 这个技能额外造成 60% 攻击力的物理伤害（并入原生直接命中） | skill_hit |
| K14 | `card_held_breath` | 憋着的气 | 转换 | 这个技能命中时消耗你身上护盾的 50%，变成同类型的等量伤害 | shield |
| K15 | `card_hurt_heal` | 打疼了我回血 | 转换 | 这个技能实际生命伤害的 25% 治疗自己 | heal, skill_hit |
| K16 | `card_held_shot` | 憋着一发狠的 | 成长 | 这个技能就绪后每闲置 1 s，下一次伤害 +15%（乘区），无上限，释放后清零；只算交战中的闲置，开打前最多带入 5 s | idle |
| K17 | `card_practice` | 越用越顺手 | 成长 | 这个技能每命中参赛者一次，本局伤害 +2%（乘区），无上限 | skill_hit |
| K18 | `card_combo_chain` | 一招接一招 | 成长 | 放这个技能时，过去 4 s 内你每放过一个其他技能，这次伤害 +20%（乘区） | skill_cast |
| K25 | `card_blast_reload` | 炸了接着放 | 元素 | 这个技能触发反应 → 本技能冷却 −3 s | reaction |
| D01 | `card_bump_reset` | 撞着就刷新 | 触发 | 冲刺接触参赛者 → 冲刺冷却刷新；接触野怪或画饼老板减剩余冷却的 50%（每次冲刺最多一次） | dash_contact |
| D02 | `card_bump_again` | 撞着了再撞 | 触发 | 冲刺接触敌人 → 所有技能冷却 −1.5 s | dash_contact, skill_cast |
| D03 | `card_dash_punch` | 蹿过去抡一拳 | 触发 | 冲刺结束 → 3 s 内下一次普攻 +100% 攻击力（并入主击） | dash, basic_hit |
| D04 | `card_dodge_strike` | 闪完就打 | 触发 | 完美闪避后，你的下一个技能不耗劲 | perfect_dodge, momentum |
| D05 | `card_double_dash` | 两段冲刺 | 改造 | 冲刺分两段，各 200 cm，第二段需在 1 s 内再按 D；冷却不变 | dash |
| D06 | `card_pass_through` | 穿人而过 | 改造 | 冲刺穿过敌人身体（不被阻挡），对每个穿过的人造成一次普攻（算普攻命中，不推进连击） | dash_contact, basic_hit |
| D07 | `card_head_down` | 闷头撞到底 | 改造 | 冲刺距离 +60%；撞到的敌人受 30 + 0.4 × 攻击力物理伤害并被撞开 150 cm / 0.12 s | dash_contact, knockback |
| D08 | `card_paid_dash` | 冲刺耗劲 | 转换 | 冲刺消耗 15 劲，冷却 ×0.4 | momentum, dash |
| D09 | `card_slippery` | 越躲越快 | 成长 | 每次完美闪避，本局移速 +1.5%，无上限 | perfect_dodge, move_speed |
| D10 | `card_ice_trail` | 一路结冰 | 元素 | 冲刺路径留冰面（宽 160 cm，5 s；每份一条） | ground |
| D11 | `card_move_turf` | 挪地盘 | 元素 | 冲刺结束 → 把你最近放下的地面状态、区域或陷阱挪到脚下（每个只挪一次；跟随你的区域不算） | ground |
| Y01 | `card_spiky_hide` | 打我也硌手 | 触发 | 受到敌方直接伤害（含被护盾吸收）→ 向出手者反弹 25%（魔法伤害，不暴击）；反弹不吸血；持续伤害与反弹不触发 | damaged |
| Y02 | `card_takedown` | 人头刷新 | 触发 | 击杀或助攻参赛者 → 刷新全部技能冷却，劲回满 | kill, momentum |
| Y03 | `card_hang_on` | 死扛 | 触发 | 生命低于 15% → 2 s 内受到的每次伤害降为 1（每条命一次） | low_hp |
| Y04 | `card_reaction_refund` | 反应回本 | 触发 | 触发反应 → 回 15 劲 | reaction, momentum |
| Y05 | `card_wave_after` | 一波接一波 | 触发 | 触发反应 → 反应结算后给目标挂一个随机的、与刚被消耗的不同的元素；该元素触发的二次反应豁免一次 0.5 s 反应锁 | reaction, element |
| Y06 | `card_shell_burst` | 壳碎了也崩你 | 触发 | 护盾被打破 → 300 cm 内的敌人受 90 + 40% 法强 + 6% 生命上限魔法伤害 | shield_break |
| Y07 | `card_hard_skin` | 挨揍起硬皮 | 触发 | 受到敌方正伤害后获得护盾 45 + 4%H，3 s；获得后 8 s 才能再获得 | damaged, shield |
| Y08 | `card_ball_kick` | 拿你当球踢 | 触发 | 你造成的击退撞上固定障碍 → 眩晕 0.8 s；撞上另一敌人 → 两人各受 P 60 + 0.5AD | knockback, wall_impact |
| Y09 | `card_skill_crit` | 技能也能暴击 | 改造 | 你的技能可以暴击（用你的暴击率另加 15%，倍率 2） | crit, skill_hit |
| Y10 | `card_skill_drain` | 吸血对技能生效 | 改造 | 你的吸血也按技能实际生命伤害生效 | heal, skill_hit |
| Y11 | `card_short_breath` | 气短 | 改造 | 劲到 35 就能爆劲，爆劲倍率 140%（多份取同一门槛，不叠加） | overdrive, momentum |
| Y12 | `card_double_stall` | 双摊位 | 改造 | 你的陷阱、风扇和技能放下的地面状态每份多同时留一份；D10 的冰带每份多留一倍时长 | ground |
| Y13 | `card_thick_hide` | 皮糙肉厚 | 转换 | 每 1 点护甲 → 生命上限 +6 | armor, max_hp |
| Y14 | `card_overfed` | 吃撑了 | 转换 | 溢出的治疗变成护盾（上限 30% 生命上限，5 s，与现有护盾取强） | heal, shield |
| Y15 | `card_berserk` | 残血狂暴 | 转换 | 每少 1% 生命，攻速 +1% | low_hp, attack_speed |
| Y16 | `card_quick_brain` | 脑子转得快 | 转换 | 技能急速每 10 点，你每次技能命中多回 1 劲 | momentum, skill_hit |
| Y17 | `card_punch_heal` | 揍人续命 | 转换 | 普攻吸血 15% | heal, basic_hit |
| Y18 | `card_run_hit` | 跑起来打 | 转换 | 移速每比基础高 10%，伤害 +5%（乘区） | move_speed |
| Y19 | `card_grow_fat` | 越吃越壮 | 成长 | 每回复 100 生命，本局生命上限 +18，无上限 | heal, max_hp |
| Y20 | `card_kill_streak` | 越杀越猛 | 成长 | 每次击杀参赛者本局攻击力 +4、法强 +6；每次助攻一半 | kill |
| Y21 | `card_tough_skin` | 越挨越硬 | 成长 | 每被控制一次，本局护甲 +3、魔抗 +3，无上限 | controlled, armor |
| Y22 | `card_charge_up` | 攒个大的 | 成长 | 不造成伤害的每 1 s，下一次伤害 +20%（乘区），无上限；造成伤害后清零（持续伤害不清零）；只算交战中的闲置，开打前最多带入 5 s | idle |
| Y23 | `card_turf_growth` | 地头蛇 | 成长 | 站在自己的地面状态上，每满 1 s 伤害 +3%（乘区），无上限；离开清零 | ground |
| Y24 | `card_tally` | 揍一下记一笔 | 成长 | 每次命中记一笔，满 5 笔兑现：每笔 12 + 0.1 × 法强魔法伤害；6 s 没有新账就清空 | skill_hit, basic_hit |
| Y25 | `card_live_wire` | 浑身带电 | 元素 | 安装时选一种元素（烫/湿/麻/冻/馊）；你身上常驻该元素的光环，200 cm 内对你造成直接伤害的人被挂上 | element, damaged |
| Y26 | `card_long_soak` | 久泡 | 元素 | 你施加的元素持续时间 ×2 | element |
| Y27 | `card_louder_blast` | 炸得更响 | 元素 | 你触发的反应伤害 +45%（乘区） | reaction |
| Y28 | `card_endless_burn` | 烧不完 | 元素 | 你施加的灼烧上限 5 → 10 层 | element |
| Y29 | `card_burn_guard` | 烧着还挺扛揍 | 元素 | 你造成的灼烧伤害的 30% 变成护盾，3 s，同源护盾余额封顶 30% 最大生命 | element, shield |
| U01 | `card_brain_speed` | 脑转手快 | 转化 | 每 40 法强提升 6% 攻速；每 12 技能急速使普攻附加 10 点魔法伤害 | attack_speed, basic_hit |
| U02 | `card_spellblade_echo` | 咒刃回响 | 触发 | 施放技能后 4s 内接下来 2 次普攻向前挥出 280cm 法术剑气（附 0.35×法强魔法伤害并携带宿主元素） | skill_cast, basic_hit, element |
| U03 | `card_heavy_mallet` | 千斤重锤 | 转化 | 攻速上限锁在 0.75 次/秒；溢出攻速按 140% 转化为普攻伤害与击退距离加成，且第 3 段普攻必定暴击 | basic_hit, combo3, crit, knockback |
| U04 | `card_let_bullets_fly` | 让子弹飞 | 改造 | 技能弹道与飞行普攻每飞行 100cm 伤害提高 8%，最高 +64% | range, skill_hit, basic_hit |
| U05 | `card_eagle_stance` | 鹰眼架枪 | 改造 | 站立不动满 0.8s 进入架枪态（移动后保留 0.35s）：普攻与指向/弹道/地面技能射程 +160cm | range, idle, basic_hit |
| U06 | `card_runaway_train` | 失控列车 | 转化 | 冲刺碰撞附加 45% 当前移速 + 30% 本次冲刺距离的物理伤害；冲刺撞墙返还 40% 剩余冲刺冷却 | dash, dash_contact, move_speed, wall_impact |
| U07 | `card_phantom_molt` | 幻影脱壳 | 触发 | 触发精准闪避时原地留下 1.0s 嘲讽野怪的残影，随后爆炸对 240cm 内造成 70 + 0.65 自适应伤害并减速 35%（1.2s） | perfect_dodge, dash, area |
| U08 | `card_skate_blade` | 冰刀割裂 | 触发 | 敌人在冰面上每位移或滑行 100cm 受 28 + 0.30 自适应物理伤害；自身站在冰面上攻速 +25%、移速 +20% | ground, element, move_speed, attack_speed |
| U09 | `card_tonnage_crush` | 吨位碾压 | 转化 | 第 3 段普攻与自身周围范围技能额外附加 3.5% 最大生命物理伤害（对野怪翻倍为 7%） | max_hp, combo3, area |
| U10 | `card_hulking_presence` | 大块头有威慑 | 转化 | 每拥有 800 额外最大生命，自身范围/光环/护盾爆破半径扩大 10%，受到的击退距离减少 12% | max_hp, area, knockback |
| U11 | `card_iron_hedgehog` | 铁刺猬 | 转化 | 反伤额外附加 (护甲+魔抗)×0.8 魔法伤害；处于嘲讽、顶着揍或招架期间反伤比例提高 50% | armor, damaged, control |
| U12 | `card_reactive_plating` | 反应装甲 | 触发 | 身上已有护盾时再获新护盾，向 260cm 内迸射 35% 新护盾量的物理破片；消耗护盾的技能获得 +25% 暴击率 | shield, area, crit |
| U13 | `card_desperado` | 亡命之徒 | 改造 | 生命低于 45% 时，每缺失 2% 最大生命提升 1.5% 造成伤害与受治疗效果 | low_hp, heal, damaged |
| U14 | `card_fuel_the_fire` | 火上浇油 | 触发 | 宿主技能直接命中满层灼烧或挂馊目标时，立即额外结算 1.2s 持续伤害且不消耗层数 | skill_hit, element |
| U15 | `card_rogue_works` | 违章工事 | 改造 | 占座书包额外继承 35% 最大生命；电风扇吹过自身火场/毒雾/水洼时刮出元素风暴，追加 45 + 0.35×法强魔法伤害并附着对应元素 | ground, max_hp, element |
| U16 | `card_martial_scholar` | 文武双全 | 触发 | 同时拥有 ≥40 额外攻击力与 ≥60 法强时：技能命中使 4s 内下次普攻偷取 8 劲并双倍破防；普攻命中使 4s 内下个技能伤害 +22% | basic_hit, skill_hit, momentum |

**元素勋章**（`form: medal`）：装进已学技能或本体的元素槽，不占工坊格子、没有数量上限；异色替换把原勋章（按其已融合枚数拆回）退回牌库，向已有**同色勋章**的槽位再次安装同色勋章则融合升阶（最高 `MEDAL.MaxTier = 3` 阶，卸下时按实际消耗枚数全额退回牌库）：
- **Ⅰ 阶**：宿主命中挂对应主元素（无元素技能打爆劲时同步读取该勋章元素）；
- **Ⅱ 阶**：该宿主触发的元素反应伤害提高 `+35%`（`MEDAL.Tier2ReactionBonusRatio`）；
- **Ⅲ 阶**：命中挂主元素后立即额外附着确定性异种副元素（烫→麻、湿→麻、麻→湿、冻→湿、馊→烫），单次命中自触发元素反应。

勋章不进牌三选一，也不上商店货架，只按下表几率掉落一枚（五种等概率）进牌库：

| 参数 | 值 | 来源 |
| --- | --- | --- |
| `MEDAL.LevelChance` | 0.08 | 每次不给牌三选一的升级（`CARDS.OfferEveryLevels` 不整除的等级） |
| `MEDAL.CampLowChance`、`CampHighChance`、`CampEliteChance`、`CampAncientChance` | 0、0.02、0.08、0.20 | 野怪最后一击，按营地档位 |
| `MEDAL.BossChance` | 1 | Boss 最后一击 |
| `MEDAL.EventChance` | 0.25 | 空投、外卖柜、小迷你旗、擂台、二手贩子的奖励，各自判定一次 |

| ID | 名称 | 效果 |
| --- | --- | --- |
| `medal_fire` | 烫勋章 | 命中挂 2 层灼烧 |
| `medal_water` | 湿勋章 | 命中挂湿 |
| `medal_electric` | 麻勋章 | 命中挂麻 |
| `medal_chill` | 冻勋章 | 命中挂冻 |
| `medal_rot` | 馊勋章 | 命中挂馊 |

**元素、反应与地面状态**（运行中，规格 §3.10、§8）：

主动原生元素：烫（烫你屁股、脚底开锅）、湿（滋你一脸）、麻（电你个哆嗦）、冻（冰可乐摇一摇）、
馊（过期外卖）；元素勋章让任意无元素技能（如蛮牛顶、疯狗乱拳、甩你一鞋等）或第 2 段起的普攻带上元素（见上方构筑牌目录）。每个目标同时只挂一种元素（灼烧层视为挂烫）；同元素刷新时长；
不同元素到达时按下表反应并消耗旧元素。过期外卖与冰可乐摇一摇命中推开 100cm（滋你一脸、蛮牛顶与扯嗓子喷你自带击退），近战普攻不推。

| 参数 | 值 | 作用 |
| --- | --- | --- |
| `COMBAT.ReactionBase`、`COMBAT.ReactionPerLevel`、`COMBAT.ReactionAPRatio` | 30、4、0.25 | 反应伤害 R = 30 + 4(L − 1) + 0.25 × 法强，魔法伤害，不乘 G；L 与法强取触发者（新元素的施加者） |
| `COMBAT.ReactionLockSec` | 0.5 s | 同一目标发生反应后这么久内不再发生反应；期间会引发反应的元素不挂上，同种元素照常刷新 |
| `COMBAT.WetSec`、`COMBAT.WetSlow` | 4 s、0.15 | 湿：移速 −15% |
| `COMBAT.ShockSec`、`COMBAT.ShockEveryNthHit`、`COMBAT.ShockRatio`、`COMBAT.ShockSplashRadiusCm` | 4 s、3、0.5、350 cm | 麻：持有者每第 3 次受到直接伤害，额外受 0.5R；施加者的其他敌人在 350cm 内时也受 0.5R。这份伤害不是直接伤害，不推进计数，不触发反弹与命中类牌 |
| `COMBAT.ChillSec`、`COMBAT.ChillAttackSlow`、`COMBAT.ChillMoveSlow` | 4 s、0.15、0.10 | 冻：攻速 −15%、移速 −10% |
| `COMBAT.RotSec`、`COMBAT.RotHealCut`、`COMBAT.RotPerSec`、`COMBAT.RotAPRatioPerSec`、`COMBAT.RotTickSec` | 4 s、0.25、3、0.03、0.5 s | 馊：受到的治疗与护盾 −25%；每 0.5 s 受 (3 + 0.03 × 施加者法强) × 0.5 魔法伤害，算施加者 |
| `COMBAT.PushSec` | 0.12 s | 电风扇每次吹与台风推动的时长 |
| `COMBAT.BurnSec`、`COMBAT.BurnTickSec`、`COMBAT.BurnMaxStacks` | 3 s、0.5 s、5 | 烫：每层独立计时（§4.1） |
| `OVERDRIVE.ChillRootSec` | 0.6 s | 冻的爆劲：定身 0.6s（计硬控递减） |

| 组合 | 反应 | 主目标 | 附带 | 参数 |
| --- | --- | --- | --- | --- |
| 烫 + 湿 | 起雾 | R ＋被消耗灼烧的剩余伤害立即结算 | 原地雾区 250cm / 3s，每秒给区内无元素敌人挂湿 | `COMBAT.FogRadiusCm`、`COMBAT.FogSec`、`COMBAT.FogIntervalSec` = 250 cm、3 s、1 s |
| 烫 + 麻 | 过载 | 1.5R，2 层破防 | 200cm 内其他敌人 R | `COMBAT.OverloadRatio`、`COMBAT.OverloadBreachStacks`、`COMBAT.OverloadRadiusCm` = 1.5、2、200 cm |
| 烫 + 冻 | 融化 | 2R | 脚下水洼 | `COMBAT.MeltRatio` = 2 |
| 烫 + 馊 | 沼气爆 | 1.5R，再挂 2 层灼烧 | 250cm 内其他敌人 R | `COMBAT.MethaneRatio`、`COMBAT.MethaneBurnStacks`、`COMBAT.MethaneRadiusCm` = 1.5、2、250 cm |
| 湿 + 麻 | 导电 | 1.5R，减速 30% 1.5s | 450cm 内其他湿的敌人 R（各自的湿被消耗） | `COMBAT.ConductRatio`、`COMBAT.ConductSlow`、`COMBAT.ConductSlowSec`、`COMBAT.ConductRadiusCm` = 1.5、0.3、1.5 s、450 cm |
| 湿 + 冻 | 冻住 | R，定身 1.2s（控制） | — | `COMBAT.FreezeRootSec` = 1.2 s |
| 湿 + 馊 | 发酵 | R，挂馊，持续 8s，削减 60% | — | `COMBAT.FermentRotSec`、`COMBAT.FermentHealCut` = 8 s、0.6 |
| 麻 + 冻 | 碎冰 | R；4s 内下一次受到的技能伤害 +50%（乘区，用掉即止） | — | `COMBAT.ShatterSkillDamage`、`COMBAT.ShatterSec` = 0.5、4 s |
| 麻 + 馊 | 腐蚀 | R，5 层破防，6s | — | `COMBAT.CorrodeBreachStacks`、`COMBAT.CorrodeBreachSec` = 5、6 s |
| 冻 + 馊 | 冻疮 | R，攻速 −40%，4s | — | `COMBAT.FrostbiteAttackSlow`、`COMBAT.FrostbiteSec` = 0.4、4 s |

- 反应伤害与控制归触发者；触发者获得 `MOMENTUM.Reaction`；控制类反应计入硬控递减。
- 日志 `LD_REACTION` 写 `kind`、`held`、`incoming`、`r`、`struck` 与 `ground`；麻的额外伤害写 `LD_SHOCK`。

**地面状态**：只伤主人的敌人（主人站在上面没有任何效果，Y23 的增益只算自己的）；多名参赛者的地面状态各自存在，不融合；
不改寻路、不挡视线、不产生碰撞。元素类命中（技能、牌、反应附带）的目标脚点在某地面状态内时，该状态按“再加元素”转换；
转换后的状态归新元素的施加者，被台风推动的状态保留主人。

| 状态 | 半径 / 时长 | 站在上面（每次结算） | 再加元素 | 参数 |
| --- | --- | --- | --- | --- |
| 水洼 | 180 cm / 6 s | 每 0.5 s 挂湿 | 麻 → 电场；冻 → 冰面；烫 → 起雾并消失 | `GROUND.Puddle*` |
| 冰面 | 180 cm / 5 s（D10 为宽 160 cm 的路径） | 起步与停下的加速度 ×0.5（惯性），被击退与冲刺的距离 ×1.5 | 烫 → 水洼 | `GROUND.Ice*`、`GROUND.IceAccelScale` = 0.5、`GROUND.IceDisplacementScale` = 1.5 |
| 火场 | 180 cm / 4 s | 每 1 s 挂 1 层灼烧 | 湿 → 熄灭并起雾 | `GROUND.Fire*`、`GROUND.FireBurnStacks` = 1 |
| 毒雾 | 200 cm / 4 s | 每 0.5 s 挂馊 | 烫 → 区内每人 1.5R（沼气爆）后消失 | `GROUND.Miasma*` |
| 电场 | 180 cm / 4 s | 每 0.5 s 受 0.3R 并挂麻 | — | `GROUND.Field*`、`GROUND.FieldRatio` = 0.3 |

- 性能保护：`GROUND.MaxPerOwner` = 6、`GROUND.MaxTotal` = 40，超出时最早的消失（不是玩法上限）。
- 地面状态是公开信息：在视野内时发布类型、形状、圆心、半径、剩余时间与主人座位色（[视野与界面](../systems/VISION_AND_UI.md)）。

#### 4.4.1 休息商店与连锁

规则见 [App Spec §4.5](../APP_SPEC.md#45-休息商店)。

| 参数 | 值 | 作用 |
| --- | --- | --- |
| `GROWTH.ActiveSkipPoints` | 1 | 跳过一组主动候选得到的属性点 |
| `SHOP.CardSlots`、`SHOP.VocabCardSlots`、`SHOP.VocabWeight` | 4、2、1.8 | 牌位数；其中按词汇加权的位数与共享词汇的权重倍率（其余位均匀） |
| `SHOP.SkillSlots` | 2 | 技能位数（既未学也非图纸的主动，均匀） |
| `SHOP.CardPrice`、`SHOP.SkillPrice` | 2（普通 2 / 稀有 3 / 传说 4）、2 | 牌按稀有度阶梯定价（2 / 3 / 4 属性点），技能为 2 属性点 |
| `SHOP.RefreshPrice`、`SHOP.RefreshStep` | 0、1 | 第一次刷新的价格（免费，打折日也免费）；本次休息里每刷一次的涨价，下次休息重置 |
| `SHOP.SkillPointPrice`、`SHOP.SkillPointMaxPerMatch` | 3、2 | 技能点价格（属性点）；每局最多买几个 |

### 4.5 关键词、状态与共用战斗规则

**关键词**（运行中；吸血的来源是 Y17 揍人续命）：

| 关键词 | 悬停解释（玩家可见） | 硬控（计入递减） | 打断喷漆（内部，不写进可读文本） | 属于“控制” |
| --- | --- | --- | --- | --- |
| 减速 | 移速降低一定比例；同时有多个减速时只取最强的。减速不是控制 | 否 | 否 | 否 |
| 定身 | 不能移动和冲刺，可以普攻和施法 | 是 | 是 | 是 |
| 眩晕 | 什么都不能做 | 是 | 是 | 是 |
| 沉默 | 不能施放技能，可以移动、普攻和冲刺 | 是 | 是 | 是 |
| 缴械 | 不能普攻，可以移动和施法 | 是 | 否 | 是 |
| 嘲讽 | 被迫攻击嘲讽者，不能施法 | 是 | 是 | 是 |
| 击退 | 被推开一段距离，期间不能行动 | 否 | 是 | 是 |
| 控制 | 处于定身、眩晕、沉默、缴械、嘲讽、击退中的任意一种。不含减速 | — | — | — |
| 破防 | 每层护甲和魔抗 −3，最多 5 层 | 否 | 否 | 否 |
| 灼烧 | 每层每秒造成魔法伤害，最多 5 层 | 否 | 否 | 否 |
| 潮湿 | 移速降低；遇烫起雾，遇麻导电，遇冻冻住（§4.4） | 否 | 否 | 否 |
| 麻（感电） | 每第三次受到直接伤害额外挨一下电，附近的人也挨（§4.4） | 否 | 否 | 否 |
| 冻 | 攻速和移速降低（§4.4） | 否 | 否 | 否 |
| 馊 | 受到的治疗和护盾减少，并持续受到少量伤害（§4.4） | 否 | 否 | 否 |
| 护盾 | 先于生命承受伤害；多源独立层叠加总量、各层独立计时，优先消耗最早到期层，每打空一层计一次打破 | — | 否 | — |
| 吸血 | 按实际造成的生命损失回复生命（不含护盾吸收与溢出）；多来源相加，不设上限 | — | — | — |

尺度词：“身边”= 以自己为圆心 300cm，只用于自身周围的范围效果；指向单位的技能一律写“目标”；“一段冲刺”= 350cm。

**沉默**：只禁止施放 Q/W/E；不禁止移动、普攻、D 冲刺和成长操作。前摇中被沉默则取消，不扣劲、不进冷却；
排队的技能轮到时按当时状态判定，被拒绝则跳过。沉默计入硬控递减，并在施加瞬间清零喷漆进度。
锅盖挡脸不能解除沉默。头顶显示“沉默”图标，技能栏变灰并显示锁。

**叠层（破防、灼烧）**：

| 项 | 破防 | 灼烧 |
| --- | --- | --- |
| 作用 | 每层护甲 −3、魔抗 −3（固定值，抗性最低降到 0） | 每层每秒魔法伤害按 §4.1 公式；每 0.5 秒结算一次 |
| 上限 / 持续 | 5 层（最多 −15 双抗）/ 4 秒 | 5 层 / 每层 3 秒 |
| 刷新 | 新的一层把整组时间重置为满值；到期整组一起消失 | 每层自施加起独立计时，新层不刷新旧层；每层到期各自消失 |
| 超过上限 | 停在 5 层，只刷新时间 | 停在 5 层，最早到期的一层换成新的一层（更新数值与归属） |
| 在目标身上 | 每个目标只有一份，所有来源共享 | 共享层数上限；每层记住施加者、数值与自己的计时，伤害与击杀归属按层计算 |
| 结算 | — | 不暴击，不触发吸血或命中效果；受伤害乘区影响 |
| 来源离局 | 层保留 | 该来源的层移除（`POLICY.SourceLeave`） |
| 野怪 | 同样生效 | 同样生效；回位时清空 |
| 显示与助攻 | 目标头顶“破防×3”，外圈显示剩余时间；施加者算助攻 | 同左（“灼烧×5”） |

**乘法伤害**（spec §3.7）：一次伤害的每个加成来源是一个乘区（同一张牌的多份先相加），乘区相乘为 `DamageFactorBp`，
乘积有技术上限 `DAMAGE.FactorTechCap`（×1000，触顶记 `LD_DAMAGE_FACTOR_CAPPED`）。技能不暴击（Y09 除外）；普攻暴击 ×2。
结算顺序：原始量 → 暴击 → × Π(1 + Σ同源加成) → 抗性（含破防）→ 护盾 → 生命。`LD_HIT` 写 `factor_bp` 与 `factors`。

**吸血**：所有吸血比例相加，没有上限。普攻吸血（Y17）按普攻主击实际掉血计算（不计合并强化普攻包），技能吸血（K15、Y10）按技能实际掉血计算，均不含护盾吸收与溢出（`POLICY.LifestealBasis`）。

**共用战斗规则**：

- **硬控递减**：定身、眩晕、沉默、缴械、嘲讽共用一个计数，跨所有来源共享。上一次硬控生效期间或结束后 2 秒内再中硬控，
  持续时间依次为 100%、60%、30%（第三次及以后），最短 0.2 秒（原持续更短时取原值）；连续 2 秒没有新硬控即清零。
  减速与击退不递减；未生效的控制不推进计数。
- **击退**：服务器权威的强制位移，沿地面以与冲刺相同的原生位移推开，遇阻停在最远合法位置，不传送；方向由施放者指向目标脚点。
  期间不能移动、普攻或施法，打断未出手的动作并中断当前移动/攻击订单；计入控制、喷漆打断与助攻，不参与硬控递减。
- **护盾**：采用 `StackToLatestExpiry` + 多源独立层栈（`ShieldLayers`）：每次授予把新量加到当前总护盾并独立记一层到期时刻，受击优先消耗最早到期层，每打空一层独立触发一次“打破”（到期或被技能主动消耗不算打破）。
- **限时增益**：同一来源重复施放刷新为新值与新期限，不叠两份；不同来源相加。
- **剩余冷却缩减**只把尚未结束的冷却结束时间提前（不早于当前时刻），不改基础冷却。

### 4.6 全员规则与地图事件

**人人脱战慢回**：每名参赛者（不含野怪）自上次战斗事件满 `REGEN.OutOfCombatDelaySec=5` 秒后，
每秒恢复 `REGEN.OutOfCombatMaxHPRatioPerSec=0.015` 倍当前生命上限（约 65 秒从零回满）；进入战斗立即停止。死体不回复，复活时刻是新生命的脱战起点。
按 `POLICY.RegenIntegration` 积分（`LDTactical::SettleRegen`），
导入器要求延迟为正、比例为正且是 0.001 的整数倍，使任意 P2 上限都能精确到每毫秒 1e-6 P2。

每名参赛者只有一个唯一战斗时间：有效 AttackStart、伤害技能 release、承受正 mitigated 伤害（屏障全吸收也算受击）都更新它。
失败尝试虽然已按用户规则解除保护，仍不算成功战斗事件；
纯移动/dash、自保盾、选择不刷新该脱战时钟。成功伤害技能释放即使后来打空也刷新。
副击对受害者造成正mitigated伤害仍刷新其受击时钟，不能被“禁追加proc”错误过滤。

**躺平角**：参赛者（不含野怪）脚点在躺平角圆形判定区 `DORM.RadiusCm=400` 内（边界算在内），且自上次战斗事件满
`DORM.OutOfCombatDelaySec=3` 秒后，每秒恢复 `DORM.MaxHPRatioPerSec=0.10` 倍当前生命上限（约 10 秒从零回满）；与上面的脱战慢回共用唯一战斗时间，各按自己的延迟起算并叠加
（3 秒后 10%/秒，5 秒后 11.5%/秒）。判定读取每个权威步结算时的脚点，离开或死亡后下一步停止。
地图的两处躺平角是东北、西南两个高台角上的 `dorm_ne` 与 `dorm_sw`（`SourceData/World/demo_world.json` 的
`Dorm` 锚点，技术标识仍为 Dorm）。世界生成器要求躺平角圆盘是合法站立地面，且不与占领圈、营地净空区和其他事件圆重叠；
数量在 1..8 之间，标识不再固定。同样要求比例是 0.001 的整数倍。数值是首轮试玩值，未经平衡验证。

**天降外卖（技术标识 airdrop）**：所有时刻都用野外进度 T（只在野外推进，休息暂停，不因休息补算或重发）。
第 n 份外卖在 T = `AIRDROP.LandSec[n]` 落地，表为 40、110、200、270、360、430 s，共 6 份、全房最多 6 份，之后不再有第七份；
提前 `AIRDROP.AnnounceLeadSec=15` 秒公开落点和倒计时。落地后第一个脚点进入
`AIRDROP.PickupRadiusCm=150` 圆内、且不在复活保护中的存活参赛者获得 `AIRDROP.RewardXP=200` 流量
加一次构筑奖励（上文“野外构筑奖励”：50% 主动图纸 / 50% 稀缺件），两者使用业务键事件 `airdrop` 与序号、只结算一次；
领取后还恢复最大生命的 20%（不超过上限）并获得 40 劲（`AIRDROP.Momentum`），移速加法池增加 20% 并持续 6 秒，后续领取刷新时长。
落地后 `AIRDROP.ExpireSec=30` 秒仍无人领取则消失，不发任何奖励。
同一拍内多人在圈内时离中心最近者得，仍并列时由服务器随机决定（不按参赛者编号）。下一次公开时当前外卖被取代。
地图有四处落点：西北、东南两座坡地的坡顶，以及东、西两路上的开阔处（`SourceData/World/demo_world.json` 的
`Airdrop` 锚点 `airdrop_e/nw/se/w`）。每份外卖由只在服务器的随机种子从上一份以外的落点中选一处，公告时才公开，所以后续落点无法提前推知。
公开视图只说“已被领走”，不公开领取者和领取时间；领取者从本人视图得知。世界生成器要求拾取圆是合法站立地面，且不与占领圈、
营地净空区和躺平角重叠；内容校验要求落地表严格递增、首次公告不早于 T=0、每份在下一份公告前消失，且最后一份在三段定时野外结束（T=480 s）前消失。恢复走统一 `HealBody`/`GrantMomentum`。

**上热搜**：从开局起每 `LEADER.RefreshSec=10` 秒刷新。仍在对局中、等级不低于 `LEADER.MinLevel=3` 的参赛者里
等级唯一最高者上热搜，等级相同按击杀数，仍相同则无人上热搜；此刻已死亡也不上热搜。公开圆半径 `LEADER.CircleRadiusCm=1500`，
圆心是刷新瞬间脚点加均匀随机偏移，最多 `LEADER.CircleMaxOffsetCm=1000`（内容校验要求不超过半径，上热搜者总在圈内）；
随机流只由对局身份和刷新序号决定。击杀上热搜者另得 `REWARD.BountyXP`；同一人连续当选上热搜且未死亡，每满 `LEADER.SurvivalPointSec=30` 秒得到 1 个属性点（在刷新时结算，换人或死亡后重新计时）。
上热搜者死亡结束本次上热搜。是首轮试玩值，未经平衡验证。

**画饼老板 Boss**：`content.json` 的 `boss` 块（`monster_dean`，显示名“画饼老板”）。野外进度 T=`appear_at=210` 秒
出现在 `boss_patrol_1`（原巡逻入口），整局仅一只，以 `move_speed=225` cm/s 沿 `boss_patrol_1..4` 循环巡逻；地图把四个巡逻点放在旗杆盆地内、
围着旗杆一圈（用户：“boss patrols around the flag pole in the center”），是 `SourceData/World/demo_world.json` 的
`BossWaypoint` 锚点。生命 `hp=2800`，护甲/魔抗 30，每 `attack_interval=1.5` 秒一次
`attack=210` 物理伤害（前摇 `attack_windup=0.5` 秒，与中野相同），攻击距离 `attack_range=250` cm，
碰撞半径与参赛者相同（40 cm）。目标：`attacker_memory=5` 秒内打过它的参赛者（取最近者），否则 `aggro_radius=400` cm
内最近的可见参赛者；距离相同按参赛者编号。每局一次，不复活；既有数值不因新时刻自动增加。T=`leave_at=480` 秒（第三段定时野外结束）
仍未被击杀则退场：没有死亡、击杀、经验或奖励，永远不进入最终野外；到那时仍未能出现也不再出现。最后一击的参赛者得 `reward_xp=320` 流量（走 `DeathReward` 结算），此时仍存活的再得一次构筑奖励（上文“野外构筑奖励”，业务键 `boss`/1）。内容校验要求碰撞半径等于参赛者、攻击距离覆盖两者半径、
索敌半径大于攻击距离、前摇短于攻击间隔、移速在移速上下限内、出现早于退场且退场不晚于最终野外。

**标记营地（技术标识 camp marker）**：前三个野外段（段 s=1..3）各在 T = (s−1)×`MATCH.FieldSegmentSec` +
`CAMPMARK.RevealSec=11` 秒（T=11/171/331）同时公开标记一处中野营地（h01–h08、h10、h12）和一处大野营地（e01–e04），
绑定营地本身的生命与清场。远古不在构筑标记候选池中。中野营地在本档营地中均匀抽取，不连续选上一段的同一处；大野营地优先在与该中野营地
相对的公开地图象限（以旗杆 `capture` 为中心，X、Y 符号都翻转）中、排除上一段大野营地均匀抽取，该象限没有合法候选时在全部大野营地中
排除上一段那处均匀抽取。抽取用只在服务器的随机种子（由外卖种子派生），公开后固定，不重开面板重抽。本段第一次完成该营地清理（使营地
清空的那次死亡）的击杀归属参赛者领取：中野营地一份主动图纸（业务键 `camp_marker_high`/段号），大野营地一组牌三选一
（`camp_marker_elite`/段号）；正常怪物经验照旧，不额外加经验；没有合格的参赛者归属时标记继续开放。标记时营地已清空则等它正常刷新，
不强刷；领奖后本段不再发；段结束（进入休息）未完成则机会消失。每类每段最多 1 份，共 3+3 份；最终野外不再生成构筑奖励目标，
全房地图构筑机会上限 6 空投 + 1 Boss + 6 营地标记 = 13 份。公开视图列出当前（或上一段）标记的段号、类别、营地、位置、状态
（已标记/已领走/已失效）与起止 T，不公开领取者。内容校验要求公开时刻落在段内、两类营地各至少两处。已由用户批准，未经平衡验证。

### 4.7 终局改装

`final_mods` 是独立于 24 个角色主动与 94 项 `cards` 的八项规则目录，八项全部普适：不绑定来源、不看构筑。R3（独立配装窗 `REST.FinalBuildSec = 20s`）每人从八项中无放回均匀抽三项（击杀画饼老板或赢得擂台多一项，最多一次），一局只选一项；揭示后固定，选择及其公开展示见 App Spec §2.1、规则见 §2.2。每项只改终局的胜负或旗杆规则，不造成伤害、不改构筑数值。终局棘轮（`SHOWDOWN.SprayRatchetSec = [1.15s, 2.3s]`、`SHOWDOWN.GuardRatchetSec = [7s, 14s]`）与终局基础击杀守护时间（`SHOWDOWN.KillGuardBonusSec = 4s`）统一由 `content.json` 参数化驱动。

| ID / 名称 | 参数 | 规则 |
| --- | --- | --- |
| `final_hit_and_run` 打一枪换个地方 | `sprays` 4，`fade` 4s | 累计成功喷漆 4 次即胜；其涂鸦不计守护，4s 后褪色、旗杆无主；须离开喷漆区后才能再喷；每次成功喷漆把 `fade` 计入历史守护（超时排名用） |
| `final_clear_out` 清场 | — | 旗杆解锁后，其他仍在局的参赛者同时处于死亡而持有者存活的那一刻立即获胜；复活保护者算活人 |
| `final_kill_seconds` 人头换秒 | `guard` 3s | 每击杀一名参赛者额外 +3s 历史守护；当时是涂鸦主人则额外 +3s 当前守护（叠加于终局基础击杀 +4s 守护之上） |
| `final_usurp` 篡位 | — | 击杀主旗杆涂鸦主人时，涂鸦归持有者并继承其当前守护进度 |
| `final_carry_flag` 扛旗跑路 | — | 持有者是涂鸦主人期间，主旗杆与喷漆区跟随其移动；封街圈圆心不跟随 |
| `final_cage` 铁笼 | `duration` 5s，`hp` 600 | 喷上涂鸦的瞬间在喷漆区边界升起 5s 围栏：只挡身体，不挡远程与范围技能；除持有者外的人隔着围栏打出的原始伤害（普攻、飞过的技能弹道、碰到围栏的范围）累计 600 时围栏提前碎掉 |
| `final_private_pole` 私人旗杆 | `spray` 3.5s，`guard` 28s，`radius` 280cm | 入场口旁只有持有者能喷的小旗杆，守满即胜；他人进圈冻结守护；不计喷漆次数，超时结算时按有效守点百分比（`max(主旗杆/20s, 私人旗杆/28s)`）参与比较 |
| `final_double_yellow` 双黄线 | `radius` 220cm | 持有者是主旗杆涂鸦主人期间，喷漆区半径为 220cm |

同一结算时刻多人达成时，守护满 > 喷漆次数满 > 清场，同一条件仍并列则无人获胜。超时裁决在存活、在局、非复活保护且有贡献的参赛者中依次比较**有效守点完成百分比 → 终局击杀数 → 终局伤害**，仍并列则无人获胜。

## 6. 经验、营地和发育供给

### 6.1 经验与击杀

`XP.Start=0`、`XP.MaxLevel=16` 是 Spec 固定值
（`content.json` 的 `progression.xp_start / max_level`，另有 `active_slots=3`、`active_offer_levels=[2,5,9]`）。
XP 保留为累计整数；没有“当前级经验归零后丢掉溢出”的第二账。

| 达到等级 L | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `XP.Threshold[L]` | 0 | 60 | 140 | 240 | 375 | 545 | 750 | 990 | 1265 | 1575 | 1920 | 2300 | 2715 | 3165 | 3650 | 4170 |
| 该级到下级 XP | 60 | 80 | 100 | 135 | 170 | 205 | 240 | 275 | 310 | 345 | 380 | 415 | 450 | 485 | 520 | 不再升级 |

阈值保存在 `progression.xp_thresholds`，并须与参数 `XP.BaseToNext=60`、`XP.IncreasePerLevel=20`、`XP.SteepFromLevel=5`、`XP.SteepExtraPerLevel=15` 一致。
比赛开始时（成长 `BeginMatch`，即 Progression 提供者 Prepare，早于发育期开放）每名参赛者（真人与 AI）得到 `GROWTH.OpeningPoints`
与 1 号主动机会，并固定其首组开局候选；之后每达到一级得到 `GROWTH.PointsPerLevel`，达到 `progression.active_offer_levels` 的等级
各加一次主动机会；复活不再授予，新局重建。真人的开局面板在本局首次显示该候选时自动打开一次，不代选。
抽取、排除与学习规则见 §4.3。
DEV 成长夹具可预置主动候选，不把 DEV 预置当作随机分布证据。
升十六级以后继续记录合法奖励的累计XP，不再升级或授予主动机会，按 §4.1 每满 `GROWTH.MaxLevelPointsXP` 给 `GROWTH.MaxLevelPoints` 点。
累计存储有界int64，溢出在整个奖励计划提交前明确拒绝，不静默饱和；UI十六级显示MAX而非暗示17级。
野怪奖励全部归最后一击，表中“每人平均”只能用于供给估计，不是实现经验分摊。
参赛者之间的击杀按 App Spec §4.1 的助攻规则（参考 LoL）在同一笔死亡奖励中结算：
击杀经验先按 `REWARD.PlayerXP` 取受害者等级的基础值，再乘等级差系数（`REWARD.LevelGapXP`，下表）。击杀者拿全额折后经验（本野外段前 2 次击杀各得一组牌三选一，之后改为 +2 属性点），每名助攻者另得其 `REWARD.AssistXPRatio`（向下取整）与 1 个属性点，不稀释击杀者。
击杀时受害者正“上热搜”的，击杀者再得 `REWARD.BountyXP`、3 个属性点，并得到一组 `REWARD.BountyCardOffer` 张的稀有/传说三选一。
终局之外每次击杀参赛者，击杀者按 `REWARD.KillMedalChance` 掉一枚元素石。
例：Lv1 击杀者与两名助攻者击杀 Lv1，击杀者得 70 XP，每名助攻者各得 35 XP 与 1 个属性点。资格窗口为 `REWARD.AssistWindowSec`。

| 奖励参数/公式 ID | 已批准值，整数 | 样例与归属 |
| --- | --- | --- |
| `REWARD.PlayerXP`（`.Base=70`、`.PerLevel=20`） | $70+20(L_{victim}-1)$ | 同级击杀Lv1得70XP、Lv10得250XP；以致死瞬间受害者等级快照 |
| `REWARD.LevelGapXPStep` / `LevelGapXPMin` / `LevelGapXPMax` | 0.12 / 0.4 / 1.6 | 等级差系数 = $1-0.12(L_{killer}-L_{victim})$，夹在 0.4–1.6，乘基础值后向下取整；击杀者等级取结算时的当前等级。12 级杀 5 级得 60XP，5 级杀 12 级得 464XP；助攻者按折后经验取比例，悬赏经验不乘 |
| `REWARD.AssistXPRatio` | 0.5 | 参赛者击杀中每名助攻者得到的击杀经验比例（另固定 +1 属性点） |
| `REWARD.BountyXP` | 150 | 击杀正“上热搜”的参赛者时击杀者额外得到的经验（另得 3 属性点） |
| `REWARD.BountyCardOffer` | 3 | 悬赏击杀另给的稀有/传说选牌张数（选一张） |
| `REWARD.KillMedalChance` | .25 | 击杀参赛者掉元素石的概率 |
| `REWARD.AssistWindowSec` | 15s | 受害者死亡前15s内（含）有过正伤害（含被护盾吸收）或任一控制/减益的其他在局参赛者算助攻，每次新贡献重新计时；死亡界面的承伤列表用同一窗口 |
| `CAMP.Low.RewardXP`（怪物 `reward_xp`） | 每只11XP | 小野整组33XP |
| `CAMP.High.RewardXP` | 每只32XP | 中野两只，整组64XP |
| `CAMP.Elite.RewardXP` | 每只145XP | 大野整组145XP；1级只有普攻的角色单挑打不过 |
| `CAMP.Ancient.RewardXP` | 每只300XP | 远古整组300XP |

以上经验是 0 阶的基础值；每条野怪生命按生成时的成长阶段多给经验（§6.2.1）。

不保留旧 PvP 配对冷却、总XP上限、固定击杀次数池。合法新生命可继续产生奖励；
同一生命重复致死/回包不能产生第二份。人为送人头仍是自由混战的行为风险，不能未经产品授权
添加死亡惩罚或无提示的递减奖励来修正。断线不是击杀，不产生本表奖励。

### 6.2 四档营地与种类池

34处语义营地固定档位：18小野、10中野、4大野、2远古。`Low/High/Elite/Ancient` 是四档的技术标识，
`Boss` 是独立厨师事件。`camps.placements.monster_id` 引用一个固定的档位代表定义，用它的 `tier` 关联种类池，
不指定本次活着的种类。h09、h11属于远古；其余营地锚点与周围人工布局保留。

服务器在首次生成和每次整组刷新前，在本档三种中独立等概率选一种，允许连续重复；整组不混编。
一次生成准备期间的选种固定，落点被占而重试不能重抽；成功后整组生命固定种类，只有全部死亡后的下一次刷新才再抽。
回位、休息、重连和观察不触发重抽。公开营地记录仅含固定位置和档位代表；当前种类只随合法可见身体发布。
同档共用成员数量、整营经验、刷新时间和成长曲线，并通过行为区分打法。

| 字段 / 单位 | 小野 Low，每组3只 | 中野 High，每组2只 | 大野 Elite，每组1只 | 远古 Ancient，每组1只 |
| --- | --- | --- | --- | --- |
| 每只HP | 90 | 315 | 1350 | 2100 |
| 每只攻击 | 14 | 50 | 126 | 182 |
| 护甲 / 魔抗 | 8 / 8 | 20 / 20 | 30 / 30 | 40 / 40 |
| 攻速，次/s | .80 | .65 | .60 | .55 |
| 普攻前摇，s | .35 | .50 | .50 | .50 |
| 普攻射程，cm | 150 | 175 | 175 | 175 |
| 移速，cm/s | 240 | 260 | 260 | 260 |
| 碰撞半径，cm | 40 | 40 | 40 | 40 |
| 拴绳 / 感知半径，cm | 900（HR）、1050（地推、骑手） | 1050 | 1050 | 1050 |
| 失去目标宽限，s | 3 | 4 | 4 | 4 |
| 清空后刷新，s | 40 | 75 | 90 | 120 |
| 每只 / 整营基础XP | 11 / 33 | 32 / 64 | 145 / 145 | 300 / 300 |

每只仍独立life和最后一击奖励，不再次发整营经验。三十四处营地的0阶首轮总供给为2876XP，
不等于任何人的可保证收益；技能、竞争、刷新与随时间成长还会改变实际发育速度。
本表是试玩输入，未经平衡验收。基础回复、AP和暴击率均为0；野怪没有劲，技能不耗劲。
未受击不主动；确认的合法命中触发同营反击，每个成员仍须独立合法感知目标，不共享隐藏坐标。

| 档位 | 种类 / 稳定ID | 能力与应对 |
| --- | --- | --- |
| 小野 | HR `monster_scavenger` | 同组同伴真实死亡后攻击+25%，不叠加；出生阶段≥3时为+50%。回位清除，旧死亡不重触发 |
| 小野 | 地推 `monster_promoter` | 传单轰炸：锁方向扇形，侧步躲避 |
| 小野 | 骑手 `monster_courier` | 催单冲刺：锁方向直线短冲400cm，受真实阻挡与营地拴绳限制 |
| 中野 | 私教 `monster_guardian` | 强制加练：锁方向扇形重击，绕后躲避 |
| 中野 | 中介 `monster_agent` | 看房合同：直线非追踪投射物，速度900cm/s，受真实阻挡 |
| 中野 | 保安 `monster_guard` | 禁止通行：正面减伤50%持续1s，并释放扇形反击；可以绕后攻击 |
| 大野 | 导师 `monster_advisor` | 限时交稿：锁定施法开始时合法目标脚点的预警圆 |
| 大野 | 审计 `monster_auditor` | 突击抽查：沿锁定方向分三次扫线 |
| 大野 | 甲方 `monster_client` | 再来一版：同一锁定圆形落点分两次结算 |
| 远古 | 总监 `monster_director` | 末位淘汰：三方向分段重击；HP≤50%时攻速+20%，通过现有临时属性投影生效，离开阈值失效 |
| 远古 | 终审甲方 `monster_final_client` | 最终终审：两处依次生效的预警圆，第二圆沿初始方向偏移250cm |
| 远古 | 物业主任 `monster_property` | 本区禁行：环绕自身分段扫击，观察安全扇区绕行 |

HR以外，每第三次已释放攻击以技能替代普攻，不额外叠加原普攻。普攻/技能共用原有动作与请求序列，
只有成功释放推进循环；回位清循环、被动和技能准备。预警开始锁定方向或脚点，不在释放时追踪玩家。
预警期间取消、死亡、被禁止当前动作的控制命中或开始回位，未释放动作不能继续；已释放独立效果沿现有生命周期结算。
休息清场时取消战斗效果，身体保留剩余资源；休息不重抽种、不生成奖励。

`skill`为每种的完整运行参数，不在代码补产品默认值：小野/中野预警.8s，大野/远古1s，收势.6s。
单段1.5AD；双段各1AD，间隔.8s；三段各.75AD，间隔.5s，基础整次最多2.25AD。
扇形完整夹角60°；直线半宽80cm，圆半径180cm。骑手范围400cm，其余小/中野600cm，大/远古800cm。
三方向扫线每段转30°；总监每段转60°、物业每段转120°；多段围绕初始方向对称排列。
导师、甲方和终审甲方造成魔法伤害，其余技能为物理伤害；都读取该生命成长后的AD，不假设怪物有AP。
出生阶段≥3时中野技能命中附20%减速1s；出生阶段≥6时大野/远古在末段.8s后于同一末段形状追加.5AD。
整个生命固定出生阶段，跨阶段或回位不会进化；时间成长参数见§6.2.1。

同档整营XP相同；固定同级、同构筑、同阶段比较清营耗时与净损血，按该档中位数±15%作为调参目标。
须覆盖裸普攻、近战/远程技能、成长阶段及真人躲招；目录属性相同不能证明难度已达成此目标。
所有初始小野种类都须能由一级裸普攻开始发育，不能让随机结果出现无解开局。
表现复用现有HR、私教、导师的人物模型；种类通过名字、配色、技能名与攻击形状区分，
远古尚无独立模型。现有语义home点仍为必需依赖，删除必需点会明确拒绝，不能宣称已支持任意删除。

共同政策键 `CAMP.ReturningDamageable=true`、`CAMP.ReturningControllable=true`。
回位一旦开始，不攻击、不新开目标、不因为受击取消返回，但仍可被合法伤害/控制，也可能被击杀；
Root 只会暂时禁止运动，不把返回订单变成追击。沿合法路径到达自己的成员锚点后，
先处理该时刻已到期命中/死亡，仍活着的成员才满已定义HP、清仇恨/临时状态并使自身技能就绪，
再次等待合法受击。这是幂等CampHomeReset，不调用玩家普通同比投影、不刷新任何玩家IssuedCooldown。
怪物技能不耗劲，`CAMP.MomentumPolicy=NotApplicable`；野怪没有劲。
**已经死掉的组员不会因存活成员回位而复活**。
回位不发奖、不换 life、不新建奖励预算；若回位无合法路径，明确报告布局/导航错误，
不能静默传送、生成新生命或发奖掩盖问题。
回位开始取消未释放的怪物动作；到家后只重置这个存活怪物的战斗准备，不取消或改写玩家已付费来源。
旧受击/仇恨通知必须按事件身份与reset时序去重，不能让到家前的迟到通知重新启动追击。

共同参数 `CAMP.ResetArrivalRadiusCm=100`（Cm，已批准）以真实脚点到本成员静态MemberHome的中心距判定，
同时要求同层合法capsule，不再分Low/High配置。40cm怪+40cm玩家的最小分离80cm再留20cm，
避免单个玩家占home时怪永远停在80cm外、进不了过小归位区而只能挨打。
成员home通过同一地面、导航与胶囊预约入口检查，不能只凭作者坐标认定可放置。
导航agent-overlap不能再加40变成140cm成功判定，
也不强行snap到被占的home中心；home坐标不随reset漂移。
两个归位区可以重叠，实体仍遵守动态防重叠；多人围堵及原生实际接近尚须运行验收。

**回位无路收尾**：回位中的成员连续 `CAMP.ReturnDeadEndPlans=3` 次规划被拒（每次被拒后等 2 s 再规划），
或可以移动却累计 `CAMP.ReturnDeadEndSec=6` 秒没有沿路径前进（受控制期间暂停计时，已累计的时间保留），就在原地重置：
满已定义生命、清临时效果与仇恨、技能就绪，不发奖励、不增加生命，记一次 `LD_CAMP_RETURN_DEAD_END`。
不传送回家；营地活动范围仍按营地锚点计算，只约束交战中的成员。原地重置后、在活动范围外的成员坚守原地：
受击后就地反击（目标在攻击距离内才出手，不追击），不因活动范围返回；失去目标且宽限期结束后才再次尝试回家，
回到家中重置后恢复正常规则。是已批准的首轮值，未经试玩验证。

例如一组小野先死一只，只发这只的XP；其他存活成员回位只回血，奖励仍未发、原 life 不变。
之后它在t=10s死亡，整组从t=10开始25s等待，于t=35重新抽种并生成三条全新生命。
旧弹体/命中持有旧 target life，不能伤害这次刷新。

### 6.2.1 随对局时间成长

每组野怪生成时（开局整组生成与每次清空后刷新）取成长阶段
$k=\min(\text{CAMP.GrowthMaxStage},\lfloor T_{生成}/\text{CAMP.GrowthStageSec}\rfloor)$，$T_{生成}$ 是生成时的野外进度 T（只在野外推进、休息中冻结），开局生成时为 0；
整组同阶，整条生命保持该阶段，回位重置（含原地重置）恢复到该阶段的满值，不退回基础值；只有下一次刷新才取新的阶段。
画饼老板不是营地，固定 0 阶。

| 参数 ID（`content.json`） | 值 | 作用（各自按存储单位向下取整） |
| --- | --- | --- |
| `CAMP.GrowthStageSec` | 43 s | 每满 43 s 升一阶 |
| `CAMP.GrowthMaxStage` | 10 | T=430 s 起达到 10 阶封顶（三段定时野外合计 T=480 s） |
| `CAMP.GrowthHPRatioPerStage` | .12 | 生命 = 基础 × (1 + .12k)，上限时 2.2 倍 |
| `CAMP.GrowthAttackRatioPerStage` | .08 | 攻击 = 基础 × (1 + .08k)，上限时 1.8 倍 |
| `CAMP.GrowthResistPerStage` | 2 点 | 护甲、魔抗各 + 2k，上限时 +20 |
| `CAMP.GrowthRewardXPRatioPerStage` | .10 | 每只经验 = 基础 × (1 + .1k)，上限时 2 倍 |
| `CAMP.ClearHealMissingRatio` / `CAMP.ClearMomentum` | .12 / 25 | 打完一整营时在最后一击落点掉一个回复球：走进 90 cm 拾取半径的存活非保护参赛者回复**已损生命**的 12% 并得到 25 劲 |
| `CAMP.ClearOrbSec` | 12 s | 回复球无人拾取时的存在时间 |

攻速、前摇、射程、移速、拴绳、刷新时间都不随阶段变化。对手 AI 估计营地代价时按当前公开时间的阶段计算
（活着的野怪不会高于这个阶段），大野与远古营地沿用中野的入场与保底血量门槛。

### 6.3 AI 只读决策参数

| ID | 建议值/单位 | 使用边界 |
| --- | --- | --- |
| `AI.RetreatHPRatio` | .25 / 比例 | 低血量时优先撤退/恢复，不直接改 HP 或传送 |
| `AI.ResumeFarmHPRatio` | .45 / 比例 | 恢复到此再评估营地 |
| `AI.LeaderHuntMaxLevelGap` | 2 / 等级数 | 上热搜者公开等级高出超过 2 级时不去追 |
| `AI.HighCampEntryHPRatio` / `HighCampReserveHPRatio` / `LowCampReserveHPRatio` | .80 / .35 / .15 | 入场与保守站桩预计余血门槛，不读隐藏怪物血量 |
| `AI.CaptureReactionBudgetSec` | 2 / 秒 | 对公开喷漆/守护进度的反应预算 |
| `AI.DecisionIntervalSec` | .25 / 秒 | 常规决策节拍；死亡/失视野立即停止非法行为 |
| `AI.RetryPolicy.MaxAttempts` / `InitialBackoffSec` / `MaxBackoffSec` | 3 / .25 s / .50 s | 可重试传输/路径/服务失败的退避上限 |
| `AI.ExploreRevisitIntervalSec` / `EngagementBudgetSec` | 20 s / 40 s | 静态兴趣点重访间隔与单目标持续投入上限 |

### 8.1 性能预算

帧预算是开发机（RTX 3060 Ti，1920×1080，不锁帧、关 VSync）上的打包版验收线：
低画质（Scalability 0）满员人群下 ≥ 120 FPS（帧 ≤ 8.33 ms），高画质（Scalability 2）满员人群下 ≥ 60 FPS（帧 ≤ 16.67 ms）。测量入口为 `RunPerfBudget.ps1 -Match`（[试玩问题排查](../DEVELOPMENT.md#试玩问题排查)）。

## 10. 数据来源与提交契约

- **唯一作者源与管线**：`SourceData\Demo\content.json`、`content.schema.json`、`native_ids.json` 经 `Scripts\DemoContent.py`（`parse_float=Decimal` 严格有限与定点校验）与 `Scripts\SyncDemoContentData.py` 生成 `Content\Lockdown\Data\Demo\demo_content.json`、`manifest.json` 与 `Source\Lockdown\Private\Development\LDDemoContentContract.h`，再由 `BootstrapAssets.ps1 -Mode RefreshDefinitions` 写入并校验 `DA_DemoContent`（`ULDDemoContentCatalog`）。
- **目录根摘要**：`CatalogDigest` 唯一取 `manifest.json` 规范字节的 SHA-256（线格式 32 字节），一局内固定为不可变 `FLDDemoContentSnapshot`；原生定义 ID（`LDDemoContentIds.h`）只追加不重排。
- **显示字符串与图标**：所有内容显示元数据统一登记于 `content.json` 的 `display`（`DemoContent` 字符串表 `<id>.name` / `<id>.description`，占位符由类型化字段实时求值）与 `SourceData\UI\ui_icons.json`（`icon_<id>` 128×128 PNG）；公开临时状态另含 `root`、`shield`、`camp_returning`、`respawn_protection`。
- **核心数值与提交不变量**：能力身份、绝对 `ReadyAt` 冷却债务、同比最大生命缩放（`floor(oldCurrent * newMax / oldMax)`）、亚 P2 回复余数积分与唯一 `DeathKey` 奖励原子性由[战斗与成长](../systems/COMBAT_AND_GROWTH.md)、[成长事务](../systems/GROWTH_TRANSACTIONS.md)及[共享契约](SHARED_CONTRACTS.md)执行。

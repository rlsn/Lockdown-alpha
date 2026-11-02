# 战斗、效果与成长

本文说明可复用的原生结算链。Demo 的属性、三选一、两个主动槽和死亡保留规则来自
[App Spec](../APP_SPEC.md)，不能靠修改文档或目录标签宣称已经实现。

## 当前组件

| 入口 | 责任 |
| --- | --- |
| `Combat\LDCombatDirector` | 唯一 Bodies/P2 资源记录与按实体保留的 Combat 记录（构筑投影、来源冷却轨道、普攻节拍、Direct 请求窗口）；真人、对手 AI 与营地成员共用的动作入口；对外只读身体状态 |
| `Combat\LDCombatDriver.cpp` | 唯一权威 driver：按真实到期点切步，按契约顺序执行各阶段，步后发布视野与本人 `CombatView` |
| `Combat\LDCombatActions/Effects/Sources/Life.cpp` | 准入/释放/回执；命中、投射物、状态到期、回复与致死锁存；构筑安装、来源绑定与可用性读模型；身体准备/安装/退役 |
| `Combat\LDTacticalRules` | `EvaluateDamage` / `EvaluateBasicTiming` / `CooldownDurationMs`，以及 Root/屏障/净化/定点回复纯规则；不提交资源或产生效果 |
| `Combat\LDAttributeProjection` | 已批准构筑输入的属性与贡献投影、一次最大值变化的精确资源比例 |
| `Framework\LDGameModeLifecycle` | 唯一生命/身体绑定、借用许可、当前时间、原生运动前缀与只读发布 |
| `Framework\LDGameModeLife.cpp` | 阶段顺序与许可分派、生命结束/十点复活/保护、出生落地与营地整组生命提交 |
| `World\LDCampDecision` | 仅参数化营地决策；不读 Actor、不提交移动/攻击/奖励 |
| `World\LDCampLifecycle`、`World\LDCampDirector` | 营地名册与每局记账；服务器营地 Director 经 Combat 驱动阶段入口生成野怪、推进运动、提交普攻、消费受击/死亡事实并做回营重置与整组刷新，奖励只由成长 sink 支付 |
| `Development\LDCharacterBuild` | 当前构筑纯值与候选投影；不是已接通的三选一服务 |
| `Development\LDV08Loadout`、`LDV08Modifiers` | 来源绑定、装备派生与修正器 |

这些入口位于 `Source\Lockdown\Public`/`Private`。Demo 运行输入沿
`SourceData\Demo`、`SyncDemoWorldData` / `SyncDemoContentData` 和原生不可变目录生成链读取。
保留纯模型尚需的旧 Catalog 只服务其明确依赖，不作为 Demo 的默认属性、技能或物品授予。
旧职业任务、暗杀、烟雾、元素地表、内容请求协议与对应执行器已随闭包删除。

## 从意图到结果

UI 只能准备或发送动作，不直接扣蓝、改 HP、增加效果或确认命中。
服务器验证 match/life、序列、真实动作来源、当前绑定、资源/冷却、状态与目标几何。
单位目标必须重新解析观察句柄，不能依赖预览时的旧权限或隐藏 Actor 指针。

预览、接受、释放、命中和结束是不同阶段。正式资源消耗与实际释放必须遵循同一提交边界；
失败要明确，不让客户端通过同时点击技能图标和快捷键产生两次效果。
已释放的效果是否继续存在由其生命周期决定，不通过销毁 UI 或更换槽位退款。

普攻、Q/W 与装备 1/2 经同一 `SubmitAction` 入口（真人 RPC、对手 AI 与营地成员各自的服务器 Caller）：
本人/形状/请求窗口先验，进攻意图随即经生命域解除保护，再验来源、冷却、蓝量、控制、目标与几何；
未准入不付费，已解除的保护不恢复。释放时重验来源与目标，成功才扣蓝并按 `FORMULA.CooldownMs`
写入该来源的持久冷却轨道（每个来源独立，失源、换槽、死亡都不刷新）。
已接受但未释放的 Direct 动作因死亡、取消、打断或营地回位而结束时，回执落为 Rejected 并带原因。
Root/屏障/Root 净化/定点回复的规则从 d70211a 的 `LDTactical` 抽取为纯函数：
Root 按效果实例保存、同实例不续期、禁止 `POLICY.RootBlocks` 所列动作；屏障为 ReplaceIfNotWeaker，
不继承旧上限或严格大于判定；回复按 CarrySubP2Remainder 积分。
旧 `LDTactical::Allows/FState` 及未选状态不能作为新 Demo 的兼容入口。

## 权威时序

`AdvanceAuthority(const FLDMatchContext&, int64 ThroughMs, FString&)` 在 `Combat\LDCombatDriver.cpp`
中逐步推进：步终点取可用时间、原生最大步长和最早的真实到期点（释放、状态到期、冷却、
普攻节拍、脱战回复起点、保护到期、复活时刻、开圈与独占完成时刻）。每步依次执行 Motion → Session
（保护半开到期）→ Actions（先结算到本边界的回复）→ Effects → DeathsAndRewards → RespawnsAndWorld →
Transactions → Capture → Close 和完成边界，仅推进原生游戏时间已经提供的预算；
Close 锁定结果后仍完成该边界，随后无 permit 的发布依次调用成长 `PublishGrowth` 与 `ExecuteCapturePublish`，然后停止推进。
DeathsAndRewards 先结束本集合全部生命，再逐个调用成长 `CommitDeathReward`：可重试的拒绝只在下一边界再试一次，
其余未支付结果明确报错并故障；结果锁定时仍待重试的奖励记录为未支付。Transactions 每步调用一次 `ProcessTransactions`。
生命变化（Joined/Respawned/ProtectionEnded/Died）在各自阶段向占领归约器提交资格事实并通知提供者。
原生 CMC 每身体每步只执行一次；Owner 显示时钟只复制完成步，视野在本次 driver 推进完成后采当前姿态并发布。
`PublishAuthority` 失败返回同一 driver，由 Core 进入 Fault，而不是保留一局失去视野服务的正常比赛。
旧无参/秒数世界推进、`Sample`、旧回拨 `UWorld::TimeSeconds` 和旧目标采样分支已删除。
营地成员在同一分派中接入：Motion 调 `AdvanceMemberMotion`；Actions 结算回复后先调 `ProcessAuthorityIntents`，
本步野怪请求随即与 Direct 请求一同准入；Effects 把营地成员受到的已提交敌对命中（伤害或控制）作为 `OnCombatHostileContact` 交给营地；
DeathsAndRewards 依次 `EndCampMemberLife` → `ConsumeMemberDeath` → `CommitDeathReward`；RespawnsAndWorld
在参赛者复活后调 `AdvanceCamps`。初始出生在同一开局许可内、参赛者 Joined 之后调用 `SpawnInitialCamps`。
任一营地入口返回 false 即为权威故障。

修改这个顺序会影响同时死亡、资源释放、命中与占领完成的同刻竞争，必须连同对应场景验证。
争圈的归约与结果归 GameMode 核心的占领归约器（`Framework\LDCaptureReducer`），不能沿用已退役的 Heart 授权或旧交互中断条件。

## 状态、伤害和效果归属

HP/MP/盾只在 `Combat.Bodies.Resources` 中以 P2 整数保存，Owner 的点数用于显示。
伤害求值先完整验证定义、来源值、抗性及算术，再处理保护、盾吸收、实际 HP 损失和过量伤害；
只有 `HPLostP2` 是实际生命损失，不能把盾吸收也当作吸血依据。求值不读 Actor、时钟或随机数，
也不代替拥有 permit 的命中/死亡提交。
`ReadObservedPublicFacts` 只读当前已绑定身体上明确提交的公开构筑/状态投影。
复活保护仅由 Alive/current Body 且 Core NowMs 小于 Lives.ProtectedUntilMs 派生
`RespawnProtection` 的单层状态及精确期限，不存成可驱散的 Combat buff，不在读取时补默认等级；
命中点以同一生命状态做统一保护闸（免伤免控，群攻副目标排除保护者）。
公开 Root/Shield 提示同样在读取时由当前 Combat 状态派生，不另存一份可漂移的公开状态。
驱散不等于回滚伤害，也不等于永久禁止持续来源重新施加效果。
护盾、普通抗性和条件修正应走统一伤害路径，不能由某个装备在末尾再加一层未经约束的减伤。

持续伤害按原计划的 tick 和受害者 life 去重；晚进入区域不重新领取整段总伤害。
释放者、资源预算所有者、当前伤害归属和受害者生命是不同概念，
反射、死亡或替换来源不能重建免费预算或让旧投射物击中新 life。

付费施法必须先保证分配与来源合法，提交时一致消耗；
不得出现已扣资源但静默没有效果，或多个来源对同一额度重复返还。
表现层不得成为第二个伤害、碰撞或效果计时权威。

## 构筑与来源

`FLDCharacterBuild` 当前仍有六维属性、Ranks、Slots/Actions、Cooldowns、Revision 等字段，
并受所选规则格式约束。这不是新 Demo 的 MOBA 属性与三选一模型。

`FLDBuildPlan` 携带预期构筑与库存版本；`Prepare`、`LDV08::Project` 仍保留纯投影与候选校验，
不能把客户端草稿或失败的候选结果直接复制成真实构筑。旧站点准备、读书和 Loadout hold
不再有运行入口；N04 的机会/候选/确认、拒旧与回执必须按同一核心许可实际接入。
学习、动作引用、物理装备绑定和物品实例分别管理；失去物品时，
`InvalidateSources` 必须让依赖该来源的动作失效，不能留下悬空热键。

等级、技能学习和 cooldown 是参赛状态，不应随 UI 或 Pawn 重建而免费重置。
后续实现 Demo 成长时优先复用身份、请求、来源与回执边界，
但需要明确改造当前构筑字段、选项状态和死亡策略，而不是继续继承旧职业/书籍规则。

## 检查入口

`Lockdown.Rules.DemoCombatEvaluation.*`、`DemoCombatStatus.*`、`DemoAttributeProjection.*` 与 `DemoCore.*`
分别覆盖纯数值、Root/屏障/回复/冷却规则、属性及核心身份/许可/时间/投影边界；`LDV08LoadoutTests`、
`LDPreparationRulesTests` 仍验证保留纯来源模型的候选和拒绝，不能解释为 Demo 多槽已经批准。
`RunDemoCoreCheck.ps1` 用真实 Host 和两个 owning clients 运行明确标记的 DEV 初生/运动接缝，
旧 Combat/Content probe 与 runner 已删除。
新动作 presenter 的来源、资源和冷却表现由 N04/N05 与 UI 接线后验证，不保留无消费者的旧动作栏。
这些规则不恢复旧 profile，也不代替新成长、商店或真实拥有连接的运行验证。

数据编辑先运行对应 Sync `--check` 与输入测试。测试用的授予、传送或改时钟要明确是夹具，
不能当作正常升级、打野收益或完整对局已经成立。

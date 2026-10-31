# 战斗、效果与成长

本文说明可复用的原生结算链。Demo 的属性、三选一、两个主动槽和死亡保留规则来自
[App Spec](../APP_SPEC.md)，不能靠修改文档或目录标签宣称已经实现。

## 当前组件

| 入口 | 责任 |
| --- | --- |
| `Combat\LDCombatDirector` | 权威身体状态、意图、动作与命中批次、死亡处理 |
| `Combat\LDTacticalRules` | 状态允许性、伤害、护盾、驱散、恢复等纯规则 |
| `Combat\LDTacticalRequest`、`LDTacticalExecution.cpp` | 有界来源/目标请求和核心执行 |
| `LDContentActions.cpp`、`LDContentRelease.cpp`、`LDContentLifecycle.cpp` | 目录动作、释放和生命周期 |
| `LDEnvironmentRules`、`LDRiverRules` 与相关执行文件 | 有界地表/反应状态，独立于视觉表现 |
| `Development\LDCharacterBuild`、`LDDevelopmentDirector` | 当前构筑、等级/技能状态和准备提交 |
| `Development\LDV08Loadout`、`LDV08Modifiers` | 来源绑定、装备派生与修正器 |

这些入口位于 `Source\Lockdown\Public`/`Private`；当前目录由
`SourceData\Catalogs` 经 Sync 生成到 `Content\Lockdown\Data`。

## 从意图到结果

UI 只能准备或发送动作，不直接扣蓝、改 HP、增加效果或确认命中。
服务器验证 match/life、序列、真实动作来源、当前绑定、资源/冷却、状态与目标几何。
单位目标必须重新解析观察句柄，不能依赖预览时的旧权限或隐藏 Actor 指针。

预览、接受、释放、命中和结束是不同阶段。资源消耗与实际释放遵循当前执行器的同一提交边界；
失败要明确，不让客户端通过同时点击技能图标和快捷键产生两次效果。
已释放的效果是否继续存在由其生命周期决定，不通过销毁 UI 或更换槽位退款。

`LDTactical::Allows` 区分移动、普攻、技能、物品、位移和服务。
Stun、Silence、Mute、Root、Disarm 等不是同一个“不能操作”布尔值；
新增动作必须进入适当类别，不能靠客户端来源标签绕过状态。

## 权威时序

`LDCombatDirector::Sample` 是检查当前执行顺序的入口，不以旧设计段落代替代码：

- 接受合法意图，收集到期命中，处理直接效果及适用环境批次。
- 将命中写入权威状态，再处理本批死亡。
- 执行现有经济、生产、银行、商店、库存和成长事务。
- 处理返回、区域/世界更新，再调用目标与交易的当前 Sample 路径。

视野对外发布需要与已完成的权威变更保持一致。不要把独立 Actor 的无顺序 Tick 当作这条提交链。
有世界运行器时，`World\LDWorldClock.cpp` 的 `AdvanceAuthority` 按到期边界推进；
它防止重入，并在完成后恢复实际世界时间，不接受客户端回溯时间。

修改这个顺序会影响同时死亡、资源释放、命中与占领完成的同刻竞争，必须连同对应场景验证。
新争圈规则不能直接沿用当前 Heart 授权或旧交互中断条件。

## 状态、伤害和效果归属

`LDTactical::FState` 保存 MP、屏障、状态和各类期限；状态记录具有来源及独立到期时间。
驱散不等于回滚伤害，也不等于永久禁止持续来源重新施加效果。
护盾、普通抗性和条件修正应走统一伤害路径，不能由某个装备在末尾再加一层未经约束的减伤。

持续伤害按原计划的 tick 和受害者 life 去重；晚进入区域不重新领取整段总伤害。
释放者、资源预算所有者、当前伤害归属和受害者生命是不同概念，
反射、死亡或替换来源不能重建免费预算或让旧投射物击中新 life。

环境格有明确上限和作用平面。付费施法先保证分配与来源合法，提交时一致消耗；
不得出现已扣资源但静默没有效果，或多个来源对同一额度重复返还。
表现层不得成为第二个伤害、碰撞或效果计时权威。

## 构筑与来源

`FLDCharacterBuild` 当前仍有六维属性、Ranks、Slots/Actions、Cooldowns、Revision 等字段，
并受所选规则格式约束。这不是新 Demo 的 MOBA 属性与三选一模型。

`FLDBuildPlan` 携带预期构筑与库存版本；`Prepare`、`LDV08::Project` 在服务器投影并检查候选，
不能把客户端面板草稿直接复制成真实构筑。
学习、动作引用、物理装备绑定和物品实例分别管理；失去物品时，
`InvalidateSources` 必须让依赖该来源的动作失效，不能留下悬空热键。

等级、技能学习和 cooldown 是参赛状态，不应随 UI 或 Pawn 重建而免费重置。
后续实现 Demo 成长时优先复用身份、请求、来源与回执边界，
但需要明确改造当前构筑字段、选项状态和死亡策略，而不是继续继承旧职业/书籍规则。

## 检查入口

`RunCombatCheck.ps1`、`RunCombatNetworkCheck.ps1`、`RunContentCheck.ps1`、
`RunContentNetworkCheck.ps1`、`RunLoadoutCheck.ps1`、`RunB84Check.ps1`
覆盖不同层次；`LDCombatRulesTests`、`LDTacticalRulesTests`、
`LDV08LoadoutTests`、`LDB84RulesTests` 提供原生拒绝与边界检查。

数据编辑先运行对应 Sync `--check` 与输入测试。测试用的授予、传送或改时钟要明确是夹具，
不能当作正常升级、打野收益或完整对局已经成立。

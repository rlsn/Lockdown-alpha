# Demo 实施就绪评估

产品目标以 [App Spec](../APP_SPEC.md) 为准；当前组件接口以
[系统手册](../DEVELOPMENT.md#按任务读取系统手册) 和实际源码为准。
模块边界见 [实施计划](IMPLEMENTATION_PLAN.md)，跨模块契约见
[共享契约](SHARED_CONTRACTS.md)，依赖级复用与已完成的删除闭包见
[架构审计](ARCHITECTURE.md)。

## 结论与使用边界

**v0.10.0 Demo 的完整流程已经实现。** 流程为：前端建房或加入 → Lobby（Host 选 0–3 个对手 AI）
→ 从零打野、对抗、三选一与购物 → 开圈争夺 → 唯一结果 → Host 带仍在线的房间回 Lobby 开新局。
运行入口只有前端与唯一 Demo 世界；旧 profile、旧地图与旧选择会被明确拒绝，
[架构审计](ARCHITECTURE.md) §3.2 的删除闭包（含玩家交易与世界检查点）已经完成。
所有联机验证都在同一台机器的回环连接上运行。

运行输入是用户批准的首版内容与数值、唯一单层地图布局与操作规则，用于首次试玩。
**批准不是平衡测试结果。**

### 最终验收

提交 `3ae994d` 的 Development 包（2026-11-04 打包，manifest 记录该提交且无未提交改动）是当前唯一的已知可用构建，
本次全部打包版场景在它上面运行并通过。证据保存在本机忽略的 `Artifacts` 中；此前冻结提交 `6f8bca8` 的包在本机已不存在，
其旧验收结果不再作为当前验证。

| 检查 | 结果 |
| --- | --- |
| `BuildEditor.ps1`、`BuildGame.ps1`（均 `-FullUnity`）、`RunTests.ps1`（178 项必需规则）、Python 测试（146 项）、`BootstrapAssets.ps1`（Validate） | 通过 |
| `PackageWin64.ps1` | 通过，manifest 记录 `3ae994d` 且无未提交改动 |
| 打包版场景：Core、Combat（含操作手感数据与 9 张截图）、Growth（含第二局与账本复算）、Capture（含第二局）、Camp、Vision（含路人甲 T 恤隐身段四轮）、Opponent 满员（3 真人 + 3 AI，AI 占领获胜）、SessionFault（七段） | 全部通过 |
| 编辑器场景：Entry、Frontend、WorldRoom（这三个 runner 没有 `-PackageRoot`，只在编辑器中运行） | 全部通过 |
| `VerifyCleanCheckout.ps1` | 通过：在 `bea6211`（`3ae994d` 之后只改文档）的隔离检出中构建、Bootstrap 与 178 项规则 |
| 打包版网络模拟满员局（`-PktLag=40 -PktLagVariance=10 -PktLoss=1 -PktDup=1 -PktOrder=1`） | 通过；本局没有 AI 死亡，3 项死亡与复活检查报告为未覆盖（正常满员局已覆盖） |
| 有声运行 | 本次未运行（场景均以 `-nosound` 运行） |

Vision 的隐身段（检查组 `#ST`/`#STH`）在打包版上确认：两个受限客户端与 Listen Host 各隐身一次（Client1 两次），
隐身期间观察者的每一帧都没有在揭示半径外呈现隐身者、没有旧句柄、代理或 Host 原生身体；失视野取消锁定与追击；
近处重新发现换新句柄；攻击、施法、受伤、受控四种现身后，远处观察者在隐身本应到期前 1.7–2.8 秒重新看到它。
小地图点与飞字只读同一帧，本段在帧、句柄与代理层断言，不单独截图审阅。
PR 评审后，probe 改为经 relay 的 DEV 钩子逐帧审计，并按 Host 记录的权威隐身结束时刻给帧分类；
`844d5e1` 的候选包上重跑打包版 Vision 通过（其余场景未在该候选包上重跑，候选包随后删除，已知可用构建仍为上面的包）。

[#82](https://github.com/rlsn/Lockdown-alpha/issues/82) 的打包版实测项中，本次场景只能部分读到：
满员局三个 AI 在开局约 0.8 秒都决定去商店，最终两个 AI 持有装备，另一个整局未开店；
购买时间、AI 是否实际使用 D 冲刺、开局三选一的卡面构成（真人场景用 DEV 预置的开局选项）和 1 级普攻击杀时间都没有场景记录，
仍需试玩或专门场景确认。

### 产品决定的落地状态

| 决定 | 内容 | 状态 |
| --- | --- | --- |
| D01 | 首版内容与数值（[内容与数值](CONTENT_AND_BALANCE.md) §3–6、§10、§12、§14） | 已进入运行输入，未做平衡测试 |
| D02 | 唯一单层地图布局（[世界/营地/视野](WORLD_CAMPS_AND_VISION.md) §2、§6、§8） | 已实现，由 Bootstrap 生成 `L_Demo` |
| D03 | 攻击移动的自动选敌与续行（[操作§4.2](CONTROLS_AND_TARGETING.md#42-右键普攻与-a-攻击移动)） | 已实现 |
| D04、D05 | 校园世界观的名称、术语与游戏内说明文案 | 已被 D32 的雾港换皮取代（已实现）；运行内容 ID、数值与资产身份不变，说明仍按 D05 不写数字 |
| D06 | CC0 音效包与主音量设置 | 已实现，15 个音效；未做人工试听 |
| D07 | 小地图朝向设置：跟随镜头（默认）、跟随角色朝向、固定正北 | 已实现 |
| D08 | CC0 骨骼角色与校园装饰道具，道具只做装饰 | 已实现；旗杆遮挡时淡化 |
| D10 | 开局三选一：开局一次，每次升级一次，整局最多 10 次 | 已实现，AI 同样处理 |
| D12 | 旗杆争夺暂停原占领者进度，恢复独占后续计 | 已实现；复活时长等相关数值未改 |
| D15 | 保持 MOBA 点击操作并打磨手感：点击与悬停标记、按住右键持续移动、施法预输入、可选快速施法（默认关闭） | 已实现 |
| D16 | 施法那一刻显示技能或装备名称，不显示等级与冷却 | 已实现 |
| D09、D11 | 技能伤害与冷却调整、人人脱战回复 | 由 D18、D21 取代 |
| D13 | 校园补给箱 | 由 D26 的天降外卖取代 |
| D14 | [内容报告 §15](CONTENT_AND_BALANCE.md#15-后续批次构筑扩展已批准方向未进入运行输入)扩展第一波 | 由 D25 扩大为全部接入 |
| D17 | 1 级两人只用普攻对打，击杀时间为 10–12 秒 | 已进入运行输入，未做平衡测试（[#82](https://github.com/rlsn/Lockdown-alpha/issues/82)）：基础物攻 70、攻速 0.90，离线复算约 11.4 秒 |
| D18 | 伤害技能约等于 2–3 下普攻，冷却降 20%，全部吃力大砖飞加成 | 已进入运行输入，未做平衡测试（[#82](https://github.com/rlsn/Lockdown-alpha/issues/82)）：天外飞瓶、扒裤子加力大砖飞加成并降冷却 20%；天外飞瓶改为物理超度（[#81](https://github.com/rlsn/Lockdown-alpha/issues/81)） |
| D19 | D 键通用冲刺，AI 也会用 | 已实现（[#82](https://github.com/rlsn/Lockdown-alpha/issues/82)）：300cm/0.2s、18s 冷却、0 电量，不受整活频率影响、不解除复活保护；AI 用于追击、撤退和紧急回防。规则 `DemoCombatSources.InnateDash`、`DemoMotionProbe.DashStopsAtWall`、`DemoOpponentPolicy.InnateDash`、`LocalSettingsDashMigration` 已通过；满员局 `RunDemoOpponentCheck.ps1` 现在逐个 AI 记录冲刺，“至少一个 AI 实际完成冲刺”作为报告项（不卡门槛，多局验证见 [#117](https://github.com/rlsn/Lockdown-alpha/issues/117)），编辑器版单局出现过，打包版未复跑；冲刺手感见 [#80](https://github.com/rlsn/Lockdown-alpha/issues/80) |
| D20 | 《润学指南》改为强化冲刺的被动 | 已实现（[#82](https://github.com/rlsn/Lockdown-alpha/issues/82)）：被动「说润就润」冲刺冷却 ×0.5、距离 +50%，同名只算一次；释放后的冷却结束时间不因换装改变。规则 `DemoCombatSources.InnateDash` 已通过 |
| D21 | 人人脱战慢回；地图边缘 2–3 处躺平角（快速回复点），不是安全区 | 脱战慢回已实现（[#82](https://github.com/rlsn/Lockdown-alpha/issues/82)，规则测试 `DemoCombatStatus.OutOfCombatRecovery`，未做拥有连接场景验证）；躺平角已实现：两处对角躺平角、400 cm 判定圆、脱战 3 s 后每秒 10%，与慢回叠加，AI 恢复时会去躺平角（[#79](https://github.com/rlsn/Lockdown-alpha/issues/79)，规则 `DemoCombatStatus.DormRecovery`、`DemoOpponentPolicy.DormRecovery`；拥有连接场景未覆盖躺平角） |
| D22 | 发育期 120 秒，复活 15 秒 | 已进入运行输入，未做平衡测试（[#82](https://github.com/rlsn/Lockdown-alpha/issues/82)）；占领时长 25 秒不变 |
| D23 | 低档营地刷新 25 秒，初始的米足够开局买第一件装备 | 已进入运行输入，未做平衡测试（[#82](https://github.com/rlsn/Lockdown-alpha/issues/82)）：初始的米 320 |
| #82 追加 | 基础移速 600→520 cm/s，野怪移速同比例下调为 416/451；镜头不改 | 已进入运行输入，未做平衡测试（[#82](https://github.com/rlsn/Lockdown-alpha/issues/82)）；回防路线验证预算随之由 16 秒改为 18 秒 |
| #82 追加 | 属性卡：V 我 50 吃顿好的 +100→+60 精神状态上限，我不听我不听双抗各 +6→+10 | 已进入运行输入，未做平衡测试（[#82](https://github.com/rlsn/Lockdown-alpha/issues/82)） |
| D24 | 开局三选一：2 张随机基础属性卡（吃了口蛋白粉、伸手党、我不听我不听、V 我 50 吃顿好的，不含 5G 冲浪）加 1 个随机被动；升级三选一保持完全随机 | 已实现（[#82](https://github.com/rlsn/Lockdown-alpha/issues/82)，规则测试 `DemoGrowthOpeningPool`，未做场景验证） |
| D25 | 内容报告 §15 的全部 20 个技能和 10 件装备一起接入，含嘲讽、隐身、反伤等新机制 | 已进入运行输入，数值与规则 2026-11-04 用户批准作为试玩输入（[内容报告 §15.4、§15.7–15.9](CONTENT_AND_BALANCE.md#157-构筑扩展-a已接入条目与数值7781)），未做平衡测试；规则测试 `DemoBuildExpansion.*`、`DemoControl.*`、`DemoAttributeProjection.TemporaryTerms/ExpansionPassives`、`DemoCombatEvaluation.ControlAmplify`；对手 AI 尚不使用自身范围、跳跃与嘲讽类技能（[#77](https://github.com/rlsn/Lockdown-alpha/issues/77)、[#84](https://github.com/rlsn/Lockdown-alpha/issues/84)、[#83](https://github.com/rlsn/Lockdown-alpha/issues/83)、[#81](https://github.com/rlsn/Lockdown-alpha/issues/81)） |
| D26 | 公开的天降外卖（空投）；中期出现、会反击所有人的画饼老板 Boss（打赢即“吃到大饼”） | 天降外卖已实现：45 s 起每 75 s、三处公开落点（服务器随机、不连续重复，公告时才公开）、首个进入 150 cm 且不在复活保护中的参赛者得 200 流量 + 150 米，公开视图不公开领取者，AI 会争夺，App Spec §4.1 已改（[#79](https://github.com/rlsn/Lockdown-alpha/issues/79)，规则 `DemoAirdrop.Rules`、`DemoOpponentPolicy.Airdrop`；真人拥有连接场景未覆盖）；Boss 未实现 |
| D27 | 上热搜：领先者在小地图上有模糊位置提示，击杀他有额外赏金 | 已决定，未实现（[#79](https://github.com/rlsn/Lockdown-alpha/issues/79)）；是 App Spec §6.3 的明确公开例外 |
| D28 | 胜利条件不变 | 无需改动 |
| D29 | 整蛊表现：被扒裤子、被年轻人不讲武德等有夸张的可见状态，跟随现有控制状态 | 已决定，未实现（[#85](https://github.com/rlsn/Lockdown-alpha/issues/85)） |
| D30 | 地图布局与野怪可以重新设计；数量不变（1 个占领点、10 个出生点、5 组 2 档野怪、1 个商店） | 已决定，未实现（[#97](https://github.com/rlsn/Lockdown-alpha/issues/97)）；App Spec §6.1 随实现修改 |
| D31 | 2026-11-04 用户批准全部待批数值与规则（构筑扩展 A/B、路人甲 T 恤与嘲讽细则、体重即正义与你瞅啥），并采纳“按 D 立即朝指针冲刺” | 已进入运行输入，作为试玩输入，未经平衡测试；D 冲刺按下即朝指针方向发出、不再左键确认、与快速施法无关（规则 `DemoControlFeel.PreInput`）。原话：“全部批准，并采纳‘按 D 立即朝指针冲刺’” |
| D32 | 世界观改为雾港大学城，纯文字换皮（贴吧整活 v2）：对外标题“谁是雾港最强淫人王”，公开使用“淫”（雾港黑话，只指把人揍趴下），击杀播报“X 把 Y 淫了”；中心占领点叫“旗杆”；必须有扒裤子和掏蛋子；世界观术语集中到一张术语表，界面和测试都引用它；美术、音效、数值、机制与内容 ID 不变（[App Spec §1.1–1.2](../APP_SPEC.md#11-雾港世界观与表现)） | 已实现，[#111](https://github.com/rlsn/Lockdown-alpha/issues/111)：术语表 `Source/Lockdown/Public/UI/LDTerms.h`、52 行内容显示文字、运行时界面与测试（`Lockdown.Rules.HUDTermsGlossary` 等）均已落地，取代 D04、D05 的校园文字；与 v2 的偏差：状态 `exposed` 显示为“被截图了”（显示名必须唯一，被动已叫“已截图”）。已验证：Sync 检查、Python 测试、编辑器/游戏构建、Bootstrap Validate、184 个规则测试，以及编辑器版 Combat、Vision、Growth、Capture、Opponent、Frontend 场景与截图核对；打包版场景未运行。原话：“如果我们游戏的世界观换成 [雾港世界观] 这个版本，我们的游戏内容要有多大的改动 - 不动美术和音效。只改文字”；“必须要包含扒裤子和掏蛋子”；“还是不够有趣，还是略中二和严肃，你的想象力得天马行空，有简中互联网贴吧一样的文化氛围和梗，重做”；“公开用‘淫’：标题‘谁是雾港最强淫人王’，击杀播报‘X 把 Y 淫了’，修改 App Spec 的对外限制”；中心占领点“叫‘旗杆’”；“集中到一张术语表，界面和测试都引用它”；“now start the implementation”。#79 的地图事件同步改名（2026-11-05）：躺平角、天降外卖、画饼老板、上热搜，规则不变，原话：“采用：躺平角、天降外卖、画饼老板、上热搜，写进文档并在 #79 留言” |

## 已确认的实施方向

- 直接收敛当前工程，只维护 Demo 一条运行流程，不新增与旧模式并存的产品入口。
- 不需要的代码、配置、资产、数据、工具、测试和过期文档最终真实删除；
  不保留备用旧模式、兼容壳、备份目录或放弃方案。
- 必需底层组件原样复用、改造或抽取；不因旧玩法需要删除而全盘重写运动、结算或观测系统。
- 具体删除闭包以架构裁剪清单及当前真实依赖为依据，
  不从文件名里的 V07/V08/V09/Bxx 推断应该删除什么。
- 技术命名、类型映射和调度接线由工程实现决定；会新增或改变玩法的选择先交用户决定。

## 现有证据与不能据此得出的结论

下列编号用于需求映射。链接指向真实文件；符号比行号更适合后续维护。

| 证据 | 已核对入口 | 可以复用或确认的事实 | 不能据此声称 |
| --- | --- | --- | --- |
| E01 | [LDUIRoot.cpp](../../Source/Lockdown/Private/UI/LDUIRoot.cpp)，`ULDUIRoot::Host`；[LDDemoEntry.cpp](../../Source/Lockdown/Private/Framework/LDDemoEntry.cpp) | Host 只请求 Demo Listen 世界；本地及原生入口拒绝旧 profile/map/GameMode 与缺失/不匹配载荷 | 拒绝路径和前端可用就表示完整 Demo 可以开局 |
| E02 | [LDGameMode.h](../../Source/Lockdown/Public/Framework/LDGameMode.h)，[LDGameModeSession.cpp](../../Source/Lockdown/Private/Framework/LDGameModeSession.cpp)，[LDGameModeLife.cpp](../../Source/Lockdown/Private/Framework/LDGameModeLife.cpp) | 同一私有记录提供 Match 双身份、三真人准入、Host/Ready/AI 名额和有界会话回执；死亡、复活与成长经 Combat/Development 提供者接入同一记录 | AI 名额配置本身就是自主 AI，或旧死亡/安全出生方法可直接用于 Demo |
| E03 | [LDProgressionRules.h](../../Source/Lockdown/Public/Development/LDProgressionRules.h)，[LDGrowthProtocol.h](../../Source/Lockdown/Public/Development/LDGrowthProtocol.h)，[LDCombatSources.cpp](../../Source/Lockdown/Private/Combat/LDCombatSources.cpp) | 等级/经验、持久三选一机会队列、Q/W 与被动构筑、两装备来源与来源失效已实现，经同一账本与回执提交 | 纯值规则或回执通过就等于拥有连接下的成长与商店完成（真实连接见 `RunDemoGrowthCheck.ps1`） |
| E04 | [LDOrderTypes.h](../../Source/Lockdown/Public/Navigation/LDOrderTypes.h)，[LDNavigationComponent.h](../../Source/Lockdown/Public/Navigation/LDNavigationComponent.h) | `FLDMoveIdentity` 的 Match/Life/Order/Epoch、移动/攻击单位/攻击移动/商店接近订单、导航确认/停止、镜头接口存在 | 订单类型存在就等于拥有连接下的端到端行为（真实连接见 `RunDemoCombatCheck.ps1`） |
| E05 | [LDCombatTypes.h](../../Source/Lockdown/Public/Combat/LDCombatTypes.h)，[LDCombatActions.cpp](../../Source/Lockdown/Private/Combat/LDCombatActions.cpp)，[LDCombatDriver.cpp](../../Source/Lockdown/Private/Combat/LDCombatDriver.cpp) | 来源/目标/版本请求、动作受理与同一到期边界的命中/死亡提交链 | 公式已批准就等于每个效果的实际命中结果正确（见 `Lockdown.Rules.DemoCombatEvaluation` 与 `RunDemoCombatCheck.ps1`） |
| E06 | [LDCaptureReducer.h](../../Source/Lockdown/Public/Framework/LDCaptureReducer.h)，[LDGameModeCapture.cpp](../../Source/Lockdown/Private/Framework/LDGameModeCapture.cpp) | 连续独占圈归约、完成提案与唯一结果锁定已实现；旧工作/Claim/路线/deadline 目标规则已删除 | 纯归约测试通过就等于拥有连接下的占领与终局（真实连接见 `RunDemoCaptureCheck.ps1`） |
| E07 | [LDEconomyLedger.h](../../Source/Lockdown/Public/Economy/LDEconomyLedger.h)，[LDVendorDirector.h](../../Source/Lockdown/Public/Inventory/LDVendorDirector.h) | 账本有事务 ID、Cause、原子金币与物品变更；商店会话/报价/回执由 `ALDVendorDirector` 实现 | 账本复算通过就等于拥有连接下的买卖、满槽与死亡保留都正确（真实连接见 `RunDemoGrowthCheck.ps1`） |
| E08 | [LDViewTypes.h](../../Source/Lockdown/Public/Vision/LDViewTypes.h)，[视野与 UI](../systems/VISION_AND_UI.md) | 自身投影与观测记录分开；有 generation/revision 与隐藏/过期机制 | 任意现有地图格式都能直接接入，或 AI/UI 可读取全部服务器状态 |
| E09 | [LDOpponentController.h](../../Source/Lockdown/Public/AI/LDOpponentController.h)，[LDOpponentPolicy.h](../../Source/Lockdown/Public/AI/LDOpponentPolicy.h) | 每个 AI 槽由独立 `ALDOpponentController` 驱动，经与真人相同的领域入口发育、选择、购物并争圈 | 纯策略测试通过就等于满员对局可玩（真实连接见 `RunDemoOpponentCheck.ps1`） |
| E10 | [SourceData/Demo](../../SourceData/Demo)，[SyncDemoContentData.py](../../Scripts/SyncDemoContentData.py)，[数据管线](../systems/WORLD_AND_DATA_PIPELINE.md) | 唯一作者域、schema、稳定 ID 与载荷契约；首版内容与数值已于 2026-11-02 获批 | 首次试玩输入获批就是平衡测试结论 |
| E11 | [LDGrowthRulesTests.cpp](../../Source/Lockdown/Private/Tests/LDGrowthRulesTests.cpp)，[LDCombatSourceRulesTests.cpp](../../Source/Lockdown/Private/Tests/LDCombatSourceRulesTests.cpp) | `Lockdown.Rules.DemoGrowth*` 与战斗来源用例覆盖等级/机会、三选一、替换、两装备与来源冷却 | 规则套件全绿就满足拥有连接下的 Demo 行为 |
| E12 | [RunTests.ps1](../../Scripts/RunTests.ps1)，[CheckRuleReport.py](../../Scripts/CheckRuleReport.py)，[开发指南](../DEVELOPMENT.md#最小验证选择) | 已有构建、生成、规则和场景入口；总入口核对明确的当前用例集合，拒绝缺项、重复、失败与未完成 | 当前组件套件通过就等于 Demo 专属规则与真实场景均已实现 |
| E13 | [架构审计](ARCHITECTURE.md)，§3、§4、§8 | R01–R15 复用、D01–D34 删除闭包（已完成）、N01–N09 新增清单及 C1–C4 门槛 | 资产可不经 C3 盲删，或删除闭包完成就等于 Demo 行为已验收 |
| E14 | [操作与目标](CONTROLS_AND_TARGETING.md)，§5、§7.3、§10、§11 | 单一订单、只收件入口、写许可激活/原子交接、CMC 子段/事件序、授权终止已按该合同实现，拥有连接见 `RunDemoCombatCheck.ps1` | CT06 的全部提供者封桶（正式 Motion Fence/Sealing）已完成；核心类型仍由生命周期域单点定义 |
| E15 | [会话与生命周期](SESSION_MATCH_LIFECYCLE.md)，§4–6、§8、§12 | 唯一核心 Match/Victim/Body/DeathKey、实际时间/许可/写闸、RewardBasis、settlement-only、结果和故障退出已实现；3+3、同房间新局与网络异常见 Opponent、Capture、SessionFault 场景 | 跨机器联机已经验证；不能另造第二驱动 |
| E16 | [世界/营地/视野](WORLD_CAMPS_AND_VISION.md)，§2–5、§9；[当前世界管线](../systems/WORLD_AND_DATA_PIPELINE.md) | 现有100m单层布局于2026-11-02获首轮批准；严格world2生产者、原生目录/唯一地图、凸多边形碰撞和导航保存/重载检查已接通；营地、稀疏观测与实际运动见 `RunDemoCampCheck.ps1`、`RunDemoVisionCheck.ps1`、`RunDemoCoreCheck.ps1` | 七局部槽是七十个随机出生点，或静态导航检查等于动态落地、真实拥有连接、公平性及完整地图玩法通过 |
| E17 | [对手 AI](OPPONENT_AI.md)，§3、§6–9 | 无连接 Controller 适配、同一权威接口、P0–P9 策略与 AI 叶参数已实现，参数随中央方案获批；满员对局见 `RunDemoOpponentCheck.ps1` | AI 可新建第二世界/经济状态或读取隐藏状态；参数仍以中央内容报告为唯一来源 |
| E18 | [战斗与属性](COMBAT_AND_ATTRIBUTES.md)，§3–8、§13–15 | 唯一动作/属性/P2/来源写入、不可变致死集合、CampReset最终上限次序及34组验收有独立审核；typed 内容定义导入见 E21 | JSON/hash 或原生定义投影一致即可实现效果（实际执行见 `Lockdown.Rules.DemoCombatEvaluation` 与 `RunDemoCombatCheck.ps1`） |
| E19 | [成长/装备/经济](PROGRESSION_EQUIPMENT_ECONOMY.md)，§3–8、§12 | Gold/XP/等级/机会复合提交、账本复算/退休、持久候选、独立CD债与tagged查询有独立审核 | Ledger已经自动覆盖XP，或业务缓存可代替完整复算/拒旧（真实连接见 `RunDemoGrowthCheck.ps1`） |
| E20 | [UI与设置](UI_AND_SETTINGS.md)，§4–8、§11–12；[当前本地设置接口](../systems/MOVEMENT_INPUT_AND_CAMERA.md) | 本地类型化加载/保存、隔离测试存储、磁盘回读和无边框固定窗口已有实现；玩法界面经各领域的合法投影与单一输入层接线（`Lockdown.Rules.HUD*`） | 规则测试与离屏截图已经证明打包版的无边框窗口、OS 输入与音效听感 |
| E21 | [内容与数值](CONTENT_AND_BALANCE.md)，§3–14；[SyncDemoContentData.py](../../Scripts/SyncDemoContentData.py)；[LDDemoContentCatalog.h](../../Source/Lockdown/Public/Development/LDDemoContentCatalog.h)；[数据管线](../systems/WORLD_AND_DATA_PIPELINE.md) | 批准方案已生成正式作者数据/载荷/原生目录；严格 Decimal/定点、受保护输出、不可变 typed 目录及全字段校验接同一可信根。世界源/schema亦受独立字段/cm合同与字节身份约束 | 定义导入、地图生成或输入批准就表示已经实现战斗处理器、真实3+3与人类平衡 |

## App Spec 完成标准的覆盖

“场景”指 DEV 拥有连接场景（`RunDemo*Check.ps1`，发布验收在打包版上运行），“规则”指 `RunTests.ps1` 的必需规则。
每个场景覆盖与不覆盖的内容见[开发指南](../DEVELOPMENT.md#最小验证选择)。

| 完成标准 | 覆盖来源 | 未运行或只有部分覆盖 |
| --- | --- | --- |
| 正常启动 | 场景：每个角色从包内启动，经 Lobby 正常开局，以退出码 0 结束；SessionFault 使用前端加入控件；Combat 在局内打开设置。规则：`DemoEntryContract`、`DemoEntryObservation` 拒绝旧入口 | 前端界面点击“建房”；真人 OS 输入 |
| 满规模联机 | 场景：Opponent 以参赛的 Listen Host、两个拥有客户端和三个 AI 开局，名单锁定，AI 不补位、不替换；SessionFault 的真实加入按钮在满员时被拒并显示原因。规则：`DemoLobbyAuthority` 拒绝第四名真人和开局后加入，空 AI 名额不补人 | 跨机器联机；Opponent 中真人不参战 |
| 地图与寻路 | Bootstrap Validate 与 `DemoWorld.*`：对象数量、十个出生锚点、凸多边形碰撞与地点连通；`ArtDecorations` 与 `CaptureDemoArt.ps1`：装饰无碰撞、不挡视线、不改 navmesh。场景 Core：拥有身体经完整路线和原生 CMC 到达终点。场景 Combat：Recast 收缩边外的脚点作为点选地面被拒、作为追击终点可规划 | — |
| 营地反击与回位 | 场景 Camp：未受击不先手、整组反击、拴绳回营只重置一次且不发奖、清营后按批准时长整组刷新，另存回工位与刷新截图。规则：`DemoCamp.*`、`CampDecision` | 回营途中的受伤与控制没有拥有连接场景 |
| 从零发育 | 场景：Camp 中两次低档营地最后一击按批准值发放并升到 2 级；Growth 只靠真实击杀获得经验与金币，完成选择并购买；Opponent 中 AI 从零发育、购物并获胜 | — |
| 普攻成长 | 场景 Combat：学到增程属性后，右键追击在超出基础射程处命中。规则：`DemoAttributeProjection.*` 覆盖射程加成与群攻被动的来源归属 | 群攻（自拍杆横扫）只有规则层覆盖 |
| 成长边界 | 规则：`DemoGrowthOpening`（开局一次、升级各一次、最多 10 次、新局重抽）与 `DemoGrowth*`（等级与机会、死亡期间确认、替换与重放）。场景 Growth：开局 Offer、死亡等待中确认选择且不复活不治疗；Opponent：每个 AI 开局十秒内处理开局三选一 | 连升多级与满级只在规则层 |
| 上限变化 | 场景 Growth：死亡期间选择精神状态上限属性，只在下一条命生效并满血复活。规则：`DemoAttributeProjection.ResourceMaxChange` | 存活时的百分比保持只在规则层 |
| 技能替换 | 场景 Growth：Q/W 满槽未确认替换被拒且不消耗机会；确认替换后旧技能及等级遗忘、再学从 1 级开始，重学沿用原冷却截止时间。规则：`DemoGrowth*` | — |
| 装备边界 | 场景 Growth：两槽、同款两件、第三件被拒（EquipmentFull）、两件独立冷却、卖出后冷却债由新件继承。规则：`DemoAttributeProjection.DuplicateEquipment`、`DemoCombatSources.CooldownTracks` | D 键冲刺与《润学指南》加成没有拥有连接场景 |
| 奖励与死亡 | 场景：Growth 一次击杀恰好一次奖励，Listen Host 作为击杀者在本人视图、HUD 与“+X 米 / +X 流量”飘字中看到精确奖励，账本由 `CheckEconomyEvidence.py` 独立复算；Camp 每只每生命一次；Combat 击杀播报。规则：`DemoGrowthLedger` | 死亡后独立效果的存续没有拥有连接场景；真人击杀对手 AI 没有场景（[#89](https://github.com/rlsn/Lockdown-alpha/issues/89)） |
| 复活保护 | 场景 Combat：在十个出生锚点之一复活，保护窗口精确，空 Q 即解除保护且被拒动作不恢复。规则：`DemoSpawn.UnbiasedSample`、`DemoCapture.ProtectionAndContracts` | 保护期间免伤免控没有拥有连接场景 |
| 占领规则 | 场景 Capture：开局关闭；两次争夺都暂停原占领者进度并由他续计；争夺中原占领者离圈后清零，另一参赛者从零独占；圈空后原占领者从零完成；唯一结果并关闭写闸。场景 Opponent：AI 作为被暂停的占领者留圈迎战。规则：`DemoCapture.*`（含 `ContestPause`） | 争夺中原占领者死亡清零只在规则层 |
| 视野一致 | 场景 Vision：客户端只持有本人身体与当前帧代理，丢帧超时后清除且没有 LastSeen，Listen Host 不显示非本人的权威身体。规则：`DemoVision.*`、`ViewPrivacySchema`、`HUDObservationBoundary`；施法名只在施法那一刻随批准来源出现（`DemoVision.ActionCues`、`WorldFeedback.Casts`） | 失视停止追击已实现，没有拥有连接场景 |
| AI 可玩 | 场景 Opponent：AI 自主移动、升级并确认选择（含开局三选一）、到店购买、死亡期间不发身体请求、复活后行动、请求有界、开圈前取得经验，争夺暂停下的占领者留圈迎战，最终按占领获胜 | 本局没有出现的情形报告为未覆盖，不计为通过 |
| UI 完整 | 规则：`HUD*`、`WorldFeedback.*`、`WorldLabels.*`、`Minimap.*`、`LocalSettings*` 覆盖行动栏与冷却、资源、生命状态、飘字与升级提示、头顶标签、小地图三种朝向、下一步提示与设置保存；`Audio*` 与 `LocalSettingsMasterVolume` 覆盖音效与主音量。场景：Capture 与 SessionFault 的结果行与局内离开菜单；Vision、Camp、Combat、Opponent 保存截图供人工审阅；Frontend 渲染设置页 | 截图之外的人工界面审阅与音效人工试听 |
| 网络失败 | 场景 SessionFault：局中离开、Host 失联回菜单、地址不可达的加入失败；真实加入按钮遇到版本不兼容、三真人满员与对局已开始，各自显示中文原因与原因码且不进入房间；加入已通过 PreLogin 后 Host 开局，最终准入检查拒绝并显示“对局已开始”；Host 经局内菜单离开，客户端回主菜单显示“房主已离开”；Host 进程在客户端仍连接时正常退出，Host 日志无权威故障，客户端回主菜单显示会话中断。规则：`DemoLobbyAuthority`、`DemoEntry`（原因码映射，含 InvalidName）、`DemoOwnerViewTeardown`（拆除与关房期间不发布本人视图，对局中的真实投影不一致仍判故障） | 跨机器局域网与公网；网络损伤只有同机模拟；Host 进程被结束时客户端约 60 秒后显示会话中断 |
| 新局隔离 | 场景 Capture 与 Growth 的第二局：开局成长、金币、构筑、冷却、营地与抢圈全部复位，旧代请求被拒。规则：`DemoOrders.BookIsolation`、`DemoMatchIdentity` | — |
| 正常完整对局 | 场景：Opponent 的满员房间从大厅开局到 AI 占领获胜、结果和回 Lobby；Capture 中真人占领获胜后开第二局 | 三名真人的完整对局与人类试玩 |

## 未运行、未实现与已知限制

- **未实现**：CT06 的全部提供者封桶（正式 Motion Fence/Sealing）、root motion 全链与历史时刻的子段姿态（[#90](https://github.com/rlsn/Lockdown-alpha/issues/90)）。
  当前按步边界与核心当前时刻提交，偏离时明确故障；说明见[移动、输入与镜头](../systems/MOVEMENT_INPUT_AND_CAMERA.md)。
  D19–D21、D24–D27、D29、D30 已经决定，但还没有实现或进入运行输入，见下文“剩余工作”。
- **未运行**：跨机器局域网与公网直连（[#91](https://github.com/rlsn/Lockdown-alpha/issues/91)）；人类试玩与数值平衡（[#88](https://github.com/rlsn/Lockdown-alpha/issues/88)）；Shipping 配置打包（[#95](https://github.com/rlsn/Lockdown-alpha/issues/95)）；
  真实 OS 失焦与显示模式切换、完整人工 UI 验收（[#87](https://github.com/rlsn/Lockdown-alpha/issues/87)）；前端界面点击“建房”（场景以同一 `L_Demo?listen` 地址启动 Host）；
  音效的人工试听（[#86](https://github.com/rlsn/Lockdown-alpha/issues/86)；场景以 `-nosound` 运行，只有一次有声运行确认设备初始化、音效目录与混音生效）。
- **只有同机模拟**：延迟、抖动与丢包只用引擎包模拟参数在同机运行，不代表真实网络。
- **已知限制**：Host 进程消失后客户端约 60 秒才判定断开（引擎默认 ConnectionTimeout，未调整，[#91](https://github.com/rlsn/Lockdown-alpha/issues/91)）；
  截图只供人工审阅，不做像素比对；新包第一次带渲染运行的退出较慢（驱动管线缓存，最长超过 120 秒），退出码仍为 0。
  玩法与 AI 的场景覆盖缺口见 [#89](https://github.com/rlsn/Lockdown-alpha/issues/89)，画面与 UI 的验收缺口见 [#87](https://github.com/rlsn/Lockdown-alpha/issues/87)。

## 剩余工作

Demo 的完整流程已经跑通，但还没有经过人类试玩和平衡，**现在还不好玩**。下一阶段以好玩为先：

1. 先补齐影响玩法的逻辑和内容；
2. 再反复试玩，调整玩法、构筑和数值；
3. 最后做正式美术和声音。

下表列出缺少的内容。每一项的范围和验收标准见对应的 Issue；进度和排期只在 Issue 与 [Project 看板](https://github.com/users/sinmentis/projects/4)里维护。

### 玩法逻辑与内容（先做）

| 缺少的内容 | Issue |
| --- | --- |
| D 键冲刺、《润学指南》、开局卡池和脱战慢回（D19–D21、D24）；核心数值（D17、D18、D22、D23）已进入运行输入，待试玩 | [#82](https://github.com/rlsn/Lockdown-alpha/issues/82) |
| 内容报告 §15 的全部 20 个技能和 10 件装备：数据、说明文案和接入（D25） | [#81](https://github.com/rlsn/Lockdown-alpha/issues/81) |
| 构筑扩展的新机制：减速、限时与条件增益、自身范围伤害；缴械与眩晕、强化下一击、跳跃、反伤、背后判定；嘲讽与隐身 | [#77](https://github.com/rlsn/Lockdown-alpha/issues/77)、[#84](https://github.com/rlsn/Lockdown-alpha/issues/84)、[#83](https://github.com/rlsn/Lockdown-alpha/issues/83) |
| 地图事件：画饼老板 Boss、上热搜（D26、D27）及 AI 应对；躺平角（D21）、天降外卖（D26）与营地回位无路收尾（#28，规则 `DemoCamp.ReturnDeadEnd`，原生场景未注入无路情形）已实现 | [#79](https://github.com/rlsn/Lockdown-alpha/issues/79) |
| AI 使用全部新技能与装备；Opponent 满员场景的严格门槛稳定通过 | [#117](https://github.com/rlsn/Lockdown-alpha/issues/117) |
| 地图布局与野怪重新设计（D30） | [#97](https://github.com/rlsn/Lockdown-alpha/issues/97) |
| 打击感与核心手感：命中反馈、前后摇、移动和冲刺的手感 | [#80](https://github.com/rlsn/Lockdown-alpha/issues/80) |

### 迭代与试玩支撑

| 缺少的内容 | Issue |
| --- | --- |
| 调参热重载、调试控制台、AI 满局遥测、每日试玩包 | [#78](https://github.com/rlsn/Lockdown-alpha/issues/78) |
| 跨机器联机与组网说明、房主掉线超时、真实网络下的手感 | [#91](https://github.com/rlsn/Lockdown-alpha/issues/91) |
| 试玩记录与问卷、好玩门槛指标、AI 胜率调校、门槛评审 | [#88](https://github.com/rlsn/Lockdown-alpha/issues/88) |

### 表现、验收与交付

| 缺少的内容 | Issue |
| --- | --- |
| 整蛊表现（D29）、新技能占位特效、事件视觉、图标、HUD 可读性 | [#85](https://github.com/rlsn/Lockdown-alpha/issues/85) |
| 新技能和新事件的音效、人工试听、播放证据 | [#86](https://github.com/rlsn/Lockdown-alpha/issues/86) |
| 玩法与 AI 的拥有连接场景覆盖 | [#89](https://github.com/rlsn/Lockdown-alpha/issues/89) |
| 画面与 UI 的截图证据、完整人工 UI 验收、打包版回归门槛 | [#87](https://github.com/rlsn/Lockdown-alpha/issues/87) |
| 正式美术：风格指南、UI 换皮、建模与外观、动作、正式特效、场景、无障碍配色 | [#93](https://github.com/rlsn/Lockdown-alpha/issues/93) |
| 内部交付：冻结、打包、全套验收，核对文档与实际玩法是否一致 | [#92](https://github.com/rlsn/Lockdown-alpha/issues/92) |

### 内部交付之后

| 缺少的内容 | Issue |
| --- | --- |
| CT06 运动封桶；两个 commandlet 改为 `IsClient=true` | [#90](https://github.com/rlsn/Lockdown-alpha/issues/90) |
| 原创音乐与完整音频制作 | [#94](https://github.com/rlsn/Lockdown-alpha/issues/94) |
| 对外发布：Steam、Shipping、崩溃上报、宣发、新手教程、AI 难度、第二张地图、断线重连 | [#95](https://github.com/rlsn/Lockdown-alpha/issues/95) |

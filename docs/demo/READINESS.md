# Demo 实施就绪评估

本页只记录当前版本的验收状态：哪些已经验证、哪些没有运行、哪些没有实现。
产品规则以 [App Spec](../APP_SPEC.md) 为准，组件接口以[系统手册](../DEVELOPMENT.md#按任务读取系统手册)和源码为准，
待办与排期在 [Issue](https://github.com/rlsn/Lockdown-alpha/issues) 与 [Project 看板](https://github.com/users/sinmentis/projects/4)。

## 结论

**v0.11.0 Demo 的主流程已接入，完整验收尚未完成。** 流程为：前端建房或加入 → Lobby（Host 选 0–3 个对手 AI）
→ 开局休息 R0 → 三段野外各接一次休息 → 最终野外旗杆开放后喷漆、守护与围攻 → 唯一结果 → Host 带仍在线的房间回 Lobby 开新局。
规则工坊、元素、构件、公共休息室、PK、野外奖励、蓝图与评估入口已接入；局部规则与同机拥有连接已验证。
运行入口只有前端与唯一 Demo 世界，旧 profile、旧地图与旧选择会被明确拒绝。

- 运行输入是用户批准的内容与数值、唯一单层地图布局与操作规则，用于试玩。**批准不是平衡测试结果。**
- 所有联机验证都在同一台机器的回环连接上运行。
- 没有在本页记为通过的检查，按未验证处理；PR 里的验证结果只覆盖该 PR 的改动。

## 现有证据与不能据此得出的结论

下列编号用于需求映射。链接指向真实文件；符号比行号更适合后续维护。

| 证据 | 已核对入口 | 可以复用或确认的事实 | 不能据此声称 |
| --- | --- | --- | --- |
| E01 | [LDUIRoot.cpp](../../Source/Lockdown/Private/UI/LDUIRoot.cpp)，`ULDUIRoot::Host`；[LDDemoEntry.cpp](../../Source/Lockdown/Private/Framework/LDDemoEntry.cpp) | Host 只请求 Demo Listen 世界；本地及原生入口拒绝旧 profile/map/GameMode 与缺失/不匹配载荷 | 拒绝路径和前端可用就表示完整 Demo 可以开局 |
| E02 | [LDGameMode.h](../../Source/Lockdown/Public/Framework/LDGameMode.h)，[LDGameModeSession.cpp](../../Source/Lockdown/Private/Framework/LDGameModeSession.cpp)，[LDGameModeLife.cpp](../../Source/Lockdown/Private/Framework/LDGameModeLife.cpp) | 同一私有记录提供 Match 双身份、六席准入、Host/Ready/AI 名额和有界会话回执；死亡、复活与成长经 Combat/Development 提供者接入同一记录 | AI 名额配置本身就是自主 AI，或旧死亡/安全出生方法可直接用于 Demo |
| E03 | [LDProgressionRules.h](../../Source/Lockdown/Public/Development/LDProgressionRules.h)，[LDGrowthProtocol.h](../../Source/Lockdown/Public/Development/LDGrowthProtocol.h)，[LDCombatSources.cpp](../../Source/Lockdown/Private/Combat/LDCombatSources.cpp) | 等级/经验、五维点数、持久主动机会队列与待学图纸、Q/W/E 学习、来源失效经同一回执提交（部分规则已验证） | 纯值规则或回执通过就等于拥有连接下的成长完成（真实连接见 `RunDemoGrowthCheck.ps1`） |
| E04 | [LDOrderTypes.h](../../Source/Lockdown/Public/Navigation/LDOrderTypes.h)，[LDNavigationComponent.h](../../Source/Lockdown/Public/Navigation/LDNavigationComponent.h) | `FLDMoveIdentity` 的 Match/Life/Order/Epoch、移动/攻击单位/攻击移动订单、导航确认/停止、镜头接口存在 | 订单类型存在就等于拥有连接下的端到端行为（真实连接见 `RunDemoCombatCheck.ps1`） |
| E05 | [LDCombatTypes.h](../../Source/Lockdown/Public/Combat/LDCombatTypes.h)，[LDCombatActions.cpp](../../Source/Lockdown/Private/Combat/LDCombatActions.cpp)，[LDCombatDriver.cpp](../../Source/Lockdown/Private/Combat/LDCombatDriver.cpp) | 来源/目标/版本请求、动作受理与同一到期边界的命中/死亡提交链 | 公式已批准就等于每个效果的实际命中结果正确（见 `Lockdown.Rules.DemoCombatEvaluation` 与 `RunDemoCombatCheck.ps1`） |
| E06 | [LDCaptureReducer.h](../../Source/Lockdown/Public/Framework/LDCaptureReducer.h)，[LDGameModeCapture.cpp](../../Source/Lockdown/Private/Framework/LDGameModeCapture.cpp) | 喷漆与守护归约（独自喷漆、争夺暂停、硬控打断、涂鸦覆盖与清除、守护暂停（主人不在区内）、冻结、续计与主人离区清零）、完成提案与唯一结果锁定已实现 | 纯归约测试通过就等于拥有连接下的喷漆、守护与终局（真实连接见 `RunDemoCaptureCheck.ps1`） |
| E08 | [LDViewTypes.h](../../Source/Lockdown/Public/Vision/LDViewTypes.h)，[视野与 UI](../systems/VISION_AND_UI.md) | 自身投影与观测记录分开；有 generation/revision 与隐藏/过期机制 | 任意现有地图格式都能直接接入，或 AI/UI 可读取全部服务器状态 |
| E09 | [LDOpponentController.h](../../Source/Lockdown/Public/AI/LDOpponentController.h)，[LDOpponentPolicy.h](../../Source/Lockdown/Public/AI/LDOpponentPolicy.h) | 每个 AI 槽由独立 `ALDOpponentController` 驱动，经与真人相同的领域入口发育、选择并喷漆、守护与围攻 | 纯策略测试通过就等于满员对局可玩（真实连接见 `RunDemoOpponentCheck.ps1`） |
| E10 | [SourceData/Demo](../../SourceData/Demo)，[SyncDemoContentData.py](../../Scripts/SyncDemoContentData.py)，[数据管线](../systems/WORLD_AND_DATA_PIPELINE.md) | 唯一作者域、schema、稳定 ID 与载荷契约；首版内容与数值已获批 | 首次试玩输入获批就是平衡测试结论 |
| E11 | [LDGrowthRulesTests.cpp](../../Source/Lockdown/Private/Tests/LDGrowthRulesTests.cpp)，[LDCombatSourceRulesTests.cpp](../../Source/Lockdown/Private/Tests/LDCombatSourceRulesTests.cpp) | `Lockdown.Rules.DemoGrowth*` 与战斗来源用例覆盖等级/点数/主动机会、学习与加点准入与来源冷却（未运行） | 规则套件全绿就满足拥有连接下的 Demo 行为 |
| E12 | [RunTests.ps1](../../Scripts/RunTests.ps1)，[CheckRuleReport.py](../../Scripts/CheckRuleReport.py)，[开发指南](../systems/VERIFICATION.md#最小验证选择) | 已有构建、生成、规则和场景入口；总入口核对明确的当前用例集合，拒绝缺项、重复、失败与未完成 | 当前组件套件通过就等于 Demo 专属规则与真实场景均已实现 |
| E14 | [操作与目标](CONTROLS_AND_TARGETING.md)，§5、§7.3、§10、§11 | 单一订单、只收件入口、写许可激活/原子交接、CMC 子段/事件序、授权终止已按该合同实现，拥有连接见 `RunDemoCombatCheck.ps1` | CT06 的全部提供者封桶（正式 Motion Fence/Sealing）已完成；核心类型仍由生命周期域单点定义 |
| E15 | [会话与生命周期](SESSION_MATCH_LIFECYCLE.md)，§4–6、§8、§12 | 唯一核心 Match/Victim/Body/DeathKey、实际时间/许可/写闸、RewardBasis、settlement-only、结果和故障退出已实现；3+3、同房间新局与网络异常见 Opponent、Capture、SessionFault 场景 | 跨机器联机已经验证；不能另造第二驱动 |
| E16 | [世界/营地/视野](WORLD_CAMPS_AND_VISION.md)，§2–5、§9；[当前世界管线](../systems/WORLD_AND_DATA_PIPELINE.md) | 现有100m单层布局已获批准；严格world2生产者、原生目录/唯一地图、凸多边形碰撞和导航保存/重载检查已接通；营地、稀疏观测与实际运动见 `RunDemoCampCheck.ps1`、`RunDemoVisionCheck.ps1`、`RunDemoCoreCheck.ps1` | 锚点内七个局部槽是额外的随机出生点，或静态导航检查等于动态落地、真实拥有连接、公平性及完整地图玩法通过 |
| E17 | [对手 AI](OPPONENT_AI.md)，§3、§6–9 | 无连接 Controller 适配、同一权威接口、P0–P9 策略与 AI 叶参数已实现，参数随中央方案获批；满员对局见 `RunDemoOpponentCheck.ps1` | AI 可新建第二世界/经济状态或读取隐藏状态；参数仍以中央内容报告为唯一来源 |
| E18 | [战斗与属性](COMBAT_AND_ATTRIBUTES.md)，§3–8、§13–15 | 唯一动作/属性/P2/来源写入、不可变致死集合、CampReset最终上限次序及34组验收有独立审核；typed 内容定义导入见 E21 | JSON/hash 或原生定义投影一致即可实现效果（实际执行见 `Lockdown.Rules.DemoCombatEvaluation` 与 `RunDemoCombatCheck.ps1`） |
| E19 | [成长与奖励](PROGRESSION_AND_REWARDS.md)，§3–8、§12 | XP/等级/机会复合提交、持久候选、独立CD债与tagged查询 | 业务缓存可代替拒旧（真实连接见 `RunDemoGrowthCheck.ps1`） |
| E20 | [UI与设置](UI_AND_SETTINGS.md)，§4–8、§11–12；[当前本地设置接口](../systems/MOVEMENT_INPUT_AND_CAMERA.md) | 本地类型化加载/保存、隔离测试存储、磁盘回读和无边框固定窗口已有实现；玩法界面经各领域的合法投影与单一输入层接线（`Lockdown.Rules.HUD*`） | 规则测试与离屏截图已经证明打包版的无边框窗口、OS 输入与音效听感 |
| E21 | [内容与数值](CONTENT_AND_BALANCE.md)，§3–14；[SyncDemoContentData.py](../../Scripts/SyncDemoContentData.py)；[LDDemoContentCatalog.h](../../Source/Lockdown/Public/Development/LDDemoContentCatalog.h)；[数据管线](../systems/WORLD_AND_DATA_PIPELINE.md) | 批准方案已生成正式作者数据/载荷/原生目录；严格 Decimal/定点、受保护输出、不可变 typed 目录及全字段校验接同一可信根。世界源/schema亦受独立字段/cm合同与字节身份约束 | 定义导入、地图生成或输入批准就表示已经实现战斗处理器、真实3+3与人类平衡 |

## App Spec 完成标准的覆盖

“场景”指 DEV 拥有连接场景（`RunDemo*Check.ps1`，发布验收在打包版上运行），“规则”指 `RunTests.ps1` 的必需规则。
每个场景覆盖与不覆盖的内容见[验证手册](../systems/VERIFICATION.md#最小验证选择)。

| 完成标准 | 覆盖来源 | 未运行或只有部分覆盖 |
| --- | --- | --- |
| 正常启动 | 场景：每个角色从包内启动，经 Lobby 正常开局，以退出码 0 结束；SessionFault 使用前端加入控件；Combat 在局内打开设置。规则：`DemoEntryContract`、`DemoEntryObservation` 拒绝旧入口 | 前端界面点击“建房”；真人 OS 输入 |
| 满规模联机 | 场景：Opponent 以参赛的 Listen Host、两个拥有客户端和三个 AI 开局，名单锁定，AI 不补位、不替换；SessionFault 的真实加入按钮在满员时被拒并显示原因。规则：`DemoLobbyAuthority` 检查六席满员和开局后加入，空 AI 名额不补人 | 跨机器联机；Opponent 中真人不参战；六真人完整对局待验收 |
| 地图与寻路 | Bootstrap Validate 与 `DemoWorld.*`：对象数量、出生锚点、凸多边形碰撞与地点连通；`ArtDecorations` 与 `CaptureDemoArt.ps1`：装饰无碰撞、不挡视线、不改 navmesh。场景 Core：拥有身体经完整路线和原生 CMC 到达终点。场景 Combat：Recast 收缩边外的脚点作为点选地面被拒、作为追击终点可规划 | — |
| 营地反击与回位 | 场景 Camp：未受击不先手、整组反击、拴绳回营只重置一次且不发奖、清营后按批准时长整组刷新，另存回位与刷新截图。规则：`DemoCamp.*`、`CampDecision` | 回营途中的受伤与控制没有拥有连接场景 |
| 从零发育 | 场景：Camp 中两次低档营地最后一击按批准值发放并升到 2 级；Growth 只靠真实击杀获得经验，完成选择；Opponent 中 AI 从零发育并获胜 | — |
| 普攻成长 | 强化普攻、射程和元素来源在当前目录中；规则 `DemoGrowth*`、`DemoCombatEvaluation.*` | 完整拥有连接场景未覆盖全部组合 |
| 主动选择与学习 | 规则 `DemoGrowth*` 与 Growth 场景：开局池与 3/6 级机会、排除已学与图纸、为零时排队、能学就学（Q/W/E 已满时存为图纸）、学习图纸、交战中与死亡中学习和加点、PK 步骤受理、阶段关闭拒绝；Growth 场景在交战窗口内学到 W 并加点、死亡中学到 E 并加点 | 当前版本的 Growth 场景未在本页记为通过 |
| 成长边界 | 规则 `DemoGrowth*`：开局点数与机会、逐级点数、16 级后按 XP 给点、加点原子性、新局重建 | 部分规则已运行；连升多级与满级只在规则层 |
| 上限变化 | 规则 `DemoAttributeProjection.ResourceMaxChange` | 拥有连接场景未覆盖；存活时的百分比保持只在规则层 |
| 技能替换 | 满槽替换与遗忘只在休息时进行（`ReplaceActive`/`ForgetActive`，冷却债保留）；野外满槽时新主动保留为图纸。规则：`DemoRest.*`、`DemoGrowthBuildCommands`、`HUDBuildGate` | 没有拥有连接场景 |
| 奖励与死亡 | 场景：Growth 一次击杀恰好一次奖励，Listen Host 作为击杀者在本人视图、HUD 与“+X 流量”飘字中看到精确奖励；Camp 每只每生命一次；Combat 击杀播报。规则：`DemoGrowth*` | 死亡后独立效果的存续没有拥有连接场景；真人击杀对手 AI 没有场景（[#89](https://github.com/rlsn/Lockdown-alpha/issues/89)）；助攻分配由规则 `DemoAssist.*` 覆盖，满员局 `HostKillAssist.png` 只截到播报，助攻奖励只在遥测与规则层 |
| 复活保护 | 场景 Combat：在野外出生锚点之一复活，保护窗口精确，空 Q 即解除保护且被拒动作不恢复。规则：`DemoSpawn.UnbiasedSample`、`DemoCapture.ProtectionAndContracts` | 保护期间免伤免控没有拥有连接场景 |
| 喷漆与守护 | 场景 Capture：开局关闭；争夺暂停与续计、喷上涂鸦、主人出区守护暂停、他人独自改漆使守护保持冻结并在完成前出区（改漆清零、守护仍暂停）、主人独自回区从保留值续计（C18）、主人回区后与他人同在区内（守护冻结）再走出使守护清零、他人出区后守护停在 0、主人独自回区后从 0 计时（C19）、主人独自在区内站满守护时长以 `GuardSucceeded` 唯一锁定；场景 Opponent：AI 以喷漆守护获胜，`HostSpray.png`/`HostGuard.png` 截图审阅喷漆与守护 HUD 和旗杆涂鸦。规则：`DemoCapture.*`（含 `SprayAndGuard`、`GuardFreeze`、`GuardNeedsOwner`、`SprayInterrupt`、`GraffitiCleared`、`CombatHitInterrupt`：真实命中的眩晕与击退在施加瞬间清零喷漆） | 喷漆者死亡、硬控打断、主人死亡清除涂鸦与被击退出区只在规则层；主人回区后与他人同在区内再走出、守护清零后从 0 重计（C19）未在打包版上运行 |
| 战斗原语 | 规则：`DemoCombatPrimitives.*`（硬控递减、冷却缩减、解除减速、唯一控制入口）、`DemoCombatEvaluation.ControlAmplify`、`DemoMechanics.*`（护盾打破、击退方向、区域脉冲）、`DemoBuildPrimitives.*`（沉默、共享破防与灼烧、伤害加成池、吸血上限、减速中仍被定身、交战窗口触发（只供 HUD 交战折叠）、区域脉冲次数、多次强化普攻）、`DemoGrowthSkillGrowth`（等级成长）、`DemoAttributeProjection.*`（五维点数与夹取）（部分规则已运行）。场景 Combat 改用扒裤子定身、锅盖挡脸护盾与解除定身、甩你一鞋命中与减速 | 相关规则与场景不能代替全部技能效果的拥有连接验收 |
| 视野一致 | 场景 Opponent：`HostOpponentCard.png` 截图审阅可见 AI 的 Q/W 卡（截图早于当前目录，需重新截图）；规则 `DemoVision.OpponentActives`。场景 Vision：客户端只持有本人身体与当前帧代理，丢帧超时后清除且没有 LastSeen，Listen Host 不显示非本人的权威身体。规则：`DemoVision.*`、`ViewPrivacySchema`、`HUDObservationBoundary`；施法名只在施法那一刻随批准来源出现（`DemoVision.ActionCues`、`WorldFeedback.Casts`） | 失视停止追击已实现，没有拥有连接场景 |
| AI 可玩 | 场景 Opponent：AI 自主移动、升级并确认选择、死亡期间不发身体请求、复活后行动、请求有界、开圈前取得经验，争夺暂停下的喷漆进度持有者留区迎战，以喷漆守护获胜；遥测记录改漆与守护用时 | 当前版本的满员场景未通过：最近一次在 R1 返野外时权威故障，返场修复后未复跑完整场景；本局没有出现的情形报告为未覆盖，不计为通过 |
| UI 完整 | 截图审阅：`HostChoices.png`、`HostKillAssist.png`（播报助攻者）、`HostResults.png`（助攻列）、`HostOpponentCard.png`、`HostSpray.png`/`HostGuard.png`。规则：`HUD*`、`WorldFeedback.*`、`WorldLabels.*`、`Minimap.*`、`LocalSettings*` 覆盖行动栏与冷却、资源、生命状态、飘字与升级提示、头顶标签、小地图三种朝向与设置保存；`Audio*` 与 `LocalSettingsMasterVolume` 覆盖音效与主音量 | 截图早于当前构筑界面与构件库；构件库没有截图或人工界面审阅；全部游戏状态的人工界面审阅与音效人工试听未完成 |
| 伤害统计 | 规则：`DamageRecap.Window`（承伤窗口、求和与排序、各来源总量、列表校验）、`DamageRecap.Display`（死亡承伤列表与结算页本人伤害的可用条件、行文与合并） | 没有拥有连接场景：远端客户端收到自己的承伤列表与伤害总量、别人收不到，未经场景证明；死亡横幅与结算页的新段落没有截图或人工界面审阅 |
| 网络失败 | 场景 SessionFault：局中离开、Host 失联回菜单、地址不可达的加入失败；真实加入按钮遇到版本不兼容、六席满员与对局已开始，各自显示中文原因与原因码且不进入房间；加入已通过 PreLogin 后 Host 开局，最终准入检查拒绝并显示“对局已开始”；Host 经局内菜单离开，客户端回主菜单显示“房主已离开”；Host 进程在客户端仍连接时正常退出，Host 日志无权威故障，客户端回主菜单显示会话中断。规则：`DemoLobbyAuthority`、`DemoEntry`（原因码映射，含 InvalidName）、`DemoOwnerViewTeardown`（拆除与关房期间不发布本人视图，对局中的真实投影不一致仍判故障） | 跨机器局域网与公网；网络损伤只有同机模拟；Host 进程被结束时客户端约 60 秒后显示会话中断；六席满员场景待验证 |
| 新局隔离 | 场景 Capture 与 Growth 的第二局：开局成长、构筑、冷却、营地与抢圈全部复位，旧代请求被拒。规则：`DemoOrders.BookIsolation`、`DemoMatchIdentity` | — |
| 正常完整对局 | 场景 Opponent 的满员房间从大厅开局到喷漆守护获胜、结果和回 Lobby；Capture 中真人守护获胜后开第二局 | 当前版本的完整对局与三名真人试玩未验收 |

## 未运行、未实现与已知限制

- **未实现**：CT06 的全部提供者封桶（正式 Motion Fence/Sealing）、root motion 全链与历史时刻的子段姿态（[#90](https://github.com/rlsn/Lockdown-alpha/issues/90)）。
  当前按步边界与核心当前时刻提交，偏离时明确故障；说明见[移动、输入与镜头](../systems/MOVEMENT_INPUT_AND_CAMERA.md)。
  整蛊表现（被扒裤子等夸张的可见状态）未实现（[#85](https://github.com/rlsn/Lockdown-alpha/issues/85)）。
- **评估工具限制**：NPC 购买、单独遗忘、补缺、拆解、精英营地构件选择和选择时直接替换目前不能规划，明确报告 `UnsupportedDecision`；这不表示游戏内命令不支持。
  Harness 只完成小型真实对决、野外加点计划和一个 Case 的重放，不能代表全部决策或平衡验证。
- **未完成验收**：
  - 打包版已取得同机三拥有连接下的开局公共休息和构筑面板画面，以及前端真实渲染首帧；完整前端交互未验收。
  - 打包版三拥有连接夹具覆盖共享假人可见、准备、首次返野外和坡缝原脚点的返场回放。
  - 满员场景未通过：R0 三个 AI 报名并进入 PK，R01 假人槽位通过但账本未实际增长（未覆盖），R02 检查通过；
    R1 返回野外时出现 `Prepared body does not occupy its exact registered placement` 权威故障。返场已改为按物理胶囊实际支撑中心预约出生，
    相关地面、出生、营地和休息规则通过，但完整满员对局未复跑。
  - NPC 服务、构件安装、PK 与观战没有拥有连接场景；全套规则未在本页记为通过。
- **未运行**：跨机器局域网与公网直连；三真人完整对局与系统性数值平衡；Shipping 配置打包（[#95](https://github.com/rlsn/Lockdown-alpha/issues/95)）；
  真实 OS 失焦与显示模式切换、完整人工 UI 验收；前端界面点击“建房”（场景以同一 `L_Demo?listen` 地址启动 Host）；
  音效的人工试听（[#86](https://github.com/rlsn/Lockdown-alpha/issues/86)；场景以 `-nosound` 运行）。
- **只有同机模拟**：延迟、抖动与丢包只用引擎包模拟参数在同机运行，不代表真实网络。
- **已知限制**：Host 进程消失后客户端约 60 秒才判定断开（引擎默认 ConnectionTimeout）；
  唯一的弹道技能甩你一鞋沿用粉笔弹体外观；截图只供人工审阅，不做像素比对；
  新包第一次带渲染运行的退出较慢（驱动管线缓存，最长超过 120 秒），退出码仍为 0。
  玩法与 AI 的场景覆盖缺口见 [#89](https://github.com/rlsn/Lockdown-alpha/issues/89)。

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

### 产品决定的落地状态

| 决定 | 内容 | 状态 |
| --- | --- | --- |
| D01 | 首版内容与数值（[内容与数值](CONTENT_AND_BALANCE.md) §3–6、§10、§12、§14） | 已进入运行输入，未做平衡测试 |
| D02 | 唯一单层地图布局（[世界/营地/视野](WORLD_CAMPS_AND_VISION.md) §2、§6、§8） | 已实现，由 Bootstrap 生成 `L_Demo` |
| D03 | 攻击移动的自动选敌与续行（[操作§4.2](CONTROLS_AND_TARGETING.md#42-右键普攻与-a-攻击移动)） | 已实现 |
| D04、D05 | 校园世界观的名称、术语与游戏内说明文案 | 已实现；运行内容 ID、数值与资产身份不变 |
| D06 | CC0 音效包与主音量设置 | 已实现，15 个音效；未做人工试听 |
| D07 | 小地图朝向设置：跟随镜头（默认）、跟随角色朝向、固定正北 | 已实现 |
| D08 | CC0 骨骼角色与校园装饰道具，道具只做装饰 | 已实现；旗杆遮挡时淡化 |
| D10 | 开局三选一：开局一次，每次升级一次，整局最多 10 次 | 已实现，AI 同样处理 |
| D12 | 升旗台争夺暂停原占领者进度，恢复独占后续计 | 已实现；复活时长等相关数值未改 |
| D15 | 保持 MOBA 点击操作并打磨手感：点击与悬停标记、按住右键持续移动、施法预输入、可选快速施法（默认关闭） | 已实现 |
| D16 | 施法那一刻显示技能或装备名称，不显示等级与冷却 | 已实现 |
| D09、D11 | 技能伤害与冷却调整、人人脱战回复 | 暂缓，未进入运行输入 |
| D13 | 校园补给箱 | 下一轮再定，未实现 |
| D14 | [内容报告 §15](CONTENT_AND_BALANCE.md#15-后续批次校园构筑扩展已批准方向未进入运行输入)扩展第一波 | 已批准方向，未进入运行输入 |

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
| E20 | [UI与设置](UI_AND_SETTINGS.md)，§4–8、§11–12；[当前本地设置接口](../systems/MOVEMENT_INPUT_AND_CAMERA.md) | 本地类型化加载/保存、隔离测试存储、磁盘回读和显示试用生命周期已有实现；玩法界面经各领域的合法投影与单一输入层接线（`Lockdown.Rules.HUD*`） | 规则测试与离屏截图已经证明真实显示模式切换、OS 输入与音效听感 |
| E21 | [内容与数值](CONTENT_AND_BALANCE.md)，§3–14；[SyncDemoContentData.py](../../Scripts/SyncDemoContentData.py)；[LDDemoContentCatalog.h](../../Source/Lockdown/Public/Development/LDDemoContentCatalog.h)；[数据管线](../systems/WORLD_AND_DATA_PIPELINE.md) | 批准方案已生成正式作者数据/载荷/原生目录；严格 Decimal/定点、受保护输出、不可变 typed 目录及全字段校验接同一可信根。世界源/schema亦受独立字段/cm合同与字节身份约束 | 定义导入、地图生成或输入批准就表示已经实现战斗处理器、真实3+3与人类平衡 |

## App Spec 完成标准的覆盖

“场景”指 DEV 拥有连接场景（`RunDemo*Check.ps1`，发布验收在打包版上运行），“规则”指 `RunTests.ps1` 的必需规则。
每个场景覆盖与不覆盖的内容见[开发指南](../DEVELOPMENT.md#最小验证选择)。

| 完成标准 | 覆盖来源 | 未运行或只有部分覆盖 |
| --- | --- | --- |
| 正常启动 | 场景：每个角色从包内启动，经 Lobby 正常开局，以退出码 0 结束；SessionFault 使用前端加入控件；Combat 在局内打开设置。规则：`DemoEntryContract`、`DemoEntryObservation` 拒绝旧入口 | 前端界面点击“建房”；真人 OS 输入 |
| 满规模联机 | 场景：Opponent 以参赛的 Listen Host、两个拥有客户端和三个 AI 开局，名单锁定，AI 不补位、不替换；SessionFault 的真实加入按钮在满员时被拒并显示原因。规则：`DemoLobbyAuthority` 拒绝第四名真人和开局后加入，空 AI 名额不补人 | 跨机器联机；Opponent 中真人不参战 |
| 地图与寻路 | Bootstrap Validate 与 `DemoWorld.*`：对象数量、十个出生锚点、凸多边形碰撞与地点连通；`ArtDecorations` 与 `CaptureDemoArt.ps1`：装饰无碰撞、不挡视线、不改 navmesh。场景 Core：拥有身体经完整路线和原生 CMC 到达终点。场景 Combat：Recast 收缩边外的脚点作为点选地面被拒、作为追击终点可规划 | — |
| 营地反击与回位 | 场景 Camp：未受击不先手、整组反击、拴绳回营只重置一次且不发奖、清营后按批准时长整组刷新，另存回岗与刷新截图。规则：`DemoCamp.*`、`CampDecision` | 回营途中的受伤与控制没有拥有连接场景 |
| 从零发育 | 场景：Camp 中两次低档营地最后一击按批准值发放并升到 2 级；Growth 只靠真实击杀获得经验与金币，完成选择并购买；Opponent 中 AI 从零发育、购物并获胜 | — |
| 普攻成长 | 场景 Combat：学到增程属性后，右键追击在超出基础射程处命中。规则：`DemoAttributeProjection.*` 覆盖射程加成与群攻被动的来源归属 | 群攻（值日扫帚横扫）只有规则层覆盖 |
| 成长边界 | 规则：`DemoGrowthOpening`（开局一次、升级各一次、最多 10 次、新局重抽）与 `DemoGrowth*`（等级与机会、死亡期间确认、替换与重放）。场景 Growth：开局 Offer、死亡等待中确认选择且不复活不治疗；Opponent：每个 AI 开局十秒内处理开局三选一 | 连升多级与满级只在规则层 |
| 上限变化 | 场景 Growth：死亡期间选择最大面子属性，只在下一条命生效并满血复活。规则：`DemoAttributeProjection.ResourceMaxChange` | 存活时的百分比保持只在规则层 |
| 技能替换 | 场景 Growth：Q/W 满槽未确认替换被拒且不消耗机会；确认替换后旧技能及等级遗忘、再学从 1 级开始，重学沿用原冷却截止时间。规则：`DemoGrowth*` | — |
| 装备边界 | 场景 Growth：两槽、同款两件、第三件被拒（EquipmentFull）、两件独立冷却、卖出后冷却债由新件继承。规则：`DemoAttributeProjection.DuplicateEquipment`、`DemoCombatSources.CooldownTracks` | 方向冲刺等其他装备主动没有拥有连接场景 |
| 奖励与死亡 | 场景：Growth 一次击杀恰好一次奖励，Listen Host 作为击杀者在本人视图与 HUD 看到精确奖励，账本由 `CheckEconomyEvidence.py` 独立复算；Camp 每只每生命一次；Combat 击杀播报。规则：`DemoGrowthLedger` | 死亡后独立效果的存续没有拥有连接场景 |
| 复活保护 | 场景 Combat：在十个出生锚点之一复活，保护窗口精确，空 Q 即解除保护且被拒动作不恢复。规则：`DemoSpawn.UnbiasedSample`、`DemoCapture.ProtectionAndContracts` | 保护期间免伤免控没有拥有连接场景 |
| 占领规则 | 场景 Capture：开局关闭；两次争夺都暂停原占领者进度并由他续计；争夺中原占领者离圈后清零，另一参赛者从零独占；圈空后原占领者从零完成；唯一结果并关闭写闸。场景 Opponent：AI 作为被暂停的占领者留圈迎战。规则：`DemoCapture.*`（含 `ContestPause`） | 争夺中原占领者死亡清零只在规则层 |
| 视野一致 | 场景 Vision：客户端只持有本人身体与当前帧代理，丢帧超时后清除且没有 LastSeen，Listen Host 不显示非本人的权威身体。规则：`DemoVision.*`、`ViewPrivacySchema`、`HUDObservationBoundary`；施法名只在施法那一刻随批准来源出现（`DemoVision.ActionCues`、`WorldFeedback.Casts`） | 失视停止追击已实现，没有拥有连接场景 |
| AI 可玩 | 场景 Opponent：AI 自主移动、升级并确认选择（含开局三选一）、到店购买、死亡期间不发身体请求、复活后行动、请求有界、开圈前取得经验，争夺暂停下的占领者留圈迎战，最终按占领获胜 | 本局没有出现的情形报告为未覆盖，不计为通过 |
| UI 完整 | 规则：`HUD*`、`WorldFeedback.*`、`WorldLabels.*`、`Minimap.*`、`LocalSettings*` 覆盖行动栏与冷却、资源、生命状态、飘字与升级提示、头顶标签、小地图三种朝向、下一步提示与设置保存；`Audio*` 与 `LocalSettingsMasterVolume` 覆盖音效与主音量。场景：Capture 与 SessionFault 的结果行与局内离开菜单；Vision、Camp、Combat、Opponent 保存截图供人工审阅；Frontend 渲染设置页 | 截图之外的人工界面审阅与音效人工试听 |
| 网络失败 | 场景 SessionFault：局中离开、Host 失联回菜单、地址不可达的加入失败；真实加入按钮遇到版本不兼容、三真人满员与对局已开始，各自显示中文原因与原因码且不进入房间；加入已通过 PreLogin 后 Host 开局，最终准入检查拒绝并显示“对局已开始”；Host 经局内菜单离开，客户端回主菜单显示“房主已离开”。规则：`DemoLobbyAuthority`、`DemoEntry`（原因码映射，含 InvalidName） | 跨机器局域网与公网；网络损伤只有同机模拟；Host 进程被结束时客户端约 60 秒后显示会话中断 |
| 新局隔离 | 场景 Capture 与 Growth 的第二局：开局成长、金币、构筑、冷却、营地与抢圈全部复位，旧代请求被拒。规则：`DemoOrders.BookIsolation`、`DemoMatchIdentity` | — |
| 正常完整对局 | 场景：Opponent 的满员房间从大厅开局到 AI 占领获胜、结果和回 Lobby；Capture 中真人占领获胜后开第二局 | 三名真人的完整对局与人类试玩 |

## 未运行、未实现与已知限制

- **未实现**：CT06 的全部提供者封桶（正式 Motion Fence/Sealing）、root motion 全链与历史时刻的子段姿态。
  当前按步边界与核心当前时刻提交，偏离时明确故障；说明见[移动、输入与镜头](../systems/MOVEMENT_INPUT_AND_CAMERA.md)。
  D09、D11、D13 与内容报告 §15 的扩展不在运行输入中。
- **未运行**：跨机器局域网与公网直连；人类试玩与数值平衡；Shipping 配置打包；
  真实 OS 失焦与显示模式切换；前端界面点击“建房”（场景以同一 `L_Demo?listen` 地址启动 Host）；
  音效的人工试听（场景以 `-nosound` 运行，只有一次有声运行确认设备初始化、音效目录与混音生效）。
- **只有同机模拟**：延迟、抖动与丢包只用引擎包模拟参数在同机运行，不代表真实网络。
- **已知限制**：Host 进程消失后客户端约 60 秒才判定断开（引擎默认 ConnectionTimeout，未调整）；
  截图只供人工审阅，不做像素比对；新包第一次带渲染运行的退出较慢（驱动 PSO 缓存），退出码仍为 0。

## 剩余工作

1. CT06 的全部提供者封桶、root motion 全链与历史时刻子段姿态。
2. 跨机器局域网与公网直连验证、人类试玩与数值平衡（D09、D11 的数值调整在这之后再定）。
3. Shipping 配置打包与分发。
4. D13 校园补给箱与 D14（[内容报告 §15](CONTENT_AND_BALANCE.md#15-后续批次校园构筑扩展已批准方向未进入运行输入)扩展第一波）按用户决定逐批审查后再进入运行输入。
5. 音效人工试听、真实显示模式与 OS 失焦检查。

# Demo 实施计划与模块边界

本文记录 [App Spec](../APP_SPEC.md) 如何拆成模块与实施切片、各切片的验证入口，以及当前的交付流程；
不另设玩法规格或数值。验收状态与剩余工作见 [就绪评估](READINESS.md)，
跨模块契约见 [共享契约](SHARED_CONTRACTS.md)，运行命令见 [开发指南](../DEVELOPMENT.md)。

**I1–I8 各切片已经接入，Demo 的完整流程已经实现。** 运行输入是用户批准的首版内容与数值、
唯一单层地图布局与[操作§4.2](CONTROLS_AND_TARGETING.md#42-右键普攻与-a-攻击移动)的攻击移动规则；
批准是首次试玩输入，不是平衡测试结果。校园构筑扩展只在
[内容报告 §15](CONTENT_AND_BALANCE.md#15-后续批次校园构筑扩展已批准方向未进入运行输入)维护，尚未进入运行输入。

## 实施原则

- 运行入口只有唯一 Demo 流程，不以新的平行模式绕开它。
- 复用现有 CharacterMovement、导航、命令/回执、战斗、事务和视野边界；
  对旧产品系统做真实删除，对必要底层能力做有依据的保留、改造或抽取。
- 每次改动同时处理生产者、消费者、序列化、生成输入、资产依赖、入口和验证；
  不保留兼容壳，也不先删依赖再把失败断言关掉。
- 不以旧 SourceData 值或“临时默认值”填接口；新的数值方案经用户批准后才进入运行输入。
- 按接口所有者和资产负责人分工；共享头文件、核心配置和二进制地图先协调。
  使用独立工作区与 PR，已共享提交只用新提交修正或 merge 同步，不改写历史。

## 文档与模块负责范围

领域报告说明各域的设计依据、实现位置与检查入口；`docs/systems` 描述当前组件的接口与约束。
数值只由内容报告维护，其他文档引用正式键。

| 文档/领域 | 架构职责映射 | 内容 |
| --- | --- | --- |
| [架构审计](ARCHITECTURE.md) | R/D/N、C1–C4 | 唯一运行链；复用、改造、抽取与已完成的删除闭包；配置/资产/数据/工具/测试联动 |
| [会话与生命周期](SESSION_MATCH_LIFECYCLE.md) | M1、M2、M5 | 核心类型/时钟/许可/驱动边界；房间、占点、死亡/复活、结果、新局及故障退出 |
| [操作与目标](CONTROLS_AND_TARGETING.md) | M3 | 全部输入、点击分派、持续攻击/攻击移动、目标模式、可见性重查、取消、UI 捕获、镜头与操作手感 |
| [战斗与属性](COMBAT_AND_ATTRIBUTES.md) | M4 | 动作/来源/P2 写入、typed 字段消费、Prepare/Apply 投影、致死集合与 CampReset |
| [成长/装备/经济](PROGRESSION_EQUIPMENT_ECONOMY.md) | M6、M7、M8 | 死亡奖励、开局与升级三选一、替换、两装备与独立冷却债、账本复算、查询结果 |
| [世界/营地/视野](WORLD_CAMPS_AND_VISION.md) | M9G、M9C、M10 | 单层坐标与模型、合法路径、十锚点、营地、单一稀疏观测与隐私 |
| [对手 AI](OPPONENT_AI.md) | M12 | 受限快照、无连接 adapter、P0–P9 核心循环、同一命令/回执；参数来自内容表 |
| [UI与设置](UI_AND_SETTINGS.md) | M11 | 单一输入层、全界面合法投影、设置保存与显示试用 |
| [内容与数值](CONTENT_AND_BALANCE.md) | 各域内容输入 | 首版目录、公式/参数与依据、typed 生成和消费映射 |
| [共享契约](SHARED_CONTRACTS.md) | 跨域接缝 | 所有权、身份、单位/取整、来源冷却、命令/事件/读模型和全生命周期的跨文件唯一解释 |
| [就绪评估](READINESS.md) | 全部 Spec | 决定落地状态、§11 完成标准覆盖、未运行与未实现项 |

## 实施切片

切片之间的依赖是真实接口依赖。下表的“验证入口”列出当前覆盖该切片的规则与场景，
各场景覆盖与不覆盖的内容见[开发指南](../DEVELOPMENT.md#最小验证选择)。

| 切片 | 范围 | 状态与验证入口 |
| --- | --- | --- |
| B01 入口/基础门禁 | 前端、原生 Browse/GameMode、启动/打包/Bootstrap 拒绝旧选择与缺失输入 | 已实现；`TestDemoEntry.ps1`、`RunDemoEntryCheck.ps1`、`DemoEntryContract` |
| I1 单一入口与身份骨架 | 菜单/房间、名单/权限、Match 映射、服务接线与新局协议 | 已实现；`DemoLobbyAuthority`、`DemoMatchIdentity`、`RunDemoWorldRoomCheck.ps1`、`RunDemoSessionFaultCheck.ps1` |
| I2 世界几何、观察与地面移动 | 唯一地图地点、十点落地、遮挡/观察查询、CharacterMovement 与地面订单 | 已实现；`DemoWorld.*`、`DemoVision.*`、`RunDemoCoreCheck.ps1`、`RunDemoVisionCheck.ps1` |
| I3 统一战斗、目标与身体生命 | 目标订单、属性投影、普攻与群攻/增程、追击/攻击移动/施法、控制、致死、复活保护 | 已实现；`DemoCombat*`、`DemoAttributeProjection.*`、`DemoControlFeel.*`、`RunDemoCombatCheck.ps1` |
| I4 奖励、成长与两装备 | 奖励复合提交、经验与开局/升级三选一、替换、两装备买卖、来源冷却与界面 | 已实现；`DemoGrowth*`、`CheckEconomyEvidence.py`、`RunDemoGrowthCheck.ps1` |
| I5 连续独占与终局 | 有效集合、争夺暂停、同刻重查、公共状态、先锁写闸再清理/发布 | 已实现；`DemoCapture.*`、`RunDemoCaptureCheck.ps1` |
| I6 营地完整成长循环 | 五组两档营地、战斗/回位/清空刷新、每成员生命奖励 | 已实现；`DemoCamp.*`、`CampDecision`、`RunDemoCampCheck.ps1` |
| I7 能完成循环的对手 AI | 自主打野、选择、购买、战斗/恢复、争圈、死亡复活；同一接口等待/拒绝/重试 | 已实现；`DemoOpponentPolicy.*`、`RunDemoOpponentCheck.ps1`（3 真人 + 3 AI） |
| I8 全界面、删除闭包与完整交付 | 完整界面/设置、旧运行链删除复核、正常对局、多人回 Lobby/新局、打包 | 已实现；`HUD*`、`WorldFeedback.*`、`Minimap.*`、`LocalSettings*`、`Audio*`、`Art*`；打包版场景见就绪评估 |

旧系统清理随各切片完成，玩家交易与世界检查点已退出接口。以后的删除仍须满足具体闭包适用的
C3 资产/反射和 C4 有效配置/插件条件，不能沿用另一个闭包的检查结果盲删。
任何改动都不另造第二套经济、技能结算、位置积分或胜利状态。

## 验证层次

| 层次 | 入口 | 覆盖 |
| --- | --- | --- |
| 文档 | 相对链接、源码/脚本符号核对、`git diff --check` | 无失效链接；参数单点；状态描述与代码一致 |
| 内容/生成 | `SyncDemoContentData.py --check`、`SyncDemoWorldData.py --check`、`Test*Import.py` | 首版目录/参数可生成；非法值与不合法引用被拒；载荷/contract 一致；测试夹具不成为正式运行输入 |
| 原生与资产 | `BuildEditor.ps1`、`BuildGame.ps1`、`BootstrapAssets.ps1 -Mode Validate` | 无反射/链接/Cook 悬空依赖；唯一入口、地图、音效与美术目录可加载 |
| 规则 | `RunTests.ps1`（必需集合见 `CheckRuleReport.py`） | 属性/成长/装备/胜利/视野/界面/设置/操作的纯规则与原生规则；身份、取消与事务不变量 |
| 拥有连接场景 | `RunDemo*Check.ps1`，发布验收加 `-PackageRoot` 在打包版上运行 | 真实拥有客户端的订单、战斗、成长与买卖、营地、抢圈与终局、视野、AI 满员对局与网络异常 |
| 交付 | `VerifyCleanCheckout.ps1`、`PackageWin64.ps1`、`OpenPackaged.ps1` | 本地 LFS 缓存检出可构建（不证明远端下载）；发布包实际运行通过后才认定可用 |

所有联机证据都在同一台机器的回环连接上取得；局域网与正确配置的公网直连需要另外记录。
同机脚本和 Host 全知调试只覆盖各自边界，不能替代远端拥有客户端或网络配置。
运行输出写入忽略的 `Artifacts`，不把日志和快照写进产品文档；只保留一个已确认可用的发布构建。

## 交付流程

工作以 GitHub Issue 为单位，按 Milestone 排期，规则见 [AGENTS](../../AGENTS.md) 的“任务管理”一节。

1. 每张票从最新 `main` 建分支。PR 至少通过 `BuildEditor.ps1` 和受影响的规则测试，并写明新测试名。
2. 改动较大的 PR 和每个里程碑交付前，跑[开发指南](../DEVELOPMENT.md#合并前与里程碑检查)里的一轮检查。失败的按归属建票修复。
3. 冻结后依次跑完整门禁、`VerifyCleanCheckout.ps1`、`PackageWin64.ps1`。然后在包上跑全部拥有连接场景、
   网络模拟下的满员场景和有声运行检查。全部通过后，才替换唯一的已知可用包。
4. PR 说明里写清实际范围、批准状态、检查结果与限制。未达到 App Spec 退出条件的部分，在就绪评估中明确列出。

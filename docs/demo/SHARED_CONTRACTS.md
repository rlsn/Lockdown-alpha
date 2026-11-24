# Demo 共享契约

本文件定义跨模块必须一致的所有权、身份代次、时序、生命周期与读模型边界。产品规则见 [App Spec](../APP_SPEC.md)，领域签名见[系统手册](../DEVELOPMENT.md#按任务读取系统手册)，运行数值见[内容与数值](CONTENT_AND_BALANCE.md)。

## SC-01：唯一状态所有者

每个运行时真相只有一个写入域，其他域只消费只读投影或受限回执，不建立镜像可写状态：

| 状态 | 唯一权威写入域 | 允许消费者 |
| --- | --- | --- |
| 房间名单、Host、准备状态、AI 名额、连接准入与恢复 | `ALDGameMode` 会话域 | Lobby 公开视图（`ALDGameState::DemoLobby`）、拥有连接回执 |
| 比赛身份、权威时钟、阶段与时间线、喷漆/守护归约、唯一终局结果 | `ALDGameMode` 对局核心与 `FLDCaptureReducer` | `ALDGameState::PublicMatch`、`MatchResult`、拥有者阶段投影 |
| 结算记录与终局规则板摘要（含已学技能及分支、已装牌及位置、五维已分配点数、终局改装） | 服务器参赛记录与终局冻结投影 | 结果锁定后的全部客户端与同房间返回后的席位卡（局中绝不发布） |
| 赛后复盘（最终属性、阶段成长、按卡牌定义计的贡献与按类型汇总的战斗事实） | 成长事务与战斗提交记录，由服务器在结果锁定时冻结 | 结果锁定后的全部客户端，局内不发布；不参与判胜 |
| 本人伤害统计（死亡助攻窗承伤明细、局末本人各来源伤害与离谱时刻） | `ALDCombatDirector`（按已提交命中累加，致死瞬间冻结承伤） | 仅本人连接（死亡界面与结算页）；该来源明细不参与判胜 |
| 参赛者等级、XP、属性点、技能与分支、待学图纸、构筑牌、休息商店、终局改装 | `ALDDevelopmentDirector` / `ALDGrowthDirector` | 本人 `FLDOwnerState.Growth`；对应 AI 的 `ReadAISelf` |
| 身体资源（HP/劲/多源护盾栈 P2）、动作、控制、元素反应、地面状态、冷却轨道 | `ALDCombatDirector` | 本人 `CombatView`；经 `ALDVisionDirector` 过滤的公开观察事实 |
| 身体位置与碰撞积分 | 原生 `ULDCharacterMovementComponent` + `ULDNavigationComponent` | 本人预测/校正平滑；经 `ALDVisionDirector` 授权的呈现代理 |
| 静态世界几何、营地组生命、刷新与回位 | `ULDMapRegistry`、`ALDCampDirector` | 静态公开锚点与地形，预告中的空投落点，以及各观察者授权的野怪、恢复球与目击空营地 |
| 观察权限、句柄、`ObservationEpoch`、可见集合与授权死亡见证 | `ALDVisionDirector` → `ALDWorldViewRelay` | 拥有连接客户端表现与 HUD；对应 AI 的受限感知帧 |
| 玩家名称、按键绑定、画面模式/分辨率/帧率上限、音量、镜头与界面偏好 | 本地 `ULDControlSettings`（Schema 25）与 `UGameUserSettings` | 本机 UI/镜头/渲染；名称经 `?LDName=` 提交服务器复核 |

## SC-02：身份、代次与幂等回执

| 跨域身份 | 正式类型 | 约束 |
| --- | --- | --- |
| 比赛双身份 | `FLDMatchContext { uint32 Generation; FGuid LedgerMatch }` | 网络只传 `Generation`，服务器账本绑定 `LedgerMatch`，换局同时推进，不得强转混用 |
| 逻辑主体与身体 | `FLDVictimKey { Kind, EntityId, Life }`、`FLDCombatBodyRef` | 身份独立于槽位、Pawn 或显示名；新生命拒绝旧生命输入、命中与观察 |
| 死亡领奖主键 | `FLDDeathKey { LedgerMatch, Victim }` | 同一条生命至多一次致死奖励结算；重放或重复回调不二次发奖 |
| 移动与取消 | `FLDMoveIdentity { Match, Life, Order, Epoch }` | 新 Order 与递增 Epoch 拒绝迟到包，不重启已取消路线 |
| 动作请求与令牌 | `FLDCombatRequestKey`（Direct 用 `DirectSequence`，Nav 用 `MoveIdentity + AttackCycle`）→ 准入后发 `CombatToken` | 客户端取消上界 `ThroughDirectSequence` 只与 `DirectSequence` 比较，不与 `CombatToken` 混比 |
| 成长与会话请求 | `TLDGrowthRequestStream`（64 槽窗口）、`FLDSessionRequestWindow` | 同 ID 同条款重放既有回执；同 ID 异条款 `RequestConflict`；超窗 `ReceiptExpired` 拒旧且绝不重执行 |
| 目录摘要与定义 ID | `FLDCatalogDigest`（32 字节 SHA-256）、`FLDDefinitionWire`（64 字节 NUL 终止小写 ASCII） | 逐字节核对根 `manifest.json` 摘要；非法编码在形状校验直接拒绝 |

## SC-03：权威阶段序与同刻提交

每步唯一权威执行顺序（`LDCombatDriver.cpp`）：
`Motion → Session → Actions → Effects → DeathsAndRewards → RespawnsAndWorld → Transactions → Capture → Close → Publish`。

- **时基语义**：世界位置/距离统一用厘米（cm），移速 cm/s，权威截止与冷却用 `int64` 毫秒（`GetAuthorityNowMs()` 取 checked floor 判断就绪，事件按真实秒排序、ceil 分桶）。客户端倒计时到零不自行触发复活、技能就绪或判胜。
- **同刻裁决**：喷漆与守护完成前必须先结清同一时刻的位移进出、硬控打断、死亡与离局；完成锁定后立即关闭写闸，后续动作、效果或奖励不再改写胜负。
- **收件与激活分离**：网络 RPC 与 AI 提交入口只校验形状并入队，真正的状态变更只在对应权威阶段持有效 `FLDGameplayWritePermit` 执行。

## SC-04：死亡、休息、断线、终局与新局

| 状态 | 野外死亡/复活 | 进入/离开休息室 | 主动离开 vs 意外断线 | 终局锁定 / 回 Lobby 新局 |
| --- | --- | --- | --- | --- |
| 等级、XP、点数、技能、图纸、构筑牌 | 全部保留；死亡期可加点、选候选学空槽、学图纸、选牌、装卸牌与升技能分支（不可洗点、商店或换招） | 全部保留；休息配装开放洗点、商店、遗忘与替换（野外仅躺平角脱战可替换） | 主动离开立即移除身体并取消未结独立效果；意外断线挂入被动控制器保留身体与构筑供同进程重连 | 终局冻结为只读结果；回 Lobby 开新局全部按初始配置重建 |
| 来源冷却债 | 按服务器时间继续流逝，不因换 Pawn 刷新 | 进出休息阶段清理在途效果并就绪冷却 | 失源/遗忘期间继续计时，重学沿用原 `ReadyAtMs` | 终局停结算；新局清空 |
| 已经释放的独立效果 | 施放者正常死亡或遗忘技能后继续到原定结束，击杀仍归在局施放者 | 阶段边界统一取消清理（`CancelledByPhase`），不产生跨阶段命中 | 主动离开立即取消未结效果并清理待结算命中 | 终局停止后续结算 |
| 旗杆涂鸦与守护 | 死者未完喷漆清零；涂鸦、当前守护与历史贡献保留（守护暂停） | 前三段野外旗杆未开放；终局无休息 | 主动离局者失去冠军资格但保留结算行 | 唯一结果只提交一次；新局全部归零 |

Host 主动离开或进程结束即关闭房间，客户端显示原因返回主菜单，不支持 Host 迁移或中途新玩家加入。

## SC-05：奖励、构筑、来源冷却与复活保护

- **致死到奖励原子性**：同一批致死事实先冻结全部 `FLDCommittedDeath`（含助攻者快照），再在 `DeathsAndRewards` 阶段一次性提交击杀与助攻奖励；营地回位重置以 `LedgerMatch + VictimKey + ReturnEpoch` 幂等恢复存活野怪。中场返场保留营地槽身份并递增各成员生命与营地代次，退役存活的旧身体不产生死亡或奖励。
- **来源与多源护盾**：`FLDCombatSourceRef` 区分 `Basic / Learned / Innate`（D 冲刺固定单轨，不吃技能急速）。护盾总量为独立 `ShieldLayers` 之和，按最早到期层先吸收伤害，每打空一层独立触发一次破盾事件。
- **属性单次投影**：构筑变更对最终属性一次性投影并夹取至 `STAT.<属性>.Min/Max`；存活时最大生命变化按最终上限一次性同比换算当前生命（下溢为 0 时以 `NumericUnderflow` 整笔拒绝，往返不刷血）。
- **进攻意图先解保护**：请求通过身份、当前 `Match/Life`、去重与写闸准入后，凡属进攻意图（`AttackUnit`、`AttackMove`、普攻、角色主动技能及带撞击伤害的冲刺）立即解除复活保护并发布资格变化；随后即使因缺劲、冷却、越距或目标无效被拒，保护也不恢复，且不扣劲、不进冷却。

## SC-06：观察权限与本地 UI 边界

- **严格白名单观察（`FLDWorldView`）**：客户端与对手 AI 只接收视野内单位的名称、等级、HP/护盾比例、劲比例、蓄力、连击段、公开状态及可见参赛者的 Q/W/E 技能定义与等级（施法瞬间附技能名提示），绝不下发敌方冷却、属性点、构筑牌或隐藏坐标；Listen Host 本地画面与拾取遵守同一过滤。观察帧硬上限：单位 86、每单位公开状态 19、终结 16、飞行与区域 128、地面状态 40。
- **客户端合法记忆与剪影**：目标离开视野立即撤销动态代理与追击订单；左上目标卡仅保留 `2.5s` 半透明最后目击快照，Tab 战术板仅展示本局视野内曾亲眼目击过的等级与主动技能图标；被静态墙体遮挡的自身与当前授权可见敌方参赛者才可绘制屏幕剪影。
- **单一输入层（`ULDUIRoot::TopInputLayer`）**：`Esc` 严格按「文本框/按键捕获 → 子确认模态 → 施法/A 键准备态 → 主面板 → 局内菜单」逐层消费，不隐式发 `Stop` 或消费候选。本地设置经显式校验与磁盘回读确认后持久化。

# 库存、商店与事务

可复用的是身份、版本校验、原子提交和反馈机制，不是全部旧经济玩法。
Demo 只有两件装备与一个地图商店，范围见 [App Spec](../APP_SPEC.md)；
目标合同见 [成长、装备与经济](../demo/PROGRESSION_EQUIPMENT_ECONOMY.md)。

## 状态与入口

| 入口 | 责任 |
| --- | --- |
| `Economy\LDEconomyLedger` | 纯权威账本：每参赛者一份 Gold、两槽装备实例、独立提交/发行序号、审计前缀与有界尾部 |
| `Economy\LDEconomyDirector` | 成长/商店域枢纽：唯一账本、死亡奖励汇点、Transactions 分派、账户退休与拥有者发布 |
| `Development\LDDevelopmentDirector` | 等级/XP、机会队列与唯一持久三选一、Q/W 与被动、属性卡 |
| `Inventory\LDInventoryDirector` | 从账本读取自身两槽装备与可见对手装备外观；不持有第二份库存 |
| `Inventory\LDVendorDirector` | 商店会话、报价、单件 Buy/Sell 复合事务与回执 |
| `Development\LDGrowthProtocol.h` | 命令、回执、查询、提交结果与三类本人视图的线协议 |
| `Economy\LDGrowthProviders.cpp` | 向对局门禁注册 Progression/Shop 提供者及最终等级读取 |

`ALDEconomyDirector::SpawnDomain` 在核心初始化时创建四个 director，`RetireDomain` 随
`ALDGameMode::RetireMatchDomains` 退休；Progression 提供者的 Prepare 以锁定名单（真人加 AI 槽）
初始化账本账户（`SHOP.StartingGold`）与成长记录。复活、开店或读取投影都不会初始化钱包。

拥有连接的 RPC 在 `Framework\LDPlayerGrowth.cpp`：`SubmitChoice/SubmitShopCommand`
分配本族 RequestId 并在 RPC 前记入在途，服务器入口只调用对应 `Queue*`。
开店意图没有请求号：`BeginPlay` 绑定 `Navigation->OnShopArrived`，拥有者客户端在当前 ApproachShop
绑定到达时以该 Life 和本地 `ULDMapRegistry::Revision()` 发送一次 `SubmitShopOpen`；本地拒绝与服务器结果
都经 `OnShopOpenResult` 通知，HUD 用 `ReadShopInteraction` 判断结果是否仍属当前绑定。
本人视图随 `SyncOwner` 一起复制在 `FLDOwnerState.Growth`；`ReadOwnShop` 只在当前生命已 Opened 时有效。
旧背包面板（`LDInventoryPanel`、`LDPlayerInventory.cpp`）及其整理/丢弃链已删除；小卖部页面是 HUD 停靠的
`ULDShopPanel`，只格式化 `ReadOwnShop/ReadOwnEquipment`，买卖经 `SubmitShopCommand` 提交所见报价与修订。

## 物品身份不是格子

每件装备实例由账本发行：`InstanceId` 一一编码本局 `LedgerMatch` 与 `IssueSerial`，
记录定义、拥有者、槽位（0/1）与 ItemRevision。一个实例只有一个位置，两槽之外没有背包或地面位置。
同款两件是两个实例；第三件在准备阶段以 `EquipmentFull` 拒绝（先于余额检查，满槽时先卖是唯一出路），
不自动出售或替换。
来源冷却身份（`IssuedCooldownId`）与回绑由 Combat 持有，买卖只把变更后的完整装备列表交给 Combat 准备。

## 复合事务

奖励、选择和买卖都按同一形状提交：纯 Prepare 完成全部可失败工作（整数 checked、版本、容量、
Combat 投影与 HP/MP 同比换算），在同一调用栈重验许可后进入不会失败的 Apply，然后标记发布。

| 事务 | 同次提交 |
| --- | --- |
| `CommitDeathReward` | DeathKey 去重与事实指纹；Mint 金币 + 累计 XP + 逐级机会；升级时 Combat 投影；无主/自杀/离局为 NoReward |
| `CommitPendingChoice` | 原卡/版本/生死阶段重查；学习、升阶、属性或确认替换；消费一次机会；之后才抽下一组 |
| `CommitPendingShopCommand` | 会话/报价/世界资格/余额/槽位重查；Burn+Create 或 Consume+Mint；BuildRevision 与 Combat 投影 |

`ProcessTransactions` 按服务器受理顺序处理开店意图、选择与买卖，之后退休已离局账户并压缩账本。
单条请求失败只形成拒绝回执；只有失去写许可等权威故障才返回 false。
`PublishGrowth(Boundary)` 在 Close 之后执行，不需要许可，只对变化者调用 `SyncOwner` 并推送终态回执。

## 账本接口

`LDEconomy::FLedger` 不参与 UObject 复制。`Initialize` 只在新局建立一次；`Prepare(FTransaction)`
按 Cause 固定形状（DeathReward、Buy、Sell、AccountRetire）在完整后像上校验，返回可移动的
`FPreparedCommit`；`ApplyPrepared` 只交换已准备的状态并追加日志尾部。
提交序号与发行序号独立于尾部长度，失败不推进任何计数。

尾部容量为 64 条工程限额，不是对局次数上限。`TryCompactResolvedPrefix` 在维护点从旧前缀独立重放
全部尾部，与当前状态逐账户、实例、库存修订与总量比对一致后折叠成新前缀；被 pin、序号不符或历史不一致时
不覆盖。`Reconcile` 以同一重放核对 `初始总量 + Mint - Burn`。
账户退休是带核心离局戳的独立提交：金币和冻结装备仍参与审计，之后不再接受金钱或物品变化。

业务去重分层完成：请求序号屏障（每族 64 份回执窗口与终结水位）、DeathKey 生命水位与事实指纹、
以及尾部内的业务键/条款冲突检查。窗口外的旧请求返回 `ReceiptExpired`，不重新执行。

## 对外信息

普通客户端只接收自己的 Gold、装备、等级、Offer、商店报价与自身回执。
可见对手只经世界/视野发布获得两槽装备定义（`ProjectObservedEquipment`），
不含实例、修订、冷却、金币、XP 或技能。拒绝原因只给有权的本人。

## 检查入口

`RunTests.ps1` 中的 `Lockdown.Rules.DemoGrowthWire`、`DemoGrowthLedger`、`DemoGrowthRequests`、
`DemoGrowthProgression` 与 `DemoGrowthPrivacySchema` 覆盖线编码、账本守恒/压缩/退休、请求窗口、
等级/候选/替换纯规则与本人视图隐私白名单。
账本另有独立复算：DEV 构建的 `LDEconomy::ExportEvidence`（场景经 `ALDEconomyDirector::ExportLedgerEvidence`）
导出 schema 3 证据（审计前缀、其后日志尾、当前状态与 `devFixture` 区块），`python -B Scripts\CheckEconomyEvidence.py <证据>`
逐条按 `ApplyEntry` 规则重放并要求等于当前状态；`DemoGrowthLedger` 带 `-LDGrowthLedgerEvidence=<文件>`
时写出一份覆盖全部 cause 的原生样本，`TestEconomyEvidence.py` 用合成证据覆盖拒绝分支。

DEV 成长夹具（`Development\LDDevGrowthFixture.*`）只在 `WITH_DEV_AUTOMATION_TESTS` 构建中存在，
只在以 `-LDDevGrowthFixture` 启动的权威进程、当前开放对局内生效，没有 RPC，客户端、AI 和正常玩法不会调用。
Host 的 DEV probe 在游戏线程直接调用：
`ALDDevelopmentDirector::DevQueueOffer(Contender, 三个定义)` 为该参赛者之后生成的队首 Offer 按先进先出各预置一组，
入队时按当前构筑的 `LegalPool` 校验（恰好三张互异、种类由构筑推出、不超过剩余可抽的 Offer 数），
抽取时按当次合法池再验，失效则打 `LD_DEV_FIXTURE_PRESET_REFUSED` 错误并按正常种子抽取；
选择仍经 `SubmitChoice`→`CommitPendingChoice`→Combat 安装。
`ALDEconomyDirector::DevGrantReward(Contender, XP, Gold)` 排入下一个 Transactions 阶段，
与死亡奖励共用 `CommitRewardParts` 复合提交（账本 cause `DevFixture` 铸币、`PrepareExperience` 与 Combat 投影），
不伪造死亡事实；返回票号由 `DevGrantStatus` 轮询 Pending/Granted/Rejected。
开局与每次调用都打 `LD_DEV_FIXTURE_ACTIVE`。证据的 `devFixture` 记录进程是否带开关及整局全部 DevFixture 铸币；
只有给 `CheckEconomyEvidence.py` 加 `--dev-fixture` 时才接受这些铸币，且须与日志尾一致，
未加时任何夹具证据都判失败。
`RunDemoGrowthCheck.ps1` 是 DEV 拥有连接场景：渲染的参赛 Listen Host 作受害者并带 `-LDDevGrowthFixture`，
开局即为买方预置全部六组 Offer（先拒绝一组重复卡的预置）；一个真实拥有客户端
经自身连接真实击杀得奖励、完成一次选择与回执查询，再经 `ApproachShop`→`OnShopArrived` 开店，
买两件带无目标主动的同款、满槽 `EquipmentFull`、同号重放，用其中一件的主动（另一件轨道保持就绪），
出售用过的那件后报价显示其冷却债会被继承，最后离店 `OutsideRange`。之后买方把 Q 的技能升到 2 级、
在 W 学第二个主动并使用 Q；Q/W 已满时只点新主动不确认替换被 `ReplacementRequired` 拒绝且不消费，
确认替换 Q 后旧技能及等级从构筑消失、新技能 1 级绑定自己的就绪轨道，被遗忘的技能只以 1 级学习卡返回，
重学到 W 后绑定原 `IssuedCooldownId` 与原截止时间。随后 Host 经夹具给自己发同等级 XP 与金币（账本 `DevFixture`），
用自己的攻击命令击杀买方；买方死亡等待中确认一次选择：按 Dead 阶段受理并应用，仍为同一 Life 与复活时间、
无身体、HP 为零，只有下一条命的最大 HP 增长，正常复活后满血满蓝。Host 导出账本证据由
`CheckEconomyEvidence.py --dev-fixture` 复算并与客户端本人投影比对，且只列出 Host 自己那笔夹具铸币。
随后 Host 无胜者结束本局并回到新一代 Lobby，
两人 Ready 后正常开第二局：双方按本人投影核对等级/经验/金币回到开局值、无机会/技能/属性卡/装备、
Combat 无来源与冷却债；第一局的选择与 Buy 原样重放被 `OldMatch` 拒绝、旧局回执查询为 `InvalidContext`
且不改任何状态；第二局新开的商店报价不继承冷却；第二局账本在新 LedgerMatch 下重新初始化并经复算。
开局走正常 Lobby Start（全部开局提供者已注册），不覆盖营地、AI、抢圈或终局。
已持有两件同款时，报价不再为该物品预览冷却绑定（第三件必然 `EquipmentFull`）。
真实拥有连接下的奖励、选择、买卖、死亡/终局与新局隔离由 N05 与集成场景验证；
纯规则通过不代表联机场景已经通过。

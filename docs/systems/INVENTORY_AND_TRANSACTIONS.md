# 库存、商店与事务

可复用的是身份、版本校验、原子提交和反馈机制，不是全部现有经济玩法。
Demo 仅有两件装备与地图商店，具体范围见 [App Spec](../APP_SPEC.md)。

## 状态与入口

| 入口 | 责任 |
| --- | --- |
| `Inventory\LDInventoryTypes` | 物品种类、物理实例、库存投影与绑定 |
| `Inventory\LDInventoryDirector` | 拥有关系、整理/丢弃请求、回执、生命清理；使用和购买 opcode 显式拒绝 |
| `Economy\LDEconomyLedger` | 纯权威账本、账户/物品变更、预算和事务回执 |
| `Economy\LDEconomyDirector` | 保留物理实例/账户快照、记录、审计与导出；没有旧市场初始化或生产/银行服务 |

运行时头文件/实现分别位于 `Source\Lockdown\Public` 与 `Private`。
`Framework\LDPlayerInventory.cpp` 等负责拥有连接的 RPC 接线，
`UI\LDInventoryPanel.cpp` 只表现自身库存及仍有效的整理、移动、丢弃操作。
它不再提供旧书店、银行、市场、生产、采购或准备/读书入口。
集成须在同次切换真删 `LDPlayerBank.cpp`、`LDPlayerCommerce.cpp`、
`LDPlayerProduction.cpp`、`LDPlayerDevelopment.cpp` 和旧 `LDPlayerVendor.cpp`，
并清理保留 `LDPlayerInventory.cpp` 中的准备/读书调用；不能留下无服务的 Server 方法静默返回。

## 物品身份不是格子

每个物品有稳定 ID、定义、所在位置与 revision。一个权威实例只有一个位置；
背包、装备绑定、快捷入口、地面物体和 UI 图标不能成为多份独立库存。

排序或更换显示格子不改变实例 ID。动作/快捷引用按 ID 解析，不保存 Widget 指针或容易变化的数组下标。
消费、丢失所有权或解除来源时，在同一提交中清理相关引用，保持物品或来源 cooldown。
预约不转移所有权，不提供额外容量，也不能阻止必须发生的死亡清理。

当前库存容量与多绑定布局仍是已有运行规则。不能只缩小 HUD 就宣称实现 Demo 两装备限制；
容量、购买、派生属性、动作来源、复制和测试需要一起调整。

## Demo 商店接线边界

旧书店 `ALDVendorDirector`（会话、报价、购买队列）已整体退役：它唯一的业务是买书，Demo 运行中从未创建。
Demo 地图商店由 N05 在同名类中按 [成长、装备与经济](../demo/PROGRESSION_EQUIPMENT_ECONOMY.md) 的许可、会话、报价、Buy/Sell 与回执合同新建；当前没有可运行的商店服务。
不能以库存按钮、未初始化的账本或没有商品的会话骨架代替成交。

## 账本接口

`LDEconomy::FLedger` 是不参与 UObject 复制的权威域。
`Initialize` 建立一局的起始账户/预算；`OpenAccount` 不重置已有余额。
`Commit(FTransaction)` 接受具有唯一事务 ID 与 Cause 的金钱、物品和可选预算变更。
`Receipt`、`Reconcile`、`ExportJson` 用于查重、一致性与诊断。
这里的 `Initialize` 是纯 `FLedger` 的显式初始化，不是市场 Actor 的初始化。
Demo 实际账户与初始资源由 I4 接入，不能恢复旧市场 endowment 或添加临时初始经济。
I4 初始化接线之前，`Record` 明确记录未初始化错误并返回失败，不把未记账伪装成成功。

`FGoldDelta` 携带预期账户 revision；`FItemDelta` 携带操作、预期物品 revision、
来源/去向、定义及父身份。Create、Transfer、Consume 与质量/临时变化不是同一种操作。
金币使用有界整数，并区分流转、明确 Mint 与 Burn，不能把初始化或读取投影当作奖励来源。

复合事务先验证全部后果，再应用全部变更。任意一部分失败则不部分成交；
当前账本表示中的预算、输入消费、输出创建和父身份在同一事务中维护，不能分别重试。
这些保留字段不授权生产、采购或银行玩法；I4 按实际提交集继续收敛它们。
账本账户退休仍参与总量核对，不能通过“删除记录”隐藏丢失的资源。

## 事务安全边界

`LDInventory::Fits` 先投影移出和移入后的最终容量；预约和快捷引用不额外占用容量。
`LDInventory::Remove` 与 `LDV08::InvalidateSources` 负责同次变更中的引用清理。
`bReserved` 仍参与库存的来源可用性检查，不能把这一检查与旧生产服务一起删除。
纯投影写入的是候选结果；返回失败时不得发布候选，也不能直接把权威状态当作输出缓冲。

`FLedger::Commit` 以事务 ID 和完整条款去重，拒绝旧账户/物品版本、错误所有权与溢出，
全部校验成功后才更新钱物。重复的已提交请求只返回原结果，不能重新获取已转出的来源。
商店的会话、报价与库存版本重查由 N05 新建的 Vendor 承担，不另建交易状态或第二账本。

## 清理和对外信息

`EndLife`、`Logout`、终局与新局重置要协调预约、库存、派生动作和世界实例。
当前实现存在自身的死亡损失策略，不能当成 Demo 全保留规则已经实现。

普通客户端只接收自己的库存、合法报价和自身事务结果。
账本导出、隐藏钱包、物品血缘全表与内部失败细节不能用来填充对手 Tooltip。
失败反馈应可理解，但不能成为探测隐藏库存或 NPC 身份的接口。

## 检查入口

`RunTests.ps1` 保留 `EconomyAccounting`、`EconomyAtomicItemTransfer`、
`InventoryIdentity`、`InventoryPrivacySchema` 等实际规则。
新增的 `EconomyAtomicReplacement`、`EconomyUninitializedRefusal`、`InventoryProjectionIdentity`、
`InventoryMassProjection`、`PreparationProjection`、`GearProjection`、`SourceInvalidation`、
`LoadoutProjectionFailureOutputs` 由集成随同次切换加入明确必需集合；
不能在 `RequiredTestsDelta` 尚未应用时宣称这些名称已在运行器必需集合中。
`CheckEconomyEvidence.py` 复算当前 `FLedger` 导出，拒绝重复身份/变更、坏版本、
不守恒钱物、错误父身份与失败形状；`TestEconomyEvidence.py` 提供输入拒绝回归。
它独立于市场目录，不从旧场景报告推导本次游戏通过。

真实拥有连接下的创建失败、购物、取消/迟到回执、死亡/终局和新局隔离仍由 N05 与集成场景验证。
纯账本中无效输出身份的拒绝不是原生 Actor 分配失败的运行证据。

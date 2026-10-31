# 库存、商店与事务

可复用的是身份、版本校验、原子提交和反馈机制，不是全部现有经济玩法。
Demo 仅有两件装备与地图商店，具体范围见 [App Spec](../APP_SPEC.md)。

## 状态与入口

| 入口 | 责任 |
| --- | --- |
| `Inventory\LDInventoryTypes` | 物品种类、物理实例、库存投影与绑定 |
| `Inventory\LDInventoryDirector` | 拥有关系、组织/使用请求、回执、生命清理 |
| `Inventory\LDVendorDirector` | 商店会话、报价版本、购买队列与最终结果 |
| `Inventory\LDTradeDirector` | 当前实现中的双方交换和预约生命周期 |
| `Economy\LDEconomyLedger` | 纯权威账本、账户/物品变更、预算和事务回执 |
| `Economy\LDB92Services`、`LDB92Economy` | 完整地图服务的接入与账本投影 |

运行时头文件/实现分别位于 `Source\Lockdown\Public` 与 `Private`。
`Framework\LDPlayerInventory.cpp`、`LDPlayerVendor.cpp` 等负责拥有连接的 RPC 接线，
相应 `UI\LDInventory*Panel.cpp` 只表现合法状态。

## 物品身份不是格子

每个物品有稳定 ID、定义、所在位置与 revision。一个权威实例只有一个位置；
背包、装备绑定、快捷入口、地面物体和 UI 图标不能成为多份独立库存。

排序或更换显示格子不改变实例 ID。动作/快捷引用按 ID 解析，不保存 Widget 指针或容易变化的数组下标。
消费、丢失所有权或解除来源时，在同一提交中清理相关引用，保持物品或来源 cooldown。
预约不转移所有权，不提供额外容量，也不能阻止必须发生的死亡清理。

当前库存容量与多绑定布局仍是已有运行规则。不能只缩小 HUD 就宣称实现 Demo 两装备限制；
容量、购买、派生属性、动作来源、复制和测试需要一起调整。

## 商店会话与报价

`ALDVendorDirector::Open` 为合法的当前连接建立会话和报价。
`Queue` 带有 match/life/request、会话 ID、报价版本、展示价格和库存版本；
`Sample` 通过实际服务处理一次事务。

- 验证当前生命、距离/视野和服务状态，再检查价格、金币与容量。
- 展示过期价格的旧点击不能按新价格悄悄成交；刷新并等待新的明确操作。
- Pending 只表示已发请求，不意味着已拥有物品。
- 成功后扣费、生成/转移实例、更新库存与回执必须一致。
- 重复请求返回同一结果；创建失败不能留下扣款或虚假成功。
- 死亡、离开、终局或失效会话后，迟到请求不得继续购买。

当前 Vendor 仍提供书籍等既有功能；Demo 装备购买与出售需按新规格接入，不能只重命名按钮。

## 账本接口

`LDEconomy::FLedger` 是不参与 UObject 复制的权威域。
`Initialize` 建立一局的起始账户/预算；`OpenAccount` 不重置已有余额。
`Commit(FTransaction)` 接受具有唯一事务 ID 与 Cause 的金钱、物品和可选预算变更。
`Receipt`、`Reconcile`、`ExportJson` 用于查重、一致性与诊断。

`FGoldDelta` 携带预期账户 revision；`FItemDelta` 携带操作、预期物品 revision、
来源/去向、定义及父身份。Create、Transfer、Consume 与质量/临时变化不是同一种操作。
金币使用有界整数，并区分流转、明确 Mint 与 Burn，不能把初始化或读取投影当作奖励来源。

复合事务先验证全部后果，再应用全部变更。任意一部分失败则不部分成交；
奖励预算、输入消费、输出创建和父身份在同一事务中维护，不能分别重试。
账本账户退休仍参与总量核对，不能通过“删除记录”隐藏丢失的资源。

## 复用已有交易安全性

现有 `LDTradeDirector` 的流程是邀请、编辑、双方 Ready、精确版本确认和终局。
编辑、相关物品或余额变化会使批准失效；第二份有效确认才能提交。
应先投影双方交换后的库存容量，而不是只看交易前是否满包。

这套修订/预约/原子性知识可用于其他事务，但玩家互相交易不属于当前 Demo。
不要把当前交易的社交 UI、银行、采购或职业体系作为保留文档后的新增需求。

## 清理和对外信息

`EndLife`、`Logout`、终局与新局重置要协调预约、库存、派生动作和世界实例。
当前实现存在自身的死亡损失策略，不能当成 Demo 全保留规则已经实现。

普通客户端只接收自己的库存、合法报价、被明确公开的交易项和结果。
账本导出、隐藏钱包、物品血缘全表与内部失败细节不能用来填充对手 Tooltip。
失败反馈应可理解，但不能成为探测隐藏库存或 NPC 身份的接口。

## 检查入口

使用 `RunInventoryCheck.ps1`、`RunVendorCheck.ps1`、`RunTradeCheck.ps1`、
相应原生规则测试，以及 `CheckEconomyEvidence.py` 的实际账本复算。
完整地图服务另有 `RunB92Services.ps1` 等场景；这类当前结果校验不是已经删除的冻结档案复演。
重点是重复提交、旧版本、金币不足、满容量、创建失败、死亡/交易竞争和新局隔离。

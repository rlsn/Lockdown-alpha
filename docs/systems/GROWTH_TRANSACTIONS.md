# 成长与奖励事务

本页记录经验、等级、主动机会、图纸、五维点数、技能点、构筑牌/元素勋章、休息商店、终局改装与死亡/野外奖励的权威契约（`LDProgressionRules`、`LDDevelopmentDirector`、`LDGrowthDirector`、`LDGrowthProtocol`）。数值以 [内容与数值](../demo/CONTENT_AND_BALANCE.md) 与 `SourceData/Demo/content.json` 为准。

## 单一所有者与权威边界

- `ALDDevelopmentDirector` 是每名参赛者本局成长状态（`FLDGrowthBuild`、XP、未分配属性点/技能点、待处理候选队列、待学图纸、牌库与已装牌/勋章、终局改装、`BuildRevision`、回执表）的唯一权威所有者。
- `ALDPlayerController` 只持有已接受的本人只读投影 `OwnerState.Progression`（`ReadOwnProgression()` const 访问器）与单在途请求跟踪；AI 经 `LDOpponentGateway` 读取自身投影并提交同一套 `FLDBuildCommand`。
- 公开通道只包含名册、等级、K/D/A、目标贡献、终局改装揭示（终局开启后）与可见对手当前 Q/W/E 的基础定义 ID（不含技能等级、分支、冷却、属性点、牌或候选）。局末 `MatchResult` 公布全员最终等级、Q/W/E 定义及等级与分支、已分配五维点数数组（`TArray<int32>`）和终局改装。

## 经验、升级、点数与候选生成

- **累计 XP 与等级**：开局 L1、`XP=0`，阈值 `progression.xp_thresholds`（L1..L16 累计 `0..4170 XP`，分段曲线 L5 起每级额外 +15 斜率）。L16 封顶后继续累计合法 XP，每满 `GROWTH.MaxLevelPointsXP`（240）授予 `GROWTH.MaxLevelPoints`（3）属性点，不再升级或触发升级候选。
- **开局与升级授予**：
  - `BeginMatch`：授予 `GROWTH.OpeningPoints`（3）属性点与开局主动三选一（仅从 `opening=true` 的 15 个主动抽取）；处理完开局主动后立即派生一次**开局构筑牌三选一**。
  - 每升 1 级：授予 `GROWTH.PointsPerLevel`（3）属性点。
  - 达到 `progression.active_offer_levels`（`[2, 5, 9]` 级）：各授予一次主动技能三选一（全 24 主动池）。
  - 达到 `[3, 6, 9, 12, 15]` 级：各授予 1 个技能点（共 5 点；审计营地最多 +1，休息商店每局最多买 `SHOP.SkillPointMaxPerMatch=2`，单局最多 8 个技能点）。
  - 每升 3 级（`CARDS.OfferEveryLevels=3`，即 3/6/9/12/15 级）：各授予一次构筑牌三选一（按普通 100 / 稀有 22 / 传说 6 `drop_weight` 与词汇加权 `1.2× / 1.8×` 无放回抽取，词汇并入已选技能分支 `VocabMask`，不含元素勋章）；其余升级等级不发牌选组，按 `MEDAL.LevelChance`（8%）独立判掉 1 枚元素勋章进牌库。野外交战中（`bInCombat`）新入队的非开局三选一在 HUD 默认折叠为 `待选 ×N [B]` 徽章，脱战、按开合键或进入休息室时展开。
- **候选池与不阻塞队列**：主动候选自动排除当前已学 Q/W/E 与待学图纸（重学已遗忘技能不受限，但沿用原定义绝对 `ReadyAt` 冷却债务）。多组主动候选、牌候选与终局改装候选各自独立排队，互不阻塞，也无需重抽按钮。
- **放弃（Skip）规则**：仅常规主动三选一（开局与 2/5/9 级、空投/Boss 主动选组）支持放弃并换取 `GROWTH.ActiveSkipPoints`（1）属性点；牌三选一与终局改装候选不支持跳过换点。

## 构筑命令（`ELDBuildCommandKind`）与时机准入

所有构筑命令均验证 `FLDMatchContext`、`ContenderId`、`RequestId`（幂等回执重放）、`CatalogDigest`（32 字节 SHA-256）与 `ExpectedBuildRevision`：

| 命令 | 准入时机（含死亡等待与交战中） | 关键提交行为与回滚边界 |
| --- | --- | --- |
| `ChooseOffer` / `ChooseCardOffer` / `ChooseFinalMod` | 对局进行中随时（`ChooseFinalMod` 限 R3 候选存在时） | 有空槽且请求学习时直接装入空槽并签发/复用 Combat 冷却轨道；满槽或不立即学习时主动转为待学图纸；构筑牌进入牌库 |
| `LearnBlueprint` | 对局进行中随时（目标 Q/W/E 必须为空槽） | 消耗 1 份待学图纸装入指定空槽 |
| `UpgradeActive` | 对局进行中随时 | 消耗 1 技能点将指定槽位升至 L2 或 L3，并在该级 A/B 分支中二选一锁定（单局内不可逆，仅遗忘整个技能后重学可重选） |
| `AllocatePoints` | 对局进行中随时 | 按五维（体格/脸皮/力道/脑力/身手）非负增量一次校验并扣除未分配属性点；单项首次满 18 点自动激活对应关键石 |
| `InstallCard` / `RemoveCard` / `SwapCard` | 对局进行中随时 | 构筑牌受总容量 `6 + floor((L-1)/3)`（L16 为 11 格）与宿主形态/控制/命中类型（含当前已选分支效果）约束；`Y25` 绑定元素；元素勋章装入宿主专属元素槽（不占格子；异色替换将原勋章按阶拆回牌库，同色勋章再次装入则融合升为 `Ⅱ/Ⅲ 阶`，卸下时按已融合枚数全额拆回牌库） |
| `SwapSlots` | 对局进行中随时 | 互换两个 Q/W/E 槽位上的技能及其附着技能牌/元素勋章，不重置或串槽冷却轨道 |
| `ReplaceActive` | **休息室规划窗**，或**野外躺平角脱战就绪（`bDormRested`）**且无未决主动候选 | 用待学图纸替换指定已学槽位：旧技能被遗忘并**全额退还其已投技能点**；原槽附着的技能牌/勋章经 `ProgressionTransferOrReclaimAttachments` 检查——与新技能兼容的留在原槽，不兼容的退回牌库 |
| `ForgetActive` | **仅休息室规划窗**，且无未决主动候选 | 遗忘指定槽位技能，全额退还其已投技能点，槽上全部技能牌与元素勋章退回牌库 |
| `Respec` | **仅休息室规划窗** | 支持全退或按五维单退指定点数（客户端 `ULDStatBar` 支持混合退点+加点批处理，并对 >255 点拆批发送）；跌破 18 点即失去对应关键石 |
| `ShopBuyCard` / `ShopBuySkill` / `ShopBuySkillPoint` / `ShopRefresh` | **仅休息室规划窗**（R0 不开商店；R1–R3 开放） | 消耗未分配属性点（打折日 `ceil(price * 0.5)` 且 ≥1；首刷 0 点免费，同次休息每刷 +1 点）；购买的技能为无可用宿主牌也会入库并在 UI 提示宿主缺失 |

每次改变构筑的操作均先调用 `ALDCombatDirector::PrepareBuildChange` 计算单次最终属性投影与最大生命同比换算 `floor(oldCurrent * newMax / oldMax)`（活体正血缩至 0 时返回 `NumericUnderflow` 整笔拒绝，不制造无主死亡），成功后再与成长账本原子提交。

## 死亡奖励与野外事件奖励原子性

- **唯一致死结算（`ALDGrowthDirector::CommitDeathReward`）**：以 `(Match, VictimIdentity, VictimLife)` 为唯一 `DeathKey` 去重。
  - **野怪与 Boss**：XP 全额归最后一击的在局参赛者，**无助攻分摊**；按营地档位（小 15% / 中 25% / 大 45% / 远古 70%）或 Boss（100%）独立判掉元素勋章。
  - **参赛者击杀**：同一事务内同时 Prepare 击杀者与全部 `REWARD.AssistWindowSec`（10 s）内有效助攻者。击杀者获全额 `floor((70 + 20*(L_victim - 1)) * 等级差系数)` XP（系数为 `1 - 0.12*(L_killer - L_victim)`，夹在 0.4–1.6，由 `LDProgression::LevelGapXP` 计算）、本野外段前 2 次击杀各获牌三选一（第 3 次起改为 +2 属性点）、非终局 50% 掉元素勋章；若受害者正上热搜，额外获 `REWARD.BountyXP`（150）、+3 属性点与稀有/传说牌三选一。每名助攻者独立获得 `floor(0.5 * PlayerXP)` 与 +1 属性点，不稀释击杀者。
- **野外事件奖励（`PayFieldReward` / `CommitOwedFieldReward`）**：空投、Boss 构筑奖、标记营地、小迷你旗、擂台、外卖柜、二手贩子、热搜存活点、审计技能点与中场 PK 奖池均按 `(EventKind, Serial)`（PK 按个人键）幂等记账；暂态锁定期间进入待付队列，由权威步重试兑现，终局冻结后停止重试。
- **DEV 破格构筑预设（`LDDevelopmentDirector`）**：仅显式开启 `-LDDevTestMode` 时开放，包含 10 套一键预设（含 `Y05` 元素连锁、`K06` 双发核弹、`D10/D11` 冰道风筝、`Y17` 狂战吸血、`Y01/Y06` 反甲破盾、召唤物海及四套五维关键石流派），重置并装入对应等级、点数、分支与多份同名牌供实战手感验证。

## 检查入口

- 纯规则与目录：`Lockdown.Rules.DemoGrowth*`、`Lockdown.Rules.DemoAssist.*`、`Lockdown.Rules.HUDFlowSmoothness`、`Scripts/TestDemoContentImport.py`、`Scripts/TestBuildEvaluation.py`。
- 拥有连接场景：`Scripts/RunDemoGrowthCheck.ps1`、`Scripts/RunDemoCampCheck.ps1`。

# 世界、资产与数据管线

| 项 | 内容 |
| --- | --- |
| 规则 | [App Spec](../APP_SPEC.md) §1.1、§5；数值只在[内容与数值](../demo/CONTENT_AND_BALANCE.md)；关卡编辑约束见[关卡创作自由与开发约束](../LEVEL_AUTHORING_CONTRACT.md) |
| 文件 | `SourceData\Demo\content.json`、`SourceData\World\demo_world.json`；`Scripts\SyncDemoContentData.py`、`SyncDemoWorldData`、`SyncDemoMap.ps1`、`ApplyDemoMapEdits.py`、`CatalogTools.py`、`DemoContent.py`、`BootstrapAssets.ps1`；`Region\LDMapRegistry`、`ULDMapDefinition`、`Navigation\LDDemoNavigationSystem`、`World\LDCampLifecycle`、`LDCampDecision`、`LDCampDirector`、`LDCaptureCircle`；`Source\LockdownEditor\...\LDDemoWorldBuilder.cpp` |
| 入口函数 | `ULDMapRegistry::QueryGround` / `QueryCompletePath`、`FindApproach`、`TraceSight`、`QueryCircleMembership` / `ExtractCircleCrossings`、`QueryCombatContacts`；`ALDCampDirector::OnCombatHostileContact` / `AdvanceCamps` / `FreezeForRest` / `ThawAfterRest`；`ULDDemoContentCatalog` 快照 |
| 不变量 | 一张地图、一个旗杆；SourceData 是生成输入、Content 是游戏读取的载荷，docs 不是构建输入；载荷只经 Sync 生成，不手改哈希；查询不授予观察或交易权限；营地不另存身体、生命、战斗或奖励真值；出生锚点等概率有放回抽样 |
| 检查 | 对应 Sync `--check` 与 `Test*Import.py`、`BootstrapAssets.ps1 -Mode Validate`、`Lockdown.Rules.DemoWorld.*`、`StaticScene.*`、`DemoCamp.*`；场景 `RunDemoWorldRoomCheck.ps1`、`RunDemoCampCheck.ps1`（见[检查入口](#检查入口)） |

本手册保留当前地形、对象和生成链的维护知识，不要求 Demo 继承完整城市、人口或环境玩法。

## 三种不同的文件

| 位置 | 角色 |
| --- | --- |
| `docs` | 人和 AI 阅读的产品与技术说明，不作为构建输入 |
| `SourceData` | 脚本或原生导入实际消费的 JSON、schema、ID 映射、CSV |
| `Content` 与生成 contract 头文件 | 游戏读取的载荷、原生资产及它们的身份/哈希约束 |

SourceData 不是“给 AI 看的文档”，也不是新 Demo 的数值方案。
其中保留的规则字段是当前可构建工程的输入；不能据此反向扩充 App Spec。

## 生成入口与输出

| 输入/入口 | 输出或消费者 |
| --- | --- |
| `SourceData\Demo\content.json`、保存的 `L_Demo`、派生的 `SourceData\World\demo_world.json` 与两份 schema | 唯一 Demo 关卡→world→content 同步链及原生 Bootstrap |

`CatalogTools.py` 提供 Demo 生成器复用的确定性编码与原生 ID 校验。
schema8/schema9、V07/V08/V09、Bxx 命名是当前技术边界，不是产品版本 `0.11.0`。

源数据按 UTF-8 读取；涉及按字节计算的 SHA/MD5 时保留原始编码和换行。
`native_ids.json` 保留稳定原生编号，不通过重排枚举自动“修正”已有 ID。
`--check` 比较现有载荷与生成结果而不写文件；不一致应定位输入/生成链，不能删除断言或重写期望值。

## Demo 内容编译合同

公共休息室与野外共用唯一地面与同一连接：地面向东延伸，野外东墙是隔墙，休息室四面封闭。
`RestSpawn` 的 `rest_spawn_0..5` 只供阶段放置，不加入野外随机出生池；
`PKSignup` 的 `pk_signup_0..5` 在休息室里，六个点的平均位置是公共报名圈的圆心，各点也是 AI 按名单槽位站的位置；
`PKSpawn` 的 `pk_spawn_0..5`（`world.pk_spawn_ids`）围着旗杆，是 PK 的出生点；`PKThrow` 的 `pk_throw`（`world.pk_throw_id`）是观众投掷的射程原点。
这三类都不加入野外随机出生池。
`RestService` 的 `rest_service_3` 为训练师（`rest_service_1`、`rest_service_2` 不承载服务），三个定位点，位于 y=-500 cm、x=7100/7500/7900 cm；
服务交互仍复用 `AIRDROP.PickupRadiusCm` 的150 cm范围。`TrainingDummy` 的六个 `training_dummy_0..5` 位于北侧
y=700 cm，间距400 cm；各参赛者在对应锚点生成真实共享假人，由获准观察代理同步实际位置、生命与状态，地面边界另作公共展示；
训练锚点不进入野外 POI 或小地图地点。
Python/native importer 都识别新增锚点，服务范围、假人偏移与出生候选仍走实际胶囊净空检查。休息出生候选还须有到 `rest_service_1`
的完整室内原生路径；野外出生以野外旗杆为路径参考，终局出生以终局旗杆为参考。Bootstrap 校验每个休息出生偏移到该服务的路径。
改动后依序执行 `SyncDemoWorldData.py`、`SyncDemoContentData.py`、`BuildEditor.ps1`、
`BootstrapAssets.ps1 -Mode Regenerate`；必须重生成原生地图与定义，不能只刷新旧定义或复制旧地图作为新几何。

`Scripts\DemoContent.py` 与 `Scripts\SyncDemoContentData.py` 提供独立于旧目录的严格作者校验、
定点投影和根身份生成。`SourceData\Demo\content.schema.json`、`native_ids.json` 以及
`Public\Development\LDDemoContentIds.h` 只定义格式与编号保留，不批准候选技能或任何数值。
`native_ids.json` 的 `retired_ranges` 列出不得复用的旧编号（技能 0–91、104–113、119–125，属性卡 0–26，状态 28–29，以及退役的效果操作）；新编号必须同时匹配作者映射与原生声明。

生产输入必须是经审查的 `SourceData\Demo\content.json`，其中
`identity.approval.status` 为 `approved`，`decision` 引用实际审批决定。
这个字段记录审批，不是编译器授予审批。没有已批准内容或真实世界产物时明确拒绝，
不创建 `content.json`、不从旧目录补值，也没有 candidate/fixture 运行开关。
`Scripts\DemoContentTestFixtures.py` 仅在 unittest 的 `Artifacts` 临时根提供合成机制夹具；
其审批形状的测试元数据、数值和几何均不能作为产品输入。

| 边界 | 当前编译器行为 |
| --- | --- |
| 读取与 schema | UTF-8；小数从原文直接读为 Decimal，拒绝重复键、未知字段/操作/ID、错误单位/等级/引用、bool 整数、非有限值和不可表示精度 |
| 数值投影 | P2 → `{unit, p2}` 百分之一点；R4 → `{unit, bp}`；秒 → `{unit: "ms", ms}`；距离/速率 → `{unit, numerator, denominator}` 的约分 int64 有理数；计数/XP 为 int64 |
| 完整定义 | 角色参数、单级主动的完整数值/effects 与开局池标记、五维属性每点加成、成长点数参数、XP 阈值与主动机会等级、四档营地与十二种的攻击模式、AI 参数和显示引用均进入闭合树；没有透传的 ActiveJson 或丢弃坏字段 |
| 纯提供者 | `compile_content(root)` 返回不可变 Catalog、三份输出字节、输入快照和 32-byte 根摘要；`build_outputs(root)` 返回输出映射；Catalog 不拥有 Match、时钟、HP、XP 或其他玩法状态 |
| 世界前置 | 必须读取真实 `Content\Lockdown\Data\Demo\world.json` 及 `Private\World\LDDemoWorldContract.h`，核对实际字节，不接受调用方单独给出的 digest 或空世界占位 |
| 输出 | 同时生成 `Content\Lockdown\Data\Demo\demo_content.json`、`manifest.json` 与 `Private\Development\LDDemoContentContract.h`；不生成世界文件或世界契约 |

大整数不得经过 JSON double 再取回。原生消费者需按整数 token/明确整数编码解析这些字段，
不能用 UE 的浮点 JSON 读取当作完整 typed importer。定义 ID 的窄 wire 为 64 字节：
最多 63 字节小写 ASCII、NUL 终止、余字节为零；目录 digest 为 32 原始字节，显示才用 64 位小写 hex。
纯 wire 编解码与格式拒绝在 `DemoContent.py` 中提供，不替代业务域对未知/退役 ID 的拒绝。

世界字节入口的完整检查见 `DemoWorld`：当前互换版本为 schema/projection 5，
包含 map_id、作者/schema 的真实字节摘要、批准记录、真实 surfaces/blockers 多边形及
Spawn、Camp、Capture、Dorm、Airdrop、BossWaypoint、MiniFlag、Locker、Vending 和休息室锚点、营地成员 home 与七个局部落地偏移；
坐标是有符号约分整数对，营地另有成员 home。检查支持面/坐标形状、ID、数量和内容引用，
**不证明胶囊净空、路径可达、碰撞或原生地图资产正确**，这些仍由世界生成器/Builder 负责。
世界作者校验有代码拥有的完整字段/类型/cm 单位约束；可编辑 schema 不能扩充原生支持集后
丢弃新字段，也不能将米伪装成厘米。内容根生成重新调用同一世界 producer 比较全部真实产物，
不能仅手写 world 和匹配摘要让正式链通过。

终局场地使用独立锚点：`FinalSpawn` 固定为 `final_spawn_0..5`，`FinalCapture` 固定为
`final_capture`。保存地图的 `LDSceneAnchor:<id>` 点由 `SyncDemoMap.ps1` 投影进世界输入；
原生目录校验六个出生点同层、等距并按60°顺序分布，Bootstrap 再验证出生偏移的胶囊地面与到旗点的完整导航路径。
它们不进入普通野外 POI/小地图；显式出生选择通过 `LDDemoWorld::FinalSpawnId` 和
`LDDemoWorld::FinalCaptureId` 解析，不替代野外的 `capture` 锚点。

野外目标与隐藏物使用三类可选锚点，id 前缀决定类别：`MiniFlag` 为 `mini_flag_*`（至少 3 个，最多 16 个），
`Locker` 为 `locker_*`（至少 6 个，最多 32 个），`Vending` 为 `vending_*`（至少 2 个，最多 16 个）。
关卡作者可以移动、复制或删除这些点：复制的点以 `<原 id>_copy_<演员名的 8 位十六进制校验值>` 得到新 id（`LDStaticScene::OptionalCopyId`，投影与 RefreshLayoutPoints 共用），删除只要不低于最少数量即可；
`DemoWorld.py` 与 `LDMapGeometry.cpp` 同样检查前缀与类别一致、数量范围、单胶囊合法站位，以及各自作用圆
（小迷你旗 `OBJECTIVE.FlagRadiusCm`、外卖柜 `EXPLORE.LockerRadiusCm`、售货机 `AIRDROP.PickupRadiusCm`）落在合法地面且不进入营地清空圈。
内容另检查 `EXPLORE.LockerCount` 不超过外卖柜点数、`LIFE.SpawnChoiceCount` 不超过出生点数。
外卖柜与小迷你旗点不进入 AI 公开地点或静态小地图（外卖柜只在本人看见后显示，小迷你旗只在开放或预告时显示）；售货机公开显示在小地图上。

`camps.placements.monster_id`是固定档位代表，运行按该定义的tier聚合三种候选，不是本次生成结果。
同档必须具有相同成员数量、奖励与刷新时间。`FLDCampGroupState`保存本次选种与准备中的选种，
SpawnBlocked重试保留抽签；整组生命提交后才切换当前种类。公开POI保留档位代表，动态CreatureId只经合法观察发布。
移动营地仍通过原有语义锚点/home点，模型替换不改档位。`BootstrapAssets.ps1 -Mode RefreshLayoutPoints`导入内容/世界定义，只为批准的新成员与新增定位点（含 `mini_flag_*`、`locker_*`、`vending_*`、`pk_signup_*`、`pk_spawn_*`、`pk_throw`）新增语义点，
保留现存点和人工布景；现存必需点缺失、重复身份或成员落点重叠会拒绝，不能用此入口重建被删除的点。
该定向同步不刷新碰撞、导航或美术，也不代表完整Bootstrap通过；新增成员的实际胶囊落地及接线由拥有连接营地场景验证。
普通布景不参与成员数量或档位判定；全场景源快照导出对未注册静态碰撞的限制仍存在。

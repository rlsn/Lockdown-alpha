# 世界、资产与数据管线

| 项 | 内容 |
| --- | --- |
| 规则 | [App Spec](../APP_SPEC.md) §1.1、§5；数值只在[内容与数值](../demo/CONTENT_AND_BALANCE.md)；关卡编辑约束见[关卡创作自由与开发约束](../LEVEL_AUTHORING_CONTRACT.md) |
| 文件 | `SourceData\Demo\content.json`、`SourceData\World\demo_world.json`；`Scripts\SyncDemoContentData.py`、`SyncDemoWorldData`、`SyncDemoMap.ps1`、`ApplyDemoMapEdits.py`、`CatalogTools.py`、`DemoContent.py`、`BootstrapAssets.ps1`；`Region\LDMapRegistry`、`ULDMapDefinition`、`Navigation\LDDemoNavigationSystem`、`World\LDCampLifecycle`、`LDCampDecision`、`LDCampDirector`、`LDCaptureCircle`；`Source\LockdownEditor\...\LDDemoWorldBuilder.cpp` |
| 入口函数 | `ULDMapRegistry::QueryGround` / `QueryCompletePath`、`FindApproach`、`TraceSight`、`QueryCircleMembership` / `ExtractCircleCrossings`、`QueryCombatContacts`；`ALDCampDirector::OnCombatHostileContact` / `AdvanceCamps` / `FreezeForRest` / `ThawAfterRest`；`ULDDemoContentCatalog` 快照 |
| 不变量 | 一张地图、一个旗杆；SourceData 是生成输入、Content 是游戏读取的载荷，docs 不是构建输入；载荷只经 Sync 生成，不手改哈希；查询不授予观察或交易权限；营地不另存身体、生命、战斗或奖励真值；出生锚点等概率有放回抽样 |
| 检查 | 对应 Sync `--check` 与 `Test*Import.py`、`BootstrapAssets.ps1 -Mode Validate`、`Lockdown.Rules.DemoWorld.*`、`StaticScene.*`、`DemoCamp.*`；场景 `RunDemoWorldRoomCheck.ps1`、`RunDemoCampCheck.ps1` |

本手册保留当前地形、对象和生成链的维护知识，不要求 Demo 继承完整城市、人口或环境玩法。

## 标准关卡编辑与同步

正式关卡 `L_Demo` 在 UE 编辑器中直接编辑保存：普通静态网格体、装饰、材质和灯光保存即可 Play，碰撞、地面与视线直接读取当前关卡原生物理体。`Scripts\SyncDemoMap.ps1` 将保存地图中的 `LDSceneAnchor:<id>` 布局点投影进 `SourceData\World\demo_world.json`。

| 位置 | 角色 |
| --- | --- |
| `docs` | 人和 AI 阅读的产品与技术说明，不作为构建输入 |
| `SourceData` | 脚本与原生导入消费的 JSON、schema、稳定 ID 映射与素材清单 |
| `Content` 与 `*Contract.h` | 游戏读取的载荷、原生资产及字节摘要契约 |

`CatalogTools.py` 提供确定性编码与原生 ID 校验；源数据按 UTF-8 读取，保留原始换行以保证 SHA-256 一致。`native_ids.json` 的 `retired_ranges` 锁定退役编号，严禁重排或复用。

## Demo 内容与世界编译合同

保存地图中的野外 `PlayerStart` 提供合法出生池，每点使用唯一 `PlayerStartTag`。开局、复活候选与返场备用位置共用该池，按实际支撑、角色净空和通往旗杆的导航路径校验。

`LDSceneAuthoringLibrary::GameplayTraversalAudit` 在编辑器中读取实际碰撞、原生导航和角色胶囊，检查出入口、通道宽度、碰撞外扩和合法脚点路径。保存地图的回归入口是 `Lockdown.Rules.StaticScene.MapTraversal`。单出口区域计入诊断。窄通道以实际角色胶囊跨过导航连接处的结果为准。营地成员须具备原生地面支撑和落点净空，通往营地中心与旗杆的路径及失败坐标由营地审计单独记录。旗杆与出生点的路径仍是开局必要条件。

公共休息室、野外与终局战场共用唯一地面与连接：
- **休息室与 PK 定位点**（不进入野外随机出生池）：`rest_spawn_0..7`、`rest_service_1..3`（休息室布局与可达路径参考，服务本身均在工坊台内提交）、`training_dummy_0..7`（每位参赛者生成真实共享假人，可复制额外通用假人；总观察单位上限 86）、`pk_signup_0..7`（八点均值即报名圈圆心）、`pk_spawn_0..7`（每 45° 一个，相邻至少 950 cm）、`pk_throw`。
- **终局定位点**：`final_spawn_0..7`（同层等距 45° 分布，亦作为持有者 `final_private_pole` 的圆心锚点）与 `final_capture`。
- **两类可选锚点**（支持作者移动、复制与不低于下限的删除，复制 ID 为 `<原id>_copy_<8位hex>`）：`mini_flag_*`（3–16 个）与 `locker_*`（6–32 个），作用圆须落在合法地面且避开营地净空圈，仅在事件预告/出现期间显示在小地图上。

改动世界几何与内容后依序执行：`SyncDemoWorldData.py` → `SyncDemoContentData.py` → `BuildEditor.ps1` → `BootstrapAssets.ps1 -Mode Regenerate`（纯数值/文案改动用 `-Mode RefreshDefinitions`；只补新增语义锚点用 `-Mode RefreshLayoutPoints`）。

| 边界 | 编译器（`DemoContent.py` / `DemoWorld.py`）行为 |
| --- | --- |
| 严格解析 | `Decimal` 词法读取，拒绝重复键、未知字段/op/ID、错量纲、bool 冒充整数、NaN/Inf 与超精度小数；大整数不经 JSON double 往返 |
| 定点投影 | P2 → `{unit, p2}`；比例 → `{unit, bp}`；秒 → `{unit: "ms", ms}`；距离/速率 → 约分有理数 `{numerator, denominator}` |
| 完整闭合 | 角色基值、26 项三级分支主动、五维属性与 18 点关键石、73 张构筑牌 + 5 枚勋章、8 项终局改装、四档 12 种野怪与 Boss、AI 参数及显示表全部进入不可变快照 |
| 产物核对 | 必须读取真实 `world.json` 与 `LDDemoWorldContract.h` 字节，输出 `demo_content.json`、`manifest.json` 与 `LDDemoContentContract.h` |

`camps.placements.monster_id` 仅代表营地固定档位；运行时 `FLDCampSpec::Pools` 按档位聚合 3 种候选并在每次整组生成时独立抽样（`SpawnBlocked` 重试保留原抽签，提交后才切换种类）。

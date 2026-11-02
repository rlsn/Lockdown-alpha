# 世界、资产与数据管线

本手册保留当前地形、对象和生成链的维护知识，不要求 Demo 继承完整城市、人口或环境玩法。
地图规模与必需地点由 [App Spec](../APP_SPEC.md) 决定。

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
| `SourceData\Demo\content.json`、`SourceData\World\demo_world.json` 与两份 schema | 唯一 Demo world→content 生成链及原生 Bootstrap；不是旧 prototype 导入入口 |
| 尚未退出的 `SourceData\Catalogs\V07` + 对应 SyncV07 入口 | 仍有实际消费者的战斗/装备/获取载荷与 contract；旧 WorldCatalog 生成链及其 v07_rules 输入已随世界闭包删除 |
| `SourceData\Catalogs\V08` + `SyncB84Data.py` | 三个 `*_b84.json`、B84Manifest、`LDB84Contract.h` |

`CatalogTools.py` 复用 schema、确定性编码与原生 ID 校验，B84 生成不依赖历史说明或档案 ZIP。
schema8/schema9、V07/V08/V09、Bxx 命名是当前技术边界，不是产品版本 `0.10.0`。

源数据按 UTF-8 读取；涉及按字节计算的 SHA/MD5 时保留原始编码和换行。
`native_ids.json` 保留稳定原生编号，不通过重排枚举自动“修正”已有 ID。
`--check` 比较现有载荷与生成结果而不写文件；不一致应定位输入/生成链，不能删除断言或重写期望值。

## Demo 内容编译合同

`Scripts\DemoContent.py` 与 `Scripts\SyncDemoContentData.py` 提供独立于旧目录的严格作者校验、
定点投影和根身份生成。`SourceData\Demo\content.schema.json`、`native_ids.json` 以及
`Public\Development\LDDemoContentIds.h` 只定义格式与编号保留，不批准候选技能或任何数值。
旧技能编号 0–61、旧物品编号 0–98 不得复用；新编号必须同时匹配作者映射与原生声明。

生产输入必须是经审查的 `SourceData\Demo\content.json`，其中
`identity.approval.status` 为 `approved`，`decision` 引用实际审批决定。
这个字段记录审批，不是编译器授予审批。没有已批准内容或真实世界产物时明确拒绝，
不创建 `content.json`、不从旧目录补值，也没有 candidate/fixture 运行开关。
`Scripts\DemoContentTestFixtures.py` 仅在 unittest 的 `Artifacts` 临时根提供合成机制夹具；
其审批形状的测试元数据、数值和几何均不能作为产品输入。

| 边界 | 当前编译器行为 |
| --- | --- |
| 读取与 schema | UTF-8；小数从原文直接读为 Decimal，拒绝重复键、未知字段/操作/ID、错误单位/等级/引用、bool 整数、非有限值和不可表示精度 |
| 数值投影 | P2 → `{unit, p2}` 百分之一点；R4 → `{unit, bp}`；秒 → `{unit: "ms", ms}`；距离/速率 → `{unit, numerator, denominator}` 的约分 int64 有理数；计数/金币/XP 为 int64 |
| 完整定义 | 角色参数、三份完整技能 rank/effects、属性奖励、两装备能力定义、价格/售价、XP 阈值、两档营地、AI 参数和显示引用均进入闭合树；没有透传的 ActiveJson 或丢弃坏字段 |
| 纯提供者 | `compile_content(root)` 返回不可变 Catalog、三份输出字节、输入快照和 32-byte 根摘要；`build_outputs(root)` 返回输出映射；Catalog 不拥有 Match、时钟、HP、金币或其他玩法状态 |
| 世界前置 | 必须读取真实 `Content\Lockdown\Data\Demo\world.json` 及 `Private\World\LDDemoWorldContract.h`，核对实际字节，不接受调用方单独给出的 digest 或空世界占位 |
| 输出 | 同时生成 `Content\Lockdown\Data\Demo\demo_content.json`、`manifest.json` 与 `Private\Development\LDDemoContentContract.h`；不生成世界文件或世界契约 |

大整数不得经过 JSON double 再取回。原生消费者需按整数 token/明确整数编码解析这些字段，
不能用 UE 的浮点 JSON 读取当作完整 typed importer。定义 ID 的窄 wire 为 64 字节：
最多 63 字节小写 ASCII、NUL 终止、余字节为零；目录 digest 为 32 原始字节，显示才用 64 位小写 hex。
纯 wire 编解码与格式拒绝在 `DemoContent.py` 中提供，不替代业务域对未知/退役 ID 的拒绝。

世界字节入口的完整检查见 `DemoWorld`：当前互换版本为 schema/projection 2，
包含 map_id、作者/schema 的真实字节摘要、批准记录、真实 surfaces/blockers 多边形及
十 Spawn、五 Camp、一 Shop、一 Capture 锚点、八个成员 home、七个局部落地偏移和五个入口连接；
坐标是有符号约分整数对，营地另有成员 home。检查支持面/坐标形状、ID、数量和内容引用，
**不证明胶囊净空、路径可达、碰撞或原生地图资产正确**，这些仍由世界生成器/Builder 负责。
世界作者校验有代码拥有的完整字段/类型/cm 单位约束；可编辑 schema 不能扩充原生支持集后
丢弃新字段，也不能将米伪装成厘米。内容根生成重新调用同一世界 producer 比较全部真实产物，
不能仅手写 world 和匹配摘要让正式链通过。

世界生成头依次包含 `#pragma once`、`LD_DEMO_WORLD_SCHEMA_VERSION`、
`LD_DEMO_WORLD_PROJECTION_REVISION` 和
`LD_DEMO_WORLD_PAYLOAD_SHA256 TEXT("…")`。根 manifest 覆盖内容、显示投影、作者 schema、
native_ids、原生编号声明、handler/projection 修订、世界子载荷和世界头的真实摘要。
所有摘要按文件完整字节计算，不规范化换行；manifest 自身不保存自身哈希。
根摘要由 `CatalogTools.encode` 编码规范树后求 SHA-256，写入内容头
`LD_DEMO_CATALOG_SHA256` / `LD_DEMO_CATALOG_DIGEST_BYTES`。
内容头还提供 `LD_DEMO_CONTENT_SCHEMA_VERSION`、`LD_DEMO_CONTENT_HANDLER_REVISION`、
`LD_DEMO_CONTENT_PROJECTION_REVISION`、`LD_DEMO_CONTENT_PAYLOAD_SHA256`。
这些 revision 描述投影合同，**不证明原生 gameplay handler 已实现**。

```powershell
python -B .\Scripts\SyncDemoContentData.py --check
python -B -m unittest discover -s .\Scripts -p "TestDemoContentImport.py"
```

`--check` 不生成或修复文件。生成模式需要 Windows 文件共享语义，不在其他平台退回不受保护的
“比较后替换”。生成器先取得排他事务标记，再对全部输入和三个输出持有拒写/拒删除的句柄，
包括本次无需重写的输出；已有写入者会使本次生成明确失败。取得保护后重新比较快照，
同时核对命名路径与句柄身份，避免编辑器临时文件替换或原地保存落入检查与写入之间。

全量暂存后，只通过这些受保护的句柄原地写入，manifest 最后写；回滚也使用同一组句柄，
不再用带竞态的 `os.replace` 恢复旧文件。写入后、备份清理后和事务标记移除后都核验全部
输入/输出；最后一次核验是成功提交点，此时保护尚未释放。检测到的冲突字节不被回滚覆盖。
这不是全局多文件原子事务：读者仍须验证根与子摘要，进程中断不能把混合代次当作有效目录。

`Content\Lockdown\Data\Demo\.compile.lock` 是 fsync 后的恢复记录，包含每个输出的原始字节
（Base64；原本不存在的文件明确为 null）、期望新字节、阶段和错误信息，不只是一个 PID。
备份目录部分或全部删除后，清理/解锁失败仍在保护区内回滚，完整原件仍可从记录恢复。
无法重新取得主标记时，在 `Artifacts\DemoContent\recovery-*.json` 持久化同样的证据。
未解决的记录会阻止重新生成和 `--check`，即使当前输出恰好与期望字节相同也不能走 no-op 绕过。
`RecoveryRequired` 要求检查实际字节并人工解决，不能直接删标记重试。
提交点之后若仅句柄收尾失败，错误明确携带 `committed=True`，保留原件，但不再对已解除保护的
文件冒险回滚；如果连恢复证据也无法持久化则明确报 `RecoveryPersistenceFailed`，不谎称原件已保存。

`verify_runtime_files(root, expected_digest)` 只验证当前根、子文件和生成头的字节一致性。
这里的 root 是包含生成头的工作区；包内原生校验应使用编译入的头常量，
不能因此把 SourceData、脚本或源码头加入运行包。
它不读取 docs 或旧目录，也不证明审批、原生 handler、DataAsset、Bootstrap 或可玩状态；
唯一 `ULDDemoContentCatalog` 已实现下述原生导入、精确资产投影校验及入口/Bootstrap 消费；
玩法 handler 的字段消费、正式资产生成/Cook 与实际世界生成仍须分别接线和验证。

## 资产导入与打包

`LDScaffoldBootstrapCommandlet` 的公开入口已收敛到 Demo：
先验证编入程序的根/子载荷 SHA-256，再导入或校验唯一 `DA_DemoContent`、`DA_DemoWorld`、
前端、`L_Demo` 和 WBP 根。纯载荷检查不要求尚待生成的 DataAsset；正常运行入口仍要求原生资产校验，
不能用生成前置检查代替游戏准入。
Validate 检查合并后的默认地图/GameMode/GameInstance、Blueprint/CDO、唯一 MapAnchor、
十个唯一出生 ID、全部 typed 字段、精确凸多边形碰撞、导航 agent 和地点连通。
它不把资产存在当作导航、战斗或真实联机验收。

`LDDemoWorldBuilder` 已复用原生 mesh/nav/保存能力，生成唯一平地、十个真实多边形阻挡、
十个出生标记与一个原生导航区域。旋转墙不是 AABB 近似，商店台面阻走但不阻视线。
保存校验同时要求地面/阻挡保持 WorldStatic object type 和 Pawn 阻挡响应，
并逐项核对凸元局部 Transform 为 Identity 及全部顶点，不能只看 Actor transform、
原始 VertexData、导航或 LOS。专用原生规则使用隔离包真实保存、卸载和重载，
覆盖仅改 Pawn 响应、object type 和凸元局部平移的损坏，不修改生产资产。
`ULDMapDefinition` 只导入根合同约束的 Demo 世界；保存投影逐字段精确回核，失败不先清空资产。
旧城市/河流字段不属于 Demo，不能携带非空旧数据通过 Demo 资产校验；
其反射字段与存量包的移除仍与集成的资产审计/重存同步，不保留旧 map JSON 导入或独立 B90 Builder/脚本入口。
Generate/Regenerate 不再通过这个入口生成旧区域、职业目录、固定旧输入或旧地图。
保留的区域/完整地图 Builder 组件须按世界闭包复用或删除；旧 SourceData 和 native loader
的剩余消费者并未因此全部退出，不能只删文件而不迁移对应 contract/staging。
`.uasset`/`.umap` 仍通过 Unreal 和 Git LFS 管理，不以文本拼接修改。

`Lockdown.Build.cs` 精确声明三个 Demo JSON 的 UFS staging 和两份生成头的外部构建依赖；
字节参与摘要的 Demo JSON、生成头及原生编号头有精确 `-text` 规则，不经 Git 换行转换。
它仍声明待迁移组件的旧运行载荷依赖；最终 Demo 的 cook/staging 闭包尚未完成。
`PackageWin64.ps1` 在打包前做 Demo compiler 检查和 Bootstrap Validate，
只选择前端与 Demo map set，从项目设置读取产品版本，并记录源输入与包文件摘要。
缺少实际 Demo 输入或原生世界资产时不能进入旧图打包路径。
SourceData 不应整目录加入运行时打包，运行程序不需要 docs 才能启动。

### Demo 原生不可变内容目录

`Development\LDDemoContentCatalog.h` 的 `ULDDemoContentCatalog` 是 Demo 内容导入提供者；
固定资产路径为 `/Game/Lockdown/Generated/Data/DA_DemoContent.DA_DemoContent`。
它只保存定义，不保存 Match、角色生命、HP/MP、钱包、选择或冷却状态，也不启动对局。
原生编号来自 `LDDemoContentIds.h` 的保留编号；能解析某个 opcode **不代表战斗处理器已接通**。

作者链为 `Scripts\DemoContent.py` / `SyncDemoContentData.py`：
`SourceData\Demo\content.json`、schema 与 native_ids，加上世界载荷/contract，
生成 `Content\Lockdown\Data\Demo\demo_content.json`、`manifest.json` 和
`Source\Lockdown\Private\Development\LDDemoContentContract.h`。
这里的原生实现不创建正式 content、world、manifest、contract 或 `.uasset`，
也不通过编译器夹具批准候选值。只有实际审批和相应生成/资产接线完成后才能启用正式内容。

| API | 输入、结果与失败边界 |
| --- | --- |
| `FLDDemoContentDigest::ParseHex` | 64 个小写十六进制字符转为 32 raw bytes；不接受大小写别名、截断或额外字符 |
| `FLDDemoContentBundle::ReadImportFiles` | 从指定工程根读取 content、manifest、world、两份生成 contract、内容 schema、native_ids、原生 ID 头及世界作者/schema，共十份原始字节；缺失/空文件/超过 8 MiB 拒绝，输出参数不变 |
| `PrepareImport` / `PrepareImportFromFiles` | 传入目标资产及独立可信的当前根摘要，例如实际生成 contract 的 `LD_DEMO_CATALOG_SHA256`；不能先计算待导入 manifest 的摘要再把它当作生产准入依据。全部校验成功才产生绑定目标及导入代次的不透明 `FLDDemoPreparedContent` |
| `CommitImport` | Game thread 上核对目标、导入代次、当前可信根及文件快照后消费一次 token，一次安装完整字节与 typed definitions；失败导入不先清空原资产。保存包仍由编辑器/Bootstrap 所有者负责 |
| `ValidatePrepared` | 对照当前 freshly prepared 定义，比较全部保存字节及全部反射属性；包括每个 rank/effect、可选能力的存在性和数组，不只比较 checksum |
| `Validate(ExpectedRoot)` / `Read(ExpectedRoot)` | 重新严格解析保存字节并比较完整投影。只有相符时才返回 `TSharedPtr<const FLDDemoContentSnapshot>`；旧快照保持不可变，旧资产不因以前导入成功而获得新根的有效性 |
| `ReadRuntimeFiles` / `Load` | 当前 runtime data 目录必须同时存在并匹配 manifest/content/world；`Load` 使用固定资产根和 `Content\Lockdown\Data\Demo`。不在运行时读 SourceData/源码、不采用旧目录或存储字节代替缺失的当前载荷 |

根摘要是完整规范 manifest **文件字节**的 SHA-256，manifest 内没有自哈希。
原生加载核对内容 schema1/handler1/projection2、世界 schema2/projection2、每个子文件摘要、显示子树摘要、
native ID 映射与编译枚举、world contract 的原始字节摘要及其 payload/revision。
作者 schema/原生头的摘要是由可信根固定的来源字节身份；实际可接受字段/类型/操作由
原生投影代码再次关闭校验，不在游戏中执行作者 JSON Schema 或源码。
准备 token 绑定目标对象身份、导入代次及可信根；文件准备还冻结全部输入字节。
提交前在拒写的读取句柄范围内重查文件，旧准备不能覆盖更新导入，也不能换目标或根继续使用。
保存投影比较递归检查字符串的精确内容，不依赖默认不区分大小写的 FString 反射相等性。

`LDDemoContentDefinitions.h` 的成员区分 P2、R4 basis points、毫秒及约分有理数；
参数、公式/政策枚举、角色、完整三级效果、属性、装备、经验、营地和显示定义均有原生成员。
`Snapshot.Get()` 和 `FindAbility/FindAttribute/FindEquipment/FindMonster/FindDisplay`
提供只读消费；查找失败给出错误，不返回伪造的默认定义。世界几何仅用于检查已编译的
边界、锚点、成员和引用；该目录不创建另一套地图 Actor/导航/营地状态。

UE 5.8 的 `StoreNumbersAsStrings` 保留 `EJson::Number` 及其数字词法。
导入先拒绝重复键、坏 UTF-8/Unicode、非整数词法、超 int64 和非规范编码，
再从词法读取整数；不经 `double` 取回大整数。每个字段再验证单位、范围、
ID/opcode/rank/引用和多余字段，复算 XP、出售额、攻击间隔、感知半径与构筑上界。
导入期有理数运算使用带位宽预检的 UE `TBigInt` 暂存；不是另一套游戏伤害计算。
共享 `FNumbers` 按符号和正幅值比较，避开锁定 UE 负数关系运算的错误/递归，不修改共享引擎。
几何负坐标、正负分数和 int64 边界由原生专用规则覆盖。

`RunTests.ps1` 调用 `ExportDemoNativeFixtures.py --output <全新 Artifacts 子目录>`，
只导入现有测试夹具及唯一 compiler，绝不写 live SourceData/Content。
向原生自动化传 `-LDDemoContentFixtures="<该目录>"`，执行
`Lockdown.Rules.DemoContent.TypedProjection`、`.StrictRefusal`、`.AtomicAssetRoundTrip`。
缺少夹具会失败而非跳过。覆盖全部反射字段的独立断言、每个 rank 数量字段变异、
完整保存投影逐字段损坏拒绝、内存 DataAsset 序列化、失败保留与当前文件/根拒旧。
这些证明导入和定义一致性，不证明真实 `.uasset` 保存/Cook、图标资源解析、地图生成、
战斗公式执行或完整多人对局。正常入口在可信编译摘要下调用 runtime `Load`，
Bootstrap 另与 freshly prepared 作者输入逐字段比较；缺少真实资产不能由夹具补位。

## 世界组件

`ULDMapDefinition`、`LDMapRegistry` 与编辑器 Builder 提供唯一 Demo 世界的
typed 投影、稳定地址、原生地面、完整路径及接近点查询。
`ALDRegionNavBounds` 仍由 `LDDemoWorldBuilder` 生成和校验，不能随旧 Region 名称误删。
旧区域导演、哨兵、WatchControl、全图预览与 `LDMapApproaches` 不再提供运行入口；
旧 RegionDefinition/Anchor 与已保存地图的反射迁移由同一次资产闭包处理。

`FLDWorldRevision` 只含 MapId/LayoutRevision/GeometryRevision，不另分配 Match 身份。
`ULDMapRegistry::QueryGround` 要求显式注册 SurfaceHint、当前世界修订和正确 capsule；
返回局部实际支持面，不把高处输入向下吸附。`QueryCompletePath` 重新验证端点，
拒绝 partial 和未声明的 XY 投影，保留路径长度与实际投影量；这些查询不授予交易或观察权限。
`ULDDemoNavigationSystem` 仍使用同一个原生 `UNavigationSystemV1`/Recast，
在导航数据注册前用中央内容半径和现有身体高度配置该世界，不在注册后改 agent 或复制数值到 ini。
角色的 Demo 分支消费同一 capsule/基础移动速度，不从旧 PrototypeTuning 补 Demo 参数。
完整 CMC 写许可/实际子段、稀疏观察和拥有客户端场景仍须继续接线验证。
`RunDemoWorldRoomCheck.ps1` 已覆盖三个实际真人 Lobby 进程的保存世界加载、统一根/名单、
本地拥有连接和原生 shop 路径；其零 Pawn 断言确保未接通玩法没有被提前激活，
不把这一场景当作已完成 AI、实际身体移动或完整多人对局。

`LDGroundQueries` 的静态地面与平面可达性能力仍由核心维护，不是观察权限、完整路径或
带世界修订/SurfaceHint/capsule 的 Demo 落地证明。正式路径与落地使用上述 Registry 查询，
观察使用单一 `TraceSight`/Observer 边界，不以 `ClearSegment` 代替。

定义中的地址与视觉 Actor 不是同一身份；重复导入、世界重载不能重复生成地点、奖励源或权威记录。
新地图先验证合法出生、地点连通、入口与阻挡，再接入交互。

## 营地与占领的复用边界

营地追击、回位、活动范围和稳定选敌保留为参数化纯决策 `LDCampDecision`，不读取 Actor/World 全局，
也不创建另一份身体、战斗或奖励状态。调用方须从批准内容、地图注册表和新观测服务提供输入。
`TraceSight` 只判定静态遮挡：它按 `VISION.RadiusCm` 构建，不是营地感知半径；
其 InvalidPoint/StaleWorld 由调用层作为故障上抛。纯决策只从 N03 记录的、实际攻击过本营的参赛者
身体中选敌，营地成员和未攻击本营的旁观者即使更近也不选；目标还须在该怪物
`PerceptionRadiusCm`（内容规定等于 LeashRadiusCm，从成员当前脚点量，含边界）以内。
感知半径大于 TraceSight 半径时明确失败；不保留旧 Blind 缩半径或 200/650/1000 默认值。
活动范围只按成员自身到 Camp 锚点的 XY 距离判定，不按目标位置过滤：站在 Leash 圈外但在感知内的
攻击者仍可选；成员严格超界才转 Returning，之后不再选敌、受击也不回追。
回位目标是该成员在注册表中的 MemberHome，不是全组共用 Camp 中心。
观察读取失败必须在调用层按权威故障处理，纯函数也拒绝未完成的读取，不能当空候选成功。
N03 的五营地、真实身体、反击/回位重置、刷新与死亡奖励仍需正式接线，纯原语不是完整营地。
旧营地 Actor、职业/技能初始包、三营地/有限刷新预算、Boss/Tower/圣坛与探索奖励不作备用实现。

唯一占领归约和结果所有者是现有 GameMode 核心，不再保留独立 Objective Actor、HeartSensor、
Seal/路线/hold/deadline 或 Claim fallback。复用的是恰一次结果与先关写闸再清理/发布的语义；
现有 phase/GateRevision/借用 scope 不等于完整 `TryCloseMatch`、结果 CAS 或圈 reducer 已实现。

## 保存与复位边界

世界不提供持久检查点、自动保存或恢复入口。物品身份、消耗与原子提交由当前库存/账本维护，
不通过旧世界文件或已退役的物件服务重建和补发。
新局和新身体的隔离仍走 GameMode 的 Match/Life 与各领域拒旧协议，见
[架构与网络](ARCHITECTURE_AND_NETWORKING.md#请求代次与回执)；这不等于完整 Demo 新局流程已经验收。

本地按键与镜头偏好仍由 `ULDControlSettings::Save/Load` 管理，见
[移动、输入与镜头](MOVEMENT_INPUT_AND_CAMERA.md#相机与小地图)，不保存局内战力或世界状态。

## 检查入口

对应 Sync `--check`、`Test*Import.py`、Bootstrap Validate 先验证生成链。
几何与导航使用当前 `Lockdown.Rules.DemoWorld` 和 Bootstrap 校验；
观察的有界编码、隐私、句柄和遮挡使用 `Lockdown.Rules.DemoVision.*`。
真实拥有连接使用 `RunDemoWorldRoomCheck.ps1` 与相应核心/后续 N03 场景。
旧 B90/B91、World/Objective/Region probe 的授予、改时钟、重进旧图或独立恢复进程
不作为 Demo 验收入口；未完成的 N03/N02 与真实拓扑断言仍需在对应所有者接通。
模型/数据检查、原生路径、实际运动、限制客户端表现分别证明不同事情。

工作树可能共享 LFS 缓存，不能把当前工作树的 Git 元数据目录拼成固定 `lfs\objects` 路径。
`VerifyCleanCheckout.ps1` 通过 `git lfs env` 的 `LocalMediaDir` 定位实际缓存，
复制到隔离检出后再验证；这与从远端下载验证是不同的边界。
验证检出使用 `Artifacts` 下的短目录，避免长工作树路径触发 UBT 的 Windows action 路径长度限制。

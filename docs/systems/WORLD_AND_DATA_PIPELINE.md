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
| `SourceData\Demo\content.json`、保存的 `L_Demo`、派生的 `SourceData\World\demo_world.json` 与两份 schema | 唯一 Demo 关卡→world→content 同步链及原生 Bootstrap |

`CatalogTools.py` 提供 Demo 生成器复用的确定性编码与原生 ID 校验。
schema8/schema9、V07/V08/V09、Bxx 命名是当前技术边界，不是产品版本 `0.11.0`。

源数据按 UTF-8 读取；涉及按字节计算的 SHA/MD5 时保留原始编码和换行。
`native_ids.json` 保留稳定原生编号，不通过重排枚举自动“修正”已有 ID。
`--check` 比较现有载荷与生成结果而不写文件；不一致应定位输入/生成链，不能删除断言或重写期望值。

## Demo 内容编译合同

公共休息室与野外共用唯一地面与同一连接：地面向东延伸，野外东墙是隔墙，休息室四面封闭。
`RestSpawn` 的 `rest_spawn_0..5` 只供阶段放置，不加入野外随机出生池；
`RestService` 的 `rest_service_1..3` 分别为技能商人、构件工匠、训练师，位于 y=-500 cm、x=7100/7500/7900 cm；
服务交互仍复用 `AIRDROP.PickupRadiusCm` 的150 cm范围。`TrainingDummy` 的六个 `training_dummy_0..5` 位于北侧
y=700 cm，间距400 cm；各参赛者在对应锚点生成真实共享假人，由获准观察代理同步实际位置、生命与状态，地面边界另作公共展示；
训练锚点不进入野外 POI 或小地图地点。六个 `PKSignup` 锚点是公共报名圈里的不同站位，报名范围复用 `AIRDROP.PickupRadiusCm`。
Python/native importer 都识别新增锚点，服务范围、假人偏移与出生候选仍走实际胶囊净空检查。休息出生候选还须有到 `rest_service_1`
的完整室内原生路径；野外和 PK 出生仍以旗杆为路径参考。Bootstrap 校验每个休息出生偏移到该服务的路径。
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
| 完整定义 | 角色参数、单级主动的完整数值/effects 与开局池标记、五维属性每点加成、成长点数参数、XP 阈值与主动机会等级、三档营地、AI 参数和显示引用均进入闭合树；没有透传的 ActiveJson 或丢弃坏字段 |
| 纯提供者 | `compile_content(root)` 返回不可变 Catalog、三份输出字节、输入快照和 32-byte 根摘要；`build_outputs(root)` 返回输出映射；Catalog 不拥有 Match、时钟、HP、XP 或其他玩法状态 |
| 世界前置 | 必须读取真实 `Content\Lockdown\Data\Demo\world.json` 及 `Private\World\LDDemoWorldContract.h`，核对实际字节，不接受调用方单独给出的 digest 或空世界占位 |
| 输出 | 同时生成 `Content\Lockdown\Data\Demo\demo_content.json`、`manifest.json` 与 `Private\Development\LDDemoContentContract.h`；不生成世界文件或世界契约 |

大整数不得经过 JSON double 再取回。原生消费者需按整数 token/明确整数编码解析这些字段，
不能用 UE 的浮点 JSON 读取当作完整 typed importer。定义 ID 的窄 wire 为 64 字节：
最多 63 字节小写 ASCII、NUL 终止、余字节为零；目录 digest 为 32 原始字节，显示才用 64 位小写 hex。
纯 wire 编解码与格式拒绝在 `DemoContent.py` 中提供，不替代业务域对未知/退役 ID 的拒绝。

世界字节入口的完整检查见 `DemoWorld`：当前互换版本为 schema/projection 2，
包含 map_id、作者/schema 的真实字节摘要、批准记录、真实 surfaces/blockers 多边形及
Spawn、Camp、Capture、Dorm、Airdrop、BossWaypoint、PK 和休息室锚点、营地成员 home 与七个局部落地偏移；
坐标是有符号约分整数对，营地另有成员 home。检查支持面/坐标形状、ID、数量和内容引用，
**不证明胶囊净空、路径可达、碰撞或原生地图资产正确**，这些仍由世界生成器/Builder 负责。
世界作者校验有代码拥有的完整字段/类型/cm 单位约束；可编辑 schema 不能扩充原生支持集后
丢弃新字段，也不能将米伪装成厘米。内容根生成重新调用同一世界 producer 比较全部真实产物，
不能仅手写 world 和匹配摘要让正式链通过。

PK 有两类锚点：`PKSpawn` 固定为 `pk_spawn_0..5`，
`PKThrow` 固定为 `pk_throw`，内容 `world.pk_spawn_ids`/`world.pk_throw_id` 按序引用，
原生为 `FLDDemoWorldReferences::PKSpawnIds`（索引序）与 `PKThrowId`，用 `ULDMapRegistry::Resolve` 取位置/支持面。
`DemoWorld.py` 与 `LDMapGeometry.cpp` 同样检查：六个 PK 出生点与 capture 同层、距其1000–1400cm、两两≥1000cm、
按 id 逆时针每步60°±1°；投掷锚点同层且距 capture 在 (1800, 2200] cm（4000cm 射程覆盖1800cm初始场）；
全部为单胶囊合法站位且不在营地清空圈内。`SelectSpawnAnchor` 接受 PK 出生点（与出生点同一胶囊/落地/占位规则），
`DrawSpawnAnchor` 只抽 `Spawn`；AI 公开地点与小地图不把 PK 锚点当野外地点，美术摆放只为其留出一具身体的净空。
场地内的可走路径不由 Python 证明。

站圈报名使用 `PKSignup` 的六个站位 `pk_signup_0..5`，共同圆心为六站位的平均位置；所有玩家使用相同的 `AIRDROP.PickupRadiusCm` 圆盘。
`DemoWorld.py` 与 `LDMapGeometry.cpp` 校验站位身份、同层、站位互相及对圆边的胶囊净空、圆盘合法地面、出生偏移与营地净空；Python 另检查休息位到站位的直线路径。
`ULDMapRegistry::SharedSignupCentre` 给服务器报名和客户端标记提供同一个圆心；AI 走向本人槽位的站位，避免全部挤在圆心。
AI 野外公开地点、小地图与美术摆放跳过这些锚点。

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
唯一 `ULDDemoContentCatalog` 实现下述原生导入、精确资产投影校验及入口/Bootstrap 消费，
各玩法领域经它读取字段；字段消费、资产生成/Cook 与世界生成分别由各领域规则、Bootstrap 和打包版场景验证。

## 资产导入与打包

`LDScaffoldBootstrapCommandlet` 的公开入口已收敛到 Demo：
先验证编入程序的根/子载荷 SHA-256，再导入或校验唯一 `DA_DemoContent`、`DA_DemoWorld`、
前端、`L_Demo` 和 WBP 根。纯载荷检查不要求尚待生成的 DataAsset；正常运行入口仍要求原生资产校验，
不能用生成前置检查代替游戏准入。
Validate 检查合并后的默认地图/GameMode/GameInstance、Blueprint/CDO、唯一 MapAnchor、
与内容引用一一对应的唯一出生 ID、全部 typed 字段、精确凸多边形碰撞、导航 agent，以及逐元素的功能检查
（墙/围栏/树按声明阻挡移动与视线、坡道可走、悬崖不可走、锚点站位合法）；不检查当前布局的地点间连通或路线长度。
它不把资产存在当作导航、战斗或真实联机验收。

`LDDemoWorldBuilder` 复用原生 mesh/nav/保存能力，从关卡派生数据刷新唯一平地、高台与坡道、全部真实阻挡的碰撞载荷、
每个出生锚点一个出生标记与一个原生导航区域。锚点数量随作者世界变化，范围在 `LDMapGeometry.h`
（`MaxAnchors`、`MinSpawns`/`MaxSpawns`、`MaxCamps`、`MaxDorms`、`MaxAirdrops`，Boss 巡逻点固定 4 个），
阻挡上限 `MaxBlockers` = 2048（与身体同粗的树成片时需要一千多个圆柱）；`DemoWorld.py` 的 `ANCHOR_COUNTS`/`MAX_BLOCKERS`
与两份 schema 保持同一范围。同尺寸的树共用一个树干 mesh（`SM_Demo_trunk_r<半径>_h<高度>`，放在各自圆心），
墙、围栏与地形仍各有自己的精确凸 mesh。

`ULDMapRegistry::Rebuild` 缓存每块地形的原生物理包围盒、批准胶囊、step height 和静态查询的 ignored actors；
`QueryGround` 先做保守 AABB 预筛，仍由原有 sweep、静态 overlap 和候选 floor 的窄相位给出结果与拒绝文本。
`QueryBodyGround` 只供已经解析为当前存活身体的调用者复用几何结果，键包含 match、完整身体引用、
营地已完成的 reset epoch、精确脚点和 surface；身体胶囊按该局不可变内容计算一次，仍与注册胶囊核对。
独立 Boss 必须同时通过 Boss 身体身份和核心存活注册检查；它没有营地 reset 周期，使用 epoch 0。
死亡后的引用由核心解析拒绝，新生命、重开、reset 完成或 Rebuild（即使作者 revision 未变）不复用旧结果。
Demo 的查询相关静态碰撞来自保存的世界，局中不生成、移除或移动；动态身体与观测代理是 Pawn 且忽略地面通道，
空投、标记、尸体表现和装饰无碰撞，因此它们的变化不影响该缓存。未来若增加动态地面支持或可变 WorldStatic 几何，
必须先接入失效边界或恢复实时查询，不能仅凭作者 world revision 复用结果。

悬崖不靠高度挡人：生成器沿每条朝向更低平地的高台边与每条坡道的两侧放置隐形的薄挡块（`cliff_*`，
引擎立方体，2 cm 厚，高过最高层上整个胶囊），只阻挡 Pawn 与导航，对地面、视线、可见性和镜头通道都放行，
所以高处仍能隔着悬崖看见低处。Validate 按同一规则重算挡块并逐个核对位置与通道，
再在每段悬崖上方把胶囊抬过高侧横扫，确认被挡块拦下。所有生成几何 `CanCharacterStepUpOn = No`：
身体不能迈上任何东西，只能走上可走的坡面；`MaxStepHeight` 只是下坡顶时贴地跟随的距离，
`bMaintainHorizontalGroundVelocity` 让坡上水平速度与平地相同。旋转墙不是 AABB 近似。
场景材质、光照、天空、雾与后期都保存在关卡/原生资产中，可用标准编辑器调整；同步不重写材质图或重摆外观。
地面、坡道与阻挡仍使用经过校验的简单凸碰撞；树、墙、围栏的可见根网格与隐藏 `GameplayCollision`
组件一起保存在标准 `StaticMeshActor` 中。保存校验查询实际碰撞子组件，保留 WorldStatic、Pawn/地面/视线响应、
凸元局部 Transform 和顶点、胶囊横扫、眼高射线与导航检查，不把外观无碰撞误当成没有游戏阻挡。
铺地为独立纯视觉对象，不参与碰撞和导航；材质可以替换，不要求继续使用生成器的默认材质路径。
保存场景须有可用的太阳与实时天光，其他环境表现由关卡维护。
`ULDMapDefinition` 只导入根合同约束的 Demo 世界；保存投影逐字段精确回核，失败不先清空资产。
它只保留 SourceJson/Checksum 与导入、投影准备、校验和读取；旧城市/河流/路线字段、
旧 map JSON 导入和独立 B90 Builder/脚本入口已在资产引用审计后删除。
Generate/Regenerate 不生成旧区域、职业目录、固定旧输入或旧地图；
`SourceData` 只剩 Demo 内容与 Demo 世界两组作者输入。
`.uasset`/`.umap` 仍通过 Unreal 和 Git LFS 管理，不以文本拼接修改。

`Lockdown.Build.cs` 只声明三个 Demo JSON 的 UFS staging 和两份生成头的外部构建依赖，不再声明旧运行载荷；
字节参与摘要的 Demo JSON、生成头及原生编号头有精确 `-text` 规则，不经 Git 换行转换。
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
| `ValidatePrepared` | 对照当前 freshly prepared 定义，比较全部保存字节及全部反射属性；包括每个主动数值/effect、可选能力的存在性和数组，不只比较 checksum |
| `Validate(ExpectedRoot)` / `Read(ExpectedRoot)` | 重新严格解析保存字节并比较完整投影。只有相符时才返回 `TSharedPtr<const FLDDemoContentSnapshot>`；旧快照保持不可变，旧资产不因以前导入成功而获得新根的有效性 |
| `ReadRuntimeFiles` / `Load` | 当前 runtime data 目录必须同时存在并匹配 manifest/content/world；`Load` 使用固定资产根和 `Content\Lockdown\Data\Demo`。不在运行时读 SourceData/源码、不采用旧目录或存储字节代替缺失的当前载荷 |
| `ReadDevReloadFiles` / `LoadDevReload` / `SameDevReloadBase`（DEV） | 只用于调参热重载（`LDDemoEntry::IsDevContentReload`，`-LDDevContentReload`，未 cook 构建）。资产须先通过编译根的 `Validate`；再以 `ReadImportFiles` 读工程根下全部 Sync 输入（这是唯一在运行时读 SourceData/源码的路径），以磁盘 `manifest.json` 的哈希为根完整 `Project`。只有内容载荷、显示文本、根与审批字段可以与资产不同；世界字节与源文件、schema、原生 ID、contract 与各版本号任一不同即拒绝并说明需重新构建 |

根摘要是完整规范 manifest **文件字节**的 SHA-256，manifest 内没有自哈希。
原生加载核对内容 schema2/handler2/projection4、世界 schema3/projection3、每个子文件摘要、显示子树摘要、
native ID 映射与编译枚举、world contract 的原始字节摘要及其 payload/revision。
作者 schema/原生头的摘要是由可信根固定的来源字节身份；实际可接受字段/类型/操作由
原生投影代码再次关闭校验，不在游戏中执行作者 JSON Schema 或源码。
准备 token 绑定目标对象身份、导入代次及可信根；文件准备还冻结全部输入字节。
提交前在拒写的读取句柄范围内重查文件，旧准备不能覆盖更新导入，也不能换目标或根继续使用。
保存投影比较递归检查字符串的精确内容，不依赖默认不区分大小写的 FString 反射相等性。

`LDDemoContentDefinitions.h` 的成员区分 P2、R4 basis points、毫秒及约分有理数；
参数、公式/政策枚举、角色、单级主动（一组数值）的完整效果与卡面模板、五维属性、经验、营地和显示定义均有原生成员。
`Snapshot.Get()`、`FindAbility/FindStat/FindMonster/FindDisplay` 与按等级成长的 `FindValues`
提供只读消费；查找失败给出错误，不返回伪造的默认定义。世界几何仅用于检查已编译的
边界、锚点、成员和引用；该目录不创建另一套地图 Actor/导航/营地状态。

显示行另含玩家可见的 `name_text`/`description_text`（projection 4；主动说明以 `[op.field]`、五维说明以 `[per_point.<Stat>]` 读取 typed 数值）。编译器 `display_text` 与原生
`ValidateDisplayText` 使用同一约束：两项必填，名称至多 32、说明至多 160 个 Unicode 标量；拒绝控制、
不可见、双向及四字节字符、花括号和首尾空格；说明不得含数字，数值只由 typed 字段显示；名称在全部显示行中唯一。
`LDDemoContentText::Build` 从已验证定义生成 namespace `DemoContent` 的字符串表，键为
`<contentId>.name`/`<contentId>.description`。`LDDemoEntry::LoadRuntimeContent` 在
`ULDDemoContentCatalog::Load` 成功后以 `LDDemoContentText::TableId()` 注册该表，客户端与 Host 走同一路径；
同一进程重复注册相同文本为空操作，同 ID 已有不同条目则加载失败，不混用两套文本。

UE 5.8 的 `StoreNumbersAsStrings` 保留 `EJson::Number` 及其数字词法。
导入先拒绝重复键、坏 UTF-8/Unicode、非整数词法、超 int64 和非规范编码，
再从词法读取整数；不经 `double` 取回大整数。每个字段再验证单位、范围、
ID/opcode/引用和多余字段，复算 XP、出售额、攻击间隔、感知半径与构筑上界。
导入期有理数运算使用带位宽预检的 UE `TBigInt` 暂存；不是另一套游戏伤害计算。
共享 `FNumbers` 按符号和正幅值比较，避开锁定 UE 负数关系运算的错误/递归，不修改共享引擎。
几何负坐标、正负分数和 int64 边界由原生专用规则覆盖。

`RunTests.ps1` 调用
`ExportDemoNativeFixtures.py --cache Artifacts\DemoContentFixtureCache --profile <runtime|full>`：
按导出所读全部输入的哈希取缓存目录，逐个文件核对哈希后复用，输入或文件有任何变化就导出到新的临时目录，与导出实际写出的文件清单一致后再发布；
`--output <全新 Artifacts 子目录>` 仍可一次性导出。
普通规则使用 `runtime`，只写 `base`、`approved_base`、`exact_integer`、`world_spawn_shift`
和一个用于保持拒绝索引形状的轻量用例；
只有 5 个数据导入测试使用 `full` 的完整有效变异与拒绝矩阵。
只导入现有测试夹具及唯一 compiler，绝不写 live SourceData/Content；原生测试只读夹具，
需要改源文件的往返检查先把 base 用例复制到 `Intermediate` 下的临时目录。
向原生自动化传 `-LDDemoContentFixtures="<该目录>"`，执行
`Lockdown.Rules.DemoContent.TypedProjection`、`.StrictRefusal`、`.AtomicAssetRoundTrip`。
`DemoWorld.TypedProjection` 在同一次运行中直接取用 `DemoContent.TypedProjection` 对同一夹具字节导入出的内容快照，
单独运行时自行导入。
缺少夹具会失败而非跳过。覆盖全部反射字段的独立断言、每个主动数值字段变异、
完整保存投影逐字段损坏拒绝、内存 DataAsset 序列化、失败保留与当前文件/根拒旧。
这些证明导入和定义一致性，不证明真实 `.uasset` 保存/Cook、图标资源解析、地图生成、
战斗公式执行或完整多人对局。正常入口在可信编译摘要下调用 runtime `Load`，
Bootstrap 另与 freshly prepared 作者输入逐字段比较；缺少真实资产不能由夹具补位。

## 世界组件

`ULDMapDefinition`、`LDMapRegistry` 与编辑器 Builder 提供唯一 Demo 世界的
typed 投影、稳定地址、原生地面、完整路径及接近点查询。
`ALDRegionNavBounds` 仍由 `LDDemoWorldBuilder` 生成和校验，不能随旧 Region 名称误删。

`FLDWorldRevision` 只含 MapId/LayoutRevision/GeometryRevision，不另分配 Match 身份。
`ULDMapRegistry::QueryGround` 要求显式注册 SurfaceHint、当前世界修订和正确 capsule；
返回局部实际支持面，不把高处输入向下吸附。`QueryCompletePath` 重新验证端点，
拒绝 partial 和未声明的 XY 投影，保留路径长度与实际投影量；这些查询不授予交易或观察权限。
`ULDDemoNavigationSystem` 仍使用同一个原生 `UNavigationSystemV1`/Recast，
在导航数据注册前用中央内容半径和现有身体高度配置该世界，不在注册后改 agent 或复制数值到 ini。
角色的 Demo 分支消费同一 capsule/基础移动速度，不从旧 PrototypeTuning 补 Demo 参数。
CMC 写许可与实际子段见[架构与网络](ARCHITECTURE_AND_NETWORKING.md)，拥有身体的移动由 `RunDemoCoreCheck.ps1` 验证。
`RunDemoWorldRoomCheck.ps1` 只覆盖三个实际真人 Lobby 进程的保存世界加载、统一根/名单、
本地拥有连接；其零 Pawn 断言确认 Lobby 阶段不生成玩法身体，
不代替身体移动、AI 或完整多人对局场景。

`LDGroundQueries` 的静态地面与平面可达性能力仍由核心维护，不是观察权限、完整路径或
带世界修订/SurfaceHint/capsule 的 Demo 落地证明。正式路径与落地使用上述 Registry 查询，
观察使用单一 `TraceSight`/Observer 边界，不以 `ClearSegment` 代替。

定义中的地址与视觉 Actor 不是同一身份；重复导入、世界重载不能重复生成地点、奖励源或权威记录。
新地图先验证合法站位与各元素功能，再接入交互；测试不针对具体布局。

### 美术素材与装饰

场地道具与野怪身体用确认为 CC0 的素材包（Quaternius Ultimate Modular Men/Women、Stylized Nature MegaKit、Toon Shooter Game Kit、
若干 Kenney 套件），只提交实际选用的 `.gltf`/`.bin`/`.glb`/`.png`（Git LFS），各包许可原文 `License.txt` 与文件放在同一目录。
Toon Shooter 的 GLB 下载不带许可文本，其 `License.txt` 记录作者页面所写的 CC0 与核对日期。
项目所有者批准使用项目自有或已授权的非 CC0 来源：参赛者身体（Tripo 生成，`tripo-basketball-hero`）、
Boss 厨师身体（`chef-ninja`，FBX 源文件尚未入库）。
这类包的许可文件须原样写出清单声明的许可（如 `Project-owned`）；CC0 包仍须在许可原文中写明 CC0。
`SourceData\Art\art_assets.json` 是唯一清单：包名、作者、页面与下载地址、下载日期、许可及其哈希，
逐文件的包内路径、SHA-256 与用途（模型、图像；`SourceData\Art` 下未列出的文件一律拒绝）；
五个身体的显示身高、着色槽、右手骨骼、朝向骨骼和每个状态的源动画、播放速率、出手时刻；六个座位与四档野怪各用哪个身体；
道具的基础缩放、淡化能力与空投箱引用。静态装饰的位置与区域由保存关卡维护，不再在清单中存坐标或按树 id 生成外观。glTF 身体的出手时刻与 Run 参考速度由 `Scripts\MeasureDemoArt.py`
从 glTF 数据测得，`--check` 拒绝与清单不符的数值。

`Scripts\ImportDemoArt.ps1 -Mode Import` 经 Interchange 导入到 `Content\Lockdown\Art`，
所有材质槽换成 `M_ArtSurface` 的实例（向量参数 `Tint` 身份色、`Flash` 自发光；glTF `alphaMode` 为 MASK 的槽用 `M_ArtSurfaceMasked`，
按源 `alphaCutoff` 裁切；贴图按 glTF 采样器：`magFilter` 为 LINEAR 时平滑采样带 mip，否则按 Kenney 调色板最近点采样），并保存类型化目录
`DA_DemoArt`（`ULDArtCatalog`）。`-Mode Validate`（Bootstrap Validate 也执行）拒绝清单外的文件、
许可原文与声明不符的包、源哈希变化、不是由当前源字节导入的资产、与清单不符的目录项和违反放置区域的装饰。
资产经 `DirectoriesToAlwaysCook` 进入包。

`ULDArtSubsystem` 在游戏实例启动时解析目录一次；缺失或不完整时记录 `LD_ART_CATALOG_INVALID` 并返回空，
不用占位身体；身体接线拿不到条目时记录 `LD_BODY_ART_UNAVAILABLE` 且不绘制。

身体：参赛者六个座位共用编写身体 `hooper`（篮球手，显示身高 175 cm），已读不回的 HR（`monster_scavenger`）用女装
`Suit`（170 cm），追着办卡的私教（`monster_guardian`）用 `Casual_Hoodie`（205 cm），画饼老板 Boss（`monster_dean`）用编写身体
`chef`（240 cm），催毕设的导师（`monster_advisor`）用 `Suit`（220 cm）。只改显示，不改胶囊
（半径 40、半高 90）。目录给出每个身体的 `MeshTransform`（缩放 = 显示身高 / 绑定姿势身高，偏航由清单的
朝向骨骼算出、使模型正前方对齐 +X，脚底落在胶囊底面）、`TintSlots`（着色材质槽）、`HandBone`（`Wrist_R`）与 `RunSpeedCmPerSec`
（Run 播放速率 = 移动速度 / 该值）。Interchange 把 glTF 骨骼名中的 `.` 换成 `_`。
座位色和野怪色由 `ULDHumanoidVisualComponent` 唯一定义，经 `Tint` 写入目录列出的槽。
状态动画均为原地播放、无根运动：Idle、Run、Attack = `Punch_Right`、Cast = `Interact`、Hit = `HitRecieve`
（加速到 0.3 s 内）、Death、Spawn = `Wave`；包内没有对应动作的状态用相近动画替代，替代理由写在清单
`substitute` 字段，出手时刻写在 `impact_sec`。

编写身体（清单 `kind: authored`）是在 `Art/Characters/hooper`、`Art/Characters/chef` 中直接维护的成品资产（网格、骨架、物理、材质、贴图与动画），`-Mode Import` 不重新导入它们。每个材质带 `Tint` 与 `Flash`：`hooper` 身体材质只把球衣的金色与紫色
（`Scripts\MakeHooperJerseyMask.py` 由底色贴图生成的 `T_Hooper_JerseyMask`，`--check` 拒绝与重新生成结果不符的遮罩）换成座位色并按亮度保留明暗，数字、皮肤与鞋不变；
`chef` 的披风整体换成 Boss 色，其余材质为 `BaseColor × Tint`（默认白色）。`hooper` 的全部状态与 `chef` 的 Cast/Hit/Death/Spawn
由 Quaternius `Casual_2` 经 IK Retargeter 重定向得到（清单 `origin` 字段）：去掉重定向器的 Root Motion 步骤以保持原地；Tripo 骨架以米为单位、根骨缩放 182.3，
重定向后恢复根骨缩放并把骨盆位移换回米。`chef` 的 Idle/Run/Attack 是它自带的动作。替换这些资产后重跑 `-Mode Import` 刷新目录。
`-Mode Import` 与 `-Mode Validate` 读取这些资产核对目录：有 `source` 时网格须由该源文件的当前字节导入；每段动作的根骨水平漂移不超过 2 cm；出手时刻与 Run 参考速度按 `MeasureDemoArt.py`
的同一规则（240 Hz、手相对骨盆最靠前的时刻、着地脚后滑速度的中位数）从动作测得，与清单不符即失败并给出测得值。
编写身体可带穿戴件（`FLDArtAttachment`）：挂在身体插槽上的静态件（Boss 的铁勺挂 `Weapon_R`），或共享身体骨架、
以 Leader Pose 跟随身体并自带布料模拟的蒙皮件（Boss 的披风）。穿戴件只显示、无碰撞、不参与点选，随身体一起受击闪光，
可列自己的着色槽；身体与穿戴件合计至少一个着色槽。

装饰：正式 `L_Demo` 中全部静态外观是已保存的标准 `StaticMeshActor`，编辑模式即可选择、移动、旋转、缩放、复制和修改网格/材质。
`Environment/Blockers` 下的树、墙与围栏以可见模型为根，隐藏的 `GameplayCollision` 是普通实例组件，随父 Actor 一起移动与复制；
碰撞尺寸仍由其简单网格决定，替换外观不会自动扩大玩法范围。`Environment/Decorations` 下纯装饰的根组件为 NoCollision，
全部响应 Ignore、不产生重叠、不参与导航。外围边界的隐藏几何另在 `Environment/BoundaryCollision` 中可查。
`LDSceneVisual` 与 `LDSceneZone:<区域>` 是保存对象的分类标签；复制的阻挡在同步时按新的 Actor Guid 分配稳定唯一 id。
`DA_DemoArt.Decorations/HiddenBlockers` 是从保存关卡生成的只读索引，不是第二个布局编辑入口。
`ULDArtWorldSubsystem` 只核对该索引并启动显式 DEV 探针，不再生成、替换或隐藏场景对象。
`LDArt::CheckPlacements`（`LDArtPlacement.h` 常量）按已批准世界逐个核对区域：
`outside` 不压野外场地或公共休息室的可用地面，只作外围背景（树、建筑外墙、看台、路灯）；扩展地面上两处之间的封闭隔离带仍可放背景；
`wall_base` 是墙根的矮小装饰（长椅、花坛、花箱、垃圾桶），在野外或休息室墙体内侧 300 cm 内、低于视线采样高度，并避开出生点、营地与旗杆；
`ground` 是场地上的矮地被（草、花、三叶草、蘑菇、石子），守 `wall_base` 除贴墙外的全部规则；生成时另离每个阻挡至少 60 cm，只落在身体能走到的导航面上；
`blocker`（碰撞跟随美术）是一个世界阻挡的外观：保存对象绑定其阻挡 id，道具站在阻挡底面、高度等于阻挡高度，
转向后的模型包围矩形就是阻挡多边形（误差 `BlockerFitToleranceCm` 2 cm）；墙与围栏的多边形按选定模型、以阻挡高度定缩放后的脚印写入世界数据。
树的外观、朝向和缩放由各自 Actor 保存，不再从 id 哈希重建。隐藏边界由 `LDSceneHiddenGeometry` 标记并保存其隐藏状态；
`dressing.airdrop_prop` 只为动态空投保留箱子引用。可见外观和碰撞分别检查，淡化不会关闭碰撞。
阻挡外观须整块站在同一平层上（地图四周是高台），世界在墙内侧留出无树走道；
`capture` 是覆盖其锚点的细小地标（旗杆）。
除绘制世界阻挡的美术（树、废车、集装箱）外，只有旗杆会遮挡身体：
清单中标 `fadeable` 的道具使用 `M_ArtSurfaceFade`（Masked，按 `1 − LDFadeStrength·max(LDFade, 每实例自定义浮点 0)` 逐像素抖动，
材质里强度默认 0.9），由独立组件承载并带组件标签 `LDFadeable`；运行时 relay 经 MID 把 `LDFadeStrength` 设为 0.3、遮挡时设置 `LDFade`（0 不透明、1 保留约 70% 像素可见度），见[视野与 UI](VISION_AND_UI.md)。
没有找到 CC0 的篮球架，未加入。

母材质 `M_ArtSurface`、`M_ArtSurfaceFade` 的节点图就是导入器 C++ 中的 `ArtMasterMaterial`：
每次 `ImportDemoArt.ps1 -Mode Import` 都在原资产上丢弃旧图并按代码重建（表达式与参数 GUID 由包名和图中位置导出，
重建后保持不变；引擎自生成的 id 仍会让文件字节变化），再与其他美术资产一同保存。
要修改母材质，改 `ArtMasterMaterial` 与同文件的 `ArtCheckMasterGraph` 后运行 Import；
在编辑器里直接改资产会在下次 Import 时被覆盖。Import 与 Validate 都检查加载出的节点图
（表达式数、参数名、BaseColor/Roughness/Emissive/OpacityMask 连接、混合模式），不符即以
`LD_DEMO_ART_REJECTED ... does not hold the importer's graph` 失败，不会保存空图。
过去“commandlet 重新加载时节点图为空”的根因是该 commandlet 设了 `IsClient = false`：无客户端的进程
按专用服务器加载包，链接器会跳过编辑器游戏非必载的导出（材质表达式即是），于是读到空图、再保存即变黑。
美术导入 commandlet 与 `LDScaffoldBootstrap`（它的 Validate 也调用美术校验）现为 `IsClient = true`，
美术导入与校验在 `GIsClient` 为假时直接拒绝运行。仍为 `IsClient = false` 的 commandlet
（`LDDemoAudioImport`、`LDAssetReferenceAudit`）加载材质时同样看不到节点图，
不要用它们保存任何材质。`Scripts\CaptureDemoArt.ps1`（DEV `-LDDemoArtCapture`）
渲染身体、动画姿势、场地俯视与路线遮挡扫描，`Scripts\DrawDemoArtLayout.py` 画出道具布局图；输出在
`Artifacts`。原始下载包不入库。

## 营地与旗杆的复用边界

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

营地运行由两层组成，都不另存身体、生命、战斗或奖励真值：

- `World\LDCampLifecycle`：纯记账。由批准内容的营地放置/怪物定义和世界营地锚点/成员 home 生成全部营地的名册；
  每局为固定 (营地, 槽位) 分配一次 EntityId，`Life` 只镜像核心生命，并只在整组生成提交时递增。
  最后一名成员死亡时按死亡事实的权威整数毫秒固定清营时刻、按该档 `RespawnMs` 定刷新期限，不再从秒换算；死亡按生命幂等。
  反击只接受参赛者来源，全营共享来源身份而非坐标；Returning 成员忽略受击；
  出生或回营重置时刻之前的接触不能重开仇恨；每名参赛者只保留当前生命，总数受名单上限约束。
  每次进入 Returning 递增 ReturnEpoch，重置键为 LedgerMatch + 完整 VictimKey + ReturnEpoch，只能完成一次。
- `World\LDCampDirector`：Camps match provider 在 Starting 的 Prepare 中生成（服务器、不复制）并建名册，
  MatchReset 时销毁全部野怪身体并丢弃本局记账。各入口只在 Combat 驱动的对应阶段校验借用许可后执行：
  初始化 RespawnsAndWorld 内整组生成；Motion 用同一 CMC 推进与 `LDPathSteering` 跟随完整路径；
  规划起点和追击终点经 `LDPathSteering::PlanStart` 落到一个胶囊半径内的 navmesh 点（贴墙/贴身时脚点可能在
  Recast 收缩边外），已在 navmesh 上的 home 原样保留；
  Actions 读取 Combat 身体状态、用 `TraceSight` 从成员脚点证明来源可见后调用 `LDCampDecision`，
  普攻经 Combat `SubmitAction` 提交；Effects 消费已提交的 `FLDCombatHostileContact`；
  DeathsAndRewards 在核心结束生命后只记账并销毁野怪身体，奖励由成长唯一 sink 支付；
  RespawnsAndWorld 先对真实到家（同一支持面、`CAMP.ResetArrivalRadiusCm` 内）的存活成员，
  以及被 `FLDCampReturnWatch` 判为回位无路的成员（`CAMP.ReturnDeadEnd*`，原地、`bInPlace`）调用
  Combat `PrepareCampReset/ApplyCampReset`，再处理到期的整组刷新。`NextRefreshDueMs` 向 Combat 驱动报告最早的
  待刷新期限，使刷新恰好落在该步边界。刷新落点被动态占用时保留同一票据，
  只在本营成员 home 的局部偏移内重试，不换营地、不发布半组。落点原点与参赛者出生相同
  （支持面 + 胶囊半高 + CMC 地面带中值），交给核心前已 FindFloor 并在登记地板上 walkable；
  胶囊底直接贴地会在进入 Walking 时被 CMC 抬升，精确落点校验随即拒绝。
- 野怪身体 `ALDCampMemberCharacter` 不复制；观察只经 Vision 授权帧，`AppendObservedFacts`
  只补 CreatureId 和单层无期限的 `camp_returning`，`PublicViews` 只给固定营地 Id/档位/怪物/锚点。

旧营地 Actor、职业/技能初始包、三营地/有限刷新预算、Boss/Tower/圣坛与探索奖励不作备用实现。

唯一喷漆与守护归约和结果所有者是 GameMode 核心的 `FLDCaptureReducer`（`Framework\LDCaptureReducer`，
喷漆区进出由 `World\LDCaptureCircle` 提取，半径为 `CAPTURE.RadiusCm`），不再保留独立 Objective Actor、HeartSensor、
Seal/路线/hold/deadline 或 Claim fallback。复用的是恰一次结果与先关写闸再清理/发布的语义；
归约、完成提案与关闭顺序见[架构与网络](ARCHITECTURE_AND_NETWORKING.md)。
调参热重载时 `LDDemoEntry::LoadRuntimeContent` 返回一份固定快照，只在 `InitializeAuthorityCore`（新局开始）经 `RefreshDevContent` 替换；
`ULDMapDefinition::Read` 接受仍绑定编译根的 `DA_DemoWorld`，但世界字节与按重载数值重新投影的完整几何必须逐字段一致；`PreLogin` 拒绝远程客户端。
DEV 测试会话的 `FLDCaptureReducer::DevOpenAt` 只把未开放旗杆的开放时刻提前到一个尚未关闭的边界，开放仍由 `CloseBoundary` 完成。
DEV 构建中 `DevTally()`（`FLDCaptureDevTally`，经 `ALDGameMode::DevCaptureTally`）按每次归约后的状态变化计数：每名持有者的尝试（从零开始喷漆）、
续喷（暂停的进度恢复）、被争夺暂停、失去、被硬控打断和喷成次数，以及全局的进入争夺、喷成、覆盖（喷在别人涂鸦上）、
因主人死亡/离局清除涂鸦的次数和当前涂鸦的喷上时刻（无主为 -1）；只供遥测读取，任何规则都不读它，`Reset` 时清空。

## 保存与复位边界

世界不提供持久检查点、自动保存或恢复入口。成长与奖励的原子提交由成长域维护，
不通过旧世界文件或已退役的物件服务重建和补发。
新局和新身体的隔离仍走 GameMode 的 Match/Life 与各领域拒旧协议，见
[架构与网络](ARCHITECTURE_AND_NETWORKING.md#请求代次与回执)；
`RunDemoCaptureCheck.ps1` 与 `RunDemoGrowthCheck.ps1` 的第二局阶段在拥有连接上核对新局开局状态与旧代请求拒绝。

本地按键与镜头偏好仍由 `ULDControlSettings::Save/Load` 管理，见
[移动、输入与镜头](MOVEMENT_INPUT_AND_CAMERA.md#相机与小地图)，不保存局内战力或世界状态。

## 标准关卡编辑与同步

编辑器默认打开 `/Game/Lockdown/Generated/Maps/L_Demo`；游戏默认仍从 `L_Frontend` 进入。
常规修改直接在关卡里完成，不再另改 JSON 坐标。`Layout/Locations` 是标准 TargetPoint；营地成员的 home 点挂在营地点下，
移动营地时一起移动；出生点继续使用标准 PlayerStart。它们只提供保存的布局，不承载另一套游戏状态。

静态网格体可以直接增删、移动、旋转、缩放或替换模型，装饰和灯光可以直接修改，保存后即可 Play。
运行时使用当前关卡的原生碰撞和地面高度，不要求装饰派生索引与对象逐项一致；新增对象的碰撞响应也参与地面、移动和视线查询。
复制的阻挡在运行时按保存的对象名派生稳定身份，不写回编辑器。布局点保留 XY，运行时站立高度跟随当前地面。
出生和服务等必要布局点仍须有可行走支撑和胶囊净空；缺地板、过陡支撑或阻挡必要点时，会指出具体地点和碰撞问题。

需要刷新作者源快照时，保存并关闭编辑器，再执行 `Scripts/SyncDemoMap.ps1`，完成后重开。

同步先从保存对象导出坐标，再经 `ApplyDemoMapEdits.py` 更新派生的 `SourceData/World/demo_world.json`，
沿原有两条 Sync 生成载荷和 contract，构建 Editor，最后刷新派生碰撞、导航和美术索引并做 Bootstrap 验证。
无变化的导出保持世界源文件逐字节不变；坐标以 0.001 cm 精度导出，已有整数不会因浮点误差产生无关修订。
工具验证导出所基于的源摘要，拒绝旧导出和未知字段；写入前继续做世界结构与内容绑定检查，不篡改生成哈希。

显式源导出仍受作者数据格式约束：树干直立、正缩放且 X/Y 相等；地面矩形且轴对齐，坡道、高台满足层高规则。
这些导出约束不阻止普通关卡 Play。复制出生/服务等布局点造成身份或数量冲突仍会明确拒绝。
原生移动、地面高度、落地胶囊和鼠标落点统一查询阻挡 Pawn 的 WorldStatic 原生碰撞，不要求模型响应专用地面通道或保留 `demo_ground` Actor。
地面可以由多个普通模型拼接；没有真实碰撞、忽略 Pawn 的视觉面片不会作为支撑，也不会仅因其地面通道设置而阻止开局。
派生的隐形悬崖护栏只挡横向移动，不作为站立面；动态角色不参与地面射线。遮挡继续使用 DemoSight 响应。
世界修订包含当前静态网格体的模型、变换和碰撞响应，编辑后不会复用旧地面或观察缓存。

`LDLayoutPoint` 在原布局点上提供只在编辑器显示的身体、道具和范围预览。移动整个定位点会改变对应的局内生成位置；
角色、怪物和假人的实际生命与 AI 仍由服务器生成，预览没有碰撞、不参加导航、不在局内显示。
预览外观参数不修改内容目录中的正式外观、数量、技能范围或刷新规则。

复制训练假人定位点会自动启用 `Training / Shared Training Dummy`，新增可攻击的通用假人；
原有六个槽位仍按对应参赛者生成。通用假人按保存的 Actor 名派生独立身份，移动、复制和删除不争用槽位编号。
所有目标复用现有战斗、休息隔离与归零回满规则；通用目标使用既有开局等级的无构筑底盘，返回野外时清理。
通用假人直接保存在关卡中，显式作者源快照导出尚不支持该类型并会报告原因。

导航使用编辑器自动更新、运行时静态数据。完成碰撞编辑后构建路径并保存关卡；
`LDSceneAuthoringLibrary::BakeNavigation` 等待原生 Recast 构建结束并确认可用，同时启用编辑器的自动导航更新。
不在游戏中重新烘焙静态地形；若以后增加局内拆墙或建造，需要另行接入动态导航。

专项测试：`Lockdown.Rules.StaticScene.PersistenceAndBinding`、`StaticScene.CopyIdentityAndSafety` 与 `StaticScene.ReplacementFloorAndOverlay`，
以及 `Scripts/TestApplyDemoMapEdits.py` 的字节保持、坐标更新、旧摘要/重复 id/越界字段拒绝检查。

## 检查入口

对应 Sync `--check`、`Test*Import.py`、Bootstrap Validate 先验证生成链。
几何与导航使用当前 `Lockdown.Rules.DemoWorld` 和 Bootstrap 校验；
观察的有界编码、隐私、句柄和遮挡使用 `Lockdown.Rules.DemoVision.*`。
真实拥有连接使用 `RunDemoWorldRoomCheck.ps1`（Lobby）、`RunDemoCoreCheck.ps1`（身体与地面）、
`RunDemoCampCheck.ps1`（营地 N03）和 `RunDemoCaptureCheck.ps1`（喷漆、守护与终局 N02）；
全部场景及打包版运行方式见[开发指南](VERIFICATION.md#最小验证选择)。
模型/数据检查、原生路径、实际运动、限制客户端表现分别证明不同事情。

工作树可能共享 LFS 缓存，不能把当前工作树的 Git 元数据目录拼成固定 `lfs\objects` 路径。
`VerifyCleanCheckout.ps1` 通过 `git lfs env` 的 `LocalMediaDir` 定位实际缓存，
复制到隔离检出后再验证；这与从远端下载验证是不同的边界。
验证检出使用 `Artifacts` 下的短目录，避免长工作树路径触发 UBT 的 Windows action 路径长度限制。

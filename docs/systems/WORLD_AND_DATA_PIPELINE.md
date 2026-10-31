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
| `SourceData\Character\prototype_rules.json` | `LDScaffoldBootstrapCommandlet` 导入角色目录 |
| `SourceData\Catalogs\V07` + `SyncV07*Data.py` | `Content\Lockdown\Data\V07`、对应 contract；World 同时维护 `Config\V07RegionLayout.json` |
| `SourceData\Catalogs\V08` + `SyncB84Data.py` | 三个 `*_b84.json`、B84Manifest、`LDB84Contract.h` |
| `SourceData\World` + `SyncV09Data.py` | 地图载荷、地图 contract；包含 `travel_table.csv` 输入 |
| `SyncB91Data.py`、`SyncB91Objects.py`、`SyncB91Tools.py`、`SyncB91Traversal.py`、`SyncB91Grass.py`、`SyncB91Forest.py` | 环境/物件载荷及相互关联的 contract |
| `SyncB92Economy.py`、`SyncB92Population.py` | 当前经济与人口/活动输入及对应 contract |

`SyncB91Forestry.py` 等链路还消费前序生成结果；不要跳过中间步骤或只手写最终宏。
`CatalogTools.py` 复用 schema、确定性编码与原生 ID 校验，B84 生成不依赖历史说明或档案 ZIP。
schema8/schema9、V07/V08/V09、Bxx 命名是当前技术边界，不是产品版本 `0.10.0`。

源数据按 UTF-8 读取；涉及按字节计算的 SHA/MD5 时保留原始编码和换行。
`native_ids.json` 保留稳定原生编号，不通过重排枚举自动“修正”已有 ID。
`--check` 比较现有载荷与生成结果而不写文件；不一致应定位输入/生成链，不能删除断言或重写期望值。

## 资产导入与打包

`LDScaffoldBootstrapCommandlet` 读取 `Config\BootstrapLayout.json`、角色输入及当前载荷，
保存和核对 DataAsset、地图、输入和表现资产。

- Generate 生成缺失内容；Validate 检查已有资产与签名。
- Regenerate 会替换受生成器管理的资产，必须明确需要，不能作为绕过签名失败的默认动作。
- 手工制作资产不要放到生成器负责覆盖的目录内。
- 生成签名、原生加载时的内容摘要和 Cook 依赖是同一条一致性链。
- `.uasset`/`.umap` 通过 Unreal 生成和 Git LFS 管理，不作为普通文本改写。

`Lockdown.Build.cs` 声明运行载荷依赖；`PackageWin64.ps1` 在打包前做 Bootstrap Validate，
从项目设置读取产品版本，并记录 SourceData 等源输入以及包文件摘要。
SourceData 不应整目录加入运行时打包，运行程序不需要 docs 才能启动。

## 世界组件

`ULDRegionDefinition`/`ALDRegionDirector` 维护区域定义；
`ULDMapDefinition`、`LDMapRegistry`、`LDMapApproaches` 与编辑器 Builder
承担完整地图的定义、稳定地址、拟合与接近点。

`LDGroundQueries` 提供可行走表面查询。厘米坐标、接地面、高低层、碰撞与视线是相关但不同的判断，
不能以“画面看起来在同一个地方”替代它们。`ULDMapSurfaceSubsystem`
是特定地图模式的静态表面/接触切片，不是任意 profile 自动启用的全局效果服务。

定义中的地址与视觉 Actor 不是同一身份；重复导入、世界重载不能重复生成地点、奖励源或权威记录。
新地图先验证合法出生、地点连通、入口与阻挡，再接入交互。

## 动态阻挡与对象生命周期

`ALDEnvironmentDirector`、`ALDWorldObjects` 等分别负责环境观察/门与真实物件记录。
门的物理碰撞、视线代理、导航 revision 和合法表现必须对应同一次权威变化。
接受阻挡后，尚未刷新的旧路径只能停止/重算，不能继续穿越。
更新一个门或箱子应局部处理，不无条件重建整个地图。

对象能力独立：阻挡行走不自动代表阻挡视线、有库存、可摧毁或能产出奖励。
背负物、地面物、固定物和库存实例之间应只有一个位置/所有者记录；
摆放检查完整占地与支撑面，不只检查中心点。

梯子/绳索等现有链接的预约、起终点和原生运动要一致；死亡、取消、控制器交接先清理预约和进行中的动作。
这些实现约束可复用，但对应玩法是否进入 Demo 仍由 App Spec 决定。

## 保存与复位边界

当前世界检查点只覆盖其显式允许的状态，不是完整对局恢复服务。
恢复不能补回已消耗的物品、燃料、奖励或来源预算；观察句柄与服务报价要由当前连接重新建立。
有活动所有者或未决效果的状态若不支持，应明确拒绝，而不是保存部分状态后声称完整成功。
新局重置与恢复同一局是两种操作，不能混用。

## 检查入口

对应 Sync `--check`、`Test*Import.py`、Bootstrap Validate 先验证生成链。
几何、导航与实际交互用 `RunB90Structure.ps1`、`RunB90River.ps1`、`RunB90Traversal.ps1`、`RunB91Check.ps1`、
`RunB91Network.ps1` 等既有场景按需覆盖。
`RunB91Completion.ps1` 使用当前运行结果汇总，不依赖冻结文档档案。
模型/数据检查、原生路径、实际运动、限制客户端表现分别证明不同事情。

工作树可能共享 LFS 缓存，不能把当前工作树的 Git 元数据目录拼成固定 `lfs\objects` 路径。
`VerifyCleanCheckout.ps1` 通过 `git lfs env` 的 `LocalMediaDir` 定位实际缓存，
复制到隔离检出后再验证；这与从远端下载验证是不同的边界。
验证检出使用 `Artifacts` 下的短目录，避免长工作树路径触发 UBT 的 Windows action 路径长度限制。

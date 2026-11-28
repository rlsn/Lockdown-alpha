# S2 A03 地图与导航报告（Batch 01）

证据级别：L1 源码 + `Lockdown.Rules.StaticScene.MapTraversal` 审计（本分支构建，`Artifacts/MapTraversalAudit/Placements.json`）+ L3 原生对抗日志。

## 1. 数据权威关系（T03-05）

| 层 | 内容 | 运行时是否读取 |
| --- | --- | --- |
| `L_Demo.umap`（LFS） | 人工摆放的 `LDLayoutPoint` / 营地成员 / 出生点 | **是，唯一权威**：`ULDMapRegistry::Rebuild` → `LDStaticScene::ProjectRuntimeGeometry` 从地图投影锚点 |
| `SourceData/World/demo_world.json` | `SyncDemoMap.ps1` 从地图导出的快照（approval：#238 18/10/4/2 档、34 营地） | 否（生成器输入与契约） |
| `docs/LEVEL_AUTHORING_CONTRACT.md` §2 | 写「34 处野怪营地」与 Sync 名册核对 | 否 |

实测：运行时 64 营地 / 166 成员；SourceData 34 营地 / 80 成员。SourceData 中 16 个营地（e01 e03 h02 h03 h04 h06 l04 l05 l08 l09 l10 l11 l12 l19 l21 l22）地图里已不存在；地图有 46 个 SourceData 没有的营地（`authored_group_*`、`instance_LDLayoutPoint_*`、e05–e09、h14–h22 等）。SourceData 自上次 Sync 起已过期。`SyncDemoMap.ps1` 会改写 SourceData，且按名册核对 34 营地，未运行。**64 vs 34 的取舍属于 Batch 03 / PO 与关卡负责人决定，S2 未改动。**

## 2. #353 不可达营地（T03-01 / T03-04）

当前地图下 166 个成员中只剩 4 个 `field_path`/`centre_path` 失败（#353 列出的 e01、h04、h12、l09、l10、l19、authored_group_046 已从地图移除或修复）：

| 成员 | 位置 | 问题 | 建议（交关卡负责人） |
| --- | --- | --- | --- |
| h08_0 | (-75, 3312, 942) | 站在 `StaticMeshActor_278` 高台上，场地与营地中心都走不到 | 移到与场地相连的地面，或加可走坡道 |
| h09_0（4 档远古） | (-3800, 2950, 565) | 孤立导航岛（`StaticMeshActor_633`），部分路径 | 移到 h09_1/h09_2 所在地面 |
| h09_1、h09_2 | (-4457, 2843, 500)、(-4343, 2957, 500) | 走得到场地，但到营地锚点（Z=867）无通路 | 锚点下移到成员所在层，或成员上移 |

代码侧：`LDMapRegistry` 把锚点/成员脚下的支撑面直接纳入地面（`AdmitPointSupport`），这是 h08_0/h09_0 能「合法站立」却不可达的原因；收紧它会让这两个点 `NotGrounded` 并使整张地图拒绝开局，必须与点位修正同批做，S2 未改。`MapTraversal` 把不可达升级为失败前需先决定「是否允许有意放在高台上的怪」（#353 原文），当前升级会让 CI 因上述 4 点失败，**未升级，待决定**。

## 3. c001 出生点「寻路死锁」（T03-02）

根因不是导航网格：运行日志 `LD_NAV_BLOCKED obstacle=LDCampMemberCharacter`。c001 的分散入场点 (-5395,-5958) 走向旗杆的路线穿过营地 h10，5 个成员（(-4831,-3492)、(-4972,-3542)、(-5071,-3402)、(-5165,-3542)、(-5321,-3476)，间距 100–160 cm）与地形一起封住通道；成员站在家不让路，导航路径不绕身体。绕行诊断：东侧 3 个绕行点无路可达（地形），西侧被其余成员挡住。

已修（AI 侧，`bc3e36b4`、`91b52e08`、`e2ae8408`）：行走绕开路上的营地成员，无法绕行且已顶住时打穿挡路成员。c001 不再 90 s 删失：双方打穿 h10 后会面交战；但评估把与营地的任何接触判为 ExternalInteraction，c001 的 8 个 Case 现标为 Invalid（如实标注，而非时限删失）。**在 h10 移出通道前，种子 c001 不能产出干净对决样本。**

**报告关卡负责人**：h10 营地横在西南分散入场区通往旗杆的通道上，空闲时把通道完全堵死（真人玩家同样会被顶住，只能打营或绕远）。建议把 h10 成员移离通道或拉开间距（> 2 × 成员半径 + 80 cm）。另一个绕行受阻点在 (-2403,-687) 附近的营地，回放中 AI 同样选择打穿。

## 4. 其他

- `Lockdown.Rules.BuildEvaluation.Policy` 在基线 `e62ff304` 上已失败（「A blueprint without an empty slot does not hold the duel」，实际给出 SellBlueprint），与本批无关，未修。

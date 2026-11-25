# VFX 手册：美术方向、批准目录与生产流程

世界空间战斗特效的统一入口。做任何特效前先读本页：§1 美术圣经，§2 批准目录（视觉回归基准），§3 决策记录，§4 生产流程，§5 新特效工作流。
界面（HUD、构筑页）的直播风格见 [视野与 UI](VISION_AND_UI.md)；那套平面漫画语言**不**用于世界空间特效。

状态标记：**APPROVED** = 用户看过实机画面后明确认可；**PROVISIONAL** = Agent 依据已批准效果推导、尚未经用户验收；**REJECTED** = 用户否决或实测不成立；**SUPERSEDED** = 被更新方案取代。PROVISIONAL 条目不得当作正式标准引用。

视觉证据在 `docs/systems/vfx/`（Git LFS）：`approved/` 只放已批准版本，`rejected/` 放否决方案。完整采集（含逐帧、慢放、性能 JSON）在本机忽略目录 `Artifacts/Audits/PresentationOverhaul/vfx-direction/`，只作补充，可能被清理。

## 1. 美术圣经

### 1.1 总方向（APPROVED，来源：闪电链验收）

- 特效是**风格化三维能量**，服从现有风格化 3D 人物、自然日光与暖色街区材质，不是贴在世界上的二维图案。
- 能量由**暖白核心 + 柔和外层**构成，外层用降饱和的元素色；靠形状、节奏和接触反馈建立力量感，不靠粒子数量、Bloom 或屏幕震动。
- 命中必须在**正常速度、正常游戏镜头**下读得出“这个身体被打中了”：接触点有方向性的爆发，身体本身有反应，地面有光。
- 特效贴合空间：链和弧在胸口高度，落点在真实目标身上，命中光照亮地面与身体。

### 1.2 禁用手法（REJECTED，来源：用户对第一版切片特效与早期闪电的否决）

| 手法 | 否决原因 | 证据 |
| --- | --- | --- |
| 粗黑描边 | 与 3D 人物和自然光不协调，像贴纸 | `vfx/rejected/chain-mesh-comic.jpg` |
| 过饱和纯黄平涂（`#FFD23D` 直接作填充） | 刺眼，与暖色地面打架 | 同上 |
| 机械规则锯齿、平滑抖动的“软绳” | 不像电，像折线图或绳子 | `vfx/rejected/chain-niagara-v1.jpg` |
| 大型漫画星形爆点、白色圆片闪光 | 二维贴纸感，遮挡角色 | `vfx/rejected/chain-mesh-comic.jpg` |
| 平涂纸带式笔触（候选 B） | 在游戏尺度下像纸带，不像电，端点脱离目标 | `vfx/rejected/chain-candidate-b.jpg` |
| 以纯圆环作为区域的主要读法 | 像调试圈，没有元素感 | `vfx/rejected/field-grammar-proof.jpg` |

### 1.3 通用原则（PROVISIONAL，由闪电链推导，待其他类别验收）

- **分层**：核心（暖白，不随元素变色）→ 外层（降饱和元素色，饱和度 ≤ 70 %）→ 接触层（方向爆发、身体反应、火花、地面光）。
- **时序**：预兆 → 释放 → 命中 → 余势 → 消散。主体形状硬切，切前留 1 帧暗残影，不拖成下垂软线。
- **视觉权重**：普攻 < 终结 < 技能 < 反应 < 击杀；档位差异体现在轮廓和丰富度，不只是放大。
- **自己与他人**：他人的特效约 60 % 宽、50 % 亮，粒子更少，目标身上不加身体反应。
- **无元素攻击配色**：自己用柔和橙，他人用浅象牙（待验收）。
- **预算**：8 人同时出手时，每类 ΔGPU 不超过约 +0.5 ms（实测，见各条目）。

### 1.4 按类型的要求（PROVISIONAL，除注明外）

统一语言不等于统一形状。各类型只共享 §1.1 的方向与 §1.3 的分层和时序，形状、亮度和粒子结构按功能各不相同。

| 类型 | 要求 | 状态 |
| --- | --- | --- |
| 链式攻击 | 见 §2.1 | APPROVED |
| 瞬时攻击 / 挥击 | 围绕胸口的倾斜 3D 运动弧；连击段方向交替，终结段更宽、多一道残影；预兆用地面上同几何的细暖色弧逐渐显出，不用深色月牙 | 释放弧待用户验收；预兆未通过 |
| 命中爆发 | 沿攻击方向的锥形短光、身体边缘光、火花、地面光；他人的命中也沿方向，不做放射毛刺 | 待用户验收 |
| 持续区域 | 权威半径上的断续、闪动的电边，沿边缘跑脉冲；场内安静，少量小电弧爬动并连到场内的人；不得压住角色；进出场有过渡 | 重新设计中，见 §3.6 |
| 投射物、拖尾、状态附着 | 尚未设计 | — |

## 2. 批准目录

### 2.1 闪电链（APPROVED，视觉回归基准）

- **技能与事件**：`active_zap`（电你个哆嗦）每次被匹配到的电命中（首击与每次弹跳），起点为施法手或上一个受击者，终点为当前受击者胸口。导电反应等其他电效果不属于本条目。
- **批准版本**：用户批准的是候选 **A+**（实现提交 `315a6a8e`，分支 `presentation/combat-vfx`），后在 `dab423a9` 改名为唯一外观 `NS_LDChain`。批准对象是 A+ 的画面，不是候选 A、B、C 或旧版本；对比图里出现多个候选时，以 `vfx/approved/` 为准。
- **视觉证据**：
  - `vfx/approved/chain-sheet-1x-2x.jpg`：7 个场景（短跳、长跳 640 cm、首击、两次弹跳、自己与他人、多人同时施放、日照地面），上行 1×、下行 2×。
  - `vfx/approved/chain-*.jpg`：各场景 6 帧关键帧。
  - `vfx/approved/chain.mp4`：正常速度 30 fps 实机录像。
- **构图与形状**（每一跳）：
  - **主干**：离散折线，段长约 55 cm，每秒整体重随机 25 次，宽度随重随机起伏；亮脉冲从起点跑向终点；满亮 130 ms，1 帧暗残影，165 ms 切断。
  - **细丝**：两条细丝绕主干扭转，经过同一批折点、相位独立，只在自己的链上出现。
  - **分叉**：3 根短折线叉，只从主干中段长出，每 2 帧随机显隐；不超过 45° 的尖刺，施法手上没有针状分支。
- **命中**：
  - 7 道锯齿短光，偏向跳跃方向，持续 100 ms；
  - 受击身体上 3 条小电弧爬 0.3 s，源端 1 条；
  - 火花带轻重力，寿命 0.35–0.55 s，自己的链 14 颗、他人的 5 颗；
  - 0.2 s 暖色无阴影点光；地面 0.26 s 暖光，叠 0.55 s 淡焦痕；
  - 受击身体 0.22 s 边缘光（`M_LDChainBodyGlow`：加性菲涅尔外壳，沿法线外推 1.2 cm、跟随姿态）；
  - 弹跳目标略小（链宽 56 对首跳 70）。
- **施法手**：40 cm 蓄能暖光，无星爆、无白圆片。
- **他人的链**：宽 60 %、亮 50 %、火花 5 颗、无细丝、无身体光。
- **色彩**：核心暖白，外层暖白到浅金，`Body (1, .68, .26)`；不用纯 `#FFD23D` 平涂，无描边。
- **Niagara 与参数**：
  - 系统 `/Game/Lockdown/Art/VFX/Niagara/Generated/NS_LDChain`（含主干 Ribbon、细丝、分叉、接触短光与火花发射器），材质实例 `MI_LDChain_*`，身体光材质 `M_LDChainBodyGlow`。
  - User 参数：`User.Start`、`User.End`（世界坐标）、`User.Tint`、`User.Width`（cm）、`User.Intensity`、`User.Duration`（s）、`User.Focus`（1 = 自己，0 = 他人）。
- **源资产**：`SourceData/VFX/Slice/chainA_profile.png`、`chainA_energy_noise.png`、`chainA_spark_streak.png`、`chainA_flash.png`、`chainA_ground_glow.png`、`chainB_scorch_dash.png`（命名沿用候选阶段，属于 A+）；生成脚本 `SourceData/VFX/Blender/slice_chain_candidates.py` 与 `slice_render.py`；清单 `SourceData/VFX/vfx_slice_assets.json`。
- **运行时绑定**：`LDSliceVfx`（`Character/LDHitVfx*`）按可见施法者登记的击打、以时间距离匹配到电命中，经 `LDImpactFx` 生成池化的 `NS_LDChain` 组件并写入 User 参数，组件挂在受击者上，到时长后释放。命中本身不带来源，施法者不可见时按命中外观回退，不泄露隐藏单位。
- **性能**（编辑器 `-game`，1080p，8 人施法减空闲，3 次）：ΔGPU +0.14 ms（0.09–0.16）；链组件峰值 24，粒子峰值 1316。游戏线程增量本机波动 0.6–2.0 ms，与网格基线同级，**未在打包版验证**。
- **可推广**：暖白核心 + 降饱和外层的分层；硬切加 1 帧残影；命中的方向短光 + 身体反应 + 地面光组合；他人降级规则；生成器 + User 参数的结构。
- **不可推广**：折线主干、细丝、分叉、沿链脉冲是链式能量专属，不得套用到区域、挥击或投射物上（见 §3.7）。
- **保护**：任何重构、模板升级或资产重新生成后，`NS_LDChain` 的画面必须与 `vfx/approved/` 一致；改动它需要用户明确要求重新设计。

## 3. 决策记录

每条区分**用户反馈**（用户明确说过的）、**Agent 判断**与**实测**。

### 3.1 原始网格切片特效（REJECTED）
- **Context**：第一版普攻、技能命中、水柱、电链与电场用池化 StaticMesh + MID 实现，形状为带黑描边的漫画月牙、星爆与图案。
- **Alternatives**：当时没有比较 Niagara 方案（见 3.2）。
- **Decision**：整体否决。
- **Rationale**：用户反馈：粗黑描边、过饱和纯黄、机械锯齿、大星爆、像贴在三维世界上的二维图案，与地图和人物画风不协调；连同旧版本也未达到满意的美术品质。
- **Evidence**：`vfx/rejected/chain-mesh-comic.jpg`。
- **Applicability**：所有世界空间特效。网格 + 材质作为手段仍然可用，被否决的是这套图形语言。

### 3.2 “Niagara 不可程序化生成”的误判（REJECTED）
- **Context**：VFX Agent 以“程序化生成 Niagara 不可行”为由改用网格方案。
- **Alternatives**：编辑器 MCP 的 NiagaraToolsets、C++ 调用 `UNiagaraExternalEditUtilities`、复制模板只改渲染器。
- **Decision**：撤回该结论，采用 3.3 的路线。
- **Rationale**：实测发现该结论从未经过尝试，没有任何报错或日志。通过 MCP 实测时，建系统和读结构成功；一次格式错误的 `AddUserVariables` 写入了无效参数，导致编辑器断言崩溃（`NiagaraVariant.cpp:108 InCount > 0`），所以实验插件只适合查看，不适合生产。
- **Evidence**：`Artifacts/Audits/PresentationOverhaul/vfx/NIAGARA_PROOF.md` 与 `vfx/niagara/lead-mcp-crash-excerpt.txt`（本机）。
- **Applicability**：以后任何“工具不可行”的结论都必须附实际尝试与错误证据。

### 3.3 Niagara 生产路线（APPROVED，用户接受工具链）
- **Context**：需要可复现、无人工操作的 Niagara 资产。
- **Alternatives**：见 3.2。
- **Decision**：在 VFX 导入 commandlet（`ImportDemoArt.ps1 -Mode ImportVFX`，`-nullrhi` 无头）里用 C++ 调用 `UNiagaraExternalEditUtilities`，依次完成：从引擎模板建系统与发射器、加 User 参数、设置模块输入（链接、HLSL 表达式、字面量、枚举）、直接改渲染器 UPROPERTY、编译并检查 errors=0、存盘。
- **Evidence**：实测 4 次运行每一步都成功，日志在 `vfx/niagara/attempt*.log`（本机）。
- **Applicability**：所有 Niagara 特效。该 API 在头文件中标注为 EXPERIMENTAL，升级引擎时需复查。

### 3.4 早期 Niagara 闪电（REJECTED）
- **Context**：第一版 `NS_LDChain` 使用 Ribbon 加锯齿图集，带平滑抖动。
- **Decision**：否决。
- **Rationale**：Agent 判断与独立 QA 都认为它像扭动的绳子，长链会把贴图拉长，白芯与黑边比例失衡；用户反馈同 3.1。
- **Evidence**：`vfx/rejected/chain-niagara-v1.jpg`。

### 3.5 闪电链三候选与 A+（APPROVED：A+）
- **Context**：在相同地图、相机和真实事件下比较三种方向：A 风格化 3D 能量、B 精致街头漫画、C 竞技清晰。
- **Alternatives**：B 被否决（独立 QA：像纸带，端点脱离目标，他人比自己宽）；C 只保留它的时序与降级规则（QA：命中几乎感觉不到，像激光）。
- **Decision**：用户选 A，并要求更华丽、击中更明显，由此产出 A+；用户批准 A+。
- **Rationale**：用户反馈为选择 A，并给出“更华丽复杂、明显击中”的要求；随后批准 A+。QA 指出的拖尾软线、针状分支、白圆片、身体光噪点与他人难分，均已在 A+ 中修复。
- **Evidence**：`vfx/approved/*`，`vfx/rejected/chain-candidate-b.jpg`；完整 QA 报告见本机 `vfx-direction/QA.md`。

### 3.6 电场首版（REJECTED，重新设计中）
- **Context**：按 1.3 的原则做导电电场（地面电场状态半径 220，导电溅射半径 450），每个使用各自的权威半径。
- **Decision**：未通过。
- **Rationale**（Agent 与 Lead 看图判断，用户尚未看过）：
  - 边缘读起来就是一个普通白圆圈；
  - 场内电感偏弱，日照地面上更弱；
  - 地面层出现一个不透明的深灰方块（渲染缺陷，同一缺陷还以回归形式出现在闪电链上，正在修复）；
  - 地面上另有饱和黄色的折线标记，来源待查。
- **Evidence**：`vfx/rejected/field-grammar-proof.jpg`（新版行）。

### 3.7 区域特效不能套用闪电链结构（PROVISIONAL）
- **Context**：电场首版沿用了链的元素（细电弧、地面暖光），结果偏弱、像圆圈。
- **Decision**：区域特效需要自己的结构：一个能读出边界的电边、一个“带电地面”的表面层，以及与场内角色的连接；链的主干、细丝、分叉不适用。
- **Rationale**：Agent 判断。链是一维、瞬时、指向性的效果；区域是二维、持续、需要标出危险边界的效果。

## 4. 生产流程

`Blender 源资产 → 资产清单 → 导入 commandlet → 材质 → Niagara → 玩法事件 → 实机视觉验证`

1. **Blender 源资产**：`SourceData/VFX/Blender/slice_*.py`，用 Blender 5.2.4 无头运行，几何渲染，固定种子，同一脚本重复运行字节一致。生成器开头要设 `sys.dont_write_bytecode = True`，否则会留下 `__pycache__`，被导入白名单拒绝。输出到 `SourceData/VFX/Slice/`（Git LFS）。贴图约定：RGB = 辉光遮罩（0 墨、0.6 元素色、1 白芯），A = 覆盖率，线性；例外项写在清单里。
2. **清单**：`python -B SourceData/VFX/Blender/slice_manifest.py SourceData/VFX` 重算 `vfx_slice_assets.json` 的 sha256。导入器只接受清单里登记、且列在 `LDSliceVfx::ETex` 里的贴图。
3. **导入与生成**：`$env:LD_UE_ROOT='C:\Program Files\Epic Games\UE_5.8'; .\Scripts\ImportDemoArt.ps1 -Mode ImportVFX`。依次导入贴图、生成材质、在 `LDDemoVfxNiagara.cpp` 里生成 Niagara 系统并存盘。
4. **所有权**：
   - `/Game/Lockdown/Art/VFX/Niagara/Generated/` 归生成器所有：每次导入都会重建并覆盖，目录里不是本次生成的包会被删除，所以**禁止手工编辑**；
   - 生成器拒绝写入该目录以外的路径；
   - 美术调参走 User 参数、`DA_CombatVfx` 或生成代码；
   - 手工制作的特效资产放在 Generated 以外，生成器不会触碰。
   - 资产字节每次不同（引擎给 SetParameters 模块随机命名），可复现性看结构摘要 CRC，导入日志会打印。
5. **运行时**：`LDSliceVfx`（配方）、`LDImpactFx`（池化与生成）只在真实的施法、释放与命中事件上触发，不伪造命中；`DA_CombatVfx` 是美术可编辑入口。
6. **实机验证**：
   - `.\Scripts\CaptureDemoArt.ps1 -Width 1920 -Height 1080 -CombatVfx -VfxProof chain,hit,swing,field -ProofLabel <名字>` 采集固定场景（包括多人同时施放和日照地面）；
   - `python Scripts/MakeVfxProofEvidence.py` 生成对比图、30 fps 实时 mp4 与慢放版；
   - 性能每种跑 3 次，记录 ΔGPU 与游戏线程的变化范围。
   - 判断以正常速度、游戏尺度为准。同一台机器同一时间只跑一个渲染采集。
7. **复现已批准的闪电链**：先 `ImportVFX`，再用 `-VfxProof chain` 采集，与 `vfx/approved/` 逐场景对比；不一致即视为回归。

## 5. 新特效工作流

1. 读本页 §1 与相关的 §2 条目，看 `vfx/approved/` 的实机参考。
2. 判断功能类别（§1.4）与视觉权重（§1.3）。复用原则，不机械复制已有特效的形状。
3. 设计、实现，按 §4.6 做实机验证。最多两轮有针对性的迭代；仍不成立就换设计或换资产路线。
4. 交给用户验收。用户明确批准后，才把它加入 §2，并把证据放进 `vfx/approved/`。否决的方案在 §3 留一条简短原因，并把证据放进 `vfx/rejected/`，避免重复探索。
5. 改动不得改变 §2 里已批准特效的画面。

# VFX 手册：美术方向、批准目录与生产流程

世界空间战斗特效的统一入口。做任何特效前先读本页：

- §1 设计知识：全局原则、三维分类、元素矩阵、可参考手法；
- §2 批准目录：视觉回归基准；
- §3 决策记录；
- §4 技术缺陷；
- §5 生产流程；
- §6 新特效工作流。

界面（HUD、构筑页）的直播风格见 [视野与 UI](VISION_AND_UI.md)，不直接用于世界空间特效。

**状态**：

| 状态 | 含义 |
| --- | --- |
| APPROVED | 用户看过实机画面后明确批准 |
| REJECTED | 用户明确否决，或实测不成立 |
| PROVISIONAL | 尚未经用户验收的推导或方案 |
| UNEXPLORED | 尚未设计 |
| SUPERSEDED | 被取代 |

**来源**：

| 来源 | 含义 |
| --- | --- |
| 用户 | 用户明确说过或批准、否决的内容 |
| Agent | 实现或统筹 Agent 的判断 |
| QA | 独立视觉 QA 的判断 |
| 实测 | 采集、日志或性能测量的结果 |

只有“用户 + APPROVED”的条目是正式标准；其余都是可参考的依据，不是规范。

视觉证据在 `docs/systems/vfx/`（Git LFS）：`approved/` 只放已批准版本，`rejected/` 放被否决的方案。完整采集（逐帧、慢放、性能 JSON）在本机忽略目录 `Artifacts/Audits/PresentationOverhaul/vfx-direction/`，只作补充，可能被清理。

## 1. 设计知识

### 1.1 全局原则

| 原则 | 状态 · 来源 |
| --- | --- |
| 世界空间特效要与现有风格化 3D 人物、自然光和场景材质协调，不能像贴在三维世界上的二维图案 | APPROVED · 用户 |
| 禁用粗黑描边、过饱和纯色平涂、机械规则锯齿、大型漫画星形爆点（证据 `vfx/rejected/chain-mesh-comic.jpg`） | REJECTED · 用户 |
| 命中必须在正常速度、正常镜头下明显可读（用户对闪电链提出，是否适用于所有命中待验收） | APPROVED（闪电链）· 用户 |
| 区分战术信息重要性和视觉冲击强度（见 §1.2） | 用户提出的设计方向 |
| 高威胁技能的预警，不能因为来自其他玩家而降低可辨识度；降级时优先去掉非必要装饰 | 用户提出的设计方向 |
| 统一语言不等于统一形态：每个元素、每种功能各用合适的材质与运动 | 用户提出的设计方向 |

### 1.2 三维设计分类

每个特效由三个维度共同决定设计，而不是套用某个已有特效的外形：

- **元素身份**（Element Identity）：形状、材质、运动和颜色，让人认出“这是哪种元素”。见 §1.3。
- **功能类别**（Functional Category）：链、区域、命中、挥击、投射物、拖尾、状态附着。决定空间结构（一维、二维、附着在身上）和时间结构（瞬时、持续、跟随）。见 §1.4。
- **战斗优先级**（Combat Priority），分四项单独判断：

| 项 | 判断什么 |
| --- | --- |
| 战术信息重要性 | 玩家必须读到的信息：危险范围、预警、命中归属 |
| 视觉冲击强度 | 亮度、面积、动势 |
| 持续时间 | 瞬时，还是持续数秒 |
| 叠加成本 | 多人同时出现时的视觉噪声与性能开销 |

信息重要性和冲击强度可以背离。例如持续危险区域，信息重要性高，但视觉噪声应该低：边界清楚，内部安静。他人的特效降级时，先去掉装饰（火花、细丝、身体反应），不降低预警和危险边界的可辨识度。

### 1.3 元素矩阵

| 元素 | 链 | 命中 | 区域 | 其他 | 备注 |
| --- | --- | --- | --- | --- | --- |
| 电 | **APPROVED**（§2.1） | 只批准作为链的一部分 | PROVISIONAL，重新设计中（§3.6） | UNEXPLORED | 颜色与 HUD 的金色同为 `#FFD23D`，是否调整由用户决定 |
| 水 | — | UNEXPLORED | UNEXPLORED | 水柱 REJECTED（首版网格） | |
| 火 | — | UNEXPLORED | UNEXPLORED | UNEXPLORED | |
| 冰 | — | UNEXPLORED | UNEXPLORED | UNEXPLORED | |
| 毒 / 腐 | — | UNEXPLORED | UNEXPLORED | UNEXPLORED | |
| 物理 / 无元素 | — | PROVISIONAL（语法示范，用户未验收） | — | 挥击 PROVISIONAL | |

### 1.4 功能类别

| 类别 | 当前要求 | 状态 · 来源 |
| --- | --- | --- |
| 链 | 见 §2.1 | APPROVED · 用户 |
| 命中 | 沿攻击方向、能连到受击身体；档位靠轮廓和丰富度区分 | PROVISIONAL · Agent |
| 挥击 / 瞬时攻击 | 围绕身体的 3D 运动弧，不是平贴月牙；预兆不能用深色图形 | PROVISIONAL · Agent；深色月牙预兆被 Agent 判为不通过 |
| 持续区域 | 危险边界清楚，内部安静，不压角色，有进出场 | PROVISIONAL · Agent；以纯圆环为主要读法被判不通过 |
| 投射物、拖尾、状态附着 | 尚未设计 | UNEXPLORED |

### 1.5 可参考的手法

下面这些出自已批准的闪电链，可以参考，但不是强制规范。是否采用，看元素和类别是否适合：

- 暖白核心加降饱和外层：适合能量类，不一定适合水、火、冰、毒或物理攻击。
- 离散折线、每秒多次整体重随机：电专属。
- 主体硬切加 1 帧暗残影：适合瞬时类，不适合持续区域。
- 命中的组合：方向短光 + 身体反应（边缘光、身体上爬的电弧）+ 地面光。
- 他人效果降级：去掉装饰、变窄、变暗（具体比例只在闪电链上实测过）。
- 生成器 + User 参数的结构：适用于所有 Niagara 特效（§5）。

### 1.6 性能

- **预算**（PROVISIONAL · Agent）：8 人同时出手时，每类 ΔGPU 约 +0.5 ms 以内。这是单类的目标，不代表全量混战叠加的预算；全量预算尚未定。
- **实测**：只写在各批准条目里。单个特效的实测数据，不能证明全量多人特效叠加满足预算。

## 2. 批准目录

### 2.1 闪电链（APPROVED · 用户，视觉回归基准）

**技能与事件**
- `active_zap`（电你个哆嗦）每次被匹配到的电命中，包括首击和每次弹跳。
- 起点是施法手或上一个受击者，终点是当前受击者胸口。
- 导电反应等其他电效果不在本条目内。

**批准版本**
- 用户批准的是候选 **A+**，实现提交 `315a6a8e`，分支 `presentation/combat-vfx`。
- 之后在 `dab423a9` 改名为唯一外观 `NS_LDChain`。
- 批准对象是 A+ 的画面，不是候选 A、B、C 或任何旧版。对比图里出现多个候选时，以 `vfx/approved/` 为准。

**视觉证据**
- `vfx/approved/chain-sheet-1x-2x.jpg`：7 个场景（短跳、长跳 640 cm、首击、两次弹跳、自己与他人、多人同时、日照地面），上行 1×，下行 2×。
- `vfx/approved/chain-*.jpg`：各场景 6 帧关键帧。
- `vfx/approved/chain.mp4`：正常速度 30 fps。

**实现参数**（每一跳，A+ 的实际值）
- 主干：
  - 离散折线，段长约 55 cm，每秒整体重随机 25 次，宽度随之起伏；
  - 亮脉冲从起点跑向终点；
  - 满亮 130 ms，1 帧暗残影，165 ms 切断。
- 细丝：2 条绕主干扭转，经过同一批折点，相位独立，只在自己的链上。
- 分叉：3 根短折线叉，只从主干中段长出，每 2 帧随机显隐，不出现超过 45° 的尖刺。
- 命中：
  - 7 道锯齿短光，偏向跳跃方向，持续 100 ms；
  - 受击身体上 3 条小电弧爬 0.3 s，源端 1 条；
  - 火花带轻重力，寿命 0.35–0.55 s，自己的链 14 颗，他人的 5 颗；
  - 0.2 s 暖色无阴影点光；
  - 地面 0.26 s 暖光，叠一层 0.55 s 的淡焦痕；
  - 受击身体 0.22 s 边缘光（`M_LDChainBodyGlow`：加性菲涅尔外壳，沿法线外推 1.2 cm，跟随姿态）；
  - 弹跳目标略小：链宽 56，首跳 70。
- 施法手：40 cm 蓄能暖光，没有星爆和白圆片。
- 他人的链：宽 60 %、亮 50 %、火花 5 颗，没有细丝和身体光。
- 色彩：核心暖白，外层暖白到浅金 `Body (1, .68, .26)`；不用纯 `#FFD23D` 平涂，没有描边。

**Niagara**
- 系统 `/Game/Lockdown/Art/VFX/Niagara/Generated/NS_LDChain`：主干 Ribbon、细丝、分叉、接触短光、火花；材质实例 `MI_LDChain_*`；身体光 `M_LDChainBodyGlow`。
- User 参数：

| 参数 | 含义 |
| --- | --- |
| `User.Start`、`User.End` | 起点、终点（世界坐标） |
| `User.Tint` | 颜色 |
| `User.Width` | 宽度（cm） |
| `User.Intensity` | 亮度 |
| `User.Duration` | 时长（s） |
| `User.Focus` | 1 = 自己，0 = 他人 |

**源资产**
- 贴图在 `SourceData/VFX/Slice/`：`chainA_profile`、`chainA_energy_noise`、`chainA_spark_streak`、`chainA_flash`、`chainA_ground_glow`、`chainB_scorch_dash`。名字沿用候选阶段，都属于 A+。
- 生成脚本：`SourceData/VFX/Blender/slice_chain_candidates.py`、`slice_render.py`。
- 清单：`SourceData/VFX/vfx_slice_assets.json`。

**运行时**
- `LDSliceVfx`（`Character/LDHitVfx*`）用可见施法者登记的击打，按时间距离匹配电命中。
- `LDImpactFx` 生成池化的 `NS_LDChain` 组件，写入 User 参数，挂在受击者上，到时长后释放。
- 命中本身不带来源；施法者不可见时，按命中外观回退，不泄露隐藏单位。

**实测性能**（编辑器 `-game`，1080p，8 人施法减空闲，3 次）
- ΔGPU +0.14 ms（0.09–0.16）；链组件峰值 24，粒子峰值 1316。
- 游戏线程增量在本机波动 0.6–2.0 ms，和网格基线同一量级。
- 打包版与全量混战叠加都未验证。

**适用范围**
- 可以参考：§1.5 列出的手法。
- 链专属，不要推广：折线主干、细丝、分叉、沿链脉冲（见 §3.7）。

**保护**
- 任何重构、模板升级或资产重新生成之后，`NS_LDChain` 的画面都必须与 `vfx/approved/` 一致。
- 修复缺陷时，只恢复批准画面，不借机改设计。
- 重新设计需要用户明确要求。

## 3. 决策记录

### 3.1 原始网格切片特效（REJECTED · 用户）
- **Context**：第一版的普攻、技能命中、水柱、电链和电场，用池化 StaticMesh + MID，形状是带黑描边的漫画月牙、星爆和图案。
- **Alternatives**：当时没有比较 Niagara 方案（见 3.2）。
- **Decision**：整体否决。
- **Rationale**：用户反馈：粗黑描边、过饱和纯黄、机械锯齿、大星爆，像贴在三维世界上的二维图案，与地图和人物画风不协调；旧版本本身也没达到满意的品质。
- **Evidence**：`vfx/rejected/chain-mesh-comic.jpg`。
- **Applicability**：被否决的是这套图形语言。网格 + 材质作为实现手段仍然可用。

### 3.2 “Niagara 不可程序化生成”的误判（REJECTED · 实测）
- **Context**：VFX Agent 以此为由改用网格方案。
- **Alternatives**：编辑器 MCP 的 NiagaraToolsets；C++ 调用 `UNiagaraExternalEditUtilities`；复制模板、只改渲染器。
- **Decision**：撤回该结论，采用 3.3 的路线。
- **Rationale**：
  - 实测：这个结论从未尝试过，没有任何错误证据。
  - 实测：用 MCP 建系统、读结构都成功了；但一次格式错误的 `AddUserVariables` 写入了无效参数，导致编辑器断言崩溃（`NiagaraVariant.cpp:108 InCount > 0`）。所以这个实验插件只适合查看，不适合生产。
- **Evidence**：`Artifacts/Audits/PresentationOverhaul/vfx/NIAGARA_PROOF.md`、`vfx/niagara/lead-mcp-crash-excerpt.txt`（本机）。
- **Applicability**：以后凡是“工具不可行”的结论，都要附上实际尝试和错误证据。

### 3.3 Niagara 生产路线（APPROVED · 用户接受工具链；技术细节为实测）
- **Context**：需要可复现、不靠人工操作的 Niagara 资产。
- **Decision**：在 VFX 导入 commandlet（`-nullrhi` 无头）里，用 C++ 调 `UNiagaraExternalEditUtilities`，依次完成：
  1. 从引擎模板建系统和发射器；
  2. 加 User 参数；
  3. 设置模块输入（链接、HLSL 表达式、字面量、枚举）；
  4. 直接改渲染器 UPROPERTY；
  5. 编译，检查 errors=0；
  6. 存盘。
- **Evidence**：4 次运行每一步都成功，日志在 `vfx/niagara/attempt*.log`（本机）。
- **Applicability**：所有 Niagara 特效。这个 API 在头文件里标注为 EXPERIMENTAL，升级引擎时要复查。

### 3.4 早期 Niagara 闪电（REJECTED · Agent、QA、用户）
- **Context**：第一版 `NS_LDChain`：Ribbon 加锯齿图集，带平滑抖动。
- **Rationale**：
  - Agent 与 QA：像扭动的绳子，长链会把贴图拉长，白芯和黑边比例失衡。
  - 用户：理由同 3.1。
- **Evidence**：`vfx/rejected/chain-niagara-v1.jpg`。

### 3.5 闪电链三候选与 A+（APPROVED：A+ · 用户）
- **Context**：在同一地图、同一相机、真实事件下比较三个方向：A 风格化 3D 能量、B 精致街头漫画、C 竞技清晰。
- **Alternatives**：
  - B：QA 判断像纸带，端点会脱离目标，他人的比自己的还宽。
  - C：QA 判断命中几乎感觉不到，像激光。
- **Decision**：用户选 A，要求更华丽复杂、击中更明显；之后批准了 A+。
- **Rationale**：QA 指出的问题都在 A+ 里修了：拖尾软线、针状分支、白圆片、身体光噪点、自己和他人难区分。
- **Evidence**：`vfx/approved/*`、`vfx/rejected/chain-candidate-b.jpg`；QA 全文在本机 `vfx-direction/QA.md`。

### 3.6 电场首版（REJECTED · Agent；用户尚未看过）
- **Context**：导电电场有两种用法，各用自己的权威半径：地面电场状态（半径 220）和导电溅射（半径 450）。
- **Rationale**（Agent 看图判断）：
  - 边缘读起来就是一个普通白圆圈；
  - 场内的电感偏弱，日照地面上更弱；
  - 地面上有饱和黄色的折线标记，来源待查。
  - 深灰方块是渲染缺陷，记在 §4，不属于设计问题。
- **Evidence**：`vfx/rejected/field-grammar-proof.jpg`（新版那一行）。

### 3.7 区域特效不照搬闪电链（PROVISIONAL · Agent）
- **Context**：电场首版沿用了链的元素（细电弧、地面暖光），结果偏弱、读成圆圈。
- **Decision**：区域特效要按 §1.2 单独设计：信息重要性高，视觉噪声低，需要清楚的危险边界和一层“状态表面”。
- **Rationale**：Agent 判断。链是一维、瞬时、有方向的；区域是二维、持续、需要标出边界的。

## 4. 技术缺陷

| 缺陷 | 影响 | 状态 |
| --- | --- | --- |
| 地面层出现不透明的深灰方块 | 回归：已批准的闪电链目标脚下；另见电场“导电”场景 | 修复中。修复要与 `vfx/approved/` 逐场景比对，只恢复批准画面；修复时要加一道检查：生成的地面和粒子材质必须有遮罩贴图，且必须是半透明或加性混合 |

## 5. 生产流程

`Blender 源资产 → 资产清单 → 导入 commandlet → 材质 → Niagara → 玩法事件 → 实机视觉验证`

1. **Blender 源资产**：
   - 脚本是 `SourceData/VFX/Blender/slice_*.py`，用 Blender 5.2.4 无头运行，做几何渲染，固定种子，重复运行字节一致。
   - 脚本开头要设 `sys.dont_write_bytecode = True`，否则留下的 `__pycache__` 会被导入白名单拒绝。
   - 输出到 `SourceData/VFX/Slice/`（Git LFS）。
   - 贴图约定：RGB 是辉光遮罩（0 = 墨、0.6 = 元素色、1 = 白芯），A 是覆盖率，线性；例外写在清单里。
2. **清单**：运行 `python -B SourceData/VFX/Blender/slice_manifest.py SourceData/VFX`，重算 `vfx_slice_assets.json` 的 sha256。导入器只接受清单里登记、并列在 `LDSliceVfx::ETex` 里的贴图。
3. **导入与生成**：运行 `$env:LD_UE_ROOT='C:\Program Files\Epic Games\UE_5.8'; .\Scripts\ImportDemoArt.ps1 -Mode ImportVFX`，它依次导入贴图、生成材质，再由 `LDDemoVfxNiagara.cpp` 生成 Niagara 系统。
4. **所有权**：
   - `/Game/Lockdown/Art/VFX/Niagara/Generated/` 归生成器所有：每次导入都重建覆盖，目录里不是本次生成的包会被删除，所以禁止手工编辑。
   - 生成器拒绝写入这个目录以外的路径；手工资产放在目录外，生成器不会碰。
   - 美术调参走 User 参数、`DA_CombatVfx` 或生成代码。
   - 资产字节每次都不同（引擎给 SetParameters 模块随机命名），所以可复现性看导入日志打印的结构摘要 CRC。
5. **运行时**：`LDSliceVfx`（配方）和 `LDImpactFx`（池化与生成）只在真实的施法、释放、命中事件上触发，不伪造命中。美术可编辑的入口是 `DA_CombatVfx`。
6. **实机验证**：
   - 采集：`.\Scripts\CaptureDemoArt.ps1 -Width 1920 -Height 1080 -CombatVfx -VfxProof chain,hit,swing,field -ProofLabel <名字>`，场景固定，包括多人同时出手和日照地面。
   - 证据：`python Scripts/MakeVfxProofEvidence.py` 生成对比图、30 fps 实时 mp4 和慢放版。
   - 性能：每种跑 3 次，记录变化范围。
   - 以正常速度、游戏尺度判断。同一台机器同一时间只跑一个渲染采集。
7. **复现已批准的闪电链**：先 `ImportVFX`，再用 `-VfxProof chain` 采集，和 `vfx/approved/` 逐场景对比；有任何不一致都算回归。

## 6. 新特效工作流

1. 读本页 §1 和相关的 §2 条目，看 `vfx/approved/` 的实机参考。
2. 按 §1.2 判断元素身份、功能类别和战斗优先级。参考 §1.5 的手法，不要机械复制已有特效的外形。
3. 设计、实现，按 §5.6 做实机验证。最多两轮有针对性的迭代；仍不成立，就换设计或换资产路线。
4. 交给用户验收：
   - 用户明确批准后，才加入 §2，证据放进 `vfx/approved/`，相应的 §1 条目才能从 PROVISIONAL 改成 APPROVED。
   - 被否决的方案，在 §3 留一条简短原因，证据放进 `vfx/rejected/`。
5. 任何改动都不能改变 §2 里已批准特效的画面。渲染缺陷记到 §4。

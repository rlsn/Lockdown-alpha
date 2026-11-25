# VFX 手册：美术方向、批准目录与生产流程

世界空间战斗特效的统一入口。做任何特效前先读本页：

- §1 设计知识：全局原则、三维分类、元素矩阵、可参考手法；
- §2 批准目录：视觉回归基准；
- §3 决策记录；
- §4 技术缺陷；
- §5 生产流程；
- §6 新特效工作流；
- §7 覆盖清单。

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
| 电 | **APPROVED**（§2.1） | 只批准作为链的一部分 | **APPROVED**（§2.2） | UNEXPLORED | 颜色与 HUD 的金色同为 `#FFD23D`，是否调整由用户决定 |
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
| 持续区域 | 危险边界清楚，内部安静，不压角色，有进出场 | 电场的具体做法 APPROVED · 用户（§2.2）；作为所有区域的通用要求仍是 PROVISIONAL |
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

### 2.2 电场（APPROVED · 用户，视觉回归基准）

**状态与事件**
- 两种用法共用一个效果，各按自己的权威半径绘制：
  - 地面电场状态，半径 220 cm；
  - 导电反应的溅射场，半径 450 cm。
- 场内伸向人的短电弧只给站在场里的人。

**批准版本**
- 用户批准的是候选 **F3**（F1 的电栅栏边 + 带电地面），以 `presentation/combat-vfx` 的 `808949ca` 为准。这一版同时删除了地面状态的黄色折线种类图案。
- 生成器里的名字是 `NS_LDField`。
- 不批准的版本：F1（只有电边）、F2（柔边加划痕内部）、首版。

**视觉证据**
- `vfx/approved/field-sheet.jpg`：五个场景各取一帧，从左到右依次是阴影地面 r220、日照地面 r220、导电 r450、两场重叠且场内有打斗、自己与他人。
- 每个场景的 1600 px 单帧：`vfx/approved/field-*.jpg`。
- `vfx/approved/field.mp4`：两场重叠场景的连续帧，按 30 fps 合成。

**构图与实现**
- 边：
  - 一条约 30 cm 宽的活电带：暖白热芯，12 Hz 重绘的碎裂电纹，边缘抖动，三道亮脉冲绕圈跑。
  - 颜色从白偏向暖金，不用纯 `#FFD23D` 平涂。
  - 外侧贴一道窄的暗琥珀阴影边，保证日照地面上的对比。
  - 明灭段只降到约三分之一亮度，危险边界始终连续。
  - 电带上方有跳弧：自己的场 7 条，他人的场 3 条。
- 内部：
  - Blender 元素贴图 `field_web`：可平铺、各向同性、带分叉的电裂网。另有 `field_energy_noise`：R、G 是能量团，B 是与裂网对齐的格子编号。脚本是 `SourceData/VFX/Blender/slice_field.py`。
  - 按世界坐标采样，每格约 320 cm，两种尺度叠加；每秒整体重随机 3 次并缓慢漂移，个别格子短暂闪亮。
  - 整体低对比，靠近边缘处淡出。
- 过渡：开场从 75 % 半径扩到全尺寸，边亮闪一下；结束时地面先退，边再收窄、淡出。
- 他人的场：边界与自己的完全相同，只减少装饰（跳弧、地面裂网亮度、伸向人的电弧）。这一条是按战斗优先级做的：边界属于战术信息，不降级。
- 地面状态的座位色寿命轮廓（`ALDObservedGroundProxy`，玩法侧）保留，叠在电边内侧。

**实测性能**（8 人连段挥击 + 8 块他人的 r220 电场，减去空闲，3 次）
- ΔGPU +0.26 ms（0.24–0.28）；Δ游戏线程 +0.70 ms（0.59–0.90）；粒子峰值 1360。
- 打包版与全量混战叠加都未验证。

**已知小问题**（用户批准时已知）
- 阴影地面上，暗琥珀边略显棕色晕边。
- 日照地面上，内部裂网偏淡。
- 阴影一侧，圆盘整体略微发白。
- 正常速度下，格子闪亮不太明显。

**适用范围**
- 可以参考：“边界承担战术信息、内部只承担元素氛围”的分工；暗色窄边在亮地面上拉开对比；他人的场只减装饰。
- 电专属，不要推广：碎裂电纹、跳弧、电裂网。

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

### 3.6 电场首版（SUPERSEDED，被 3.8 取代）
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

### 3.8 电场三候选（APPROVED：F3 · 用户）
- **Context**：在同一套场景下比较边界承担身份（F1 电栅栏）和地面承担身份（F2 带电地面）两种结构。
- **Alternatives**：
  - F1：电感最强，但白色电带在日照地面上对比不足，熄灭段让边界断开（Agent）。
  - F2：日照下最稳，但内部像粉笔划痕，整体读成“发光圆圈加一层染色”（Agent）。
- **Decision**：用户选“F1 的边 + F2 的带电地面”。由此做出 F3，修了 F1 的对比与断边，内部换成新的电裂网贴图。用户批准 F3。
- **玩法侧地面图案**：首版说的“普通白圈”，有一部分其实是玩法侧按座位色画的寿命轮廓（实测）。电场的黄色折线种类图案，按用户要求删除，因为电场效果本身已经能表明种类。其他地面状态的种类图案还保留，等各自的特效做好再定。
- **Evidence**：`vfx/approved/field-*`；本机 `vfx-direction/field-redesign/`（三候选对比）。

## 4. 技术缺陷

| 缺陷 | 影响 | 状态 |
| --- | --- | --- |
| 共享切片材质 `M_LDSlice` 的默认 Mask 指向被删除的贴图，材质编译失败后引擎改用不透明灰色表面：闪电链目标脚下的地面光和焦痕、粉笔线与手部闪光变成深灰方块（导电场景同因） | 已批准闪电链的视觉回归 | 已修复（`presentation/combat-vfx` 的 `3188bc64`）。根因是导入器的材质签名不含默认贴图，删除贴图后材质没有重建。修复后签名包含每个贴图参数的默认值；`LDDemoVfxImport::Validate` 先执行 `VfxCheckReferences`，检查 Hit、Slice、Niagara/Generated 下的材质、材质实例与系统，所引用贴图必须存在并登记在清单中，混合模式必须是半透明或加性，任何一项不满足都让导入失败。已逐场景比对 `vfx/approved/` 并确认一致 |

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
7. **复现已批准的效果**：先 `ImportVFX`，再用 `-VfxProof chain,field` 采集，和 `vfx/approved/` 逐场景对比；有任何不一致都算回归。

## 6. 新特效工作流

1. 读本页 §1 和相关的 §2 条目，看 `vfx/approved/` 的实机参考。
2. 按 §1.2 判断元素身份、功能类别和战斗优先级。参考 §1.5 的手法，不要机械复制已有特效的外形。
3. 设计、实现，按 §5.6 做实机验证。最多两轮有针对性的迭代；仍不成立，就换设计或换资产路线。
4. 交给用户验收：
   - 用户明确批准后，才加入 §2，证据放进 `vfx/approved/`，相应的 §1 条目才能从 PROVISIONAL 改成 APPROVED。
   - 被否决的方案，在 §3 留一条简短原因，证据放进 `vfx/rejected/`。
5. 任何改动都不能改变 §2 里已批准特效的画面。渲染缺陷记到 §4。

## 7. 覆盖清单

范围来自 `SourceData/Demo/content.json`（24 个主动技能、94 张卡与勋章、5 种元素、10 种反应、5 种地面状态、12 种野怪招式与院长）和客户端能看到的权威事件（`FLDObservedUnitView` 的动作提示、命中外观与标志、状态、反应，`FLDObservedProjectileView` 的 24 种外观，`FLDObservedGroundState`，阵亡通知）。特效只能挂在这些事件上；没有公开事件的效果不能凭空表现，记为 BLOCKED 并写明缺什么。

状态：NOT STARTED / IN PROGRESS / IMPLEMENTED / LOCALLY VERIFIED / USER APPROVED / BLOCKED。“旧”指仍在运行、但属于 §3.1 否决语言的表现（网格月牙、星爆、粗黑边、平涂几何体），要整体替换，不算覆盖。证据目录 `production/vfx/` 指本机 `Artifacts/Audits/PresentationOverhaul/production/vfx/`。

### 7.1 普攻与连段

| 项 | 事件 | 状态 | 实现 · 资产 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- | --- |
| 连段 1、2 挥击 | Attack 提示，步 1–2，释放时刻 | IMPLEMENTED | `NS_LDSwing1/2`（`SwingParts`） | — | 用户未验收 |
| 连段 3 终结挥击 | Attack 提示，步 3 | IMPLEMENTED | `NS_LDSwing3` | — | 用户未验收 |
| 挥击预兆 | Attack 提示的前摇 | IMPLEMENTED | `NS_LDTell1-3`：地面暖色细弧随前摇展开 | — | 旧版深色月牙不通过；新版待采集 |
| 普攻命中、终结命中 | 命中（无元素、无标志）匹配到挥击 | IMPLEMENTED | `NS_LDHitBasic`、`NS_LDHitFinisher` | — | 用户未验收 |
| 暴击命中 | 命中标志 Critical | NOT STARTED | 旧：十字星 + 双金环（`LDHitVfx::ForHit`） | — | 星爆属否决语言 |
| 护盾吸收、护盾破碎 | 命中标志 Shield / ShieldBroken | NOT STARTED | 旧：六角壳 | — | |
| 强化普攻 | 状态 Empowered（贴脸补一拳、左右开抽、蹿过去抡一拳） | NOT STARTED | 无 | — | |
| 远程普攻（隔空抽你） | 投射物 Chalk / ChalkReturn | NOT STARTED | 旧：粒子带 | — | |

### 7.2 主动技能

每个技能按自己的功能、元素与分量单独设计；“前摇 / 释放 / 命中 / 持续”列出它实际拥有的阶段。

| 技能 | 元素 · 形态 | 状态 | 实现 · 资产 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- | --- |
| `active_zap` 电你个哆嗦 | 电 · 单体链 | USER APPROVED（链） | `NS_LDChain`（§2.1） | `vfx/approved/chain*` | 前摇仍是旧粉笔线 |
| `active_hose_down` 滋你一脸 | 水 · 直线 600×160 | NOT STARTED | 旧：网格水柱 + 粉笔线（§1.3 已否决） | — | |
| `active_elbow` 滚远点 | 无 · 单体击退 | NOT STARTED | 旧：粒子包 | — | |
| `active_pants_down` 扒裤子 | 无 · 直线拉拽 + 定身 | NOT STARTED | 旧：粒子包 | — | |
| `active_crotch_grab` 掏蛋子 | 无 · 冲刺接触 + 减速 | NOT STARTED | 旧：冲刺粒子带 | — | |
| `active_unplug` 拧你手腕 | 无 · 招架架势 280 | NOT STARTED | 旧：粒子包 | — | |
| `active_snark_bolt` 甩你一鞋 | 无 · 投射物 + 落地拖鞋 | NOT STARTED | 旧：粒子带；落地 `SlipperDrop` | — | |
| `active_seethe` 烫你屁股 | 火 · 自身圆 300 | NOT STARTED | 旧：粒子包 | — | |
| `active_quick_meeting` 脚底开锅 | 火 · 地点圆 240，3 跳 | NOT STARTED | 旧：区域粒子（AreaFire） | — | |
| `active_bail_out` 贴脸补一拳 | 无 · 冲刺 + 强化普攻 | NOT STARTED | 旧：冲刺粒子带 | — | |
| `active_not_mad` 锅盖挡脸 | 无 · 自身护盾 + 解控 | NOT STARTED | 旧：护盾几何笼 | — | |
| `active_stare_down` 来打老子啊 | 无 · 自身圆 300 嘲讽 | NOT STARTED | 旧：粒子包 | — | |
| `active_triple_combo` 左右开抽 | 无 · 3 次强化普攻 | NOT STARTED | 旧：粒子包 | — | |
| `active_calculus_drop` 板砖糊脸 | 无 · 地点延迟落地 200 | NOT STARTED | 旧：`BookShadow` 粒子 | — | |
| `active_flyer_trap` 踩一脚试试 | 无 · 地点陷阱 180 | NOT STARTED | 旧：`FlyerArming/Armed` 粒子 | — | |
| `active_open_mic` 扯嗓子喷你 | 无 · 自身圆 340 击退 | NOT STARTED | 旧：`SpeechCone` 粒子 | — | |
| `active_charged_ram` 蛮牛顶 | 无 · 冲刺撞人 + 护盾 | NOT STARTED | 旧：冲刺粒子带 | — | |
| `active_brace` 顶着揍 | 无 · 架势 3 s | NOT STARTED | 无 | — | |
| `active_cola_shake` 冰可乐摇一摇 | 冰 · 扇形 400/60° | NOT STARTED | 旧：粒子包 | — | |
| `active_desk_fan` 电风扇 | 无 · 地点风带 600×200 | NOT STARTED | 旧：`Wind` 粒子 | — | |
| `active_stale_takeout` 过期外卖 | 腐 · 投射物 + 瘴气地面 | NOT STARTED | 旧：粒子带 | — | |
| `active_school_bag` 占座书包 | 无 · 地点障碍 | NOT STARTED | 旧：`Bag`、`BagWall` | — | |
| `active_flurry_rush` 疯狗乱拳 | 无 · 4 s 拳风波 | NOT STARTED | 旧：`FistWind` 粒子 | — | |
| `active_cordon_tape` 警戒封条 | 无 · 地点封条线 500 | NOT STARTED | 旧：`CordonTape` | — | |
| 技能前摇（通用部分） | Cast 提示前摇 | NOT STARTED | 旧：粉笔线 `SkillTelegraph`（否决语言） | — | 每个技能的前摇随技能设计 |
| 施法手蓄能 | Cast 提示前摇 | NOT STARTED | 旧：`CastCharge` 蓝色粒子 | — | |

### 7.3 元素状态（附着在身上）

| 状态 | 状态编号 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- | --- |
| 灼烧 Burn | 26 | NOT STARTED | 无世界表现，只有头顶图标 | — | |
| 潮湿 Wet | 30 | NOT STARTED | 无 | — | |
| 带电 Electrified | 31 | NOT STARTED | 无 | — | |
| 冰冷 Chilled | 36 | NOT STARTED | 无 | — | |
| 腐坏 Rotten | 37 | NOT STARTED | 无 | — | |

### 7.4 元素反应

| 反应 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- |
| 导电 Conduct（溅射电场） | USER APPROVED（场） | `NS_LDField` 450（§2.2） | `vfx/approved/field-conduct.jpg` | 头顶十字闪仍是旧层 |
| 雾 Fog | NOT STARTED | 旧：十字闪 + 地环 + 粒子（`ForReaction`） | — | |
| 过载 Overload | NOT STARTED | 同上 | — | |
| 融化 Melt | NOT STARTED | 同上 | — | |
| 沼气 Methane | NOT STARTED | 同上 | — | |
| 冻结 Freeze | NOT STARTED | 同上 | — | |
| 发酵 Ferment | NOT STARTED | 同上 | — | |
| 碎冰 Shatter | NOT STARTED | 同上 | — | |
| 腐蚀 Corrode | NOT STARTED | 同上 | — | |
| 冻伤 Frostbite | NOT STARTED | 同上 | — | |

### 7.5 持续区域与地面状态

| 项 | 事件 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- | --- |
| 电场 Field | 地面状态 5 | USER APPROVED | `NS_LDField` 220 | `vfx/approved/field*` | §2.2 已知小问题 |
| 水洼 Puddle | 地面状态 1 | NOT STARTED | 旧：种类线图 + 区域粒子 | — | |
| 冰面 Ice | 地面状态 2 | NOT STARTED | 同上 | — | |
| 火地 Fire | 地面状态 3 | NOT STARTED | 同上 | — | |
| 瘴气 Miasma | 地面状态 4 | NOT STARTED | 同上 | — | |
| 技能区域 Area* | 投射物 AreaPlain/Magic/Fire/Water/Electric | NOT STARTED | 旧：`NS_LDZone` 粒子 | — | 随各自技能设计 |

### 7.6 控制与增益状态

| 状态 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- |
| 定身 Root | NOT STARTED | 旧：品红圆盘 + 脚镣几何体，近黑边 | — | |
| 护盾 Shield | NOT STARTED | 旧：青色栅栏几何笼 | — | |
| 出生保护 RespawnProtection | NOT STARTED | 旧：光柱 + 环绕球 | — | |
| 减速 Slow、攻速减慢 AttackSlowed | NOT STARTED | 无 | — | |
| 眩晕 Stun | NOT STARTED | 无 | — | |
| 缴械 Disarm | NOT STARTED | 无 | — | |
| 沉默 Silence | NOT STARTED | 无 | — | |
| 嘲讽 Taunt | NOT STARTED | 无 | — | |
| 易伤 Exposed | NOT STARTED | 无 | — | |
| 击退 Knockback | NOT STARTED | 无（身体位移本身可见） | — | |
| 架势 Braced、属性增益 StatBoost | NOT STARTED | 无 | — | |
| 营地回位 CampReturning | NOT STARTED | 旧：背后牌子 | — | 野怪状态 |
| 野怪激怒 CampEnraged、守卫 CampGuarding | NOT STARTED | 无 | — | |

### 7.7 冲刺、闪避与爆劲

| 项 | 事件 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- | --- |
| 冲刺拖尾与起步 | 身体速度超过 950 cm/s | NOT STARTED | 旧：粒子带 + 残影 + 尘环 | — | |
| 完美闪避 | PerfectDodge 命中标志 | NOT STARTED | 旧：金色光环 | — | |
| 爆劲蓄力 | `bCharging` | NOT STARTED | 旧：能量柱 | — | |
| 爆劲就绪、释放、命中 | 蓄满、`bOverdriveCast`、Overdriven 标志 | NOT STARTED | 旧：品红十字 + 法阵 | — | |

### 7.8 预警与危险区域

| 项 | 事件 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- | --- |
| 圆形预警 | 投射物 AreaMagic + `WarningRemainingMs` | NOT STARTED | 旧：区域粒子 | — | 野怪 Circle、EchoCircle、DoubleCircle |
| 扇形预警 | 投射物 ConeWarning | NOT STARTED | 旧 | — | 野怪 Cone、Fan、RotatingCone |
| 直线预警 | 投射物 LineWarning | NOT STARTED | 旧 | — | 野怪 SweepLine、Charge |
| 野怪投射物 | 投射物（Agent 招式） | NOT STARTED | 旧 | — | |
| 院长普攻 | Attack 提示（院长） | NOT STARTED | 走普攻挥击 | — | 体型更大，预兆是否单独设计待定 |

### 7.9 被动与卡牌

卡牌只在产生公开事件时有世界表现；其余只改数值或只给本人回执，世界里不应也不能单独表现。

| 卡牌 | 公开事件 | 状态 | 实现 | 已知问题 |
| --- | --- | --- | --- | --- |
| 疯狗乱拳 / 咒刃回响的拳风 | 投射物 FistWind | NOT STARTED | 旧 | |
| 幻影脱壳 | 投射物 PhantomDecoy | NOT STARTED | 旧 | |
| 反应装甲 | 投射物 ShardBurst | NOT STARTED | 旧 | |
| 一路结冰、挪地盘 | 地面状态（冰面 / 迁移） | NOT STARTED | 随地面状态 | |
| 元素勋章（火、水、电、冰、腐） | 命中外观与元素状态 | NOT STARTED | 随元素命中与状态 | |
| 其余 70 余张卡（触发、增长、换算） | 无公开事件 | BLOCKED | — | 观察协议只公开状态、命中外观与标志；新增事件要改网络，不在本阶段范围 |

### 7.10 全局事件

| 项 | 事件 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- | --- |
| 元素命中（火、水、电、魔法） | 命中外观 | NOT STARTED | 旧：星形 + 签名层（`ForHit`） | — | 电命中只在链内批准 |
| 阵亡 | 阵亡通知 | NOT STARTED | 旧：地裂 + 双环 | — | |
| 升级 | 单位 Level 增加 | NOT STARTED | 无世界表现 | — | |
| 自身脚下标记 | 本人身体 | NOT STARTED | `ALDOwnBodyMarker` | — | 在共享文件 `LDWorldViewRelay` |
| 旗杆圈与占领 | 占领圈与进度 | NOT STARTED | `LDCaptureMarker`、`LDCaptureFence` | — | |
| 出现与复活柱 | 单位出现 | NOT STARTED | 旧：光柱几何体 | — | |
| 恢复球 | 恢复球可见 | NOT STARTED | `LDRecoveryOrbMarker` 两道彩环 | — | |

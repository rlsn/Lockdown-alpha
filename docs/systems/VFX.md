# VFX 手册：美术方向、批准目录与生产流程

世界空间战斗特效的统一入口。做任何特效前先读本页：

- §1 设计知识：全局原则、三维分类、元素矩阵、可参考手法；
- §2 批准目录：视觉回归基准；
- §3 决策记录；
- §4 技术缺陷；
- §5 生产流程；
- §6 新特效工作流；
- §7 覆盖清单；
- §8 第三方素材（Asset First）。

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
| 整体是风格化半写实的质感 | APPROVED · 用户 |
| 美术完成度以已批准的闪电链（§2.1）为标杆：精致、丰富、清晰自然、与游戏画风协调。它是品质参考，不是形态模板，其他特效不复制闪电的形状 | APPROVED · 用户 |
| 存在感来自形状、材质、动态层次、元素辨识度和命中反馈；不靠放大尺寸、提高亮度、增加粒子数量或扩大爆炸规模 | APPROVED · 用户 |
| 正常游戏镜头下足够清晰，但不过度遮挡人物和战斗信息 | APPROVED · 用户 |
| 技能和元素反应不能平、淡，要有质感、层次和辨识度 | APPROVED · 用户 |
| 元素反应是独立的事件：玩家要明显感到发生了一种不同的元素组合。不用换色、套圈，也不把两个原有特效叠在一起 | APPROVED · 用户 |
| 充分利用专业素材包，但以实际视觉质量为准，不照搬素材包默认的夸张表现（大火球、大爆炸不采用） | APPROVED · 用户 |
| 普攻保持相对简洁，优先级低 | APPROVED · 用户 |
| 半写实、精致、清晰、克制的方向已确认；`active_cola_shake`（`NS_LDColaBlast`）是最接近目标的范例 | APPROVED · 用户 |
| 范围前摇保留细线：战斗信息优先，不为美术复杂化而影响可读性 | APPROVED · 用户 |
| 素材包不必整套使用，可以拆出其中质量好的材质、贴图、网格和 Niagara 模块 | APPROVED · 用户 |
| 元素的排布要自然，不机械等距（火苗、碎片、尘团的间距、高度、时机都要有变化） | APPROVED · 用户 |
| 不在单个效果上无限迭代：每个效果按确认的标准做一遍，最后统一看全量实机效果 | APPROVED · 用户 |

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

### 1.5 闪电链为什么读起来精致

对已批准闪电链（§2.1）的拆解，用来约束其他特效的完成度，不是照抄它的形状：

- **三层各有节奏**：主体（主干）、次级层（细丝、分叉）、点缀（火花）；每层的出现、闪动和消失时间都不同。
- **颜色克制**：暖白核心，外层降饱和；元素色只出现在外层和边缘，不做纯色平涂，颜色由材质的核心、主体、边缘三段承担。
- **短促而肯定**：满亮约 130 ms，硬切，留 1 帧残影；不拖泥带水。
- **落在身体和地面上**：受击身体的边缘光与爬行的小电弧、带重力的火花、短暂的点光、地面暖光和淡焦痕。特效与角色、地面有接触，才不像贴上去的。
- **他人的版本先去装饰**：变窄、变暗、去掉细丝和身体光，主体仍然可读。
- **专门绘制的贴图**：轮廓、能量噪声、火花条、闪光、地面光各有一张，不是一张通用光斑。

尺寸约束：正常游戏镜头下 1 像素约 2.5 cm，小于 12 cm 的粒子基本看不见；点缀层用少量 25 cm 以上、形状明确的元素，不用大量微粒。

### 1.5.1 可参考的手法

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
- **重场景实测**（编辑器 `-game`，1080p 验证舞台，独占 GPU，同一次运行内 3 s 空闲后 5 s 负载的平均帧时间；证据 `production/vfx/batch13-return-perf/` 的 `perf13-DemoArt.json`，探针类别 `load`）：

  | 场景 | 空闲 ms | 负载 ms | 增量 ms |
  | --- | --- | --- | --- |
  | 闪电链，8 人施法（基准，3 次） | 8.71 | 8.92 | +0.21 |
  | 电场，8 人连段 + 8 块电场（基准，3 次） | 8.73 | 9.01 | +0.28 |
  | 六个身体同时放 `active_seethe` 并各受一次火命中（2 次） | 8.81 | 9.04 | +0.22 |
  | 六个身体同时触发六种不同反应（2 次） | 8.85 | 9.00 | +0.15 |
  | PK 圈：安全圈 + 下一圈 + 四个投掷落点（2 次） | 8.85 | 9.04 | +0.19 |

  三个重场景的增量与闪电链、电场基准同一量级，都在单类预算内。基准用 8 个施法者，重场景用舞台上的 6 个身体；验证舞台本身很轻，这些是增量，不是真实对战的帧时间，也不证明全量混战叠加满足预算。

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
- **玩法侧地面图案**：首版说的“普通白圈”，有一部分其实是玩法侧按座位色画的寿命轮廓（实测），保留。
- **种类图案（APPROVED · 用户）**：五种地面状态的玩法侧几何种类图案（电场折线、水洼同心圆、冰面条纹、火地八角星、瘴气六边形）全部删除，种类只由各自设计的特效表明；评审开关 `ld.Vfx.GroundKindPatterns` 一并删除。水洼和瘴气失去图案后加厚了主体（水洼两层浸湿面与更实的水体，瘴气两层腐渍与更浓的低云）。地面状态的玩法与范围不变。
- **Evidence**：`vfx/approved/field-*`；本机 `vfx-direction/field-redesign/`（三候选对比）。

### 3.9 第一轮集中评审后的美术方向（APPROVED · 用户）
- **Context**：第一轮评审（普攻、元素技能、物理技能、自身技能、身上状态、地面状态、击杀）在正常游戏镜头下集中呈现。
- **Decision**：方向见 §1.1 中标为“APPROVED · 用户”的各条。普攻基本可以，保持简洁；技能和元素反应太平、太淡，缺少质感、层次和辨识度；`active_seethe` 用素材包做的大火球太大、太浮夸，不采用；闪电链是完成度标杆。
- **Applicability**：所有技能与元素反应。代表项（`active_hose_down`、`active_seethe`、`active_cola_shake`，起雾、冻住、过载三个反应）评审后方向确认，按同一标准推广到其余全部特效。
- **代表项的结论**：摇可乐最满意；水管明显改善；发火的大小和强度合适、不再放大，火苗排布要自然；冻住辨识度好，冰晶要有立体感；起雾要连成一团而不是几团独立白烟；过载不能像闪电，要是火电结合的爆裂。
- **Evidence**：`production/vfx/review1/`（评审材料）、`production/vfx/review2/`（代表项）。

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

状态：NOT STARTED / IN PROGRESS / IMPLEMENTED / LOCALLY VERIFIED / USER APPROVED / BLOCKED；“设计上不需要”表示该项没有独立特效，由所指的其他表现覆盖。“旧”指仍在运行、但属于 §3.1 否决语言的表现（网格月牙、星爆、粗黑边、平涂几何体），要整体替换，不算覆盖。证据目录 `production/vfx/` 指本机 `Artifacts/Audits/PresentationOverhaul/production/vfx/`。

### 7.1 普攻与连段

| 项 | 事件 | 状态 | 实现 · 资产 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- | --- |
| 连段 1、2 挥击 | Attack 提示，步 1–2，释放时刻 | LOCALLY VERIFIED | `NS_LDSwing1/2`（`SwingParts`） | `production/vfx/batch14-gate/` | 用户未验收 |
| 连段 3 终结挥击 | Attack 提示，步 3 | LOCALLY VERIFIED | `NS_LDSwing3` | `production/vfx/batch14-gate/` | 用户未验收 |
| 挥击预兆 | Attack 提示的前摇 | LOCALLY VERIFIED | `NS_LDTell1-3`：地面暖色弧随前摇展开，内侧一道淡扫掠带 | `production/vfx/batch2/swing/` | 用户未验收 |
| 普攻命中、终结命中 | 命中（无元素、无标志）匹配到挥击 | LOCALLY VERIFIED | `NS_LDHitBasic`、`NS_LDHitFinisher` | `production/vfx/batch14-gate/` | 用户未验收 |
| 暴击命中 | 命中标志 Critical | LOCALLY VERIFIED | 命中本身按其外观正常绘制，另加 `NS_LDCritBurst`：胸口一记硬金色闪光、向外甩出并落地的亮金碎屑；不再画十字星与双金环 | `production/vfx/batch11-flags-orb/` | 用户未验收 |
| 护盾吸收、护盾破碎、格挡 | 命中标志 ShieldAbsorbed / ShieldBroken / Parried | LOCALLY VERIFIED | 吸收与格挡：身体轮廓闪一下冷青色边缘光（格挡略长）；破碎：`NS_LDShardBurst` 壳片四散；不再画六角壳、青色星与十字 | `production/vfx/batch11-flags-orb/` | 用户未验收 |
| 强化普攻 | 状态 Empowered（贴脸补一拳、左右开抽、蹿过去抡一拳） | LOCALLY VERIFIED | 带 Empowered 状态的普攻不论连段步都按终结挥击 `NS_LDSwing3` 的宽弧挥出；身上的拳光见 §7.6 的强化普攻状态 | `production/vfx/batch14-gate/` | 用户未验收 |
| 远程普攻（隔空抽你） | 投射物 Chalk / ChalkReturn | LOCALLY VERIFIED | 粉笔本体（圆柱）+ `NS_LDFlightChalk`：亮白拖痕、身后膨胀悬停的粉笔灰、向两侧崩落的碎屑；旧网格拖尾与粒子爆发不再出现 | `production/vfx/batch9-props-control/` | 用户未验收 |

### 7.2 主动技能

每个技能按自己的功能、元素与分量单独设计；“前摇 / 释放 / 命中 / 持续”列出它实际拥有的阶段。

| 技能 | 元素 · 形态 | 状态 | 实现 · 资产 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- | --- |
| `active_zap` 电你个哆嗦 | 电 · 单体链 | USER APPROVED（链） | `NS_LDChain`（§2.1）；前摇 `NS_LDZapCharge` 手部蓄电（IMPLEMENTED） | `vfx/approved/chain*`；`production/vfx/batch1-selfmade/water/zap-charge` | 蓄电在游戏镜头下几乎看不见 |
| `active_hose_down` 滋你一脸 | 水 · 直线 600×160 | LOCALLY VERIFIED | `NS_LDHoseTell` 地面范围预兆；`NS_LDHoseJet`：半透明浅色水体 + 会断开的白色泡沫核、越远摆动越大、沿水流奔涌的泡沫团、末端白色水花、少量侧溅水滴、会干掉的湿痕；颜色以白与浅水色为主，不平涂元素色；命中 `NS_LDWaterHit` | `production/vfx/review2/` | 用户未验收；水柱仍偏直；落地水滴像小白片；预兆仍是细线框 |
| `active_elbow` 滚远点 | 无 · 单体击退 | LOCALLY VERIFIED | 前摇：通用族；`NS_LDElbowRelease`：身前加厚的弧形推压波 + 两道回波、被推向前方的暗色尘团；`NS_LDElbowHit`：接触点 0.1 s 暖白冲击核与光、胸口加粗的暖色冲击环、沿 300 cm 击退路径的暗色尘迹与地面拖痕、碎屑 | `production/vfx/batch5e-physical-mass/` | 用户未验收 |
| `active_pants_down` 扒裤子 | 无 · 直线拉拽 + 定身 | LOCALLY VERIFIED | 前摇：通用族；`NS_LDPantsRelease`：加粗的暖色绳鞭（地面上有暗色影绳）甩出到 440 cm、末端暖色脆响、再回抽；`NS_LDPantsHit`：脚踝暖白冲击核、两道加粗收紧的箍、朝施法者的拉扯线、脚边扬起的暗尘 | `production/vfx/batch5e-physical-mass/` | 用户未验收；脚边暗尘在亮地面上偏淡 |
| `active_crotch_grab` 掏蛋子 | 无 · 冲刺接触 + 减速 | LOCALLY VERIFIED | 前摇：通用族；`NS_LDGrabRelease`：三条加粗的贴地速度线随冲刺展开、暗色尘迹与终点刹车扬尘；`NS_LDGrabHit`：髋部高度的暖白冲击核与光、加粗的低位爆环、髋部暗尘、下坠暖色火星 | `production/vfx/batch5e-physical-mass/` | 用户未验收 |
| `active_unplug` 拧你手腕 | 无 · 招架架势 280 | LOCALLY VERIFIED | `NS_LDUnplugRelease`：招架期间（数据 0.45 s）胸前交叉的钢色双臂 X、颤动，身周细卸力环；`NS_LDUnplugHit`：反震打在出手者身上——绕胸收紧的扭腕螺旋、暖白冲击核、缴械碎屑下落 | `production/vfx/batch6-self/` | 用户未验收 |
| `active_snark_bolt` 甩你一鞋 | 无 · 投射物 + 落地拖鞋 | LOCALLY VERIFIED | 前摇：通用族；飞行：`NS_LDFlightChalk`（观察协议里它与粉笔同为 Chalk 外观）；落地 `SlipperDrop`：`SM_LD_PropSlipper` 人字拖网格按拾取半径缩放 + 细线拾取圈；命中：通用 Niagara 命中 | `production/vfx/batch9-props-control/` | 用户未验收；飞行中的本体仍是粉笔外观（协议不区分），专属命中未设计 |
| `active_seethe` 烫你屁股 | 火 · 自身圆 300 | LOCALLY VERIFIED | `NS_LDSeetheTell`；释放 `NS_LDSeetheBurst`：火舌成十一个疏密不一的簇冲到 300 cm 边缘并立住再熄，簇间有散苗，高度与时机各不相同（火焰前沿就是范围边界）、火舌脚下一层柔和的地面热光、发红后冷却的焦裂地面、少量上飘的火星；不用素材包的爆炸；命中 `NS_LDFireHit` | `production/vfx/review2/` | 大小与强度经用户确认；前摇的细边界线按用户决定保留 |
| `active_quick_meeting` 脚底开锅 | 火 · 地点圆 240，3 跳 | LOCALLY VERIFIED | 前摇：通用族；释放在落点：火区域出现时按区域半径点燃一圈 `NS_LDSeetheBurst` 的火墙，随后是 `NS_LDFireGround` 的火地面；命中：火外观的 `NS_LDFireHit` | `production/vfx/batch14-gate/` | 用户未验收；施法者身上没有单独的释放表现（技能发生在落点） |
| `active_bail_out` 贴脸补一拳 | 无 · 冲刺 + 强化普攻 | LOCALLY VERIFIED | 前摇：通用族；`NS_LDBailRelease`：肩高两条加粗滑流线随冲刺、身后暗色尘迹、终点双拳暖色亮起 0.5 s；本技能自身无命中 | `production/vfx/batch5e-physical-mass/` | 用户未验收；拳光在俯视下仍偏小 |
| `active_not_mad` 锅盖挡脸 | 无 · 自身护盾 + 解控 | LOCALLY VERIFIED | `NS_LDNotMadRelease`：脸前弹起并举住 0.5 s 的实心锅盖（90 cm 钢盘：暖白高光到灰到暗边的明暗、亮边、暗色把手）、格挡瞬间的暖白闪光、腰间向外崩碎的解控碎片；护盾见 §7.6 `NS_LDShieldState` | `production/vfx/batch6-self-rework/` | 用户未验收 |
| `active_stare_down` 来打老子啊 | 无 · 自身圆 300 嘲讽 | LOCALLY VERIFIED | 前摇：通用族；`NS_LDStareDownRelease`：胸前猩红喷气、地面猩红怒视环冲到 300 cm、再从环上向身体回收的 12 道牵引线；`NS_LDStareDownAura`：护甲增益期间（数据 3 s）胸与髋两道分成八块的钢甲环、绕行的猩红闪光 | `production/vfx/batch6-self/` | 用户未验收；被嘲讽者身上无标记（嘲讽状态不公开） |
| `active_triple_combo` 左右开抽 | 无 · 3 次强化普攻 | LOCALLY VERIFIED | `NS_LDTripleRelease`：胸前双拳暖色亮起、头顶依次点亮三颗充能点；强化期间（仅自己可见的 Empowered）`NS_LDEmpoweredState`：两只绕胸转的暖色拳光带拖尾 | `production/vfx/batch6-self/` | 用户未验收；强化普攻命中仍为通用命中 |
| `active_calculus_drop` 板砖糊脸 | 无 · 地点延迟落地 200 | LOCALLY VERIFIED | 前摇：通用族；`BookShadow`：`SM_LD_PropBrick` 带三个孔和残留灰浆的砖块网格在预警时间内翻滚着从高处落下（细线圈随预警脉动），落地扬起素材包尘环 `DustRing` 0.55 倍；命中：通用 Niagara 命中 | `production/vfx/batch12-props-two-tone/` | 用户未验收；专属命中未设计 |
| `active_flyer_trap` 踩一脚试试 | 无 · 地点陷阱 180 | LOCALLY VERIFIED | 前摇：通用族；`FlyerArming/Armed`：`SM_LD_PropFlyer` 两角卷起的纸质传单网格，上面的印刷块是警示红并随预警脉动，细线圈为触发范围；命中：通用 Niagara 命中 | `production/vfx/batch12-props-two-tone/` | 用户未验收；触发瞬间没有专属表现 |
| `active_open_mic` 扯嗓子喷你 | 无 · 自身圆 340 击退 | LOCALLY VERIFIED | 前摇：通用族；`NS_LDOpenMicRelease`：嘴前暖白爆点与光；两道相继滚出到 340 cm 的冲击前沿——以浓密暗紫灰的翻涌空气为主体（不规则、起伏、前重后轻），其上一层粗细不均的淡紫噪声带，不是清晰的线圈；地面被推到半径边的暗尘；`NS_LDOpenMicHit`：沿推力方向扫过身体的三道声弧、紫白核、身后被推开的尘；护盾见 §7.6 | `production/vfx/batch6-self-rework/` | 用户未验收 |
| `active_charged_ram` 蛮牛顶 | 无 · 冲刺撞人 + 护盾 | LOCALLY VERIFIED | 前摇：通用族；`NS_LDRamRelease`：双层楔形冲锋前沿（暗色影层 + 亮前沿）沿 460 cm 推进、船首推开的暗色尘、身后尘迹与地面拖痕；`NS_LDRamHit`：0.1 s 暖白冲击核与 380 cm 光、贴地重冲击环、暗色重尘、向上崩起的碎屑、地面撞痕 | `production/vfx/batch5e-physical-mass/` | 用户未验收 |
| `active_brace` 顶着揍 | 无 · 架势 3 s | LOCALLY VERIFIED | 前摇：通用族；`NS_LDBraceRelease`：粗铜环从胸口砸到脚下并收紧、脚下压出的暗尘；架势见 §7.6 `NS_LDBracedState`；`NS_LDBraceBurst`：架势按期结束时（释放 + 数据 3 s，且 Braced 在此时消失、未倒地）暖白核、铜色冲击波冲到 300 cm、向外推开的暗尘 | `production/vfx/batch6-self/` | 用户未验收；结束反震只在本机按期推断，提前或延长的架势不播 |
| `active_cola_shake` 冰可乐摇一摇 | 冰 · 扇形 400/60° | LOCALLY VERIFIED | `NS_LDColaTell`；`NS_LDColaBlast`：成团白色泡沫沿扇形冲出并减速散开、较大的冰晶落地停住并闪光、扇形地面结霜后化掉、下沉的冷雾；命中 `NS_LDChillHit` | `production/vfx/review2/` | 用户未验收；地面的霜有条状拉伸；预兆仍是细线扇形 |
| `active_desk_fan` 电风扇 | 无 · 地点风带 600×200 | LOCALLY VERIFIED | 前摇：通用族；`Wind`：`NS_LDWindZone` 顺着朝向滑过风带的长条气流与被带走的尘粒，细线矩形为范围；命中：通用 Niagara 命中 | `production/vfx/batch9-props-control/` | 用户未验收；气流偏淡 |
| `active_stale_takeout` 过期外卖 | 腐 · 投射物 + 瘴气地面 | LOCALLY VERIFIED | 前摇：通用族；飞行：`SM_LD_PropTakeout` 打结的外卖袋网格（袋身 + 红色标签带）按命中半径缩放 + `NS_LDFlightSock`：身后拖着的馊气、滴落到地上的油渍、袋身暗黄的反光；落地为瘴气地面；命中：通用 Niagara 命中 | `production/vfx/batch9-props-control/` | 用户未验收 |
| `active_school_bag` 占座书包 | 无 · 地点障碍 | LOCALLY VERIFIED | 前摇：通用族；`Bag`：`SM_LD_PropBag` 双肩包网格按阻挡半径与高度缩放，落地扬起素材包尘环；`BagWall`：两只包之间拉紧的两道背带；阻挡体仍是与服务器一致的方盒，只是不画出来；命中：通用 Niagara 命中 | `production/vfx/batch9-props-control/` | 用户未验收 |
| `active_flurry_rush` 疯狗乱拳 | 无 · 4 s 拳风波 | LOCALLY VERIFIED | `NS_LDFlurryRelease`：胸口炽热核与爆环、四射火星；`NS_LDFlurryAura`：狂热期间（数据 4 s）绕身螺旋上升的热痕、腰间三道飞旋的热弧、脚下快步踢起的尘 | `production/vfx/batch6-self/` | 用户未验收；拳风波仍为旧 `FistWind` 粒子（飞行物批次） |
| `active_cordon_tape` 警戒封条 | 无 · 地点封条线 500 | LOCALLY VERIFIED | 前摇：通用族；`CordonTape`：两根带底座的立柱之间挂着 `SM_LD_PropTape` 黄黑斜纹封条，中段下垂并轻微摆动，细线矩形为范围；命中：通用 Niagara 命中 | `production/vfx/batch12-props-two-tone/` | 用户未验收；封条按真实比例偏细，范围靠细线矩形 |
| 技能前摇（通用部分） | Cast 提示前摇 | LOCALLY VERIFIED | 没有专属前摇的技能按目标分两族：`NS_LDCastAim`（施法手低亮度聚能 + 地面朝前的细线）、`NS_LDCastBrace`（脚下向内收的细环，中心留空）；旧施法圆盘 `NS_LDCastCharge` 已删除 | `production/vfx/batch4/casts-*` | 用户未验收 |
| 施法手蓄能 | Cast 提示前摇 | LOCALLY VERIFIED | 旧的盖住身体的同心圆盘已撤除，由上一行的两族前摇代替 | `production/vfx/batch4/casts-*` | |

### 7.3 元素状态（附着在身上）

| 状态 | 状态编号 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- | --- |
| 灼烧 Burn | 26 | LOCALLY VERIFIED | `NS_LDBurnState`：身侧火舌循环上舔、火星上飘，层数提高强度 | `production/vfx/batch8-reactions/` | 点缀元素已调到游戏镜头下可见的大小（不扩大占地）；用户未验收 |
| 潮湿 Wet | 30 | LOCALLY VERIFIED | `NS_LDWetState`：衣摆与手上挂水滴下落、脚下湿印 | `production/vfx/batch8-reactions/` | 点缀元素已调到游戏镜头下可见的大小（不扩大占地）；用户未验收 |
| 带电 Electrified | 31 | LOCALLY VERIFIED | `NS_LDShockState`：身上短折线电弧间歇爬行（链的材质） | `production/vfx/batch8-reactions/` | 点缀元素已调到游戏镜头下可见的大小（不扩大占地）；用户未验收 |
| 冰冷 Chilled | 36 | LOCALLY VERIFIED | `NS_LDChillState`：小腿冰晶、冷雾下沉、脚下霜环 | `production/vfx/batch8-reactions/` | 点缀元素已调到游戏镜头下可见的大小（不扩大占地）；用户未验收 |
| 腐坏 Rotten | 37 | LOCALLY VERIFIED | `NS_LDRotState`：身上鼓起破裂的气泡、腰间酸腐雾 | `production/vfx/batch8-reactions/` | 点缀元素已调到游戏镜头下可见的大小（不扩大占地）；用户未验收 |

身体本身另有一层低亮度的元素色边缘光（`M_LDChainBodyGlow` 壳，与命中边缘光同一路径），按元素各自的节奏脉动：灼烧约 0.5 s 一次的暖色呼吸，潮湿约 1 s 的缓慢光泽，带电 0.16 s 的闪烁并随机漏拍，冰冷 1.3 s 的缓慢起伏，腐坏间隔不均。每个身体同一时间只显示它持有的第一种状态的边缘光，亮度低于命中边缘光。

### 7.4 元素反应

| 反应 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- |
| 导电 Conduct（溅射电场） | USER APPROVED（场） | `NS_LDField` 450（§2.2） | `vfx/approved/field-conduct.jpg` | 头顶十字闪仍是旧层 |
| 起雾 Fog（烫 + 湿） | LOCALLY VERIFIED | `NS_LDFogReact`：胸口橙光被白色压灭、一根连续上升并在顶部散开的蒸汽柱（略带倾斜）与贴地滚到雾区半径（数据 250 cm）的水汽连成一团、短促的白色嘶响条、溅起又落下的烫水滴 | `production/vfx/review2/` | 用户未验收；雾区本身仍是旧区域粒子 |
| 过载 Overload（烫 + 麻） | LOCALLY VERIFIED | `NS_LDOverloadReact`：胸口暖色热核与光、贴地向四周炸开到溅射半径（数据 200 cm）的火焰、火里贴身的短促电光（每秒重画 25 次，不是长电弧）、向外飞溅下落的火星、地面焦裂、少量烟 | `production/vfx/review2/` | 用户未验收 |
| 融化 Melt（烫 + 冻） | LOCALLY VERIFIED | `NS_LDMeltReact`：暖光一闪、冰块从身上塌落并边落边化、一缕蒸汽、融水在脚下漫成一滩 | `production/vfx/batch8-reactions/` | 用户未验收 |
| 沼气爆 Methane（烫 + 馊） | LOCALLY VERIFIED | `NS_LDMethaneReact`：绿色气团贴地涌到溅射半径（数据 250 cm）、从中心向外被火点着、褐烟 | `production/vfx/batch8-reactions/` | 用户未验收；气团的绿色偏纯 |
| 冻住 Freeze（湿 + 冻） | LOCALLY VERIFIED | `NS_LDFreezeReact`：脚边一圈棱柱状冰晶（暗面 + 亮面 + 高光面）带过冲地拔地而起并立住（时长取定身数据 1.2 s）、脚下迅速铺开的霜面、晶体上游走的闪光、外溢的冷雾，结束时冰晶落下碎裂 | `production/vfx/review2/` | 用户未验收 |
| 发酵 Ferment（湿 + 馊） | LOCALLY VERIFIED | `NS_LDFermentReact`：酸黄色泡沫从脚往上漫、气泡相继鼓起破裂、酸水滴落、脚下污渍；缓慢，无闪光 | `production/vfx/batch8-reactions/` | 用户未验收 |
| 碎冰 Shatter（麻 + 冻） | LOCALLY VERIFIED | `NS_LDShatterReact`：白光一闪、冰片贴地向四周飞溅并滑停、闪亮的小冰片、冰尘、贴身三道短电光 | `production/vfx/batch8-reactions/` | 用户未验收；之后 4 s 的易伤没有持续标记 |
| 腐蚀 Corrode（麻 + 馊） | LOCALLY VERIFIED | `NS_LDCorrodeReact`：酸黄色水泡在躯干上此起彼伏、酸液迸溅、深色甲片剥落掉下、酸烟上飘 | `production/vfx/batch8-reactions/` | 用户未验收 |
| 冻疮 Frostbite（冻 + 馊） | LOCALLY VERIFIED | `NS_LDFrostbiteReact`：灰紫色的霜爬上身体、钝冰针结在身上再掉落、沉重的冷雾下沉、脚下灰紫霜面；迟缓，不发亮 | `production/vfx/batch8-reactions/` | 用户未验收 |

### 7.5 持续区域与地面状态

| 项 | 事件 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- | --- |
| 电场 Field | 地面状态 5 | USER APPROVED | `NS_LDField` 220（经 `ShowGround` 按种类选系统） | `vfx/approved/field*` | §2.2 已知小问题 |
| 水洼 Puddle | 地面状态 1 | LOCALLY VERIFIED | `NS_LDPuddleGround`：两层浸湿暗面、冷色水体与流动反光、湿亮边、落水涟漪 | `production/vfx/batch6-grounds/` | 主体加厚后待用户评估 |
| 冰面 Ice | 地面状态 2 | LOCALLY VERIFIED | `NS_LDIceGround`：霜纹釉面、清晰冷边、边上小冰晶、表面闪光 | `production/vfx/batch3/` | |
| 火地 Fire | 地面状态 3 | LOCALLY VERIFIED | `NS_LDFireGround`：焦地裂纹脉动、边缘低火舌、内部偶发火苗与火星 | `production/vfx/batch3/` | |
| 瘴气 Miasma | 地面状态 4 | LOCALLY VERIFIED | `NS_LDMiasmaGround`：两层腐渍、较浓的缓慢低云、鼓起破裂的气泡、雾化边缘 | `production/vfx/batch6-grounds/` | 主体加厚后待用户评估 |
| 技能区域 Area* | 投射物 AreaPlain/Magic/Fire/Water/Electric | LOCALLY VERIFIED | 无元素与魔法：`NS_LDAreaPlain`（按外观着色的淡色地面晕染 + 细边线 + 上升的光点）；火、水、电区域与同元素地面状态同一套表现（`NS_LDFireGround`、`NS_LDPuddleGround`、`NS_LDField`）；占位方块不再画出，细线圈保留并承载预警脉动 | `production/vfx/batch9-props-control/` | 用户未验收；无元素区域的晕染偏淡 |

### 7.6 控制与增益状态

| 状态 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- |
| 定身 Root | LOCALLY VERIFIED | `NS_LDRootState`：脚踝处两道反向转动的钢色镣环、四根从镣环拉到地面的绷紧系索、脚下暗色抓痕；旧品红圆盘与脚镣几何体已删除 | `production/vfx/batch9-props-control/` | 用户未验收 |
| 护盾 Shield | LOCALLY VERIFIED | `NS_LDShieldState`（由提示位 Shielded 驱动，取代旧几何笼）：胸口宽、头顶窄两道反向慢转的青色壳环，六段亮面绕行，闪光沿胸环走；亮度低于元素状态 | `production/vfx/batch6-self/` | 用户未验收 |
| 出生保护 RespawnProtection | LOCALLY VERIFIED | `NS_LDProtectedState`：两道暖金色光纱从脚盘绕到头顶、随之上升的光点、脚下淡金色光；旧光柱与环绕球已删除 | `production/vfx/batch9-props-control/` | 用户未验收 |
| 减速 Slow、攻速减慢 AttackSlowed | LOCALLY VERIFIED | `NS_LDSlowState`（腿部）与 `NS_LDAttackSlowState`（手臂）：两道冷蓝色箍环一前一后沿肢体下沉、沉重的拖滞雾团与下坠的水珠；减速另有脚下冷蓝色光 | `production/vfx/batch9-props-control/` | 用户未验收 |
| 眩晕 Stun | LOCALLY VERIFIED | `NS_LDStunState`：头顶上方三点淡金色光沿摇晃的圆周转动，各拖一小段尾迹 | `production/vfx/batch9-props-control/` | 用户未验收 |
| 缴械 Disarm | LOCALLY VERIFIED | `NS_LDDisarmState`：腰间一条断成四段的浅红色带，各段下坠后弹回，钢色碎屑从手边落下 | `production/vfx/batch9-props-control/` | 用户未验收 |
| 沉默 Silence | LOCALLY VERIFIED | `NS_LDSilenceState`：头部一条带三处缺口、缓慢转动的紫色封带，下方一条反向转动的细带 | `production/vfx/batch9-props-control/` | 用户未验收 |
| 嘲讽 Taunt | LOCALLY VERIFIED | `NS_LDTauntState`：头顶上方一个红色热点，四道折线火舌从中迸出，长度每秒闪动八次 | `production/vfx/batch9-props-control/` | 用户未验收 |
| 易伤 Exposed | LOCALLY VERIFIED | `NS_LDExposedState`：躯干上四道各自弯折的橙红裂纹，错开节奏亮起又熄灭 | `production/vfx/batch9-props-control/` | 用户未验收；裂纹在亮地面上偏细 |
| 击退 Knockback | 设计上不需要 | 没有独立特效：身体被推开的位移本身可见，造成击退的技能各有命中（如 `NS_LDElbowHit` 沿击退路径拖出的尘） | `production/vfx/batch5e-physical-mass/` | |
| 架势 Braced | LOCALLY VERIFIED | `NS_LDBracedState`：腰间粗铜带 + 四个每 1.1 s 向内收紧一次的卡箍、脚下低亮铜色光 | `production/vfx/batch6-self/` | 用户未验收 |
| 强化普攻 Empowered | LOCALLY VERIFIED | `NS_LDEmpoweredState`（只有自己可见）：两只绕胸转的暖色拳光带拖尾 | `production/vfx/batch6-self/` | 用户未验收 |
| 属性增益 StatBoost | 设计上不需要 | 没有通用特效：来源众多且多数没有公开事件；`active_stare_down` 与 `active_flurry_rush` 的增益由各自释放后的光环按数据时长表现（见 §7.2） | `production/vfx/batch6-self-rework/` | |
| 营地回位 CampReturning | LOCALLY VERIFIED | `NS_LDReturnState`（由提示位 Returning 驱动）：身上升起并散开的淡色雾丝、脚下一条缓慢转动的淡色细带；刻意不亮，回位中的野怪不是威胁。旧的背后牌子已删除 | `production/vfx/batch13-return-perf/` | 用户未验收；雾丝在亮地面上很淡，只在验证舞台的球员身体上看过 |
| 野怪激怒 CampEnraged、守卫 CampGuarding | LOCALLY VERIFIED | 激怒沿用嘲讽的头顶红色火舌（`NS_LDTauntState`），守卫沿用架势的铜带（`NS_LDBracedState`）；在院长外观的身体上看过激怒 | `production/vfx/batch14-gate/` | 用户未验收 |

### 7.7 冲刺、闪避与爆劲

| 项 | 事件 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- | --- |
| 冲刺拖尾与起步 | 身体速度超过 950 cm/s | IMPLEMENTED | `NS_LDDashWake`：身后贴地扬起的尘、胸口高度两三道气流细痕；起步处素材包尘环 `DustRing` 0.35 倍；残影保留（`ULDImpactFxSubsystem::Ghost`） | — | 验证舞台没有冲刺场景，待真实对战确认 |
| 完美闪避 | PerfectDodge 命中标志 | LOCALLY VERIFIED | 身体留下一道淡金残影，轮廓闪一下金色边缘光；不画命中，也不再画金色光环 | `production/vfx/batch14-gate/` | 用户未验收 |
| 爆劲蓄力 | `bCharging` | LOCALLY VERIFIED | `NS_LDOverdriveCharge`：地面品红光环一圈圈向身体收拢、被吸向胸口的光点、脚下品红色光，强度随蓄力进度；蓄满时身体轮廓闪一下品红边缘光 | `production/vfx/batch10-moments-rings/` | 用户未验收；光点偏小 |
| 爆劲就绪、释放、命中 | 蓄满、`bOverdriveCast`、Overdriven 标志 | LOCALLY VERIFIED | 释放与带爆劲的命中：`NS_LDOverdriveBurst`（品红闪光 + 向外甩出并落地的硬质光丝）；就绪见上一行 | `production/vfx/batch14-gate/` | 用户未验收；舞台上看的是带爆劲的命中，释放用同一套爆发 |

### 7.8 预警与危险区域

| 项 | 事件 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- | --- |
| 圆形预警 | 投射物 AreaMagic + `WarningRemainingMs` | LOCALLY VERIFIED | `NS_LDAreaPlain` 的淡紫晕染 + 细线圈，细线圈随预警脉动（越近越快） | `production/vfx/batch14-gate/` | 用户未验收；野怪 Circle、EchoCircle、DoubleCircle |
| 扇形预警 | 投射物 ConeWarning | LOCALLY VERIFIED | 贴地的扇形细线轮廓随预警脉动；悬在半空的两块占位板不再画出 | `production/vfx/batch14-gate/` | 用户未验收；野怪 Cone、Fan、RotatingCone；没有填充 |
| 直线预警 | 投射物 LineWarning | LOCALLY VERIFIED | 贴地的矩形细线轮廓随预警脉动；旧的不透明平板不再画出（Road 外观同样只画轮廓） | `production/vfx/batch14-gate/` | 用户未验收；野怪 SweepLine、Charge；没有填充 |
| 野怪投射物 | 投射物（Agent 招式） | LOCALLY VERIFIED | 观察协议里野怪投射物是 Chalk 外观，与远程普攻同一套：粉笔本体 + `NS_LDFlightChalk` | `production/vfx/batch9-props-control/` | 用户未验收；协议不区分投射物来源，野怪没有自己的本体 |
| 院长普攻 | Attack 提示（院长） | LOCALLY VERIFIED | 与球员相同的普攻挥击与预兆；在院长外观的身体上看过挥击 | `production/vfx/batch14-gate/` | 用户未验收；挥击尺寸不随体型放大；验证舞台上院长的外套布料在出手时会炸开（角色资产问题，不属于特效） |

### 7.9 被动与卡牌

卡牌只在产生公开事件时有世界表现；其余只改数值或只给本人回执，世界里不应也不能单独表现。

| 卡牌 | 公开事件 | 状态 | 实现 | 已知问题 |
| --- | --- | --- | --- | --- |
| 疯狗乱拳 / 咒刃回响的拳风 | 投射物 FistWind | LOCALLY VERIFIED | `NS_LDFlightFist`：横在前进方向上的弓形压力波 + 两道回波 + 贴地拖起的尘；无本体方块 | 用户未验收 |
| 幻影脱壳 | 投射物 PhantomDecoy | LOCALLY VERIFIED | 幻影是离它最近的可见身体当前姿势的半透明紫色复制（`ULDImpactFxSubsystem::StandIn`，450 cm 内没有身体时退回圆柱）；引爆 `NS_LDDecoyPop`：紫色闪光、贴地甩出的光丝、扩到爆炸半径的紫烟 | 用户未验收 |
| 反应装甲 | 投射物 ShardBurst | LOCALLY VERIFIED | `NS_LDShardBurst`：胸口一记冷光、护盾壳片向四周平飞并滑停、前面的亮丝；细线圈为伤害范围 | 用户未验收 |
| 一路结冰、挪地盘 | 地面状态（冰面 / 迁移） | LOCALLY VERIFIED | 两张卡只通过地面状态公开：冰面是 `NS_LDIceGround`，迁移后的地面状态在新位置重新出现 | `production/vfx/batch14-gate/` | 没有卡牌自己的事件 |
| 元素勋章（火、水、电、冰、腐） | 命中外观与元素状态 | LOCALLY VERIFIED | 勋章只通过命中外观与元素状态公开：见 §7.3 的五种元素状态与 §7.10 的元素命中 | `production/vfx/batch8-reactions/` | 没有勋章自己的事件 |
| 其余 70 余张卡（触发、增长、换算） | 无公开事件 | BLOCKED | — | 观察协议只公开状态、命中外观与标志；新增事件要改网络，不在本阶段范围 |

### 7.10 全局事件

| 项 | 事件 | 状态 | 实现 | 证据 | 已知问题 |
| --- | --- | --- | --- | --- | --- |
| 元素命中（火、水、电、魔法） | 命中外观 | LOCALLY VERIFIED | 水 `NS_LDWaterHit`、火 `NS_LDFireHit`、电为闪电链的跳跃；魔法命中是它匹配到的技能自己的命中（冰可乐为 `NS_LDChillHit`），其余用较重的 Niagara 命中 `NS_LDHitFinisher` 并按出手者着色。旧的星形、十字与法阵图层配方已删除 | `production/vfx/batch14-gate/` | 用户未验收 |
| 阵亡 | 阵亡通知 | LOCALLY VERIFIED | 身体落地扬起一圈带碎屑的尘云（素材包 `DustRing`，0.45 倍），`NS_LDKnockout` 只留胸口一记亮光和地上两秒内淡去的暗色擦痕；细环与自制扬尘已删除 | `production/vfx/batch14-gate/` | 用户未验收 |
| 击杀者身上的黄色刻度环 | 本人身体标记的黄色外沿 | IMPLEMENTED | 即自身脚下标记的旧黄色圆盘外沿，随下一行一起替换 | — | 同下一行，待真实对战确认 |
| 升级 | 单位 Level 增加（本人取自成长视图，他人取自观察到的等级） | LOCALLY VERIFIED | `NS_LDLevelUp`：一秒内两道暖金光纱从腿盘到头顶之上、脚下金色光、随之升起的光点，跟随身体 | `production/vfx/batch10-moments-rings/` | 用户未验收 |
| 自身脚下标记 | 本人身体 | IMPLEMENTED | `NS_LDOwnMark`：本人座位色的柔光圆 + 细边；资产缺失时退回两层圆盘 | — | 验证舞台里的身体不是玩家 Pawn，标记未能在舞台上出现，待真实对战确认；在共享文件 `LDWorldViewRelay` |
| 旗杆圈与占领 | 占领圈与进度 | LOCALLY VERIFIED | 圈线保持清晰的边界线，其上 `NS_LDRingBand`：按占领状态着色的亮线 + 圈内侧贴地滑动的光丝；未开放的圈亮度减半 | `production/vfx/batch10-moments-rings/` | 用户未验收；占领进度与 `LDCaptureFence` 未改 |
| PK 圈与决战圈 | 安全圈、下一圈、投掷落点 | LOCALLY VERIFIED | 安全圈 `NS_LDRingDanger`：圈线外侧不断向圈线滑入的光丝，危险的一侧就是在动的一侧；下一圈 `NS_LDRingNext`：安静的虚线，圈线为暗丁香色；投掷落点 `NS_LDAreaPlain`：粉红细圈 + 圈内晕染。安全圈的墨色仍是 `SafeColor()`（HUD 标签同色） | `production/vfx/batch10-moments-rings/` | 用户未验收；只在验证舞台看过，真实 PK 待确认 |
| 出现与复活柱 | 单位出现 | LOCALLY VERIFIED | 光柱几何体已删除；复活由出生保护的金色光纱（`NS_LDProtectedState`）表现，身体自身的出现泛光保留 | `production/vfx/batch9-props-control/` | 用户未验收；没有出生保护的出现（野怪刷新）只有身体泛光 |
| 恢复球 | 恢复球可见 | LOCALLY VERIFIED | 拾取圈上 `NS_LDRingBand` 的绿色亮线与贴地光丝，中心 `NS_LDOwnMark` 的一团柔和绿光；旧的小内环只在光效无法显示时画出 | `production/vfx/batch11-flags-orb/` | 用户未验收 |

## 8. 第三方素材（Asset First）

### 8.1 生产顺序

以实际游戏里的专业画面为准，按以下顺序选择做法：

1. 直接使用合适的成熟 Niagara 系统；
2. 改它的材质、形状、时序和参数；
3. 拆开重组成熟的发射器、贴图、序列帧、网格、材质与模块；
4. 用本项目的 Blender + Niagara 生成器做缺少的元素；
5. 没有合适素材时才从零生成整个特效。

成熟素材也要按 §1 做美术适配（形状语言、材质与场景融合、元素颜色、视觉分量、节奏、多人可读性），不同来源不能直接拼接。已批准的闪电链 A+ 与电场 F3 不动；用素材重做的候选只用于对比，替换需要用户批准。

### 8.2 授权、存放与安装

授权维度、NoAI 规则、目录、本机素材库与工作区同步见 [第三方素材](THIRD_PARTY_ASSETS.md)，逐个素材的记录在 `SourceData/ThirdParty/third_party_assets.json`。VFX 专有的约定：第三方素材与其改造副本不得放进 `Niagara/Generated/`（ImportVFX 会清理该目录）；CC-BY 素材的项目改造副本放 `Content/Lockdown/Art/VFX/Adapted/`。

### 8.3 VFX 候选

| 素材 | 链接 | 类别 | 许可 | UE | 状态 |
| --- | --- | --- | --- | --- | --- |
| Mixed VFX | https://www.fab.com/listings/6518971c-c8ca-47d7-8611-1ff068b78dd5 | AI_ALLOWED | CC-BY | 4.26–5.4（5.8 下全部可加载并渲染） | 已接入 `Content/ThirdParty/CCBY/MixedVFX` |
| Basic VFX Pack (Free) | https://www.fab.com/listings/75698e52-edfc-4f76-a86c-b4f26fcf5a29 | AI_ALLOWED | 标准 | 4.27、5.0–5.5 | 待领取 |
| Stylish Fire VFX (Free) | https://www.fab.com/listings/01e8534c-5877-4ce2-8948-9a696100de11 | AI_ALLOWED | 标准 | 5.0–5.6 | 待领取 |
| Stylized Smoke VFX (Free) | https://www.fab.com/listings/172235df-7a05-4d60-ac7b-23472e530ee7 | AI_ALLOWED | CC-BY | 5.0–5.8 | 已接入 `Content/ThirdParty/CCBY/PotaVFX_Smoke` |
| Free Niagara Particles (CC BY 4.0) | https://www.fab.com/listings/183732bc-c2fb-465c-9453-f70a1ce7ba2c | AI_ALLOWED | CC-BY | 5.2–5.8 | 已接入 `Content/ThirdParty/CCBY/FreeParticle_SoftTofu`，尚未使用 |
| Niagara Examples Pack | https://www.fab.com/listings/0e188eca-4e54-4fb2-a9ed-d8b8a565e600 | NO_AI | 标准 | 5.7 | 待领取 |
| Free Spline VFX | https://www.fab.com/listings/2b923e61-b02d-4cc9-bd0b-b067c9e6056e | NO_AI | 标准 | 5.2–5.8 | 待领取 |
| [18] Free Spells Niagara | https://www.fab.com/listings/967b7892-d46c-4d75-8121-33418a6899ce | NO_AI | 标准 | 5.0–5.8 | 待领取 |
| Realistic Fire & Explosion Vol.1 Starter | https://www.fab.com/listings/2adf4e4e-5f1d-4543-9e8c-192fe3c9a8ed | NO_AI | 标准 | 5.6–5.7 | 备选（写实画风） |

### 8.4 已接入素材的用法

素材包系统作为项目特效的一层播放：`ULDImpactFxSubsystem::PlayPack(LDSliceVfx::EPack, 位置, 缩放)`，路径表在 `LDSliceVfx::PackPath`。素材原样使用，只改整体缩放、它公开的 User 参数，以及按名字关闭不合适的发射器；缺失时记一次 `LD_PACK_FX missing` 日志并跳过该层。按路径加载的素材不会被引用关系带进打包，用到的目录登记在 `Config/DefaultGame.ini` 的 `DirectoriesToAlwaysCook`（新增一个 `EPack` 时同时加它的目录）；三套素材包系统与这些目录下的依赖已确认进入 Win64 打包（cook 产物与 IoStore 容器索引里都有），打包版运行时的显示尚未验证。验证舞台的镜头臂长与视角和真实对战相同，角色在画面里一样大。验证舞台的 `-VfxProof packs` 把各包的系统按原样和按缩放放在游戏镜头下。

| `EPack` | 素材系统 | 用在哪里 |
| --- | --- | --- |
| `DustRing` | Stylized Smoke `NS_SmokeRing`（一圈米色尘团 + 碎屑；公开 `ColorSmoke`、`ScaleDebris`、`ForceUpSmoke` 等参数） | 阵亡落地 0.45；`active_charged_ram` 命中 0.5、`active_elbow` 命中 0.32、`active_crotch_grab` 与 `active_pants_down` 命中 0.22；`active_brace` 结束反震 0.6 |
| `FireBlast` | Mixed VFX `NS_ExplosionGroundSmall`（紧凑火球转黑烟；无公开参数；它的半透明红色冲击球发射器 `Empty` 在运行时关闭，见 `LDSliceVfx::PackMutedEmitters`） | 未使用：用在 `active_seethe` 上被用户判为太大、太浮夸 |
| `LightningHit` | Mixed VFX `NS_LightningBolt_Hit`（青白电弧爆裂；无公开参数） | 尚未使用 |

游戏镜头下的观感：Mixed VFX 的火焰序列帧有体积，明显好于自制火舌；爆炸原样过大过黑，缩到 0.35–0.45 才能用；斩击命中是彩色火花加闪光；Stylized Smoke 的烟环缩小后是合适的撞击扬尘；Free Niagara Particles 是很小的星点，适合升级与增益点缀。总览图在 `production/vfx/packs-survey/`。

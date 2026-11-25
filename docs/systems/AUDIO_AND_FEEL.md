# 音频、动画与打击感手册

战斗与界面声音、身体动作表现、镜头与屏幕反馈的统一入口（`LDAudio*`、`LDAudioFeel`、`LDHitFeel` 表现侧、
`SourceData/Audio/**`、`Content/Lockdown/Audio/**`）。改声音、动作节奏或打击感前先读本页：

- §1 方向与原则；§2 事件同步表（VFX、音频、动画共同遵守）；§3 音频系统与设置 API；§4 生产流程；
- §5 素材来源与授权；§6 批准目录与决策记录；§7 覆盖清单；§8 检查入口。

状态与来源的含义与 [VFX 手册](VFX.md) 相同：只有“用户 + APPROVED”是正式标准，其余为 PROVISIONAL 依据。

## 1. 方向与原则（PROVISIONAL · Agent，依据用户的生产指令）

- 打击感是节奏，不是更多震屏、闪光或音量：动作蓄力 → 能量释放 → 真实命中 → 受击反应 → 声音 → 消散。
- 物理攻击要有身体冲击和重量：低中频的“肉”与长度表现分量，不靠电平和超低频。
- 元素各有声音身份，不用同一音效改音高：电是暖色的噼啪与嗡鸣（与已批准的暖白至浅金闪电链一致），
  火是呼的燃起加爆裂，水是溅开加水滴。
- 避免廉价电子提示音、过度卡通化、持续高音量与无意义的大爆炸。界面声音与 B 方案的竞技直播风格一致，
  但比战斗声音短、干、靠后，不遮挡战斗信息。
- 不改变真实技能时序、操作响应、碰撞、移动、服务器权威与网络契约；声音、动作、镜头只读已有的表现钩子。

## 2. 事件同步表（契约，Stream C 所有）

每行的 t = 0 是客户端呈现的权威事件；只有表中的表现偏移由我们决定。

| 来源钩子 | 说明 |
| --- | --- |
| 前摇开始 / 出手 | `ULDHumanoidVisualComponent` 的 `ActionOnset` 与 `ActionImpactSeconds`（权威前摇）；出手 = 开始 + 前摇。基础普攻前摇 180 ms × 0.85 / 攻速，基础间隔 1176 ms；技能 0–250 ms（`content.json` windup） |
| 命中 | 受击身体的已结算命中（`SetHitReaction` → `PendingImpacts`）；攻击者的确认是本人回执 `LastDealtAtMs`（`ALDCharacter`）。都由服务器结算，从不预测 |
| 反应 / 击杀 | `SetReaction`；回执的击杀标志 + 公开击杀播报 |
| 升级、争旗、开局、胜负 | `FLDAudioCueTracker` 采样的本人成长与房间公开数据 |

| 事件 | 预备 | 释放 | 命中 | 消散 |
| --- | --- | --- | --- | --- |
| 普攻第 1、2 段 | 前摇期间：地面挥击提示（VFX）、前摇动作；声音在出手前 min(40 ms, 0.2×前摇) 开始，之前无声 | 出手帧：刀光、动作冲击帧、风声峰值在 0 | 受击：命中瞬态在 0（±1 帧）；受击闪白 0–180；本人停顿 60、推镜 0–120 | 刀光 ≤ 260（`ld.Feel.TrailMs`）；粒子 ≤ 280；声音尾 ≤ 350 |
| 普攻第 3 段（终结） | 同上，提示更重 | 风声更低更长，峰值在 0 | 更重的命中与低频身体冲击在 0 | 声音尾 ≤ 450 |
| 落空 | — | 只有风声 | 从不补造命中 | 风声尾 ≤ 300 |
| 技能前摇 | 前摇 ≥ 120 ms 时蓄能声随前摇上升 | 技能释放声在 0，蓄能 30 ms 内切断 | 按命中外观族在命中时刻 | 释放尾 ≤ 500 |
| 闪电链（已批准 A+） | 前摇 250：手部蓄能 | 手部电爆在 0 | 每一跳在 0；弹跳间隔 150（数据） | 链满亮 130、165 切断；声音主体 ≤ 165，余电 ≤ 300（对应身体电弧爬 0.3 s） |
| 电场（已批准 F3） | 开场边缘闪亮在 0 | — | 场内伸向人的电弧 | 持续嗡鸣随电场寿命，随边缘淡出 ≤ 300 |
| 元素反应 | — | — | 反应声在 0 | ≤ 600 |
| 本人击杀 | — | — | 击杀重音在 0，停顿 160、推镜 2× | 公开击杀播报按其数据时刻，不额外延迟 |
| 升级 | — | — | 声音、VFX、UI 同一采样在 0 | UI 横幅自管 |
| 开旗 / 开始占领 / 争夺 | — | — | 在 0（公开数据），重复间隔 ≥ 1000 | — |
| 开局 / 胜 / 负 | — | — | 阶段或结果数据的 0 | 音乐交叉淡出 1500 |
| 界面点击、选择、确认、购买 | — | — | 按下或回执时刻（购买、安装、选择只在回执后） | ≤ 400 |

规则：
- 声音瞬态与 VFX、动作关键帧对齐在一帧内（≤ 16 ms）；对不齐的层宁可不播，也不延后。
- 停顿与推镜只是本人表现，不暂停输入、移动或权威时间（`ld.Feel.*`，设置页特效栏可调）。
- 别人的声音只来自视野授权的代理（不泄露迷雾），空间化；本人声音 2D。
- 降低战斗特效只减弱推镜与闪光，不去掉命中声。

## 3. 音频系统与设置 API

**总线**（`ELDAudioBus`，`LDAudioTypes.h`）：`SC_DemoMaster` 下恰好三个子类 `SC_DemoMusic`、`SC_DemoSfx`、`SC_DemoUI`，
用户音量以 `SMix_DemoUser` 的类覆盖施加，分轨实际增益 = 总音量 × 分轨。默认：总 100 %、音乐 60 %（用户决定）、
音效 100 %、界面 100 %。没有语音轨（游戏没有语音）。线索名以 `UI` 开头的归界面轨，其余归音效轨（`ULDAudioCatalog::BusOf`）。

**设置 API**（给设置页）：
- `ULDGameInstance::SetAppliedBusVolume(ELDAudioBus, float)`：0..1，立即生效，稍后自动保存；草稿或越界被拒并说明原因。
- `ULDGameInstance::GetAppliedBusVolume(ELDAudioBus)`。
- `ULDAudioSubsystem::PlayBusPreview(ELDAudioBus)`：滑块试听（总与音效：一次普攻命中；界面：点击；音乐无试听）。
- 本地设置 schema 26 保存 `SfxVolume`、`UiVolume`；更早的文件迁移时这两项取 100 %。

**播放**（`ULDAudioSubsystem`）：
- `Play`（2D）、`PlayAt`（世界位置，身体衰减）、`PlaySkillRelease`（按技能编号；无专属声音的技能用 `CastRelease`）、
  `Start`（可被调用方切断的声音，用于蓄能）。
- 一条线索可有多个 take，每次随机选且不连续重复同一个。
- 并发：身体声音最多 16 个，超出停最安静的；界面最多 4 个，超出停最旧的；同一线索 25 ms 内重触发合为一个。
- 身体衰减：距听者 2000 cm 内满音量（游戏镜头离地约 1500–1900 cm），再 2500 cm 自然衰减到无声，按镜头视角声像。
- `-LDAudioEventLog` 时每次播放都记一行 `LD_AUDIO_EVENT`（世界时间、take、增益、音高、位置），用于证据对时。

**表现钩子**（`LDAudioFeel`，由 `ULDHumanoidVisualComponent::UpdateSound` 与命中队列调用）：挥击、蓄能、释放、他人受击；
本人命中确认在 `ALDCharacter`（`LDAudioFeel::Confirm`：手感层 + 按档的重音：普通、暴击、破盾、击杀）。
本人受击、护盾、闪避、招架、冲刺、拾取、死亡、复活、升级、争旗、胜负仍由 `FLDAudioCueTracker` 按投影产生。

## 4. 生产流程

1. 原创声音：`SourceData/Audio/Lockdown/synth_demo_audio.py`（numpy/scipy/soundfile，固定种子，同脚本同输出）。
   部分命中身体叠用仓库内的 Kenney CC0 文件。输出 48 kHz 单声道 16-bit WAV，峰值 -1 dBFS（Git LFS）。
2. 登记：`SourceData/Audio/audio_cues.json`（schema 3）：`packs`（CC0 包与授权文件）、`originals`（生成器及其 sha256）、
   `cues`（每条线索一个或多个 take）、`skills`（技能释放，按 `native_ids.json` 编号）、`music`。
3. 响度：`python Scripts/MeasureDemoAudioLoudness.py --write`：先把每个声音拉到 Kenney 源的中位响度，再按 `level_db` 定混音；
   峰值限 0.95。混音意图写在 `level_db`，不改源文件。
4. 导入：`Scripts/ImportDemoAudio.ps1 -Mode Import`，校验：`-Mode Validate`（打包前置检查也会跑）。
5. 程序化运行时声音：MetaSound Builder API 在本机无头编辑器里实测可用（`MetaSoundBuilderSubsystem.create_source_builder` →
   `add_node_by_class_name` → `connect_nodes` → `build` 或 `MetaSoundEditorSubsystem.build_to_asset`，均 SUCCEEDED）。
   用于真正需要运行时参数的声音（持续电场嗡鸣随人数变化、蓄能随进度变化）；一次性声音仍用 WAV，不为简单音效强加 MetaSound。
6. 证据：`Scripts/CaptureFeelSample.ps1 -FFmpeg <ffmpeg.exe>`，再 `python Scripts/MakeFeelEvidence.py --run <输出目录> --ffmpeg <ffmpeg.exe> --out <目录>`。
   - 在战斗演示台以真实速度、开声卡播放“feel”场景（只静音音乐轨）；`ffmpeg ddagrab` 录游戏窗口客户区
     （`gdigrab` 读不到 D3D12 翻转模型窗口），`Scripts/RecordAudioLoopback.py` 用 WASAPI 回环录扬声器实际输出
     （需 `pip install soundcard`；录制时自身播放数字静音保持端点运转，否则静音段会丢帧导致时间轴错位）。
   - 两个录制进程的启动时钟不可信（实测偏差上百毫秒），对齐按内容：音频按日志里每个 take 在回环里的匹配位置；
     画面以移除大厅界面那一帧（与日志的录制开始同一 tick）为零点，并用每次出手的画面变化峰值复核。
   - UE 5.8 的 `StartRecordingOutput` 子混音录制在本机只录到约一半时长（开启渲染调度时），不用作证据。
   - 声音的听感验收只能由人完成；Agent 只能看时序、波形、频谱与响度，并须如实说明。

## 5. 素材来源与授权

| 来源 | 授权 | 能否进本仓库 | 状态 |
| --- | --- | --- | --- |
| Kenney（已用：Digital Audio、Impact Sounds、Interface Sounds、Music Jingles） | CC0 | 能 | 使用中 |
| 本项目生成器 `synth_demo_audio.py` | 项目原创 | 能 | 使用中 |
| Fab “Free Modular Magic SFX”（Le Binh Son，290 个，含 22 个 MetaSound） | 描述写 CC BY 4.0，不要求署名 | 能（保留出处） | 待用户在 Fab 领取 |
| Fab “500 Free Essential Game SFX Starter Pack”（HomkaH） | 描述写 CC BY 4.0 | 能，须署名 | 待用户在 Fab 领取 |
| Fab “100 RPG Fantasy SFX Pack” | 页面未写授权，标注 AI 生成 | 不用 | 排除 |
| Sonniss #GameAudioGDC（2015–2026 各年，GDC 2026 约 7.5 GB） | 可商用、免署名；禁止把音效本身再分发 | **不能**：本仓库是公开的，原始文件与导入后的 `.uasset` 都不能提交；按 VFX 已定的做法只放本机 | 待用户下载 |
| Freesound | 只用 CC0 | 能 | 未开始（需登录下载） |

本仓库在 GitHub 上公开。第三方原始音频只有在授权允许公开再分发时才进入 `SourceData/Audio`；
只允许“用于成品游戏”的素材不能以源文件或可提取的 `.uasset` 形式提交。

## 6. 批准目录与决策记录

**批准目录**：暂无。用户批准前，所有声音都是 PROVISIONAL。

**决策记录**
- 挥击与技能释放从“本人投影采样”移到身体表现钩子（PROVISIONAL · Agent）：与刀光、电链同一帧触发，别人可见的身体也有声；
  原 `SelfAttack`(17)、`SelfCast`(9) 编号退役。
- 他人受击声改为世界位置空间化（PROVISIONAL · Agent）：原先是 0.38 音量的 2D 声，听不出方位。
- 本人命中确认的 Kenney 重击降为 -10 dB 的手感层（PROVISIONAL · Agent）：受击者身上的命中声才是主体，避免双重重击。

## 7. 覆盖清单

状态：NOT STARTED / IN PROGRESS / IMPLEMENTED / LOCALLY VERIFIED / USER APPROVED / BLOCKED。

### 7.1 音频

| 项 | 状态 | 实现与资产 | 证据 / 已知问题 |
| --- | --- | --- | --- |
| 普攻挥击 1–2 段 | IMPLEMENTED | `SwingLight` ×3 take，第 2 段音高 0.94 | 实机样板时序已验（`production/feel/sample-combo-zap/`）；未听感验收 |
| 普攻终结挥击 | IMPLEMENTED | `SwingFinisher` ×2 | 同上 |
| 落空 | IMPLEMENTED | 只有挥击声（设计如此） | — |
| 普通命中（他人身体） | IMPLEMENTED | `HitBasic` ×3（合成 + Kenney 重击） | 样板时序已验；联网时与本人确认层可能相差 1–2 帧，未在真实对局确认 |
| 终结命中 | IMPLEMENTED | `HitFinisher` ×2 | 依赖 VFX 击打匹配判断终结 |
| 本人命中确认（普通/暴击/破盾/击杀） | IMPLEMENTED | `SelfDealtImpact` + `SelfDealtAccent`/`Crit`/`ShieldBreak`/`Kill` | — |
| 电命中 / 闪电链每跳 | IMPLEMENTED | `HitElectric` ×3 | 样板时序已验：三跳各在日志时刻 ±7 ms |
| 火、水、魔法命中 | IMPLEMENTED | `HitFire`、`HitWater`、`HitMagic` 各 ×2 | — |
| 冰、腐（毒）命中 | BLOCKED | 命中外观只有 Magic/Fire/Water/Electric 四种，冰与腐没有外观字段 | 需要新的命中外观（网络投影改动，非本流可做） |
| 技能前摇蓄能 | IMPLEMENTED | `CastCharge`，出手 30 ms 切断 | — |
| 技能释放：电你个哆嗦 | IMPLEMENTED | 技能表 `active_zap` ×2 | 样板时序已验；待用户听感 |
| 技能释放：其余 23 个 | IN PROGRESS | 暂用 `CastRelease` 通用声 | 待逐个设计 |
| 每技能专属命中声 | BLOCKED | 命中不携带技能编号；只能按外观族与 VFX 击打匹配区分 | 同上 |
| 元素反应（10 种） | NOT STARTED | — | — |
| 地面状态循环（水洼、冰、火、瘴气、电场） | NOT STARTED | — | — |
| 投射物飞行与落点 | NOT STARTED | — | — |
| 被动触发与强化普攻 | NOT STARTED | — | — |
| 本人受击、护盾、破盾、闪避、招架、冲刺、拾取 | IMPLEMENTED | 既有 Kenney 线索 | 质量待按新方向复审 |
| 死亡、复活、升级 | IMPLEMENTED | 既有 Kenney 线索 | 同上 |
| 击杀播报、开旗、占领、争夺 | IMPLEMENTED | 既有 Kenney 线索 | 同上 |
| 开局、胜、负、无胜者 | IMPLEMENTED | 既有 Kenney 乐句；开局无专属声 | 开局 NOT STARTED |
| 界面：悬停、点击、拒绝、构筑工坊全套 | IMPLEMENTED | 既有 Kenney 线索 | UI 样板（悬停/选择/确认/购买/拒绝/奖励）未做 |
| 菜单、大厅、设置、赛后 | IN PROGRESS | 点击、悬停共用；音乐已有 | — |
| 环境氛围 | NOT STARTED | — | — |
| 音乐 | IMPLEMENTED | 用户提供的三首 | — |
| 总线与设置 API | IMPLEMENTED | §3 | 设置页 UI 由 Stream A 做 |
| 并发、优先级、衰减、空间化 | IMPLEMENTED | §3 | 多人混战未实测 |

### 7.2 动画

| 项 | 状态 | 实现与资产 | 证据 / 已知问题 |
| --- | --- | --- | --- |
| 篮球手 Idle、跑、转身、运球 | IMPLEMENTED | `ULDBasketballMotionSet`（KB_ 系列） | 来源与授权 UNKNOWN，待用户确认 |
| 普攻（三段共用一个动作） | IMPLEMENTED | `Hooper_Attack`（KB_078 单手传球）对齐权威出手 | 三段无姿态差异；终结无重心变化 |
| 技能施法（全部共用） | IMPLEMENTED | `Hooper_Cast`（KB_148 罚球） | 不同语义技能无姿态、方向差异 |
| 受击、死亡、出生 | IMPLEMENTED | Quaternius CC0 重定向 | 受击无方向与轻重之分 |
| NPC 动作 | IMPLEMENTED | Quaternius CC0 片段 | — |
| 连段专属动作、技能动作家族、蓄力、硬直、击退 | NOT STARTED | — | 需要外部动作资产或制作 |

### 7.3 镜头与屏幕反馈

| 项 | 状态 | 实现 | 证据 / 已知问题 |
| --- | --- | --- | --- |
| 本人命中停顿（普通/暴击/击杀/破盾） | IMPLEMENTED | `ld.Feel.*HitStopMs` | 起点值，非批准值 |
| 推镜 | IMPLEMENTED | `ld.Feel.KickCm/KickMs`，设置可关 | 同上 |
| 屏幕边缘闪光与色散 | IMPLEMENTED | `LDHitVfx::ScreenFlashOf`，降低特效时关闭 | 同上 |

## 8. 检查入口

- 规则测试：`Lockdown.Rules.AudioCuePublic`、`AudioCueOwn`、`AudioFeelSync`、`AudioCatalog`、`AudioCatalogAsset`、
  `AudioMusicSelection`、`CombatReceiptLog`、`LocalSettings*`。
- 资产：`Scripts/ImportDemoAudio.ps1 -Mode Validate`。
- 实机音画样板：`Scripts/CaptureFeelSample.ps1`。

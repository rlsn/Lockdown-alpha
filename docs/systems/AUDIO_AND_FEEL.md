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
- 地面循环：每种地面状态最多 2 条，超出时停掉离听者最远的那条（不按先后），淡出 0.3 s。
- 并发：每个声音（一条线索、一个技能的出手、一个技能的命中）各自一个并发组：界面 2 个、元素状态与反应 3 个、其余 4 个，
  超出停最旧的；同一声音 25 ms 内重触发合为一个。高频被动连锁只会顶掉自己，不会挤掉别的声音。
- 优先级（引擎 32 个声部不够时先保谁）：本人命中确认与受伤 3、击杀/开旗/开局/升级 2.5、出手与命中 2、元素状态 1、界面 0.8（导入时写入）。
- 他人声音的增益：挥击、命中 0.7；技能前摇、出手与元素反应 0.9（威胁不因是别人放的而被压低）；元素状态 0.55。
- 身体衰减：距听者 2000 cm 内满音量（游戏镜头离地约 1500–1900 cm），再 2500 cm 自然衰减到无声，按镜头视角声像。
- `-LDAudioEventLog` 时每次播放都记一行 `LD_AUDIO_EVENT`（世界时间、take、增益、音高、位置），用于证据对时。

**表现钩子**（`LDAudioFeel`，由 `ULDHumanoidVisualComponent::UpdateSound` 与命中队列调用）：挥击、蓄能、释放、他人受击；
本人命中确认在 `ALDCharacter`（`LDAudioFeel::Confirm`：手感层 + 按档的重音：普通、暴击、破盾、击杀）。
本人受击、护盾、闪避、招架、冲刺、拾取、死亡、复活、升级、争旗、胜负仍由 `FLDAudioCueTracker` 按投影产生。

## 4. 生产流程

1. 分层声音（主要生产方式，用户批准的方向）：`SourceData/Audio/Layered/build_layered_audio.py` 把真实录音层对齐起音后混成
   游戏声音：瞬态（皮革拳击、木裂、玻璃）、主体（材质与元素：火、冰晶、电弧）、尾音（布料、尘、碎屑），合成只做低频垫底。
   素材：Kenney Impact、Interface、RPG、Sci-Fi（CC0）与 Fab “Free Modular Magic SFX”（Le Binh Son，CC BY 4.0，导出为 WAV
   放在 `SourceData/Audio/FabMagic/`）。每个声音 3 个 take：换层与固定种子的 ±3% 音高。`--inputs` 列出全部输入文件。
   程序合成 `SourceData/Audio/Lockdown/synth_demo_audio.py` 只剩比赛事件、本人确认层与已批准的电你个哆嗦出手声。
   输出 48 kHz 单声道 16-bit WAV，峰值 -1 dBFS（Git LFS）。
2. 登记：`SourceData/Audio/audio_cues.json`（schema 3）：`packs`（CC0 或注明署名的 CC BY 4.0 包与授权文件）、
   `originals`（生成器及其 sha256；分层生成器另列 `inputs`：每个录音层的路径、sha256 与所属包）、
   `cues`（每条线索一个或多个 take）、`skills`（技能释放，按 `native_ids.json` 编号）、`music`。
3. 响度：`python Scripts/MeasureDemoAudioLoudness.py --write`：先把每个声音拉到 Kenney 源的中位响度，再按 `level_db` 定混音；
   峰值限 0.95。混音意图写在 `level_db`，不改源文件。
4. 导入：`Scripts/ImportDemoAudio.ps1 -Mode Import`，校验：`-Mode Validate`（打包前置检查也会跑）。
5. 程序化运行时声音：MetaSound Builder API 在本机无头编辑器里实测可用（`MetaSoundBuilderSubsystem.create_source_builder` →
   `add_node_by_class_name` → `connect_nodes` → `build` 或 `MetaSoundEditorSubsystem.build_to_asset`，均 SUCCEEDED）。
   用于真正需要运行时参数的声音（持续电场嗡鸣随人数变化、蓄能随进度变化）；一次性声音仍用 WAV，不为简单音效强加 MetaSound。
6. 证据：`Scripts/CaptureFeelSample.ps1 -FFmpeg <ffmpeg.exe>`，再 `python Scripts/MakeFeelEvidence.py --run <输出目录> --ffmpeg <ffmpeg.exe> --out <目录>`；
   真实对战用 `-Scenario combat`（`RunDemoCombatCheck.ps1 -CaptureOnly -RecordGame <目录>`，直接得到 `combat.mp4`）。
   - 只录游戏自己：`-LDRecordGame=<目录>`（`Probes/LDGameRecorder.h`）从游戏视口的后台缓冲抓帧（含界面），声音取主子混音
     的缓冲。游戏在屏幕外运行，不读桌面、不录系统声音，用户的窗口、视频和声音不会进入录像；`Scripts/AssembleGameRecording.py`
     按帧时间和声音起点合成 mp4。禁止再用屏幕区域录制或系统回环录音。
   - 主子混音缓冲按录像时钟落位：渲染停顿处补静音，时间轴不被压缩（UE 5.8 的 `StartRecordingOutput` 在本机只录到约一半
     时长，不用）。
   - 对齐按内容：音频按日志里每个 take 在录音里的匹配位置；画面以移除大厅界面那一帧（与日志的录制开始同一 tick）为零点，
     并用每次出手的画面变化峰值复核。
   - 声音的听感验收只能由人完成；Agent 只能看时序、波形、频谱与响度，并须如实说明。

## 5. 素材来源与授权

| 来源 | 授权 | 能否进本仓库 | 状态 |
| --- | --- | --- | --- |
| Kenney（已用：Impact Sounds、Interface Sounds、RPG Audio、Sci-Fi Sounds、Music Jingles） | CC0 | 能 | 使用中 |
| 本项目生成器 `synth_demo_audio.py` | 项目原创 | 能 | 使用中 |
| Fab “Free Modular Magic SFX”（Le Binh Son，290 个，含 22 个 MetaSound） | CC BY 4.0（署名：Free Modular Magic SFX by Le Binh Son） | 能，只提交用到的层（WAV） | 使用中：用户已领取，`Content/FreeModularMagicSFX` 只放本机（已忽略） |
| Fab “500 Free Essential Game SFX Starter Pack”（HomkaH） | 描述写 CC BY 4.0 | 能，须署名 | 下载不了，不用 |
| Fab “100 RPG Fantasy SFX Pack” | 页面未写授权，标注 AI 生成 | 不用 | 排除 |
| Sonniss #GameAudioGDC（2015–2026 各年，GDC 2026 约 7.5 GB） | 可商用、免署名；禁止把音效本身再分发 | **不能**：本仓库是公开的，原始文件与导入后的 `.uasset` 都不能提交；按 VFX 已定的做法只放本机 | 用户允许使用；含其素材的混音能否进公开仓库尚未定，暂未下载 |
| Freesound | 只用 CC0 | 能 | 未开始（需登录下载） |

Kenney 的 Impact、Interface、RPG、Sci-Fi 全包（CC0）由用户决定采用。

**动画素材**：篮球手的 KB_0xx 篮球动作（待机、跑、运球、传球、投篮）是项目自有资产（用户确认），可在其上制作新的连段与技能动作。

本仓库在 GitHub 上公开。第三方原始音频只有在授权允许公开再分发时才进入 `SourceData/Audio`；
只允许“用于成品游戏”的素材不能以源文件或可提取的 `.uasset` 形式提交。

## 6. 批准目录与决策记录

**批准目录**

- **打击感样板：普攻三连 + 电你个哆嗦（APPROVED · 用户）**：用户原话“特效和音效样片可以接受”。它是节奏与分量的参照基准：
  挥击风声峰值落在出手帧、命中在出手 +1 帧、终结更低更长、电链每跳瞬态在命中时刻并带 300 ms 余电、技能蓄能在出手时切断。
  证据：`Artifacts/Audits/PresentationOverhaul/production/feel/sample-combo-zap/`（`feel.mp4`、`timing.json`、`wav/`）。
  涉及线索：`SwingLight`、`SwingFinisher`、`HitBasic`、`HitFinisher`、`HitElectric`、`CastCharge`、`SelfDealtImpact/Accent`、技能表 `active_zap`。
  后续改这些声音或时序要与此样板回归对比。其余声音仍是 PROVISIONAL。

**决策记录**
- 挥击与技能释放从“本人投影采样”移到身体表现钩子（PROVISIONAL · Agent）：与刀光、电链同一帧触发，别人可见的身体也有声；
  原 `SelfAttack`(17)、`SelfCast`(9) 编号退役。
- 他人受击声改为世界位置空间化（PROVISIONAL · Agent）：原先是 0.38 音量的 2D 声，听不出方位。
- 本人命中确认的 Kenney 重击降为 -10 dB 的手感层（PROVISIONAL · Agent）：受击者身上的命中声才是主体，避免双重重击。
- 受击方不做受击动作（APPROVED · 用户，[#286](https://github.com/rlsn/Lockdown-alpha/issues/286) 原话“彻底取消受击”）：普通命中时受击方姿势、位置、朝向与节奏不变，只有闪白、特效、
  音效与飘字；身体位移只来自权威控制状态（击退、定身）。打击感放在攻击方：出手姿态、命中确认、停顿与推镜。

## 7. 覆盖清单

状态：NOT STARTED / IN PROGRESS / IMPLEMENTED / LOCALLY VERIFIED / USER ACCEPTED（暂用，待质量复做）/ USER APPROVED / BLOCKED。

### 7.1 音频

| 项 | 状态 | 实现与资产 | 证据 / 已知问题 |
| --- | --- | --- | --- |
| 普攻挥击 1–2 段 | USER APPROVED | `SwingLight` ×3（分层：短促风声 + 布料甩动），第 2 段音高 0.94 | 批次 2 试听后用户选定新版 |
| 普攻终结挥击 | USER APPROVED | `SwingFinisher` ×3（分层：低沉突刺风声 + 挥砍风声 + 腰带甩动） | 批次 2 试听后用户选定新版 |
| 落空 | IMPLEMENTED | 只有挥击声（设计如此） | — |
| 普通命中（他人身体） | USER APPROVED | `HitBasic` ×3（分层：皮革拳击 + 软体闷响 + 低频 + 布料） | 样板时序已验；联网时与本人确认层可能相差 1–2 帧，未在真实对局确认 |
| 终结命中 | USER APPROVED | `HitFinisher` ×3（分层：重拳 + 木裂 + 低频爆 + 尘尾） | 依赖 VFX 击打匹配判断终结 |
| 本人命中确认（普通/暴击/破盾/击杀） | IMPLEMENTED | `SelfDealtImpact` + `SelfDealtAccent`/`Crit`/`ShieldBreak`/`Kill` | — |
| 电命中 / 闪电链每跳 | USER APPROVED | `HitElectric` ×3（分层：闪电击 + 短拳） | 样板时序已验：三跳各在日志时刻 ±7 ms |
| 火命中 | USER APPROVED | `HitFire` ×3（分层：火焰冲击 + 闷响） | A/B 第一轮选定 |
| 水、魔法命中 | IMPLEMENTED | `HitWater`、`HitMagic` 各 ×3（分层：水击 + 闷响 + 水花；金属魔法冲击 + 轻拳 + 闪光尾） | 未听感验收（批次 2 试听卷） |
| 冰、腐（毒）命中 | IMPLEMENTED | 施法者可见时，命中按 VFX 的击打匹配归到技能（冰可乐 → 冰裂命中，过期外卖 → 湿拍腐烂命中） | 施法者不可见时只剩 Magic 外观，落回 `HitMagic`；彻底区分需要命中投影带元素（网络改动，非本流） |
| 技能前摇蓄能 | USER APPROVED | `CastCharge` ×3（分层：吟唱蓄能 + 上扬），出手 30 ms 切断 | 批次 2 试听后用户选定新版 |
| 技能释放：电你个哆嗦 | USER APPROVED | 技能表 `active_zap` ×2 | 样板（§6） |
| 技能释放：其余 23 个 | IMPLEMENTED | 技能表各 ×2 take，分层：按技能名字里的东西和动作取真实录音（锅盖、板砖、喷嘴、马达、封条、纸夹、皮革、拳击、火、水、冰……），起音裁到出手帧（半峰在 5 ms 内） | 未听感验收（下一卷试听） |
| 每技能专属命中声 | IMPLEMENTED | `skill_hits` 表 18 个技能各 ×2 take，分层（骨撞、鞋底拍脸、砖裂、水柱、冰裂、湿拍、书包闷击、鞭响……），起音在命中帧；按 VFX 击打匹配（`FStrikeMatch::Ability`）选用 | 未听感验收（下一卷试听） |
| 元素反应（10 种） | IMPLEMENTED | `Reaction*` 线索，挂在 `SetReaction` 钩子，与特效强度无关；全部改为分层，每种 ×3，由两种元素各自的录音层组成（雾=水柱嘶声+渐入、导电=多重闪电+水、超载=碎石爆+闪电+低频、融化=火+水、沼气=火球+黏液+低频、冻结=冻风+冰晶、发酵=毒+黏液、冰碎=冰晶+玻璃+低频碎裂、腐蚀=毒+金属、冻伤=冰晶+轻拳） | 冰碎经 A/B 第一轮选定；其余未听感验收（批次 2 试听卷） |
| 地面状态循环（水洼、冰、火、瘴气、电场） | IMPLEMENTED | `GroundPuddle`/`GroundIce`/`GroundFire`/`GroundMiasma`/`GroundField`：每处显示中的地面状态在中心放一条安静循环（分层，接缝交叉淡化），由 `ALDObservedGroundProxy` 显示时开始、离开视野或到期时 0.3 s 淡出（`ULDAudioSubsystem::StartGroundLoop`/`StopGroundLoop`）；每种最多 2 条，离得最远的让位 | 规则测试 `AudioCatalog` 覆盖循环归属；未实机采集、未听感验收 |
| 投射物飞行与落点 | NOT STARTED | — | — |
| 元素状态挂上（着火、湿身、带电、冰冷、腐烂） | IMPLEMENTED | `State*` 短促提示（分层，各 ×3：火痕、水流、电荷、冰晶、黏液+毒），状态从无到有时响一次，不做循环（避免混战堆积） | 未听感验收（批次 2 试听卷） |
| 被动触发与强化普攻 | NOT STARTED | — | — |
| 本人受击、护盾、破盾、闪避、招架、冲刺、拾取 | IMPLEMENTED | 受击、护盾、破盾（重击音提高到 1.35 倍音高）、招架、拾取沿用 Kenney；冲刺（低沉疾风与球鞋摩擦）与精准闪避（擦身风声与清脆一响）改为原创 | 未听感验收 |
| 死亡、复活、升级 | IMPLEMENTED | 死亡（重重倒地与下坠低音）、升级（温暖上扬的和弦与细碎闪光）改为原创，替换廉价电子音；复活沿用 Kenney | 未听感验收 |
| 他人被击倒 | IMPLEMENTED | `BodyFall` ×3（分层：重摔 + 皮革落地 + 低频 + 尘），挂在倒地尘环（`NS_LDKnockout`）同一钩子，按威胁级 0.9 播放；本人死亡仍是 `SelfDeath` | 未听感验收 |
| 击杀播报、开旗、占领、争夺 | IMPLEMENTED | 开旗（铁钟一击与上扬）、开始喷漆（喷漆罐摇珠与喷气，对应喷漆占点）、争夺（两下闷敲与断续喷气）改为原创；击杀播报沿用 Kenney 乐句；本人击杀另有 `SelfDealtKill` 重音 | 未听感验收 |
| 开局、胜、负、无胜者 | IMPLEMENTED | `MatchStart`（开局休息后的第一段场地，中途加入不补放）；胜负为既有 Kenney 乐句 | 规则测试 `AudioFeelSync` 覆盖开局判定 |
| 界面：悬停、点击、拒绝、构筑工坊全套 | IMPLEMENTED | 悬停与点击保留 Kenney Interface 单音（高频操作，不加层）；其余改为分层，各 ×3：拿起=撕贴纸、安装=卡扣、拒绝=低沉错误音、工坊开合=卷帘、三选一=发三张牌、获得=入袋、加点=短促上扬加闪光、按住确认=蓄能上扬、终局锁定=重闩加鸣响、融合=两下金属碰响加微光；选定（`UIChoose`）经 A/B 第一轮选定 | 选定已由用户选定；其余未听感验收（下一卷试听） |
| 菜单、大厅、设置、赛后 | IN PROGRESS | 点击、悬停共用；音乐已有 | — |
| 环境氛围 | NOT STARTED | — | — |
| 音乐 | IMPLEMENTED | 用户提供的三首 | — |
| 总线与设置 API | IMPLEMENTED | §3 | 设置页 UI 由 Stream A 做 |
| 并发、优先级、衰减、空间化 | IMPLEMENTED | §3 | 多人混战未实测 |

### 7.2 动画

| 项 | 状态 | 实现与资产 | 证据 / 已知问题 |
| --- | --- | --- | --- |
| 篮球手 Idle、跑、转身、运球 | IMPLEMENTED | `ULDBasketballMotionSet`（KB_ 系列，项目自有） | — |
| 普攻三段连击 | IMPLEMENTED | `LDActionMotion`：第 1 段右手刺拳（KB_078，上身 0.42，胸口先右蓄后左转 16°）、第 2 段左手交叉（KB_079，上身 0.7，反向蓄力后右转 30° 并侧移 14 cm）、第 3 段终结（KB_152 跳投，静止时全身，重心上提再送出）；出手帧仍按 `LDBasketballMotion::SampleTime` 对齐权威命中 | 实机帧 `production/feel/motion-combo/`；转身与侧移只作用于网格与枢轴，胶囊、朝向与时序不变；未经用户验收 |
| 技能施法动作家族 | IMPLEMENTED | 按技能语义分四族：单手投掷（甩鞋、外卖、电、肘、封条、夹子、扒裤子；传球片段）、双手定点（区域、吼叫、喷水、风扇、可乐；罚球）、全身重砸（板砖、书包、蛮牛、乱拳；跳投）、架势（锅盖、顶着揍、拧手腕、左右开抽、挑衅、补一拳；运球架势）；移动中只驱动上身 | 规则测试 `ActionMotionFamilies`；实机帧 `production/feel/motion-skill-families/`：投掷、双手定点、重砸从俯视角清楚可分；架势族改用转身运球（瞬发技能的收招按 2 倍速播完，出手仍对齐命中，`LDBasketballMotion::MaxWarp/FollowRate`），帧 `ready-stance-spin.png`；未经用户验收 |
| 受击、死亡、出生 | IMPLEMENTED | Quaternius CC0 重定向 | 受击无方向与轻重之分 |
| NPC 动作 | IMPLEMENTED | Quaternius CC0 片段 | — |
| 受击反应 | 按决定关闭（[#286](https://github.com/rlsn/Lockdown-alpha/issues/286)） | 普通命中无受击动作，见 §6；击退时的后仰与 Hit 片段保留 | — |

### 7.3 镜头与屏幕反馈

| 项 | 状态 | 实现 | 证据 / 已知问题 |
| --- | --- | --- | --- |
| 本人命中停顿（普通/暴击/击杀/破盾） | IMPLEMENTED | `ld.Feel.*HitStopMs`（60/100/160/90 ms），只停本人出手姿态，不停输入、移动与权威时间；样板中已被用户接受 | 数值是起点值，未正式批准 |
| 推镜 | IMPLEMENTED | `ld.Feel.KickCm/KickMs`（6 cm、120 ms，击杀 2 倍），设置“镜头震动”可关，“降低战斗特效”减到 45 %；受击方从不推镜（#286） | 起点值 |
| 实机验证顿帧、推镜、边缘闪光（1080p） | LOCALLY VERIFIED | 真实拥有连接的 1v1 战斗（`RunDemoCombatCheck.ps1 -CaptureOnly`，`CaptureFeelSample.ps1 -Scenario combat` 录可见窗口与扬声器）：15 次本人命中确认、5 次终结、1 次击杀（击杀重音、全场播报、倒地、升级）；全程红色受伤边缘只出现 2 次、各 70–100 ms，无战斗长闪；逐帧画面位移中位 0、p99 约 8 px（1920 宽，含跟随镜头） | `production/feel/combat-real-fight-1080p/`；“镜头震动”关与“降低特效”档位的对比录制未做；未经用户验收 |
| 混音：真实对局 | LOCALLY VERIFIED（1v1） | 同一录制（含音乐）：峰值 −3.6 dBFS、无削波，400 ms 响度中位 −25.3、p95 −22.8、最大 −21.5 dBFS，命中不叠加冲高 | 6 人同屏混战暂无可录的拥有连接入口（AI 满房只有空闲主机），未验证 |
| 屏幕边缘闪光与色散 | IMPLEMENTED | 只在击杀、暴击、破盾时出现（`LDHitVfx::ScreenFlashOf`）；本人受击只有很淡的红边；降低特效时关闭 | 起点值 |

## 8. 检查入口

- 规则测试：`Lockdown.Rules.AudioCuePublic`、`AudioCueOwn`、`AudioFeelSync`、`AudioCatalog`、`AudioCatalogAsset`、
  `AudioMusicSelection`、`CombatReceiptLog`、`LocalSettings*`。
- 资产：`Scripts/ImportDemoAudio.ps1 -Mode Validate`。
- 实机音画样板：`Scripts/CaptureFeelSample.ps1`。

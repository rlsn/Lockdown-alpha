# 架构与网络

本页记录权威会话、房间准入、时间线阶段机、主旗杆/终局/中场 PK/地图事件编排及结构化运行日志契约（`LDGameInstance`、`LDGameMode*`、`LDGameState`、`LDMatchTimeline`、`LDCaptureReducer`、`LDLog`）。

## 权威拓扑与协议边界

- **单进程权威拓扑**：Demo 采用 UE Listen Server 架构（1 名参与对局的真人 Host + 最多 2 名真人远程 Client + 自动补齐至 6 席的对手 AI，无 Dedicated Server 与平台匹配）。
- **线协议与版本校验**：网络握手固定核对 `LDNetProtocol::WireProtocol = 36`、`Config/DefaultGame.ini` 的 `ProjectVersion` 与 `CatalogDigest`（`manifest.json` 的 32 字节 SHA-256），任一不匹配直接拒绝入房并给出明确原因。
- **核心通信原则**：客户端只发窄意图 RPC（会话、空间订单、战斗动作、构筑命令、PK 报名/押注/投掷、出生点选择），服务器验证权限、阶段写闸（`FLDGameplayWritePermit`）与修订后单点提交；Listen Host 本地 UI 与远端 Client 走完全相同的只读投影（`OwnerState`、`ALDGameState::PublicMatch`、`ALDWorldViewRelay`），禁止 Widget 直调服务器 Director。

## 核心身份与生命

- **四层身份隔离**：
  1. `FLDMatchContext{uint32 Generation, FGuid LedgerMatch}`：每开一局递增 `Generation` 并换新 `LedgerMatch`，彻底隔离跨局迟到包与回执。
  2. `FLDContenderId`（座位 `0..7`）：本局固定参赛者身份，绑定名字、座位色、成长账本与公开计分。
  3. `Life`（`uint32`）：每次出生/复活递增；空间订单、受击结算、临时状态与唯一致死奖励键 `(Match, Victim, Life)` 均绑定具体 `Life`。
  4. `RequestId / Sequence`：每类命令在各自连接/生命域内去重并重放终态回执。
- **大厅准入与断线处理（`LDGameModeSession`）**：
  - **大厅准备**：非房主真人须点击 Ready；**房主免点 Ready**，全部非房主真人就绪即可 `StartMatch`；房主修改 AI 数量时**保留**在场真人的 Ready 状态。
  - **对局中断线与退房**：房主主动退房或崩溃直接结束房间；非房主主动点“离开房间”立即撤除参赛资格并移除身体；非房主意外网络断开时调用 `ParkDisconnectedMember` 将其当前身体停泊在被动控制器上保留局内资格，支持同进程凭 `LDReconnect` 令牌重连恢复（`ResumeRoomMember`），无法接管他人席位。
- **复活与出生保护（`LDGameModeLife`）**：野外无限复活，等待 `6 + 0.5 * L` 秒（考试周 +3 s），死者在 `LIFE.SpawnChoiceSec`（6 s）内从等概率不放回抽出的 `LIFE.SpawnChoiceCount`（3）个野外出生点中选择落点（超时取首项）。复活带 `LIFE.ProtectionSec`（3 s）免伤免控保护，不计入占圈人数；任何通过前闸的进攻/主动施法意图（即使随后因缺劲或冷却被拒）立即解除保护，纯移动与通用冲刺不解除。

## 休息、野外进度与阶段边界

`LDMatchTimeline` 维护唯一权威比赛时钟与野外累计进度 $T$（$T$ 仅在定时野外推进，休息与终局冻结）：

| 顺序 | 阶段 | 时长与提前结束规则 | 核心事件与写闸边界 |
| --- | --- | --- | --- |
| 1 | 开局休息 `R0` | `REST.OpeningBuildSec = 35 s`（全员真人 `ServerRestReady` 后立即跳入 `3 s` 返场倒计时；准备可再次点击撤回 `Withdrawn`） | 全体参赛者与训练假人入驻隔离休息室；选开局主动 + 开局构筑牌 + 加点；商店不开放 |
| 2 | 定时野外 `F1` | `160 s`（$T: 0 	o 160	ext{ s}$） | $T=11	ext{ s}$ 公开中野/大野标记营地；$T=40, 110	ext{ s}$ 空投；$T=90	ext{ s}$ 预告、$100\sim130	ext{ s}$ 开放小迷你旗 |
| 3 | 第一次休息 `R1` | `65 s` 规划（末 `10 s` 兼 PK 报名，真人全就绪提前收束至报名窗）→ 若 $\ge 2$ 人报名进入 PK（`6 s` 押注 + `3 s` 准备 + 最长 `45 s` 对决 + `3 s` 结算，最长 `57 s`）→ `3 s` 返场 | 开放休息商店、洗点、遗忘与满槽替换；返场由服务端共同准备分散点位及野外生态，全部就绪后一起站立，超时报告 `LD_REST_RETURN_UNREADY` |
| 4 | 定时野外 `F2` | `160 s`（$T: 160 	o 320	ext{ s}$） | $T=171	ext{ s}$ 新一轮标记营地；$T=200, 270	ext{ s}$ 空投；$T=210	ext{ s}$ 画饼老板出场（巡查日为 $120	ext{ s}$）；$T=260\sim300	ext{ s}$ 中心擂台 |
| 5 | 第二次休息 `R2` | 同 `R1` | 商店刷新价重置为 0；PK 赛制按报名人数抽单挑、车轮、混战或双队 |
| 6 | 定时野外 `F3` | `160 s`（$T: 320 	o 480	ext{ s}$） | $T=331	ext{ s}$ 末轮标记营地；$T=360, 430	ext{ s}$ 空投；$T=420\sim450	ext{ s}$ 小迷你旗；$T=480	ext{ s}$ 未击杀 Boss 退场、本段营地为 6 阶 |
| 7 | 终局前休息 `R3` | `65 s` 规划（真人全就绪立即进 `3 s` 返场；**无 PK**） | 每人抽 3 项终局改装（Boss 击杀者或擂台胜者抽 4 项）必选 1 项；最后一次商店/洗点/换招机会 |
| 8 | 终局决战 `FinalField` | 最长 `SHOWDOWN.DurationSec = 150 s`（前 `10 s` 旗杆锁定，第 `75 s` 起安全圈 `1800 -> 600 cm` 线性收缩，第 `90 s` 截止新复活入队，每人最多 2 次复活 `18 s / 24 s`） | 从 `final_spawn_0..7` 入场并全员公开终局改装；不发 XP，击杀回 `35% MaxHP + 50 劲` |

## 旗杆争夺、终局棘轮与超时裁决（`LDCaptureReducer` / `LDGameModeShowdown`）

- **常规与终局喷漆/守护**：单名存活非保护参赛者在喷漆区内独占满 `3.5 s` 上色成为涂鸦主人；硬控施加、离圈、死亡清零未完成喷漆（普通受击、减速、缴械不打断），多人进圈暂停保留。涂鸦主人独占累积守护满 `20 s` 立即获胜；**主人离圈、死亡或他人进圈仅暂停/冻结当前守护，绝不清零**，仅当对手喷漆成功易主时才清零前主人的当前守护（常规有 `10 s -> 8 s` 过半恢复档）。
- **终局抗僵局棘轮（`BeginShowdown` 注入）**：主旗杆喷漆在 `1.15 s / 2.30 s` 设回退存档点，守护在 `7.0 s / 14.0 s` 设棘轮下限，且终局每次击杀参赛者基础推进 `+4.0 s` 守护（与 `final_kill_seconds` 的 `+3.0 s` 叠加）。
- **同刻胜负与 150 s 超时裁决（`ResolveShowdownTimeoutWinner`）**：同拍达成时按“守护满 > 喷漆次数满（打一枪换个地方）> 清场”裁决，同档并列判无人胜。`150 s` 到期时，仅在**仍在局、存活、非复活保护且有正贡献**的参赛者中依次比较：**有效守点完成百分比**（`max(主旗杆历史/当前有效守护 ÷ 20s, 私人旗杆守护 ÷ 28s)`）→ **终局击杀数** → **终局对参赛者伤害**；全并列或无人合格则判无冠军结束。
- **公共状态同步（`ALDGameState::PublicMatch`）**：复制阶段计时、今日热点、旗杆与私人旗杆状态、热搜模糊圆、空投/外卖柜/小迷你旗/擂台/标记营地公开状态、清营回复球坐标列表（`RecoveryOrbs`）以及终局锁定的 `FLDMatchResult`。

## 运行日志与试玩记录

运行时统一使用 `Core/LDLog.h` 的 `LogLD*` 分类（编辑器专用为 `LogLDEditor`，严禁 `LogTemp`），格式固定为 `LD_<TAG> key=value`（字符串经 `LDLog::Quote`，定点经 `LDLog::P2`），供 `Scripts/SummarizePlaytestLog.py` 解析；扩充日志字段只追加在行尾，不改既有键名。

## 检查入口

- 纯规则：`Lockdown.Rules.DemoCore.*`、`Lockdown.Rules.DemoMatchTimeline*`、`Lockdown.Rules.DemoCapture.*`、`Lockdown.Rules.ViewPrivacySchema`、`Scripts/TestSummarizePlaytestLog.py`。
- 拥有连接场景：`Scripts/RunDemoCaptureCheck.ps1`、`Scripts/RunDemoSessionFaultCheck.ps1`。

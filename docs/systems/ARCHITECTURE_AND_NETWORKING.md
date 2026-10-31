# 架构、会话与网络权威

本文描述当前原生组件，供复用和维护；产品范围以 [App Spec](../APP_SPEC.md) 为准。
工程入口与完整命令见 [开发指南](../DEVELOPMENT.md)。

## 模块与状态归属

| 边界 | 当前入口 | 职责 |
| --- | --- | --- |
| 运行模块 | `Source\Lockdown\Lockdown.Build.cs` | CharacterMovement、Enhanced Input、UMG、导航、AI、网络与规则 |
| 编辑器模块 | `Source\LockdownEditor` | 导入、Bootstrap、生成资产与保存包；不进入运行模块依赖 |
| 进程内会话 | `Framework\LDGameInstance.h/.cpp` | 网络/旅行失败回到前端，保存本地连接反馈 |
| 世界规则 | `Framework\LDGameMode.h/.cpp` | profile 选择、服务初始化、Lobby、私有参赛记录、生命与成长 |
| 公共状态 | `ALDGameState`，定义在 `LDGameMode.h` | 明确公开的 Lobby、目标、时间及终局状态 |
| 玩家意图 | `Framework\LDPlayerController.h` 及各 `LDPlayer*.cpp` | 自身状态、RPC、输入路由和回执 |
| 身体 | `Character\LDCharacter.h/.cpp` | 原生运动、相机和可辨识表现，不自行结算经济或胜利 |

表中模块内路径分别对应 `Source\Lockdown\Public` 和 `Private`。
运行代码不依赖 UnrealEd；Game 与 Editor 目标都需要能独立构建。

## 进入一局

`Config\DefaultEngine.ini` 指定 `L_Frontend`、`ALDGameMode` 与 `ULDGameInstance`。
`ULDUIRoot::Host` 用 `OpenLevel` 打开监听世界；`Join` 检查地址/端口后调用 `ClientTravel`。
当前默认 Host 入口显式选择 `L_DevelopmentRegion?listen?Profile=V05`。
`OpenStandalone.ps1 -Profile ...` 可以选择已有工程 profile；不能把切换 profile 当作新 Demo 已接通。

`ALDGameMode::InitGame` 拒绝未知 profile，并确定要加载的目录、控制器和服务。
`PreLogin`/`PostLogin` 校验准入；Lobby 的准备和开始由服务器处理。
`SetLobbyChoice`、`StartDevelopmentMatch` 是当前 Lobby 规则入口，不应在 Widget 中重新实现人数或开局判定。

Host 同时运行权威世界和自己的客户端表现。IP 直连不依赖匹配服务，也不会自动解决 NAT。
必须分别验证独立拥有客户端和可见的 Listen Host；拥有 Host 进程的人本来就能访问权威状态，
视野过滤不等于能防范恶意 Host。

## 私有、公共与观测状态

- `FLDOwnerState` 属于具体连接的自身投影；`ALDGameMode::StateFor` 是服务器访问参赛状态的入口。
- 玩家身体和 PlayerState 的网络归属不能泄露其他人的私有资源。
- 其他身体的可见表现主要通过观测中继，而不是把所有权威 Actor 广播出去后在 UI 上隐藏。
- GameState 只放明确公开的事实，不应成为全部玩家位置、背包和构筑的总表。
- 完整地图的 `ALDB91Controller`、`ALDB92Controller` 有独立的观测/服务协议，
  不应把它们的稀疏记录强塞进小地图的固定网格格式。

详细可见性机制见 [视野与 UI](VISION_AND_UI.md)。

## 请求、代次与回执

所有权由拥有连接决定，不能相信客户端传入的另一名玩家标识。
命令携带其所需的 match、life、sequence/order、build/inventory/object revision，
目标使用当前获准的观察句柄或公开地点 ID，不使用隐藏 Actor 指针。

先验证代次、身份、目标和资源，再提交一次结果。重复的已接受请求返回原回执，
不能重复发钱、创建物品或施放效果。旧 life 的投射物、输入、菜单报价和移动确认不能作用于新身体。
UI 的 Pending 不代表已经提交，客户端不能自行生成成功回执。

`ULDNavigationComponent` 的取消 epoch、库存/商店的 request 回执和完整地图的 world receipt
是不同领域的版本，不要用一个全局“最新版本号”替代全部语义。

## 生命周期与失败

`EndContenderLife`、`ReturnDueContenders`、`RestartPlayer`、`Logout` 负责身体与私有参赛记录的协调。
死亡/断线需要取消正在进行的交互、导航和交易，再按当前实现完成必要清理；不能只销毁 Pawn。
返回时重新建立生命代次与连接投影，不能用旧菜单或观察句柄继续操作。

`ULDGameInstance::NetworkFailed` 处理 pending connection 的 WorldContext 归属，
避免把一次连接失败写入另一个 PIE 实例。错误信息到前端，不无限停留在失效世界中。

`ALDB92Roster` 提供持久席位与控制器交接，但这不是经过认证的断线重连服务。
`ALDB92SeatController` 提供无本地 UI 的权威接口，也不是已经完成的自主发育对手 AI。
新 Demo 的不重连规则、人数和 AI 策略需要按 App Spec 实施。

## 扩展与检查

新增规则先找 GameMode 所拥有的现有领域服务，保持一个权威状态和一条提交路径。
修改序列化时同步生产者、消费者、上限检查、代次拒绝和客户端显示，不能只改结构体尺寸。
新的公开信息必须显式决定，不通过日志、提示或失败原因意外公开。

当前入口：`RunResetCheck.ps1`、`RunCombatNetworkCheck.ps1`、`RunViewNetworkCheck.ps1`，
以及相关 `Source\Lockdown\Private\Tests` 中的生命周期、网络与序列化检查。
准备对局、调试授予或无玩家的监听服务不能代替真实客户端完成一局的证据。

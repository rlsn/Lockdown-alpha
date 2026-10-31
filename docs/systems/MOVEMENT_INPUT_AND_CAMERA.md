# 移动、输入与镜头

产品按键和体验目标见 [App Spec](../APP_SPEC.md)。本文保留当前运动和输入基础的工程约束，
不把某个 profile 的旧按键、槽位数或交互方式当作 Demo 规则。

## 入口

| 入口 | 作用 |
| --- | --- |
| `Character\LDCharacter` | 原生身体、SpringArm/Camera、朝向与表现 |
| `Character\LDCharacterMovementComponent` | CharacterMovement 扩展及 saved move 状态 |
| `Navigation\LDNavigationComponent` | 路径、移动命令、确认、取消、心跳和镜头 |
| `Framework\LDPlayerInput.cpp` | 输入动作、指向、菜单路由 |
| `UI\LDControlSettings`、`LDControlsPanel` | 映射注册、冲突检测、保存与本地偏好 |
| `World\LDB91SourceControls.cpp` | 完整地图的来源动作和指针路由 |

头文件在 `Source\Lockdown\Public`，实现在 `Private`。

## 一个身体只有一套运动

普通移动由 CharacterMovement 执行并参与原生预测/校正。
导航组件在运动前提供方向，不另写一个位置积分器，不沿路径直接 SetActorLocation，
也不为方便寻路把玩家临时交给另一个 AI Controller。

运动上限、碰撞、斜坡、冲刺和位移能力仍需通过该身体的合法运动路径。
镜头平移或预览不能改变人物位置、攻击距离或服务器判断。

## 命令与取消

`FLDMoveIdentity` 包含 `Match`、`Life`、`Order`、`Epoch`；
拥有连接隐含在组件的 Controller 中，不接受客户端指定另一个所有者。

1. `Navigate` 建立新 order，发送目标表面或已观察目标句柄。
2. `ServerOrder` 校验生命、顺序、边界与静态可行走性，求合法路径/接近点。
3. `ClientOrder` 只接受当前身份的结果，避免迟到确认重启已取消路线。
4. `Stop` 提升 order 和取消 epoch，同时清空本地跟随与服务器命令。
5. `ResetLife` 不倒退序列号；旧生命消息不能使新身体继续走动。

目标句柄由相应世界的观察服务重新解析。地面点击可以使用静态地图信息，
但动态目标必须当前获准可见；不能以寻路预览、自动追踪或避障反馈泄露隐藏单位。

## 路径与物理边界

接近目标要找碰撞外的合法位置，不直接寻路到物体被阻挡的中心。
不可走、层高有歧义、无完整路径或长期无进展，都应停止并给出明确失败，
不能把 partial path 当作成功到达，也不能无限重算或穿过已生效的阻挡。

`TickComponent` 维护沿路线进展、心跳和 body 生命周期。服务器保留独立的命令身份和合法性校验；
原生 correction 不是可以跳过命令验证的理由。环境碰撞更新与路径重算的关系见
[世界与数据管线](WORLD_AND_DATA_PIPELINE.md)。

## 输入所有权

输入依次由文本框、模态界面、拖拽/捕获、面板、待确认动作、小地图和世界处理。
同一次手势只能有一个消费者；按下时获得的所有权必须维持到释放。
打开窗口的点击不能继续落到新窗口内的按钮，关闭面板也不能重放仍按住的输入。

使用 `CanWorldCommand`、`V05Back`、`CancelArmed`、`CancelInterruptibleHolds`
等现有控制器边界，不在每个 Widget 中重新判断一套角色可行动状态。
暂停菜单不暂停共享世界；焦点丢失、死亡、离开和终局要清除适用的移动/指向/交互状态。

不同 profile 具有不同动作入口，完整地图的确认后持续动作不能被改回通用“必须一直按键”的假设。
修改按键要同步 `LDControlSettings`、控制器路由、帮助文本、HUD 标记与相应输入检查；
不得只修改图标标签。

## 相机与小地图

镜头使用固定方向的俯视斜角，可跟随、回中、平移和缩放。
`PanCamera`、`Recenter`、`ToggleFollow`、`PanTo` 与 `TickCamera` 共享镜头状态。
边缘平移需尊重应用焦点、UI 遮挡和操作模式；滚轮在面板上滚动面板，不同时缩放世界。

小地图的坐标转换应使用当前地图定义。点击静态地图不授予对隐藏单位的锁定或远程交互能力，
远处镜头也不会把身体的攻击/商店距离一起移动。

本地按键、文字缩放与镜头偏好通过 `LDControlSettings` 保存，不能和对局金币、技能冷却等混为一份状态。
保存失败需反馈，配置迁移不能默默给多个动作绑定同一个按键。

## 维护检查

针对变化选用 `RunMovementCheck.ps1`、`RunNavigationCheck.ps1`、`RunInputCheck.ps1`；
规则测试包括 `LDNavigationProbe`、`LDInputRulesTests` 和命令存活性相关用例。
重点覆盖：新命令取代旧命令、S 后迟到确认、死亡后旧路线、真实客户端的坡面/阻挡、
UI 不穿透、重新获得焦点不自动行动。

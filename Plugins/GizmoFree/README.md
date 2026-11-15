# GizmoFree for Unreal Engine 5.8

按用户提供的 GizmoFree README 独立重写的编辑器插件，版本 3.0.0。原始概念作者为 liwenhao12；不包含、不加载原 UE 5.6 的两个 DLL，也不是原作者源码的升级版。

本包在 Windows / Unreal Engine 5.8.4（CL 58902052，兼容构建 ID 55116800）编译和验证。描述符的 EngineVersion 使用该引擎报告的兼容基线 5.8.0；二进制匹配仍由实际模块 BuildId 校验。其他引擎构建应使用附带 Source 重新编译。

## 安装与设置

将整个 `GizmoFree` 目录放到工程的 `Plugins/GizmoFree`，保存当前工作后重新打开工程。插件默认启用；也可在 Edit → Plugins 搜索 GizmoFree。

Edit → Project Settings → Plugins → GizmoFree 提供开关及移动、旋转、缩放灵敏度。设置保存到本工程的用户编辑器配置，不改变游戏运行时设置。

## 操作

先在普通关卡编辑视口选中一个或多个 Actor。

| 操作 | 功能 |
| --- | --- |
| Alt + 左键拖动 | 沿相机画面平面移动 |
| Alt + Ctrl + 左键拖动 | 沿世界 Z 轴移动 |
| Alt + 右键拖动 | 水平拖动为 Yaw，垂直拖动为绕相机右轴的 Pitch；首个有效方向锁轴 |
| Alt + 中键拖动 | 绕相机视线方向 Roll |
| 切换至缩放模式（R），Alt + 右键拖动 | 以多选质心等比缩放；向右/上放大，向左/下缩小 |
| Esc | 取消当前拖动并恢复起始变换 |
| Ctrl + Z / Ctrl + Y | 拖动结束后撤销 / 重做 |

旋转和缩放围绕可编辑选中 Actor 的位置平均值进行。选中的父子对象只对最上层选中父对象应用变换，子对象继承变换，避免重复操作。锁定和不可编辑的 Actor 不参与。

有可编辑 Actor 选中时，上述 Alt 组合会接管关卡视口中的原生相机/复制行为。无有效选中对象、在其他面板或资产编辑器中、PIE/SIE 期间均不接管。

单次拖动为一条撤销记录；松开 Alt 提交变换，失去应用激活状态或进入 PIE 时结束拖动。插件不重定位系统鼠标；长距离操作可分段拖动。仅处理 Actor 变换，不处理组件/实例子选择，也不实现网格/角度吸附。

## 源码与验证

- `Source/GizmoFree/Private/GizmoFree.cpp`：一个 Slate 输入预处理器，范围限制到关卡视口；按 Slate 用户及指针 ID 隔离拖动。
- `GizmoFreeDrag.cpp`：起始变换快照、质心运算、父子过滤及编辑器事务。
- `GizmoFreeTests.cpp`：自动化用例 `GizmoFree.Transforms.SelectionAndUndo`。

已通过 UE 5.8.4 原生编译、独立 HostProject 中的模块加载及自动化用例（1 项，0 失败）。用例覆盖平移、垂直移动、Yaw/Pitch/Roll、质心缩放、累积误差、撤销/重做、取消、锁定过滤和父子选择。

自动化用例会创建空白地图，只应在独立测试工程中执行。未使用自动鼠标操作验证当前用户编辑器中的实际拖动手感。

模块类型为 Editor，不进入游戏运行时。

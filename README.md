# Lockdown

**v0.14.2 · Unreal Engine 5.8.4 · Windows PC**

**对外标题：谁是雾港最强淫人王**（“淫”是雾港黑话，只指把人揍趴下）。技术项目名为 Lockdown。

雾港大学城后街的自由混战游戏，采用 MOBA 点击操作与固定俯视斜角镜头。最多 6 名参赛者（真人与最多 3 名对手 AI）从零打野与对抗，用属性点、主动技能（Q/W/E 三级分支）、构筑牌与元素勋章建立优势，最终在旗杆喷上涂鸦并守满时长获胜。

## 当前流程

前端建房或加入 → Lobby（房主设 0–3 名 AI，房主免准备）→ 开局休息 R0 → 野外 1 → 休息 R1（含 PK）→ 野外 2 → 休息 R2（含 PK）→ 野外 3 → 休息 R3（选终局改装）→ 最终野外（封街争旗）→ 结算 → 房主带房间回 Lobby 开新局。

- 运行入口只有前端 `L_Frontend` 与唯一正式关卡 `L_Demo`。
- 内容与数值为试玩输入，未经平衡测试；联机自动化仅覆盖同机回环连接，验收矩阵见[就绪评估](docs/demo/READINESS.md)。
- 待办与排期见 [Issue](https://github.com/rlsn/Lockdown-alpha/issues) 与[看板](https://github.com/users/sinmentis/projects/4)。产品规则以 [App Spec](docs/APP_SPEC.md) 为准。

## 快速开始

环境与首次构建命令见[开发指南：环境与首次启动](docs/DEVELOPMENT.md#环境与首次启动)。日常用 `.\Scripts\OpenStandalone.ps1` 打开前端（同机多开加 `-Windowed`），`.\Scripts\OpenEditor.ps1` 打开编辑器；画面模式、分辨率与帧率上限可在局内设置页调整。

## 常用入口

| 目的 | 入口 |
| --- | --- |
| AI 协作与实现约束 | [AGENTS](AGENTS.md) |
| 产品规则与完成标准 | [App Spec](docs/APP_SPEC.md) |
| 内容目录、公式与运行数值 | [内容与数值](docs/demo/CONTENT_AND_BALANCE.md) |
| 跨模块所有权、身份与时序契约 | [共享契约](docs/demo/SHARED_CONTRACTS.md) |
| 验收状态与已知限制 | [就绪评估](docs/demo/READINESS.md) |
| 环境、验证、数据修改、DEV 面板与任务管理 | [开发指南](docs/DEVELOPMENT.md) |
| 关卡编辑边界 | [关卡创作自由与开发约束](docs/LEVEL_AUTHORING_CONTRACT.md) |
| 各玩法域接口与不变量 | `docs\systems\`（索引见[开发指南](docs/DEVELOPMENT.md#按任务读取系统手册)） |
| 场景覆盖与里程碑清单 | [验证手册](docs/systems/VERIFICATION.md#最小验证选择) |
| 打包、PR 检查与每日发布包 | [CI 与发布](docs/systems/CI_AND_RELEASE.md#打包与产物) |

## 数据与素材边界

`docs` 供人与 AI 阅读；`SourceData` 为生成器输入；游戏只读 `Content` 载荷与原生资产。运行输出写入忽略的 `Artifacts`。
音效与 3D 美术为 CC0 素材，背景音乐与标题/房间背景图为项目方提供；来源与哈希见 `SourceData\Audio\audio_cues.json`、`SourceData\Art\art_assets.json`、`SourceData\UI\ui_icons.json` 与 `ui_images.json`。

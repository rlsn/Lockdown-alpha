# Lockdown

**v0.14.2 · Unreal Engine 5.8.4 · Windows PC**

**对外标题：谁是雾港最强淫人王**（“淫”是雾港黑话，只指把人揍趴下）。技术项目名仍为 Lockdown。

雾港大学城后街的烂尾球场上，脆皮大学生无差别大乱斗：一款采用 MOBA 点击操作和俯视斜角镜头的自由混战游戏。
打野与战斗获取成长，用属性点和主动技能（后续还有构件）建立优势，最终在旗杆上喷上自己的涂鸦并守住，成为淫人王。

## 当前状态

Demo 的完整流程已经实现：前端建房或加入 → Lobby（Host 选 0–3 个对手 AI）→ 开局休息 R0（开局主动三选一与开局属性点）→
三段野外各接一次休息（从零打野、对抗、升级加点并在 2、6 级再选主动；休息时调整构筑）→ 最终野外旗杆开放后喷漆、守护与围攻 →
唯一结果 → Host 带房间回 Lobby 开新局。
运行入口只有前端和唯一 Demo 地图。

- 规则工坊、元素、构件、公共休息室、PK、
  野外奖励、蓝图与评估入口已接入；局部规则和同机拥有连接已验证，完整对局与整体验收仍未完成，
  限制见[实施就绪评估](docs/demo/READINESS.md)。
- 首版内容与数值是用户批准的首次试玩输入，**没有做过平衡测试**。
- 所有联机验证都在同一台机器的回环连接上运行；跨机器局域网与公网直连没有验证。
- 下一阶段以好玩为先：先补齐玩法逻辑和内容，再反复试玩调整，最后做正式美术和声音。
  待办与排期见 [Issue](https://github.com/rlsn/Lockdown-alpha/issues) 与[看板](https://github.com/users/sinmentis/projects/4)。
- 验收结果、未运行与未实现项见[实施就绪评估](docs/demo/READINESS.md)。产品规则只以 [App Spec](docs/APP_SPEC.md) 为准。

## 环境

Windows、工具链、Git LFS 与引擎路径的要求统一见[环境与首次启动](docs/DEVELOPMENT.md#环境与首次启动)。

## 快速开始

首次检出按[启动命令](docs/DEVELOPMENT.md#环境与首次启动)拉取 LFS、检查环境并构建 Editor；
日常用 `.\Scripts\OpenStandalone.ps1` 打开前端，`.\Scripts\OpenEditor.ps1` 打开编辑器。

游戏固定以无边框全屏、桌面分辨率运行。同机多开时给每个 `OpenStandalone.ps1` 加 `-Windowed`（1280×720 窗口），
一个建房，其余用回环地址加入；窗口模式下指针离开窗口就无法边缘平移，正式试玩不要用。

## 测试与打包

| 目的 | 入口 |
| --- | --- |
| 日常按改动验证 | `.\Scripts\Verify.ps1`，选择规则与定向参数见[开发指南：验证](docs/DEVELOPMENT.md#验证) |
| 数据生成、地图同步与资产导入 | [数据修改](docs/DEVELOPMENT.md#数据修改)与[世界管线](docs/systems/WORLD_AND_DATA_PIPELINE.md#标准关卡编辑与同步) |
| 规则、场景与全新检出检查 | [最小验证选择](docs/systems/VERIFICATION.md#最小验证选择)；完整检查仅用于[里程碑等指定场合](docs/systems/VERIFICATION.md#合并前与里程碑检查) |
| 试玩日志整理 | [试玩问题排查](docs/DEVELOPMENT.md#试玩问题排查) |
| 打包与打开打包版 | [打包与产物](docs/systems/CI_AND_RELEASE.md#打包与产物) |

所有运行输出写入 Git 忽略的 `Artifacts`。

## 协作流程

常驻约束见 [AGENTS](AGENTS.md)；建票、排期、PR 和合并步骤见[任务管理](docs/DEVELOPMENT.md#任务管理issuemilestoneproject)。

## 文档地图

| 需要 | 入口 |
| --- | --- |
| 游戏规则与完成标准 | [App Spec](docs/APP_SPEC.md) |
| 环境、日常验证、数据修改、运行与试玩排查 | [开发指南](docs/DEVELOPMENT.md) |
| 各检查与场景的覆盖范围、里程碑清单 | [验证手册](docs/systems/VERIFICATION.md) |
| 打包、PR 检查、每日试玩包 | [CI 与发布](docs/systems/CI_AND_RELEASE.md) |
| 协作与实现规则（人和 AI 共用） | [AGENTS](AGENTS.md) |
| 验收结果、未运行与未实现项 | [实施就绪评估](docs/demo/READINESS.md) |
| 跨模块契约 | [共享契约](docs/demo/SHARED_CONTRACTS.md) |
| 各玩法域的接口与实现说明 | `docs\systems` 下的系统手册（索引见[开发指南](docs/DEVELOPMENT.md#按任务读取系统手册)） |
| 首版内容与数值（唯一来源） | [内容与数值](docs/demo/CONTENT_AND_BALANCE.md) |

## 文件边界与第三方素材

人和 AI 阅读 `docs`；生成器读取 `SourceData`；游戏读取 `Content` 载荷和原生资产。
修改数据走对应的 Sync 或导入入口，不手改生成的载荷、合同头文件或二进制资产。

音效与 3D 美术均为 CC0 素材，背景音乐是项目所有者提供的原创曲目；来源、许可与文件哈希记录在
`SourceData\Audio\audio_cues.json` 与 `SourceData\Art\art_assets.json`，每个素材包的许可原文与源文件放在同一目录。

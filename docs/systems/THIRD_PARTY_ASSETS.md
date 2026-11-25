# 第三方素材：授权、存放与安装

所有第三方素材（VFX、动画、音频、UI、美术）登记在 `SourceData/ThirdParty/third_party_assets.json`。各领域清单（`SourceData/Art/art_assets.json`、`SourceData/Audio/audio_cues.json`）仍记录逐文件的来源与校验，总清单通过 `domain_manifest` 指向它们，不重复抄写。检查入口：`python -B Scripts/ThirdPartyAssets.py check`。

## 1. 授权维度

每个素材分别记录六项，不互相推断：

| 维度 | 字段 | 说明 |
| --- | --- | --- |
| 获取费用 | `acquisition_cost` | 免费或付费；付费素材未经用户批准不得购买 |
| 商业使用 | `commercial_use` | 能否用在商业游戏里 |
| AI 输入 | `ai_input` | 能否把内容交给生成式 AI 模型查看或处理 |
| AI 训练 | `ai_training` | 能否用于训练、微调或数据集 |
| 公开源资产再分发 | `public_source_redistribution` | 能否以源资产形式放进公开仓库 |
| 私下协作共享 | `private_collaborator_sharing` | 能否通过私有仓库与协作者共享 |

“允许 AI 输入”不代表允许公开再分发：Fab 标准许可的素材即使允许 AI，也只能在私有仓库与协作者共享（Fab EULA §5a）。

条款依据（以领取时生效的协议为准）：
- Fab EULA 标准许可：§3a 私下使用与修改；§4c 以目标代码随游戏发行，终端用户不得提取；§5a 源资产只在私有仓库与协作者共享；§6(b)(vii) 与 §16(l)(i) 禁止把 NoAI 内容用于生成式 AI 的数据集、开发和“训练输入”，并说明只操作原内容、分类或排列已有内容的程序不算生成式 AI。
- Fab 服务条款 §3.3(i) 与旧版 Epic Content License §17：NoAI 内容不得“作为生成式 AI 的输入”，比 EULA 宽。
- CC0 可自由使用与再分发；CC-BY 需保留署名并记录修改。两者都不随项目代码许可证重新授权。
- 其他公开仓库里出现某个素材，不能说明它允许公开再分发。

## 2. NoAI 素材

`ai_input` 为禁止的素材保留正常游戏使用：领取、导入、放进关卡、绑定玩法事件、以目标代码发行。导入、配置与集成用 Unreal commandlet、Python 等确定性脚本按资产路径、名称和 User 参数完成；Agent 可以写这些通用代码，但不读取素材内容，也不查看含有它的截图或采集，外观由用户验收。不用于训练、微调或数据集，不移除 NoAI 标记。授权有歧义时记录具体条款，必要时向作者或 Epic 确认，不把未确认的解释当作许可。

## 3. 存放

| 位置 | 内容 | Git |
| --- | --- | --- |
| `Content/ThirdParty/CCBY/<包>/` | CC-BY 原始素材，原样；同目录 `CREDITS.md` 写作者、链接、许可与修改记录 | 提交（LFS） |
| `Content/ThirdParty/Fab/<包>/` | 不允许公开再分发的素材（Fab 标准许可、NoAI） | `.gitignore` 忽略 |
| `Content/ThirdParty/Fab/_Adapted/` | 含上述素材内容的项目改造副本 | 忽略 |
| 各领域生成目录（如 `Content/Lockdown/Art/VFX/Niagara/Generated/`） | 只属于各自生成器 | 提交；第三方素材不得放入 |

本机素材库：环境变量 `LD_THIRDPARTY_ROOT` 指向一个工作区以外的目录，内含 `Fab/<包>/`。`python -B Scripts/ThirdPartyAssets.py link` 把当前工作区的 `Content/ThirdParty/Fab` 建成指向 `$LD_THIRDPARTY_ROOT/Fab` 的目录联接，所有工作区共用一份，并报告清单里哪些本机素材缺失。新工作区在 `git lfs pull` 之后、首次构建之前运行一次。

获取：在 Fab 商品页领取 → Epic Games Launcher → 库 → Fab 库 → “添加到工程”，再把包目录移入 `$LD_THIRDPARTY_ROOT/Fab/`，在总清单填写领取日期、原始路径与校验信息。第三方自动下载工具（`Fobal/fab-downloader`、`zirklerite/fabcli`）走启动器私有接口，需要把账户授权码交给工具并保存会话令牌，目前不使用。

## 4. 运行时与打包

- 引用本机专用素材用软引用（`TSoftObjectPtr`）按需加载；缺失时记一次日志、该效果不显示，不用替代外观冒充，游戏照常启动。
- 正式打包前检查清单里标为发行必需的素材全部存在，缺任何一个就失败，不静默遗漏。
- 提交前运行 `ThirdPartyAssets.py check`：受限目录下的文件不得出现在 Git 索引、暂存区或 LFS 中。

## 5. 长期方案：公开代码仓库 + 私有素材仓库（未实施）

可行做法：另建私有仓库（Git LFS）存放 `Fab/<包>/`，本机克隆到 `LD_THIRDPARTY_ROOT`，各工作区用上面的联接共用，不按工作区重复下载；CI 用只读部署密钥或细粒度令牌拉取私有仓库，公开仓库的派生 PR 拿不到它，所以依赖这些素材的检查只在可信的 CI 上运行。私有仓库与 LFS 配额、CI 凭证和仓库可见性由用户决定，Agent 不自行创建仓库或修改可见性。

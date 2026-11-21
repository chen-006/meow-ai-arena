[English](README_en.md)

# 正式形象 v2（第 11 集起）

第 11 集《回旋镖大作战》起视频里用的这一套角色立绘和表情差分。和上一层 [art/](../README.md)（第 10 集用的那一套）相比，除 GPT 家的 Astra、Sol 和豆包不变外，十位角色重新设计了服装、道具和整体造型，发色、发型和各家的品牌元素保持可辨认。先看 [全家福.jpg](全家福.jpg) 和 [表情差分总览](expressions/总览.jpg)。

**署名**：角色形象设计参考：小叶子XIYZ（B 站）；美术和制作：喵了个猫了个咪了个猫。以 [MIT 许可](../../LICENSE) 发布，使用时请写上这两行并附本仓库链接。角色名里的模型名和公司名属于各自的权利人，只用来说明角色对应哪个模型，不表示这些公司认可或参与本项目；头上的品牌标志发饰是照各家标志画的。

## 目录

| 路径 | 内容 |
|---|---|
| [portraits/](portraits/) | 十位角色的大版立绘，透明底 PNG（约 1024×1536）：Opus、Fable、DeepSeek、Gemini、Kimi、GLM、Grok、Qwen（千问）、MiMo、Ernie（文心） |
| [portraits_small/](portraits_small/) | 九位的小版（年少版）：Claude、DeepSeek、Gemini、Kimi、GLM、Grok、Qwen、MiMo、Ernie。第 11 集里 Gemini 4 Hydrogen 用的是 `Gemini_小` |
| [expressions/<角色>/](expressions/) | 十位大版和 Gemini 小版的表情差分：每人 12 张透明底单张（`01_常态` 开心、得意、生气、伤心、震惊、无语、思考、流口水、头顶铁盆、尴尬地笑 `12_犯困`），十位大版另有 640×640 的 `头像.png`；[总览.jpg](expressions/总览.jpg)、[Gemini_小_总览.jpg](expressions/Gemini_小_总览.jpg) |
| [prompts/portraits/](prompts/portraits/)、[prompts/portraits_small/](prompts/portraits_small/) | 大版、小版定稿用的英文提示词原文 |
| [prompts/expressions/](prompts/expressions/) | 表情差分母版的通用提示词（`表情母版说明.txt`，每位角色再接上自己的设计段），Gemini 小版的逐张提示词和生图记录 |
| [大小版对照.jpg](大小版对照.jpg)、[全家福.jpg](全家福.jpg) | 预览 |

GPT 家的 Astra（大）、Sol（小）沿用第 10 集那一套，在 [../portraits/](../portraits/)（`GPT_大_Astra.png`、`GPT_小_Sol.png`）；豆包保持官方形象的写实风，不在这里。

## 怎么做的

- 图是用 OpenAI 的图像模型经 Codex 内置的生图功能出的（不经接口，型号名没有返回）；先出若干张草稿挑一张，再出正式稿。提示词由节目组和 Gemini 一起写。
- 表情差分：每位角色按 `表情母版说明.txt` 出 4×3 的十二格母版，切成单张；头像从"常态"取脸部。母版原图不在仓库里。
- 收录时表情差分单张从约 1254×1254 等比缩到 1024 见方以内（和第 10 集那一套同样大小），立绘、小版、头像是原图。
- 生图时用的参考图（各家商标图、上一版立绘、草稿）不打包：商标是第三方的，草稿已被定稿取代。

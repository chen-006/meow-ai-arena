[English](README_en.md)

# 角色立绘、表情差分和生图提示词

从第 10 集《纸上谈兵》起，视频里代表各家 AI 的角色换成了这一套新形象。这里是交付版立绘、切好的表情差分、定稿实际用到的提示词和模型参数，以及出图、整理、切图的脚本。和仓库其他部分一样以 [MIT 许可](../LICENSE) 发布。

**署名**：角色形象设计：小叶子XIYZ（B 站）；美术和制作：喵了个猫了个咪了个猫。新形象参考自小叶子XIYZ 的设计，由节目组制作。使用时请写上这两行并附本仓库链接。

角色名里的模型名和公司名属于各自的权利人，只用来说明角色对应哪个模型，不表示这些公司认可或参与本项目。

## 目录

| 路径 | 内容 |
|---|---|
| [portraits/](portraits/) | 22 张立绘（交付版）：1672×941 透明底 PNG，人物高 927 像素、水平居中，可以直接叠进 16:9 画面 |
| [expressions/<角色>/](expressions/) | 13 位角色的表情差分：每人 12 张 1024×1024 透明底单张（胸口以上，胸口切线贴底），加 512×512 的方形头像 `头像_方.png` 和圆形头像 `头像_圆.png`；[预览_总览.jpg](expressions/预览_总览.jpg) 是总览 |
| [prompts/portraits/](prompts/portraits/) | 立绘的提示词原文 |
| [prompts/expressions/](prompts/expressions/) | 表情差分母版的提示词原文；`军需官_表情名.json` 是军需官专用的 12 个格名 |
| [prompts/jobs_portraits.txt](prompts/jobs_portraits.txt)、[jobs_expressions.txt](prompts/jobs_expressions.txt) | 定稿那一次出图的任务单：每行 `名字\|提示词文件\|参考图,…\|尺寸` |
| [tools/](tools/) | 出图、整理、切图脚本 |

## 角色

| 文件名 | 角色 | 这一集谁用它 | 形象参考 |
|---|---|---|---|
| `Claude_特大_Fable` | Claude 三姐妹的大姐 Fable | Claude Fable 5.2 | 小叶子XIYZ 的 Claude 设计 |
| `Claude_大_Opus` | Claude 二姐 Opus | — | 同上 |
| `Claude_小` | Claude 小妹 | — | 同上 |
| `GPT_大_Astra` | GPT 大姐 Astra | — | 小叶子XIYZ 的 GPT 设计 |
| `GPT_小_Sol` | GPT 小妹 Sol | GPT-6.2 Sol | 同上 |
| `DeepSeek_大`、`DeepSeek_小` | DeepSeek 大姐、小妹 | DeepSeek-V4.1-Flash（大） | 小叶子XIYZ 的 DeepSeek 设计 |
| `Gemini_大`、`Gemini_小` | Gemini 大姐、小妹 | Gemini 4 Carbon（大）、Gemini 4 Hydrogen（小） | 小叶子XIYZ 的 Gemini 设计 |
| `Kimi_大`、`Kimi_小` | Kimi | Kimi K3.1 Pro（大） | 节目组按小叶子XIYZ 的形象体系设计 |
| `千问_大`、`千问_小` | 千问 | Qwen4-Max-Preview（大） | 同上 |
| `文心_大`、`文心_小` | 文心 | 文心 ERNIE 5.5 Flash Preview（大） | 同上 |
| `GLM_大`、`GLM_小` | GLM | — | 同上 |
| `Grok_大`、`Grok_小` | Grok | — | 同上 |
| `MiMo_大`、`MiMo_小` | MiMo | — | 同上 |
| `军需官` | 节目自己的串场角色（不代表任何厂商） | 串场 | 节目组原创 |

"大"是成年的大姐姐版，"小"是约 10 岁的小妹妹版；表情差分只做了大版、GPT 小妹 Sol 和军需官。头上的品牌标志发饰是照各家标志画的。

## 模型和参数

- 模型：OpenAI 图像接口 `gpt-image-2.6-sunburst`，带参考图时走 `images/edits`。
- 立绘母版：`size 2160x3840`、`quality max`、`background transparent`、`output_format png`、`n 1`。母版经 [tools/finalize.py](tools/finalize.py) 整理成交付版：拉齐边缘 alpha、去掉和主体不相连的小碎块、按人物包围盒缩放到 927 像素高，贴进 1672×941 透明画布。
- 表情差分母版：`size 3456x2304`，其余同上。每位角色两张 3×2 六格母版（A：常态、开心、得意、生气、伤心、震惊；B：无语、思考、流口水、头顶铁盆、尴尬地笑、犯困；军需官另有一套），经 [tools/slice.py](tools/slice.py) 按连通域切成 12 张，两张母版统一比例，再从"常态"取脸部做头像。
- 母版原图（2160×3840、3456×2304）不在仓库里。

## 提示词

每份提示词都是拼好的全文（角色段 + 参考图说明 + 通用画风段），出图时原样发给接口。

| 图 | 提示词 |
|---|---|
| 大版：Fable、Opus、DeepSeek、Gemini、Kimi、GLM、Grok、千问、MiMo | `v3_cur_<名>.txt`（第三版设计，定稿） |
| 文心大版 | `v3_ernie_da.txt` |
| 小版：Claude、DeepSeek、Gemini、Kimi、GLM、Grok、千问、MiMo、文心 | `v3_small_<角色>.txt` |
| GPT 大姐 Astra、小妹 Sol | `full_gpt_astra.txt`、`full_gpt_sol.txt`（第一轮就定稿，没有重出） |
| 军需官 | `quartermaster_A.txt`（A、B 两个方案里选了 A） |
| Gemini 大版的中间稿（只作第三版 Gemini 的参考图，本身不是成品） | `v2_gem_da.txt` |
| 表情差分 | `prompts/expressions/<角色>_A.txt`、`_B.txt` |

- 通用画风段：[`_style_v3.txt`](prompts/portraits/_style_v3.txt)（第三版）由 [`_style.txt`](prompts/portraits/_style.txt)（第一轮）改来，GPT 两张用的是第一轮的那段；军需官用的是第三版改了一处的版本（她不是任何厂商的角色，去掉标志发饰的要求，只许虚构的铜钱徽），已经拼在 `quartermaster_A.txt` 里。
- `gpt_astra.txt`、`gpt_sol.txt`、`v3_ernie_da_char.txt` 是角色设计段，[tools/build_prompts.py](tools/build_prompts.py) 从它们和 `v3_cur_*.txt` 里取设计要点拼表情差分的提示词；用这个脚本重拼，24 份结果和 `prompts/expressions/` 里的逐字相同。军需官的两份差分提示词是另一个一次性脚本拼的，原文在这里。

## 参考图（不在仓库里）

任务单里的参考图都没有收录，下面写明每类是什么：

| 任务单里的文件 | 是什么 |
|---|---|
| `refs/tri_GPT.jpg`、`tri_DeepSeek.jpg` 等 | 小叶子XIYZ 画的角色三视图（第三方作品，不收录） |
| `refs/logo_<家>.png` | 各家公司的标志图，只用来画发饰的形状（商标，不收录） |
| `refs/style_Astra.png`、`style_Sol.png` | 第 1～9 集视频里用的 GPT 大姐 Astra（原形象作者小叶子XIYZ）和小妹 Sol 立绘，作画风参考 |
| `refs/anchor_*.png`、`cur_*.png`、`oldsmall_*.png`、`draft_*.png`、`elder_*.png` | 节目组同一轮重制里更早的版本（白裙版立绘、样稿），作画风和身形参考；已被第三版取代，不收录 |
| `refs/elder3_<角色>.png` | 第三版大版母版缩小到 1080×1920，作小版的参考 |
| `raw/Gemini_大_猫娘__*.png` | 上表里 Gemini 大版的中间稿 |
| 差分任务单里的 `refs/upper_<角色>.png`、`full_<角色>.png` | 由母版用 [tools/prep.py](tools/prep.py) 裁出的上半身和全身，贴浅灰底 |

## 怎么重出

会调用付费接口（立绘每张约 0.4～0.5 美元，差分母版每张约 0.49 美元），先确认再跑。测试过的环境是 Windows 11 + Git Bash，Python 3.12，装 `requests`、`pillow`、`numpy`、`opencv-python`。

```text
# 在一个空的工作目录里：把 art/prompts/portraits/ 复制成 prompts/，自备参考图放进 refs/
export OPENAI_API_KEY=...                         密钥只从环境变量读
bash <仓库>/art/tools/batch.sh 任务单.txt              出立绘母版到 raw/（任务单格式同 jobs_portraits.txt）
python <仓库>/art/tools/finalize.py raw/<母版>.png <名字>   整理成交付版，写到 portraits/、portraits_2x/
# 表情差分：立绘母版放进 master/<角色>.png
python <仓库>/art/tools/prep.py                    裁参考图到 expr/refs/
python <仓库>/art/tools/build_prompts.py           拼提示词到 expr/prompts/
cd expr && bash <仓库>/art/tools/batch.sh 任务单.txt && cd ..   出差分母版到 expr/raw/（任务单格式同 jobs_expressions.txt）
python <仓库>/art/tools/slice.py <角色>            切图到 expressions/<角色>/
python <仓库>/art/tools/preview.py                 出预览
```

脚本都在当前目录下读写；输出目录可以用环境变量 `ART_OUT`（finalize）、`EXPR_OUT`（slice、preview）、`PERSONA_MASTER`（prep 读母版）改。

## 和制作时的差别

- 脚本里的本机绝对路径改成"当前工作目录"和环境变量；密钥原来从本机文件读，改成从环境变量 `OPENAI_API_KEY` 读；去掉了脚本里写死的本机代理设置。
- 表情差分的拼提示词脚本原名 `build_prompts_v3.py`，这里叫 `build_prompts.py`（第一轮的旧版不收录）；`slice.py` 加了一处：`expr/<角色>_表情名.json` 存在时用它的格名（制作时给军需官切图是临时改了格名）。
- 任务单是把定稿那几次出图的任务行合到一起，参考图路径原样保留。
- 制作过程中的样稿、旧版（白裙版）、废片、母版原图、调用记录和花费统计不收录。

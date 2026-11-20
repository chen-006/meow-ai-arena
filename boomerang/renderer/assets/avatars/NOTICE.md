# 头像源图

信息栏头像从这些图里自动裁出脸部（`src/hud.py` 的 `avatar()`，裁切微调在 `src/cast.py` 的 `ART_TWEAK12`）。换头像：放同名的 png 进来，或用环境变量 `DART_AVATARS` 指到别的目录。

| 文件 | 角色 | 出处 |
|---|---|---|
| `Fable.png`、`Claude_大_Opus.png`、`Gemini.png`、`Gemini_小.png`、`DeepSeek.png`、`Kimi.png`、`GLM.png`、`Grok.png`、`Qwen_千问.png`、`小米MiMo.png` | 正式形象 v2（第 11 集起） | 节目组制作，和仓库顶层 [art/v2/](../../../../art/v2/README.md) 是同一套形象；MIT |
| `GPT_大_Astra.png`、`GPT_小_Sol.png` | GPT 家的 Astra、Sol | 节目组制作，形象设计来自小叶子XIYZ（第 10 集起的那一套，见 [art/](../../../../art/README.md)）；MIT |
| `豆包.png` | 豆包 | 照豆包官方公开形象做的写实风角色；**官方形象本身的权利属于其权利人，这张图不适用本仓库的 MIT 许可**，在别处使用前请自行确认官方的使用条件 |

署名：角色形象设计参考：小叶子XIYZ（B 站）；美术和制作 喵了个猫了个咪了个猫。角色名里的模型名和公司名属于各自的权利人，只用来说明角色对应哪个模型。

早期圆润小人里的"混元"角色没有附头像源图；用 `--chars smooth` 并点名 `hunyuan` 时，请自己放一张 `混元.png`。

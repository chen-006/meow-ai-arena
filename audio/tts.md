# 配音：音色、提示词、参数

节目里所有人声都是语音合成。这一页写第 10 集（纸上谈兵）实际用的设置，前几集基本相同，差别写在最后；第 11 集（OA 争霸·上）的音色、提示词和参数单独写在"第 11 集的设置"一节。

## 一句话版

| 谁 | 合成服务和模型 | 音色 | 后期 |
|---|---|---|---|
| 旁白 | Gemini TTS（`gemini-3.8-flash-tts`） | `Charon` | 句中停顿压到 0.3 秒，切首尾静音，-16 LUFS |
| 各位 AI 角色、军需官 | Breeze TTS 2（`breeze-tts-2`） | 音色库里的现成音色，见下表 | 切首尾静音，1.1 倍速（军需官 1.2 倍），-16 LUFS；不压句中停顿 |
| 豆包 | 火山引擎 `seed-tts-2.0` | `zh_female_shuangkuaisisi_uranus_bigtts`（爽快思思） | 24 kHz，不给语气指令，1.1 倍速，-16 LUFS |

## 旁白的提示词

送给 Gemini TTS 的内容 = 风格提示 + 读音提示 + 这一句的语气（有的话）+ `Read this aloud:` + 台词。

风格提示原文：

```text
Voice: an energetic male esports commentator, lively and punchy, brisk but clear. Speak Mandarin Chinese at a normal, natural conversational speed: not rushed, not slow. Keep it flowing, with no long pauses inside the line. A light, natural touch of emotion that suits the line is welcome; no exaggerated acting, no sighs, no filler sounds, and do not add any words.
```

这一句有语气时，在后面接：

```text
Performance direction for THIS line, follow it closely (it overrides the default commentator delivery): <语气表里这一句的语气>
```

读音提示：台词里出现下面这些词时，把对应的一句接进提示词。

```text
token      Pronounce "token" as the English word token.
Opus       Pronounce "Opus" like the English word opus: OH-pus.
Sonnet     Pronounce "Sonnet" like the English word sonnet.
Haiku      Pronounce "Haiku" like the English word haiku: HIGH-koo.
Astra      Pronounce "Astra" as AH-struh.
GLM        Say "GLM" as three letters: G, L, M.
GPT        Say "GPT" as three letters: G, P, T.
MiMo       Pronounce "MiMo" as MEE-moh.
Kimi       Pronounce "Kimi" as KEE-mee.
Fable      Pronounce "Fable" like the English word fable: FAY-bul.
Carbon     Pronounce "Carbon" like the English word carbon.
Hydrogen   Pronounce "Hydrogen" like the English word hydrogen: HIGH-druh-jen.
Anthropic  Pronounce "Anthropic" like the English word: an-THROP-ik.
OpenAI     Pronounce "OpenAI" as "Open A I".
GitHub     Pronounce "GitHub" as the English word GIT-hub.
```

旁白爱把整句念两遍。同一段提示词重试，出来的几乎是同一段音频，所以重配时每次换一句接在风格提示后面：

```text
Read the text exactly once from start to end; never repeat it or any part of it.
Say it once, then stop.
One single reading only, then end the audio.
```

## 角色的音色和人设（第 10 集）

送给 Breeze 的指令 = 人设 + `这一句的语气：<语气表里这一句的语气>。`，参数 `guidance_scale` 2.5、`speed` 1.0。

| 角色 | 音色名 | 音色编号 | 人设（指令原文） |
|---|---|---|---|
| 军需官 | 见习幽灵小唐 | `voc_759mqhgufhf4` | 个子小小的军装小丫头，嘴欠的雌小鬼，最爱笑话别人乱花钱，带着坏笑、得意洋洋，句尾拖长上扬像在逗人玩；是屑不是凶，不吼，不嗲过头。 |
| Sol（GPT-6.2 Sol） | 迷糊小魔女 | `voc_6t8vrjmcq4cy` | 十到十三岁的小女孩，元气天真又机灵，软萌清脆，像发现了糖果一样开心，爱用"呀""啦"。 |
| Fable（Claude Fable 5.2） | 冷面铁捕 | `voc_4fcdc882gyps` | 二十五岁左右从容讲道理的大姐姐，带一点书卷气，句子完整，温和不冷、不端着，做砸了会平静地自嘲。 |
| Carbon（Gemini 4 Carbon） | 哈基米娘 | `voc_rh6e8hdbraz6` | 十六到十八岁的猫系偶像少女，傲娇爱炫耀，清脆灵动，说话干脆，带点"哼"的小得意，嘴上从不服输。 |
| Hydrogen（Gemini 4 Hydrogen） | 傲娇云朵精灵 | `voc_2g2xfx23838q` | 十二三岁的小猫妹妹，个子小小的，声音又细又高又奶，语速偏快，天真较真，爱喊姐姐，急了带一点哭腔，开心了会蹦起来。 |
| DeepSeek | 大肥鱼（机灵版） | `voc_u3nxkd5578wc` | 十六到十九岁的急性子邻家姑娘，干净清脆，说话快，理直气壮，精打细算，嘴硬。 |
| Kimi | 墨香少女 | `voc_7jsxftc2rm3n` | 十七到二十岁清冷知性的文艺少女，斯文、一本正经，吐字清晰，舒缓从容。 |
| 千问 | 幽香调香师 | `voc_rzke52bs2ej8` | 新中式打扮的年轻女子，说话慢、稳，有点高人范儿，平静淡然，偶尔被现实打断也很平静地承认。 |
| 文心 | 冰下弦音 | `voc_5cemd59kz4cz` | 十六七岁干劲十足的少女小侦探，声音清亮，老实直率，懵的时候有点不敢相信。 |
| Opus（片尾一句） | 香农小姐 | `voc_b86bfwct4cjn` | 二十三四岁的成熟淑女，刚演完一场戏卸了妆，长出一口气，懒洋洋地抱怨，带一点笑意。 |

往期出过场的其他角色（Astra、Sonnet、GLM、MiMo、Grok、Mistral、混元等）的音色在 [tts/voices_breeze.json](tts/voices_breeze.json)。

Fable 的音色是这样挑的：三个候选音色各配同一句，让 Gemini 听了描述，再由人选。

## 逐句语气

稿子定了以后，先写一份逐句语气表，再配音。第 10 集的原文在 [tts/ep10_tones.md](tts/ep10_tones.md)。写法上的几条经验：

- 角色句每句都写；旁白里要喊、要演、被打断的句子也写，串场的句子用平常口气，不写。
- 拟声词要明说"这不是在念字，是发出一声……"，再写音高怎么走、多长、不要加什么（笑声、气声）。不这么写，合成会把"唔""嗯？"老老实实念成字。
- 稿子的句尾不要用破折号收：合成要么整句念两遍，要么句尾被截断。
- 一句旁白别超过七八十个字，太长容易整句念两遍。

## 读法：只改送去合成的文字，字幕不变

- 数字、型号、小数先转成中文读法（"1700"送"一千七百"），不然会被念成"幺七零零"。
- 多音字换成同音字："干看着"送"甘看着"（防念成 gàn），"调回放"送"掉回放"（防念成 tiáo）。
- 英文缩写拆开："APP"送"A P P"，"MIT"送"M I T"，"D100"送"D 一百"。
- 个别句子整句换写法，写在语气表第三栏。

## 挑版本和查错

- 有语气的句子每句配 3 版，把候选连同台词和语气一起发给 Gemini，让它听了挑：拟声词有没有念成字、有没有多余的笑声和杂音、有没有漏字、读错音。都不合格就改语气或念法重配。
- 旁白另用本地语音识别（faster-whisper small）数听到的字数和语速，抓"整句念了两遍"和"句尾被截断"；识别出来的文字本身不可靠，只用它的字数和时间。
- 同一个识别结果里的逐字时刻拿来给字幕定换屏时间，比按字数估要准（按字数估会早 0.3～0.5 秒）。

## 后期（ffmpeg 滤镜）

```text
切首尾静音  silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.05,areverse,silenceremove=start_periods=1:start_threshold=-45dB:start_silence=0.15,areverse
响度        acompressor=threshold=-20dB:ratio=3:attack=5:release=80,loudnorm=I=-16:TP=-1.5:LRA=7
倍速        atempo=1.1（角色、豆包）；atempo=1.2（军需官）
```

成品统一 48 kHz 立体声 16 位。混音时有人说话的地方背景音乐压低，整片约 -13 LUFS。

## 第 11 集（OA 争霸·上）的设置

模型、服务、后期滤镜都和上面一样（旁白 Charon、角色 Breeze TTS 2 的 `guidance_scale` 2.5 / `speed` 1.0、豆包 `seed-tts-2.0`），这一集只有下面这些不同。**逐句语气表不收**（等于解说稿），只写做法。

### 旁白：整组念、英文提示

- 这一集旁白按语气表分成"组"（开赛前 10 组，一两句到十来句；后半程 8 段共 56 个大组，每组一两分钟），每组一口气念完，再用本地语音识别的词级时间加静音切成句，放回各自的卡点。
- **提示词只写英文。** 中文的导演说明混在提示里，Charon 会把说明念出来、还会自己编词；Vertex 的 TTS 也不支持 `system_instruction`（返回 400）。所以送去的内容仍是 风格提示 + 读音提示 + `Read this aloud:` + 台词，风格提示改成下面这段（同一个人、同一个状态念完整段），再接**这一组的一句英文基调**（例如"这一段开场，节奏紧凑一点""规则讲解，清楚利落，数字念清楚但别一字一顿"，不收）：

```text
Keep EXACTLY the same person and the same state for the whole passage (it is one part of a longer episode narrated by the same man): a man around thirty, relaxed and conversational, like telling a friend about a match he is looking forward to; a light variety-show touch, but NOT an announcer or broadcaster voice, never shouting, no exaggerated acting. Keep loudness, pitch and energy within a narrow range. Speak Mandarin Chinese.
```

- 想让整组快一点时在后面加一句：`Keep it a little quicker and tighter than usual, with shorter pauses, but do not rush the words or swallow syllables.`
- 整组念两遍的重配：照第 10 集，换"只念一遍"的那几句接在风格提示后面。一两分钟的大组即使加了也还是会重复，所以每组至少配 3 版，再配"中组"（约 200 字一组）当后备，**逐句**在所有版本里挑：整版重复扣 20、句子被标记扣 10、听审点名扣 5、语速偏离扣分，大组优先于中组。
- 拆成短组（4 句、100 字以内）试过：同一段大组一口气 46 秒，拆成小组 62 秒，Charon 拖慢、乱停，作废，保留大组。

### 旁白的后期（和第 10 集不同的几处）

| 项 | 第 11 集 |
|---|---|
| 句内停顿 | 超过 0.35 秒的静音（-40 dB 以下）压到 0.3 秒；句首句尾不动 |
| 语速 | 后半程整体 **1.05 倍**（`atempo=1.05`）；开赛前按卡点时长对个别句微调 |
| 句间 | 拼段时每句之间补 0.3 秒静音 |
| 响度 | 同第 10 集（压缩 + -16 LUFS） |

### 角色音色（第 11 集）

送给 Breeze 的指令格式不变（人设 + 这一句的语气）。第 11 集沿用的音色编号见上面第 10 集的表和 [tts/voices_breeze.json](tts/voices_breeze.json)；新增或改动的如下（人设为指令原文）：

| 角色 | 音色名 | 音色编号 | 人设 |
|---|---|---|---|
| Astra | 清雅引路人 | `voc_jgkfvjryu84b` | 二十三到二十六岁阳光明媚的成熟大姐姐，清亮亲切，尾音微微上扬，自信从容。 |
| Grok | 傲气学姐 | `voc_kwje43kewnst` | 十五到十七岁傲娇拽酷的小恶魔少女，清脆但冷淡，居高临下带点嘲讽，句子短，面无表情地毒舌。 |
| GLM | 灵犀助手 | `voc_qtzjr6fsztb5` | 十六七岁戴透明护目镜的理科少女，声音清亮年轻，带一点少年气的小得意，笃定但不老成、不端着。（原先用的"韧性逐风者"`voc_a9qfvny3tuky` 太成熟，换掉） |
| MiMo | 街头追风少女 | `voc_nzxhjaxm6h59` | 十七八岁穿白橙机能外套的明亮少女，开心元气、有点皮，自信又有野心。 |
| MiMo 的队友（路人台词） | 山野灵果儿 | `voc_cgxpmn7k9gs2` | 十八九岁的普通女生路人，清爽自然，口语化，带一点无奈的吐槽感，不嘶吼。 |
| Opus | 香农小姐 | `voc_b86bfwct4cjn` | 二十二到二十五岁的名门大小姐，清冷端庄、有书卷气，吐字讲究，礼貌里带一点距离感。 |

- MiMo 的音色是这样选的：三个候选（山野灵果儿、街头追风少女、灵动晨曦）各配同一句，让 Gemini 听了描述选；第 7、8 集 MiMo 用的"傲气学姐"在第 9 集起归了 Grok，两人同场所以换。
- Sol、Fable、Carbon、Hydrogen、DeepSeek、Kimi、千问沿用第 10 集的音色，人设删掉了"做砸了会自嘲"这类只属于第 10 集剧情的句子。豆包仍是爽快思思，24 kHz，1.1 倍速，不给语气指令。
- 角色句每句 3 版，后期同第 10 集（切首尾静音、1.1 倍、-16 LUFS）。

### 第 11 集新增的读法

| 台词里 | 送合成的文字 |
|---|---|
| `OpenAI` | `Open A I` |
| `Kimi K3.1` | `Kimi K 三点一` |
| `Qwen4` | `Qwen 四`（再配读音提示：`Pronounce "Qwen" as one syllable: chwen.`） |
| `官方 API` | `官方 A P I` |
| `Epoch AI` | `Epoch A I`（`Pronounce "Epoch" like the English word epoch: EE-pok.`） |
| `OA` | `O A`（`Say "O A" as two English letters: O, A.`） |
| `OpenCode` | `Open Code` |

另加了读音提示：`ChatGPT`（chat 加字母 G、P、T）、`Qoder`（像 coder）、`D S H`、`A P I`（逐个字母读）。旁白的"和"一律送"跟"（防念成 hàn），字幕不变；用 Gemini 听审时要告诉它这是规定，不然会把"跟"判成错字。

## 往期的差别

- 旁白从第 3 集起一直是 Charon 加上面这段风格提示；第 6 集试过在提示词里加一点台湾口音，后来没再用。
- 角色从第 6 集起用 Breeze 的现成音色；倍速第 6 集是 1.25，第 7 集起定为 1.1。
- 豆包第 8 集及以前用的是 Seed Audio 1.0 的同一个"爽快思思"音色，第 9 集起换成 seed-tts-2.0。
- 第 11 集见上面"第 11 集的设置"：旁白改成整组念、提示只写英文，压停顿 + 1.05 倍。
- 英文版：旁白还是 Charon，角色改用 Gemini 的英文音色（中文音色念英文有口音）。

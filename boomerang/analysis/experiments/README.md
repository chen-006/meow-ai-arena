# 探索小实验：复跑与单变量对照

[bot 解析](../README.md) 里的"复跑""单变量对照"不是比赛时的对局，而是写分析时另打的：**只改程序里的一处**（关一个分支、改一个常数、补一个单位换算），和原版在同一批地图、种子、座位配对下比较，看这一处值多少分。这个目录把当时的补丁、对局结果和跑批脚本收在一起。

先说清三件事：

- 这些对局**不是正式赛**，也不进任何榜单；对手、种子、局数每个实验各不相同，数字**不要和 `results/` 里的正式成绩混用**。
- 每个改版和原版只差几行。为了不让仓库里出现十几份几乎一样的 10 万行程序，这里只放**补丁**（`patches/`），用 `make_variant.py` 套到仓库里的选手原作上生成改版。
- 有些对局会按时钟决定算多深（比赛时就有的现象），重跑逐局未必完全一致；不带时钟的对手（例如下面验证用的 Astra 6.2 对 Fable）是逐局一致的。

## 目录

| 位置 | 内容 |
|---|---|
| `patches/<组>/<名>.diff` | 59 个改版相对选手原作的补丁（`--- a/` 一行写明原作路径）；`index.json` 是总表 |
| `make_variant.py` | 把补丁套到原作上，生成改版 `.cpp` |
| `tools/run_batch.py`、`tools/runner.py`、`tools/run_specs.py` | 三个跑批脚本（当时用的三套），路径已改成用仓库的选手包 |
| `raw/<组>.zip` | 逐局结果（每局一条：地图、种子、座位、胜负、击杀、死因、各动作次数、`digest` 等）；zip 里每个 json 对应一次跑批 |
| `results_summary.json` | `run_batch.py` 累积的汇总（键 `标签\|地图\|种子范围`），244 项 |
| `readers/` | 当时读结果、读回放的小脚本（统计死因、击杀、落坑等），参考用，见下 |

## 怎么复现

```bash
cd boomerang
python build_engine.py                                    # 先编出裁判 workspace/arena.exe（只需一次）
cd analysis/experiments
python make_variant.py patches/fable/fable_v_thr60.diff /tmp/fable_v_thr60.cpp

# 逐局结果和 raw/fable.zip 里 v_thr60_af.json 对比：Astra 6.2 对 Fable 改版，2 张图 × 3 个种子 × 换边 = 12 局
cd tools
python runner.py out.json --p astra62=../../../bots/gpt-6.2-astra/player.cpp --p fable=/tmp/fable_v_thr60.cpp \
       --mode 0 --seeds 11001 11003 --maps twinlakes,ruins
```

- `runner.py`：任意程序、一对一或四人局（`--mode 1`，4 个程序轮座），输出一个逐局结果 json。`--maps all` 是六张海选图；`raw/` 里大部分文件是 `--seeds 11001 11019`、六图、两边各坐一次共 228 局（四人局 456 局）。程序名用 `名=路径.cpp`，名字就是结果里的 `name`。
- `run_batch.py`：`python run_batch.py 标签 A.cpp B.cpp 地图[,地图] 种子起 种子止 [--ffa C.cpp D.cpp]`，结果追加到同目录的 `results.json`，就是 `results_summary.json` 的来历。
- `run_specs.py`：`python run_specs.py out.json duel|ffa 名=路径,名=路径 种子起 种子数 [--maps a,b]`，输出带汇总的 json（`raw/glm.zip`、`hydrogen.zip`、`grok.zip` 里的 `summary` 字段）。
- 地图：`workspace/maps/` 两张公开图 + `tournament/hidden/` 四张保密图，六张就是海选图。每步 10 CPU 毫秒，不进沙箱，和比赛一致。
- 已实测：用上面的命令重打 Astra 6.2 对 Fable 原版和 `fable_v_thr60` 各 12 局，**12/12 局的 `digest` 和 `raw/fable.zip` 里的记录一致**；`run_batch.py` 小批试跑也正常。

## 改版清单

下面每一行都是补丁对应的真实改动（读 `.diff` 可以看到逐行内容）。"原作"指仓库 `bots/` 下对应选手的 `player.cpp`（GPT-6 Astra 对照组是 `appendix/bots/gpt-6-astra/`）。


### Fable 5.2（含对 Astra 6.2 的修改）

| 改版 | 原作 | 改动 |
|---|---|---|
| `astra_v_nolearn` | `bots/gpt-6.2-astra/player.cpp` | 学习项 `exp(4.8*success)` 改成常数 1.0（不按对手表现调整模式权重） |
| `astra_v_noparry` | `bots/gpt-6.2-astra/player.cpp` | 去掉两处"招架"候选（`consider(..., A_NONE ...)` 与 `A_MELEE` 各加 `false &&`） |
| `astra_v_nowalkparry` | `bots/gpt-6.2-astra/player.cpp` | 只去掉第一处（走位中的招架候选） |
| `fable_v_allaggr` | `bots/claude-fable-5.2/player.cpp` | 推演时对近处目标一律先用"激进"假设（改动 15 行，见补丁） |
| `fable_v_nohold` | `bots/claude-fable-5.2/player.cpp` | 常量 `HOLD_B` 150 → 0 |
| `fable_v_nophantom` | `bots/claude-fable-5.2/player.cpp` | "幻影"罚分项 `pen*1000` → 0 |
| `fable_v_norecallnear` | `bots/claude-fable-5.2/player.cpp` | 去掉"镖在手且离落点 5 米内允许召回"这一条 |
| `fable_v_thr60` | `bots/claude-fable-5.2/player.cpp` | 阈值（全员无镖 / 其他）0.33、0.45 → 0.45、0.60 |

### GLM 5.3

| 改版 | 原作 | 改动 |
|---|---|---|
| `glm_fullonly` | `bots/glm-5.3/player.cpp` | 两个候选分取小改成只取第二个（`return s0 < s1 ? s0 : s1` → `s1`） |
| `glm_nearestonly` | `bots/glm-5.3/player.cpp` | 四人局只对最近的对手做该项计算 |
| `glm_oneside` | `bots/glm-5.3/player.cpp` | 绕行偏转角由按序号奇偶交替 ±4500 改成固定 +4500 |
| `glm_probe` | `bots/glm-5.3/player.cpp` | 只加日志（stderr 输出 REALDEAD / FAKEDEAD），不改决策 |

### Gemini 4 Carbon（含 Hydrogen 去 CPU 版）

| 改版 | 原作 | 改动 |
|---|---|---|
| `carbon_nocpu` | `bots/gemini-4-carbon/player.cpp` | 去掉 CPU 预算检查（总是判"没超"），让它每步算满 |
| `carbon_nocpu_allhyp` | `bots/gemini-4-carbon/player.cpp` | 在 nocpu 基础上，去掉条件里的 `hyps[h].reactive &&` |
| `carbon_nocpu_disc035` | `bots/gemini-4-carbon/player.cpp` | 在 nocpu 基础上，折扣系数 `hw *= 0.78` → 0.35 |
| `carbon_nocpu_noclashbonus` | `bots/gemini-4-carbon/player.cpp` | 在 nocpu 基础上，镖撞镖加成 20 → 0 |
| `carbon_nocpu_noperim` | `bots/gemini-4-carbon/player.cpp` | 在 nocpu 基础上，关掉四人局、手持镖时的一个分支（代码里叫 perim，条件前加 `false &&`） |
| `carbon_nocpu_nosandwich` | `bots/gemini-4-carbon/player.cpp` | 在 nocpu 基础上，关掉四人局 `d_near_threat2 < 5600` 的"夹击"分支（`false &&`） |
| `carbon_nocpu_notax` | `bots/gemini-4-carbon/player.cpp` | 在 nocpu 基础上，关掉四人局存活对手 ≥2 时的一个分支（`false &&`，代码里叫 tax） |
| `carbon_orig_noperim` | `bots/gemini-4-carbon/player.cpp` | 保留原版 CPU 预算，只关掉同一个 perim 分支 |
| `hydrogen_nocpu` | `bots/gemini-4-hydrogen/player.cpp` | Hydrogen 去掉 CPU 预算中断，每步算满 |

### Gemini 4 Hydrogen

| 改版 | 原作 | 改动 |
|---|---|---|
| `h_ffaH11` | `bots/gemini-4-hydrogen/player.cpp` | 四人局推演深度 9 → 11 |
| `h_ffaIdeal6000` | `bots/gemini-4-hydrogen/player.cpp` | 四人局存活对手 ≥2 时的理想距离 4800 → 6000 |
| `h_ffaNoDashIn` | `bots/gemini-4-hydrogen/player.cpp` | 四人局不进入"手持镖时冲入"的分支（条件加 `&& cf.mode != M_FFA`） |
| `h_ffaNoKick` | `bots/gemini-4-hydrogen/player.cpp` | 四人局不进入原 `else` 分支（改成 `else if (cf.mode != M_FFA)`，名字里的 NoKick 指不踢） |
| `h_nosync` | `bots/gemini-4-hydrogen/player.cpp` | 去掉"推演里的镖在飞而真镖落地"时的同步分支 |
| `h_onemodel` | `bots/gemini-4-hydrogen/player.cpp` | 只用一种对手模型（原版一对一、四人局用两种） |

### Grok 4.7

| 改版 | 原作 | 改动 |
|---|---|---|
| `grok_fixgraze` | `bots/grok-4.7/player.cpp` | 两处死亡返回值不再扣"擦边"罚分 |
| `grok_graze10` | `bots/grok-4.7/player.cpp` | 擦边罚分缩到十分之一（420000/50000 → 42000/5000） |
| `grok_nophantom` | `bots/grok-4.7/player.cpp` | 不生成幻影 |
| `grok_noreact` | `bots/grok-4.7/player.cpp` | 在第 396 行后插入 `if (true) return true;`（让该函数恒返回真） |

### Claude Opus 5.5

| 改版 | 原作 | 改动 |
|---|---|---|
| `opus_v_nocpu` | `bots/claude-opus-5.5/player.cpp` | 去掉 CPU 保护（不因超时提前 break） |
| `opus_v_nocpu_dash400` | `bots/claude-opus-5.5/player.cpp` | 在 nocpu 基础上，冲刺加成 -150 → -400 |
| `opus_v_nocpu_parry` | `bots/claude-opus-5.5/player.cpp` | 在 nocpu 基础上，加入"对方手持镖且自由时，命中权重 ×0.2"的招架假设 |
| `opus_v_parry` | `bots/claude-opus-5.5/player.cpp` | 保留原版 CPU 保护，只加入同样的招架假设 |

### GPT-6.2 Astra / GPT-6.2 Sol / GPT-6 Astra（对照组）

| 改版 | 原作 | 改动 |
|---|---|---|
| `a62_nolearn` | `bots/gpt-6.2-astra/player.cpp` | 去掉两处学习权重（模式权重 `exp(4.8*…)` 改成 1.0、瞄准技巧权重改成先验） |
| `a62_noready` | `bots/gpt-6.2-astra/player.cpp` | 关注范围 `ready<dt+3 ? 4.0 : 2.6-1.2*pressure` 固定为 4.0 |
| `a62_recallhold` | `bots/gpt-6.2-astra/player.cpp` | 召回等待的加成系数 0.55 → 1.6 |
| `a6_dash2000` | `appendix/bots/gpt-6-astra/player.cpp` | GPT-6 Astra（对照组）冲刺罚分 140 → 2000 |
| `s62_combo` | `bots/gpt-6.2-sol/player.cpp` | 搜索宽度几项一起放大：NearH 8→13、"无动作"候选上限 5→9、一对一动作数上限 13→17、模拟次数上限（四人局 1650→2700，一对一 2010→2400） |
| `s62_combo_dash20` | `bots/gpt-6.2-sol/player.cpp` | 同 `s62_combo`，再把冲刺罚分 8.4 → 20 |
| `s62_dash20` | `bots/gpt-6.2-sol/player.cpp` | 冲刺罚分 8.4 → 20 |
| `s62_moves9` | `bots/gpt-6.2-sol/player.cpp` | "无动作"类候选上限 5 → 9 |
| `s62_near13` | `bots/gpt-6.2-sol/player.cpp` | NearH 8 → 13 |
| `s62_sims2700` | `bots/gpt-6.2-sol/player.cpp` | 四人局模拟次数上限 1650 → 2700 |
| `sol16` | `appendix/bots/gpt-sol-grey-desktop/player.cpp` | 与 **Sol（GPT-6.1 灰度版，对照组）** 的作品逐字节相同，没有补丁（做内战/配对用的副本） |

### DeepSeek V4.1

| 改版 | 原作 | 改动 |
|---|---|---|
| `deepseek_cap2400` | `bots/deepseek-v4.1-flash/player.cpp` | 超出偏好距离的惩罚封顶 4500 → 2400 |
| `deepseek_diag` | `bots/deepseek-v4.1-flash/player.cpp` | 只加日志（每步最优打分），不改决策 |
| `deepseek_melee700` | `bots/deepseek-v4.1-flash/player.cpp` | 近战判定距离 1450 → 700 |

### 豆包 Seed Evolving

| 改版 | 原作 | 改动 |
|---|---|---|
| `doubao_evade_speed` | `bots/doubao-seed-evolving/player.cpp` | 闪避预测里把对手速度两轴各 ×4（对齐引擎的速度换算） |
| `doubao_fix_guard_only` | `bots/doubao-seed-evolving/player.cpp` | 门卫移动改按跑速换算后再预测位置（`scale_to(..., run_v)`），只修这一处 |
| `doubao_fix_strafe_only` | `bots/doubao-seed-evolving/player.cpp` | 横摆改用实际距离做归一（原来除以固定单位），只修这一处 |

### Kimi K3.1

| 改版 | 原作 | 改动 |
|---|---|---|
| `kimi_diag` | `bots/kimi-k3.1-pro/player.cpp` | 只加日志（每步最优分、动作），不改决策 |
| `kimi_fix_speed` | `bots/kimi-k3.1-pro/player.cpp` | 自由态预测对手位移时两轴各 ×4 |

### MiMo V2.6

| 改版 | 原作 | 改动 |
|---|---|---|
| `mimo_block_real` | `bots/mimo-v2.6-pro/player.cpp` | 格挡判断只看真实的镖，不含假设轨迹 |
| `mimo_orbit_flip` | `bots/mimo-v2.6-pro/player.cpp` | 绕圈方向每 2 秒翻向（原版固定 +1） |
| `mimo_fix_waypoint` | `bots/mimo-v2.6-pro/player.cpp` | 把同一方向的目的地延长到 3 米 |

### 千问 4 Max

| 改版 | 原作 | 改动 |
|---|---|---|
| `qwen_dashcost400` | `bots/qwen4-max/player.cpp` | 冲刺在评估分里额外扣 400 |
| `qwen_diag` | `bots/qwen4-max/player.cpp` | 只加日志，不改决策 |
| `qwen_fix_units` | `bots/qwen4-max/player.cpp` | 八处位移换算补上 `/1000` 单位换算 |

## 对局结果清单

下表由脚本直接从 `raw/*.zip` 重算：一对一列"胜:负:平"（按文件里的出场顺序列各程序），四人局列场均得分（名次分 + 每次击杀 1 分，和正式赛同口径；`run_specs.py` 的文件用它自己输出的汇总）。


### fable.zip

| 文件 | 局数 | 种子 | 结果 |
|---|---|---|---|
| `a_nolearn_af.json` | 228 | 11001–11019 | fable 54胜 : astra62 171胜，平 3 |
| `a_noparry_af.json` | 228 | 11001–11019 | astra62 179胜 : fable 48胜，平 1 |
| `a_nowalkparry_af.json` | 228 | 11001–11019 | astra62 179胜 : fable 48胜，平 1 |
| `astra62_fable_60_meta.json` | 60 | 11001–11005 | astra62 46胜 : fable 14胜 |
| `base_af.json` | 228 | 11001–11019 | astra62 182胜 : fable 45胜，平 1 |
| `base_af_rep.json` | 228 | 11001–11019 | astra62 182胜 : fable 45胜，平 1 |
| `base_fc.json` | 228 | 11001–11019 | carbon 77胜 : fable 151胜 |
| `base_fo.json` | 228 | 11001–11019 | fable 147胜 : opus 80胜，平 1 |
| `fable_opus_228.json` | 228 | 11001–11019 | opus 101胜 : fable 126胜，平 1 |
| `ffa_allaggr19.json` | 456 | 11001–11019 | fable 2.70 / opus 2.38 / carbon 1.99 / hydrogen 1.89 |
| `ffa_base.json` | 240 | 11001–11010 | fable 2.80 / opus 2.27 / carbon 1.93 / hydrogen 1.96 |
| `ffa_base19.json` | 456 | 11001–11019 | fable 2.65 / opus 2.29 / carbon 2.13 / hydrogen 1.90 |
| `ffa_nophantom19.json` | 456 | 11001–11019 | opus 2.36 / carbon 2.14 / hydrogen 1.88 / fable 2.58 |
| `v_nohold_af.json` | 228 | 11001–11019 | astra62 183胜 : fable 40胜，平 5 |
| `v_nohold_fo.json` | 228 | 11001–11019 | opus 97胜 : fable 129胜，平 2 |
| `v_nophantom_af.json` | 228 | 11001–11019 | fable 56胜 : astra62 171胜，平 1 |
| `v_nophantom_fc.json` | 228 | 11001–11019 | carbon 88胜 : fable 139胜，平 1 |
| `v_nophantom_fo.json` | 228 | 11001–11019 | fable 133胜 : opus 92胜，平 3 |
| `v_norecallnear_af.json` | 228 | 11001–11019 | astra62 176胜 : fable 50胜，平 2 |
| `v_norecallnear_fo.json` | 228 | 11001–11019 | opus 98胜 : fable 127胜，平 3 |
| `v_thr60_af.json` | 228 | 11001–11019 | astra62 166胜 : fable 57胜，平 5 |
| `v_thr60_fc.json` | 228 | 11001–11019 | fable 134胜 : carbon 94胜 |
| `v_thr60_fo.json` | 228 | 11001–11019 | fable 143胜 : opus 85胜 |

### carbon.zip

| 文件 | 局数 | 种子 | 结果 |
|---|---|---|---|
| `c_allhyp_ch.json` | 228 | 11001–11019 | hydrogen 122胜 : carbon 100胜，平 6 |
| `c_base_ch.json` | 228 | 11001–11019 | hydrogen 119胜 : carbon 103胜，平 6 |
| `c_base_ch2.json` | 228 | 11001–11019 | hydrogen 119胜 : carbon 103胜，平 6 |
| `c_base_ch_rep.json` | 228 | 11001–11019 | hydrogen 119胜 : carbon 103胜，平 6 |
| `c_disc035_ch.json` | 228 | 11001–11019 | hydrogen 111胜 : carbon 113胜，平 4 |
| `c_disc035_ch_rep.json` | 228 | 11001–11019 | hydrogen 111胜 : carbon 113胜，平 4 |
| `c_noclashbonus_ch.json` | 228 | 11001–11019 | hydrogen 118胜 : carbon 104胜，平 6 |
| `ffa2_base.json` | 456 | 11001–11019 | hydrogen 2.62 / glm 1.77 / deepseek 1.93 / carbon 2.66 |
| `ffa2_noperim.json` | 456 | 11001–11019 | carbon 2.68 / hydrogen 2.48 / glm 1.70 / deepseek 2.13 |
| `ffa3_orig.json` | 456 | 11001–11019 | carbon 2.56 / grok 2.03 / sol 2.97 / kimi 1.23 |
| `ffa3_orig_noperim.json` | 456 | 11001–11019 | carbon 2.62 / grok 1.82 / sol 2.88 / kimi 1.48 |
| `ffa_base.json` | 456 | 11001–11019 | carbon 1.92 / hydrogen 2.05 / opus 2.21 / fable 2.78 |
| `ffa_noperim.json` | 456 | 11001–11019 | hydrogen 2.00 / opus 2.07 / fable 2.72 / carbon 2.18 |
| `ffa_nosandwich.json` | 456 | 11001–11019 | hydrogen 1.98 / opus 2.21 / fable 2.80 / carbon 1.98 |
| `ffa_notax.json` | 456 | 11001–11019 | carbon 2.04 / hydrogen 2.00 / opus 2.24 / fable 2.67 |
| `ffa_orig.json` | 456 | 11001–11019 | hydrogen 2.12 / opus 2.23 / fable 2.63 / carbon 1.96 |
| `ffa_orig_noperim.json` | 456 | 11001–11019 | carbon 2.14 / hydrogen 2.06 / opus 2.16 / fable 2.61 |

### opus.zip

| 文件 | 局数 | 种子 | 结果 |
|---|---|---|---|
| `base_fo_2.json` | 228 | 11001–11019 | opus 92胜 : fable 135胜，平 1 |
| `base_fo_rep.json` | 228 | 11001–11019 | opus 96胜 : fable 132胜 |
| `o_nocpu_ao.json` | 228 | 11001–11019 | opus 37胜 : astra62 190胜，平 1 |
| `o_nocpu_fo_1.json` | 228 | 11001–11019 | opus 93胜 : fable 135胜 |
| `o_nocpu_fo_2.json` | 228 | 11001–11019 | opus 93胜 : fable 135胜 |
| `o_nocpu_fo_rep.json` | 228 | 11001–11019 | opus 93胜 : fable 135胜 |
| `o_nocpudash_ao.json` | 228 | 11001–11019 | astra62 177胜 : opus 51胜 |
| `o_nocpudash_fo.json` | 228 | 11001–11019 | opus 101胜 : fable 122胜，平 5 |
| `o_nocpuparry_ao.json` | 228 | 11001–11019 | astra62 180胜 : opus 46胜，平 2 |
| `o_nocpuparry_fo.json` | 228 | 11001–11019 | opus 103胜 : fable 124胜，平 1 |

### glm.zip

| 文件 | 局数 | 种子 | 结果 |
|---|---|---|---|
| `duel_base_vs_grok_13001.json` | 360 | 13001–13030 | glm 216:140:4 / grok 140:216:4 |
| `duel_fullonly_vs_grok_11001.json` | 228 | 11001–11019 | glm_fullonly 111:117:0 / grok 117:111:0 |
| `duel_fullonly_vs_grok_12001.json` | 360 | 12001–12030 | glm_fullonly 191:163:6 / grok 163:191:6 |
| `duel_fullonly_vs_grok_13001.json` | 360 | 13001–13030 | glm_fullonly 197:161:2 / grok 161:197:2 |
| `duel_oneside_vs_grok_11001.json` | 228 | 11001–11019 | glm_oneside 135:93:0 / grok 93:135:0 |
| `duel_oneside_vs_grok_12001.json` | 360 | 12001–12030 | glm_oneside 200:157:3 / grok 157:200:3 |
| `ffa_base_31007.json` | 144 | 31007–31012 | glm 1.90 / grok 2.15 / hydrogen 2.00 / carbon 2.85 |
| `ffa_base_32001.json` | 240 | 32001–32010 | glm 1.96 / grok 2.09 / hydrogen 2.06 / carbon 2.77 |
| `ffa_fullonly_31001.json` | 144 | 31001–31006 | glm_fullonly 1.64 / grok 2.28 / hydrogen 2.24 / carbon 2.72 |
| `ffa_fullonly_31007.json` | 144 | 31007–31012 | glm_fullonly 1.84 / grok 1.78 / hydrogen 2.25 / carbon 2.96 |
| `ffa_nearestonly_31001.json` | 144 | 31001–31006 | glm_nearestonly 2.04 / grok 2.03 / hydrogen 2.19 / carbon 2.64 |
| `ffa_nearestonly_31007.json` | 144 | 31007–31012 | glm_nearestonly 2.09 / grok 2.04 / hydrogen 1.99 / carbon 2.79 |
| `ffa_nearestonly_32001.json` | 240 | 32001–32010 | glm_nearestonly 1.91 / grok 1.90 / hydrogen 2.20 / carbon 2.87 |
| `probe_duel.json` | 228 | 11001–11019 | glm_probe 124:101:3 / grok 101:124:3 |
| `probe_ffa.json` | 144 | 31001–31006 | glm_probe 1.78 / grok 2.11 / hydrogen 2.11 / carbon 2.90 |

### hydrogen.zip

| 文件 | 局数 | 种子 | 结果 |
|---|---|---|---|
| `duel_base_vs_carbon_11001.json` | 228 | 11001–11019 | hydrogen 125:100:3 / carbon 100:125:3 |
| `duel_base_vs_carbon_12001.json` | 360 | 12001–12030 | hydrogen 201:155:4 / carbon 155:201:4 |
| `duel_nosync_vs_carbon_11001.json` | 228 | 11001–11019 | h_nosync 124:101:3 / carbon 101:124:3 |
| `duel_nosync_vs_carbon_12001.json` | 360 | 12001–12030 | h_nosync 189:167:4 / carbon 167:189:4 |
| `duel_onemodel_vs_carbon_11001.json` | 228 | 11001–11019 | h_onemodel 130:92:6 / carbon 92:130:6 |
| `duel_onemodel_vs_carbon_12001.json` | 360 | 12001–12030 | h_onemodel 198:155:7 / carbon 155:198:7 |
| `ffa_base.json` | 144 | 31001–31006 | hydrogen 2.03 / carbon 2.54 / opus 2.40 / grok 1.91 |
| `ffa_base_32001.json` | 240 | 32001–32010 | hydrogen 2.14 / carbon 2.37 / opus 2.48 / grok 1.85 |
| `ffa_base_b.json` | 144 | 31007–31012 | hydrogen 2.15 / carbon 2.17 / opus 2.63 / grok 1.95 |
| `ffa_ffaH11_31001.json` | 144 | 31001–31006 | h_ffaH11 1.99 / carbon 2.31 / opus 2.71 / grok 1.86 |
| `ffa_ffaH11_31007.json` | 144 | 31007–31012 | h_ffaH11 1.75 / carbon 2.28 / opus 2.66 / grok 2.20 |
| `ffa_ffaIdeal6000_31001.json` | 144 | 31001–31006 | h_ffaIdeal6000 2.05 / carbon 2.53 / opus 2.49 / grok 1.77 |
| `ffa_ffaIdeal6000_31007.json` | 144 | 31007–31012 | h_ffaIdeal6000 1.92 / carbon 2.47 / opus 2.43 / grok 2.05 |
| `ffa_ffaNoDashIn_31001.json` | 144 | 31001–31006 | h_ffaNoDashIn 2.08 / carbon 2.23 / opus 2.53 / grok 2.00 |
| `ffa_ffaNoDashIn_31007.json` | 144 | 31007–31012 | h_ffaNoDashIn 2.32 / carbon 2.05 / opus 2.50 / grok 1.91 |
| `ffa_ffaNoDashIn_32001.json` | 240 | 32001–32010 | h_ffaNoDashIn 2.08 / carbon 2.23 / opus 2.55 / grok 1.98 |
| `ffa_ffaNoKick_31001.json` | 144 | 31001–31006 | h_ffaNoKick 2.16 / carbon 2.42 / opus 2.69 / grok 1.59 |
| `ffa_ffaNoKick_31007.json` | 144 | 31007–31012 | h_ffaNoKick 2.19 / carbon 2.35 / opus 2.36 / grok 1.98 |
| `ffa_ffaNoKick_32001.json` | 240 | 32001–32010 | h_ffaNoKick 1.76 / carbon 2.46 / opus 2.70 / grok 1.91 |
| `gaunt2_base_vs_deepseek.json` | 180 | 22001–22015 | h_base 115:65:0 / deepseek 65:115:0 |
| `gaunt2_base_vs_grok.json` | 180 | 22001–22015 | h_base 107:72:1 / grok 72:107:1 |
| `gaunt2_onemodel_vs_deepseek.json` | 180 | 22001–22015 | h_onemodel 89:85:6 / deepseek 85:89:6 |
| `gaunt2_onemodel_vs_grok.json` | 180 | 22001–22015 | h_onemodel 79:100:1 / grok 100:79:1 |
| `gaunt_base_vs_deepseek.json` | 120 | 21001–21010 | h_base 86:33:1 / deepseek 33:86:1 |
| `gaunt_base_vs_glm.json` | 120 | 21001–21010 | h_base 63:56:1 / glm 56:63:1 |
| `gaunt_base_vs_grok.json` | 120 | 21001–21010 | h_base 62:58:0 / grok 58:62:0 |
| `gaunt_base_vs_opus.json` | 120 | 21001–21010 | h_base 52:66:2 / opus 66:52:2 |
| `gaunt_base_vs_sol.json` | 120 | 21001–21010 | h_base 50:70:0 / sol 70:50:0 |
| `gaunt_onemodel_vs_deepseek.json` | 120 | 21001–21010 | h_onemodel 62:54:4 / deepseek 54:62:4 |
| `gaunt_onemodel_vs_glm.json` | 120 | 21001–21010 | h_onemodel 61:46:13 / glm 46:61:13 |
| `gaunt_onemodel_vs_grok.json` | 120 | 21001–21010 | h_onemodel 49:71:0 / grok 71:49:0 |
| `gaunt_onemodel_vs_opus.json` | 120 | 21001–21010 | h_onemodel 48:69:3 / opus 69:48:3 |
| `gaunt_onemodel_vs_sol.json` | 120 | 21001–21010 | h_onemodel 48:71:1 / sol 71:48:1 |

### grok.zip

| 文件 | 局数 | 种子 | 结果 |
|---|---|---|---|
| `duel_base_vs_glm_11001.json` | 228 | 11001–11019 | grok 93:135:0 / glm 135:93:0 |
| `duel_base_vs_glm_12001.json` | 360 | 12001–12030 | grok 154:203:3 / glm 203:154:3 |
| `duel_fixgraze_vs_glm_11001.json` | 228 | 11001–11019 | grok_fixgraze 104:124:0 / glm 124:104:0 |
| `duel_fixgraze_vs_glm_12001.json` | 360 | 12001–12030 | grok_fixgraze 159:200:1 / glm 200:159:1 |
| `duel_graze10_vs_glm_11001.json` | 228 | 11001–11019 | grok_graze10 100:127:1 / glm 127:100:1 |
| `duel_graze10_vs_glm_12001.json` | 360 | 12001–12030 | grok_graze10 155:204:1 / glm 204:155:1 |
| `duel_nophantom_vs_glm_11001.json` | 228 | 11001–11019 | grok_nophantom 98:130:0 / glm 130:98:0 |
| `duel_nophantom_vs_glm_12001.json` | 360 | 12001–12030 | grok_nophantom 165:192:3 / glm 192:165:3 |
| `duel_noreact_vs_glm_11001.json` | 228 | 11001–11019 | grok_noreact 89:138:1 / glm 138:89:1 |
| `duel_noreact_vs_glm_12001.json` | 360 | 12001–12030 | grok_noreact 169:186:5 / glm 186:169:5 |
| `ffa_base_31001.json` | 144 | 31001–31006 | grok 2.12 / glm 1.88 / hydrogen 2.24 / carbon 2.69 |
| `ffa_base_31007.json` | 144 | 31007–31012 | grok 2.28 / glm 1.85 / hydrogen 2.15 / carbon 2.58 |
| `ffa_fixgraze_31001.json` | 144 | 31001–31006 | grok_fixgraze 2.30 / glm 1.89 / hydrogen 2.02 / carbon 2.79 |
| `ffa_fixgraze_31007.json` | 144 | 31007–31012 | grok_fixgraze 2.30 / glm 1.72 / hydrogen 2.62 / carbon 2.33 |

### astra6.zip

| 文件 | 局数 | 种子 | 结果 |
|---|---|---|---|
| `duel_vs_astra62.json` | 72 | 11001–11006 | astra6 12胜 : astra 60胜 |
| `ffa_vs_top.json` | 24 | 11001–11001 | astra 2.83 / fable 2.67 / opus 1.48 / astra6 1.98 |

### results_summary.json（`run_batch.py` 累积的标签）

标签里的字母：`k` Kimi、`q` 千问、`d` DeepSeek、`g` GLM、`m` MiMo、`f`/`ffa` 四人局，`duel` 一对一；`orig` 是原版，其余后缀对应上面的改版（`fixspeed` = `kimi_fix_speed`，`cap2400`、`melee700`、`dash400`、`units`、`waypoint`、`orbit`、`blockreal`、`evade4`、`guard`、`strafe`、`diag` 同名）。每个键里的 `w` 数组按程序出场顺序记胜场、`pts` 数组记四人局累计得分；**文件里没有记程序名**，对手阵容请对照各 bot 解析里引用该标签的那一句（例如 `mq_*` 是 MiMo 对千问、`dm_*` 是豆包对 MiMo、`dk_*` 是豆包对 Kimi、`kq_*`/`qk_*` 是 Kimi 与千问互为被测方）。

| 标签 | 总局数 | 地图数 | 种子 |
|---|---|---|---|
| `speed_kq` | 8 | 1 | 11001-11004 |
| `kq_orig` | 228 | 6 | 11001-11019 |
| `kq_orig2` | 456 | 12 | 11001-11019 |
| `kq_fixspeed` | 456 | 12 | 11001-11019 |
| `ffa_kqdg` | 72 | 3 | 11001-11006 |
| `ffa_kdiag` | 72 | 3 | 11001-11006 |
| `duel_kdiag` | 36 | 3 | 11001-11006 |
| `duel_dqdiag` | 36 | 3 | 11001-11006 |
| `ffa_dqdiag` | 72 | 3 | 11001-11006 |
| `dg_orig` | 456 | 12 | 11001-11019 |
| `dg_cap2400` | 456 | 12 | 11001-11019 |
| `dg_melee700` | 456 | 12 | 11001-11019 |
| `qk_orig` | 456 | 12 | 11001-11019 |
| `qk_dash400` | 456 | 12 | 11001-11019 |
| `fq_orig` | 288 | 12 | 11001-11006 |
| `fq_units` | 288 | 12 | 11001-11006 |
| `mq_orig` | 456 | 12 | 11001-11019 |
| `mq_waypoint` | 456 | 12 | 11001-11019 |
| `mq_orbit` | 456 | 12 | 11001-11019 |
| `mq_orig_b` | 456 | 12 | 11001-11019 |
| `mq_blockreal` | 456 | 12 | 11001-11019 |
| `dm_orig` | 456 | 12 | 11001-11019 |
| `dm_evade4` | 456 | 12 | 11001-11019 |
| `dm_guard` | 456 | 12 | 11001-11019 |
| `dm_strafe` | 456 | 12 | 11001-11019 |
| `dk_orig` | 228 | 6 | 11001-11019 |

## 没有收录的

- **回放**：当时每个实验都带回放用来逐局核对（`_bot/common/replays` 等，数百 MB）。没有收；需要的话用上面的命令加 `--replays 目录` 重打（`digest` 一致就是同一局）。结果 json 里原有的回放路径字段已经删掉。
- **`readers/`**：当时读结果和回放的小脚本，路径已改成 `<本机路径，未收录>`；其中一些读的是没收录的回放和比赛时的结果文件，**不能直接运行**，只当"当时是怎么统计死因、击杀、落坑的"的参考。
- **早先一轮分析（分析稿里称"早先一轮"）的消融**：Fable、Opus、Carbon、Hydrogen 的约 20 个修复版及其配对结果（`第1组`、`第2组` 的 `ablation_results.json`、`*_paired.json` 等，合计 1 GB 以上、主要是回放）。当前分析稿正文引用的是上面这一轮，所以没收；需要可以另行整理。
- 正式赛之外的另一类对局——全员大混战、灰度版海选、对照赛——不在这里，在 [`../../appendix/`](../../appendix/)。

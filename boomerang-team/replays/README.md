# 精选与代表性回放（共 28 局）

用浏览器打开 [`../workspace/viewer.html`](../workspace/viewer.html)，将任意 `.txt` 拖入窗口即可逐帧回放（空格播放，← → 单步）；也可以直接交给 [`../renderer/`](../renderer/README.md) 渲染成 1080p 视频。

## 挑选规则

- **官方回合代表局（18 局）**：完整保留每回合赛后按固定规则抽出的 3 局（Astra 视角中位时长胜局、中位时长负局、固定种子随机局；其中 R4 `g0`、R5 `g34`、R5 `g102`、R6 `g34` 同时也是经典看点局）。
- **精选经典局（10 局）**：覆盖三大模式、两队 10 位选手代表性高光与经典反转对局。
- 为控制仓库体积，未打包全部 10,600 局原始回放；所有回放保留完整状态帧、动作与队内 `MSG`（仅清除故障行尾部可能回显的程序原始输出）。

| 文件 | 回合 | 地图 | 局号 / 种子 | 胜方 | 时长 | 说明 |
|---|---|---|---|---|---:|---|
| [`R1_g113_astra_win_median.txt`](R1_g113_astra_win_median.txt) | R1 | `caldera` | g113 / s57 | Astra队 | 26.4s | 第 1 回合官方代表胜局（Astra 视角） |
| [`R1_g97_fable_win_median.txt`](R1_g97_fable_win_median.txt) | R1 | `caldera` | g97 / s49 | Fable队 | 26.4s | 第 1 回合官方代表负局（Astra 视角） |
| [`R1_g1959_random_sample.txt`](R1_g1959_random_sample.txt) | R1 | `caldera` | g1959 / s980 | Astra队 | 28.3s | 第 1 回合官方随机局（Astra 视角） |
| [`R2_g908_astra_win_median.txt`](R2_g908_astra_win_median.txt) | R2 | `caldera` | g908 / s455 | Astra队 | 19.8s | 第 2 回合官方代表胜局（Astra 视角） |
| [`R2_g343_fable_win_median.txt`](R2_g343_fable_win_median.txt) | R2 | `caldera` | g343 / s172 | Fable队 | 19.8s | 第 2 回合官方代表负局（Astra 视角） |
| [`R2_g484_random_sample.txt`](R2_g484_random_sample.txt) | R2 | `caldera` | g484 / s243 | Fable队 | 17.6s | 第 2 回合官方随机局（Astra 视角） |
| [`R3_g0_astra_win_median.txt`](R3_g0_astra_win_median.txt) | R3 | `piers` | g0 / s1 | Astra队 | 60.0s | 第 3 回合官方代表胜局（Astra 视角） |
| [`R3_g3_fable_win_median.txt`](R3_g3_fable_win_median.txt) | R3 | `piers` | g3 / s2 | Fable队 | 60.0s | 第 3 回合官方代表负局（Astra 视角） |
| [`R3_g1177_random_sample.txt`](R3_g1177_random_sample.txt) | R3 | `piers` | g1177 / s589 | Astra队 | 60.0s | 第 3 回合官方随机局（Astra 视角） |
| [`R4_g1_astra_win_median.txt`](R4_g1_astra_win_median.txt) | R4 | `piers` | g1 / s1 | Astra队 | 60.0s | 第 4 回合官方代表胜局（Astra 视角） |
| [`R4_g0_fable_win_median.txt`](R4_g0_fable_win_median.txt) | R4 | `piers` | g0 / s1 | Fable队 | 60.0s | 第 4 回合官方代表负局（Astra 视角） |
| [`R4_g466_random_sample.txt`](R4_g466_random_sample.txt) | R4 | `piers` | g466 / s234 | Astra队 | 60.0s | 第 4 回合官方随机局（Astra 视角） |
| [`R5_g34_astra_win_median.txt`](R5_g34_astra_win_median.txt) | R5 | `keeps` | g34 / s18 | Astra队 | 30.1s | 第 5 回合官方代表胜局（Astra 视角） |
| [`R5_g102_fable_win_median.txt`](R5_g102_fable_win_median.txt) | R5 | `keeps` | g102 / s52 | Fable队 | 30.1s | 第 5 回合官方代表负局（Astra 视角） |
| [`R5_g1172_random_sample.txt`](R5_g1172_random_sample.txt) | R5 | `keeps` | g1172 / s587 | Astra队 | 30.4s | 第 5 回合官方随机局（Astra 视角） |
| [`R6_g34_astra_win_median.txt`](R6_g34_astra_win_median.txt) | R6 | `keeps` | g34 / s18 | Astra队 | 28.1s | 第 6 回合官方代表胜局（Astra 视角） |
| [`R6_g526_fable_win_median.txt`](R6_g526_fable_win_median.txt) | R6 | `keeps` | g526 / s264 | Fable队 | 28.1s | 第 6 回合官方代表负局（Astra 视角） |
| [`R6_g973_random_sample.txt`](R6_g973_random_sample.txt) | R6 | `keeps` | g973 / s487 | Astra队 | 28.2s | 第 6 回合官方随机局（Astra 视角） |
| [`R1_g366_astra_1v5.txt`](R1_g366_astra_1v5.txt) | R1 | `caldera` | g366 / s184 | Astra队 | 26.6s | R1 歼灭经典：Astra 残局一挑五全歼对手 |
| [`R2_g1591_astra_quad_kill.txt`](R2_g1591_astra_quad_kill.txt) | R2 | `caldera` | g1591 / s796 | Astra队 | 13.1s | R2 歼灭经典：全场唯一一镖纯敌四杀（Astra 去程双杀 + 回程双杀） |
| [`R2_g202_kimi_triple_kick.txt`](R2_g202_kimi_triple_kick.txt) | R2 | `caldera` | g202 / s102 | Astra队 | 22.4s | R2 歼灭经典：Kimi 2.3 秒内连续三脚踢落三人终结比赛 |
| [`R3_g780_astra_33kills.txt`](R3_g780_astra_33kills.txt) | R3 | `piers` | g780 / s391 | Astra队 | 60.0s | R3 占点经典：Astra 单局 33 次击杀对手（全场单人纪录） |
| [`R3_g897_glm_7tk_carbon.txt`](R3_g897_glm_7tk_carbon.txt) | R3 | `piers` | g897 / s449 | Astra队 | 60.0s | R3 占点经典：GLM 单局七次挥砍误杀队友 Carbon（随后促使 R4 亲手改） |
| [`R4_g587_photo_finish_005.txt`](R4_g587_photo_finish_005.txt) | R4 | `piers` | g587 / s294 | Astra队 | 60.0s | R4 占点经典：最后一帧 86.30 比 86.25 绝杀（全场最小非零分差 0.05 分） |
| [`R5_g1662_glm_kick_king.txt`](R5_g1662_glm_kick_king.txt) | R5 | `keeps` | g1662 / s832 | Fable队 | 6.2s | R5 刺王经典：GLM 开局 5.55 秒将满三命敌王 Astra 一脚踢入坑 |
| [`R5_g954_astra_intercept.txt`](R5_g954_astra_intercept.txt) | R5 | `keeps` | g954 / s478 | Astra队 | 30.8s | R5 刺王经典：Astra 空中精准撞镖截下 Opus 必中飞镖护住 Kimi |
| [`R6_g1336_kings_duel.txt`](R6_g1336_kings_duel.txt) | R6 | `keeps` | g1336 / s669 | Astra队 | 17.1s | R6 刺王经典：双王残血贴身 2.25 厘米对决，Astra 踢接砍终结 Fable |
| [`R6_g291_carbon_penta.txt`](R6_g291_carbon_penta.txt) | R6 | `keeps` | g291 / s146 | Fable队 | 13.0s | R6 刺王经典：Carbon 一人包办对面全部五人击杀（含连削敌王三命） |

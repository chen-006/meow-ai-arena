# 复现正式赛

正式赛分两段：2–8 人（`formal-2to8.json`，每位选手一份）、9–15 人（`formal-9to15.json`，每位选手两份同场），合并评分。配置里写着原来的赛程种子，所以赛程（地图、座位、对阵）可以一模一样地重建。

在本目录运行（全程约 2 小时，12 并发；可以中断后用同一命令续跑）：

```bash
python ../../arena/tournament.py plan formal-2to8.json  run-2to8
python ../../arena/tournament.py plan formal-9to15.json run-9to15
python ../../arena/tournament.py run  run-2to8  --workers 8
python ../../arena/tournament.py run  run-9to15 --workers 8
python ../../arena/tournament.py rate run-2to8 run-9to15
```

注意：旗子每次刷新在哪个旗点用的是单独的随机数（每局现取，为了防止从地图反推），所以逐局比分不会和原赛完全相同，但总排名应当一致。每回合限时 50 毫秒，对机器负载敏感：每局程序数 × 并发局数，最好不超过 CPU 线程数的约 3/4。

# 复现正式赛

`evaluate.py`、`bt.py` 是正式赛用的评测和评分脚本，原样保留。`run.py` 是开源版加的小工具：先用正式赛的编译参数把 C++ 选手编译好，再调用 `evaluate.py`。

```bash
python tournament/run.py --formal --output rerun/对战      # 冻结赛程，8930 局；4 并发约需数小时，可用 --workers 调整
```

`schedule-2to10.json.gz` 是正式赛的冻结赛程（地图种子、座位顺序），`--formal` 会自动解压使用。`evaluate.py` 会先核对选手包文件的哈希（`workspace/MANIFEST.json`），选手包里的文件被改动时会拒绝运行。

部分 bot 按时间控制搜索深度，所以逐局结果不保证与 `results/games.json.gz` 完全一致，但总排名应当一致。

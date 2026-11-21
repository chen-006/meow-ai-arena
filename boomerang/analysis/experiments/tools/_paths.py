"""实验脚本共用的路径：用仓库里的选手包（workspace）和 6 张海选图，不依赖任何本机目录。
需要先在 boomerang/ 下运行一次 `python build_engine.py` 编出 workspace/arena.exe 和陪练。"""
import sys
from pathlib import Path

BOOMERANG = Path(__file__).resolve().parents[3]
WORKSPACE = BOOMERANG / 'workspace'
HIDDEN = BOOMERANG / 'tournament' / 'hidden'
sys.path.insert(0, str(WORKSPACE))          # import match, build
MAPS = {m: WORKSPACE / 'maps' / f'{m}.txt' for m in ('twinlakes', 'yinyang')}
MAPS.update({m: HIDDEN / f'{m}.txt' for m in ('icefloes', 'ruins', 'skybridge', 'stonecircle')})

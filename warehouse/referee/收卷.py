"""手动收卷：把某队主程比赛文件夹里的 workspace/src 复制到 赛事/主程提交/<队>/src。

  python 裁判/收卷.py 队E             # 手动操作的工具（ZCode）结束后用
  python 裁判/收卷.py 队F             # 自动收卷没找到 src 时补收

会在比赛文件夹里自动寻找 workspace/src（模型可能解压到别的子目录），取最近改动过的那个。
"""
import json
import os
import shutil
import sys
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from 赛事工具 import EVENT, contest_folder, teams  # noqa: E402
from 开跑 import find_src  # noqa: E402

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')

team = sys.argv[1] if len(sys.argv) > 1 else sys.exit(__doc__)
t = next(x for x in teams() if x['team'] == team)
folder = contest_folder(team, '主程', t['主程'])
src = find_src(folder)
if not src:
    sys.exit('没有在 %s 里找到 workspace/src' % folder)
dst = os.path.join(EVENT, '主程提交', team, 'src')
shutil.rmtree(dst, ignore_errors=True)
shutil.copytree(src, dst, ignore=shutil.ignore_patterns('build', '*.exe', '*.o'))
rec = os.path.join(EVENT, '运行记录', '%s_主程' % team)
os.makedirs(rec, exist_ok=True)
mp = os.path.join(rec, '元数据.json')
meta = json.load(open(mp, encoding='utf-8')) if os.path.exists(mp) else {'队伍': team, '角色': '主程', '模型': t['主程']}
meta.update({'已收卷': dst, '收卷来源': src, '有交接文档': os.path.exists(os.path.join(dst, 'HANDOFF.md')),
             '手动收卷时间': datetime.now().isoformat(timespec='seconds')})
json.dump(meta, open(mp, 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
print('已收卷：%s → %s（交接文档：%s）' % (src, dst, '有' if meta['有交接文档'] else '无'))

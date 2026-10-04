"""测试用脚本选手：读简报，全部驻守；撤退就解散；冬季不增兵，必要时按顺序裁军。只用于验证流程。"""
import re
import subprocess
import sys

brief = sys.stdin.read()


def game(*args):
    return subprocess.run([sys.executable, 'game.py', *args], capture_output=True, text=True, encoding='utf-8').stdout


if '【本阶段你需要】' not in brief:
    sys.exit(0)
brief = brief[brief.index('【本阶段你需要】'):]  # 只看任务部分：开局消息里的规则原文也提到"裁军"
options = {}
for line in game('legal').splitlines():
    if '：' in line:
        loc, rest = line.split('：', 1)
        options[loc.strip()] = [o.strip() for o in rest.split('|')]
orders = []
if '必须裁军' in brief:
    need = int(re.search(r'必须裁军 (\d+) 支', brief).group(1))
    orders = [o for opts in options.values() for o in opts if o.endswith(' D')][:need]
elif '最多增兵' in brief:
    orders = []
else:
    for loc, opts in options.items():
        pick = next((o for o in opts if o.endswith(' H')), None) or next((o for o in opts if o.endswith(' D')), opts[0])
        orders.append(pick)
print(game('orders', *orders))

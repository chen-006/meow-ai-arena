"""演练用的假队员（不调用模型、不联网）：从标准输入读任务书，在当前目录（他自己的文件夹）里按任务书里的【假】指令动手，
输出一行和 Claude 命令行一样的 JSON（session_id、result、is_error）。

  【假】改参数      在 player.cpp 末尾加一行注释，再 python build.py player.cpp（会触发编译即共享）
  【假】慢活 N      先睡 N 秒再回（用来测"队员正忙"）
  【假】编坏        把 player.cpp 写成编译不过的样子
"""
import json
import re
import subprocess
import sys
import time
import uuid
from pathlib import Path

sys.stdout.reconfigure(encoding='utf-8')
args = sys.argv[1:]
sid = args[args.index('--resume') + 1] if '--resume' in args else 'fake-' + uuid.uuid4().hex[:8]
prompt = sys.stdin.buffer.read().decode('utf-8', errors='replace')
d = Path.cwd()
kind = '商量' if '布阵决策时间' in prompt else '开发'
if '【主办方】比赛已经结束' in prompt:
    kind = '复盘'
task = re.split(r'下面是队长给你的任务。|下面是队长的问题。', prompt)[-1]
notices = [x.strip() for x in task.splitlines() if '亲手改了你的' in x]
task = '\n'.join(x for x in task.splitlines() if '亲手改了你的' not in x).strip()
acts = []
if '【假】慢活' in task:
    n = int(re.search(r'【假】慢活\s*(\d+)', task).group(1))
    time.sleep(n)
    acts.append(f'干了 {n} 秒')
if '【假】改参数' in task:
    p = d / 'player.cpp'
    p.write_text(p.read_text(encoding='utf-8') + f'int {d.name}_param_{int(time.time() * 1000)} = 3;  // 按队长要求改的参数\n', encoding='utf-8')
    r = subprocess.run([sys.executable, 'build.py', 'player.cpp'], cwd=d, capture_output=True, text=True, encoding='utf-8', errors='replace')
    acts.append('编译：' + (r.stdout + r.stderr).strip().replace('\n', ' / '))
if '【假】编坏' in task:
    (d / 'player.cpp').write_text('int main( { // 故意编不过\n', encoding='utf-8')
    acts.append('把 player.cpp 写坏了')
reply = (f'（假队员 {d.name}，会话 {sid}）收到{kind}任务：{task[:80]}'
         + (f'\n看到主办方提醒：{"；".join(notices)}' if notices else '\n没有亲手改提醒')
         + (f'\n做了：{"；".join(acts)}' if acts else ''))
print(json.dumps(dict(type='result', session_id=sid, result=reply, is_error=False), ensure_ascii=False))

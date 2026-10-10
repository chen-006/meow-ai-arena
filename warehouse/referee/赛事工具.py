"""赛事脚本共用的路径和小工具。"""
import json
import os
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
EVENT = os.environ.get('A02_EVENT') or os.path.join(ROOT, '赛事')   # 彩排时用环境变量 A02_EVENT 指到别的目录
WORKSPACE = os.path.join(ROOT, 'workspace')
# 裁判的编译产物放在本题的 裁判/build/，不能放在选手能看到的比赛目录（C:/arena5、C:/bench）下：参考实现编译出的程序就是标准答案。
# 本机需要给"测试"文件夹加 ASR 排除项，否则新编译的程序会被拦截。
BUILD = os.path.join(HERE, 'build')
SKIP = {'build', '__pycache__'}
# 本期专用的比赛根目录：只放本期选手的文件夹（不用 C:/bench，那里有前几期几十个文件夹）
ARENA = 'C:\\arena5'


CLOCK_ROUNDS = {
    1: ('.clock_start.json', '当前的 src/', '确认输出正确、写好 src/HANDOFF.md'),
    2: ('.clock_start_round2.json', '当前的 cases/', '用 python tools/try_case.py --all 确认结果'),
    3: ('.clock_start_round3.json', '当前的 workspace/src/', '确认 python tools/bench2.py 全部正确'),
}


def clock_for(rnd):
    """生成第 rnd 轮用的 clock.py。第 1 轮用 workspace/clock.py 原样。
    第 2、3 轮（重赛版）用 裁判/clock_重赛模板.py：解压即开赛，只有 45 分钟上限，没有"建议 30 分钟"。"""
    marker, submit, finish = CLOCK_ROUNDS[rnd]
    if rnd == 1:
        return open(os.path.join(WORKSPACE, 'clock.py'), encoding='utf-8').read()
    s = open(os.path.join(HERE, 'clock_重赛模板.py'), encoding='utf-8').read()
    for old, new in (('__MARKER__', marker), ('__SUBMIT__', submit), ('__FINISH__', finish)):
        assert old in s, '模板里找不到：' + old
        s = s.replace(old, new)
    return s


def write_clock_start(folder, rnd, start=None):
    """在比赛文件夹里写本轮开赛时间，返回开赛的 unix 时间。开跑脚本、开局脚本都用它。
    start：补写时用（选手已经先开工了），给实际开工的 unix 时间。"""
    import time
    from datetime import datetime
    start = start or time.time()
    with open(os.path.join(folder, CLOCK_ROUNDS[rnd][0]), 'w', encoding='utf-8') as f:
        json.dump({'start_unix': start, 'start_local': datetime.fromtimestamp(start).isoformat(timespec='seconds'),
                   'round': rnd}, f)
    return start


FOLDER_ROLE = {'攻手': '测试手'}
# 2026-10-09 第 2、3 轮重赛：换新文件夹名，和第一次比赛的会话记录、工具的项目目录分开
ROUND_LABEL = {1: '第1轮', 2: '第2轮重赛', 3: '第3轮重赛'}


def contest_folder(team, role, model, rnd=1):
    """选手比赛文件夹。名字是全新的，按路径记录历史的工具（反重力、Kimi、OpenCode、dsh、ZCode）都会当成新项目。"""
    # 文件夹名选手看得见：攻手显示成"测试手"（"攻手""攻防"之类的字眼会被 Claude 的安全机制当成网络攻击任务）
    return os.path.join(ARENA, '%s %s %s %s' % (ROUND_LABEL[rnd], team, FOLDER_ROLE.get(role, role), model))


def teams():
    with open(os.path.join(EVENT, '抽签结果.json'), encoding='utf-8') as f:
        return json.load(f)['teams']


def substitutes():
    """赛中替补（赛事/替补.json）：[{team, 角色, 轮次, 原选手, 替补, 原因}]。没有文件时为空。"""
    p = os.path.join(EVENT, '替补.json')
    if not os.path.exists(p):
        return []
    with open(p, encoding='utf-8') as f:
        return json.load(f)['替补']


def substitute(team, role, rnd):
    """某队某角色在第 rnd 轮的替补记录，没有替补时为 None。"""
    return next((s for s in substitutes() if s['team'] == team and s['角色'] == role and s['轮次'] == rnd), None)


def player(t, role, rnd):
    """第 rnd 轮实际出场的模型：有替补用替补，否则用抽签名单。t 是 teams() 里的一项。"""
    s = substitute(t['team'], role, rnd)
    return s['替补'] if s else t[role]


def lead_src(team):
    """主程提交：赛事/主程提交/<队>/src（也接受直接放在 <队>/ 下）。"""
    d = os.path.join(EVENT, '主程提交', team)
    return os.path.join(d, 'src') if os.path.isdir(os.path.join(d, 'src')) else d


def add_dir(z, src, arc):
    for d, dirs, files in os.walk(src):
        dirs[:] = [x for x in dirs if x not in SKIP]
        for f in files:
            full = os.path.join(d, f)
            z.write(full, (arc + '/' + os.path.relpath(full, src)).replace(os.sep, '/'))


def add_text(z, arc, text):
    z.writestr(arc, text)


def zip_open(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    return zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED)

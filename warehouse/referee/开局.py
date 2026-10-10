"""手动席位的开局、计时和封存（第 1 轮的手动主程，以及第 2、3 轮的攻手）。和 开跑.py 用同一套文件夹、开赛时间和收卷规则。

  python 裁判/开局.py 队E 准备 [--round N]   # 在 C:/arena5 建本轮文件夹，放本轮的压缩包和填好的 提示词.txt（还不开始计时）
  python 裁判/开局.py 队E 开始 [--round N]   # 你粘贴提示词的同一时刻运行：写本轮开赛时间，开始 45 分钟倒计时；
                                               # 到点自动封存本轮提交物，并记录
  python 裁判/开局.py 队E 收卷 [--round N]   # 模型提前做完时运行：立即收卷（记哈希，1 分钟后复核）

各轮（--round 默认 1）：
  1  主程 · 优化     文件夹 第1轮 <队> 主程 <模型>   发 workspace.zip          收 workspace/src → 赛事/主程提交/<队>/src
  2  攻手 · 互测     文件夹 第2轮 <队> 测试手 <模型> 发 互测包_<队>.zip        收 review/cases  → 赛事/击穿提交/<队>/hacks
  3  攻手 · 接手     文件夹 第3轮 <队> 测试手 <模型>   发 接手包_<队>.zip        收 workspace/src → 赛事/接手提交/<队>/src
第 3 轮是新会话、新文件夹：里面只有本队主程的代码，没有第 2 轮看过的别队代码。

"开始"会一直运行到 45 分钟（或被"收卷"提前结束），请放在后台运行。
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import time
from datetime import datetime

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from 赛事工具 import CLOCK_ROUNDS, EVENT, ROOT, contest_folder, player, substitute, teams, write_clock_start  # noqa: E402

if hasattr(sys.stdout, 'reconfigure'):
    sys.stdout.reconfigure(encoding='utf-8')
HARD_LIMIT = 45 * 60
# 子代理、联网、求助其他模型：只靠提示词约束（用户决定，不在工具层面禁用）。

# 轮次 -> (角色, 压缩包路径模板, 提交物所在的 (父目录名, 目录名), 收卷目标模板, 运行记录目录模板)
ROUNDS = {
    1: ('主程', os.path.join(ROOT, 'workspace.zip'), ('workspace', 'src'),
        os.path.join(EVENT, '主程提交', '{队}', 'src'), '{队}_主程'),
    2: ('攻手', os.path.join(EVENT, '互测包', '互测包_{队}.zip'), ('review', 'cases'),
        os.path.join(EVENT, '击穿提交', '{队}', 'hacks'), '{队}_攻手_第2轮'),
    3: ('攻手', os.path.join(EVENT, '接手包', '接手包_{队}.zip'), ('workspace', 'src'),
        os.path.join(EVENT, '接手提交', '{队}', 'src'), '{队}_攻手_第3轮'),
}


def find_dir(folder, parent, name):
    """模型不一定解压到比赛文件夹的第一层（比如 MiMo 解压到了 workspace_unzipped/workspace/）。
    在比赛文件夹里找所有 <parent>/<name>，取最近改动过的那个。"""
    found = []
    for d, dirs, _ in os.walk(folder):
        dirs[:] = [x for x in dirs if x not in ('build', '.git', 'node_modules')]
        if os.path.basename(d) == name and os.path.basename(os.path.dirname(d)) == parent:
            mts = [os.path.getmtime(os.path.join(r, f)) for r, _, fs in os.walk(d) for f in fs]
            found.append((max(mts or [os.path.getmtime(d)]), d))
        if d.count(os.sep) - folder.count(os.sep) >= 3:
            dirs[:] = []
    return max(found)[1] if found else None


def manifest(folder):
    """每个文件的 sha256、大小、最后修改时间。"""
    out = {}
    for d, dirs, files in os.walk(folder):
        dirs[:] = [x for x in dirs if x not in ('build', '__pycache__')]
        for f in sorted(files):
            p = os.path.join(d, f)
            out[os.path.relpath(p, folder).replace(os.sep, '/')] = {
                'sha256': hashlib.sha256(open(p, 'rb').read()).hexdigest(), 'bytes': os.path.getsize(p),
                'mtime': datetime.fromtimestamp(os.path.getmtime(p)).isoformat(timespec='seconds')}
    return out


def collect(team, folder, rec_dir, reason, rnd=1):
    """收卷：复制本轮提交物到 赛事/…，记哈希；1 分钟后再算一次，确认没有后台进程还在改。"""
    _, _, (parent, name), dst_tpl, _ = ROUNDS[rnd]
    src = find_dir(folder, parent, name)
    meta_p = os.path.join(rec_dir, '元数据.json')
    meta = json.load(open(meta_p, encoding='utf-8')) if os.path.exists(meta_p) else {}
    if not src:
        meta['收卷'] = '没有找到 %s/%s' % (parent, name)
        json.dump(meta, open(meta_p, 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
        print('没有找到 %s/%s' % (parent, name))
        return
    dst = dst_tpl.format(队=team)
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns('build', '*.exe', '*.o', '__pycache__'))
    m1 = manifest(dst)
    meta.update({'收卷原因': reason, '收卷时间': datetime.now().isoformat(timespec='seconds'), '收卷来源': src,
                 '已收卷': dst, '收卷清单': m1})
    if name == 'src':
        meta['有交接文档'] = os.path.exists(os.path.join(dst, 'HANDOFF.md'))
    json.dump(meta, open(meta_p, 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
    print('已收卷 %d 个文件 → %s%s；1 分钟后复核…' % (
        len(m1), dst, '（交接文档：%s）' % ('有' if meta['有交接文档'] else '无') if name == 'src' else ''), flush=True)
    time.sleep(60)
    later = manifest(src)
    changed = sorted(k for k in set(m1) | set(later) if m1.get(k, {}).get('sha256') != later.get(k, {}).get('sha256'))
    meta['收卷后1分钟仍有改动'] = changed
    json.dump(meta, open(meta_p, 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
    print('复核：' + ('收卷后又被改动的文件 %s —— 可能有后台进程还在跑！' % changed if changed else '收卷后没有再被改动'))


def template(name):
    with open(os.path.join(ROOT, '提示词', name), encoding='utf-8') as f:
        return f.read().split('---\n')[1].strip('\n')


def broken_text(team):
    """第 3 轮提示词里的一句话：本队主程被几家击穿。"""
    p = os.path.join(EVENT, '击穿结果', '矩阵.json')
    if not os.path.exists(p):
        sys.exit('还没有击穿结果，请先运行 python 裁判/击穿判定.py')
    m = json.load(open(p, encoding='utf-8'))['matrix']
    who = {t['team']: t['攻手'] for t in teams()}
    by = [a for a in m if team in m[a] and m[a][team]['result'] == '击穿']
    if not by:
        return '上一轮没有人找到你队友代码的反例。'
    return '上一轮有 %d 位测试手找到了你队友代码的反例（%s），这些用例在接手包的 workloads2/regression/ 里。' % (
        len(by), '、'.join('%s 的测试手 %s' % (a, who[a]) for a in by))


def prompt(team, rnd):
    t = next(x for x in teams() if x['team'] == team)
    if rnd == 1:
        from 开跑 import prompt_for
        return prompt_for(team)
    if rnd == 2:
        # 重赛（2026-10-09）起 6 家同一版
        return template('2_攻手_攻防.md').replace('{队伍}', team).replace('{队友}', t['主程'])
    return (template('3_攻手_接手.md').replace('{队伍}', team).replace('{队友}', t['主程'])
            .replace('{被击穿情况}', broken_text(team)))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('team')
    ap.add_argument('action', choices=['准备', '开始', '收卷', '自动开赛'])
    ap.add_argument('--round', type=int, choices=[1, 2, 3], default=1)
    ap.add_argument('--forced', action='store_true', help='收卷时按"45 分钟到点封存"记录（给 bash 计时器用）')
    ap.add_argument('--start', help='补写开赛时间（选手已先开工）：今天的 HH:MM:SS')
    ap.add_argument('--model', help='临时指定出场模型（一般不用：赛中替补写在 赛事/替补.json 里，会自动生效）')
    args = ap.parse_args()
    rnd = args.round
    t = next((x for x in teams() if x['team'] == args.team), None) or sys.exit('没有这支队伍')
    role, pack_tpl, _, _, rec_tpl = ROUNDS[rnd]
    model = args.model or player(t, role, rnd)
    sub = substitute(args.team, role, rnd)
    folder = contest_folder(args.team, role, model, rnd)
    rec_dir = os.path.join(EVENT, '运行记录', rec_tpl.format(队=args.team))
    flag = '' if rnd == 1 else ' --round %d' % rnd

    if args.action == '准备':
        pack = pack_tpl.format(队=args.team)
        if not os.path.exists(pack):
            sys.exit('没有本轮的压缩包：%s（第 2 轮先运行 生成攻防包.py，第 3 轮先运行 击穿判定.py 和 生成接手包.py）' % pack)
        if os.path.exists(folder) and os.listdir(folder):
            sys.exit('文件夹已存在且不为空：%s' % folder)
        text = prompt(args.team, rnd)
        os.makedirs(folder, exist_ok=True)
        shutil.copy(pack, folder)
        with open(os.path.join(folder, '提示词.txt'), 'w', encoding='utf-8') as f:
            f.write(text)
        if args.model:
            flag += ' --model "%s"' % args.model
        print('已准备：%s（%s + 提示词.txt）。粘贴提示词的同时运行：python 裁判/开局.py %s 开始%s' % (
            folder, os.path.basename(pack), args.team, flag))
        return

    if args.action == '自动开赛':
        # 给 裁判/自动计时.sh 轮询用：选手一解压（比赛文件夹里出现 review/ 或 workspace/），就把那个文件夹的创建时间
        # 记为开赛时间，写开赛时间文件和元数据，打印 "START HH:MM:SS"。还没解压、或已经记过，什么都不打印。不常驻。
        marker = os.path.join(folder, CLOCK_ROUNDS[rnd][0])
        if not os.path.isdir(folder) or os.path.exists(marker):
            return
        parent = ROUNDS[rnd][2][0]
        found = []
        for d, dirs, _ in os.walk(folder):
            if d.count(os.sep) - folder.count(os.sep) >= 3:
                dirs[:] = []
            if os.path.basename(d) == parent:
                st = os.stat(d)
                found.append(getattr(st, 'st_birthtime', None) or st.st_ctime)
        if not found:
            return
        os.makedirs(rec_dir, exist_ok=True)
        start = write_clock_start(folder, rnd, min(found))
        meta = {'队伍': args.team, '轮次': rnd, '角色': role, '模型': model, '工具': '手动（见 赛事/工具与key配置.md）',
                '文件夹': folder, '开始': datetime.fromtimestamp(start).isoformat(timespec='seconds'), '强制停止': False,
                '开始依据': '选手解压比赛包的时刻（%s/ 文件夹的创建时间），由 自动计时.sh 记录' % parent}
        if model != t[role]:
            meta['替补'] = '%s 代替 %s（%s）' % (model, t[role], sub['原因'] if sub else '临时指定')
        json.dump(meta, open(os.path.join(rec_dir, '元数据.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
        stop_flag = os.path.join(rec_dir, '.已提前收卷')
        if os.path.exists(stop_flag):
            os.remove(stop_flag)
        print('START ' + datetime.fromtimestamp(start).strftime('%H:%M:%S'))
        return

    if args.action == '开始':
        if not os.path.isdir(folder):
            sys.exit('请先运行"准备"')
        os.makedirs(rec_dir, exist_ok=True)
        given = None
        if args.start:
            given = datetime.combine(datetime.now().date(), datetime.strptime(args.start, '%H:%M:%S').time()).timestamp()
        start = write_clock_start(folder, rnd, given)
        meta = {'队伍': args.team, '轮次': rnd, '角色': role, '模型': model, '工具': '手动（见 赛事/工具与key配置.md）',
                '文件夹': folder, '开始': datetime.fromtimestamp(start).isoformat(timespec='seconds'), '强制停止': False}
        if model != t[role]:
            meta['替补'] = '%s 代替 %s（%s）' % (model, t[role], sub['原因'] if sub else '临时指定')
        json.dump(meta, open(os.path.join(rec_dir, '元数据.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
        stop_flag = os.path.join(rec_dir, '.已提前收卷')
        if os.path.exists(stop_flag):
            os.remove(stop_flag)
        print('第 %d 轮 %s 开赛 %s，%d 分钟后封存。' % (rnd, args.team, meta['开始'], HARD_LIMIT // 60), flush=True)
        while time.time() - start < HARD_LIMIT:
            if os.path.exists(stop_flag):
                print('已提前收卷，计时结束。')
                return
            time.sleep(5)
        meta = json.load(open(os.path.join(rec_dir, '元数据.json'), encoding='utf-8'))
        meta.update({'强制停止': True, '结束': datetime.now().isoformat(timespec='seconds'), '用时分钟': 45.0})
        json.dump(meta, open(os.path.join(rec_dir, '元数据.json'), 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
        print('！！！%s 第 %d 轮已到 45 分钟：请立即在工具里停止模型。现在封存提交物。' % (args.team, rnd), flush=True)
        collect(args.team, folder, rec_dir, '45 分钟到点封存', rnd)
        return

    if args.action == '收卷':
        os.makedirs(rec_dir, exist_ok=True)
        open(os.path.join(rec_dir, '.已提前收卷'), 'w').close()
        mp = os.path.join(rec_dir, '元数据.json')
        meta = json.load(open(mp, encoding='utf-8')) if os.path.exists(mp) else {}
        if meta.get('开始'):
            used = (time.time() - datetime.fromisoformat(meta['开始']).timestamp()) / 60
            meta.update({'结束': datetime.now().isoformat(timespec='seconds'), '用时分钟': 45.0 if args.forced else round(used, 1)})
            if args.forced:
                meta['强制停止'] = True
            json.dump(meta, open(mp, 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
        collect(args.team, folder, rec_dir, '45 分钟到点封存' if args.forced else '模型自己结束后收卷', rnd)


if __name__ == '__main__':
    main()

"""第 2 轮结束后：正式判定所有击穿用例，生成 6×5 击穿矩阵和被击穿用例清单。

  python 裁判/击穿判定.py

输入：赛事/击穿提交/<攻手所在队>/hacks/<目标队>/*.in（把攻手文件夹里的 attack/hacks 整个复制过来）
      赛事/主程提交/<队>/src
输出：赛事/击穿结果/矩阵.json、矩阵.md
      赛事/击穿结果/被击穿用例/<目标队>/from<攻手队字母>_<文件名>.in（只放成功击穿的用例，接手包会用到）

规则：每个目标最多取按文件名排序的前 3 个用例；用例必须通过 check_input.py（击穿上限）；
基线必须在 10 秒内跑完；目标输出不一致、退出码非 0、运行超过 60 秒或编译失败 → 击穿。

超时类反例的复核（重赛起，见 赛事/重赛记录.md）：要在机器空闲时判定（不要和比赛同时跑）。
先给 100 秒：到 100 秒还没结束，直接成立（超过限时的 1.6 倍，不会是抖动）；
60～100 秒之间结束的，再跑 2 次、限时 60 秒，3 次都超过 60 秒才成立，否则算守住。
为了省时间同时跑 3 个用例（每个程序都是单线程）。每个用例的墙钟用时记在矩阵里。
"""
import json
import os
import shutil
import sys
from concurrent.futures import ThreadPoolExecutor

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'workspace', 'tools'))
from 赛事工具 import BUILD, EVENT, WORKSPACE, lead_src, teams  # noqa: E402
import check_input  # noqa: E402
import measure  # noqa: E402

BASE_LIMIT, TARGET_LIMIT, MAX_CASES = 10, 60, 3
SURE_LIMIT, WORKERS = 100, 3


def judge_one(base, target_exe, path, rec):
    b = measure.run(base, path, timeout=BASE_LIMIT)
    rec['基线秒'] = round(b['wall'], 2)
    if b['timeout'] or b['code'] != 0:
        rec.update(verdict='无效', why='基线没能在 %d 秒内跑完' % BASE_LIMIT)
        return rec
    if target_exe is None:
        rec.update(verdict='击穿', why='目标编译失败')
        return rec
    r = measure.run(target_exe, path, timeout=SURE_LIMIT)
    rec['目标秒'] = round(r['wall'], 2)
    if r['timeout']:
        rec.update(verdict='击穿', why='超时：%d 秒仍未结束（限时 %d 秒，基线 %.1f 秒）' % (SURE_LIMIT, TARGET_LIMIT, b['wall']), kind='超时')
    elif r['code'] != 0:
        rec.update(verdict='击穿', why='退出码 %d' % r['code'])
    elif r['digest'] != b['digest']:
        la = measure.normalized(b['output']).split(b'\n')
        lb = measure.normalized(r['output']).split(b'\n')
        i = next((i for i in range(max(len(la), len(lb))) if la[i:i + 1] != lb[i:i + 1]), 0)
        rec.update(verdict='击穿', why='第 %d 行不同：基线 %s / 目标 %s' % (
            i + 1, la[i:i + 1][0][:80].decode(errors='replace') if la[i:i + 1] else '<EOF>',
            lb[i:i + 1][0][:80].decode(errors='replace') if lb[i:i + 1] else '<EOF>'))
    elif r['wall'] > TARGET_LIMIT:
        walls = [r['wall']]
        for _ in range(2):
            x = measure.run(target_exe, path, timeout=TARGET_LIMIT)
            walls.append(TARGET_LIMIT + 1 if x['timeout'] else x['wall'])
        rec['复核秒'] = [round(w, 1) for w in walls]
        if all(w > TARGET_LIMIT for w in walls):
            rec.update(verdict='击穿', why='超时：3 次都超过 %d 秒（第 1 次 %.1f 秒，基线 %.1f 秒）' % (TARGET_LIMIT, walls[0], b['wall']), kind='超时')
        else:
            rec.update(verdict='守住', why='输出一致；用时在 %d 秒上下（%s），3 次里有不超时的，不算' % (TARGET_LIMIT, ' / '.join('%.1f' % w for w in walls)))
    else:
        rec.update(verdict='守住', why='一致（%.1f 秒，基线 %.1f 秒）' % (r['wall'], b['wall']))
    return rec


def main():
    ts = teams()
    bdir = os.path.join(BUILD, 'hack')
    base = measure.build(os.path.join(WORKSPACE, 'baseline'), os.path.join(bdir, 'baseline' + measure.EXE))
    exes = {}
    for t in ts:
        try:
            exes[t['team']] = measure.build(lead_src(t['team']), os.path.join(bdir, 'lead_%s' % t['team'] + measure.EXE))
        except RuntimeError as e:
            exes[t['team']] = None
            print('%s 的主程代码编译失败：%s' % (t['team'], str(e)[:300]))
    out_dir = os.path.join(EVENT, '击穿结果')
    broken_dir = os.path.join(out_dir, '被击穿用例')
    shutil.rmtree(broken_dir, ignore_errors=True)
    # 先把所有合法用例判完（同时跑 WORKERS 个），下面按队整理
    todo = []
    for t in ts:
        for target in t['攻击对象']:
            hdir = os.path.join(EVENT, '击穿提交', t['team'], 'hacks', target)
            for f in (sorted(x for x in os.listdir(hdir) if x.endswith('.in'))[:MAX_CASES] if os.path.isdir(hdir) else []):
                path = os.path.join(hdir, f)
                try:
                    with open(path, 'rb') as fh:
                        check_input.check(fh.read().decode('ascii'))
                except (UnicodeDecodeError, check_input.Bad):
                    continue
                todo.append((target, path, f))
    with ThreadPoolExecutor(WORKERS) as ex:
        pre = dict(zip([x[1] for x in todo],
                       ex.map(lambda x: judge_one(base, exes[x[0]], x[1], {'file': x[2]}), todo)))
    matrix = {}   # 攻手队 -> 目标队 -> {'result': 击穿/守住/未提交, 'cases': [...]}
    for t in ts:
        a = t['team']
        matrix[a] = {}
        for target in t['攻击对象']:
            hdir = os.path.join(EVENT, '击穿提交', a, 'hacks', target)
            files = sorted(f for f in os.listdir(hdir) if f.endswith('.in'))[:MAX_CASES] if os.path.isdir(hdir) else []
            cell = {'result': '未提交' if not files else '守住', 'cases': []}
            for f in files:
                path = os.path.join(hdir, f)
                rec = {'file': f}
                try:
                    with open(path, 'rb') as fh:
                        check_input.check(fh.read().decode('ascii'))
                except (UnicodeDecodeError, check_input.Bad) as e:
                    rec.update(verdict='无效', why='不合法：%s' % e)
                    cell['cases'].append(rec)
                    continue
                rec = pre[path]
                if rec['verdict'] == '击穿':
                    cell['result'] = '击穿'
                    os.makedirs(os.path.join(broken_dir, target), exist_ok=True)
                    shutil.copy(path, os.path.join(broken_dir, target, 'from%s_%s' % (a[-1], f)))   # 文件名只用 ASCII，避免终端乱码
                cell['cases'].append(rec)
                print('[%s → %s] %-20s %s  %s' % (a, target, f, rec['verdict'], rec['why']), flush=True)
            matrix[a][target] = cell

    names = [t['team'] for t in ts]
    who = {t['team']: t for t in ts}
    attack = {a: sum(1 for c in matrix[a].values() if c['result'] == '击穿') for a in names}
    hit = {d: sum(1 for a in names if a != d and matrix[a][d]['result'] == '击穿') for d in names}
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, '矩阵.json'), 'w', encoding='utf-8') as f:
        json.dump({'matrix': matrix, 'attack': attack, 'broken_by': hit}, f, ensure_ascii=False, indent=2)
    L = ['# 击穿矩阵', '', '行是攻手（所在队），列是被攻击的主程（所在队）。✖ = 击穿，○ = 守住，— = 未提交。', '',
         '| 攻手 → 主程 | ' + ' | '.join('%s %s' % (d, who[d]['主程']) for d in names) + ' | 拿下 |',
         '|---|' + '---|' * (len(names) + 1)]
    sym = {'击穿': '✖', '守住': '○', '未提交': '—'}
    for a in names:
        cells = ['' if a == d else sym[matrix[a][d]['result']] for d in names]
        L.append('| %s %s | %s | %d |' % (a, who[a]['攻手'], ' | '.join(cells), attack[a]))
    L.append('| 被击穿次数 | ' + ' | '.join(str(hit[d]) for d in names) + ' | |')
    with open(os.path.join(out_dir, '矩阵.md'), 'w', encoding='utf-8') as f:
        f.write('\n'.join(L) + '\n')
    print('\n'.join(L))


if __name__ == '__main__':
    main()

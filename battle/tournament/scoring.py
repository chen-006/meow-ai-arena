"""《纸上谈兵》评测和算分：host.py 的"评测""出榜"两个阶段。

评测：逐人检查七份布阵（war.exe check）、用选手包 build.py 里同一条命令编译指挥程序，
然后用 war.exe batch 并行跑五关、1v1 全部对局、四人混战全部对局，每场存回放和结果（eval\\results\\<场>.json）。
中断后重跑只补没有结果的场次。

算分口径（选手包 RULES.md 第 2 节）：
  关卡分：没过 0（布阵不合法或没交也是 0，不开打）；过关的人里造价最低的 100，
          其余 max(保底, 100 − 每贵一倍扣 × log2(造价 ÷ 全场最低造价))，默认每贵一倍扣 25、保底 20（配置 level_score）；
          "全场最低"只在参赛选手里取，主办方参考造价（levels.<关>.reference_cost）不参与算分，只在闯关榜上列作参考线
  闯关分 = 各关平均
  1v1：每对 2 张图 × 换座位 = 4 场，胜 1 平 0.5 负 0；1v1 分 = 平均 × 100
  混战：任取 4 人的每种组合，固定第一位坐南边，另三位轮遍其余三个座位（6 场）；每场得分 = 名次分 + 歼敌分（引擎给出）；混战分 = 平均 ÷ 6 × 100
  对抗分 = 1v1 × 60% + 混战 × 40%；总分 = 闯关 × 60% + 对抗 × 40%（权重从配置 weights 读）
  总榜同分：依次比过关数、对抗分、过了的关的造价之和（少的在前）；闯关榜同分比过关数。

过没过只看引擎结果里的 passed（引擎按关卡文件 objectives 判），这里不自己判过关条件。
引擎错误（war.exe batch 回 {"error":"engine_timeout"}，一场超过墙上时钟上限）：结果照样存进 results\（带 error 字段），
记进 eval\引擎错误场次.json，不算"没跑成"、重跑评测也不会自动重跑它（要重跑用 评测 --重跑）。
出榜时：闯关那一格记"引擎错误"、暂按 0 分；1v1、混战的这一场不计入平均（不当输也不当赢）；
各榜和赛事摘要的"需要看一眼的"都列出这些场次，由主办方决定怎么处理。
"""
import collections
import concurrent.futures as futures
import datetime
import importlib.util
import itertools
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import uuid


def read(p, default=None):
    p = Path(p)
    return json.loads(p.read_text(encoding='utf-8-sig')) if p.exists() else default


def write_json(p, obj):
    p = Path(p)
    p.parent.mkdir(parents=True, exist_ok=True)
    tmp = p.with_name(p.name + '.' + uuid.uuid4().hex[:8] + '.tmp')
    tmp.write_text(json.dumps(obj, ensure_ascii=False, indent=1), encoding='utf-8')
    os.replace(tmp, p)


def level_ids(c):
    return sorted(int(k) for k in c['levels'])


def scenario_files(c):
    kit = Path(c['root']) / 'kit'
    levels = {}
    for n in level_ids(c):
        found = sorted((kit / 'levels').glob(f'第{n}关_*.json'))
        if len(found) != 1:
            raise RuntimeError(f'选手包 levels\\ 里第 {n} 关的文件应正好一个，找到 {len(found)} 个')
        levels[n] = found[0]
    duel = [kit / 'maps' / f'{m}.json' for m in c['duel_maps']]
    melee = kit / 'maps' / f"{c['melee_map']}.json"
    for f in duel + [melee]:
        if not f.is_file():
            raise RuntimeError('选手包里没有对抗图 ' + f.name)
    return levels, duel, melee


# ---------------------------------------------------------------- 检查和编译
def war_check(war, scenario, deploy):
    p = subprocess.run([str(war), 'check', str(scenario), str(deploy), '--seat', '0'], capture_output=True)
    txt = p.stdout.decode('utf-8', 'replace').strip()
    try:
        return json.loads(txt.splitlines()[-1])
    except (ValueError, IndexError):
        return dict(ok=False, errors=[(txt or p.stderr.decode('utf-8', 'replace')).strip()[:300]])


def load_build(kit):
    spec = importlib.util.spec_from_file_location('a12_pack_build', kit / 'build.py')
    b = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(b)
    return b


def compile_commander(b, src, outdir):
    """和选手包 build.py 的 build() 完全同一条命令：[g++] + FLAGS + ['-I', commander\\, 源码, '-o', 输出]。"""
    outdir.mkdir(parents=True, exist_ok=True)
    if not src.is_file():
        return dict(state='未交', exe=None)
    local = outdir / 'commander.cpp'
    local.write_bytes(src.read_bytes())
    cxx = b.find_compiler()
    if not cxx:
        raise RuntimeError('找不到 g++（和选手包 build.py 同样的找法）')
    exe, tmp = outdir / 'commander.exe', outdir / 'commander.tmp.exe'
    cmd = [cxx] + list(b.FLAGS) + ['-I', str(b.HEADER.parent), str(local), '-o', str(tmp)]
    p = subprocess.run(cmd, capture_output=True, cwd=str(outdir))
    out = (p.stdout + p.stderr).decode('utf-8', 'replace').strip()
    (outdir / '编译输出.txt').write_text(out + '\n', encoding='utf-8')
    if p.returncode != 0 or not tmp.is_file():
        tmp.unlink(missing_ok=True)
        return dict(state='编译失败', exe=None, cmd=cmd, tail=out[-1500:])
    if exe.exists():
        exe.unlink()
    tmp.rename(exe)
    return dict(state='编译通过', exe=str(exe), cmd=cmd)


def check_player(c, war, b, levels, duel, melee, n):
    root = Path(c['root'])
    sub = root / 'sealed' / n / 'submission'
    deploys = {}

    def one(key, fname, maps):
        f = sub / fname
        if not f.is_file():
            deploys[key] = dict(state='未交', file=None)
            return
        errs, cost = [], None
        for m in maps:
            r = war_check(war, m, f)
            cost = r.get('cost', cost)
            if not r.get('ok'):
                errs += [(f'{m.stem}：' if len(maps) > 1 else '') + e for e in r.get('errors', [])]
        deploys[key] = dict(state='合法' if not errs else '不合法', file=str(f), cost=cost, errors=errs[:10])

    for lv, f in levels.items():
        one(f'level{lv}', f'level{lv}.json', [f])
    one('duel', 'duel.json', duel)
    one('melee', 'melee.json', [melee])
    cmd = compile_commander(b, sub / 'commander.cpp', root / 'eval' / 'build' / n)
    return dict(deploys=deploys, commander=cmd)


# ---------------------------------------------------------------- 赛程
def make_plan(c, checks, levels, duel, melee):
    root = Path(c['root'])
    names = [p['name'] for p in c['players']]
    replays = c.get('judge', {}).get('replays', True)
    south = int(c.get('melee_south_seat', 0))

    def dep(n, key):
        d = checks[n]['deploys'][key]
        return d['file'] if d['file'] else '-'

    def cmd(n):
        return checks[n]['commander'].get('exe') or '-'

    plan = {}

    def add(key, kind, scen, seats, mode_key, **kw):
        plan[key] = dict(kind=kind, map=str(scen), map_name=scen.stem, seats=seats,
                         deploys=[dep(n, mode_key) for n in seats], cmds=[cmd(n) for n in seats],
                         replay=str(root / 'eval' / 'replays' / f'{key}.wrp') if replays else None, **kw)

    for lv, f in levels.items():
        for n in names:
            if checks[n]['deploys'][f'level{lv}']['state'] == '合法':   # 不合法、没交：不开打，判 0
                add(f'L{lv}_{n}', 'level', f, [n], f'level{lv}', level=lv)
    for a, b in itertools.combinations(names, 2):
        for mi, m in enumerate(duel):
            for s0, s1 in ((a, b), (b, a)):
                add(f'D{mi}_{s0}_{s1}', 'duel', m, [s0, s1], 'duel', pair=[a, b])
    others_seats = [k for k in range(4) if k != south]
    for gi, grp in enumerate(itertools.combinations(names, 4)):
        for pi, perm in enumerate(itertools.permutations(grp[1:])):
            seats = [None] * 4
            seats[south] = grp[0]
            for s, n in zip(others_seats, perm):
                seats[s] = n
            add(f'M{gi:03d}_{pi}', 'melee', melee, seats, 'melee', group=list(grp))
    return plan


def run_batch(c, war, plan, pending, say):
    root = Path(c['root'])
    ev = root / 'eval'
    (ev / 'results').mkdir(parents=True, exist_ok=True)
    (ev / 'replays').mkdir(parents=True, exist_ok=True)
    workers = int(c.get('judge', {}).get('workers', 8))
    lines = []
    wall = c.get('judge', {}).get('match_wall_s')
    for i, key in enumerate(pending):
        t = plan[key]
        d = dict(id=i, map=t['map'], deploys=t['deploys'], cmds=t['cmds'], names=t['seats'])
        if t['replay']:
            d['replay'] = t['replay']
        lines.append(json.dumps(d, ensure_ascii=False))
    say(c, '评测', '', f'war.exe batch：{len(pending)} 场，{workers} 线程')
    errors, got = {}, 0
    with open(ev / 'batch_stderr.txt', 'ab') as se:
        args = [str(war), 'batch', '--workers', str(workers)] + (['--match-wall-s', str(wall)] if wall is not None else [])
        p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=se, cwd=str(ev))
        p.stdin.write(('\n'.join(lines) + '\n').encode('utf-8'))
        p.stdin.close()
        step = max(1, len(pending) // 10)
        for raw in p.stdout:
            try:
                r = json.loads(raw.decode('utf-8', 'replace'))
                key = pending[int(r['id'])]
            except (ValueError, KeyError, IndexError):
                continue
            if 'error' in r and r['error'] != ENGINE_TIMEOUT:
                errors[key] = r['error']
                continue
            if 'error' in r:   # 引擎超时：照样存下来（出榜时单独标出），不算没跑成
                say(c, '评测', '', f"{key}：引擎超时（{r.get('detail', '')}），记为引擎错误")
            write_json(ev / 'results' / f'{key}.json', r)
            got += 1
            if got % step == 0:
                say(c, '评测', '', f'已跑完 {got}/{len(pending)} 场')
        p.wait()
    if errors:
        write_json(ev / '没跑成的场次.json', errors)
    return got, errors


ENGINE_TIMEOUT = 'engine_timeout'


def engine_errors(plan, results):
    """结果里带 error 字段的场次（引擎超时）：[{key, kind, seats, map_name, error, detail}]。"""
    out = []
    for key, t in plan.items():
        r = results.get(key)
        if r and 'error' in r:
            out.append(dict(key=key, kind=t['kind'], seats=t['seats'], map_name=t['map_name'], error=r['error'], detail=r.get('detail', '')))
    return out


def health(c, plan, results):
    """每位选手的指挥程序在各场里的运行情况（超时、崩溃），只汇总引擎报的字段，供摘要提示。"""
    out = collections.defaultdict(lambda: dict(matches=0, timeouts=0, timeout_matches=0, crash_matches=0, statuses={}))
    for key, t in plan.items():
        r = results.get(key)
        if not r:
            continue
        for k, n in enumerate(t['seats']):
            cm = (r.get('commanders') or [{}] * 4)[k]
            if not cm.get('used'):
                continue
            h = out[n]
            h['matches'] += 1
            h['timeouts'] += int(cm.get('timeouts', 0) or 0)
            h['timeout_matches'] += 1 if cm.get('timeouts') else 0
            if cm.get('status') not in (None, 'ok'):
                h['crash_matches'] += 1
                h['statuses'][cm['status']] = h['statuses'].get(cm['status'], 0) + 1
    return dict(out)


def judge(c, say, rerun=False):
    root = Path(c['root'])
    ev, kit = root / 'eval', root / 'kit'
    if rerun and ev.exists():
        dest = root / '作废' / ('eval_重跑前_' + datetime.datetime.now().strftime('%Y%m%d_%H%M%S'))
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(ev), str(dest))
        say(c, '评测', '', f'旧的评测结果存到 {dest}，从头重跑')
    war = kit / 'war.exe'
    levels, duel, melee = scenario_files(c)
    names = [p['name'] for p in c['players']]
    checks = read(ev / '检查.json')
    if checks is None or set(checks) != set(names):
        b = load_build(kit)
        with futures.ThreadPoolExecutor(max_workers=4) as pool:
            got = dict(zip(names, pool.map(lambda n: check_player(c, war, b, levels, duel, melee, n), names)))
        checks = {n: got[n] for n in names}
        cxx = b.find_compiler()
        ver = subprocess.run([cxx, '--version'], capture_output=True).stdout.decode('utf-8', 'replace').splitlines()[:1]
        write_json(ev / '编译器.json', dict(cxx=cxx, version=ver, flags=list(b.FLAGS)))
        write_json(ev / '检查.json', checks)
        for n in names:
            dep = checks[n]['deploys']
            bad = ['%s %s' % (k, v['state']) for k, v in dep.items() if v['state'] != '合法']
            say(c, '评测', n, f"布阵 {len(dep) - len(bad)}/{len(dep)} 合法" + ('（' + '、'.join(bad) + '）' if bad else '')
                + f"；程序 {checks[n]['commander']['state']}")
    plan = make_plan(c, checks, levels, duel, melee)
    write_json(ev / '对局索引.json', plan)
    pending = [k for k in plan if not (ev / 'results' / f'{k}.json').exists()]
    counts = collections.Counter(t['kind'] for t in plan.values())
    say(c, '评测', '', f"赛程：闯关 {counts['level']} 场、1v1 {counts['duel']} 场、混战 {counts['melee']} 场；还要跑 {len(pending)} 场")
    if pending:
        got, errors = run_batch(c, war, plan, pending, say)
        if errors:
            raise RuntimeError(f'{len(errors)} 场没跑成（见 eval\\没跑成的场次.json），重跑"评测"只补这些场')
        if got != len(pending):
            raise RuntimeError(f'war.exe batch 只给回 {got}/{len(pending)} 场结果（见 eval\\batch_stderr.txt），重跑"评测"补齐')
    results = {k: read(ev / 'results' / f'{k}.json') for k in plan}
    write_json(ev / '程序运行情况.json', health(c, plan, results))
    bad = engine_errors(plan, results)
    write_json(ev / '引擎错误场次.json', bad)
    if bad:
        say(c, '评测', '', f'有 {len(bad)} 场引擎错误（见 eval\\引擎错误场次.json），出榜时单独标出、不当输赢')


# ---------------------------------------------------------------- 算分
LEVEL_SCORE_DEFAULT = dict(per_double=25.0, floor=20.0)


def level_score_cfg(c):
    cfg = dict(LEVEL_SCORE_DEFAULT)
    cfg.update({k: float(v) for k, v in (c.get('level_score') or {}).items() if k in cfg})
    return cfg


def level_scores(costs, per_double=25.0, floor=20.0):
    """一关的关卡分。costs：{选手: 过关时的造价}，没过的写 None（或不写）。
    返回 ({选手: 分}, 全场最低造价或 None)。过关的人里最省的 100；比他每贵一倍扣 per_double，最少 floor；没过 0。"""
    passed = {n: x for n, x in costs.items() if x is not None}
    best = min(passed.values()) if passed else None
    out = {}
    for n, x in costs.items():
        if x is None:
            out[n] = 0.0
        elif x <= best:
            out[n] = 100.0
        elif best <= 0:
            out[n] = floor
        else:
            out[n] = max(floor, 100.0 - per_double * math.log2(x / best))
    return out, best


def ranked(rows, key):
    """rows 按 key（越小越好）排序并标名次；key 完全相同的并列（1、1、3）。"""
    rows = sorted(rows, key=key)
    prev, rank = None, 0
    for i, r in enumerate(rows):
        k = key(r)
        if k != prev:
            rank, prev = i + 1, k
        r['名次'] = rank
    return rows


def r6(x):
    return None if x is None else round(x, 6)


def mean(xs):
    return sum(xs) / len(xs) if xs else None


def f1(x, nd=1):
    return '—' if x is None else f'{x:.{nd}f}'


def boards(c):
    root = Path(c['root'])
    ev, out = root / 'eval', root / '成绩'
    checks, plan = read(ev / '检查.json'), read(ev / '对局索引.json')
    results = {k: read(ev / 'results' / f'{k}.json') for k in plan}
    missing = [k for k, v in results.items() if v is None]
    if missing:
        raise RuntimeError(f'还有 {len(missing)} 场没有结果，先把"评测"跑完')
    names = [p['name'] for p in c['players']]
    disp = {p['name']: p.get('display') or p['name'] for p in c['players']}
    w = c['weights']
    refs = {n: c['levels'][str(n)].get('reference_cost') for n in level_ids(c)}
    ls = level_score_cfg(c)
    level_names = {t['level']: t['map_name'].split('_', 1)[-1] for t in plan.values() if t['kind'] == 'level'}

    # ---- 闯关
    cells = {n: {} for n in names}
    for n in names:
        for lv in level_ids(c):
            d = checks[n]['deploys'][f'level{lv}']
            if d['state'] != '合法':
                cells[n][lv] = dict(state=d['state'], errors=d.get('errors', []))
                continue
            key = f'L{lv}_{n}'
            r = results[key]
            if 'error' in r:   # 引擎错误：不当成没过，单独标出；算分时暂按 0
                cells[n][lv] = dict(state='引擎错误', cost=d.get('cost'), reason=r['error'], secs=None, replay=None, result=key)
                continue
            cells[n][lv] = dict(state='过关' if r.get('passed') is True else '没过', cost=r['seats'][0]['deploy_cost'],
                                reason=r.get('reason'), secs=r.get('secs'), replay=plan[key]['replay'], result=key)
    best, refline_rows = {}, []
    for lv in level_ids(c):
        pts, best[lv] = level_scores({n: cells[n][lv]['cost'] if cells[n][lv]['state'] == '过关' else None for n in names},
                                     ls['per_double'], ls['floor'])
        for n in names:
            x = cells[n][lv]
            x['score'] = pts[n]
            if x['state'] == '过关':
                x['vs_best'] = x['cost'] / best[lv] if best[lv] else None
        ref = refs[lv]
        cheaper = [n for n in names if ref is not None and cells[n][lv]['state'] == '过关' and cells[n][lv]['cost'] < ref]
        refline_rows.append(dict(level=lv, name=level_names.get(lv, ''), reference_cost=ref, best_cost=best[lv],
                                 best_players=[n for n in names if best[lv] is not None and cells[n][lv]['state'] == '过关' and cells[n][lv]['cost'] == best[lv]],
                                 passed=sum(1 for n in names if cells[n][lv]['state'] == '过关'), cheaper_than_reference=cheaper))
    lv_rows = []
    for n in names:
        cs = cells[n]
        passed = [lv for lv, x in cs.items() if x['state'] == '过关']
        lv_rows.append(dict(player=n, display=disp[n], 闯关分=mean([x['score'] for x in cs.values()]), 过关数=len(passed),
                            过关造价和=sum(cs[lv]['cost'] for lv in passed), levels={str(k): v for k, v in cs.items()}))

    # ---- 1v1
    duel = {n: [] for n in names}
    pair = collections.defaultdict(float)
    pair_n = collections.defaultdict(int)
    for key, t in plan.items():
        if t['kind'] != 'duel':
            continue
        r = results[key]
        if 'error' in r:   # 引擎错误：这一场不计入
            continue
        for k, n in enumerate(t['seats']):
            pts = 0.5 if r['winner'] < 0 else (1.0 if r['winner'] == k else 0.0)
            duel[n].append(pts)
            o = t['seats'][1 - k]
            pair[(n, o)] += pts
            pair_n[(n, o)] += 1
    # ---- 混战
    melee = {n: [] for n in names}
    for key, t in plan.items():
        if t['kind'] != 'melee':
            continue
        r = results[key]
        if 'error' in r:
            continue
        for k, n in enumerate(t['seats']):
            tm = r['teams'][k]
            melee[n].append(dict(score=tm['score'], rank=tm['rank'], rank_pts=tm['rank_pts'], kill_pts=tm['kill_pts']))
    adv_rows = []
    for n in names:
        d, m = duel[n], melee[n]
        s1 = 100.0 * mean(d) if d else None
        s2 = 100.0 * mean([x['score'] for x in m]) / 6.0 if m else None
        if s1 is not None and s2 is not None:
            adv = w['1v1'] * s1 + w['混战'] * s2
        else:
            adv = s1 if s1 is not None else s2
        adv_rows.append(dict(player=n, display=disp[n], 对抗分=adv, **{'1v1分': s1, '混战分': s2},
                             duel=dict(games=len(d), win=d.count(1.0), draw=d.count(0.5), loss=d.count(0.0)),
                             melee=dict(games=len(m), avg_score=mean([x['score'] for x in m]), avg_rank=mean([x['rank'] for x in m]),
                                        avg_kill_pts=mean([x['kill_pts'] for x in m]), firsts=sum(1 for x in m if x['rank'] == 1))))

    # ---- 总榜
    lv_by, adv_by = {r['player']: r for r in lv_rows}, {r['player']: r for r in adv_rows}
    tot_rows = []
    for n in names:
        a, b = lv_by[n], adv_by[n]
        total = w['闯关'] * a['闯关分'] + w['对抗'] * b['对抗分'] if b['对抗分'] is not None else a['闯关分']
        tot_rows.append(dict(player=n, display=disp[n], 总分=total, 闯关分=a['闯关分'], 对抗分=b['对抗分'], **{'1v1分': b['1v1分'], '混战分': b['混战分']},
                             过关数=a['过关数'], 过关造价和=a['过关造价和']))

    neg = lambda x: -(x if x is not None else -1e18)  # noqa: E731
    lv_rows = ranked(lv_rows, lambda r: (neg(r6(r['闯关分'])), -r['过关数']))
    tot_rows = ranked(tot_rows, lambda r: (neg(r6(r['总分'])), -r['过关数'], neg(r6(r['对抗分'])), r['过关造价和']))
    adv_total = ranked([dict(x) for x in adv_rows], lambda r: (neg(r6(r['对抗分'])),))
    adv_1v1 = ranked([dict(x) for x in adv_rows], lambda r: (neg(r6(r['1v1分'])),))
    adv_mel = ranked([dict(x) for x in adv_rows], lambda r: (neg(r6(r['混战分'])),))
    pairs = {a: {b: dict(points=pair[(a, b)], games=pair_n[(a, b)]) for b in names if b != a} for a in names}

    rule = (f"这一关全场最省的人 100 分；比他每贵一倍扣 {ls['per_double']:g} 分；过关最少 {ls['floor']:g} 分；没过 0 分。"
            '闯关分 = 五关平均；同分比过关数。')
    meta = dict(name=c.get('name'), level_score=ls, weights=w, generated=datetime.datetime.now().astimezone().isoformat(timespec='seconds'))
    out.mkdir(parents=True, exist_ok=True)

    def gcost(x):
        return '—' if x is None else f'{x:g}'

    bad = engine_errors(plan, results)
    engine_md = []
    if bad:
        how = {'level': '闯关这一格暂按 0 分', 'duel': '不计入 1v1 平均', 'melee': '不计入混战平均'}
        engine_md = ['', '## 引擎错误的场次（待主办方处理）', '',
                     '引擎超时、没有胜负的场次，不当成输也不当成赢；处理办法由主办方定（例如空机 `评测 --重跑`）。', '',
                     '| 场次 | 类型 | 地图 | 座位 | 现在怎么算 |', '|---|---|---|---|---|']
        engine_md += [f"| {x['key']} | {x['kind']} | {x['map_name']} | {'、'.join(disp.get(n, n) if n else '—' for n in x['seats'])} | {how[x['kind']]} |" for x in bad]

    # 闯关榜
    md = ['# 闯关榜', '', rule, '',
          '| 名次 | 选手 | 闯关分 | 过关数 | ' + ' | '.join(f'第{lv}关' for lv in level_ids(c)) + ' |',
          '|---|---|---|---|' + '---|' * len(level_ids(c))]

    def cell(x):
        if x['state'] == '过关':
            return f"{x['score']:.1f}（{x['cost']:g}）"
        if x['state'] == '没过':
            return f"没过（{x['cost']:g}）"
        if x['state'] == '引擎错误':
            return '引擎错误（暂记 0）'
        return x['state']
    for r in lv_rows:
        md.append(f"| {r['名次']} | {r['display']} | {f1(r['闯关分'])} | {r['过关数']} | " + ' | '.join(cell(r['levels'][str(lv)]) for lv in level_ids(c)) + ' |')
    md += ['', '格子里是"关卡分（造价）"；没过的关只写造价；"引擎错误"是引擎超时、没有胜负的场次，暂按 0 分算，待主办方处理。', '',
           '## 每关最低造价和主办方参考线', '',
           '主办方参考造价是主办方赛前自己摆出来的过关解，不参与算分，只作对照。', '',
           '| 关 | 过关人数 | 全场最低造价 | 最低的是谁 | 主办方参考造价 | 比参考更省的选手 |', '|---|---|---|---|---|---|']
    for x in refline_rows:
        md.append(f"| 第{x['level']}关 {x['name']} | {x['passed']} | {gcost(x['best_cost'])} | {'、'.join(disp[n] for n in x['best_players']) or '—'} | "
                  f"{gcost(x['reference_cost'])} | "
                  + ('、'.join(f"{disp[n]}（{cells[n][x['level']]['cost']:g}）" for n in x['cheaper_than_reference']) or '—') + ' |')
    md += ['', '## 各关明细', '']
    for lv in level_ids(c):
        md += [f"### 第{lv}关 {level_names.get(lv, '')}（全场最低造价 {gcost(best[lv])}）", '',
               '| 选手 | 结果 | 造价 | 是最低的几倍 | 关卡分 | 结束原因 | 用时（秒） |', '|---|---|---|---|---|---|---|']
        for r in lv_rows:
            x = r['levels'][str(lv)]
            why = x.get('reason') or '；'.join(x.get('errors', []))[:120] or '—'
            ratio = f"{x['vs_best']:.2f}" if x.get('vs_best') is not None else '—'
            md.append(f"| {r['display']} | {x['state']} | {gcost(x.get('cost'))} | {ratio} | {x['score']:.1f} | {why.replace('|', '/')} | {f1(x.get('secs'))} |")
        md.append('')
    md += engine_md
    (out / '闯关榜.md').write_text('\n'.join(md) + '\n', encoding='utf-8')
    write_json(out / '闯关榜.json', dict(meta, rule=rule, levels=refline_rows, rows=lv_rows, engine_errors=bad))

    # 对抗榜
    md = ['# 对抗榜', '', f"对抗分 = 1v1 分 × {w['1v1']:g} + 混战分 × {w['混战']:g}。", '', '## 合成', '',
          '| 名次 | 选手 | 对抗分 | 1v1 分 | 混战分 |', '|---|---|---|---|---|']
    md += [f"| {r['名次']} | {r['display']} | {f1(r['对抗分'])} | {f1(r['1v1分'])} | {f1(r['混战分'])} |" for r in adv_total]
    md += ['', '## 1v1', '', '每对 4 场（' + '、'.join(c['duel_maps']) + ' × 换座位），胜 1 平 0.5 负 0；1v1 分 = 平均 × 100。', '',
           '| 名次 | 选手 | 1v1 分 | 胜 | 平 | 负 | 场数 |', '|---|---|---|---|---|---|---|']
    md += [f"| {r['名次']} | {r['display']} | {f1(r['1v1分'])} | {r['duel']['win']} | {r['duel']['draw']} | {r['duel']['loss']} | {r['duel']['games']} |" for r in adv_1v1]
    order = [r['player'] for r in adv_1v1]
    md += ['', '### 两两比分（行对列的得分，满分 4）', '', '| | ' + ' | '.join(disp[n] for n in order) + ' |', '|---|' + '---|' * len(order)]
    for a in order:
        md.append(f'| {disp[a]} | ' + ' | '.join('—' if a == b else f"{pairs[a][b]['points']:g}" for b in order) + ' |')
    md += ['', '## 四人混战', '', f"任取 4 人的每种组合打 6 场（固定第一位坐南边）。每场得分 = 名次分（3/2/1/0）+ 歼敌分（打掉的造价 ÷ 预算，最多 3）；混战分 = 场均 ÷ 6 × 100。", '',
           '| 名次 | 选手 | 混战分 | 场均得分 | 平均名次 | 场均歼敌分 | 第 1 名次数 | 场数 |', '|---|---|---|---|---|---|---|---|']
    md += [f"| {r['名次']} | {r['display']} | {f1(r['混战分'])} | {f1(r['melee']['avg_score'], 2)} | {f1(r['melee']['avg_rank'], 2)} | {f1(r['melee']['avg_kill_pts'], 2)} | {r['melee']['firsts']} | {r['melee']['games']} |" for r in adv_mel]
    md += engine_md
    (out / '对抗榜.md').write_text('\n'.join(md) + '\n', encoding='utf-8')
    write_json(out / '对抗榜.json', dict(meta, combined=adv_total, duel=adv_1v1, melee=adv_mel, pairs=pairs, engine_errors=bad))

    # 总榜
    md = ['# 总榜', '', f"总分 = 闯关分 × {w['闯关']:g} + 对抗分 × {w['对抗']:g}；同分依次比过关数、对抗分、过了的关的造价之和（少的在前）。", '',
                                    '| 名次 | 选手 | 总分 | 闯关分 | 对抗分 | 1v1 分 | 混战分 | 过关数 | 过了的关造价和 |', '|---|---|---|---|---|---|---|---|---|']
    md += [f"| {r['名次']} | {r['display']} | {f1(r['总分'])} | {f1(r['闯关分'])} | {f1(r['对抗分'])} | {f1(r['1v1分'])} | {f1(r['混战分'])} | {r['过关数']} | {r['过关造价和']:g} |" for r in tot_rows]
    md += engine_md
    (out / '总榜.md').write_text('\n'.join(md) + '\n', encoding='utf-8')
    write_json(out / '总榜.json', dict(meta, rows=tot_rows, engine_errors=bad))

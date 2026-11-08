"""第 9 集造桥：从选手设计重算全部正式成绩，并和 results/ 里的最终成绩逐项核对。

  python tournament/score.py                 重算第 1～6 关 + 第 7 关 72 局，和 results/ 核对（默认）
  python tournament/score.py --levels-only   只重算第 1～6 关
  python tournament/score.py --workers 8     并行进程数（默认 CPU 核数，最多 16）
  python tournament/score.py --write         用重算结果改写 results/scores.json 和 results/duels.json（维护用）

先编译物理内核：python build_engine.py。全部重算在 8 核机器上约 1～2 分钟。
退出码：0 全部一致；1 有不一致；4 参考答案或引擎有问题。

计分（与比赛相同）：
- 第 1～6 关：全部公开场景通过才算过关，关卡分 = round(100 × 参考造价 ÷ 造价, 2)；没过、没交、不合法 = 0。
- 第 7 关：每位选手的决赛桥 × 其他 8 位的捣蛋车，共 72 局。标准车队全部按时完好到达 = 守住；
  没有全部到达 = 攻破（记给捣蛋车一方）。攻防分 = 50 × 守住局数 ÷ 8 + 50 × 攻破局数 ÷ 8。
- 闯关分 = 六关关卡分的平均；总分 = (六关关卡分之和 + 攻防分) ÷ 7。
"""
import hashlib, json, os, sys
from concurrent.futures import ProcessPoolExecutor

os.environ['BRIDGE_NO_ARCHIVE'] = '1'          # 不在当前目录写 archive/
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WS = os.path.join(ROOT, 'workspace')
sys.path.insert(0, WS)
import bridge as B       # noqa: E402  赛时选手包原样代码
import saboteur as SB    # noqa: E402

DESIGNS = os.path.join(ROOT, 'designs')
REF = os.path.join(ROOT, 'reference')
RESULTS = os.path.join(ROOT, 'results')
LEVELS = [str(n) for n in range(1, 7)]


def roster():
    with open(os.path.join(DESIGNS, 'roster.json'), encoding='utf-8') as f:
        return json.load(f)


def read(p):
    """返回 (内容, sha256, 错误)。同一份字节算哈希和解析。"""
    if not os.path.isfile(p):
        return None, None, '未交'
    with open(p, 'rb') as f:
        data = f.read()
    sha = hashlib.sha256(data).hexdigest()
    try:
        return B.loads_bytes(data), sha, None
    except ValueError as e:
        return None, sha, f'文件读不了：{e}'


def _scene(args):
    lv_id, design, sc_path = args
    lv = B.load_level(lv_id)
    P = B.parse_design(lv, design)
    res, _ = B.simulate(lv, P, B.load_json(sc_path))
    return bool(res['ok'])


def run_level(ex, lv_id, design):
    lv = B.load_level(lv_id)
    try:
        P = B.parse_design(lv, design)
    except B.DesignError as e:
        return dict(valid=False, error=str(e).split('\n')[0][:200], cost=None, passed=0, n=len(lv['samples']))
    paths = B.sample_paths(lv)
    oks = list(ex.map(_scene, [(lv_id, design, p) for p in paths]))
    return dict(valid=True, cost=P['cost'], passed=sum(oks), n=len(paths))


def level_score(r, ref):
    return round(100.0 * ref / r['cost'], 2) if r['valid'] and r['passed'] == r['n'] else 0.0


def _duel(args):
    braw, sraw = args
    lv = B.load_level('final')
    r, _ = SB.duel(lv, B.parse_design(lv, braw), SB.parse_sab(lv, sraw))
    res = r['res']
    return dict(vehicles=res['vehicles'], std=[v['status'] for v in res['std_vehicles']],
                sab_arrived=bool(r['sab_arrived']), anomaly=r['anomaly'])


def main(argv):
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass
    if not os.path.isfile(B.SIM_EXE):
        print('还没有物理内核：先运行 python build_engine.py'); return 4
    workers = int(argv[argv.index('--workers') + 1]) if '--workers' in argv else min(16, os.cpu_count() or 4)
    levels_only = '--levels-only' in argv
    names = roster()
    ids = [p['id'] for p in names]
    disp = {p['id']: p['name'] for p in names}
    out = dict(reference={}, players={})
    refc = {}
    with ProcessPoolExecutor(workers) as ex:
        for n in LEVELS:
            d, _, err = read(os.path.join(REF, f'level{n}.json'))
            r = run_level(ex, n, d) if err is None else None
            if not r or not r['valid'] or r['passed'] < r['n']:
                print(f'！！第 {n} 关参考答案没有全部通过，不能计分'); return 4
            out['reference'][n] = round(r['cost'], 2)
            refc[n] = r['cost']
        for pid in ids:
            row = dict(name=disp[pid], levels={})
            for n in LEVELS:
                d, sha, err = read(os.path.join(DESIGNS, pid, f'level{n}.json'))
                if err:
                    row['levels'][n] = dict(score=0.0, note=err, cost=None, passed=None, n=None, sha256=sha)
                    continue
                r = run_level(ex, n, d)
                row['levels'][n] = dict(score=level_score(r, refc[n]), cost=None if r['cost'] is None else round(r['cost'], 2),
                                        passed=r['passed'], n=r['n'], note=r.get('error', ''), sha256=sha)
            row['闯关'] = round(sum(row['levels'][n]['score'] for n in LEVELS) / 6, 2)
            out['players'][pid] = row
            cells = '  '.join(f'L{n} {row["levels"][n]["score"]:6.2f}' for n in LEVELS)
            print(f'{disp[pid]:<20} {cells}')
        if levels_only:
            return compare(out, None, ids, levels_only=True)
        files = {}
        for pid in ids:
            b, bsha, berr = read(os.path.join(DESIGNS, pid, 'final_bridge.json'))
            s, ssha, serr = read(os.path.join(DESIGNS, pid, 'saboteur.json'))
            lv = B.load_level('final')
            if berr is None:
                try: B.parse_design(lv, b)
                except B.DesignError as e: berr = '不合法：' + str(e).split('\n')[0]
            if serr is None:
                try: SB.parse_sab(lv, s)
                except B.DesignError as e: serr = '不合法：' + str(e).split('\n')[0]
            files[pid] = dict(bridge=b, bridge_sha=bsha, bridge_err=berr, sab=s, sab_sha=ssha, sab_err=serr)
        keys = [(a, b) for a in ids for b in ids if a != b]
        jobs = [k for k in keys if not files[k[0]]['bridge_err'] and not files[k[1]]['sab_err']]
        raw = dict(zip(jobs, ex.map(_duel, [(files[a]['bridge'], files[b]['sab']) for a, b in jobs])))
    duels = []
    for a, b in keys:
        if (a, b) in raw:
            g = raw[a, b]
            breached = not all(s == 2 for s in g['std'])
            duels.append(dict(bridge=a, saboteur=b, breached=breached, convoy_status=g['std'], sab_arrived=g['sab_arrived'],
                              anomaly=g['anomaly'], vehicles=g['vehicles']))
        else:   # 文件不合法：桥不合法算攻破，捣蛋车不合法算守住（本届 9 人的文件都合法）
            duels.append(dict(bridge=a, saboteur=b, breached=bool(files[a]['bridge_err']), invalid=True))
    for pid in ids:
        held = sum(1 for g in duels if g['bridge'] == pid and not g['breached'])
        hit = sum(1 for g in duels if g['saboteur'] == pid and g['breached'])
        row = out['players'][pid]
        row.update(桥守住=held, 车攻破=hit, 攻防=round(50 * held / 8 + 50 * hit / 8, 2),
                   final_bridge_sha256=files[pid]['bridge_sha'], saboteur_sha256=files[pid]['sab_sha'])
        row['总分'] = round((sum(row['levels'][n]['score'] for n in LEVELS) + row['攻防']) / 7, 2)
    print()
    print('第 7 关（行 = 桥，列 = 捣蛋车；X = 攻破，. = 守住）')
    print(' ' * 10 + ' '.join(f'{p[:6]:>6}' for p in ids))
    for a in ids:
        cells = []
        for b in ids:
            if a == b:
                cells.append(f'{"—":>6}')
            else:
                g = next(g for g in duels if g['bridge'] == a and g['saboteur'] == b)
                cells.append(f'{"X" if g["breached"] else ".":>6}')
        print(f'{a:<10}' + ' '.join(cells))
    print()
    for pid in sorted(ids, key=lambda k: -out['players'][k]['总分']):
        r = out['players'][pid]
        print(f'{r["name"]:<20} 总分 {r["总分"]:7.2f}  闯关 {r["闯关"]:7.2f}  攻防 {r["攻防"]:6.2f}  守住 {r["桥守住"]}/8  攻破 {r["车攻破"]}/8')
    if '--write' in argv:
        with open(os.path.join(RESULTS, 'scores.json'), 'w', encoding='utf-8', newline='\n') as f:
            json.dump(out, f, ensure_ascii=False, indent=1)
            f.write('\n')
        with open(os.path.join(RESULTS, 'duels.json'), 'w', encoding='utf-8', newline='\n') as f:
            json.dump(duels, f, ensure_ascii=False, indent=1)
            f.write('\n')
        print('已改写 results/scores.json、results/duels.json')
        return 0
    return compare(out, duels, ids)


def compare(out, duels, ids, levels_only=False):
    with open(os.path.join(RESULTS, 'scores.json'), encoding='utf-8') as f:
        want = json.load(f)
    bad = []
    if out['reference'] != want['reference']:
        bad.append(f'参考造价 {out["reference"]} ≠ {want["reference"]}')
    for pid in ids:
        got, exp = out['players'][pid], want['players'][pid]
        for n in LEVELS:
            for k in ('score', 'cost', 'passed', 'sha256'):
                if got['levels'][n][k] != exp['levels'][n][k]:
                    bad.append(f'{pid} 第 {n} 关 {k}：{got["levels"][n][k]} ≠ {exp["levels"][n][k]}')
        keys = ['闯关'] if levels_only else ['闯关', '桥守住', '车攻破', '攻防', '总分', 'final_bridge_sha256', 'saboteur_sha256']
        for k in keys:
            if got[k] != exp[k]:
                bad.append(f'{pid} {k}：{got[k]} ≠ {exp[k]}')
    if duels is not None:
        with open(os.path.join(RESULTS, 'duels.json'), encoding='utf-8') as f:
            wd = {(g['bridge'], g['saboteur']): g for g in json.load(f)}
        for g in duels:
            e = wd[g['bridge'], g['saboteur']]
            for k in ('breached', 'convoy_status', 'vehicles'):
                if g.get(k) != e.get(k):
                    bad.append(f'桥 {g["bridge"]} × 捣蛋车 {g["saboteur"]} {k} 不一致')
    print()
    if bad:
        print(f'！！和 results/ 不一致 {len(bad)} 处：')
        for b in bad[:40]:
            print('  ' + b)
        return 1
    what = '第 1～6 关 54 份设计' if levels_only else '第 1～6 关 54 份设计、第 7 关 72 局逐车结果、九人总分'
    print(f'核对通过：{what}与 results/ 完全一致。')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

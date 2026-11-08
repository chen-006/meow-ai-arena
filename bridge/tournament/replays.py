"""生成回放网页（双击就能打开，内嵌 workspace/viewer.html）。

  python tournament/replays.py                 重新生成 replays/ 里收录的 9 份
  python tournament/replays.py --all out/回放   生成全部：54 份设计 × 每个场景、6 份参考答案 × 每个场景、第 7 关 72 局

收录规则（不按好看或结果挑）：
- 第 1～6 关：每关关卡分最高的那份设计，跑该关第一个场景（sample1，与 bridge.py --html 相同）。
- 第 7 关：72 局里用 random.Random(20261107) 抽 3 局。
先编译物理内核：python build_engine.py。
"""
import json, os, random, sys

os.environ['BRIDGE_NO_ARCHIVE'] = '1'
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'workspace'))
import bridge as B       # noqa: E402
import saboteur as SB    # noqa: E402

DESIGNS = os.path.join(ROOT, 'designs')


def level_replay(n, design_path, sample_path, out):
    lv = B.load_level(n)
    P = B.parse_design(lv, B.load_json(design_path))
    res, rep = B.simulate(lv, P, B.load_json(sample_path), replay=True)
    B.write_html(rep, out)
    return bool(res['ok'])


def duel_replay(bridge_id, sab_id, out):
    lv = B.load_level('final')
    P = B.parse_design(lv, B.load_json(os.path.join(DESIGNS, bridge_id, 'final_bridge.json')))
    S = SB.parse_sab(lv, B.load_json(os.path.join(DESIGNS, sab_id, 'saboteur.json')))
    r, rep = SB.duel(lv, P, S, replay=True)
    # 回放里的结论改写成正式计分口径：标准车队没有全部到达 = 攻破
    breached = not all(v['status'] == 2 for v in r['res']['std_vehicles'])
    reason = '标准车队没有全部按时完好到达：攻破，捣蛋车一方得分' if breached else '标准车队全部按时完好到达：守住，桥方得分'
    lines = [l for l in r['text'].split('\n') if not l.startswith('  裁决：') and '不算数' not in l]
    text = '\n'.join(lines + ['  正式计分：' + reason])
    rep['result'] = dict(ok=not breached, text=text, raw_ok=bool(r['res'].get('ok')))
    rep['verdict'] = dict(bridge_win=not breached, sab_win=breached, sab_arrived=r['sab_arrived'], reason=reason, baseline=None)
    B.write_html(rep, out)
    return reason


def selection():
    with open(os.path.join(ROOT, 'results', 'scores.json'), encoding='utf-8') as f:
        sc = json.load(f)
    ids = list(sc['players'])
    top = {}
    for n in map(str, range(1, 7)):
        top[n] = max(ids, key=lambda p: sc['players'][p]['levels'][n]['score'])   # 同分取名单靠前者
    games = [(a, b) for a in ids for b in ids if a != b]
    picks = random.Random(20261107).sample(games, 3)
    return top, picks


def main(argv):
    sys.stdout.reconfigure(encoding='utf-8')
    if not os.path.isfile(B.SIM_EXE):
        raise SystemExit('还没有物理内核：先运行 python build_engine.py')
    if '--all' in argv:
        out = argv[argv.index('--all') + 1]
        os.makedirs(out, exist_ok=True)
        ids = [p['id'] for p in json.load(open(os.path.join(DESIGNS, 'roster.json'), encoding='utf-8'))]
        for n in map(str, range(1, 7)):
            lv = B.load_level(n)
            for who in ids + ['reference']:
                dp = os.path.join(ROOT, 'reference', f'level{n}.json') if who == 'reference' else os.path.join(DESIGNS, who, f'level{n}.json')
                if not os.path.isfile(dp):
                    continue
                try:
                    B.parse_design(lv, B.load_json(dp))
                except B.DesignError:
                    print(f'跳过 {who} 第 {n} 关（设计不合法）'); continue
                for sp in B.sample_paths(lv):
                    s = os.path.splitext(os.path.basename(sp))[0]
                    level_replay(n, dp, sp, os.path.join(out, f'L{n}_{who}_{s}.html'))
        for a in ids:
            for b in ids:
                if a != b:
                    duel_replay(a, b, os.path.join(out, f'L7_桥_{a}_vs_捣蛋车_{b}.html'))
        print('已写出：', out)
        return 0
    top, picks = selection()
    rd = os.path.join(ROOT, 'replays')
    for n, who in top.items():
        lv = B.load_level(n)
        ok = level_replay(n, os.path.join(DESIGNS, who, f'level{n}.json'), B.sample_paths(lv)[0], os.path.join(rd, f'L{n}-{who}.html'))
        print(f'第 {n} 关 {who}：{"通过" if ok else "失败"}')
    for a, b in picks:
        r = duel_replay(a, b, os.path.join(rd, f'L7-bridge-{a}-vs-saboteur-{b}.html'))
        print(f'第 7 关 桥 {a} × 捣蛋车 {b}：{r}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))

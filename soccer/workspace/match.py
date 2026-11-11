"""本地对战工具：用你的程序、队友的程序和基准 / 陪练打比赛。对局由 arena.exe（与正式比赛同一个裁判）完成。

参赛者可以写：
  baseline        基准 bot（中等强度，只有程序）
  midfield        中档陪练 bot（介于基准和陪练之间，只有程序）
  sparring        陪练 bot（较强，只有程序）
  template        template.cpp（最简单的合法程序，很弱）
  路径.cpp        自动编译（与正式比赛相同的编译方式，结果缓存在 build/）
  路径.exe        直接用

命令：
  python match.py check player.cpp
      检查：能编译；你的程序 ×2 对 baseline 打一局正式计时的比赛，报告超时、非法输出、崩溃和每步用时
  python match.py play A1 A2 B1 B2 [--seed 1] [--first-kick 0]
      打一局：A 队 = A1、A2（守左门），B 队 = B1、B2。显示比分、故障、用时；回放存进 replays/
  python match.py run --me player.cpp [--mate 队友.cpp] [--opp baseline] [--seeds 8] [--workers K]
      我方（我 + 队友）对 --opp 打 2×seeds 局：每个种子换边各一局（两局分别由双方先开球）；偶数种子我是队内 0 号，奇数种子我是 1 号。
      --mate 不写 = 两个座位都是我的程序。--opp 可写 "x"（两名对手都是 x）或 "x,y"。
      打印得分率（胜 1 平 0.5）、净胜球、故障统计；回放存进 replays/run-<时间>/
共同选项：--step-ms（默认 10，正式值）--no-stderr（不保存程序的标准错误输出）
  --sandbox none|appcontainer（默认 none：不做身份隔离；超时、内存 1 GB、不能开子进程照样由裁判的 Job Object 限制。
  appcontainer = 正式比赛用的 Windows 隔离，只在想按赛时环境复查时用）
看回放：python replay.py info <回放文件>，或用浏览器打开 viewer.html 把回放拖进去。
"""
import argparse
import datetime
import json
import os
import subprocess
import sys
from pathlib import Path

KIT = Path(__file__).resolve().parent
sys.path.insert(0, str(KIT))
import build  # noqa: E402

ARENA = KIT / 'arena.exe'
REPLAYS = KIT / 'replays'
BUILTIN = {'baseline': KIT / 'bots' / 'baseline.exe', 'midfield': KIT / 'bots' / 'midfield.exe', 'sparring': KIT / 'bots' / 'sparring.exe'}


def arena_exe():
    # 开源版源码更新后重建，避免继续运行带旧沙箱路径的缓存裁判。
    sources = [KIT / 'arena.cpp', KIT / 'soccer.hpp']
    if ARENA.is_file() and ARENA.stat().st_mtime_ns >= max(p.stat().st_mtime_ns for p in sources):
        return ARENA
    # 没有现成的 arena.exe：从源码编译（需要 g++）
    cxx = build.find_compiler()
    if not cxx:
        raise build.BuildError('没有 arena.exe，也没有找到 g++ 来编译它')
    r = subprocess.run([cxx, '-std=c++17', '-O2', '-static', 'arena.cpp', '-o', 'arena.exe', '-lshell32', '-luserenv'], cwd=KIT,
                       capture_output=True, text=True, encoding='utf-8', errors='replace')
    if r.returncode:
        raise build.BuildError('编译 arena.cpp 失败：\n' + r.stderr[-3000:])
    return ARENA


def resolve(spec):
    """参赛者 → (显示名, exe 路径)"""
    low = spec.lower()
    if low in BUILTIN:
        return low, BUILTIN[low]
    if low == 'template':
        return 'template', build.compile_cpp(KIT / 'template.cpp')
    p = Path(spec)
    if not p.is_file():
        raise build.BuildError(f'找不到：{spec}（也可以写 baseline、midfield、sparring、template）')
    name = p.stem if p.stem not in ('player', 'bot', 'main') else f'{p.parent.name}-{p.stem}'
    name = name.replace(' ', '_') or 'player'
    if p.suffix.lower() in ('.cpp', '.cc', '.cxx'):
        return name, build.compile_cpp(p)
    return name, p.resolve()


def limit_args(a):
    return ['--step-ms', str(a.step_ms), '--sandbox', getattr(a, 'sandbox', 'none')]


def with_sandbox(extra):
    """arena 自己默认 appcontainer（赛时设置）；本地工具没写就用 none。"""
    return list(extra) if '--sandbox' in extra else [*extra, '--sandbox', 'none']


def show_game(g, labels=None):
    print(f"比分  A {g['score'][0]} : {g['score'][1]} B    （{g['steps']} 步，进球步数：" +
          ', '.join(f"{t}{'AB'[k]}" for t, k in g['goals']) + '）')
    for p, pl in enumerate(g['players']):
        tag = f"{'AB'[p // 2]}{p % 2 + 1}"
        line = (f"  {tag} {pl['name']:<20s} 有效踢球 {pl['kicks']:3d}  每步 CPU 平均 {pl['avg_ms']:.3f} ms  最多 {pl['max_ms']:.3f} ms"
                f"  第 0 步 {pl['first_ms']:.1f} ms")
        bad = []
        if pl['timeouts']:
            bad.append(f"超时 {pl['timeouts']} 次")
        if pl['invalid']:
            bad.append(f"非法输出 {pl['invalid']} 次")
        if pl['status'] != 'ok':
            bad.append({'crash': '程序退出/崩溃', 'lost': '失去响应被结束', 'timeout_limit': '累计超时过多被结束',
                        'start_fail': '启动失败', 'output_limit': '输出超量被结束'}.get(pl['status'], pl['status']) +
                       f"（第 {pl['dead_step']} 步起不再发送输入，裁判代填 0 0 0 0 0）")
        if bad:
            line += '  ！' + '，'.join(bad)
            if pl['faults']:
                f0 = pl['faults'][0]
                line += f"  第一次：第 {f0[0]} 步 {f0[1]} {f0[2]}"
        print(line)


def cmd_play(a):
    if len(a.players) != 4:
        print('play 要 4 个参赛者：A1 A2 B1 B2')
        return 2
    rs = [resolve(s) for s in a.players]
    REPLAYS.mkdir(exist_ok=True)
    stamp = datetime.datetime.now().strftime('%m%d-%H%M%S')
    stem = f"{stamp}-{os.getpid()}_{rs[0][0]}+{rs[1][0]}_vs_{rs[2][0]}+{rs[3][0]}_s{a.seed}"
    rep = REPLAYS / f'{stem}.txt'
    js = REPLAYS / f'{stem}.json'
    cmd = [str(arena_exe()), 'play', *[str(r[1]) for r in rs], '--seed', str(a.seed), '--first-kick', str(a.first_kick),
           '--names', ','.join(r[0] for r in rs), '--replay', str(rep), '--json', str(js), *limit_args(a)]
    if not a.no_stderr:
        cmd += ['--stderr-dir', str(REPLAYS / f'{stem}_stderr')]
    r = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace')
    if r.returncode or not js.is_file():
        print(f'arena 出错（退出码 {r.returncode}）：', r.stdout, r.stderr)
        return 1
    try:
        g = json.loads(js.read_text(encoding='utf-8', errors='replace'))
    except (OSError, ValueError) as e:
        print(f'结果文件读不懂或没写完整：{js}（{e}）')
        return 1
    if not replay_complete(rep):
        print(f'回放文件不存在或没写完整（缺 END 行）：{rep}')
        return 1
    show_game(g)
    print(f'回放：{rep}')
    if not a.no_stderr:
        print(f"程序的标准错误输出：{REPLAYS / (stem + '_stderr')}\\g0_p<球员号>.txt")
    print(f'看回放：python replay.py info "{rep}"  或用浏览器打开 viewer.html 把回放拖进去')
    return 0


class ArenaError(Exception):
    """arena 失败或结果不完整。partial = 已经收到的结果（可能残缺）。"""

    def __init__(self, msg, partial=None):
        super().__init__(msg)
        self.partial = partial or []


def replay_complete(path):
    """回放文件存在且以 END 行结尾（arena 写完整了）。"""
    try:
        with open(path, 'rb') as f:
            f.seek(0, os.SEEK_END)
            n = f.tell()
            f.seek(max(0, n - 4096))
            tail = f.read().decode('utf-8', errors='replace').rstrip().splitlines()
    except OSError:
        return False
    return bool(tail) and tail[-1].startswith('END ')


def check_results(res, planned):
    """核对收到的局号集合与计划完全相同、没有重复。返回问题说明列表（空 = 完整）。"""
    ids = [g.get('id') for g in res]
    probs = []
    if not res:
        probs.append('没有收到任何一局的结果')
    dup = sorted({i for i in ids if ids.count(i) > 1})
    if dup:
        probs.append(f'重复的局号：{dup}')
    miss = sorted(set(planned) - set(ids))
    if miss:
        probs.append(f'缺少的局号：{miss}')
    extra = sorted(set(ids) - set(planned), key=str)
    if extra:
        probs.append(f'计划外的局号：{extra}')
    return probs


def run_batch(entries, games, workers, extra, replay_dir=None, stderr_dir=None):
    """entries: [(名字, exe)]；games: [(局号, 种子, 开球队, [4 个 entries 下标])] → 结果列表。
    arena 非零退出、结果读不懂、局号不全或重复、回放没写完整：抛 ArenaError（partial 带上已收到的结果）。"""
    lines = [f'BOT {i} {n} {p}' for i, (n, p) in enumerate(entries)]
    lines += [f'GAME {gid} {seed} {fk} ' + ' '.join(str(x) for x in seats) for gid, seed, fk, seats in games]
    cmd = [str(arena_exe()), 'batch', '--workers', str(workers), *with_sandbox(extra)]
    if replay_dir:
        cmd += ['--replay-dir', str(replay_dir)]
    if stderr_dir:
        cmd += ['--stderr-dir', str(stderr_dir)]
    r = subprocess.run(cmd, input='\n'.join(lines) + '\n', capture_output=True, text=True, encoding='utf-8',
                       errors='replace')
    out = []
    bad_json = 0
    for ln in r.stdout.splitlines():
        if ln.startswith('{'):
            try:
                out.append(json.loads(ln))
            except ValueError:
                bad_json += 1
        elif ln.strip():
            print(ln)
    if r.returncode:
        raise ArenaError(f'arena 出错（退出码 {r.returncode}）：{r.stderr[-2000:]}', out)
    if bad_json:
        raise ArenaError(f'arena 输出里有 {bad_json} 行结果读不懂', out)
    probs = check_results(out, [g[0] for g in games])
    if probs:
        raise ArenaError('结果不完整：' + '；'.join(probs), out)
    if replay_dir:
        broken = [g[0] for g in games if not replay_complete(Path(replay_dir) / f'g{g[0]}.txt')]
        if broken:
            raise ArenaError(f'这些局的回放文件不存在或没写完整（缺 END 行）：{broken}', out)
    return out


def cmd_run(a):
    me = resolve(a.me)
    mate = resolve(a.mate) if a.mate else me
    opps = [s.strip() for s in a.opp.split(',') if s.strip()]
    if len(opps) == 1:
        opps *= 2
    o1, o2 = resolve(opps[0]), resolve(opps[1])
    entries = [me, mate, o1, o2]
    games = []
    meta = {}
    gid = 0
    for k in range(a.seeds):
        seed = a.seed_base + k
        mine = [0, 1] if k % 2 == 0 else [1, 0]      # 偶数种子我是队内 0 号
        for side in (0, 1):
            seats = mine + [2, 3] if side == 0 else [2, 3] + mine
            games.append((gid, seed, 0, seats))        # 第一次开球总是 A 队：换边后双方各先开球一次（物理左右镜像，换边本身不改变比赛）
            meta[gid] = (side, seed, mine[0])
            gid += 1
    stamp = datetime.datetime.now().strftime('%m%d-%H%M%S')
    rdir = REPLAYS / f'run-{stamp}-{os.getpid()}'
    rdir.mkdir(parents=True, exist_ok=True)
    workers = a.workers or max(1, min(len(games), (os.cpu_count() or 2) // 2))
    print(f"我方：{me[0]} + {mate[0]}   对手：{o1[0]} + {o2[0]}   {len(games)} 局（{a.seeds} 个种子 × 换边），并行 {workers}")
    incomplete = None
    try:
        res = run_batch(entries, games, workers, limit_args(a), rdir, None if a.no_stderr else rdir / 'stderr')
    except ArenaError as e:
        incomplete = str(e)
        planned = set(meta)
        seen = set()
        res = []
        for g in e.partial:                       # 只统计计划内、不重复的局
            if g.get('id') in planned and g['id'] not in seen:
                seen.add(g['id'])
                res.append(g)
    pts = net = gf = ga = 0
    w = d = l = 0
    faults = {}
    maxms = {}
    rows = []
    for g in sorted(res, key=lambda x: x['id']):
        side, seed, my_idx = meta[g['id']]
        my, op = (g['score'][0], g['score'][1]) if side == 0 else (g['score'][1], g['score'][0])
        pts += 1 if my > op else 0.5 if my == op else 0
        w += my > op
        d += my == op
        l += my < op
        net += my - op
        gf += my
        ga += op
        for p in ((0, 1) if side == 0 else (2, 3)):
            pl = g['players'][p]
            key = pl['name'] + ('（我）' if (p % 2 == 0) == (my_idx == 0) else '（队友）')
            f = faults.setdefault(key, dict(timeouts=0, invalid=0, dead=0))
            f['timeouts'] += pl['timeouts']
            f['invalid'] += pl['invalid']
            f['dead'] += pl['status'] != 'ok'
            maxms[key] = max(maxms.get(key, 0), pl['max_ms'])
        rows.append(f"  g{g['id']:<3d} 种子 {seed:<4d} 我方{'A 队（守左门）' if side == 0 else 'B 队（守右门）'}  我是队内 {my_idx} 号  "
                    f"{my}:{op}  {'胜' if my > op else '平' if my == op else '负'}")
    n = len(res)
    if incomplete:
        print(f'！不完整：{incomplete}')
    if not n:
        print('没有完成任何一局')
        return 1
    for r_ in rows:
        print(r_)
    tag = f'（不完整：计划 {len(games)} 局，只收到 {n} 局，下面的统计不能当作完整测试）' if incomplete else ''
    print(f'合计 {n} 局{tag}：得分率 {100 * pts / n:.1f}%  胜平负 {w}/{d}/{l}  进 {gf} 失 {ga}  场均净胜 {net / n:+.2f}')
    for k, f in faults.items():
        bad = f"超时 {f['timeouts']} 次  非法输出 {f['invalid']} 次  中途被结束/崩溃 {f['dead']} 局" if any(f.values()) else '无故障'
        print(f'  {k}: {bad}  单步 CPU 最多 {maxms[k]:.3f} ms')
    print(f'回放：{rdir}\\g<局号>.txt   看回放：python replay.py info <回放文件>')
    return 1 if incomplete else 0


def cmd_check(a):
    try:
        me = resolve(a.prog)
    except build.BuildError as e:
        print(e)
        return 1
    if not BUILTIN['baseline'].is_file():
        print(f"找不到基准程序：{BUILTIN['baseline']}")
        return 1
    print(f'编译 / 找到程序：{me[1]}')
    entries = [me, BUILTIN and ('baseline', BUILTIN['baseline'])]
    REPLAYS.mkdir(exist_ok=True)
    stamp = datetime.datetime.now().strftime('%m%d-%H%M%S')
    rdir = REPLAYS / f'check-{stamp}-{os.getpid()}'
    planned = [(0, 1, 0, [0, 0, 1, 1]), (1, 2, 1, [1, 1, 0, 0])]
    ok = True
    try:
        res = run_batch(entries, planned, 2, limit_args(a), rdir, rdir / 'stderr')
    except ArenaError as e:
        print(f'！对局没有全部完成：{e}')
        ok = False
        res = [g for g in e.partial if g.get('id') in (0, 1)]
    probs = check_results(res, [g[0] for g in planned])
    if probs:
        print('！' + '；'.join(probs))
        ok = False
    seen = set()
    for g in sorted(res, key=lambda x: x['id']):
        if g['id'] in seen:
            continue
        seen.add(g['id'])
        print(f"第 {g['id'] + 1} 局（你在 {'A' if g['id'] == 0 else 'B'} 队）：")
        show_game(g)
        for p in ((0, 1) if g['id'] == 0 else (2, 3)):
            pl = g['players'][p]
            if pl['timeouts'] or pl['invalid'] or pl['status'] != 'ok':
                ok = False
    print('检查通过：没有超时、非法输出、崩溃。' if ok and res else '有问题，见上面的 "！" 行；调试输出在 ' + str(rdir / 'stderr'))
    print(f'回放：{rdir}')
    return 0 if ok else 1


def main():
    if hasattr(sys.stdout, 'reconfigure'):
        try:
            sys.stdout.reconfigure(encoding='utf-8')
        except Exception:
            pass
    ap = argparse.ArgumentParser(description='足球本地对战工具（详见文件开头说明或 README.md）')
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('play')
    p.add_argument('players', nargs='+')
    p.add_argument('--seed', type=int, default=1)
    p.add_argument('--first-kick', type=int, default=0)
    r = sub.add_parser('run')
    r.add_argument('--me', required=True)
    r.add_argument('--mate')
    r.add_argument('--opp', default='baseline')
    r.add_argument('--seeds', type=int, default=8)
    r.add_argument('--seed-base', type=int, default=1)
    r.add_argument('--workers', type=int, default=0)
    c = sub.add_parser('check')
    c.add_argument('prog')
    for s in (p, r, c):
        s.add_argument('--step-ms', type=float, default=10)
        s.add_argument('--no-stderr', action='store_true')
        s.add_argument('--sandbox', choices=['none', 'appcontainer'], default='none')
    a = ap.parse_args()
    try:
        sys.exit({'play': cmd_play, 'run': cmd_run, 'check': cmd_check}[a.cmd](a))
    except (build.BuildError, ArenaError) as e:
        print(e)
        sys.exit(1)
    except OSError as e:
        print(f'读写文件出错：{e}')
        sys.exit(1)


if __name__ == '__main__':
    main()

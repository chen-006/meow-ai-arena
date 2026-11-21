"""本地对战工具：用你的飞镖乱斗程序、别人的程序和参考程序打对局、出统计。对局由 arena.exe（与正式比赛同一个裁判）完成。

程序的写法：
  template        template.cpp（最简单的合法程序，很弱）
  weak            bots\\weak.exe（弱档陪练，只有可执行文件）
  mid             bots\\mid.exe（中档陪练，只有可执行文件）
  strong          bots\\strong.exe（强档陪练，只有可执行文件）
  路径.cpp        自动编译（与正式比赛相同的编译方式，结果缓存在 build\\）
  路径.exe        直接用

三种形式（--mode）：0 duel 1v1（2 人）  1 ffa 四人各自为战（4 人）  2 team 4v4（8 人；座位 0..3 一队、4..7 一队）

命令：
  python match.py check <程序>
      编译；1v1 对 weak 打 2 局（你分别坐 0、1 号）、四人局（你 + 3 个 weak）1 局、4v4（你 + 3 个 weak 对 4 个 weak）1 局，
      正式计时。报告胜负、超时、非法输出、崩溃、每步用时（含第 0 步）。
  python match.py duel <A> <B> [--games 20] [--seed-base 1] [--workers K] [--cfg K=V ...] [--map 名字 ...] [--replays]
      1v1：地图轮流用，每张图每个种子打两局（A 坐 0 号一次、B 坐 0 号一次），--games 是总局数（取偶数，向上凑）。
      种子决定出生点的小抖动（同一张图打出不同的局）。
      输出 A 胜 / B 胜 / 平、平均局长（秒）、各自的出局方式分布、扔 / 砍 / 踢 / 冲刺 / 召回的平均次数、故障、单步 CPU 最多用了多少。
  python match.py ffa <A> [<B> <C> <D>] [--fill mid] [--games 20] ...
      四人各自为战：不足 4 个用 --fill 补。每 4 局用同一张图、同一个种子，座位整体轮转（--games 取 4 的倍数，每个程序坐遍每个座位）。
      输出每个程序（同名的分别编号 mid#1）：平均名次、各名次次数、平均得分（积分表 FFA_POINTS，并列平分；外加每次击杀 KILL_POINTS）、
      平均击杀、出局方式。
  python match.py team <A1> [A2 A3 A4] --vs <B1> [B2 B3 B4] [--fill-a X] [--fill-b Y] [--games 20] ...
      4v4："我的程序 + 队友的程序"混搭：A 队最多 4 个程序，不足用 --fill-a（默认与 A1 相同）补；B 队同理（默认 mid）。
      每个种子打两局（A 队坐 0..3 一次、坐 4..7 一次），--games 是总局数（取偶数，向上凑）。
      输出两队胜 / 负 / 平、平均局长、每个程序的击杀、误伤队友次数（k_team）、出局方式。
  python match.py play <程序...>（个数 = 该形式人数）[--mode 0|1|2] [--map 名字] [--seed 1] [--cfg K=V ...]
      打一局，存回放（replays\\），显示每人的结果表。不写 --mode 时按程序个数定（2 → 0，4 → 1，8 → 2）。
      不写 --map 用 maps\\ 里第一张支持这种形式的图（按名字排序）。
  共同选项：--step-ms（默认 10，正式值）--no-stderr（不保存程序的标准错误输出）--workers K（并行局数）
            duel / ffa / team 默认不存回放，加 --replays 才存（replays\\ 下按局号）；check / play 默认存，加 --no-replay 不存。
  时间一律显示成秒（步数 ÷ 20，保留 1 位小数）。
"""
import argparse
import datetime
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

KIT = Path(__file__).resolve().parent
sys.path.insert(0, str(KIT))
import build  # noqa: E402

# 四人局的积分表（第 1..4 名；并列的名次平分这几档的分数）和每次击杀的加分。原型负责人以后会改这两个常量。
FFA_POINTS = [3, 2, 1, 0]
KILL_POINTS = 1

TICK = 20                                   # 每秒步数
MODE_N = [2, 4, 8]                          # 各形式人数
MODE_NAME = ['duel', 'ffa', 'team']
MODE_CN = ['1v1', '四人各自为战', '4v4']
ARENA = KIT / 'arena.exe'
REPLAYS = KIT / 'replays'
BUILD_HINT = '开源版不带编译好的程序：先在上一层（boomerang\\）运行 python build_engine.py，生成裁判 arena.exe 和三档陪练 bots\\*.exe'  # 开源版加的提示
BUILTIN = {'template': KIT / 'template.cpp', 'weak': KIT / 'bots' / 'weak.exe', 'mid': KIT / 'bots' / 'mid.exe',
           'strong': KIT / 'bots' / 'strong.exe'}  # 三档陪练只发可执行文件
STATUS_CN = {'crash': '程序退出/崩溃', 'lost': '失去响应被结束', 'timeout_limit': '累计超时过多被结束',
             'start_fail': '启动失败', 'output_limit': '输出超量被结束'}
CAUSE_CN = {'none': '存活', 'dart': '被镖', 'slash': '被砍', 'fall': '落坑'}
CAUSES = ['none', 'dart', 'slash', 'fall']
ACTS = [('throws', '扔'), ('slashes', '砍'), ('kicks', '踢'), ('dashes', '冲刺'), ('recalls', '召回')]
MAPS = KIT / 'maps'


def map_modes(path):
    for ln in Path(path).read_text(encoding='utf-8', errors='replace').splitlines():
        w = ln.split()
        if w and w[0] == 'MODES':
            return {int(x) for x in w[1:] if x in ('0', '1', '2')}
    return set()


def resolve_maps(a, mode):
    """--map 名字或文件（可多个）→ 地图文件列表；不写 = maps\\ 里所有支持这种形式的图（按名字排序）"""
    names = getattr(a, 'map', None) or []
    out = []
    if not names:
        out = [p for p in sorted(MAPS.glob('*.txt')) if mode in map_modes(p)]
        if not out:
            raise build.BuildError(f'maps\\ 里没有支持形式 {mode}（{MODE_NAME[mode]}）的地图')
        return out
    for nm in names:
        p = Path(nm)
        if not p.suffix:
            p = MAPS / f'{nm}.txt'
        if not p.is_file():
            raise build.BuildError(f'找不到地图：{nm}（写 maps\\ 里的名字，例如 twinlakes，或者地图文件的路径）')
        if mode not in map_modes(p):
            raise build.BuildError(f'地图 {nm} 不支持形式 {mode}（{MODE_NAME[mode]}）：看地图文件的 MODES 行')
        out.append(p.resolve())
    return out


def map_label(p):
    return Path(p).stem


class ArenaError(Exception):
    """arena 失败或结果不完整。partial = 已经收到的结果（可能残缺）。"""

    def __init__(self, msg, partial=None):
        super().__init__(msg)
        self.partial = partial or []


def warn_stale_engine():
    """开发目录里（选手包旁边有 引擎\\）：选手包里的 dart.hpp / arena.cpp 和引擎目录的不一样时提醒一声。"""
    eng = KIT.parent / '引擎'
    for f in ('dart.hpp', 'arena.cpp'):
        a, b = KIT / f, eng / f
        if a.is_file() and b.is_file() and a.read_bytes() != b.read_bytes():
            print(f'注意：选手包里的 {f} 和 引擎\\{f} 不一样（引擎改过还没同步？可以先跑 引擎\\打包.py）')


def arena_exe():
    srcs = [KIT / 'arena.cpp', KIT / 'dart.hpp']
    if ARENA.is_file():
        # arena.exe 里编进了规则引擎：比 arena.cpp / dart.hpp 旧时先试着重新编译，编不了就照用旧的并提醒
        mt = ARENA.stat().st_mtime
        if all(s.is_file() for s in srcs) and any(s.stat().st_mtime > mt + 1 for s in srcs):
            try:
                return compile_arena()
            except build.BuildError as e:
                print(f'注意：arena.exe 比 arena.cpp / dart.hpp 旧，重新编译失败，先用旧的（{str(e)[:200]}）')
        return ARENA
    return compile_arena()


def compile_arena():
    # 从源码编译 arena.exe（需要 g++）。复制到纯英文临时目录再编，避开路径里的中文
    cxx = build.find_compiler()
    if not cxx:
        raise build.BuildError('没有 arena.exe，也没有找到 g++ 来编译它。' + BUILD_HINT)
    for f in ('arena.cpp', 'dart.hpp'):
        if not (KIT / f).is_file():
            raise build.BuildError(f'没有 arena.exe，也没有 {f}，编译不了裁判程序。' + BUILD_HINT)
    print('编译裁判程序 arena.exe ...')
    work = Path(tempfile.mkdtemp(prefix='arena-'))
    try:
        for f in ('arena.cpp', 'dart.hpp'):
            shutil.copyfile(KIT / f, work / f)
        env = dict(os.environ)
        env['PATH'] = str(Path(cxx).resolve().parent) + os.pathsep + env.get('PATH', '')
        r = subprocess.run([cxx, '-std=c++17', '-O2', '-static', 'arena.cpp', '-o', 'arena.exe', '-lshell32', '-luserenv',
                            '-ladvapi32'], cwd=work, capture_output=True, text=True, encoding='utf-8', errors='replace',
                           env=env)
        if r.returncode or not (work / 'arena.exe').is_file():
            raise build.BuildError('编译 arena.cpp 失败：\n' + (r.stderr or r.stdout)[-3000:])
        shutil.copyfile(work / 'arena.exe', ARENA)
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return ARENA


# ---------------------------------------------------------------- 程序

_resolved = {}


def resolve_prog(spec):
    """程序写法 → (显示名, exe 路径)。同一个写法只编译一次。"""
    key = spec.strip()
    if key in _resolved:
        return _resolved[key]
    low = key.lower()
    if low in BUILTIN:
        src = BUILTIN[low]
        if src.suffix.lower() == '.exe':
            if not src.is_file():
                raise build.BuildError(f'找不到陪练 {low}：{src}\n' + BUILD_HINT)
            res = (low, src.resolve())
        elif src.is_file():
            res = (low, build.compile_cpp(src))
        else:
            raise build.BuildError(f'找不到参考程序 {low}：{src}')
    else:
        p = Path(key)
        if not p.is_file():
            raise build.BuildError(f'找不到程序：{key}（也可以写 template、weak、mid、strong）')
        name = p.stem if p.stem.lower() not in ('player', 'bot', 'main') else f'{p.parent.name}-{p.stem}'
        name = ''.join('_' if c.isspace() or c in ',:#' else c for c in name) or 'bot'
        if p.suffix.lower() in ('.cpp', '.cc', '.cxx'):
            res = (name, build.compile_cpp(p))
        elif p.suffix.lower() == '.exe':
            res = (name, p.resolve())
        else:
            raise build.BuildError(f'程序要是 .cpp 或 .exe：{key}')
    _resolved[key] = res
    return res


def label_progs(progs):
    """[(名字, exe)] → 显示名列表；同名的多个分别编号 mid#1 mid#2 ..."""
    names = [n for n, _ in progs]
    seen = {}
    out = []
    for n in names:
        if names.count(n) > 1:
            seen[n] = seen.get(n, 0) + 1
            out.append(f'{n}#{seen[n]}')
        else:
            out.append(n)
    return out


# ---------------------------------------------------------------- 调 arena

def common_args(a):
    args = ['--step-ms', str(a.step_ms), '--sandbox', a.sandbox]
    for kv in a.cfg or []:
        if kv.upper().startswith('MODE='):
            raise build.BuildError('形式由命令定（duel / ffa / team / play --mode），不要写 --cfg MODE=...')
        args += ['--cfg', kv]
    return args


def replay_complete(path, n):
    """回放文件存在、有 END 行、END 之后恰好 n 行 RESULT（arena 写完整了）。"""
    try:
        with open(path, 'rb') as f:
            f.seek(0, os.SEEK_END)
            size = f.tell()
            f.seek(max(0, size - 4096))
            tail = f.read().decode('utf-8', errors='replace').rstrip().splitlines()
    except OSError:
        return False
    ends = [i for i, s in enumerate(tail) if s.startswith('END ')]
    return bool(ends) and all(s.startswith('RESULT ') for s in tail[ends[-1] + 1:]) and len(tail) - ends[-1] - 1 == n


def check_results(res, planned):
    """planned：{局号: 人数}。核对收到的局号集合与计划完全相同、没有重复、每局人数齐全。返回问题说明列表（空 = 完整）。"""
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
    short = sorted(g.get('id') for g in res if g.get('id') in planned and len(g.get('players', [])) != planned[g['id']])
    if short:
        probs.append(f'这些局的人数不对：{short}')
    return probs


def good_partial(partial, planned):
    """从残缺结果里挑出计划内、不重复、人数齐全的局"""
    seen, out = set(), []
    for g in partial:
        i = g.get('id')
        if i in planned and i not in seen and len(g.get('players', [])) == planned[i]:
            seen.add(i)
            out.append(g)
    return out


def run_batch(entries, games, workers, extra, replay_dir=None, stderr_dir=None):
    """entries: [(名字, exe)]；games: [(局号, 种子, [entries 下标...], 形式)] → 结果列表。
    arena 非零退出、结果读不懂、局号不全或重复、回放没写完整：抛 ArenaError（partial 带上已收到的结果）。"""
    lines = [f'BOT {i} {n} {p}' for i, (n, p) in enumerate(entries)]
    for gid, seed, seats, mode, mp in games:
        lines.append(f'GAME {gid} {seed} ' + ','.join(str(b) for b in seats) + f' MODE={mode} MAP={mp}')
    cmd = [str(arena_exe()), 'batch', '--workers', str(workers), *extra]
    if replay_dir:
        Path(replay_dir).mkdir(parents=True, exist_ok=True)
        cmd += ['--replay-dir', str(replay_dir)]
    if stderr_dir:
        Path(stderr_dir).mkdir(parents=True, exist_ok=True)   # arena 只建最后一层目录
        cmd += ['--stderr-dir', str(stderr_dir)]
    r = subprocess.run(cmd, input='\n'.join(lines) + '\n', capture_output=True, text=True, encoding='utf-8',
                       errors='replace')
    out = []
    bad_json = 0
    fatal = []
    for ln in r.stdout.splitlines():
        if ln.startswith('{'):
            try:
                out.append(json.loads(ln))
            except ValueError:
                bad_json += 1
        elif ln.strip():
            fatal.append(ln)
    if r.returncode:
        msg = '；'.join(fatal) or r.stderr[-2000:].strip()
        raise ArenaError(f'arena 出错（退出码 {r.returncode}）：{msg}', out)
    if fatal:
        print('arena 的其他输出：' + '；'.join(fatal))
    if r.stderr.strip():
        print('arena：' + r.stderr.strip()[-1000:])
    if bad_json:
        raise ArenaError(f'arena 输出里有 {bad_json} 行结果读不懂', out)
    probs = check_results(out, {g[0]: MODE_N[g[3]] for g in games})
    if probs:
        raise ArenaError('结果不完整：' + '；'.join(probs), out)
    if replay_dir:
        broken = [g[0] for g in games if not replay_complete(Path(replay_dir) / f'g{g[0]}.txt', MODE_N[g[3]])]
        if broken:
            raise ArenaError(f'这些局的回放文件不存在或没写完整（缺 END / RESULT 行）：{broken}', out)
    return out


def entries_for(progs):
    """[(名字, exe)] → 去重后的程序表 [(名字, exe)] 和每个座位对应的下标"""
    entries, idx, seats = [], {}, []
    for n, exe in progs:
        k = str(exe)
        if k not in idx:
            idx[k] = len(entries)
            entries.append((n, exe))
        seats.append(idx[k])
    return entries, seats


# ---------------------------------------------------------------- 显示

def sec(steps):
    return f'{steps / TICK:.1f}'


def fault_text(p):
    bad = []
    if p['timeouts']:
        bad.append(f"超时 {p['timeouts']} 次")
    if p['invalid']:
        bad.append(f"非法输出 {p['invalid']} 次")
    if p['status'] != 'ok':
        bad.append(STATUS_CN.get(p['status'], p['status']) + f"（第 {p['dead_step']} 步起什么都不做）")
    if not bad:
        return ''
    s = '！' + '，'.join(bad)
    if p['faults']:
        f0 = p['faults'][0]
        s += f'  第一次：第 {f0[0]} 步 {f0[1]} {f0[2]}'
    return s


def p_bad(p):
    return bool(p['timeouts'] or p['invalid'] or p['status'] != 'ok')


def out_text(p):
    """出局情况：存活 / 第几秒 被谁怎么打出局"""
    if p['alive']:
        return '存活'
    s = f"第 {sec(p['dead_t'])} 秒{CAUSE_CN.get(p['cause_name'], p['cause_name'])}"
    if p['killer'] >= 0:
        s += f"（{p['killer']} 号）"
    return s


def winner_text(g, labels=None):
    w = g['winner']
    if w < 0:
        return '平局'
    if g['mode'] == 2:
        return f'{w} 队胜（座位 {"0..3" if w == 0 else "4..7"}）'
    return f"{w} 号 {labels[w] if labels else g['players'][w]['name']} 胜"


def game_head(g, labels=None):
    return (f"形式 {g['mode_name']}（{MODE_CN[g['mode']]}）种子 {g['seed']}  {g['steps']} 步 = {sec(g['steps'])} 秒  "
            f"{winner_text(g, labels)}")


def show_players(g, labels=None, mark=None):
    """每人一行的结果表。labels[seat] = 显示名；mark = 要标"（我）"的座位集合"""
    print(game_head(g, labels))
    for p in sorted(g['players'], key=lambda c: (c['rank'], c['seat'])):
        nm = (labels[p['seat']] if labels else p['name']) + ('（我）' if mark and p['seat'] in mark else '')
        acts = ' '.join(f"{lab}{p[k]}" for k, lab in ACTS)
        line = (f"  第 {p['rank']} 名  {p['seat']} 号 队{p['team']} {nm:<14s} {out_text(p):<16s} 击杀 {p['kills']} "
                f"误伤 {p['k_team']}  {acts}  CPU 每步平均 {p['avg_ms']:.3f} 最多 {p['max_ms']:.3f} ms 第 0 步 {p['first_ms']:.1f} ms")
        f = fault_text(p)
        if f:
            line += '  ' + f
        print(line)


def stamp():
    return datetime.datetime.now().strftime('%m%d-%H%M%S') + f'-{os.getpid()}'


def default_workers(n_games, players_per_game):
    per = max(1, players_per_game)
    return max(1, min(n_games, (os.cpu_count() or 8) // per))


def new_stat():
    return dict(games=0, win=0, loss=0, draw=0, rank=0, ranks=[0, 0, 0, 0], pts=0.0, kills=0, k_team=0, steps=0,
                causes={c: 0 for c in CAUSES}, acts={k: 0 for k, _ in ACTS}, faults=0, dead=0, maxms=0.0, firstms=0.0)


def accumulate(s, p, g):
    s['games'] += 1
    s['kills'] += p['kills']
    s['k_team'] += p['k_team']
    s['causes'][p['cause_name'] if p['cause_name'] in CAUSES else 'none'] += 1
    for k, _ in ACTS:
        s['acts'][k] += p[k]
    s['faults'] += p['faults_total']
    s['dead'] += p['status'] != 'ok'
    s['maxms'] = max(s['maxms'], p['max_ms'])
    s['firstms'] = max(s['firstms'], p['first_ms'])


def cause_line(s):
    return ' '.join(f"{CAUSE_CN[c]} {s['causes'][c]}" for c in CAUSES)


def act_line(s):
    n = max(1, s['games'])
    return '  '.join(f"{lab} {s['acts'][k] / n:.1f}" for k, lab in ACTS)


def fault_line(s):
    return f"！故障 {s['faults']} 次，中途被结束/崩溃 {s['dead']} 局" if s['faults'] or s['dead'] else '无故障'


def timeup(g):
    """到时限还有两方以上活着（按队伍算）"""
    return len({p['team'] for p in g['players'] if p['alive']}) >= 2


def pts_for_ranks(players):
    """四人局积分：名次对应 FFA_POINTS，并列的人平分他们占的那几档；返回 {座位: 分}。"""
    out = {}
    byr = {}
    for p in players:
        byr.setdefault(p['rank'], []).append(p['seat'])
    for r, seats in byr.items():
        slots = [FFA_POINTS[i] if i < len(FFA_POINTS) else 0 for i in range(r - 1, r - 1 + len(seats))]
        for s in seats:
            out[s] = sum(slots) / len(seats)
    return out


def execute(entries, games, a, planned_n, tag, workers=None, replays_dir=None):
    """跑一批并处理不完整：返回 (结果列表, 不完整说明或 None, 回放目录, stderr 目录)"""
    base = REPLAYS / f'{tag}-{stamp()}'
    rdir = base if a.replays else None
    sdir = None if a.no_stderr else base / 'stderr'
    planned = {g[0]: MODE_N[g[3]] for g in games}
    incomplete = None
    try:
        res = run_batch(entries, games, workers, common_args(a), rdir, sdir)
    except ArenaError as e:
        incomplete = str(e)
        res = good_partial(e.partial, planned)
    return res, incomplete, rdir, sdir


def finish(a, incomplete, rdir, sdir, n_games, n_res, id_label='g<局号>'):
    if rdir:
        print(f'回放：{rdir}\\{id_label}.txt')
    if sdir:
        print(f'程序的标准错误输出：{sdir}\\g<局号>_p<座位号>.txt')
    if incomplete:
        print(f'！不完整：{incomplete}')
        print(f'（计划 {n_games} 局，只收到 {n_res} 局，上面的统计不能当作完整测试）')
        return 1
    return 0


# ---------------------------------------------------------------- 命令

def cmd_check(a):
    me = resolve_prog(a.prog)
    weak = resolve_prog('weak')
    print(f'编译 / 找到程序：{me[1]}')
    entries, (mi, wi) = entries_for([me, weak])
    REPLAYS.mkdir(exist_ok=True)
    rdir = REPLAYS / f'check-{stamp()}'
    # (局号, 种子, 座位上的程序, 形式, 我的座位, 说明)
    m0, m1, m2 = resolve_maps(a, 0)[0], resolve_maps(a, 1)[0], resolve_maps(a, 2)[0]
    plan = [(0, 1, [mi, wi], 0, 0, f'1v1 对 weak，你坐 0 号，地图 {map_label(m0)}', m0),
            (1, 1, [wi, mi], 0, 1, f'1v1 对 weak，你坐 1 号，地图 {map_label(m0)}', m0),
            (2, 3, [mi, wi, wi, wi], 1, 0, f'四人局：你 + 3 个 weak，地图 {map_label(m1)}', m1),
            (3, 4, [mi, wi, wi, wi, wi, wi, wi, wi], 2, 0, f'4v4：你 + 3 个 weak（0 队）对 4 个 weak（1 队），地图 {map_label(m2)}', m2)]
    games = [(i, s, seats, m, mp) for i, s, seats, m, _, _, mp in plan]
    planned = {g[0]: MODE_N[g[3]] for g in games}
    ok = True
    try:
        res = run_batch(entries, games, 2, common_args(a), None if a.no_replay else rdir,
                        None if a.no_stderr else rdir / 'stderr')
    except ArenaError as e:
        print(f'！对局没有全部完成：{e}')
        ok = False
        res = good_partial(e.partial, planned)
    probs = check_results(res, planned)
    if probs:
        print('！' + '；'.join(probs))
        ok = False
    step_avg, step_max, first_max = [], 0.0, 0.0
    for g in sorted(res, key=lambda x: x['id']):
        _, _, seats, m, seat, desc, _ = plan[g['id']]
        print(f"第 {g['id'] + 1} 局（{desc}）：")
        labels, k = [], 0
        for q in range(MODE_N[m]):
            if q == seat:
                labels.append(me[0])
            else:
                k += 1
                labels.append(f'weak#{k}')
        show_players(g, labels, mark={seat})
        p = g['players'][seat]
        if m == 0:
            won = g['winner'] == seat
        elif m == 1:
            won = p['rank'] == 1 and p['alive']
        else:
            won = g['winner'] == 0
        print(f"  -> 你{'胜' if won else ('平' if g['winner'] < 0 else '负')}"
              f"{'（四人局名次 第 %d 名）' % p['rank'] if m == 1 else ''}，击杀 {p['kills']}，"
              f"每步 CPU 平均 {p['avg_ms']:.3f} / 最多 {p['max_ms']:.3f} ms，第 0 步 {p['first_ms']:.1f} ms")
        step_avg.append(p['avg_ms'])
        step_max = max(step_max, p['max_ms'])
        first_max = max(first_max, p['first_ms'])
        if p_bad(p):
            ok = False
    if res:
        print(f"用时汇总：每步平均 {sum(step_avg) / len(step_avg):.3f} ms，单步最多 {step_max:.3f} ms（预算 {a.step_ms:g} ms），"
              f"第 0 步最多 {first_max:.1f} ms")
    if ok and res:
        print('检查通过：没有超时、非法输出、崩溃。（胜负只是参考：对手是 weak）')
    else:
        print('有问题，见上面的 "！" 行' + ('' if a.no_stderr else f'；调试输出在 {rdir / "stderr"}'))
    if not a.no_replay:
        print(f'回放：{rdir}\\g<局号>.txt')
    return 0 if ok and res else 1


def games_count(a, per):
    """--games → 实际局数（凑成 per 的倍数向上取整）"""
    n = max(1, a.games)
    m = (n + per - 1) // per * per
    if m != n:
        print(f'注意：--games {n} 不是 {per} 的倍数，改成 {m} 局')
    return m


def cmd_duel(a):
    A, B = resolve_prog(a.A), resolve_prog(a.B)
    progs = [A, B]
    labels = label_progs(progs)
    entries, seats = entries_for(progs)
    n = games_count(a, 2)
    maps = resolve_maps(a, 0)
    games, meta = [], {}
    for k in range(n):
        order = [0, 1] if k % 2 == 0 else [1, 0]      # 座位上是第几个程序
        j = k // 2
        games.append((k, a.seed_base + j, [seats[i] for i in order], 0, maps[j % len(maps)]))
        meta[k] = order
    workers = a.workers or default_workers(len(games), 2)
    print(f"1v1：{labels[0]} 对 {labels[1]}，{n} 局（地图 {'、'.join(map_label(m) for m in maps)} 轮流；每张图每个种子两人各坐 0 号一次；"
          f"种子 {a.seed_base}..{a.seed_base + n // 2 - 1} 决定出生点抖动），并行 {workers}")
    res, incomplete, rdir, sdir = execute(entries, games, a, 2, 'duel', workers)
    if not res:
        print('没有完成任何一局')
        finish(a, incomplete, rdir, sdir, len(games), 0)
        return 1
    st = [new_stat(), new_stat()]
    dr_up = dr_both = 0
    steps = 0
    for g in sorted(res, key=lambda x: x['id']):
        order = meta[g['id']]
        steps += g['steps']
        w = g['winner']
        if w < 0:
            if timeup(g):
                dr_up += 1
            else:
                dr_both += 1
            for i in (0, 1):
                st[i]['draw'] += 1
        else:
            st[order[w]]['win'] += 1
            st[order[1 - w]]['loss'] += 1
        for seat, p in enumerate(g['players']):
            accumulate(st[order[seat]], p, g)
    m = len(res)
    print(f"合计 {m} 局：{labels[0]} 胜 {st[0]['win']}，{labels[1]} 胜 {st[1]['win']}，平 {st[0]['draw']}"
          f"（到时限都活着 {dr_up}，同时出局 {dr_both}）；平均局长 {sec(steps / m)} 秒")
    for i in (0, 1):
        s = st[i]
        print(f"  {labels[i]:<14s} 胜 {s['win']:3d} 负 {s['loss']:3d} 平 {s['draw']:3d}  出局方式 [{cause_line(s)}]  "
              f"平均每局 {act_line(s)}  击杀 {s['kills'] / s['games']:.2f}")
        print(f"  {'':<14s} {fault_line(s)}  单步 CPU 最多 {s['maxms']:.3f} ms（第 0 步最多 {s['firstms']:.1f} ms）")
    return finish(a, incomplete, rdir, sdir, len(games), m)


def cmd_ffa(a):
    if len(a.progs) > 4:
        print(f'ffa 最多 4 个程序（现在 {len(a.progs)} 个）')
        return 2
    progs = [resolve_prog(p) for p in a.progs]
    if len(progs) < 4:
        fill = resolve_prog(a.fill or 'mid')
        progs += [fill] * (4 - len(progs))
    labels = label_progs(progs)
    entries, seats = entries_for(progs)
    n = games_count(a, 4)
    maps = resolve_maps(a, 1)
    games, meta = [], {}
    for k in range(n):
        r = k % 4
        j = k // 4
        order = [(q + r) % 4 for q in range(4)]        # 座位 q 上是第 order[q] 个程序
        games.append((k, a.seed_base + j, [seats[i] for i in order], 1, maps[j % len(maps)]))
        meta[k] = order
    workers = a.workers or default_workers(len(games), 4)
    print('四人各自为战：' + '，'.join(labels))
    print(f"{n} 局（地图 {'、'.join(map_label(m) for m in maps)} 轮流；同一张图、同一个种子打 4 局，座位整体轮转），并行 {workers}；"
          f"积分：第 1..4 名 {'/'.join(map(str, FFA_POINTS))}（并列平分）+ 每次击杀 {KILL_POINTS}")
    res, incomplete, rdir, sdir = execute(entries, games, a, 4, 'ffa', workers)
    if not res:
        print('没有完成任何一局')
        finish(a, incomplete, rdir, sdir, len(games), 0)
        return 1
    st = [new_stat() for _ in range(4)]
    steps = 0
    nup = 0
    for g in sorted(res, key=lambda x: x['id']):
        order = meta[g['id']]
        steps += g['steps']
        nup += timeup(g)
        pts = pts_for_ranks(g['players'])
        for seat, p in enumerate(g['players']):
            s = st[order[seat]]
            accumulate(s, p, g)
            s['rank'] += p['rank']
            s['ranks'][min(p['rank'], 4) - 1] += 1
            s['pts'] += pts[seat] + KILL_POINTS * p['kills']
    m = len(res)
    print(f"合计 {m} 局，平均局长 {sec(steps / m)} 秒，到时限还有两人以上活着 {nup} 局。每个程序（按平均得分排）：")
    for i in sorted(range(4), key=lambda i: (-st[i]['pts'], st[i]['rank'], i)):
        s = st[i]
        c = s['games']
        ranks = ' '.join(str(x) for x in s['ranks'])
        print(f"  {labels[i]:<14s} 平均名次 {s['rank'] / c:4.2f}  平均得分 {s['pts'] / c:5.2f}  各名次次数 [{ranks}]  "
              f"平均击杀 {s['kills'] / c:.2f}  出局方式 [{cause_line(s)}]")
        print(f"  {'':<14s} 平均每局 {act_line(s)}  {fault_line(s)}  单步 CPU 最多 {s['maxms']:.3f} ms（第 0 步最多 {s['firstms']:.1f} ms）")
    return finish(a, incomplete, rdir, sdir, len(games), m)


def cmd_team(a):
    if len(a.progs) > 4 or len(a.vs) > 4:
        print('team 每队最多 4 个程序')
        return 2
    if not a.vs:
        print('team 要用 --vs 写出对手队')
        return 2
    A = [resolve_prog(p) for p in a.progs]
    B = [resolve_prog(p) for p in a.vs]
    fa = resolve_prog(a.fill_a) if a.fill_a else A[0]
    fb = resolve_prog(a.fill_b or 'mid')
    A += [fa] * (4 - len(A))
    B += [fb] * (4 - len(B))
    allp = A + B                                        # 下标 0..3 = A 队，4..7 = B 队
    labels = label_progs(allp)
    entries, seats = entries_for(allp)
    n = games_count(a, 2)
    maps = resolve_maps(a, 2)
    games, meta = [], {}
    for k in range(n):
        order = list(range(8)) if k % 2 == 0 else [4, 5, 6, 7, 0, 1, 2, 3]   # 座位上是第几个程序
        j = k // 2
        games.append((k, a.seed_base + j, [seats[i] for i in order], 2, maps[j % len(maps)]))
        meta[k] = order
    workers = a.workers or default_workers(len(games), 8)
    print('A 队：' + '，'.join(labels[:4]))
    print('B 队：' + '，'.join(labels[4:]))
    print(f"{n} 局（地图 {'、'.join(map_label(m) for m in maps)} 轮流；每张图每个种子 A 队坐 0..3 和 4..7 各一次），并行 {workers}")
    res, incomplete, rdir, sdir = execute(entries, games, a, 8, 'team', workers)
    if not res:
        print('没有完成任何一局')
        finish(a, incomplete, rdir, sdir, len(games), 0)
        return 1
    st = [new_stat() for _ in range(8)]
    wins = [0, 0]
    draws = 0
    steps = 0
    nup = 0
    for g in sorted(res, key=lambda x: x['id']):
        order = meta[g['id']]
        steps += g['steps']
        nup += timeup(g)
        a_team = 0 if order[0] == 0 else 1             # A 队在这一局是第几队
        w = g['winner']
        if w < 0:
            draws += 1
        else:
            wins[0 if w == a_team else 1] += 1
        for seat, p in enumerate(g['players']):
            accumulate(st[order[seat]], p, g)
    m = len(res)
    print(f"合计 {m} 局：A 队 胜 {wins[0]} 负 {wins[1]} 平 {draws}；平均局长 {sec(steps / m)} 秒；到时限双方都有人活着 {nup} 局")
    for lab, rng in (('A 队', range(0, 4)), ('B 队', range(4, 8))):
        print(f'{lab}每个程序：')
        for i in rng:
            s = st[i]
            c = s['games']
            print(f"  {labels[i]:<14s} 平均击杀 {s['kills'] / c:.2f}  误伤队友 {s['k_team']} 次（共 {c} 局）  "
                  f"出局方式 [{cause_line(s)}]  平均每局 {act_line(s)}")
            print(f"  {'':<14s} {fault_line(s)}  单步 CPU 最多 {s['maxms']:.3f} ms（第 0 步最多 {s['firstms']:.1f} ms）")
    return finish(a, incomplete, rdir, sdir, len(games), m)


def cmd_play(a):
    n = len(a.progs)
    if a.mode is None:
        if n not in MODE_N:
            print(f'play 要 2、4 或 8 个程序（现在 {n} 个），或用 --mode 指定形式')
            return 2
        mode = MODE_N.index(n)
    else:
        mode = a.mode
        if n != MODE_N[mode]:
            print(f'--mode {mode}（{MODE_NAME[mode]}）要 {MODE_N[mode]} 个程序（现在 {n} 个）')
            return 2
    progs = [resolve_prog(p) for p in a.progs]
    labels = label_progs(progs)
    mp = resolve_maps(a, mode)[0]
    REPLAYS.mkdir(exist_ok=True)
    stem = f"{stamp()}_{MODE_NAME[mode]}_{map_label(mp)}_s{a.seed}"
    rep = REPLAYS / f'{stem}.txt'
    js = REPLAYS / f'{stem}.json'
    cmd = [str(arena_exe()), 'play', *[str(exe) for _, exe in progs], '--seed', str(a.seed), '--mode', str(mode),
           '--names', ','.join(labels), '--json', str(js), '--map', str(mp), *common_args(a)]
    if not a.no_replay:
        cmd += ['--replay', str(rep)]
    if not a.no_stderr:
        (REPLAYS / f'{stem}_stderr').mkdir(parents=True, exist_ok=True)
        cmd += ['--stderr-dir', str(REPLAYS / f'{stem}_stderr')]
    print(f'地图 {map_label(mp)}，种子 {a.seed}；座位（0 号起）：' + '，'.join(f'{i} 号 {lab}' for i, lab in enumerate(labels)))
    r = subprocess.run(cmd, capture_output=True, text=True, encoding='utf-8', errors='replace')
    if r.returncode or not js.is_file():
        print(f'！arena 出错（退出码 {r.returncode}）：', r.stdout.strip(), r.stderr.strip())
        return 1
    try:
        g = json.loads(js.read_text(encoding='utf-8', errors='replace'))
    except (OSError, ValueError) as e:
        print(f'！结果文件读不懂或没写完整：{js}（{e}）')
        return 1
    if len(g.get('players', [])) != n:
        print(f'！结果里的人数不对（应为 {n}）：{js}')
        return 1
    if not a.no_replay and not replay_complete(rep, n):
        print(f'！回放文件不存在或没写完整（缺 END / RESULT 行）：{rep}')
        return 1
    show_players(g, labels)
    if r.stderr.strip():
        print(r.stderr.strip())
    if not a.no_replay:
        print(f'回放：{rep}')
    if not a.no_stderr:
        print(f"程序的标准错误输出：{REPLAYS / (stem + '_stderr')}\\g0_p<座位号>.txt")
    return 0


def main():
    if hasattr(sys.stdout, 'reconfigure'):
        try:
            sys.stdout.reconfigure(encoding='utf-8')
        except Exception:
            pass
    ap = argparse.ArgumentParser(description='飞镖乱斗本地对战工具（详见文件开头说明）')
    sub = ap.add_subparsers(dest='cmd', required=True)
    c = sub.add_parser('check')
    c.add_argument('prog', help='程序')
    c.add_argument('--no-replay', action='store_true')
    d = sub.add_parser('duel')
    d.add_argument('A')
    d.add_argument('B')
    f = sub.add_parser('ffa')
    f.add_argument('progs', nargs='+')
    f.add_argument('--fill')
    t = sub.add_parser('team')
    t.add_argument('progs', nargs='+')
    t.add_argument('--vs', nargs='+', default=[])
    t.add_argument('--fill-a')
    t.add_argument('--fill-b')
    p = sub.add_parser('play')
    p.add_argument('progs', nargs='+')
    p.add_argument('--mode', type=int, choices=[0, 1, 2])
    p.add_argument('--seed', type=int, default=1)
    p.add_argument('--no-replay', action='store_true')
    for s in (d, f, t):
        s.add_argument('--games', type=int, default=20)
        s.add_argument('--seed-base', type=int, default=1)
        s.add_argument('--workers', type=int, default=0)
        s.add_argument('--replays', action='store_true')
    for s in (c, d, f, t, p):
        s.add_argument('--map', action='append', metavar='名字或文件', help='地图（可写多次）；不写 = maps\\ 里所有支持这种形式的图')
        s.add_argument('--step-ms', type=float, default=10)
        s.add_argument('--sandbox', choices=['none', 'appcontainer'], default='none')
        s.add_argument('--no-stderr', action='store_true')
        s.add_argument('--cfg', action='append', metavar='K=V')
    a = ap.parse_args()
    warn_stale_engine()
    try:
        common_args(a)        # 先查一遍参数写法（例如 --cfg MODE=），免得编译、开跑之后才报错
        sys.exit({'check': cmd_check, 'duel': cmd_duel, 'ffa': cmd_ffa, 'team': cmd_team, 'play': cmd_play}[a.cmd](a))
    except (build.BuildError, ArenaError) as e:
        print(e)
        sys.exit(1)
    except OSError as e:
        print(f'读写文件出错：{e}')
        sys.exit(1)


if __name__ == '__main__':
    main()

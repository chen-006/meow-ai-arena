"""圈地对战本地裁判 / Local referee for the territory game.

用法 Usage:
  python engine/referee.py BOT_A BOT_B [--games N] [--seed S] [--jobs J] [--replay-dir DIR] [--stderr-dir DIR]
  python engine/referee.py BOT_A BOT_B BOT_C BOT_D [...]      (4 个 bot = 4 人混战 / melee)

bot 可以是 .cpp（按正式参数自动编译）或可执行文件，同一个文件可以重复出现。
1 对 1：同一地图交换出生位各打一局。混战：同一地图轮换座位打 4 局，每个 bot 每个座位各坐一次。
Bots are .cpp files (compiled with the official flags) or executables; a file may repeat.
1v1 games come in pairs on the same map with sides swapped; melee games come in groups of 4
on the same map with seats rotated.
"""
import argparse
import json
import os
import queue
import subprocess
import sys
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from game import Game, config_for, make_start  # noqa: E402
import build  # noqa: E402

MOVE_MS = 50          # per-turn time limit announced to bots
INIT_MS = 2000        # time limit for turn 0 (includes process start-up)
GRACE_MS = 15         # tolerance added by the referee, do not rely on it
MAX_ERRORS = 10       # timeouts + malformed replies allowed per game before forfeiting
MEM_MB = 512

if sys.platform == 'win32':
    # Windows 默认计时精度约 15.6 ms，会让响应时间测量失真；调到 1 ms
    try:
        import ctypes
        ctypes.WinDLL('winmm').timeBeginPeriod(1)
    except Exception:
        pass


def _limit_memory():
    try:
        import resource
        b = MEM_MB * 1024 * 1024
        resource.setrlimit(resource.RLIMIT_AS, (b, b))
    except Exception:
        pass


class Bot:
    def __init__(self, exe, stderr_path=None):
        self.stderr_file = open(stderr_path, 'wb') if stderr_path else subprocess.DEVNULL
        kw = {}
        if sys.platform != 'win32':
            kw['preexec_fn'] = _limit_memory
        else:
            kw['creationflags'] = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=self.stderr_file, bufsize=0, **kw)
        self.q = queue.Queue()
        self.out = queue.Queue()
        self.crashed = False
        self.errors = 0
        self.timeouts = 0
        self.max_ms = 0.0
        threading.Thread(target=self._reader, daemon=True).start()
        threading.Thread(target=self._writer, daemon=True).start()

    def _reader(self):
        try:
            for line in self.p.stdout:
                self.q.put((time.perf_counter(), line))  # 记录到达时刻，超时按到达时刻判定
        except Exception:
            pass
        self.q.put((time.perf_counter(), None))

    def _writer(self):
        # a separate thread, so a bot that stops reading stdin cannot block the referee
        while True:
            data = self.out.get()
            if data is None:
                return
            try:
                self.p.stdin.write(data)
                self.p.stdin.flush()
            except (BrokenPipeError, OSError, ValueError):
                self.crashed = True
                return

    def send(self, text):
        if not self.crashed:
            self.out.put(text.encode())

    def receive(self, turn, sent_at, limit_ms):
        """Return a direction, or None on timeout / malformed reply (counted as errors)."""
        # 两个 bot 按顺序检查，但是否超时只看回复的到达时刻，所以检查顺序不影响结果
        deadline = sent_at + (limit_ms + GRACE_MS) / 1000.0
        while not self.crashed:
            remaining = deadline - time.perf_counter()
            try:
                if remaining > 0:
                    arrived, line = self.q.get(timeout=remaining)
                else:
                    arrived, line = self.q.get_nowait()
            except queue.Empty:
                if remaining > 0:
                    continue
                self.timeouts += 1
                self.errors += 1
                return None
            if line is None:
                if arrived <= deadline:
                    self.crashed = True
                    return None
                self.q.put((arrived, None))  # 进程在时限后才退出：本回合先按超时处理
                self.timeouts += 1
                self.errors += 1
                return None
            parts = line.decode(errors='replace').split()
            if len(parts) == 2 and parts[0].lstrip('-').isdigit() and parts[1] in ('U', 'D', 'L', 'R'):
                t = int(parts[0])
                if t < turn:
                    continue  # 对之前回合的迟到回复，丢弃
                if t == turn:
                    if arrived > deadline:
                        self.timeouts += 1
                        self.errors += 1
                        return None
                    if turn > 0:  # 第 0 回合含进程启动时间，不计入
                        self.max_ms = max(self.max_ms, (arrived - sent_at) * 1000)
                    return parts[1]
            if arrived > deadline:
                self.timeouts += 1
            self.errors += 1
            return None
        return None

    def close(self):
        self.out.put(None)
        try:
            self.p.kill()
        except Exception:
            pass
        try:
            self.p.wait(timeout=2)
        except Exception:
            pass
        if self.stderr_file is not subprocess.DEVNULL:
            self.stderr_file.close()


def ranks_of(areas, forfeit):
    """名次（1 = 最好，并列同名次）。判负者的面积按 -1 计，排在所有人之后。"""
    eff = [-1 if f else a for a, f in zip(areas, forfeit)]
    return [1 + sum(e > x for e in eff) for x in eff]


def run_match(exes, names, seed, cfg=None, replay_path=None, stderr_paths=None):
    """Play one game. exes[i] plays as player i (2 bots = 1v1, 4 bots = melee). Returns a result dict."""
    n = len(exes)
    cfg = cfg or config_for(n)
    spawns, dirs = make_start(seed, cfg)
    game = Game(cfg, spawns, dirs)
    initial_owner = ''.join('.0123456789'[o + 1] for o in game.owner)
    bots = [Bot(e, (stderr_paths or [None] * n)[i]) for i, e in enumerate(exes)]
    frames = []
    forfeit = [None] * n
    try:
        for i, b in enumerate(bots):
            head = 'INIT\n%d %d %d %d %d %d %d\n' % (
                cfg['width'], cfg['height'], cfg['max_turns'], game.N, i, MOVE_MS, INIT_MS)
            head += ''.join('SPAWN %d %d %d\n' % (j, x, y) for j, (x, y) in enumerate(spawns))
            b.send(head)
        while not game.over():
            t = game.turn
            text = game.state_text(t)
            limit = INIT_MS if t == 0 else MOVE_MS
            live = [i for i in range(n) if not forfeit[i]]
            sent = {}
            for i in live:
                bots[i].send(text)
                sent[i] = time.perf_counter()
            moves = [None] * n
            for i in live:
                moves[i] = bots[i].receive(t, sent[i], limit)
            newly = []
            for i in live:
                if bots[i].crashed:
                    forfeit[i] = 'crash'
                elif bots[i].errors >= MAX_ERRORS:
                    forfeit[i] = 'errors'
                if forfeit[i]:
                    newly.append(i)
            if newly and n == 2:
                break  # 1v1：有人判负，本局立即结束
            for i in newly:
                game.eliminate(i)
                bots[i].close()
            if all(forfeit):
                break
            frame = game.step(moves)
            frame['moves'] = [m or '-' for m in moves]
            for i in newly:
                frame['events'].insert(0, {'type': 'out', 'player': i, 'reason': forfeit[i]})
                frame['trail_clear'].append(i)
            frames.append({k: v for k, v in frame.items() if v})
    finally:
        for b in bots:
            b.close()

    areas = game.areas()
    ranks = ranks_of(areas, forfeit)
    best = [i for i in range(n) if ranks[i] == 1]
    winner = best[0] if len(best) == 1 else -1
    result = {
        'mode': 'duel' if n == 2 else 'melee',
        'seed': seed, 'names': names, 'winner': winner, 'ranks': ranks, 'areas': areas, 'turns': game.turn,
        'forfeit': forfeit, 'deaths': game.deaths, 'kills': game.kills,
        'timeouts': [b.timeouts for b in bots], 'errors': [b.errors for b in bots],
        'max_ms': [round(b.max_ms, 1) for b in bots],
    }
    if replay_path:
        replay = {'version': 2, 'config': cfg, 'names': names, 'seed': seed,
                  'spawns': spawns, 'init_dirs': dirs, 'initial_owner': initial_owner,
                  'frames': frames, 'result': result}
        os.makedirs(os.path.dirname(os.path.abspath(replay_path)), exist_ok=True)
        with open(replay_path, 'w') as f:
            json.dump(replay, f, separators=(',', ':'))
    return result


def _play(job):
    return job[0], run_match(*job[1:])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('bots', nargs='+', metavar='BOT', help='2 bots = 1v1; 4 bots = melee (the same file may repeat)')
    ap.add_argument('--games', type=int, default=0,
                    help='number of games; rounded up to a multiple of 2 (1v1) or 4 (melee). '
                         'Default: 2 (1v1) or 4 (melee)')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--jobs', type=int, default=1,
                    help='games played in parallel (each game runs 2 or 4 bots at once; '
                         'too many makes bots slow and causes timeouts)')
    ap.add_argument('--replay-dir', help='save a replay JSON per game (open with viewer/viewer.html)')
    ap.add_argument('--stderr-dir', help='save bot stderr per game')
    args = ap.parse_args()

    n = len(args.bots)
    if n not in (2, 4):
        ap.error('give 2 bots (1v1) or 4 bots (melee)')
    exes = [build.resolve(b) for b in args.bots]
    labels = ['%s:%s' % ('ABCD'[i], os.path.basename(b)) for i, b in enumerate(args.bots)]
    per_map = n  # 1v1: swap sides; melee: rotate seats so every bot plays every seat
    games = max(args.games or per_map, 1)
    games = (games + per_map - 1) // per_map * per_map
    jobs = []
    for g in range(games):
        seed = args.seed + g // per_map
        rot = g % per_map
        order = [(i + rot) % n for i in range(n)]  # order[seat] = bot index
        replay = os.path.join(args.replay_dir, 'game_%d_seed%d_rot%d.json' % (g, seed, rot)) \
            if args.replay_dir else None
        errs = None
        if args.stderr_dir:
            os.makedirs(args.stderr_dir, exist_ok=True)
            errs = [os.path.join(args.stderr_dir, 'game_%d_p%d.log' % (g, i)) for i in range(n)]
        jobs.append((g, order, seed, [exes[o] for o in order], [labels[o] for o in order], seed,
                     None, replay, errs))

    points = [0.0] * n    # 1v1: win 1 / draw 0.5; melee: sum of ranks
    area_sum = [0] * n

    def report(g, order, seed, r):
        for seat, b in enumerate(order):
            area_sum[b] += r['areas'][seat]
            if n == 2:
                points[b] += 1.0 if r['winner'] == seat else 0.5 if r['winner'] == -1 else 0.0
            else:
                points[b] += r['ranks'][seat]
        if n == 2:
            outcome = 'draw' if r['winner'] == -1 else labels[order[r['winner']]] + ' wins'
        else:
            outcome = 'ranks ' + ' '.join('%s=%d' % (labels[o][0], r['ranks'][s]) for s, o in enumerate(order))
        print('game %d seed %d  %s  areas %s  deaths %s  timeouts %s  max_ms %s  forfeit %s  -> %s' % (
            g, seed, ' vs '.join(r['names']), r['areas'], r['deaths'], r['timeouts'],
            r['max_ms'], r['forfeit'], outcome), flush=True)

    if args.jobs <= 1:
        for j in jobs:
            report(j[0], j[1], j[2], run_match(*j[3:]))
    else:
        from concurrent.futures import ProcessPoolExecutor
        meta = {j[0]: j[1:3] for j in jobs}
        with ProcessPoolExecutor(args.jobs) as pool:
            for g, r in pool.map(_play, [(j[0],) + j[3:] for j in jobs]):
                report(g, meta[g][0], meta[g][1], r)
    if n == 2:
        print('TOTAL  ' + '  :  '.join('%s %.1f' % (labels[i], points[i]) for i in range(n)))
    else:
        print('TOTAL average rank (1 = best)  ' + '  '.join(
            '%s %.2f' % (labels[i], points[i] / games) for i in range(n)))
    print('average area  ' + '  '.join('%s %.0f' % (labels[i], area_sum[i] / games) for i in range(n)))


if __name__ == '__main__':
    main()

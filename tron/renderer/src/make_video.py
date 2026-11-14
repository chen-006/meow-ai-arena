"""光轮对战：一条命令把一局回放渲成 1920×1080 的 mp4（或一张静帧）。

    python renderer/src/make_video.py <回放.html|.json> [-o 输出.mp4] [--from 秒] [--to 秒] [--fps 30]
                                      [--still 回合] [--tps 4] [--slow 1.5] [--style mix|neon|flat|avatar] [--vor]
                                      [--palette 配色.json] [--names 甲,乙,...] [--sub 标题]

回放可以是仓库 tron/replays/ 里的 .html（查看器和回放打包在一起），也可以是比赛工具存的 tron-replay-1 .json。
--from / --to 是成片里的秒数（按 --tps / --slow 的播放速度算），不给就出整局；整局末尾多停 1 秒。
"""
import argparse, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import common
import tronboard as T


def main():
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass
    ap = argparse.ArgumentParser(description='光轮对战回放 → mp4 / 静帧')
    ap.add_argument('replay', help='回放 .html（仓库 replays/ 里的）或 tron-replay-1 .json')
    ap.add_argument('-o', '--out', help='输出 .mp4（出静帧时是 .png）；默认 renderer/renders/<回放名>/')
    ap.add_argument('--from', dest='t_from', type=float, help='从成片第几秒开始')
    ap.add_argument('--to', dest='t_to', type=float, help='到成片第几秒结束')
    ap.add_argument('--fps', type=int, default=30)
    ap.add_argument('--still', type=float, metavar='回合', help='只出一张静帧：第几回合（可以是小数；-1 = 终局）')
    ap.add_argument('--tps', type=float, default=4.0, help='每秒播几回合（网页查看器 1× = 4）')
    ap.add_argument('--slow', type=float, default=1.5, help='出局、抢格前后放慢到每秒几回合')
    ap.add_argument('--style', default=T.DEFAULT_STYLE, choices=list(T.STYLES), help='画风，默认 mix（视频用的霓虹 + 头像）')
    ap.add_argument('--vor', action='store_true', help='叠加地盘（离谁近就算谁的）热区，侧栏加地盘条')
    ap.add_argument('--palette', help='配色 JSON：{"colors": {...}, "labels": {...}, "avatars": {...}}')
    ap.add_argument('--names', help='按座位顺序替换显示的名字，逗号分隔')
    ap.add_argument('--sub', help='多人局的标题（默认"N 人局"）')
    ap.add_argument('--ss', type=int, default=2, help='超采样倍数（2 = 视频用的画质；1 更快）')
    a = ap.parse_args()

    if a.palette:
        T.load_palette(a.palette)
    names = [s.strip() for s in a.names.split(',')] if a.names else None
    rp = T.TronReplay(a.replay, style=a.style, names=names)
    stem = os.path.splitext(os.path.basename(a.replay))[0]
    work = os.environ.get('ARENA_WORK') or os.path.join(HERE, '..', 'renders')
    print(f'{stem}：{rp.N} 人，{rp.W}×{rp.H}，{rp.T} 回合')

    if a.still is not None:
        f = rp.T if a.still < 0 else min(a.still, rp.T)
        out = a.out or os.path.join(work, stem, f'{stem}_{f:g}.png')
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        T.frame(rp, f, style=a.style, vor=a.vor, sub=a.sub, ss=a.ss).convert('RGB').save(out)
        print('输出', out)
        return

    times = rp.play_times(0, rp.T, fps=a.fps, tps=a.tps, slow_tps=a.slow, hold=1.0)
    total = len(times) / a.fps
    t0, t1 = common.parse_range(a.t_from, a.t_to, total)
    times = times[int(round(t0 * a.fps)):int(round(t1 * a.fps))]
    out = a.out or os.path.join(work, stem, f'{stem}.mp4')
    p = common.open_video(out, T.W1080, T.H1080, a.fps)
    st, last, buf, n = time.time(), None, None, 0
    for f in times:
        if f != last:
            buf = T.frame(rp, f, style=a.style, vor=a.vor, sub=a.sub, ss=a.ss).convert('RGB').tobytes()
            last, n = f, n + 1
        p.stdin.write(buf)
    p.stdin.close()
    if p.wait() != 0:
        raise SystemExit('ffmpeg 出错')
    print(f'输出 {out}：成片 {t0:.1f}–{t1:.1f} 秒（第 {times[0]:.1f}–{times[-1]:.1f} 回合），{len(times)} 帧，'
          f'用时 {time.time() - st:.0f} 秒')


if __name__ == '__main__':
    main()

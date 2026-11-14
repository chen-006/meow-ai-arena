"""坦克大战：一条命令把一局回放渲成 1920×1080 的 mp4（或一张静帧）。

    python renderer/src/make_video.py <回放.html|.json|.json.gz> [-o 输出.mp4] [--from 秒] [--to 秒] [--fps 30]
                                      [--still 回合] [--tps 2.2] [--slow 1.0] [--style A|B|C]

--from / --to 是成片里的秒数（按 --tps / --slow 的播放速度算），不给就出整局；末尾多停 1 秒。
"""
import argparse, os, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from PIL import Image, ImageDraw
import common
import tboard as T

W, H = 1920, 1080
_bg = []


def background():
    if not _bg:
        im = Image.new('RGBA', (W, H), (12, 16, 28, 255))
        d = ImageDraw.Draw(im)
        for y in range(0, H, 4):
            d.line([(0, y), (W, y)], fill=(12 + y // 60, 16 + y // 50, 28 + y // 30, 255), width=4)
        _bg.append(im)
    return _bg[0]


def frame(rp, f, style=None, bs=900):
    img = background().copy()
    d = ImageDraw.Draw(img)
    bx, by = 90, 130
    d.text((bx, 62), f'{rp.label(0)}（下） vs {rp.label(1)}（上）', font=T.font(40), fill=(235, 238, 245), anchor='lm')
    d.text((W - 90, 62), 'AI 算法对抗大赛 · 第 6 集 · 坦克大战', font=T.font(24, 'Medium'), fill=(130, 136, 152), anchor='rm')
    img.alpha_composite(rp.draw(f, bs, style=style), (bx, by))
    img.alpha_composite(rp.scoreboard(f, 640, bs, style=style), (bx + bs + 70, by))
    return img


def main():
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass
    ap = argparse.ArgumentParser(description='坦克大战回放 → mp4 / 静帧')
    ap.add_argument('replay', help='回放 .html（仓库 replays/ 里的）或 tank-replay-1 .json / .json.gz')
    ap.add_argument('-o', '--out', help='输出 .mp4（出静帧时是 .png）；默认 renderer/renders/<回放名>/')
    ap.add_argument('--from', dest='t_from', type=float, help='从成片第几秒开始')
    ap.add_argument('--to', dest='t_to', type=float, help='到成片第几秒结束')
    ap.add_argument('--fps', type=int, default=30)
    ap.add_argument('--still', type=float, metavar='回合', help='只出一张静帧：第几回合（可以是小数；-1 = 终局）')
    ap.add_argument('--tps', type=float, default=2.2, help='每秒播几回合')
    ap.add_argument('--slow', type=float, default=1.0, help='击毁、吃星、终局前后放慢到每秒几回合')
    ap.add_argument('--style', default=None, choices=list(T.STYLES), help='A 扁平 / B 像素 / C 解说版（默认，视频用的）')
    a = ap.parse_args()

    rp = T.TReplay(a.replay)
    stem = os.path.splitext(os.path.basename(a.replay))[0].split('.')[0]
    work = os.environ.get('ARENA_WORK') or os.path.join(HERE, '..', 'renders')
    print(f'{stem}：{rp.label(0)} vs {rp.label(1)}，{rp.T} 回合')
    if a.still is not None:
        f = rp.T if a.still < 0 else min(a.still, rp.T)
        out = a.out or os.path.join(work, stem, f'{stem}_{f:g}.png')
        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        frame(rp, f, a.style).convert('RGB').save(out)
        print('输出', out)
        return
    times = rp.play_times(0, rp.T, fps=a.fps, tps=a.tps, slow_tps=a.slow, hold=1.0)
    t0, t1 = common.parse_range(a.t_from, a.t_to, len(times) / a.fps)
    times = times[int(round(t0 * a.fps)):int(round(t1 * a.fps))]
    out = a.out or os.path.join(work, stem, f'{stem}.mp4')
    p = common.open_video(out, W, H, a.fps)
    st, last, buf = time.time(), None, None
    for f in times:
        if f != last:
            buf, last = frame(rp, f, a.style).convert('RGB').tobytes(), f
        p.stdin.write(buf)
    p.stdin.close()
    if p.wait() != 0:
        raise SystemExit('ffmpeg 出错')
    print(f'输出 {out}：成片 {t0:.1f}–{t1:.1f} 秒，{len(times)} 帧，用时 {time.time() - st:.0f} 秒')


if __name__ == '__main__':
    main()

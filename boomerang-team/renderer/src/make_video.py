# -*- coding: utf-8 -*-
"""一条命令把一份回放渲染成 1080p 30 帧的 mp4（venv 的 Python 跑；见 ..\\出片.cmd）。
    python make_video.py <回放> [-o 输出.mp4] [--procs 3] [--samples 12] [--roster claude,gpt,...]
                         [--frames a:b] [--work 目录] [--keep] [--no-hud] [--demo 导演表.json]
--demo：演示模式（demo.py）：导演表决定每帧的游戏时间和镜头，不画信息栏；另写一份逐帧屏幕坐标 <输出>.coords.json。
步骤：地图预处理（按地图缓存）→ 分段并行渲染（每段一个 Blender 进程，已有的帧跳过 = 断点续渲）→ 信息栏 → ffmpeg 合成。
中断后原样再跑一次同一条命令即可接着渲。只结束自己启动的进程。
"""
import os as _os_ff
FFMPEG_EXE = _os_ff.environ.get("DART_FFMPEG") or "ffmpeg"

import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if "--chars" in sys.argv[:-1]:          # 要在读入信息栏模块之前定下来（主色跟着造型走）
    os.environ["DART_CHARS"] = sys.argv[sys.argv.index("--chars") + 1]
import hud as HUD  # noqa: E402
import replay  # noqa: E402
import timeline as TLM  # noqa: E402

BLENDER = os.environ.get("DART_BLENDER") or shutil.which("blender") or "blender"
WORK = os.environ.get("DART_WORK") or os.path.join(HERE, "..", "work")
GRADE = "eq=saturation=1.08:contrast=1.04,vignette=angle=PI/9"


def run_prep(rp_path):
    r = subprocess.run([sys.executable, os.path.join(HERE, "prep.py"), rp_path], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if r.returncode != 0:
        print(r.stdout[-2000:], r.stderr[-4000:])
        raise SystemExit("地图预处理失败")


def frame_ok(path):
    """帧文件完整：存在、够大、以 JPEG 结束标记收尾（进程被中断时可能留下写了一半的文件）。"""
    try:
        if os.path.getsize(path) < 1000:
            return False
        with open(path, "rb") as fh:
            fh.seek(-2, 2)
            return fh.read(2) == bytes((0xFF, 0xD9))
    except OSError:
        return False


def missing(frames_dir, a, b, clean=False):
    out = []
    for f in range(a, b):
        p = os.path.join(frames_dir, "%05d.jpg" % f)
        if not frame_ok(p):
            out.append(f)
            if clean and os.path.exists(p):
                os.remove(p)
    return out


def render_frames(rp_path, frames_dir, a, b, procs, samples, roster, log_dir):
    todo = missing(frames_dir, a, b, clean=True)
    if not todo:
        return 0.0
    # 把还没渲的帧所在的区间切成 procs 段
    lo, hi = todo[0], todo[-1] + 1
    n = hi - lo
    procs = max(1, min(procs, n // 8 or 1))
    step = (n + procs - 1) // procs
    children = []
    t0 = time.time()
    os.makedirs(log_dir, exist_ok=True)
    for i in range(procs):
        s, e = lo + i * step, min(hi, lo + (i + 1) * step)
        if s >= e:
            continue
        cmd = [BLENDER, "-b", "--factory-startup", "-P", os.path.join(HERE, "render_bl.py"), "--", rp_path,
               "--out", frames_dir, "--frames", "%d:%d" % (s, e), "--samples", str(samples)]
        if roster:
            cmd += ["--roster", roster]
        log = open(os.path.join(log_dir, "seg_%05d_%05d.log" % (s, e)), "w", encoding="utf-8", errors="replace")
        children.append((subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT), log, s, e))
    try:
        while any(p.poll() is None for p, _, _, _ in children):
            time.sleep(5)
            left = len(missing(frames_dir, a, b))
            print("\r渲染中：还剩 %d 帧，已用 %.0f 秒   " % (left, time.time() - t0), end="", flush=True)
    except KeyboardInterrupt:
        for p, _, _, _ in children:          # 只结束自己起的进程
            if p.poll() is None:
                p.terminate()
        raise
    finally:
        for _, log, _, _ in children:
            log.close()
    print()
    return time.time() - t0


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser()
    ap.add_argument("replay")
    ap.add_argument("-o", "--out", default=None)
    ap.add_argument("--procs", type=int, default=3)
    ap.add_argument("--samples", type=int, default=12)
    ap.add_argument("--roster", default=None)
    ap.add_argument("--frames", default=None)
    ap.add_argument("--work", default=None)
    ap.add_argument("--keep", action="store_true", help="保留中间帧")
    ap.add_argument("--no-hud", action="store_true")
    ap.add_argument("--theme", default=None, help="强制用某个主题（默认按地图）")
    ap.add_argument("--chars", default=None, choices=["voxel", "smooth"], help="角色造型：voxel 像素小人（默认）/ smooth 旧的圆润小人")
    ap.add_argument("--demo", default=None, help="演示模式的导演表（json，见 demo.py）")
    a = ap.parse_args()
    director = None
    if a.demo:
        import demo as DEMO
        os.environ["DART_DEMO"] = os.path.abspath(a.demo)     # Blender 子进程从环境变量读
        director = DEMO.load(a.demo)
        a.no_hud = True
        if not a.roster and director.get("roster"):
            a.roster = ",".join(director["roster"])
    if a.chars:
        os.environ["DART_CHARS"] = a.chars
    if a.theme:
        os.environ["DART_THEME"] = a.theme
    rp_path = os.path.abspath(a.replay)
    name = os.path.splitext(os.path.basename(rp_path))[0]
    work = a.work or os.path.join(WORK, name)
    frames_dir = os.path.join(work, "frames")
    hud_dir = os.path.join(work, "hud")
    os.makedirs(frames_dir, exist_ok=True)
    out = a.out or os.path.join(work, name + ".mp4")
    t0 = time.time()
    run_prep(rp_path)
    rp = replay.load(rp_path)
    roster = a.roster.split(",") if a.roster else None
    tl = DEMO.DemoTimeline(rp, roster, director) if director else TLM.Timeline(rp, roster)
    fa, fb = 0, tl.n
    if a.frames:
        fa, fb = [int(v) for v in a.frames.split(":")]
        fb = min(fb, tl.n)
    print("回放 %s：%d 步，成片 %d 帧（%.1f 秒），渲染第 %d～%d 帧，%d 个进程" % (name, rp.T, tl.n, tl.n / 30.0, fa, fb, a.procs))
    for attempt in range(3):
        sec = render_frames(rp_path, frames_dir, fa, fb, a.procs, a.samples, a.roster, os.path.join(work, "logs"))
        miss = missing(frames_dir, fa, fb)
        if not miss:
            break
        print("还有 %d 帧没渲出来，重试（第 %d 次）" % (len(miss), attempt + 1))
    else:
        raise SystemExit("渲染没有完成，看 %s 里的日志" % os.path.join(work, "logs"))
    if sec > 0:
        print("渲染用时 %.0f 秒，折合 %.2f 秒/帧" % (sec, sec / max(1, fb - fa)))
    cmd = [FFMPEG_EXE, "-y", "-loglevel", "error", "-framerate", "30", "-start_number", str(fa), "-i",
           os.path.join(frames_dir, "%05d.jpg")]
    if a.no_hud:
        cmd += ["-frames:v", str(fb - fa), "-vf", GRADE]
    else:
        HUD.render_range(rp_path, hud_dir, roster, fa, fb)
        cmd += ["-framerate", "30", "-start_number", str(fa), "-i", os.path.join(hud_dir, "%05d.png"),
                "-frames:v", str(fb - fa), "-filter_complex", "[0:v]%s[bg];[bg][1:v]overlay=format=auto" % GRADE]
    cmd += ["-c:v", "libx264", "-preset", "slow", "-crf", "16", "-pix_fmt", "yuv420p", "-movflags", "+faststart", out]
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    subprocess.run(cmd, check=True)
    if director:
        DEMO.export_coords(tl, os.path.splitext(out)[0] + ".coords.json", fa, fb)
    print("完成：%s（总用时 %.0f 秒）" % (out, time.time() - t0))
    if not a.keep and not a.frames:
        shutil.rmtree(hud_dir, ignore_errors=True)


if __name__ == "__main__":
    main()

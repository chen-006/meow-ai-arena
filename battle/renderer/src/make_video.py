# -*- coding: utf-8 -*-
"""一条命令把一份回放渲染成 1080p 30 帧的 mp4（见 ..\\出片.cmd）。
    python make_video.py <回放.wrp> [-o 输出.mp4] [--from 秒] [--to 秒] [--procs 3] [--samples 12]
                         [--lord default|<名字>|<造型.json>] [--shots 机位表.json] [--no-close] [--names 甲,乙]
                         [--no-hud] [--work 目录] [--keep]
步骤：分段并行渲染（每段一个 Blender 进程，已经渲好的帧跳过 = 断点续渲）→ 信息栏 → ffmpeg 合成。
片子包含回放最后一帧时，最后定格 3 秒放结果牌。
中断后原样再跑一次同一条命令即可接着渲。按 Ctrl+C 只结束本命令自己起的 Blender 进程。
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import hud as HUD  # noqa: E402
import replay as RP  # noqa: E402

# Blender 和 ffmpeg 的位置：环境变量优先，其次 PATH
BLENDER = os.environ.get("A12_BLENDER") or shutil.which("blender") or "blender"
FFMPEG = os.environ.get("A12_FFMPEG") or shutil.which("ffmpeg") or "ffmpeg"
# 中间帧和默认输出放在 renderer/renders/<回放名>/（不入库）
WORK = os.environ.get("A12_WORK") or os.path.join(HERE, "..", "renders")
GRADE = "eq=saturation=1.06:contrast=1.03,vignette=angle=PI/10"


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


def render_frames(rp_path, frames_dir, a, b, args, log_dir):
    todo = missing(frames_dir, a, b, clean=True)
    if not todo:
        return 0.0
    lo, hi = todo[0], todo[-1] + 1
    n = hi - lo
    procs = max(1, min(args.procs, n // 10 or 1))
    step = (n + procs - 1) // procs
    children = []
    t0 = time.time()
    os.makedirs(log_dir, exist_ok=True)
    for i in range(procs):
        s, e = lo + i * step, min(hi, lo + (i + 1) * step)
        if s >= e:
            continue
        cmd = [BLENDER, "-b", "--factory-startup", "-P", os.path.join(HERE, "render_replay.py"), "--", rp_path,
               "--out", frames_dir, "--frames", "%d:%d" % (s, e), "--samples", str(args.samples), "--lord", args.lord]
        if args.shots:
            cmd += ["--shots", os.path.abspath(args.shots)]
        if args.no_close:
            cmd += ["--no-close"]
        if args.pitch is not None:
            cmd += ["--pitch", str(args.pitch)]
        if args.no_fx:
            cmd += ["--no-fx"]
        if args.no_shake:
            cmd += ["--no-shake"]
        log = open(os.path.join(log_dir, "seg_%05d_%05d.log" % (s, e)), "w", encoding="utf-8", errors="replace")
        children.append((subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT), log))
    try:
        while any(p.poll() is None for p, _ in children):
            time.sleep(5)
            left = len(missing(frames_dir, a, b))
            print("\r渲染中：还剩 %d 帧，已用 %.0f 秒   " % (left, time.time() - t0), end="", flush=True)
    except KeyboardInterrupt:
        for p, _ in children:          # 只结束自己起的进程
            if p.poll() is None:
                p.terminate()
        raise
    finally:
        for _, log in children:
            log.close()
    print()
    return time.time() - t0


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="回放 -> mp4")
    ap.add_argument("replay")
    ap.add_argument("-o", "--out", default=None)
    ap.add_argument("--from", dest="t_from", type=float, default=None, help="从第几秒开始（默认开头）")
    ap.add_argument("--to", dest="t_to", type=float, default=None, help="到第几秒（默认结尾，含结尾定格）")
    ap.add_argument("--procs", type=int, default=3)
    ap.add_argument("--samples", type=int, default=12)
    ap.add_argument("--lord", default="default", help="主帅造型：default 通用将领，或某家 AI 的名字 / 造型 JSON 路径")
    ap.add_argument("--shots", default=None, help="手动机位表 JSON（见 README.md）")
    ap.add_argument("--no-close", action="store_true", help="不自动切近景，全程全景")
    ap.add_argument("--pitch", type=float, default=None)
    ap.add_argument("--names", default=None, help="信息栏上各方的名字，逗号分隔，按队伍号")
    ap.add_argument("--no-hud", action="store_true")
    ap.add_argument("--no-fx", action="store_true", help="不出特效（调试用）")
    ap.add_argument("--no-shake", action="store_true", help="不要镜头震动")
    ap.add_argument("--work", default=None)
    ap.add_argument("--keep", action="store_true", help="保留中间帧")
    a = ap.parse_args()
    rp_path = os.path.abspath(a.replay)
    name = os.path.splitext(os.path.basename(rp_path))[0]
    work = a.work or os.path.join(WORK, name)
    frames_dir = os.path.join(work, "frames")
    hud_dir = os.path.join(work, "hud")
    os.makedirs(frames_dir, exist_ok=True)
    out = os.path.abspath(a.out or os.path.join(work, name + ".mp4"))
    t0 = time.time()
    rp = RP.load(rp_path)
    fps = 1.0 / (rp.dt * rp.rec_every)
    fa = 0 if a.t_from is None else max(0, int(round(a.t_from * fps)))
    fb = rp.nx if a.t_to is None else min(rp.nx, int(round(a.t_to * fps)))
    if fb >= rp.n:
        fb = rp.nx                      # 到了结尾（之后接定格和结果牌）
    print("回放 %s：%d 帧（%.1f 秒），渲染第 %d～%d 帧，%d 个进程" % (name, rp.n, rp.n / fps, fa, fb, a.procs))
    # 信息栏和渲染同时做（纯 Python 画图，放在另一个线程里）
    names = a.names.split(",") if a.names else None
    hud_thread = None
    if not a.no_hud:
        import threading
        if os.path.isdir(hud_dir):
            shutil.rmtree(hud_dir, ignore_errors=True)
        hud_thread = threading.Thread(target=HUD.render_range, args=(rp, hud_dir, fa, fb), kwargs=dict(names=names, hold=True))
        hud_thread.start()
    for attempt in range(3):
        sec = render_frames(rp_path, frames_dir, fa, fb, a, os.path.join(work, "logs"))
        miss = missing(frames_dir, fa, fb)
        if not miss:
            break
        print("还有 %d 帧没渲出来，重试（第 %d 次）" % (len(miss), attempt + 1))
    else:
        raise SystemExit("渲染没有完成，看 %s 里的日志" % os.path.join(work, "logs"))
    if sec > 0:
        print("渲染用时 %.0f 秒，折合 %.2f 秒/帧" % (sec, sec / max(1, fb - fa)))
    # 结尾定格：最后一帧复制若干份，信息栏在这段放结果
    end = fb
    if fb >= rp.nx:
        end = rp.nx + HUD.hold_frames(rp)
        last = os.path.join(frames_dir, "%05d.jpg" % (rp.nx - 1))
        for f in range(rp.nx, end):
            shutil.copyfile(last, os.path.join(frames_dir, "%05d.jpg" % f))
    cmd = [FFMPEG, "-y", "-loglevel", "error", "-threads", "4", "-framerate", "30", "-start_number", str(fa), "-i",
           os.path.join(frames_dir, "%05d.jpg")]
    if a.no_hud:
        cmd += ["-frames:v", str(end - fa), "-vf", GRADE]
    else:
        hud_thread.join()
        cmd += ["-framerate", "30", "-start_number", str(fa), "-i", os.path.join(hud_dir, "%05d.png"),
                "-frames:v", str(end - fa), "-filter_complex", "[0:v]%s[bg];[bg][1:v]overlay=format=auto" % GRADE]
    cmd += ["-c:v", "libx264", "-preset", "medium", "-crf", "16", "-pix_fmt", "yuv420p", "-movflags", "+faststart", out]
    os.makedirs(os.path.dirname(out), exist_ok=True)
    subprocess.run(cmd, check=True)
    print("完成：%s（%.1f 秒，总用时 %.0f 秒）" % (out, (end - fa) / fps, time.time() - t0))
    if not a.keep:
        shutil.rmtree(hud_dir, ignore_errors=True)


if __name__ == "__main__":
    main()

# -*- coding: utf-8 -*-
"""按时间表出片：分段并行调 Blender（render_timeline.py）→（可选）信息栏 → ffmpeg 合成 mp4。
    python run_timeline.py <plan.json> <输出.mp4> [--procs 2] [--samples 12] [--work 目录] [--no-grade]
plan 里可选 "hud": [每个输出帧用回放的第几帧画信息栏（整数；超过回放末帧 = 结尾结果牌那段）或 null 不画]，"names": [...]。
断点续渲：work\frames 里已经渲好的帧跳过；换了画面或时间表要重渲时先删 work。只结束自己起的 Blender 进程。
"""
import argparse
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_video as MV  # noqa: E402


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser()
    ap.add_argument("plan")
    ap.add_argument("out")
    ap.add_argument("--procs", type=int, default=2)
    ap.add_argument("--samples", type=int, default=12)
    ap.add_argument("--work", default=None)
    ap.add_argument("--no-grade", action="store_true")
    a = ap.parse_args()
    plan_path = os.path.abspath(a.plan)
    plan = json.load(open(plan_path, encoding="utf-8"))
    n = len(plan["frames"])
    work = a.work or os.path.join(MV.WORK, "timeline", os.path.splitext(os.path.basename(a.out))[0])
    fr = os.path.join(work, "frames")
    os.makedirs(fr, exist_ok=True)
    t0 = time.time()
    todo = MV.missing(fr, 0, n, clean=True)
    if todo:
        lo, hi = todo[0], todo[-1] + 1
        procs = max(1, min(a.procs, (hi - lo) // 10 or 1))
        step = (hi - lo + procs - 1) // procs
        kids = []
        os.makedirs(os.path.join(work, "logs"), exist_ok=True)
        for i in range(procs):
            s, e = lo + i * step, min(hi, lo + (i + 1) * step)
            if s >= e:
                continue
            cmd = [MV.BLENDER, "-b", "--factory-startup", "-P", os.path.join(HERE, "render_timeline.py"), "--", plan_path,
                   "--out", fr, "--frames", "%d:%d" % (s, e), "--samples", str(a.samples)]
            log = open(os.path.join(work, "logs", "seg_%05d_%05d.log" % (s, e)), "w", encoding="utf-8", errors="replace")
            p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT)
            print("Blender 进程 %d：帧 %d～%d" % (p.pid, s, e), flush=True)
            kids.append((p, log))
        try:
            while any(p.poll() is None for p, _ in kids):
                time.sleep(5)
        except KeyboardInterrupt:
            for p, _ in kids:
                if p.poll() is None:
                    p.terminate()
            raise
        finally:
            for _, log in kids:
                log.close()
        left = MV.missing(fr, 0, n)
        if left:
            raise SystemExit("还有 %d 帧没渲出来，看 %s\\logs" % (len(left), work))
        print("渲染 %d 帧用时 %.0f 秒（%.2f 秒/帧）" % (len(todo), time.time() - t0, (time.time() - t0) / len(todo)))
    grade = "" if a.no_grade else MV.GRADE
    cmd = [MV.FFMPEG, "-y", "-loglevel", "error", "-threads", "4", "-framerate", "30", "-i", os.path.join(fr, "%05d.jpg")]
    if plan.get("hud"):
        import hud as HUD
        import replay as RP
        rp = RP.load(plan["replay"])
        h = HUD.Hud(rp, plan.get("names"))
        hd = os.path.join(work, "hud")
        os.makedirs(hd, exist_ok=True)
        blank = None
        for j, hi_ in enumerate(plan["hud"]):
            p = os.path.join(hd, "%05d.png" % j)
            if hi_ is None:
                if blank is None:
                    from PIL import Image
                    blank = Image.new("RGBA", (1920, 1080), (0, 0, 0, 0))
                blank.save(p)
            else:
                h.frame(int(hi_)).save(p, compress_level=1)
        cmd += ["-framerate", "30", "-i", os.path.join(hd, "%05d.png"), "-frames:v", str(n), "-filter_complex",
                "[0:v]%s[bg];[bg][1:v]overlay=format=auto" % (grade or "null")]
    else:
        cmd += ["-frames:v", str(n)] + (["-vf", grade] if grade else [])
    cmd += ["-c:v", "libx264", "-preset", "medium", "-crf", "17", "-pix_fmt", "yuv420p", "-movflags", "+faststart",
            os.path.abspath(a.out)]
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    subprocess.run(cmd, check=True)
    print("完成：%s（%d 帧，%.1f 秒，总用时 %.0f 秒）" % (a.out, n, n / 30.0, time.time() - t0))


if __name__ == "__main__":
    main()

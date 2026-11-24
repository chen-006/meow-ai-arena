# -*- coding: utf-8 -*-
"""第 12 集样片出片链（venv 的 Python 跑，用 PowerShell Start-Process 起成独立进程）：
    三种模式各一整局 → 1080p mp4 → 每段抽帧拼联系表 → 写 渲染器\\DONE_样片.txt。
中断后原样再跑一次即可接着渲（make_video 跳过已渲好的帧；已出好的 mp4 跳过）。
进度一行一行追加到 渲染器\\_work\\chain12.log。"""
import os as _os_ff
FFMPEG_EXE = _os_ff.environ.get("DART_FFMPEG") or "ffmpeg"

import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
LOG = os.path.join(ROOT, "work", "chain12.log")
OUT = os.path.join(ROOT, "样片")
JOBS = [  # (回放, 输出名)
    (os.path.join(ROOT, "回放样例", "引擎对局", "caldera_g11.txt"), "样片_歼灭_火山口_caldera_g11.mp4"),
    (os.path.join(ROOT, "回放样例", "引擎对局", "keeps_g33.txt"), "样片_刺王_双堡_keeps_g33.mp4"),
    (os.path.join(ROOT, "回放样例", "引擎对局", "piers_g21.txt"), "样片_占点_三座栈桥_piers_g21.mp4"),
]


def log(msg):
    with open(LOG, "a", encoding="utf-8") as f:
        f.write(time.strftime("%m-%d %H:%M:%S ") + msg + "\n")


def main():
    os.makedirs(OUT, exist_ok=True)
    log("开始，PID %d" % os.getpid())
    ok = []
    for rp, name in JOBS:
        dst = os.path.join(OUT, name)
        if os.path.exists(dst) and os.path.getsize(dst) > 100000:
            log("已有，跳过 " + name)
            ok.append(dst)
            continue
        t0 = time.time()
        log("渲染 " + name)
        work = os.path.join(ROOT, "work", "work", os.path.splitext(name)[0])
        r = subprocess.run([sys.executable, os.path.join(HERE, "make_video.py"), rp, "-o", dst, "--work", work,
                            "--procs", "3", "--samples", "12"], capture_output=True, text=True, encoding="utf-8",
                           errors="replace")
        with open(os.path.join(ROOT, "work", "chain12_%s.out.txt" % os.path.splitext(name)[0]), "w", encoding="utf-8") as f:
            f.write(r.stdout[-20000:] + "\n" + r.stderr[-20000:])
        if r.returncode != 0 or not os.path.exists(dst):
            log("失败 %s（退出码 %d），看 _work\\chain12_*.out.txt" % (name, r.returncode))
            continue
        log("完成 %s，%.0f 秒" % (name, time.time() - t0))
        ok.append(dst)
    for dst in ok:
        sheet = os.path.splitext(dst)[0] + "_联系表.jpg"
        subprocess.run([FFMPEG_EXE, "-y", "-loglevel", "error", "-i", dst, "-vf",
                        "fps=1/3,scale=480:-1,tile=5x4", "-frames:v", "1", sheet])
        log("联系表 " + os.path.basename(sheet))
    with open(os.path.join(ROOT, "DONE_样片.txt"), "w", encoding="utf-8") as f:
        f.write(time.strftime("%Y-%m-%d %H:%M:%S") + " 样片链结束：%d/%d 段成功\n" % (len(ok), len(JOBS)))
        for dst in ok:
            f.write(dst + "\n")
    log("结束：%d/%d 段成功" % (len(ok), len(JOBS)))


if __name__ == "__main__":
    main()

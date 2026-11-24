# -*- coding: utf-8 -*-
"""第 12 集 5v5 的关键时刻静帧（venv 的 Python 跑；见 ..\\出静帧12.cmd）：
    python stills12.py <回放> <名字>@<时刻> [<名字>@<时刻> ...] [--out 目录] [--samples 32] [--roster ...]
时刻：数字 = 游戏步；final = 终结一击（慢放里出手后、命中前）；final+3.4 之类 = 相对终结时刻；
      capture:i = 第 i 次翻点完成前 1.5 步；respawn:i = 第 i 次复活前 6 步；life:i = 国王第 i 次掉命后 1.5 步；kill:i = 第 i 次击杀；
      f1234 = 成片第 1234 帧。
一个 Blender 进程按同一条成片时间表（镜头、运镜都和出片一样）一次出完这几张，再叠上同一帧的信息栏。
另：python stills12.py --sheet 输出.jpg 图1.png 图2.png ... [--cols 3] [--labels 名字1,名字2,...] 拼联系表。"""
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from PIL import Image, ImageDraw  # noqa: E402

import hud as HUD  # noqa: E402
import replay  # noqa: E402
import timeline as TLM  # noqa: E402

BLENDER = os.environ.get("DART_BLENDER") or shutil.which("blender") or "blender"
WORK = os.path.join(os.environ.get("DART_WORK") or os.path.join(os.path.dirname(HERE), "work"), "stills12")


def run_prep(path):
    r = subprocess.run([sys.executable, os.path.join(HERE, "prep.py"), path], capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    if r.returncode != 0:
        print(r.stdout[-1500:], r.stderr[-3000:])
        raise SystemExit("地图预处理失败")


def resolve(rp, tl, spec):
    """时刻写法 -> 成片帧号。"""
    def frame_of(gt):
        return min(range(tl.n), key=lambda i: abs(tl.frames[i]["gt"] - gt))
    if spec.startswith("f") and spec[1:].isdigit():
        return int(spec[1:])
    if spec.startswith("final"):
        if not tl.final:
            raise SystemExit("这局没有终结一击")
        off = float(spec[5:] or "-1.1")
        return frame_of(tl.final[0] + off)
    kind, _, idx = spec.partition(":")
    i = int(idx or 0)
    if kind == "capture":
        return frame_of(rp.captures[i][0] - 0.5)
    if kind == "respawn":
        ts = sorted(t for k in range(rp.n) for t in rp.respawns[k])
        return frame_of(ts[i] + 1.0 - 6.0)
    if kind == "life":
        return frame_of(rp.lifeloss[i][0] + 2.5)
    if kind == "kill":
        ks = sorted(t for (t, k, c, kl) in rp.death_list if c in (1, 2))
        return frame_of(ks[i] + 1.4)
    return frame_of(float(spec))


def sheet(out, paths, cols=3, labels=None, w=640):
    ims = [Image.open(p).convert("RGB") for p in paths]
    h = int(w * 9 / 16)
    rows = (len(ims) + cols - 1) // cols
    lab_h = 30 if labels else 0
    S = Image.new("RGB", (cols * w + (cols + 1) * 6, rows * (h + lab_h) + (rows + 1) * 6), (24, 24, 28))
    d = ImageDraw.Draw(S)
    f = HUD.font(20, "Bold")
    for i, im in enumerate(ims):
        x = 6 + (i % cols) * (w + 6)
        y = 6 + (i // cols) * (h + lab_h + 6)
        S.paste(im.resize((w, h), Image.LANCZOS), (x, y + lab_h))
        if labels and i < len(labels):
            d.text((x + 4, y + 2), labels[i], font=f, fill=(235, 235, 235))
    S.save(out, quality=88)
    return out


def main():
    a = sys.argv[1:]
    if a and a[0] == "--sheet":
        out = a[1]
        rest = a[2:]
        cols, labels = 3, None
        if "--cols" in rest:
            i = rest.index("--cols")
            cols = int(rest[i + 1])
            rest = rest[:i] + rest[i + 2:]
        if "--labels" in rest:
            i = rest.index("--labels")
            labels = rest[i + 1].split(",")
            rest = rest[:i] + rest[i + 2:]
        print(sheet(out, rest, cols, labels))
        return
    opt = dict(out=os.path.join(os.path.dirname(HERE), "静帧"), samples="32", roster=None)
    specs = []
    i = 1
    path = os.path.abspath(a[0])
    while i < len(a):
        if a[i].startswith("--"):
            opt[a[i][2:]] = a[i + 1]
            i += 2
        else:
            specs.append(a[i].split("@", 1))
            i += 1
    run_prep(path)
    rp = replay.load(path)
    roster = opt["roster"].split(",") if opt["roster"] else None
    tl = TLM.Timeline(rp, roster)
    shots = [dict(name=nm, f=resolve(rp, tl, sp)) for nm, sp in specs]
    work = os.path.join(WORK, os.path.splitext(os.path.basename(path))[0])
    os.makedirs(work, exist_ok=True)
    js = os.path.join(work, "shots.json")
    json.dump(shots, open(js, "w", encoding="utf-8"), ensure_ascii=False)
    cmd = [BLENDER, "-b", "--factory-startup", "-P", os.path.join(HERE, "render_bl.py"), "--", path, "--out", work,
           "--shots", js, "--samples", opt["samples"]]
    if opt["roster"]:
        cmd += ["--roster", opt["roster"]]
    log = os.path.join(work, "blender.log")
    with open(log, "w", encoding="utf-8", errors="replace") as fh:
        r = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        raise SystemExit("Blender 出错，看 " + log)
    h = HUD.Hud(rp, roster)
    os.makedirs(opt["out"], exist_ok=True)
    for sh in shots:
        bg = Image.open(os.path.join(work, sh["name"] + ".png")).convert("RGBA")
        key, paint = h.draw(sh["f"])
        bg.alpha_composite(paint())
        dst = os.path.join(opt["out"], sh["name"] + ".png")
        bg.convert("RGB").save(dst)
        print("STILL", dst, "frame", sh["f"], "gt %.1f" % tl.frames[sh["f"]]["gt"])


if __name__ == "__main__":
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    main()

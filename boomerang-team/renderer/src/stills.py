# -*- coding: utf-8 -*-
"""出静帧（venv 的 Python 跑；见 ..\\出静帧.cmd）：四张定妆照 + 角色阵容图 + 前后对比图，输出到 ..\\静帧\\。
    python stills.py [ffa team closeup collapse lineup compare]
每张图 = 起一次 Blender 渲一帧（与成片同一套场景、角色、动作和特效）+ 同样的调色 + 信息栏。
"""
import os as _os_ff
FFMPEG_EXE = _os_ff.environ.get("DART_FFMPEG") or "ffmpeg"

import math
import os
import subprocess
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
if "--chars" in sys.argv[:-1]:          # 要在读入信息栏模块之前定下来（主色跟着造型走）
    os.environ["DART_CHARS"] = sys.argv[sys.argv.index("--chars") + 1]
import cast  # noqa: E402
import hud as HUD  # noqa: E402
import replay  # noqa: E402
import timeline as TLM  # noqa: E402
from make_video import BLENDER, GRADE, run_prep  # noqa: E402

ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "静帧")
WORK = os.path.join(os.environ.get("DART_WORK") or os.path.join(ROOT, "work"), "stills")
REPLAYS = os.environ.get("DART_SAMPLES") or os.path.join(ROOT, "..", "replays")
FFA = os.path.join(REPLAYS, "四人_双湖_twinlakes_s590488064.txt")
TEAM = os.path.join(REPLAYS, "4v4_峡谷_canyon_s660339338.txt")
TEAM2 = os.path.join(REPLAYS, "4v4_林地_forest_s1372611326.txt")
OLD = os.environ.get("DART_OLD_STILLS", "")
REFS = []


def blender_still(rp_path, name, gt, cam=None, samples=64, extra=(), f=None):
    """gt：游戏时间（步），用全景机位；f：成片的帧号，用成片在这一帧的机位（近景、结算镜头就是这么出的）。"""
    os.makedirs(WORK, exist_ok=True)
    samples = int(os.environ.get("STILL_SAMPLES", samples))
    cmd = [BLENDER, "-b", "--factory-startup", "-P", os.path.join(HERE, "render_bl.py"), "--", rp_path, "--out", WORK,
           "--still", name, "--samples", str(samples), "--fast", "0"]
    cmd += ["--f", str(f)] if f is not None else ["--gt", str(gt)]
    if cam:
        cmd += ["--cam", cam]
    cmd += list(extra)
    r = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace")
    png = os.path.join(WORK, name + ".png")
    if "STILL" not in r.stdout or not os.path.exists(png):
        print(r.stdout[-3000:], r.stderr[-3000:])
        raise SystemExit("渲染失败：" + name)
    return png


def grade(src, dst):
    subprocess.run([FFMPEG_EXE, "-y", "-loglevel", "error", "-i", src, "-vf", GRADE, dst], check=True)
    return dst


def frame_at(rp_path, what):
    """成片里的关键帧号：final = 终结一击定格的那一帧，win = 结算镜头推到位的那一帧。"""
    tl = TLM.Timeline(replay.load(rp_path))
    if what == "final":
        return min(range(tl.n), key=lambda i: abs(tl.frames[i]["gt"] - (tl.final[0] - 1.1)))      # 出手后、命中前的一瞬
    if what == "cut":
        return min(range(tl.n), key=lambda i: abs(tl.frames[i]["gt"] - (tl.final[0] + 3.4)))      # 切开后两半正往两边倒
    return tl.n - 14


def with_hud(png, rp_path, gt, out_png, hud=True, f=None):
    g = grade(png, os.path.join(WORK, "_g_" + os.path.basename(png)))
    im = Image.open(g).convert("RGBA")
    if hud:
        rp = replay.load(rp_path)
        h = HUD.Hud(rp)
        if f is None:
            f = min(range(h.tl.n), key=lambda i: abs(h.tl.frames[i]["gt"] - gt))
        key, paint = h.draw(f)
        im.alpha_composite(paint())
    os.makedirs(OUT, exist_ok=True)
    im.convert("RGB").save(out_png)
    print("->", out_png, flush=True)


# ---------------------------------------------------------------- 阵容图用的假回放（顺便用 0.5 米的格子，验证渲染不依赖格子边长）
def lineup_replay():
    os.makedirs(WORK, exist_ok=True)
    p = os.path.join(WORK, "lineup.txt")
    W, H = 26, 10
    rows = ["".join("0" if (2 <= x < 24 and 2 <= y < 8) else "2" for x in range(W)) for y in range(H)]
    n = 8
    xs = [1.6 + i * 1.25 for i in range(n)]
    y = 2.45
    ch = " | ".join("1 %d %d 0 0 9000 0 0 0 0 0 0 0 0 255 -1 0 -1" % (x * 1000, y * 1000) for x in xs)
    dr = " | ".join("0 %d %d 0 0 0 0" % (x * 1000, y * 1000) for x in xs)
    lines = ["DART-REPLAY 2", "ENGINE dartfu-v3", "CFG MODE=1 STEPS=800 CELL=500", "SEED 0", "MODE 1 ffa", "MAPNAME lineup",
             "MAP %d %d" % (W, H)] + rows + ["WAVE"] + ["." * W] * H + ["WAVES 0", "PLAYERS %d" % n]
    lines += ["PLAYER %d %d p%d" % (i, i, i) for i in range(n)]
    lines += ["S 0 | %s | %s | 0 -1" % (ch, dr), "END 0 -1 0 time"]
    open(p, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    return p, xs, y


def proj(P, tx, ty, tz, dist, pitch, lens, w=1920, h=1080):
    p = math.radians(pitch)
    cx, cy, cz = tx, ty - math.cos(p) * dist, tz + math.sin(p) * dist
    v = (P[0] - cx, P[1] - cy, P[2] - cz)
    fwd = (0.0, math.cos(p), -math.sin(p))
    up = (0.0, math.sin(p), math.cos(p))
    zc = v[1] * fwd[1] + v[2] * fwd[2]
    yc = v[1] * up[1] + v[2] * up[2]
    th = 18.0 / lens
    return (v[0] / zc / th * 0.5 + 0.5) * w, (0.5 - yc / zc / (th * h / w) * 0.5) * h


NOTES = {
    "claude": ("橙", ["橙色长发、头侧一朵大橙花", "脑后偏一只黑蝴蝶结", "米白长裙 · 橙裙片 · 黑边"]),
    "gpt": ("翠绿", ["薄荷绿大波浪长发", "脑后一只大绿蝴蝶结", "白色层叠裙 · 绿饰边"]),
    "gemini": ("蓝紫", ["蓝→粉紫渐变长发", "大猫耳 + 渐变猫尾", "藏青短裙 · 白裙边"]),
    "doubao": ("珊瑚红", ["深棕齐耳短发", "头上、胸前各一颗红豆", "黑色短外套 · 红裙边"]),
    "kimi": ("月光青", ["银白长发、发梢浅蓝", "头顶一枚立着的金色新月", "藏青星点披肩"]),
    "deepseek": ("海蓝", ["深蓝→青渐变长发", "鲸鳍耳 + 大鲸鱼尾", "女仆发带 · 白围裙"]),
    "grok": ("金", ["金色大双马尾", "黑蝙蝠翼 + 小金冠", "黑色大摆裙露红衬"]),
    "hunyuan": ("兰紫", ["左黑右白的阴阳长发", "紫发带 · 宽袖", "白色长袍 · 紫腰带"]),
}


def make_lineup():
    rp_path, xs, y = lineup_replay()
    run_prep(rp_path)
    keys = HUD.DEFAULT_ROSTER[8]
    cx = (xs[0] + xs[-1]) / 2
    cam = (cx, -y, 19.5, 15.0, 0.0, 70.0, 0.55)
    a = blender_still(rp_path, "lineup", 0, "%g,%g,%g,%g,%g,%g,%g" % cam, extra=["--nodecor", "--sun", "235,40"])
    cam2 = (cx, -y, 29.6, 58.0)
    b = blender_still(rp_path, "lineup_top", 0, "%g,%g,%g,%g" % cam2, extra=["--nodecor"])
    img = Image.open(grade(a, os.path.join(WORK, "_g_lineup.png"))).convert("RGBA")
    top = Image.open(grade(b, os.path.join(WORK, "_g_lineup_top.png"))).convert("RGBA")
    d = ImageDraw.Draw(img)
    F = HUD.font
    d.text((70, 52), "角色阵容", font=F(46, "Bold"), fill=HUD.INK)
    d.text((70 + d.textlength("角色阵容", font=F(46, "Bold")) + 22, 72),
           "8 个原创 Q 版小人：主色和标志物对应各自的立绘，每人一层深色描边", font=F(21, "Medium"), fill=HUD.SUB)
    d.text((70, 122), "场上不放常驻名字：全景里靠发色、头上的标志物、描边和脚下色环认人；4v4 时描边外面多一圈队伍色，色环加粗并换成队伍色。",
           font=F(17, "Regular"), fill=HUD.SUB)
    cw, chh = 214, 176
    for i, key in enumerate(keys):
        px, fy = proj((xs[i], -y, 0.0), cam[0], cam[1], cam[6], cam[2], cam[3], cam[5])
        x0, y0 = int(px - cw / 2), int(fy + 46)
        HUD.panel(img, (x0, y0, x0 + cw, y0 + chh), 18, blur=8, alpha=40)
        d = ImageDraw.Draw(img)
        col = HUD.rgb(HUD.COLOR[key])
        d.rounded_rectangle((x0, y0, x0 + cw, y0 + 7), 3, fill=col)
        av = 52
        d.ellipse((x0 + 9, y0 + 15, x0 + 14 + av, y0 + 20 + av), fill=col)
        img.alpha_composite(HUD.avatar(key, av), (x0 + 12, y0 + 18))
        d = ImageDraw.Draw(img)
        d.text((x0 + 12 + av + 10, y0 + 16), HUD.LABEL[key], font=F(25, "Bold"), fill=HUD.INK)
        cname, lines = NOTES[key]
        chip = "主色 " + cname
        tw = d.textlength(chip, font=F(15, "Medium"))
        c0 = x0 + 12 + av + 10
        d.rounded_rectangle((c0, y0 + 50, c0 + tw + 16, y0 + 72), 11, fill=col)
        d.text((c0 + 8, y0 + 51), chip, font=F(15, "Medium"), fill=(255, 255, 255))
        for j, ln in enumerate(lines):
            d.text((x0 + 14, y0 + 84 + j * 26), "· " + ln, font=F(15, "Regular"), fill=(88, 80, 72))
        # 全景里的实际大小
        qx, qy = proj((xs[i], -y, 0.5), cam2[0], cam2[1], 0.0, cam2[2], cam2[3], 50.0)
        s_ = 140
        crop = top.crop((int(qx - s_ / 2), int(qy - s_ / 2 - 6), int(qx + s_ / 2), int(qy + s_ / 2 - 6)))
        m = Image.new("L", crop.size, 0)
        ImageDraw.Draw(m).rounded_rectangle((0, 0, s_ - 1, s_ - 1), 20, fill=255)
        tx0, ty0 = int(px - s_ / 2), 912
        HUD.panel(img, (tx0 - 3, ty0 - 3, tx0 + s_ + 3, ty0 + s_ + 3), 22, fill=(255, 252, 246, 255), blur=6, alpha=40)
        img.paste(crop, (tx0, ty0), m)
        d = ImageDraw.Draw(img)
    d.text((70, 878), "四人局全景里的实际大小（同一机位、同一距离，1:1 裁切）", font=F(17, "Medium"), fill=HUD.SUB)
    os.makedirs(OUT, exist_ok=True)
    img.convert("RGB").save(os.path.join(OUT, "角色阵容.png"))
    print("->", os.path.join(OUT, "角色阵容.png"), flush=True)


def make_compare():
    """第四轮 → 现在：同一机位的两张全景并排，下面一行是用户给的两张参考图。"""
    F = HUD.font
    pairs = [(os.path.join(OLD, "定妆_1_四人局全景.png"), os.path.join(OUT, "定妆_1_四人局全景.png"), "四人局"),
             (os.path.join(OLD, "定妆_2_4v4全景.png"), os.path.join(OUT, "定妆_2_4v4全景.png"), "4v4")]
    tw, th = 1120, 630
    pad, head = 40, 110
    refs = [Image.open(p).convert("RGB") for p in REFS if os.path.exists(p)]
    rh = 300
    Wd = pad * 3 + tw * 2
    Hd = head + (th + 56) * 2 + (rh + 90 if refs else 0) + pad
    cv = Image.new("RGB", (Wd, Hd), (250, 247, 240))
    d = ImageDraw.Draw(cv)
    d.text((pad, 30), "第四轮定妆  →  现在", font=F(44, "Bold"), fill=HUD.INK)
    d.text((pad + 470, 48), "左：第四轮（用户评价\u201c不错啊，可以\u201d的那一版）    右：正式渲染器第一阶段", font=F(22, "Medium"), fill=HUD.SUB)
    y = head
    for old, new, lab in pairs:
        for i, p in enumerate((old, new)):
            im = Image.open(p).convert("RGB").resize((tw, th), Image.LANCZOS)
            x = pad + i * (tw + pad)
            cv.paste(im, (x, y))
            d.text((x, y + th + 10), ("第四轮 · " if i == 0 else "现在 · ") + lab, font=F(24, "Bold"), fill=HUD.INK)
        y += th + 56
    if refs:
        d.text((pad, y + 6), "用户给的参考图（只学精致度和手法）", font=F(22, "Medium"), fill=HUD.SUB)
        x = pad
        for im in refs:
            w = int(im.size[0] * rh / im.size[1])
            cv.paste(im.resize((w, rh), Image.LANCZOS), (x, y + 44))
            x += w + pad
    cv.save(os.path.join(OUT, "对比_第四轮_现在.png"))
    print("->", os.path.join(OUT, "对比_第四轮_现在.png"), flush=True)


MAPS_DIR = os.path.join(ROOT, "地图定稿")
SUF = "_像素" if cast.style() == "voxel" else ""      # 像素造型出的定妆照另存，不覆盖旧图


def lineup_replay_n(n, gap=1.25, name="lineup12"):
    """n 个人站一排的假回放。"""
    os.makedirs(WORK, exist_ok=True)
    p = os.path.join(WORK, name + ".txt")
    W, H = int((n * gap + 4) / 0.5), 10
    rows = ["".join("0" if (2 <= x < W - 2 and 2 <= y < 8) else "2" for x in range(W)) for y in range(H)]
    xs = [1.9 + i * gap for i in range(n)]
    y = 2.45
    ch = " | ".join("1 %d %d 0 0 9000 0 0 0 0 0 0 0 0 255 -1 0 -1" % (x * 1000, y * 1000) for x in xs)
    dr = " | ".join("0 %d %d 0 0 0 0" % (x * 1000, y * 1000) for x in xs)
    lines = ["DART-REPLAY 2", "ENGINE dartfu-v3", "CFG MODE=1 STEPS=800 CELL=500", "SEED 0", "MODE 1 ffa", "MAPNAME " + name,
             "MAP %d %d" % (W, H)] + rows + ["WAVE"] + ["." * W] * H + ["WAVES 0", "PLAYERS %d" % n]
    lines += ["PLAYER %d %d p%d" % (i, i, i) for i in range(n)]
    lines += ["S 0 | %s | %s | 0 -1" % (ch, dr), "END 0 -1 0 time"]
    open(p, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    return p, xs, y


def make_lineup_voxel():
    """像素小人 12 位的阵容图：两排各 6 人的正面 + 每人一张卡片（立绘头像、主色、三行说明、四人局全景里的真实大小）。"""
    import voxchars
    keys = cast.ORDER12
    rp_path, xs, y = lineup_replay_n(len(keys), gap=1.6)
    run_prep(rp_path)
    roster = ",".join(keys)
    ex = ["--nodecor", "--roster", roster]
    F = HUD.font
    Wd, Hd = 1920, 1080
    cv = Image.new("RGBA", (Wd, Hd), (250, 247, 240, 255))
    d = ImageDraw.Draw(cv)
    d.text((60, 30), "角色阵容 · 像素小人", font=F(44, "Bold"), fill=HUD.INK)
    d.text((60 + d.textlength("角色阵容 · 像素小人", font=F(44, "Bold")) + 22, 50),
           "正赛 12 位：方块堆出来的 Q 版小人，主色、发型、标志物对应各自的立绘", font=F(20, "Medium"), fill=HUD.SUB)
    d.text((60, 92), "场上不放常驻名字：全景里靠头顶的颜色、发型的体量和头上的标志物认人；4v4 时外面多一圈队伍色。"
                     "每张卡片右边是四人局全景里的真实大小（1:1 裁切）。", font=F(16, "Regular"), fill=HUD.SUB)
    per = 6
    sh, ch_h = 340, 148
    dist, pitch, lens, tz = 19.6, 14.0, 70.0, 0.52
    for r in range(2):
        sel = list(range(r * per, (r + 1) * per))
        cx = (xs[sel[0]] + xs[sel[-1]]) / 2
        cam = (cx, -y, dist, pitch, 0.0, lens, tz)
        a = blender_still(rp_path, "lineup_vox%d" % r, 0, "%g,%g,%g,%g,%g,%g,%g" % cam, extra=ex + ["--sun", "235,40"])
        img = Image.open(grade(a, os.path.join(WORK, "_g_lineup_vox%d.png" % r))).convert("RGBA")
        b = blender_still(rp_path, "lineup_vox_top%d" % r, 0, "%g,%g,%g,%g" % (cx, -y, 29.6, 58.0), extra=ex)
        top = Image.open(grade(b, os.path.join(WORK, "_g_lineup_vox_top%d.png" % r))).convert("RGBA")
        y0 = 124 + r * (sh + ch_h - 12)
        _, fy = proj((cx, -y, 0.0), cam[0], cam[1], cam[6], cam[2], cam[3], cam[5])
        band = img.crop((0, int(fy - sh + 44), Wd, int(fy + 44)))
        m = Image.new("L", band.size, 0)
        ImageDraw.Draw(m).rounded_rectangle((24, 0, Wd - 25, sh - 1), 22, fill=255)
        cv.paste(band, (0, y0), m)
        for j, i in enumerate(sel):
            key = keys[i]
            px, _ = proj((xs[i], -y, 0.0), cam[0], cam[1], cam[6], cam[2], cam[3], cam[5])
            cw = 296
            x0, yc = int(px - cw / 2), y0 + sh - 20
            HUD.panel(cv, (x0, yc, x0 + cw, yc + ch_h), 16, blur=8, alpha=40)
            d = ImageDraw.Draw(cv)
            col = HUD.rgb(voxchars.color(key))
            d.rounded_rectangle((x0, yc, x0 + cw, yc + 7), 3, fill=col)
            av = 46
            d.ellipse((x0 + 9, yc + 14, x0 + 14 + av, yc + 19 + av), fill=col)
            cv.alpha_composite(HUD.avatar(key, av), (x0 + 12, yc + 17))
            d = ImageDraw.Draw(cv)
            d.text((x0 + 12 + av + 9, yc + 13), voxchars.label(key), font=F(23, "Bold"), fill=HUD.INK)
            cname, lines = voxchars.notes(key)
            chip = "主色 " + cname
            tw = d.textlength(chip, font=F(13, "Medium"))
            c0 = x0 + 12 + av + 9
            d.rounded_rectangle((c0, yc + 44, c0 + tw + 14, yc + 63), 9, fill=col)
            lum = 0.299 * col[0] + 0.587 * col[1] + 0.114 * col[2]
            d.text((c0 + 7, yc + 45), chip, font=F(13, "Medium"), fill=(255, 255, 255) if lum < 170 else HUD.INK)
            for k_, ln in enumerate(lines):
                d.text((x0 + 13, yc + 74 + k_ * 22), ln, font=F(13, "Regular"), fill=(88, 80, 72))
            # 全景里的真实大小
            qx, qy = proj((xs[i], -y, 0.5), cx, -y, 0.0, 29.6, 58.0, 50.0)
            s_ = 92
            crop = top.crop((int(qx - s_ / 2), int(qy - s_ / 2 - 8), int(qx + s_ / 2), int(qy + s_ / 2 - 8)))
            mk = Image.new("L", crop.size, 0)
            ImageDraw.Draw(mk).rounded_rectangle((0, 0, s_ - 1, s_ - 1), 14, fill=255)
            cv.paste(crop, (x0 + cw - s_ - 9, yc + 14), mk)
    os.makedirs(OUT, exist_ok=True)
    out = os.path.join(OUT, "角色阵容_像素.png")
    cv.convert("RGB").save(out)
    print("->", out, flush=True)


def make_compare_chars():
    """旧角色 → 像素角色：同一帧全景、同一帧近景并排。左边直接用已有的旧定妆照（同一份回放、同一帧）。"""
    F = HUD.font
    pairs = [("定妆_1_四人局全景", "四人局全景"), ("定妆_3_近景_终结一击", "近景 · 终结一击前一瞬")]
    tw, th = 1120, 630
    pad, head = 40, 110
    Wd = pad * 3 + tw * 2
    Hd = head + (th + 56) * len(pairs) + pad
    cv = Image.new("RGB", (Wd, Hd), (250, 247, 240))
    d = ImageDraw.Draw(cv)
    d.text((pad, 30), "角色：旧  →  像素小人", font=F(44, "Bold"), fill=HUD.INK)
    d.text((pad + 520, 48), "同一份回放、同一帧、同一机位；场景没有动。左：圆润小人（旧）    右：像素小人（新，默认）",
           font=F(22, "Medium"), fill=HUD.SUB)
    yy = head
    for nm, lab in pairs:
        for i, p in enumerate((os.path.join(OUT, nm + ".png"), os.path.join(OUT, nm + "_像素.png"))):
            im = Image.open(p).convert("RGB").resize((tw, th), Image.LANCZOS)
            x = pad + i * (tw + pad)
            cv.paste(im, (x, yy))
            d.text((x, yy + th + 10), ("旧 · " if i == 0 else "像素 · ") + lab, font=F(24, "Bold"), fill=HUD.INK)
        yy += th + 56
    out = os.path.join(OUT, "对比_角色_旧_新.png")
    cv.save(out)
    print("->", out, flush=True)


def map_still(name, samples=48, hud=False, out_png=None, theme=None):
    """按地图文件（没有对局）出一张全景：角色站在出生点。theme 给了就强制用这个主题。"""
    import mapfile
    if theme:
        os.environ["DART_THEME"] = theme
    else:
        os.environ.pop("DART_THEME", None)
    cached = os.path.join(WORK, "_g_map_%s.png" % name)
    if not out_png and not theme and "--reuse" in sys.argv and os.path.exists(cached):
        return cached
    rp_path, m = mapfile.to_replay(os.path.join(MAPS_DIR, name + ".txt"), os.path.join(WORK, "map_%s.txt" % name))
    run_prep(rp_path)
    png = blender_still(rp_path, "map_" + name + ("_" + theme if theme else ""), 0, samples=samples)
    g = grade(png, os.path.join(WORK, "_g_map_%s.png" % name))
    if out_png:
        with_hud(png, rp_path, 0, out_png, hud=hud)
        return out_png
    return g


def make_theme_stills():
    """十张定稿地图各按自己的主题出一张全景。"""
    import finalize_maps as FM
    for (nm, cn, form, public, theme) in FM.FINAL:
        make_one_theme(nm, samples=int(os.environ.get("STILL_SAMPLES", "64")))


def theme_png(name):
    import finalize_maps as FM
    import themes
    for (nm, cn, form, public, theme) in FM.FINAL:
        if nm == name:
            return os.path.join(OUT, "主题_%s_%s.png" % (themes.get(theme)["title"], cn))
    raise SystemExit("没有这张图：" + name)


def make_one_theme(name, samples=64):
    """一张定稿地图按它的主题出一张带信息栏的全景，同时刷新总览用的缓存图。"""
    out = theme_png(name)
    map_still(name, samples=samples, hud=True, out_png=out)
    Image.open(os.path.join(WORK, "_g_map_%s.png" % name)).convert("RGB").save(os.path.join(WORK, "_g_map_%s.png" % name))
    return out


def make_maps_overview():
    """十图总览：每张定稿地图按各自主题渲一张全景，拼成一张。"""
    import finalize_maps as FM
    import themes
    F = HUD.font
    tw, th = 912, 513
    pad, head, lab = 28, 120, 64
    cols = 2
    rows6 = [r for r in FM.FINAL if "4v4" not in r[2]]
    rows4 = [r for r in FM.FINAL if "4v4" in r[2]]
    order = rows6 + rows4
    Wd = pad + (tw + pad) * 4
    nrow = 3
    Hd = head + (th + lab + pad) * nrow + 10
    cv = Image.new("RGB", (Wd, Hd), (250, 247, 240))
    d = ImageDraw.Draw(cv)
    d.text((pad, 26), "定稿 10 张地图 · 各自的主题", font=F(46, "Bold"), fill=HUD.INK)
    d.text((pad + 700, 46), "细格子（0.5 米一格）· 角色站在出生点 · 每张图一个主题：地面、障碍物、场外远景、天色各不相同",
           font=F(21, "Medium"), fill=HUD.SUB)
    for i, (name, cn, form, public, theme) in enumerate(order):
        img = Image.open(map_still(name)).convert("RGB").resize((tw, th), Image.LANCZOS)
        x = pad + (i % 4) * (tw + pad)
        y = head + (i // 4) * (th + lab + pad)
        cv.paste(img, (x, y))
        t = themes.get(theme)
        d.text((x, y + th + 8), "%s %s" % (cn, name), font=F(28, "Bold"), fill=HUD.INK)
        wn = d.textlength("%s %s" % (cn, name), font=F(28, "Bold"))
        d.text((x + wn + 16, y + th + 16), "%s · %s · %s" % (t["title"], form, "公开" if public else "不公开"),
               font=F(20, "Medium"), fill=HUD.SUB)
    cv.save(os.path.join(MAPS_DIR, "总览_定稿十图.png"))
    print("->", os.path.join(MAPS_DIR, "总览_定稿十图.png"), flush=True)


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    which = [a for a in sys.argv[1:] if not a.startswith("--")] or ["ffa", "team", "team2", "closeup", "win", "collapse", "lineup"]
    if "ffa" in which:
        run_prep(FFA)
        with_hud(blender_still(FFA, "ffa" + SUF, 204.0), FFA, 204.0, os.path.join(OUT, "定妆_1_四人局全景%s.png" % SUF))
    if "team" in which:
        run_prep(TEAM)
        with_hud(blender_still(TEAM, "team" + SUF, 177.0), TEAM, 177.0, os.path.join(OUT, "定妆_2_4v4全景%s.png" % SUF))
    if "team2" in which:
        run_prep(TEAM2)
        with_hud(blender_still(TEAM2, "team2" + SUF, 52.0), TEAM2, 52.0, os.path.join(OUT, "定妆_2b_4v4全景_林地%s.png" % SUF))
    if "closeup" in which:              # 终结一击：成片在这一帧自己的机位（压低、拉近）
        run_prep(FFA)
        f = frame_at(FFA, "final")
        with_hud(blender_still(FFA, "closeup" + SUF, None, f=f), FFA, None,
                 os.path.join(OUT, "定妆_3_近景_终结一击%s.png" % SUF), f=f)
        f = frame_at(TEAM, "final")
        with_hud(blender_still(TEAM, "closeup_t" + SUF, None, f=f), TEAM, None,
                 os.path.join(OUT, "定妆_3b_近景_4v4终结%s.png" % SUF), f=f)
    if "cut" in which:                  # 像素小人：镖命中、切成两半的那一下（成片在这一帧自己的机位）
        run_prep(FFA)
        f = frame_at(FFA, "cut")
        with_hud(blender_still(FFA, "cut" + SUF, None, f=f), FFA, None, os.path.join(OUT, "定妆_3c_近景_切成两半%s.png" % SUF), f=f)
    if "win" in which:                  # 结算镜头
        run_prep(FFA)
        f = frame_at(FFA, "win")
        with_hud(blender_still(FFA, "win" + SUF, None, f=f), FFA, None, os.path.join(OUT, "定妆_5_结算镜头%s.png" % SUF), f=f)
    if "collapse" in which:
        run_prep(TEAM)
        with_hud(blender_still(TEAM, "collapse" + SUF, 273.0), TEAM, 273.0, os.path.join(OUT, "定妆_4_塌陷瞬间%s.png" % SUF))
    if "lineup" in which:
        make_lineup_voxel() if SUF else make_lineup()
    if "compare_chars" in which:
        make_compare_chars()
    if "compare" in which:
        make_compare()
    if "themes" in which:
        make_theme_stills()
    if "maps" in which:
        make_maps_overview()
    for w in which:                              # one:<地图名>[,<地图名>...]：只出这几张图的主题全景
        if w.startswith("one:"):
            for nm in w[4:].split(","):
                make_one_theme(nm, samples=int(os.environ.get("STILL_SAMPLES", "64")))


if __name__ == "__main__":
    main()

# -*- coding: utf-8 -*-
"""体素小人联系表：每人一行 = 新立绘缩略图 | 正面 | 斜 45（普通 Python + Pillow）：
    python voxsheet.py <预览目录> <输出.jpg> [key1,key2,...]"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

ART = os.environ.get("DART_PORTRAITS") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "art", "v2", "portraits")
ARTNAME = {"opus": "Opus", "fable": "Fable", "deepseek": "DeepSeek", "qwen": "Qwen", "glm": "GLM", "grok": "Grok",
           "gemini": "Gemini", "kimi": "Kimi", "mimo": "MiMo", "ernie": "Ernie"}
ARTPATH = {"gemini_s": os.path.join(os.path.dirname(ART), "portraits_small", "Gemini_小.png")}   # 小版立绘另放
ORDER = ["opus", "fable", "deepseek", "qwen", "glm", "grok", "gemini", "gemini_s", "kimi", "mimo"]
H = int(os.environ.get("SHEET_H", "420"))
BG = (200, 200, 204)


def fit(im, h):
    bb = im.getbbox()
    if bb:
        im = im.crop(bb)
    w = max(1, int(im.width * h / im.height))
    return im.resize((w, h), Image.LANCZOS)


def main():
    src, out = sys.argv[1], sys.argv[2]
    keys = sys.argv[3].split(",") if len(sys.argv) > 3 else ORDER
    rows = []
    for k in keys:
        cells = []
        ap = ARTPATH.get(k) or (os.path.join(ART, ARTNAME[k] + ".png") if k in ARTNAME else None)
        if ap:
            a = Image.open(ap).convert("RGB")
            cells.append(a.resize((int(a.width * H / a.height), H), Image.LANCZOS))
        for v in ("正面", "斜45"):
            p = os.path.join(src, "%s_%s.png" % (k, v))
            if os.path.exists(p):
                im = fit(Image.open(p).convert("RGBA"), H - 20)
                bg = Image.new("RGBA", (int(H * 0.9), H), BG + (255,))
                bg.alpha_composite(im, ((bg.width - im.width) // 2, 10))
                cells.append(bg.convert("RGB"))
        rows.append((k, cells))
    ncol = int(os.environ.get("SHEET_COLS", "1"))
    nrow = (len(rows) + ncol - 1) // ncol
    W = max(sum(c.width for c in cs) + 6 * len(cs) for _, cs in rows) + 10
    sheet = Image.new("RGB", (W * ncol, (H + 4) * nrow), (90, 90, 96))
    d = ImageDraw.Draw(sheet)
    try:
        font = ImageFont.truetype("msyh.ttc", 22)
    except Exception:
        font = None
    for i, (k, cs) in enumerate(rows):
        x, y = (i // nrow) * W, (i % nrow) * (H + 4)
        for c in cs:
            sheet.paste(c, (x, y))
            x += c.width + 6
        d.text((x - 6 - int(H * 0.9) * 2 + 8, y + 6), k, fill=(20, 20, 20), font=font)
    sheet.save(out, quality=88)
    print(out, sheet.size)


if __name__ == "__main__":
    main()

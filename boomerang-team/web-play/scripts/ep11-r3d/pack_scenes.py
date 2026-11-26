# -*- coding: utf-8 -*-
"""第 11–12 期 3D 场景打包：把 Blender 导出的网格包（export_scene.py / export_scene12.py 的产物）
整理成网站按图加载的三件套，放进 scripts/data/ep11_r3d/scenes/：

  <图>.json   场景描述（地图、调色板、灯光、材质表、物体表；地面贴图文件名）
  <图>.bin    网格（原样，br_bins.mjs 再压成 .bin.br 入库）
  <图>.webp   正片画好的地面贴图（ground.png 原尺寸转 WebP）
  <图>_glow.webp  第 12 集火山口：地面裂缝发光强度（灰度）

用法（渲染器 venv 的 Python，只读原始目录）：
  python scripts/ep11-r3d/pack_scenes.py <原始目录> [图名 ...]
"""
import json
import os
import shutil
import sys
from pathlib import Path

from PIL import Image

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "data" / "ep11_r3d" / "scenes"
RAW = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("_scene_raw")


def pack(name):
    meta = json.loads((RAW / (name + ".json")).read_text(encoding="utf-8"))
    OUT.mkdir(parents=True, exist_ok=True)
    g = meta.pop("ground", None)
    im = Image.open(g).convert("RGB")
    im.save(OUT / (name + ".webp"), "WEBP", quality=88, method=6)
    meta["tex"] = name + ".webp"
    glow = meta.pop("ground_glow", None)
    if glow and os.path.exists(glow):
        gi = Image.open(glow).convert("L")
        gi = gi.resize((gi.width // 2, gi.height // 2), Image.LANCZOS)
        gi.convert("RGB").save(OUT / (name + "_glow.webp"), "WEBP", quality=90, method=6)
        meta["glow"] = name + "_glow.webp"
    shutil.copyfile(RAW / (name + ".bin"), OUT / (name + ".bin"))
    (OUT / (name + ".json")).write_text(json.dumps(meta, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    print("%-12s 网格 %5d KB  贴图 %4d KB%s" % (name, os.path.getsize(OUT / (name + ".bin")) // 1024,
                                           os.path.getsize(OUT / (name + ".webp")) // 1024,
                                           "  发光 %d KB" % (os.path.getsize(OUT / (name + "_glow.webp")) // 1024) if "glow" in meta else ""))


names = sys.argv[2:] or sorted(p.stem for p in RAW.glob("*.json"))
for n in names:
    pack(n)

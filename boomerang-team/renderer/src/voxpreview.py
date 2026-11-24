# -*- coding: utf-8 -*-
"""只出体素小人的正面 / 斜 45 预览（不导出 glb、json），给改造型时对照立绘用（Blender 里跑）：
    blender.exe -b --factory-startup -P voxpreview.py -- <输出目录> [key1,key2,...]
环境变量 VOX_SAMPLES（默认 24）、VOX_RES（默认 640）。拼联系表用 voxsheet.py。"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bpy  # noqa: E402

import chars_voxel as CV  # noqa: E402
import export_voxchars as EX  # noqa: E402
import voxchars  # noqa: E402


def main():
    a = sys.argv[sys.argv.index("--") + 1:]
    out = a[0]
    keys = a[1].split(",") if len(a) > 1 else voxchars.KEYS
    os.makedirs(out, exist_ok=True)
    res = int(os.environ.get("VOX_RES", "640"))
    for key in keys:
        sc = EX.new_scene()
        sc.render.resolution_x = sc.render.resolution_y = res
        sc.eevee.taa_render_samples = int(os.environ.get("VOX_SAMPLES", "24"))
        coll = bpy.data.collections.new("c")
        sc.collection.children.link(coll)
        rig = CV.VoxRig(key, coll, key)
        rig.apply(dict(EX.REST))
        for ob in (rig.dart, rig.blob, rig.ring):
            ob.hide_render = True
        objs = list(rig.parts.values())
        EX.shot(sc, objs, 0.0, os.path.join(out, key + "_正面.png"))
        EX.shot(sc, objs, 45.0, os.path.join(out, key + "_斜45.png"))
        print("PREVIEW", key, flush=True)


if __name__ == "__main__":
    main()

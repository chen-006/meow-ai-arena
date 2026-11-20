# -*- coding: utf-8 -*-
"""把体素小人导出成不依赖本集场景的素材（Blender 里跑）：
    blender.exe -b --factory-startup -P export_voxchars.py -- <输出目录> [key1,key2,...]
每人：<key>.glb（各部件是独立节点、带顶点色，枢轴在关节上）、<key>.json（方块数据，voxkit.from_json 可读回）、
<key>_正面.png、<key>_斜45.png（透明底，带描边）。"""
import json
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import bpy  # noqa: E402
import numpy as np  # noqa: E402
from mathutils import Vector  # noqa: E402

import chars as CH  # noqa: E402
import chars_voxel as CV  # noqa: E402
import scene_lib as SL  # noqa: E402
import voxchars  # noqa: E402
import voxkit as VK  # noqa: E402
from geom import MeshBuilder  # noqa: E402

REST = dict(x=0.0, y=0.0, z=0.0, heading=90.0, lean=0.0, roll=0.0, spin=0.0, sx=1.0, sy=1.0, sz=1.0, body_dz=0.0, twist=0.0,
            head=(0.0, 0.0, 0.0), head_dz=0.0, hand_a=(0.0, 0.25, 0.29), hand_b=(0.0, -0.25, 0.29),
            foot_a=(0.0, 0.085, 0.04), foot_b=(0.0, -0.085, 0.04), hair=(0.0, 0.0), face="open", tint=(1.0, 1.0, 1.0),
            flash=0.0, ground=0.0, ring=0.0, visible=True, hop=0.0)


def new_scene():
    SL.clear_scene()
    SL._MATS.clear()
    sc = bpy.context.scene
    sc.render.engine = 'BLENDER_EEVEE'
    sc.render.film_transparent = True
    sc.render.resolution_x = 1024
    sc.render.resolution_y = 1024
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    sc.view_settings.view_transform = 'Standard'
    sc.eevee.taa_render_samples = 48
    SL.setup_world(strength=0.75)
    SL.add_sun(azimuth_deg=235.0, elevation_deg=48.0, energy=4.2, color=(1.0, 0.95, 0.88), angle_deg=5)
    return sc


def shot(sc, objs, yaw_deg, path):
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    bpy.context.view_layer.update()
    for ob in objs:
        if ob.type != 'MESH' or ob.hide_render:
            continue
        for v in ob.data.vertices:
            w = ob.matrix_world @ v.co
            lo = Vector((min(lo.x, w.x), min(lo.y, w.y), min(lo.z, w.z)))
            hi = Vector((max(hi.x, w.x), max(hi.y, w.y), max(hi.z, w.z)))
    c = (lo + hi) / 2
    cd = bpy.data.cameras.new("cam")
    cd.type = 'ORTHO'
    cd.ortho_scale = max(hi.z - lo.z, (hi - lo).length * 0.82) * 1.12
    cam = bpy.data.objects.new("cam", cd)
    sc.collection.objects.link(cam)
    sc.camera = cam
    yaw, pitch = math.radians(yaw_deg), math.radians(12.0)
    d = Vector((math.sin(yaw) * math.cos(pitch), -math.cos(yaw) * math.cos(pitch), math.sin(pitch)))
    cam.location = c + d * 8.0
    cam.rotation_euler = (-d).to_track_quat('-Z', 'Y').to_euler()
    sc.render.filepath = path
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(cam)


def export_glb(key, path):
    """干净的版本：没有描边，每个部件一个节点，顶点色 = 颜色。"""
    SL.clear_scene()
    SL._MATS.clear()
    M = voxchars.build(key)
    mat = bpy.data.materials.new("vox_" + key)
    nb = SL.NB(mat)
    p = nb.principled(nb.out(nb.attr('col'), 'Color'), rough=0.92, spec=0.04)
    nb.output(p)
    root = bpy.data.objects.new(key, None)
    bpy.context.scene.collection.objects.link(root)
    obs = {}
    for nm, part in M.parts.items():
        d = part.mesh()
        mb = MeshBuilder()
        mb.add(d["V"], d["F"], 0, vcolors=d["C"], smooth=False)
        ob = mb.build(nm, [mat], None)
        try:
            ob.data.color_attributes.active_color = ob.data.color_attributes["col"]
            ob.data.color_attributes.render_color_index = 0
        except Exception:
            pass
        obs[nm] = ob
    for nm, ob in obs.items():
        piv = np.array(M.parts[nm].pivot) * VK.U
        par, ref = root, None
        if nm in ("hairback", "tailL", "tailR", "face_open", "face_tight", "face_happy"):
            par, ref = obs["head"], "head"
        elif nm in ("armA", "armB", "tail"):
            par, ref = obs["body"], "body"
        ob.parent = par
        ob.location = tuple(piv - (np.array(M.parts[ref].pivot) * VK.U if ref else 0))
        if nm in ("face_tight", "face_happy"):
            ob.hide_viewport = True
            ob.hide_render = True
    kw = dict(filepath=path, export_format='GLB', export_yup=True, export_apply=False, use_visible=False)
    try:
        bpy.ops.export_scene.gltf(export_vertex_color='ACTIVE', export_all_vertex_colors=True, **kw)
    except TypeError:
        try:
            bpy.ops.export_scene.gltf(export_vertex_color='ACTIVE', **kw)
        except TypeError:
            bpy.ops.export_scene.gltf(**kw)


def main():
    a = sys.argv[sys.argv.index("--") + 1:]
    out = a[0]
    keys = a[1].split(",") if len(a) > 1 else voxchars.KEYS
    os.makedirs(out, exist_ok=True)
    for key in keys:
        M = voxchars.build(key)
        json.dump(M.to_json(), open(os.path.join(out, key + ".json"), "w", encoding="utf-8"), ensure_ascii=False)
        sc = new_scene()
        coll = bpy.data.collections.new("c")
        sc.collection.children.link(coll)
        rig = CV.VoxRig(key, coll, key)
        rig.apply(dict(REST))
        for ob in (rig.dart, rig.blob, rig.ring):
            ob.hide_render = True
        objs = list(rig.parts.values())
        shot(sc, objs, 0.0, os.path.join(out, key + "_正面.png"))
        shot(sc, objs, 45.0, os.path.join(out, key + "_斜45.png"))
        export_glb(key, os.path.join(out, key + ".glb"))
        print("EXPORTED", key, flush=True)


if __name__ == "__main__":
    main()

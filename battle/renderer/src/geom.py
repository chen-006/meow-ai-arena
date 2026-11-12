# -*- coding: utf-8 -*-
"""纯 numpy 的小网格工具（在 Blender 里用）：基本体、车削、拼合，最后一次性建成 Blender 网格。"""
import math

import numpy as np

_ICO_CACHE = {}


def icosphere(subdiv=2):
    if subdiv in _ICO_CACHE:
        v, f = _ICO_CACHE[subdiv]
        return v.copy(), f.copy()
    t = (1.0 + 5 ** 0.5) / 2.0
    v = [(-1, t, 0), (1, t, 0), (-1, -t, 0), (1, -t, 0), (0, -1, t), (0, 1, t), (0, -1, -t), (0, 1, -t),
         (t, 0, -1), (t, 0, 1), (-t, 0, -1), (-t, 0, 1)]
    f = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2),
         (10, 7, 6), (7, 1, 8), (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5),
         (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    verts = [np.array(p, dtype=np.float64) / np.linalg.norm(p) for p in v]
    faces = list(f)
    for _ in range(subdiv):
        cache = {}
        nf = []

        def mid(a, b):
            key = (min(a, b), max(a, b))
            if key in cache:
                return cache[key]
            p = verts[a] + verts[b]
            verts.append(p / np.linalg.norm(p))
            cache[key] = len(verts) - 1
            return cache[key]

        for a, b, c in faces:
            ab, bc, ca = mid(a, b), mid(b, c), mid(c, a)
            nf += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = nf
    V = np.array(verts, dtype=np.float64)
    F = np.array(faces, dtype=np.int64)
    _ICO_CACHE[subdiv] = (V, F)
    return V.copy(), F.copy()


def uv_sphere(seg=24, rings=14):
    verts = [(0, 0, 1)]
    for i in range(1, rings):
        th = math.pi * i / rings
        for j in range(seg):
            ph = 2 * math.pi * j / seg
            verts.append((math.sin(th) * math.cos(ph), math.sin(th) * math.sin(ph), math.cos(th)))
    verts.append((0, 0, -1))
    faces = []
    for j in range(seg):
        faces.append((0, 1 + j, 1 + (j + 1) % seg))
    for i in range(rings - 2):
        a0 = 1 + i * seg
        b0 = 1 + (i + 1) * seg
        for j in range(seg):
            j2 = (j + 1) % seg
            faces.append((a0 + j, b0 + j, b0 + j2, a0 + j2))
    last = len(verts) - 1
    a0 = 1 + (rings - 2) * seg
    for j in range(seg):
        faces.append((a0 + j, last, a0 + (j + 1) % seg))
    return np.array(verts, dtype=np.float64), faces


def lathe(profile, seg=24, close_top=True, close_bottom=True):
    """profile: [(r, z), ...] 从下到上。绕 Z 轴旋转。"""
    verts = []
    n = len(profile)
    for j in range(seg):
        ph = 2 * math.pi * j / seg
        c, s = math.cos(ph), math.sin(ph)
        for r, z in profile:
            verts.append((r * c, r * s, z))
    faces = []
    for j in range(seg):
        j2 = (j + 1) % seg
        for i in range(n - 1):
            faces.append((j * n + i, j2 * n + i, j2 * n + i + 1, j * n + i + 1))
    if close_bottom and profile[0][0] > 1e-6:
        verts.append((0, 0, profile[0][1]))
        ci = len(verts) - 1
        for j in range(seg):
            j2 = (j + 1) % seg
            faces.append((j2 * n, j * n, ci))
    if close_top and profile[-1][0] > 1e-6:
        verts.append((0, 0, profile[-1][1]))
        ci = len(verts) - 1
        for j in range(seg):
            j2 = (j + 1) % seg
            faces.append((j * n + n - 1, j2 * n + n - 1, ci))
    return np.array(verts, dtype=np.float64), faces


def cone(r0, r1, h, seg=12, z0=0.0):
    return lathe([(r0, z0), (r1, z0 + h)], seg=seg)


def rot_matrix(rx=0.0, ry=0.0, rz=0.0):
    cx, sx = math.cos(rx), math.sin(rx)
    cy, sy = math.cos(ry), math.sin(ry)
    cz, sz = math.cos(rz), math.sin(rz)
    Rx = np.array([[1, 0, 0], [0, cx, -sx], [0, sx, cx]])
    Ry = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]])
    Rz = np.array([[cz, -sz, 0], [sz, cz, 0], [0, 0, 1]])
    return Rz @ Ry @ Rx


def transform(V, scale=(1, 1, 1), rot=(0, 0, 0), loc=(0, 0, 0), R=None):
    V = np.asarray(V, dtype=np.float64) * np.asarray(scale, dtype=np.float64)
    if R is None:
        R = rot_matrix(*rot)
    V = V @ R.T
    return V + np.asarray(loc, dtype=np.float64)


class MeshBuilder:
    """攒顶点、面、材质号、颜色属性，最后建一个 Blender 物体。"""

    def __init__(self):
        self.V = []
        self.F = []
        self.M = []
        self.C = []  # 每个顶点的 RGBA（可选）
        self.n = 0
        self.smooth = []

    def add(self, V, faces, mat=0, color=(1, 1, 1, 1), smooth=True, vcolors=None):
        V = np.asarray(V, dtype=np.float64)
        self.V.append(V)
        if vcolors is None:
            self.C.append(np.tile(np.asarray(color, dtype=np.float64), (len(V), 1)))
        else:
            self.C.append(np.asarray(vcolors, dtype=np.float64))
        for f in faces:
            self.F.append(tuple(int(i) + self.n for i in f))
            self.M.append(mat)
            self.smooth.append(smooth)
        self.n += len(V)
        return self

    def build(self, name, materials, collection=None, sharp_angle=None):
        import bpy
        V = np.concatenate(self.V) if self.V else np.zeros((0, 3))
        C = np.concatenate(self.C) if self.C else np.zeros((0, 4))
        me = bpy.data.meshes.new(name)
        me.from_pydata(V.tolist(), [], self.F)
        me.update()
        for m in materials:
            me.materials.append(m)
        if len(me.polygons):
            me.polygons.foreach_set("material_index", self.M)
            me.polygons.foreach_set("use_smooth", self.smooth)
        if len(C):
            attr = me.color_attributes.new("col", 'FLOAT_COLOR', 'POINT')
            attr.data.foreach_set("color", C.astype(np.float32).ravel())
        if sharp_angle is not None:
            try:
                me.set_sharp_from_angle(angle=sharp_angle)
            except Exception:
                pass
        ob = bpy.data.objects.new(name, me)
        (collection or bpy.context.scene.collection).objects.link(ob)
        return ob

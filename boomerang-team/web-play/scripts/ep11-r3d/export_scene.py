# -*- coding: utf-8 -*-
"""网页查看器的场景：让 Blender 无界面跑正片渲染器自己的搭景代码（env.build_env），把搭好的场景导出成网格包。

  blender -b --factory-startup -P export_scene.py -- <输出目录> [图名 ...]

正片渲染器 src\\ 只读引用；地图用渲染器的 maps\\<图名>.txt（mapfile.to_replay 转成只有开局状态的回放，和出静帧同一条路）。
地形缓存（prep.py 的产物）用渲染器的 ENV.CACHE（环境变量 DART_CACHE，默认 renderer\\work\\cache）。

每张图输出 <图名>.json + <图名>.bin（未压缩，pack_scenes.py 再压缩、配地面贴图）：
  json：地图尺寸、调色板 PAL、灯光、材质表、物体表、地面贴图路径和覆盖范围
  bin：每个物体依次 位置 i16×3（乘 q 还原，Blender 坐标 X、Y、Z）、法线 i8×4、颜色 u8×4（sRGB；地形的 r 通道 = 柱面随机量）、
       索引 u16 或 u32（顶点数 < 65536 用 u16）
  物体 kind：state（第 k 波塌之前的岛体）、piece（塌陷碎块，顶点相对块心；块上的石头、点缀也并在里面）、
           grass（一格高草，顶点相对格心，可能挂在某个碎块上）、cloud（云海，会飘）、static（其余所有静态物，按材质合并）
"""
import json
import math
import os
import sys
import time

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
R11 = os.environ.get("EP11_RENDERER", os.path.abspath(os.path.join(HERE, "..", "..", "..", "..", "boomerang", "renderer")))
SRC = os.path.join(R11, "src")
MAPDIR = os.path.join(R11, "maps")
sys.path.insert(0, SRC)

import bpy  # noqa: E402
import numpy as np  # noqa: E402

import env as ENV  # noqa: E402
import mapfile  # noqa: E402
import replay  # noqa: E402
import scene_lib as SL  # noqa: E402

import copy  # noqa: E402
PAL0 = copy.deepcopy(SL.PAL)
ROCK0 = copy.deepcopy(ENV.ROCK)
MAPS = ["twinlakes", "yinyang", "stonecircle", "skybridge", "ruins", "icefloes", "hourglass", "canyon", "oldtown", "forest"]
# 减面目标（三角形数）
STATE_TRIS = int(os.environ.get("EXP_STATE_TRIS", "18000"))
PIECE_DENS = float(os.environ.get("EXP_PIECE_DENS", "0.07"))     # 碎块按原面数的比例
BIG_TRIS = int(os.environ.get("EXP_BIG_TRIS", "5000"))           # 其余单个物体超过这么多面就减


def lin2srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)


def mat_info(m):
    """材质 -> 网页用的种类和参数。"""
    if m is None:
        return dict(name="none", kind="vcol", emis=0.0, haze=False)
    nm = m.name.split(".")[0]
    nodes = m.node_tree.nodes if m.node_tree else []
    types = {n.bl_idname for n in nodes}
    kind = "vcol"
    if nm == "M_terrain":
        kind = "terrain" if "ShaderNodeTexImage" in types else "terrain_proc"
        nm = "M_terrain" if kind == "terrain" else "M_terrain_proc"
    elif nm == "M_cloud":
        kind = "cloud"
    elif nm == "M_water":
        kind = "water"
    elif "ShaderNodeBsdfTransparent" in types:
        kind = "alpha"
    emis = 0.0
    rough = 0.85
    for n in nodes:
        if n.bl_idname == "ShaderNodeBsdfPrincipled":
            rough = float(n.inputs['Roughness'].default_value)
            ec = n.inputs.get('Emission Color')
            if ec is not None and ec.is_linked:
                emis = float(n.inputs['Emission Strength'].default_value)
        if n.bl_idname == "ShaderNodeEmission":
            emis = float(n.inputs['Strength'].default_value)
    haze = kind == "vcol" and "ShaderNodeSeparateXYZ" in types
    return dict(name=nm, kind=kind, emis=emis, haze=bool(haze), rough=rough)


def decimated(ob, ratio):
    """加一个减面修改器，取求值后的网格（不改原物体以外的东西）。"""
    if ratio < 0.999:
        md = ob.modifiers.new("dec", 'DECIMATE')
        md.decimate_type = 'COLLAPSE'
        md.ratio = max(0.002, ratio)
        md.use_collapse_triangulate = True
    dg = bpy.context.evaluated_depsgraph_get()
    dg.update()
    ev = ob.evaluated_get(dg)
    me = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=True, depsgraph=dg)
    if ratio < 0.999:
        ob.modifiers.remove(ob.modifiers["dec"])
    return me


def ntris(ob):
    me = ob.data
    me.calc_loop_triangles()
    return len(me.loop_triangles)


def arrays(me, M=None, facet=False):
    """网格 -> (位置 N×3, 法线 N×3, 颜色 N×4 线性, 三角形 T×3, 每个三角形的材质名)。
    按 (顶点, 法线) 去重：平滑面共用顶点，硬边各自一份。M：4×4 变换（默认不变）。"""
    me.calc_loop_triangles()
    nl = len(me.loops)
    nv = len(me.vertices)
    vidx = np.zeros(nl, np.int64)
    me.loops.foreach_get("vertex_index", vidx)
    co = np.zeros(nv * 3)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    cn = np.zeros(nl * 3)
    me.corner_normals.foreach_get("vector", cn)
    cn = cn.reshape(-1, 3)
    if facet:
        col = np.zeros((nv, 4))
        at = me.attributes.get("facet")
        if at is not None:
            tmp = np.zeros(nv)
            at.data.foreach_get("value", tmp)
            col[:, 0] = tmp * 0.5 + 0.5
        at = me.attributes.get("edge_s")
        if at is not None:
            tmp = np.zeros(nv)
            at.data.foreach_get("value", tmp)
            col[:, 1] = np.clip(tmp / 2.0 + 0.5, 0, 1)
        at = me.attributes.get("depth")
        if at is not None:
            tmp = np.zeros(nv)
            at.data.foreach_get("value", tmp)
            col[:, 2] = np.clip(tmp / 12.0, 0, 1)
        col_l = col[vidx]
    else:
        col_l = np.ones((nl, 4))
        ca = me.color_attributes.get("col")
        if ca is not None:
            if ca.domain == 'POINT':
                tmp = np.zeros(nv * 4)
                ca.data.foreach_get("color", tmp)
                col_l = tmp.reshape(-1, 4)[vidx]
            else:
                tmp = np.zeros(nl * 4)
                ca.data.foreach_get("color", tmp)
                col_l = tmp.reshape(-1, 4)
    P = co[vidx]
    N = cn
    if M is not None:
        M = np.array(M)
        P = P @ M[:3, :3].T + M[:3, 3]
        N = N @ np.linalg.inv(M[:3, :3])
        N /= np.linalg.norm(N, axis=1, keepdims=True) + 1e-12
    nt = len(me.loop_triangles)
    tri = np.zeros(nt * 3, np.int64)
    me.loop_triangles.foreach_get("loops", tri)
    tri = tri.reshape(-1, 3)
    mi = np.zeros(nt, np.int64)
    me.loop_triangles.foreach_get("material_index", mi)
    names = [(mat_info(m)["name"]) for m in me.materials] or ["none"]
    mnames = [names[min(i, len(names) - 1)] for i in mi]
    # 去重
    qn = np.round(N * 64).astype(np.int64)
    key = np.stack([vidx, qn[:, 0], qn[:, 1], qn[:, 2]], 1)
    if not facet:
        qc = np.round(col_l * 255).astype(np.int64)
        key = np.concatenate([key, qc], 1)
    _, first, inv = np.unique(key, axis=0, return_index=True, return_inverse=True)
    inv = inv.ravel()
    return P[first], N[first], col_l[first], inv[tri], mnames


class Packer:
    def __init__(self):
        self.objs = []
        self.chunks = []
        self.mats = {}

    def mat_id(self, m):
        info = mat_info(m)
        if info["name"] not in self.mats:
            info["id"] = len(self.mats)
            self.mats[info["name"]] = info
        return self.mats[info["name"]]["id"]

    def add(self, parts, **meta):
        """parts：[(P, N, C, tri, mnames)]，合并成一个物体，三角形按材质排好分组。"""
        if not parts:
            return
        Ps, Ns, Cs, Ts, Ms = [], [], [], [], []
        off = 0
        for P, N, C, T, mn in parts:
            Ps.append(P); Ns.append(N); Cs.append(C); Ts.append(T + off); Ms += mn
            off += len(P)
        P = np.concatenate(Ps); N = np.concatenate(Ns); C = np.concatenate(Cs); T = np.concatenate(Ts)
        if len(T) == 0:
            return
        order = sorted(range(len(T)), key=lambda i: Ms[i])
        T = T[order]
        Ms = [Ms[i] for i in order]
        groups = []
        for i, m in enumerate(Ms):
            if not groups or groups[-1][0] != m:
                groups.append([m, i * 3, 0])
            groups[-1][2] += 3
        q = max(0.004 if meta.get("kind") in ("state", "piece_terrain", "cloud", "static", "decor_terrain") else 0.002, float(np.abs(P).max()) / 32000.0)
        pq = np.round(P / q).astype(np.int16)
        nq = np.zeros((len(N), 4), np.int8)
        nq[:, :3] = np.round(np.clip(N, -1, 1) * 127).astype(np.int8)
        if meta.get("kind") in ("state", "piece_terrain", "decor_terrain"):
            cq = np.round(np.clip(C, 0, 1) * 255).astype(np.uint8)
        else:
            cq = np.round(lin2srgb(C) * 255).astype(np.uint8)
            cq[:, 3] = np.round(np.clip(C[:, 3], 0, 1) * 255).astype(np.uint8)
        idx = T.ravel()
        big = len(P) >= 65536
        ib = idx.astype(np.uint32 if big else np.uint16).tobytes()
        if len(ib) % 4:
            ib += b"\0\0"
        # 压缩友好的排布：位置、法线、颜色都按分量分开存（位置再做差分），索引做差分（按 16 / 32 位回绕）
        pd = np.diff(pq.astype(np.int32), axis=0, prepend=0).astype(np.int16)
        idd = np.diff(idx.astype(np.int64), prepend=0)
        idd = (idd % (1 << 32)).astype(np.uint32) if big else (idd % 65536).astype(np.uint16)
        ib = idd.tobytes()
        if len(ib) % 4:
            ib += b"\0\0"
        pb = pd.T.copy().tobytes()
        if len(pb) % 4:
            pb += b"\0\0"
        self.chunks.append(pb + nq.T.copy().tobytes() + cq.T.copy().tobytes() + ib)
        self.objs.append(dict(nv=len(P), ni=len(idx), q=q, i32=big, groups=[[self.mats[g[0]]["id"] if g[0] in self.mats else 0, g[1], g[2]] for g in groups], **meta))


def mesh_parts(ob, M, ratio=1.0, facet=False, pk=None):
    for m in ob.data.materials:
        pk.mat_id(m)
    me = decimated(ob, ratio)
    for m in me.materials:
        pk.mat_id(m)
    r = arrays(me, M, facet=facet)
    bpy.data.meshes.remove(me)
    return r


def export_map(name, out_dir, tmp_dir):
    t0 = time.time()
    rpath, m = mapfile.to_replay(os.path.join(MAPDIR, name + ".txt"), os.path.join(tmp_dir, name + "_map.txt"))
    rp = replay.load(rpath)
    SL.clear_scene()          # 和 render_bl.Stage 一样，每张图从空场景开始（不清的话上一张图的东西会混进来）
    # 正片每次出图是新进程：调色板、石头样式从默认值开始再叠主题。这里一个进程导出多张图，要先还原，免得上一张图的主题配色漏过来
    SL.PAL.clear(); SL.PAL.update(copy.deepcopy(PAL0))
    ENV.ROCK.clear(); ENV.ROCK.update(copy.deepcopy(ROCK0))
    SL._MATS.clear()
    env = ENV.build_env(rp)
    ENV.std_light()
    t1 = time.time()
    pk = Packer()
    done = set()
    I4 = np.eye(4)
    # 岛体各状态
    for k, ob in enumerate(env.states):
        if ob is None:
            continue
        n = ntris(ob)
        P, N, C, T, mn = mesh_parts(ob, I4, min(1.0, STATE_TRIS / max(1, n)), facet=True, pk=pk)
        pk.add([(P, N, C, T, mn)], kind="state", k=k)
        done.add(ob.name)
    # 塌陷碎块：地形一个物体，块上的东西（石头、点缀）另一个物体，都相对块心；高草格单独
    grass_obs = {g[0].name: g for g in env.grass}
    for i, p in enumerate(env.pieces):
        ob = p["ob"]
        if ob is None:
            continue
        cx, cy = p["cx"], -p["cy"]
        Mloc = np.eye(4)                     # 碎块网格本来就相对块心
        n = ntris(ob)
        P, N, C, T, mn = mesh_parts(ob, Mloc, min(1.0, max(PIECE_DENS, 600.0 / max(1, n))), facet=True, pk=pk)
        meta = dict(id=p["id"], wave=p["wave"], cx=p["cx"], cy=p["cy"], d_in=p["d_in"], rnd=p["rnd"])
        pk.add([(P, N, C, T, mn)], kind="piece_terrain", piece=i, **meta)
        done.add(ob.name)
        kids = []
        for ch in ob.children:
            if ch.name in grass_obs or ch.type != 'MESH':
                continue
            Mw = np.array(ch.matrix_world)
            Mw[0, 3] -= cx
            Mw[1, 3] -= cy
            kids.append(mesh_parts(ch, Mw, 1.0, pk=pk))
            done.add(ch.name)
        pk.add(kids, kind="piece_deco", piece=i)
    piece_of = {p["ob"].name: i for i, p in enumerate(env.pieces) if p["ob"] is not None}
    for g in env.grass:
        ob, gx, gy, ph = g[0], g[1], g[2], g[3]
        par = piece_of.get(ob.parent.name) if ob.parent is not None else None
        P, N, C, T, mn = mesh_parts(ob, I4, 1.0, pk=pk)
        pk.add([(P, N, C, T, mn)], kind="grass", gx=gx, gy=gy, ph=ph, piece=par)
        done.add(ob.name)
    for ob, v in env.clouds:
        n = ntris(ob)
        P, N, C, T, mn = mesh_parts(ob, np.array(ob.matrix_world), min(1.0, 12000 / max(1, n)), pk=pk)
        pk.add([(P, N, C, T, mn)], kind="cloud", speed=v)
        done.add(ob.name)
    if env.fall is not None:
        done.add(env.fall.ob.name)
    # 其余静态物：按材质合并
    stat = []
    dterr = []
    budget = {"decor_leaves": 36000, "decor_flowers": 16000}
    cand = []
    for ob in bpy.context.scene.objects:
        if ob.type not in ('MESH', 'CURVE') or ob.name in done or ob.name.startswith("Cube"):
            continue
        if ob.type == 'MESH' and not len(ob.data.polygons):
            continue
        try:
            n = ntris(ob) if ob.type == 'MESH' else 0
        except Exception:
            n = 0
        is_t = ob.type == 'MESH' and any(mm is not None and mm.name.startswith("M_terrain") for mm in ob.data.materials)
        cand.append((ob, n, is_t))
    # 场外的东西按总面数封顶：场外岛 6 万、其余（树、花、房子……）9 万
    tot_t = sum(n for _, n, t in cand if t) or 1
    tot_s = sum(n for _, n, t in cand if not t) or 1
    for ob, n, is_t in cand:
        lim = budget.get(ob.name.split(".")[0], BIG_TRIS)
        if is_t:
            r = min(1.0, 60000.0 / tot_t)
        else:
            r = min(1.0, lim / n) if n > lim else 1.0
            r = min(r, max(0.15, 90000.0 / tot_s)) if n > 400 else r
        if os.environ.get("EXP_LIST"):
            print("STATIC", ob.name, ob.type, n, [mm.name for mm in ob.data.materials] if ob.type == 'MESH' else '', flush=True)
        if is_t:
            dterr.append(mesh_parts(ob, np.array(ob.matrix_world), r, facet=True, pk=pk))
        else:
            stat.append(mesh_parts(ob, np.array(ob.matrix_world), r, pk=pk))
    pk.add(stat, kind="static")
    pk.add(dterr, kind="decor_terrain")
    th = ENV.THEME
    L = th.get("light", {})
    pal = {k: v for k, v in SL.PAL.items()}
    meta = dict(map=name, title=m.get("title"), theme=rp.theme, W=rp.W, H=rp.H, cell=rp.cell, Wm=rp.Wm, Hm=rp.Hm,
                grid="".join("".join(map(str, r)) for r in rp.grid),
                wave="".join("".join(("." if c < 0 else "0123456789abcdefghijklmnopqrstuvwxyz"[c]) for c in r) for r in rp.wave),
                pal=pal, light=dict(L), ambient=th.get("ambient"), decor=th.get("decor"),
                ground=os.path.join(ENV.CACHE, rp.map_key(), "ground.png"),
                uv_rect=[-0.6, -(rp.Hm + 0.6), rp.Wm + 1.2, rp.Hm + 1.2],
                mats=sorted(pk.mats.values(), key=lambda d: d["id"]), objects=pk.objs)
    os.makedirs(out_dir, exist_ok=True)
    open(os.path.join(out_dir, name + ".bin"), "wb").write(b"".join(pk.chunks))
    open(os.path.join(out_dir, name + ".json"), "w", encoding="utf-8").write(json.dumps(meta, ensure_ascii=False, default=lambda o: o.tolist() if hasattr(o, "tolist") else str(o)))
    kinds = {}
    for o in pk.objs:
        a = kinds.setdefault(o["kind"], [0, 0, 0])
        a[0] += 1; a[1] += o["nv"]; a[2] += o["ni"] // 3
    print("MAP %s build %.1fs export %.1fs  %d KB  %s" % (name, t1 - t0, time.time() - t1, sum(len(c) for c in pk.chunks) // 1024,
                                                     json.dumps(kinds)), flush=True)


def main():
    a = sys.argv[sys.argv.index("--") + 1:]
    out = a[0]
    names = a[1:] or MAPS
    tmp = os.path.join(out, "_tmp")
    os.makedirs(tmp, exist_ok=True)
    for n in names:
        export_map(n, out, tmp)
    open(os.path.join(out, "DONE_" + "_".join(names)[:60] + ".txt"), "w").write("ok")


main()

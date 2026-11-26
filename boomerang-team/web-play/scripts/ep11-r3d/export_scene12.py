# -*- coding: utf-8 -*-
"""第 12 集三张 5v5 图（火山口 caldera、三座栈桥 piers、双堡 keeps）的网页场景导出：
让 Blender 无界面跑第 12 集正片渲染器自己的搭景代码（env.build_env），导出成和第 11 集同格式的网格包。

  blender -b --factory-startup -P scripts/ep11-r3d/export_scene12.py -- <输出目录> [图名 ...]

做法照第 11 集网页查看器的 export_scene.py（正片渲染器 web\\export_scene.py），另加第 12 集的东西：
  - 液面：岩浆湖 / 海水（M_lava / M_sea 两种材质，网页端照 scene_lib.mat_lava / mat_sea 用 GLSL 重写），
    每组液面带"跟着哪一波塌陷没掉"（liquid_drain），岸塌了以后挂的瀑布（liqfall）；岩浆湖上的橙色点光（只记位置）
  - 火山口地面的发光裂缝贴图（ground_glow.png）
  - 双堡的队旗（单独物体，顶点色 alpha 记"离旗杆多远"，网页端让它飘）
  - 队伍色：双堡的旗、幡、塔顶原来按"左边红、右边蓝"烤死在网格里；网站的口径是 Astra 队永远蓝、Fable 队永远红（见 src/games/boomerang/teams.ts），
    换边回合左边可能是 Fable 队。这里把 props12.team_cols 换成四个占位色（左旗、左顶、右旗、右顶），网页端按这一局两队的颜色换回去。
正片渲染器 src\\ 和地形缓存 _work\\cache 只读；回放样例\\<图>.txt 只取地图。
"""
import copy
import json
import math
import os
import sys
import time

sys.dont_write_bytecode = True
HERE = os.path.dirname(os.path.abspath(__file__))
R12 = os.environ.get("EP12_RENDERER", os.path.abspath(os.path.join(HERE, "..", "..", "..", "renderer")))
REPLAY_DIR = os.environ.get("EP12_REPLAYS", os.path.abspath(os.path.join(HERE, "..", "..", "..", "replays")))
SAMPLE_REPLAY = {"caldera": 'R1_g113_astra_win_median.txt', "piers": 'R3_g0_astra_win_median.txt', "keeps": 'R5_g102_fable_win_median.txt'}
SRC = os.path.join(R12, "src")
sys.path.insert(0, SRC)

import bpy  # noqa: E402
import numpy as np  # noqa: E402

import env as ENV  # noqa: E402
import liquid as LQ  # noqa: E402
import liquid_drain as LD  # noqa: E402
import props12 as P12  # noqa: E402
import replay  # noqa: E402
import scene_lib as SL  # noqa: E402

PAL0 = copy.deepcopy(SL.PAL)
ROCK0 = copy.deepcopy(ENV.ROCK)
MAPS = ["caldera", "piers", "keeps"]
TITLE = {"caldera": "火山口", "piers": "三座栈桥", "keeps": "双堡"}
STATE_TRIS = int(os.environ.get("EXP_STATE_TRIS", "20000"))
PIECE_DENS = float(os.environ.get("EXP_PIECE_DENS", "0.07"))
BIG_TRIS = int(os.environ.get("EXP_BIG_TRIS", "5000"))

# 队伍色占位（线性）：左旗、左顶、右旗、右顶。g≈0 = 左边；r≈0 = 右边；另两个分量之比 1 = 旗 / 幡，0.5 = 塔顶
SENT = {"flagL": (0.75, 0.0, 0.75, 1.0), "roofL": (0.75, 0.0, 0.375, 1.0),
        "flagR": (0.0, 0.75, 0.75, 1.0), "roofR": (0.0, 0.375, 0.75, 1.0)}


def _team_cols(rp, bbox):
    left = (bbox[0] + bbox[2]) / 2 < rp.W / 2
    if left:
        return np.array(SENT["flagL"]), np.array(SENT["roofL"]), left
    return np.array(SENT["flagR"]), np.array(SENT["roofR"]), left


P12.team_cols = _team_cols


def lin2srgb(c):
    c = np.clip(c, 0, 1)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)


def mat_info(m):
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
    elif nm == "M_lava":
        kind = "lava"
    elif nm == "M_sea":
        kind = "sea"
    elif "ShaderNodeBsdfTransparent" in types:
        kind = "alpha"
    emis = 0.0
    rough = 0.85
    for n in nodes:
        if n.bl_idname == "ShaderNodeBsdfPrincipled":
            rough = float(n.inputs['Roughness'].default_value)
            ec = n.inputs.get('Emission Color')
            if ec is not None and ec.is_linked and kind == "vcol":
                emis = float(n.inputs['Emission Strength'].default_value)
        if n.bl_idname == "ShaderNodeEmission":
            emis = float(n.inputs['Strength'].default_value)
    haze = kind == "vcol" and "ShaderNodeSeparateXYZ" in types
    return dict(name=nm, kind=kind, emis=emis, haze=bool(haze), rough=rough)


def decimated(ob, ratio):
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


def arrays(me, M=None, facet=False, alpha_u=None):
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
        for nm_, ch, f in (("facet", 0, lambda t: t * 0.5 + 0.5), ("edge_s", 1, lambda t: np.clip(t / 2.0 + 0.5, 0, 1)),
                           ("depth", 2, lambda t: np.clip(t / 12.0, 0, 1))):
            at = me.attributes.get(nm_)
            if at is not None:
                tmp = np.zeros(nv)
                at.data.foreach_get("value", tmp)
                col[:, ch] = f(tmp)
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
    if alpha_u is not None:
        col_l = col_l.copy()
        col_l[:, 3] = alpha_u[vidx]
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
        q = max(0.004 if meta.get("kind") in ("state", "piece_terrain", "cloud", "static", "decor_terrain") else 0.002,
                float(np.abs(P).max()) / 32000.0)
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
        self.objs.append(dict(nv=len(P), ni=len(idx), q=q, i32=big,
                              groups=[[self.mats[g[0]]["id"] if g[0] in self.mats else 0, g[1], g[2]] for g in groups], **meta))


def mesh_parts(ob, M, ratio=1.0, facet=False, pk=None, alpha_u=None):
    for m in ob.data.materials:
        pk.mat_id(m)
    me = decimated(ob, ratio)
    for m in me.materials:
        pk.mat_id(m)
    r = arrays(me, M, facet=facet, alpha_u=alpha_u)
    bpy.data.meshes.remove(me)
    return r


def flag_u(ob):
    """旗面顶点离旗杆多远（0..1）：旗面只沿 x 展开，旗杆在 x 最小或最大的一侧。"""
    co = np.zeros(len(ob.data.vertices) * 3)
    ob.data.vertices.foreach_get("co", co)
    x = co.reshape(-1, 3)[:, 0]
    for f in P12.FLAGS:
        if f[0] == ob:
            return np.array(f[2], dtype=float)
    a, b = x.min(), x.max()
    return np.clip((x - a) / max(1e-6, b - a), 0, 1)


def export_map(name, out_dir):
    t0 = time.time()
    rp = replay.load(os.path.join(REPLAY_DIR, SAMPLE_REPLAY.get(name, name + ".txt")))
    SL.clear_scene()
    SL.PAL.clear(); SL.PAL.update(copy.deepcopy(PAL0))
    ENV.ROCK.clear(); ENV.ROCK.update(copy.deepcopy(ROCK0))
    SL._MATS.clear()
    del SL._TIMED[:]
    env = ENV.build_env(rp)
    ENV.std_light()
    t1 = time.time()
    pk = Packer()
    done = set()
    I4 = np.eye(4)
    flag_obs = {f[0].name for f in P12.FLAGS}
    for k, ob in enumerate(env.states):
        if ob is None:
            continue
        n = ntris(ob)
        P, N, C, T, mn = mesh_parts(ob, I4, min(1.0, STATE_TRIS / max(1, n)), facet=True, pk=pk)
        pk.add([(P, N, C, T, mn)], kind="state", k=k)
        done.add(ob.name)
    grass_obs = {g[0].name: g for g in env.grass}
    liquid_obs = {L["ob"].name for L in env.liquids}
    for i, p in enumerate(env.pieces):
        ob = p["ob"]
        if ob is None:
            continue
        cx, cy = p["cx"], -p["cy"]
        n = ntris(ob)
        P, N, C, T, mn = mesh_parts(ob, np.eye(4), min(1.0, max(PIECE_DENS, 600.0 / max(1, n))), facet=True, pk=pk)
        meta = dict(id=p["id"], wave=p["wave"], cx=p["cx"], cy=p["cy"], d_in=p["d_in"], rnd=p["rnd"])
        pk.add([(P, N, C, T, mn)], kind="piece_terrain", piece=i, **meta)
        done.add(ob.name)
        kids = []
        for ch in ob.children:
            if ch.name in grass_obs or ch.type != 'MESH' or ch.name in liquid_obs:
                continue
            Mw = np.array(ch.matrix_world)
            Mw[0, 3] -= cx
            Mw[1, 3] -= cy
            if ch.name in flag_obs:
                Pf = mesh_parts(ch, Mw, 1.0, pk=pk, alpha_u=flag_u(ch))
                pk.add([Pf], kind="flag", piece=i)
            else:
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
        for f in getattr(env.fall, "falls", [env.fall]):
            done.add(f.ob.name)
    # 液面（岩浆 / 海水）、岸塌以后的瀑布、岩浆点光
    lights = []
    for L in env.liquids:
        ob = L["ob"]
        done.add(ob.name)
        w = L.get("w")
        if L["kind"] == "light":
            lights.append(dict(x=ob.location.x, y=-ob.location.y, z=ob.location.z, w=-1 if w is None else int(w)))
            continue
        P, N, C, T, mn = mesh_parts(ob, np.array(ob.matrix_world), 1.0, pk=pk)
        if L["kind"] == "sheet":
            pk.add([(P, N, C, T, mn)], kind="liquid", w=-1 if w is None else int(w), z=L["z"])
        else:
            wf = L.get("w_from")
            pk.add([(P, N, C, T, mn)], kind="liqfall", w=-1 if w is None else int(w), w_from=-1 if wf is None else int(wf))
    # 不挂在碎块上的旗
    for f in P12.FLAGS:
        ob = f[0]
        if ob.name in done:
            continue
        Pf = mesh_parts(ob, np.array(ob.matrix_world), 1.0, pk=pk, alpha_u=np.array(f[2], dtype=float))
        pk.add([Pf], kind="flag", piece=None)
        done.add(ob.name)
    stat, dterr, cand = [], [], []
    budget = {"decor_leaves": 36000, "decor_flowers": 16000}
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
    lq = LQ.spec(rp)
    cells = sorted(LQ.cells(rp)) if lq else []
    d = os.path.join(ENV.CACHE, rp.map_key())
    glow = os.path.join(d, "ground_glow.png")
    meta = dict(map=name, title=TITLE.get(name), theme=rp.theme, W=rp.W, H=rp.H, cell=rp.cell, Wm=rp.Wm, Hm=rp.Hm,
                grid="".join("".join(map(str, r)) for r in rp.grid),
                wave="".join("".join(("." if c < 0 else "0123456789abcdefghijklmnopqrstuvwxyz"[c]) for c in r) for r in rp.wave),
                pal=pal, light=dict(L), ambient=th.get("ambient"), decor=th.get("decor"),
                ground=os.path.join(d, "ground.png"),
                uv_rect=[-0.6, -(rp.Hm + 0.6), rp.Wm + 1.2, rp.Hm + 1.2],
                liquid=lq, liquid_cells=[[x, y] for (x, y) in cells], lights=lights,
                team_sentinel=bool(P12.FLAGS) or name == "keeps",
                mats=sorted(pk.mats.values(), key=lambda dd: dd["id"]), objects=pk.objs)
    if os.path.exists(glow) and th.get("gstyle", {}).get("ash"):
        meta["ground_glow"] = glow
    os.makedirs(out_dir, exist_ok=True)
    open(os.path.join(out_dir, name + ".bin"), "wb").write(b"".join(pk.chunks))
    open(os.path.join(out_dir, name + ".json"), "w", encoding="utf-8").write(
        json.dumps(meta, ensure_ascii=False, default=lambda o: o.tolist() if hasattr(o, "tolist") else str(o)))
    kinds = {}
    for o in pk.objs:
        a = kinds.setdefault(o["kind"], [0, 0, 0])
        a[0] += 1; a[1] += o["nv"]; a[2] += o["ni"] // 3
    print("MAP %s build %.1fs export %.1fs  %d KB  %s  mats=%s" % (name, t1 - t0, time.time() - t1, sum(len(c) for c in pk.chunks) // 1024,
                                                              json.dumps(kinds), [m["name"] + ":" + m["kind"] for m in meta["mats"]]), flush=True)


def main():
    a = sys.argv[sys.argv.index("--") + 1:]
    out = a[0]
    names = a[1:] or MAPS
    for n in names:
        export_map(n, out)
    open(os.path.join(out, "DONE_" + "_".join(names)[:60] + ".txt"), "w").write("ok")


main()
